#include "mradio/mpris/service.hpp"

#include "mradio/ipc/bus.hpp"

#include "mpris_adaptor.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mradio::mpris {
namespace {

constexpr const char* kObjectPath = "/org/mpris/MediaPlayer2";

// The path MPRIS reserves for "nothing is playing".
constexpr const char* kNoTrack = "/org/mpris/MediaPlayer2/TrackList/NoTrack";

using PlayerAdaptor = org::mpris::MediaPlayer2::Player_adaptor;

// A view onto the player, in MPRIS terms.
//
// It reads nothing but the ViewState the view model publishes, which is why it
// never has to look a station up or reason about playback internals.
class MprisService final
    : public Service,
      public sdbus::AdaptorInterfaces<org::mpris::MediaPlayer2_adaptor,
                                      PlayerAdaptor,
                                      sdbus::Properties_adaptor>,
      public vm::IViewStateListener {
public:
    MprisService(sdbus::IConnection& connection,
                 vm::PlayerViewModel& view_model,
                 std::function<void()> quit)
        : AdaptorInterfaces(connection, sdbus::ObjectPath{kObjectPath}),
          view_model_(view_model),
          quit_(std::move(quit))
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
        {
            const std::lock_guard lock{track_mutex_};
            std::string current = state.track.display();
            if (current != last_track_) {
                last_track_ = std::move(current);
                // Clients key off mpris:trackid to notice a new track, so it
                // has to change whenever the stream's title does.
                track_serial_.fetch_add(1);
            }
        }

        // Position and CanControl are declared EmitsChangedSignal="false".
        // Naming either of them here makes sd-bus reject the entire batch, so
        // the valid properties would be dropped along with them.
        try {
            emitPropertiesChangedSignal(PlayerAdaptor::INTERFACE_NAME,
                                        {sdbus::PropertyName{"PlaybackStatus"},
                                         sdbus::PropertyName{"Metadata"},
                                         sdbus::PropertyName{"Volume"}});
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
    bool HasTrackList() override { return false; }

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

        std::map<std::string, sdbus::Variant> metadata;

        if (!state.station.has_value()) {
            metadata.emplace("mpris:trackid", sdbus::Variant{sdbus::ObjectPath{kNoTrack}});
            return metadata;
        }

        metadata.emplace("mpris:trackid", sdbus::Variant{sdbus::ObjectPath{track_path()}});
        metadata.emplace("xesam:url", sdbus::Variant{state.station_url});

        // The station goes in as the album: that is the line panel applets
        // show beneath the track, which is where a listener looks for it.
        metadata.emplace("xesam:album", sdbus::Variant{state.station_name});

        // Falls back to the station name so the applet never shows a blank
        // line while the stream has yet to send any metadata.
        metadata.emplace("xesam:title",
                         sdbus::Variant{state.track.title.empty() ? state.station_name
                                                                  : state.track.title});

        if (!state.track.artist.empty()) {
            metadata.emplace("xesam:artist",
                             sdbus::Variant{std::vector<std::string>{state.track.artist}});
        }

        return metadata;
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

    // ---- helpers ----

    [[nodiscard]] std::string track_path() const
    {
        return std::string{"/org/mpris/MediaPlayer2/mradio/track/"}
               + std::to_string(track_serial_.load());
    }

    vm::PlayerViewModel& view_model_;
    std::function<void()> quit_;

    std::atomic<std::uint64_t> track_serial_{0};
    std::mutex track_mutex_;
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
