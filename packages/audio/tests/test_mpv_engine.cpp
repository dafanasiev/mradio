#include "mradio/audio/mpv_engine.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <unistd.h>  // getpid, for a temp directory name unique to this run

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using mradio::audio::MpvEngine;
using mradio::audio::MpvOptions;
using mradio::audio::RetryPolicy;
using mradio::core::PlaybackStatus;
using mradio::core::PlayerState;
using mradio::core::Volume;

using namespace std::chrono_literals;

namespace {

// These tests drive a real mpv. They need no network and no sound card, but
// they do wait on real playback, so every wait is bounded.
constexpr auto kTimeout = 20s;

MpvOptions test_options()
{
    // "null" is what makes this runnable on a machine with no audio hardware.
    //
    // Retrying is off: the failure tests below want the failure now rather
    // than in a minute. The retry policy has a test of its own, with delays
    // measured in milliseconds.
    return MpvOptions{
        .audio_output = "null",
        .network_timeout_seconds = 5,
        .retry = RetryPolicy{.max_attempts = 0},
    };
}

class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path()
                / ("mradio-audio-" + std::to_string(::getpid()) + "-"
                   + std::to_string(counter_++)))
    {
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    static inline int counter_ = 0;
    std::filesystem::path path_;
};

void put_u32(std::ofstream& out, std::uint32_t value)
{
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8) & 0xFFu),
        static_cast<char>((value >> 16) & 0xFFu),
        static_cast<char>((value >> 24) & 0xFFu)};
    out.write(bytes.data(), bytes.size());
}

void put_u16(std::ofstream& out, std::uint16_t value)
{
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8) & 0xFFu)};
    out.write(bytes.data(), bytes.size());
}

// Writes a mono 16-bit 44.1 kHz WAV of silence.
//
// A generated file rather than a checked-in fixture: it keeps the repository
// free of binaries, and the engine cannot tell a local file from a stream, so
// it exercises the same state machine either way.
std::filesystem::path write_silent_wav(const std::filesystem::path& path, double seconds)
{
    constexpr std::uint32_t kRate = 44100;
    constexpr std::uint16_t kChannels = 1;
    constexpr std::uint16_t kBitsPerSample = 16;
    constexpr std::uint16_t kBlockAlign = kChannels * kBitsPerSample / 8;

    const auto frames = static_cast<std::uint32_t>(static_cast<double>(kRate) * seconds);
    const std::uint32_t data_bytes = frames * kBlockAlign;

    std::ofstream out(path, std::ios::binary);

    out.write("RIFF", 4);
    put_u32(out, 36u + data_bytes);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    put_u32(out, 16u);                       // PCM header size
    put_u16(out, 1u);                        // format: PCM
    put_u16(out, kChannels);
    put_u32(out, kRate);
    put_u32(out, kRate * kBlockAlign);       // byte rate
    put_u16(out, kBlockAlign);
    put_u16(out, kBitsPerSample);

    out.write("data", 4);
    put_u32(out, data_bytes);

    const std::vector<char> silence(data_bytes, '\0');
    out.write(silence.data(), static_cast<std::streamsize>(silence.size()));

    return path;
}

// Blocks until the engine publishes a snapshot matching a predicate.
class StatusWaiter final : public mradio::core::IAudioEngineListener {
public:
    void on_status_changed(const PlaybackStatus& status) override
    {
        {
            const std::lock_guard lock{mutex_};
            latest_ = status;
            seen_.push_back(status.state);
        }
        changed_.notify_all();
    }

    template <typename Predicate>
    [[nodiscard]] bool wait_until(Predicate predicate)
    {
        std::unique_lock lock{mutex_};
        return changed_.wait_for(lock, kTimeout, [&] { return predicate(latest_); });
    }

    // How many snapshots carried this state. Counted rather than compared as a
    // sequence: mpv publishes several snapshots per attempt, and how many is
    // not something a test should pin down.
    [[nodiscard]] std::size_t times_seen(PlayerState state)
    {
        const std::lock_guard lock{mutex_};
        return static_cast<std::size_t>(std::count(seen_.begin(), seen_.end(), state));
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    PlaybackStatus latest_;
    std::vector<PlayerState> seen_;
};

}  // namespace

TEST_CASE("a fresh engine is idle", "[audio]")
{
    auto engine = MpvEngine::create(test_options());

    REQUIRE(engine.has_value());
    CHECK((*engine)->status().state == PlayerState::idle);
    CHECK((*engine)->status().url.empty());
}

TEST_CASE("an empty URL is rejected before mpv is involved", "[audio]")
{
    auto engine = MpvEngine::create(test_options());
    REQUIRE(engine.has_value());

    const auto played = (*engine)->play("");

    REQUIRE_FALSE(played.has_value());
    CHECK(played.error().code == mradio::core::Errc::invalid_argument);
    CHECK((*engine)->status().state == PlayerState::idle);
}

