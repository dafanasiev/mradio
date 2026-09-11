#include "mradio/audio/mpv_engine.hpp"

#include <mpv/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace mradio::audio {
namespace {

using core::Errc;
using core::PlayerState;

// Ids handed to mpv_observe_property. They come back on the event, so the
// handler dispatches on an integer instead of comparing property names.
enum ObservedId : std::uint64_t {
    kIdleActive = 1,
    kCoreIdle,
    kVolume,
    kMute,
    kMetadata,
};

core::Error backend_error(int code, std::string_view what)
{
    return core::make_error(Errc::backend_error,
                            std::string(what) + ": " + mpv_error_string(code));
}

// Pulls the now-playing line out of mpv's metadata map.
//
// Icecast and Shoutcast deliver it as "icy-title". Other containers use
// "title", which is worth taking as a fallback but never in preference: a
// stream that sends both means the ICY line to be the live one.
std::optional<std::string> icy_title_of(const mpv_node& node)
{
    if (node.format != MPV_FORMAT_NODE_MAP || node.u.list == nullptr) {
        return std::nullopt;
    }

    const mpv_node_list& map = *node.u.list;
    std::optional<std::string> fallback;

    for (int i = 0; i < map.num; ++i) {
        if (map.values[i].format != MPV_FORMAT_STRING || map.keys[i] == nullptr) {
            continue;
        }
        const std::string_view key{map.keys[i]};
        if (key == "icy-title") {
            return std::string{map.values[i].u.string};
        }
        if (key == "title") {
            fallback = std::string{map.values[i].u.string};
        }
    }

    return fallback;
}

bool flag_of(const void* data) noexcept
{
    return *static_cast<const int*>(data) != 0;
}

}  // namespace

