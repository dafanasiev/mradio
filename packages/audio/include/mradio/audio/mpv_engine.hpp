#pragma once

#include "mradio/core/audio_engine.hpp"
#include "mradio/core/error.hpp"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

struct mpv_handle;

namespace mradio::audio {

// What to do when a stream drops.
//
// ffmpeg reconnects underneath mpv on its own (see the stream-lavf-o options in
// create()); this policy covers the drops it could not paper over, which arrive
// here as an mpv end-of-file error. Retrying them matters because the usual
// cause is the network blinking or the station's server restarting, neither of
// which should cost the user their station.
struct RetryPolicy {
    // Attempts after a failure. Zero disables retrying, which is what the
    // tests want when they are checking the failure path itself.
    //
    // The budget is restored the moment audio flows again, so it is a budget
    // per stretch of bad luck rather than per station: a stream that drops
    // every hour is retried forever, while a URL that never plays at all is
    // given up on. libmpv reports no HTTP status, so "temporarily down" and
    // "gone for good" cannot be told apart any other way.
    int max_attempts = 8;

    // Doubles with every attempt, up to max_delay.
    std::chrono::milliseconds first_delay{1000};
    std::chrono::milliseconds max_delay{30000};
};

struct MpvOptions {
    // mpv's --ao. Empty lets mpv choose; tests pass "null" so they can run on a
    // machine with no sound hardware.
    std::string audio_output;

    // How long mpv waits on a stalled network read before giving up.
    int network_timeout_seconds = 15;

    RetryPolicy retry;
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
    MpvEngine(mpv_handle* handle, MpvOptions options);

    void run_event_loop();
    void handle_property_change(void* event_data);
    void handle_end_file(void* event_data);

    // Hands a URL to mpv and publishes the result. Used by play() and by the
    // retry path, which is the whole reason it is not inlined into play().
    // Must be called with no lock held.
    core::Status issue_loadfile(const std::string& url);

    // How long to let mpv_wait_event block: the time left until a pending
    // retry, or -1 for "until something happens".
    [[nodiscard]] double retry_wait_seconds() const;

    // Re-issues the pending retry if its delay has run out. Called on every
    // turn of the event loop.
    void retry_if_due();

    // Delay before the `attempt`-th retry, counting from 1.
    [[nodiscard]] std::chrono::milliseconds retry_delay(int attempt) const;

    // Builds a snapshot and hands it to the listener. Must be called with no
    // lock held.
    void republish();

    // Derives the player state from the raw mpv flags. Caller must hold mutex_.
    [[nodiscard]] core::PlayerState derive_state() const;

    // Snapshot of the current state. Caller must hold mutex_.
    [[nodiscard]] core::PlaybackStatus snapshot() const;

    mpv_handle* handle_ = nullptr;
    const MpvOptions options_;
    std::thread event_thread_;
    std::atomic<bool> stopping_{false};

    mutable std::mutex mutex_;
    core::PlaybackStatus status_;

    // Raw mpv properties. State is derived from them rather than stored,
    // so a stale combination cannot linger after an event is missed.
    bool idle_active_ = true;
    bool core_idle_ = true;

    // Set from the moment a loadfile is issued and cleared once mpv reports it
    // really has something loaded. It is what makes the state read as
    // "connecting" both while the first attempt is in flight and between
    // retries, in neither of which mpv itself is out of the idle state yet.
    bool loading_ = false;

    // Sticky until the next play(): when a stream errors out mpv also goes
    // idle, and without this the failure would immediately read as "idle".
    bool failed_ = false;
    core::Error last_error_;

    // Retry bookkeeping. retry_at_ is set only while one is pending.
    int retry_attempts_ = 0;
    std::optional<std::chrono::steady_clock::time_point> retry_at_;

    std::mutex listener_mutex_;
    core::IAudioEngineListener* listener_ = nullptr;
};

}  // namespace mradio::audio
