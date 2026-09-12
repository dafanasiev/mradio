#include "mradio/mpris/service.hpp"

#include "mradio/ipc/bus.hpp"
#include "mradio/mpris/track_list_model.hpp"

#include "mpris_adaptor.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mradio::mpris {
namespace {

constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";

using PlayerAdaptor = org::mpris::MediaPlayer2::Player_adaptor;
using TrackListAdaptor = org::mpris::MediaPlayer2::TrackList_adaptor;

// The reserved path, as the type the adaptors want it in.
sdbus::ObjectPath no_track()
{
    return sdbus::ObjectPath{std::string{TrackListModel::no_track()}};
}

// A view onto the player, in MPRIS terms.
//
// What is playing it reads from the ViewState the view model publishes, so it
// never reasons about playback internals. The station list it does read
// directly: the track list has to describe stations nobody is listening to,
// and a ViewState is only ever about the one that is on.
class MprisService final
    : public Service,
      public sdbus::AdaptorInterfaces<org::mpris::MediaPlayer2_adaptor,
                                      PlayerAdaptor,
                                      TrackListAdaptor,
                                      sdbus::Properties_adaptor>,
      public vm::IViewStateListener {
public:
    MprisService(sdbus::IConnection& connection,
                 vm::PlayerViewModel& view_model,
                 std::function<void()> quit)
        : AdaptorInterfaces(connection, sdbus::ObjectPath{kObjectPath}),
          view_model_(view_model),
          quit_(std::move(quit)),
          tracks_(view_model.stations())
    {
        registerAdaptor();
        view_model_.add_listener(this);
    }

    ~MprisService() override
    {
        view_model_.remove_listener(this);
        unregisterAdaptor();
    }

    void on_view_state_changed(const vm::ViewState& state) override
    {
        // Asked before anything is emitted, because it is what remembers the
        // station and the song this state was seen with.
        const std::optional<sdbus::ObjectPath> changed = track_if_changed(state);

        // Position and CanControl are declared EmitsChangedSignal="false".
        // Naming either of them here makes sd-bus reject the entire batch, so
        // the valid properties would be dropped along with them.
        try {
            emitPropertiesChangedSignal(PlayerAdaptor::INTERFACE_NAME,
                                        {sdbus::PropertyName{"PlaybackStatus"},
                                         sdbus::PropertyName{"Metadata"},
                                         sdbus::PropertyName{"Volume"}});

            // mpris:trackid names the station now, so it no longer moves when
            // the stream moves on to the next song. This is what says so to a
            // client following the track list.
            if (changed && state.station.has_value()) {
                emitTrackMetadataChanged(*changed, metadata_for(*state.station, state));
            }
        }
        catch (const sdbus::Error&) {
            // A dead bus must not take playback down with it.
        }
    }

private:
    // ---- org.mpris.MediaPlayer2 ----

    void Raise() override {}  // there is no window to raise

    void Quit() override
    {
        if (quit_) {
            quit_();
        }
    }

    bool CanQuit() override { return true; }
    bool CanRaise() override { return false; }
    bool HasTrackList() override { return true; }

    std::string Identity() override { return "mradio"; }
    std::string DesktopEntry() override { return "mradio"; }

    // Empty: this player only plays what is in the user's playlist, so it
    // advertises no ability to open arbitrary URIs.
    std::vector<std::string> SupportedUriSchemes() override { return {}; }
    std::vector<std::string> SupportedMimeTypes() override { return {}; }

    // ---- org.mpris.MediaPlayer2.Player ----

    void Next() override { view_model_.next(); }
    void Previous() override { view_model_.previous(); }

    void Pause() override {}  // CanPause is false; a live stream cannot pause

    void PlayPause() override { view_model_.toggle_play_stop(); }

    void Stop() override { view_model_.stop(); }

    void Play() override
    {
        // Play must never stop something that is already on, so it cannot just
        // forward to the toggle.
        if (!view_model_.view_state().is_playing) {
            view_model_.toggle_play_stop();
        }
    }

    // Seeking has no meaning on a live stream, and CanSeek says so.
    void Seek(const int64_t& /*offset*/) override {}
    void SetPosition(const sdbus::ObjectPath& /*track*/, const int64_t& /*position*/) override {}
    void OpenUri(const std::string& /*uri*/) override {}

    std::string PlaybackStatus() override
    {
        // Never "Paused": connecting counts as playing, because from the
        // user's side the station is on and the tray already says so.
        return view_model_.view_state().is_playing ? "Playing" : "Stopped";
    }

    double Rate() override { return 1.0; }
    void Rate(const double& /*value*/) override {}
    double MinimumRate() override { return 1.0; }
    double MaximumRate() override { return 1.0; }
    int64_t Position() override { return 0; }

    std::map<std::string, sdbus::Variant> Metadata() override
    {
        const vm::ViewState state = view_model_.view_state();

        if (!state.station.has_value()) {
            return no_track_metadata();
        }

        return metadata_for(*state.station, state);
    }

    double Volume() override { return view_model_.view_state().volume.normalised(); }

    void Volume(const double& value) override
    {
        view_model_.set_volume(core::Volume{value});
    }

    bool CanGoNext() override { return !view_model_.stations().empty(); }
    bool CanGoPrevious() override { return !view_model_.stations().empty(); }
    bool CanPlay() override { return !view_model_.stations().empty(); }
    bool CanPause() override { return false; }
    bool CanSeek() override { return false; }
    bool CanControl() override { return true; }

    // ---- org.mpris.MediaPlayer2.TrackList ----

    std::vector<sdbus::ObjectPath> Tracks() override
    {
        const std::vector<std::string> ids = tracks_.track_ids();

        std::vector<sdbus::ObjectPath> paths;
        paths.reserve(ids.size());
        for (const std::string& id : ids) {
            paths.emplace_back(id);
        }

        return paths;
    }

    // The playlist is a file the user edits; a client cannot add to it.
    bool CanEditTracks() override { return false; }

    std::vector<std::map<std::string, sdbus::Variant>> GetTracksMetadata(
        const std::vector<sdbus::ObjectPath>& track_ids) override
    {
        const vm::ViewState state = view_model_.view_state();

        std::vector<std::map<std::string, sdbus::Variant>> metadata;
        metadata.reserve(track_ids.size());

        // An id this player never handed out is left out of the answer
        // instead of answered with an empty map: the signature has no way to
        // say "no such track", and a{sv} without an mpris:trackid in it would
        // be a worse answer than a shorter array.
        for (const sdbus::ObjectPath& track_id : track_ids) {
            if (const auto station = tracks_.station_of(track_id); station) {
                metadata.push_back(metadata_for(*station, state));
            }
        }

        return metadata;
    }

    // The only way to pick a particular station over MPRIS. An id from
    // anywhere else has no effect, which is what the spec asks for.
    void GoTo(const sdbus::ObjectPath& track_id) override
    {
        if (const auto station = tracks_.station_of(track_id); station) {
            static_cast<void>(view_model_.play(*station));
        }
    }

    // CanEditTracks is false, so the spec allows these either to do nothing or
    // to raise NotSupported. They raise: silence here would be indistinguishable
    // from having worked.
    void AddTrack(const std::string& /*uri*/,
                  const sdbus::ObjectPath& /*after_track*/,
                  const bool& /*set_as_current*/) override
    {
        refuse_to_edit();
    }

    void RemoveTrack(const sdbus::ObjectPath& /*track_id*/) override { refuse_to_edit(); }

    // ---- helpers ----

    [[noreturn]] static void refuse_to_edit()
    {
        throw sdbus::Error{sdbus::Error::Name{"org.freedesktop.DBus.Error.NotSupported"},
                           "mradio reads its station list once at startup; "
                           "edit playlist.m3u and restart it instead"};
    }

    static std::map<std::string, sdbus::Variant> no_track_metadata()
    {
        std::map<std::string, sdbus::Variant> metadata;
        metadata.emplace("mpris:trackid", sdbus::Variant{no_track()});
        return metadata;
    }

    // Everything MPRIS has to say about one station, for the Player's Metadata
    // and for the track list alike.
    //
    // What the stream is playing right now belongs to the station that is
    // actually on, and only to it: the rest are described by their own name
    // and URL, which is all that is known about a station nobody is listening
    // to.
    [[nodiscard]] std::map<std::string, sdbus::Variant> metadata_for(
        const core::StationId& id, const vm::ViewState& state) const
    {
        const core::Station* const station = view_model_.stations().find(id);
        if (station == nullptr) {
            return no_track_metadata();
        }

        const std::optional<std::string> track_id = tracks_.id_of(id);

        std::map<std::string, sdbus::Variant> metadata;
        metadata.emplace("mpris:trackid",
                         sdbus::Variant{track_id ? sdbus::ObjectPath{*track_id} : no_track()});
        metadata.emplace("xesam:url", sdbus::Variant{station->url});

        // The station goes in as the album: that is the line panel applets
        // show beneath the track, which is where a listener looks for it.
        metadata.emplace("xesam:album", sdbus::Variant{station->name});

        const bool is_current = state.station == id;

        // Falls back to the station name so the applet never shows a blank
        // line while the stream has yet to send any metadata.
        metadata.emplace("xesam:title",
                         sdbus::Variant{is_current && !state.track.title.empty()
                                            ? state.track.title
                                            : station->name});

        if (is_current && !state.track.artist.empty()) {
            metadata.emplace("xesam:artist",
                             sdbus::Variant{std::vector<std::string>{state.track.artist}});
        }

        return metadata;
    }

    // The track id of the station that is on, but only when the track list has
    // something new to say about it - a different station, or a new song on the
    // one that was already playing. A volume change is not news about a track.
    [[nodiscard]] std::optional<sdbus::ObjectPath> track_if_changed(const vm::ViewState& state)
    {
        const std::lock_guard lock{track_mutex_};

        std::string song = state.track.display();
        if (state.station == last_station_ && song == last_track_) {
            return std::nullopt;
        }

        last_station_ = state.station;
        last_track_ = std::move(song);

        if (!state.station.has_value()) {
            return std::nullopt;
        }

        const std::optional<std::string> track_id = tracks_.id_of(*state.station);
        if (!track_id) {
            return std::nullopt;
        }

        return sdbus::ObjectPath{*track_id};
    }

    vm::PlayerViewModel& view_model_;
    std::function<void()> quit_;
    TrackListModel tracks_;

    std::mutex track_mutex_;
    std::optional<core::StationId> last_station_;
    std::string last_track_;
};

}  // namespace

core::Result<std::unique_ptr<Service>> Service::start(sdbus::IConnection& connection,
                                                      vm::PlayerViewModel& view_model,
                                                      std::function<void()> quit)
{
    try {
        return std::unique_ptr<Service>{
            new MprisService{connection, view_model, std::move(quit)}};
    }
    catch (const sdbus::Error& error) {
        return std::unexpected(ipc::from_sdbus(error));
    }
}

}  // namespace mradio::mpris
