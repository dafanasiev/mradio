#include "mradio/audio/mpv_engine.hpp"

#include <mpv/client.h>

#include <array>
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

    // Radio streams drop. Letting ffmpeg reconnect underneath us is far cheaper
    // than a retry loop up here, and it keeps a "reconnecting" case out of the
    // state machine entirely.
    set("stream-lavf-o", "reconnect=1,reconnect_streamed=1,reconnect_delay_max=5");

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
    std::unique_ptr<MpvEngine> engine{new MpvEngine{handle}};
    engine->event_thread_ = std::thread{[raw = engine.get()] { raw->run_event_loop(); }};
    return engine;
}

MpvEngine::MpvEngine(mpv_handle* handle) : handle_(handle) {}

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
        mpv_event* const event = mpv_wait_event(handle_, -1.0);

        switch (event->event_id) {
            case MPV_EVENT_NONE:
                break;  // woken by the destructor
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
    }

    republish();
}

void MpvEngine::handle_end_file(void* event_data)
{
    const auto* const event = static_cast<const mpv_event*>(event_data);
    const auto* const end = static_cast<const mpv_event_end_file*>(event->data);

    if (end->reason == MPV_END_FILE_REASON_ERROR) {
        const std::lock_guard lock{mutex_};
        failed_ = true;
        last_error_ = backend_error(end->error, "playback failed");
    }

    republish();
}

core::PlayerState MpvEngine::derive_state() const
{
    // Order matters: a failed stream also leaves mpv idle, so the sticky
    // failure has to win over idle_active_.
    if (failed_) {
        return PlayerState::failed;
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
        failed_ = false;
        last_error_ = {};
        status_.url = target;
        status_.track = {};
    }

    // "replace" is mpv's default; spelling it out documents that selecting a
    // station abandons the previous one rather than queueing behind it.
    std::array<const char*, 4> command{"loadfile", target.c_str(), "replace", nullptr};
    if (const int rc = mpv_command(handle_, command.data()); rc < 0) {
        const std::lock_guard lock{mutex_};
        failed_ = true;
        last_error_ = backend_error(rc, "cannot start " + target);
        return std::unexpected(last_error_);
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
        failed_ = false;
        last_error_ = {};
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
