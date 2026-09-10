#pragma once

#include "mradio/core/audio_engine.hpp"
#include "mradio/core/error.hpp"
#include "mradio/core/station.hpp"
#include "mradio/core/track_info.hpp"
#include "mradio/core/volume.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace mradio::vm {

// Everything a view needs in order to draw itself, already resolved.
//
// The point of resolving here is that no view has to go back to the model: the
// tray does not look a station up to label its menu, and MPRIS does not look
// one up to fill in its metadata. Both read the same fields, which is what
// keeps the two front-ends showing the same thing.
struct ViewState {
    // Which station is on, or nullopt when stopped. The tray puts its play
    // glyph on exactly this one.
    std::optional<core::StationId> station;

    std::string station_name;  // empty when stopped
    std::string station_url;   // empty when stopped

    // True from the moment a station is chosen, not from the moment audio
    // starts. The user clicked; the menu should say so immediately.
    bool is_playing = false;

    core::TrackInfo track;
    core::Volume volume;
    bool muted = false;

    std::string error;  // empty unless the last attempt failed

    friend bool operator==(const ViewState&, const ViewState&) = default;
};

class IViewStateListener {
public:
    virtual ~IViewStateListener() = default;

    // Runs on whichever thread produced the change: the audio engine's event
    // thread for playback updates, the D-Bus loop thread for a command. Be
    // quick and be thread-safe.
    virtual void on_view_state_changed(const ViewState& state) = 0;
};

// The view model: the program's behaviour and its observable state, with no
// idea who is watching.
//
// The tray menu and the MPRIS interface are both views bound to this one
// object - they issue commands to it and re-render from the ViewState it
// publishes. Nothing about D-Bus, mpv or menus appears here, which is what
// makes the behaviour testable against a fake engine.
class PlayerViewModel final : public core::IAudioEngineListener {
public:
    PlayerViewModel(core::StationList stations, core::IAudioEngine& engine);
    ~PlayerViewModel() override;

    PlayerViewModel(const PlayerViewModel&) = delete;
    PlayerViewModel& operator=(const PlayerViewModel&) = delete;

    // The playlist, fixed for the lifetime of the program: it is read once at
    // startup and nothing adds to it afterwards.
    [[nodiscard]] const core::StationList& stations() const noexcept { return stations_; }

    [[nodiscard]] ViewState view_state() const;

    // ---- commands ----

    // Fails only when the id is not in the playlist. A stream that turns out
    // to be dead surfaces later, through the view state.
    core::Status play(const core::StationId& id);

    void stop();

    // Stops if a station is on, otherwise starts the first one. With no pause
    // on a live stream, this is the only meaningful toggle, and it is what
    // MPRIS PlayPause and the play/pause media key map onto.
    void toggle_play_stop();

    // Step through the playlist, wrapping at both ends. With nothing on,
    // next() starts the first station and previous() the last.
    void next();
    void previous();

    void set_volume(core::Volume volume);

    // Shifts the volume by `delta` in normalised units - what the tray calls
    // when the wheel turns over the icon.
    void adjust_volume(double delta);

    void set_muted(bool muted);

    // ---- binding ----

    // A listener must outlive this object or remove itself first. Listeners
    // must not add or remove listeners from inside the callback.
    void add_listener(IViewStateListener* listener);
    void remove_listener(IViewStateListener* listener);

    // core::IAudioEngineListener
    void on_status_changed(const core::PlaybackStatus& status) override;

private:
    // Requires mutex_ to be held.
    [[nodiscard]] ViewState build_view_state() const;

    void publish(const ViewState& state);

    core::StationList stations_;
    core::IAudioEngine& engine_;

    mutable std::mutex mutex_;

    // The station that is on, cleared by stop(). Nothing outlives a stop:
    // there is no memory of what played before and nothing is persisted
    // between runs.
    std::optional<core::StationId> station_;

    core::PlaybackStatus playback_;

    mutable std::mutex listener_mutex_;
    std::vector<IViewStateListener*> listeners_;
};

}  // namespace mradio::vm