core::Result<std::unique_ptr<MpvEngine>> MpvEngine::create(MpvOptions options)
{
    mpv_handle* const handle = mpv_create();
    if (handle == nullptr) {
        return core::fail(Errc::backend_error, "mpv_create() failed");
    }

    // Options have to be set before mpv_initialize.
    const auto set = [handle](const char* name, const char* value) {
        mpv_set_option_string(handle, name, value);
    };

    set("terminal", "no");       // mpv must never write to our stdout
    set("video", "no");
    set("vo", "null");
    set("audio-display", "no");
    set("idle", "yes");          // stay alive with nothing loaded
    set("keep-open", "no");

    // Nothing in ~/.config/mpv may reach this program. A stray mpv.conf with
    // an audio filter, a volume-max or a different --ao in it would silently
    // become ours, and there would be nothing in mradio to explain why.
    set("config", "no");

    // libmpv comes up with mpv's built-in Lua scripts running - the OSD
    // console, the stats overlay, the youtube-dl hook, the select, positioning
    // and context menus - each on a thread of its own. This program has no
    // OSD, no key bindings and no video site to resolve, so that is a Lua
    // runtime and half a dozen threads bought for nothing.
    //
    // There is no single switch for them: each built-in has its own, and
    // mpv_set_option_string simply refuses a name a given build does not know,
    // which is why listing options that may not exist yet is safe.
    for (const char* const script : {"load-scripts",
                                     "load-osd-console",
                                     "load-stats-overlay",
                                     "load-auto-profiles",
                                     "load-select",
                                     "load-positioning",
                                     "load-commands",
                                     "load-context-menu",
                                     "ytdl"}) {
        set(script, "no");
    }

    // Radio streams drop: the network blinks, the server restarts, a proxy cuts
    // a connection it thinks is idle. Most of that is ffmpeg's HTTP layer
    // reconnecting underneath us and never reaches this class at all.
    //
    // The retry count is deliberately small. ffmpeg's own reconnecting is
    // invisible from up here - no event, no property, nothing to report - so
    // the longer it goes on, the longer the program has nothing to say for
    // itself. A handful of quick attempts inside ffmpeg, and then the failure
    // comes to us as an end-of-file error and RetryPolicy takes over on a
    // delay we control and a state the views can see.
    //
    // mpv ignores keys a given ffmpeg build does not have, so naming the
    // newer options costs nothing on an older one.
    set("stream-lavf-o",
        "reconnect=1,"
        "reconnect_at_eof=1,"            // for a live stream, EOF *is* a drop
        "reconnect_streamed=1,"          // ...and it is never seekable
        "reconnect_on_network_error=1,"
        "reconnect_max_retries=3,"
        "reconnect_delay_max=5,"
        "reconnect_delay_total_max=20");

    const std::string timeout = std::to_string(options.network_timeout_seconds);
    set("network-timeout", timeout.c_str());

    if (!options.audio_output.empty()) {
        set("ao", options.audio_output.c_str());
    }

    if (const int rc = mpv_initialize(handle); rc < 0) {
        mpv_terminate_destroy(handle);
        return std::unexpected(backend_error(rc, "mpv_initialize"));
    }

    mpv_observe_property(handle, kIdleActive, "idle-active", MPV_FORMAT_FLAG);
    mpv_observe_property(handle, kCoreIdle, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(handle, kVolume, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(handle, kMute, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(handle, kMetadata, "metadata", MPV_FORMAT_NODE);

    // Private constructor, so make_unique is not an option here.
    std::unique_ptr<MpvEngine> engine{new MpvEngine{handle, std::move(options)}};
    engine->event_thread_ = std::thread{[raw = engine.get()] { raw->run_event_loop(); }};
    return engine;
}

MpvEngine::MpvEngine(mpv_handle* handle, MpvOptions options)
    : handle_(handle), options_(std::move(options))
{
}

MpvEngine::~MpvEngine()
{
    stopping_.store(true, std::memory_order_relaxed);

    // Breaks the event thread out of mpv_wait_event's indefinite wait.
    mpv_wakeup(handle_);

    if (event_thread_.joinable()) {
        event_thread_.join();
    }
    mpv_terminate_destroy(handle_);
}

void MpvEngine::run_event_loop()
{
    while (!stopping_.load(std::memory_order_relaxed)) {
        // A pending retry is the only thing this loop has to wake up for on
        // its own, which is why the delay lives here instead of in a timer
        // thread of its own.
        mpv_event* const event = mpv_wait_event(handle_, retry_wait_seconds());

        switch (event->event_id) {
            case MPV_EVENT_NONE:
                break;  // the retry delay ran out, or the destructor woke us
            case MPV_EVENT_SHUTDOWN:
                return;
            case MPV_EVENT_PROPERTY_CHANGE:
                handle_property_change(event);
                break;
            case MPV_EVENT_END_FILE:
                handle_end_file(event);
                break;
            default:
                break;
        }

        retry_if_due();
    }
}

void MpvEngine::handle_property_change(void* event_data)
{
    // Taken as void* so that <mpv/client.h> stays out of the public header.
    const auto* const event = static_cast<const mpv_event*>(event_data);
    const auto* const property = static_cast<const mpv_event_property*>(event->data);

    if (property->data == nullptr) {
        return;  // mpv has no value for this property at the moment
    }

    {
        const std::lock_guard lock{mutex_};
        switch (event->reply_userdata) {
            case kIdleActive:
                idle_active_ = flag_of(property->data);
                if (!idle_active_) {
                    loading_ = false;  // mpv really has the stream now
                }
                break;
            case kCoreIdle:
                core_idle_ = flag_of(property->data);
                break;
            case kVolume:
                status_.volume =
                    core::Volume::from_percent(*static_cast<const double*>(property->data));
                break;
            case kMute:
                status_.muted = flag_of(property->data);
                break;
            case kMetadata: {
                const auto title = icy_title_of(*static_cast<const mpv_node*>(property->data));
                status_.track = title ? core::parse_icy_title(*title) : core::TrackInfo{};
                break;
            }
            default:
                break;
        }

        // Audio is flowing: whatever went wrong before is over, and the next
        // drop deserves the full retry budget rather than the remains of the
        // last one.
        if (derive_state() == PlayerState::playing) {
            retry_attempts_ = 0;
        }
    }

    republish();
}

void MpvEngine::handle_end_file(void* event_data)
{
    const auto* const event = static_cast<const mpv_event*>(event_data);
    const auto* const end = static_cast<const mpv_event_end_file*>(event->data);

    if (end->reason != MPV_END_FILE_REASON_ERROR) {
        republish();
        return;
    }

    {
        const std::lock_guard lock{mutex_};
        const core::Error error = backend_error(end->error, "playback failed");

        // stop() got there first, so this error belongs to a stream nobody is
        // waiting for any more. Reporting it would leave a player the user
        // deliberately stopped sitting in the failed state.
        if (status_.url.empty()) {
            loading_ = false;
            retry_at_.reset();
        }
        // Still on a station, and the budget is not spent: keep the
        // "connecting" face and come back to it after the delay. The user
        // clicked this station and has not asked for anything else, so the
        // tray keeps its play glyph and nothing reports a failure yet.
        else if (retry_attempts_ < options_.retry.max_attempts) {
            ++retry_attempts_;
            loading_ = true;
            failed_ = false;
            last_error_ = error;
            retry_at_ = std::chrono::steady_clock::now() + retry_delay(retry_attempts_);
        }
        else {
            loading_ = false;
            failed_ = true;
            retry_at_.reset();
            last_error_ =
                retry_attempts_ > 0
                    ? core::make_error(error.code,
                                       error.message + " (gave up after "
                                           + std::to_string(retry_attempts_) + " attempts)")
                    : error;
        }
    }

    republish();
}

std::chrono::milliseconds MpvEngine::retry_delay(int attempt) const
{
    std::chrono::milliseconds delay = options_.retry.first_delay;
    for (int i = 1; i < attempt && delay < options_.retry.max_delay; ++i) {
        delay *= 2;
    }
    return std::min(delay, options_.retry.max_delay);
}

double MpvEngine::retry_wait_seconds() const
{
    const std::lock_guard lock{mutex_};
    if (!retry_at_.has_value()) {
        return -1.0;  // nothing pending: block until mpv has something to say
    }

    const auto remaining = *retry_at_ - std::chrono::steady_clock::now();
    if (remaining <= std::chrono::steady_clock::duration::zero()) {
        return 0.0;
    }
    return std::chrono::duration<double>(remaining).count();
}

void MpvEngine::retry_if_due()
{
    if (stopping_.load(std::memory_order_relaxed)) {
        return;  // teardown woke us, not the delay
    }

    std::string url;
    {
        const std::lock_guard lock{mutex_};
        if (!retry_at_.has_value() || std::chrono::steady_clock::now() < *retry_at_) {
            return;
        }
        retry_at_.reset();
        url = status_.url;
    }

    if (url.empty()) {
        return;  // stopped while the delay was running
    }

    // A failure here is already published by issue_loadfile; there is no one
    // to hand a return value to on this thread.
    static_cast<void>(issue_loadfile(url));
}

core::PlayerState MpvEngine::derive_state() const
{
    // Order matters. A failed stream also leaves mpv idle, so the sticky
    // failure has to win over idle_active_; and a loadfile that has been
    // issued but not yet picked up - the first attempt, or one between
    // retries - leaves mpv idle too, while the station is very much on.
    if (failed_) {
        return PlayerState::failed;
    }
    if (loading_) {
        return PlayerState::connecting;
    }
    if (idle_active_) {
        return PlayerState::idle;
    }
    if (core_idle_) {
        return PlayerState::connecting;  // loaded, but no audio flowing yet
    }
    return PlayerState::playing;
}

core::PlaybackStatus MpvEngine::snapshot() const
{
    core::PlaybackStatus result = status_;
    result.state = derive_state();
    result.error = failed_ ? last_error_ : core::Error{};
    return result;
}

void MpvEngine::republish()
{
    core::PlaybackStatus published;
    {
        const std::lock_guard lock{mutex_};
        published = snapshot();
    }

    // The listener lock is held across the callback so that a concurrent
    // set_listener(nullptr) cannot pull the listener out from under a call in
    // flight. That is why set_listener must not be called from the callback.
    const std::lock_guard lock{listener_mutex_};
    if (listener_ != nullptr) {
        listener_->on_status_changed(published);
    }
}

core::Status MpvEngine::play(std::string_view url)
{
    if (url.empty()) {
        return core::fail(Errc::invalid_argument, "empty stream URL");
    }

    const std::string target{url};

    {
        const std::lock_guard lock{mutex_};
        loading_ = true;
        failed_ = false;
        last_error_ = {};
        retry_attempts_ = 0;
        retry_at_.reset();
        status_.url = target;
        status_.track = {};
    }

    return issue_loadfile(target);
}

core::Status MpvEngine::issue_loadfile(const std::string& url)
{
    // "replace" is mpv's default; spelling it out documents that selecting a
    // station abandons the previous one rather than queueing behind it.
    std::array<const char*, 4> command{"loadfile", url.c_str(), "replace", nullptr};

    if (const int rc = mpv_command(handle_, command.data()); rc < 0) {
        const core::Error error = backend_error(rc, "cannot start " + url);
        {
            const std::lock_guard lock{mutex_};
            loading_ = false;
            failed_ = true;
            retry_at_.reset();
            last_error_ = error;
        }
        republish();
        return std::unexpected(error);
    }

    republish();
    return {};
}

void MpvEngine::stop()
{
    std::array<const char*, 2> command{"stop", nullptr};
    mpv_command(handle_, command.data());

    {
        const std::lock_guard lock{mutex_};
        status_.url.clear();
        status_.track = {};
        loading_ = false;
        failed_ = false;
        last_error_ = {};
        retry_attempts_ = 0;
        retry_at_.reset();
    }

    republish();
}

void MpvEngine::set_volume(core::Volume volume)
{
    double percent = volume.percent();
    mpv_set_property(handle_, "volume", MPV_FORMAT_DOUBLE, &percent);
}

void MpvEngine::set_muted(bool muted)
{
    int flag = muted ? 1 : 0;
    mpv_set_property(handle_, "mute", MPV_FORMAT_FLAG, &flag);
}

core::PlaybackStatus MpvEngine::status() const
{
    const std::lock_guard lock{mutex_};
    return snapshot();
}

void MpvEngine::set_listener(core::IAudioEngineListener* listener)
{
    const std::lock_guard lock{listener_mutex_};
    listener_ = listener;
}

}  // namespace mradio::audio
