#include "mradio/stations/loader.hpp"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>  // getpid, for a temp directory name unique to this run

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

using mradio::core::Errc;
using mradio::core::StationId;
using mradio::stations::Loaded;

namespace {

// Sets an environment variable for the duration of a test and puts the
// previous value back afterwards, so tests stay independent of each other and
// of the environment they were launched in.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name)
    {
        if (const char* const previous = std::getenv(name)) {
            saved_ = previous;
        }
        if (value != nullptr) {
            ::setenv(name, value, 1);
        }
        else {
            ::unsetenv(name);
        }
    }

    ~ScopedEnv()
    {
        if (saved_) {
            ::setenv(name_, saved_->c_str(), 1);
        }
        else {
            ::unsetenv(name_);
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* name_;
    std::optional<std::string> saved_;
};

// A directory that removes itself, for the tests that need a real file.
class TempDir {
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path()
                / ("mradio-test-" + std::to_string(::getpid()) + "-"
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

    std::filesystem::path write(const std::string& name, std::string_view content) const
    {
        const std::filesystem::path file = path_ / name;
        std::ofstream out(file, std::ios::binary);
        out << content;
        return file;
    }

private:
    static inline int counter_ = 0;
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("a playlist becomes an ordered station list", "[stations]")
{
    const auto loaded = mradio::stations::load_from_text(
        "#EXTM3U\n"
        "#EXTINF:-1,SomaFM Groove Salad\n"
        "https://ice1.somafm.com/groovesalad-128-mp3\n"
        "#EXTINF:-1,Radio Paradise\n"
        "https://stream.radioparadise.com/aac-320\n");

    REQUIRE(loaded.has_value());
    CHECK(loaded->warnings.empty());
    REQUIRE(loaded->stations.size() == 2);

    CHECK(loaded->stations[0].id.str() == "somafm-groove-salad");
    CHECK(loaded->stations[0].name == "SomaFM Groove Salad");
    CHECK(loaded->stations[0].url == "https://ice1.somafm.com/groovesalad-128-mp3");

    CHECK(loaded->stations[1].id.str() == "radio-paradise");
}

TEST_CASE("a UTF-8 title survives into the name and the id", "[stations]")
{
    const auto loaded = mradio::stations::load_from_text(
        "#EXTM3U\n#EXTINF:-1,Радио Джаз\nhttp://example.org/jazz\n");

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->stations.size() == 1);
    CHECK(loaded->stations[0].name == "Радио Джаз");
    CHECK(loaded->stations[0].id.str() == "Радио-Джаз");
}

TEST_CASE("an untitled entry is named after its host", "[stations]")
{
    const auto loaded = mradio::stations::load_from_text(
        "#EXTM3U\nhttps://user@ice1.somafm.com:8000/groovesalad\n");

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->stations.size() == 1);
    CHECK(loaded->stations[0].name == "ice1.somafm.com");
    CHECK(loaded->stations[0].id.str() == "ice1-somafm-com");
    // The stream URL itself must be handed over untouched.
    CHECK(loaded->stations[0].url == "https://user@ice1.somafm.com:8000/groovesalad");
}

TEST_CASE("repeated names get distinct ids", "[stations]")
{
    const auto loaded = mradio::stations::load_from_text(
        "#EXTINF:-1,Jazz\nhttp://example.org/1\n"
        "#EXTINF:-1,Jazz\nhttp://example.org/2\n"
        "#EXTINF:-1,Jazz\nhttp://example.org/3\n");

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->stations.size() == 3);
    CHECK(loaded->stations[0].id.str() == "jazz");
    CHECK(loaded->stations[1].id.str() == "jazz-2");
    CHECK(loaded->stations[2].id.str() == "jazz-3");

    // All three still reachable, which is the point of the suffixes.
    CHECK(loaded->stations.find(StationId{"jazz-2"})->url == "http://example.org/2");
}

TEST_CASE("a name with no usable characters still yields an id", "[stations]")
{
    const auto loaded = mradio::stations::load_from_text("#EXTINF:-1,!!!\nhttp://example.org/x\n");

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->stations.size() == 1);
    CHECK(loaded->stations[0].id.str() == "station-1");
    CHECK(loaded->stations[0].name == "!!!");
}

TEST_CASE("a malformed line is a warning, not a failure", "[stations]")
{
    const auto loaded = mradio::stations::load_from_text(
        "#EXTM3U\n"
        "#EXTINF:abc,Broken\n"
        "http://example.org/broken\n"
        "#EXTINF:-1,Fine\n"
        "http://example.org/fine\n");

    REQUIRE(loaded.has_value());
    CHECK(loaded->warnings.size() == 1);
    CHECK(loaded->warnings[0].starts_with("line 2:"));
    // Both stations are kept - that is the whole point of being lenient.
    CHECK(loaded->stations.size() == 2);
}

TEST_CASE("an empty playlist loads to an empty list", "[stations]")
{
    const auto loaded = mradio::stations::load_from_text("#EXTM3U\n");

    REQUIRE(loaded.has_value());
    CHECK(loaded->stations.empty());
    CHECK(loaded->warnings.empty());
}

TEST_CASE("a playlist is read from disk", "[stations]")
{
    const TempDir dir;
    const auto file = dir.write("playlist.m3u",
                                "#EXTM3U\r\n#EXTINF:-1,Windows Written\r\nhttp://example.org/s\r\n");

    const auto loaded = mradio::stations::load(file);

    REQUIRE(loaded.has_value());
    REQUIRE(loaded->stations.size() == 1);
    CHECK(loaded->stations[0].name == "Windows Written");
    CHECK(loaded->stations[0].url == "http://example.org/s");
}

TEST_CASE("a missing playlist reports not_found with the path", "[stations]")
{
    const TempDir dir;

    const auto loaded = mradio::stations::load(dir.path() / "absent.m3u");

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().code == Errc::not_found);
    CHECK(loaded.error().message.find("absent.m3u") != std::string::npos);
}

TEST_CASE("the default path follows XDG_CONFIG_HOME", "[stations]")
{
    const ScopedEnv xdg{"XDG_CONFIG_HOME", "/xdg/config"};

    const auto path = mradio::stations::default_playlist_path();

    REQUIRE(path.has_value());
    CHECK(*path == std::filesystem::path{"/xdg/config/mradio/playlist.m3u"});
}

TEST_CASE("a relative XDG_CONFIG_HOME is treated as unset", "[stations]")
{
    const ScopedEnv xdg{"XDG_CONFIG_HOME", "relative/dir"};
    const ScopedEnv home{"HOME", "/home/someone"};

    const auto path = mradio::stations::default_playlist_path();

    REQUIRE(path.has_value());
    CHECK(*path == std::filesystem::path{"/home/someone/.config/mradio/playlist.m3u"});
}

TEST_CASE("an empty XDG_CONFIG_HOME falls back to HOME", "[stations]")
{
    const ScopedEnv xdg{"XDG_CONFIG_HOME", ""};
    const ScopedEnv home{"HOME", "/home/someone"};

    const auto path = mradio::stations::default_playlist_path();

    REQUIRE(path.has_value());
    CHECK(*path == std::filesystem::path{"/home/someone/.config/mradio/playlist.m3u"});
}

TEST_CASE("with neither variable set there is nowhere to look", "[stations]")
{
    const ScopedEnv xdg{"XDG_CONFIG_HOME", nullptr};
    const ScopedEnv home{"HOME", nullptr};

    const auto path = mradio::stations::default_playlist_path();

    REQUIRE_FALSE(path.has_value());
    CHECK(path.error().code == Errc::not_found);
}
