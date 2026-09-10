#pragma once

#include "mradio/core/audio_engine.hpp"
#include "mradio/core/error.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

struct mpv_handle;

namespace mradio::audio {

struct MpvOptions {
    // mpv's --ao. Empty lets mpv choose; tests pass "null" so they can run on a
    // machine with no sound hardware.
    std::string audio_output;

    // How long mpv waits on a stalled network read before giving up.
    int network_timeout_seconds = 15;
};

// IAudioEngine implemented on libmpv.
//
// mpv runs threads of its own; this class adds one more, which sits in
// mpv_wait_event and turns mpv's property changes into PlaybackStatus
// snapshots. Every public method here is safe to call from any thread.
class MpvEngine final : public core::IAudioEngine {
public:
    // Creates and initialises the mpv handle. Fails if libmpv cannot be
    // brought up at all, which in practice means a broken installation.
    static core::Result<std::unique_ptr<MpvEngine>> create(MpvOptions options = {});

    ~MpvEngine() override;

    core::Status play(std::string_view url) override;
    void stop() override;
    void set_volume(core::Volume volume) override;
    void set_muted(bool muted) override;

    [[nodiscard]] core::PlaybackStatus status() const override;

    // Must not be called from inside on_status_changed: the callback runs with
    // the listener lock held, and re-entering here would deadlock.
    void set_listener(core::IAudioEngineListener* listener) override;

private:
    explicit MpvEngine(mpv_handle* handle);

    void run_event_loop();
    void handle_property_change(void* event_data);
    void handle_end_file(void* event_data);

    // Builds a snapshot and hands it to the listener. Must be called with no
    // lock held.
    void republish();

    // Derives the player state from the raw mpv flags. Caller must hold mutex_.
    [[nodiscard]] core::PlayerState derive_state() const;

    // Snapshot of the current state. Caller must hold mutex_.
    [[nodiscard]] core::PlaybackStatus snapshot() const;

    mpv_handle* handle_ = nullptr;
    std::thread event_thread_;
    std::atomic<bool> stopping_{false};

    mutable std::mutex mutex_;
    core::PlaybackStatus status_;

    // Raw mpv properties. State is derived from them rather than stored,
    // so a stale combination cannot linger after an event is missed.
    bool idle_active_ = true;
    bool core_idle_ = true;

    // Sticky until the next play(): when a stream errors out mpv also goes
    // idle, and without this the failure would immediately read as "idle".
    bool failed_ = false;
    core::Error last_error_;

    std::mutex listener_mutex_;
    core::IAudioEngineListener* listener_ = nullptr;
};

}  // namespace mradio::audio
