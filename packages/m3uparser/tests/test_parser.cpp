#include "mradio/m3u/parser.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string_view>

using mradio::m3u::parse;
using mradio::m3u::Playlist;

namespace {

// The shape a user's ~/.config/mradio/playlist.m3u is expected to take.
constexpr std::string_view kTypical =
    "#EXTM3U\n"
    "#EXTINF:-1,SomaFM Groove Salad\n"
    "https://ice1.somafm.com/groovesalad-128-mp3\n"
    "#EXTINF:-1,Radio Paradise\n"
    "https://stream.radioparadise.com/aac-320\n";

}  // namespace

TEST_CASE("parses a typical radio playlist", "[m3u]")
{
    const Playlist pl = parse(kTypical);

    REQUIRE(pl.problems.empty());
    REQUIRE(pl.entries.size() == 2);

    CHECK(pl.entries[0].title == "SomaFM Groove Salad");
    CHECK(pl.entries[0].url == "https://ice1.somafm.com/groovesalad-128-mp3");
    CHECK(pl.entries[0].duration_seconds == Catch::Approx(-1.0));

    CHECK(pl.entries[1].title == "Radio Paradise");
    CHECK(pl.entries[1].url == "https://stream.radioparadise.com/aac-320");
}

TEST_CASE("the #EXTM3U header is optional", "[m3u]")
{
    const Playlist pl = parse("#EXTINF:-1,Only Station\nhttp://example.org/s\n");

    REQUIRE(pl.problems.empty());
    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].title == "Only Station");
}

TEST_CASE("CRLF line endings leave no carriage return on the URL", "[m3u]")
{
    // A stray '\r' on the end of a URL is invisible in a terminal and breaks
    // the stream request, so it is worth asserting directly.
    const Playlist pl = parse("#EXTM3U\r\n#EXTINF:-1,Win Station\r\nhttp://example.org/s\r\n");

    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].url == "http://example.org/s");
    CHECK(pl.entries[0].title == "Win Station");
}

TEST_CASE("a leading UTF-8 BOM is ignored", "[m3u]")
{
    const Playlist pl = parse("\xEF\xBB\xBF#EXTM3U\n#EXTINF:-1,BOM Station\nhttp://example.org/s\n");

    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].title == "BOM Station");
}

TEST_CASE("a title keeps its commas", "[m3u]")
{
    const Playlist pl = parse("#EXTINF:-1,Jazz, Blues & Soul\nhttp://example.org/s\n");

    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].title == "Jazz, Blues & Soul");
}

TEST_CASE("a URI with no #EXTINF still yields an entry", "[m3u]")
{
    const Playlist pl = parse("#EXTM3U\nhttp://example.org/bare\n");

    REQUIRE(pl.problems.empty());
    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].url == "http://example.org/bare");
    CHECK(pl.entries[0].title.empty());
}

TEST_CASE("comments and unmodelled directives are ignored", "[m3u]")
{
    const Playlist pl = parse(
        "#EXTM3U\n"
        "# just a note\n"
        "#EXT-X-VERSION:3\n"
        "#EXTINF:-1,Station\n"
        "http://example.org/s\n");

    REQUIRE(pl.problems.empty());
    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].title == "Station");
}

TEST_CASE("blank and indented lines are ignored", "[m3u]")
{
    const Playlist pl = parse("#EXTM3U\n\n   \n  #EXTINF:-1,Station  \n\t http://example.org/s \n\n");

    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].title == "Station");
    CHECK(pl.entries[0].url == "http://example.org/s");
}

TEST_CASE("a real duration is kept", "[m3u]")
{
    const Playlist pl = parse("#EXTINF:12.5,Track\nhttp://example.org/t\n");

    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].duration_seconds == Catch::Approx(12.5));
}

TEST_CASE("an absent duration field is unknown, not an error", "[m3u]")
{
    const Playlist pl = parse("#EXTINF:,Station\nhttp://example.org/s\n");

    CHECK(pl.problems.empty());
    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].title == "Station");
    CHECK(pl.entries[0].duration_seconds == Catch::Approx(-1.0));
}

TEST_CASE("a non-numeric duration is reported but the station survives", "[m3u]")
{
    const Playlist pl = parse("#EXTINF:abc,Station\nhttp://example.org/s\n");

    REQUIRE(pl.problems.size() == 1);
    CHECK(pl.problems[0].line == 1);

    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].title == "Station");
    CHECK(pl.entries[0].duration_seconds == Catch::Approx(-1.0));
}

TEST_CASE("a dangling #EXTINF at end of file is reported", "[m3u]")
{
    const Playlist pl = parse("#EXTM3U\n#EXTINF:-1,Lonely\n");

    CHECK(pl.entries.empty());
    REQUIRE(pl.problems.size() == 1);
    CHECK(pl.problems[0].line == 2);
}

TEST_CASE("two #EXTINF lines in a row are reported and the first is dropped", "[m3u]")
{
    const Playlist pl = parse(
        "#EXTINF:-1,First\n"
        "#EXTINF:-1,Second\n"
        "http://example.org/s\n");

    REQUIRE(pl.problems.size() == 1);
    CHECK(pl.problems[0].line == 1);

    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].title == "Second");
}

TEST_CASE("empty input yields nothing", "[m3u]")
{
    const Playlist pl = parse("");

    CHECK(pl.entries.empty());
    CHECK(pl.problems.empty());
}

TEST_CASE("a file with no trailing newline still yields its last entry", "[m3u]")
{
    const Playlist pl = parse("#EXTINF:-1,Last\nhttp://example.org/last");

    REQUIRE(pl.entries.size() == 1);
    CHECK(pl.entries[0].url == "http://example.org/last");
}
