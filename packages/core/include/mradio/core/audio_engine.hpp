#pragma once

#include "mradio/core/error.hpp"
#include "mradio/core/player_state.hpp"
#include "mradio/core/track_info.hpp"
#include "mradio/core/volume.hpp"

#include <string>
#include <string_view>

namespace mradio::core {

// Everything observable about playback, delivered as one value.
//
// It is a whole snapshot rather than a set of getters so that a listener can
// never catch the engine half-updated, and so the MPRIS layer has a single
// object to diff against its previous copy when deciding what to put in
// PropertiesChanged.
struct PlaybackStatus {
    PlayerState state = PlayerState::idle;
    std::string url;  // empty when idle
    TrackInfo track;
    Volume volume;
    bool muted = false;
    Error error;  // meaningful only when state == PlayerState::failed

    friend bool operator==(const PlaybackStatus&, const PlaybackStatus&) = default;
};

class IAudioEngineListener {
public:
    virtual ~IAudioEngineListener() = default;

    // Called after any change to the snapshot.
    //
    // Threading contract: this runs on the engine's own event thread, not on
    // the D-Bus loop. Implementations must either be thread-safe or hand the
    // snapshot over to their own loop and return immediately; blocking here
    // stalls playback event handling.
    virtual void on_status_changed(const PlaybackStatus& status) = 0;
};

// The audio backend, as the rest of the app is allowed to see it.
//
// It deals in URLs, not stations: mapping a station to its URL belongs to the
// layer that owns the playlist, which keeps this interface trivially fakeable
// in tests.
class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;

    IAudioEngine(const IAudioEngine&) = delete;
    IAudioEngine& operator=(const IAudioEngine&) = delete;

    // Starts a new stream, replacing whatever was playing. Returning success
    // means the request was accepted, not that audio is flowing: the stream is
    // still connecting, and a failure surfaces later through the listener.
    virtual Status play(std::string_view url) = 0;

    virtual void stop() = 0;
    virtual void set_volume(Volume volume) = 0;
    virtual void set_muted(bool muted) = 0;

    [[nodiscard]] virtual PlaybackStatus status() const = 0;

    // Pass nullptr to detach. The listener must outlive the engine, or be
    // detached before it is destroyed.
    virtual void set_listener(IAudioEngineListener* listener) = 0;

protected:
    IAudioEngine() = default;
};

}  // namespace mradio::core