TEST_CASE("a file plays and stops", "[audio]")
{
    const TempDir dir;
    const auto file = write_silent_wav(dir.path() / "tone.wav", 30.0);

    // Declared before the engine so that it outlives it, as the listener
    // contract requires.
    StatusWaiter waiter;

    auto created = MpvEngine::create(test_options());
    REQUIRE(created.has_value());
    const std::unique_ptr<MpvEngine>& engine = *created;
    engine->set_listener(&waiter);

    REQUIRE(engine->play(file.string()).has_value());
    REQUIRE(waiter.wait_until([](const PlaybackStatus& s) { return s.state == PlayerState::playing; }));
    CHECK(engine->status().url == file.string());

    engine->stop();
    REQUIRE(waiter.wait_until([](const PlaybackStatus& s) { return s.state == PlayerState::idle; }));
    CHECK(engine->status().url.empty());

    engine->set_listener(nullptr);
}

TEST_CASE("volume and mute round-trip through mpv", "[audio]")
{
    StatusWaiter waiter;

    auto created = MpvEngine::create(test_options());
    REQUIRE(created.has_value());
    const std::unique_ptr<MpvEngine>& engine = *created;
    engine->set_listener(&waiter);

    engine->set_volume(Volume{0.42});
    REQUIRE(waiter.wait_until([](const PlaybackStatus& s) {
        return s.volume.normalised() == Catch::Approx(0.42).margin(0.01);
    }));

    engine->set_muted(true);
    REQUIRE(waiter.wait_until([](const PlaybackStatus& s) { return s.muted; }));

    engine->set_muted(false);
    REQUIRE(waiter.wait_until([](const PlaybackStatus& s) { return !s.muted; }));

    engine->set_listener(nullptr);
}

TEST_CASE("a broken stream is retried before it is given up on", "[audio]")
{
    const TempDir dir;

    StatusWaiter waiter;

    MpvOptions options = test_options();
    options.retry = RetryPolicy{.max_attempts = 2, .first_delay = 20ms, .max_delay = 20ms};

    auto created = MpvEngine::create(options);
    REQUIRE(created.has_value());
    const std::unique_ptr<MpvEngine>& engine = *created;
    engine->set_listener(&waiter);

    // Fails the same way a dead stream URL does, only without the wait.
    REQUIRE(engine->play((dir.path() / "absent.mp3").string()).has_value());

    REQUIRE(waiter.wait_until([](const PlaybackStatus& s) { return s.state == PlayerState::failed; }));

    // Three attempts: the first one and two retries. Each of them reports
    // itself as connecting, so the tray keeps the station marked as on and
    // nothing in the UI flickers through "failed" on the way.
    CHECK(waiter.times_seen(PlayerState::connecting) >= 3);

    // The message says the retries happened, which is the difference between
    // a station that is down and one whose URL is wrong.
    CHECK(engine->status().error.message.find("2 attempts") != std::string::npos);

    engine->set_listener(nullptr);
}

TEST_CASE("retrying is not attempted after stop", "[audio]")
{
    const TempDir dir;

    StatusWaiter waiter;

    MpvOptions options = test_options();
    options.retry = RetryPolicy{.max_attempts = 20, .first_delay = 30ms, .max_delay = 30ms};

    auto created = MpvEngine::create(options);
    REQUIRE(created.has_value());
    const std::unique_ptr<MpvEngine>& engine = *created;
    engine->set_listener(&waiter);

    REQUIRE(engine->play((dir.path() / "absent.mp3").string()).has_value());

    // Stop lands while a retry is pending, which must drop it: the user asked
    // for silence, not for twenty more attempts.
    engine->stop();

    REQUIRE(waiter.wait_until([](const PlaybackStatus& s) { return s.state == PlayerState::idle; }));

    std::this_thread::sleep_for(150ms);  // several retry delays' worth

    CHECK(engine->status().state == PlayerState::idle);
    CHECK(engine->status().url.empty());

    engine->set_listener(nullptr);
}

TEST_CASE("an unplayable URL ends in the failed state with a message", "[audio]")
{
    const TempDir dir;

    StatusWaiter waiter;

    auto created = MpvEngine::create(test_options());
    REQUIRE(created.has_value());
    const std::unique_ptr<MpvEngine>& engine = *created;
    engine->set_listener(&waiter);

    // Accepted by mpv, then fails when the file turns out not to exist - which
    // is the path a dead stream URL takes too.
    REQUIRE(engine->play((dir.path() / "absent.mp3").string()).has_value());

    REQUIRE(waiter.wait_until([](const PlaybackStatus& s) { return s.state == PlayerState::failed; }));

    const PlaybackStatus status = engine->status();
    CHECK(status.state == PlayerState::failed);
    CHECK_FALSE(status.error.message.empty());

    engine->set_listener(nullptr);
}
