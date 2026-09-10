#include "mradio/core/track_info.hpp"

#include <catch2/catch_test_macros.hpp>

using mradio::core::parse_icy_title;
using mradio::core::TrackInfo;

TEST_CASE("splits the conventional artist - title line", "[track]")
{
    const TrackInfo t = parse_icy_title("Bonobo - Kiara");

    CHECK(t.artist == "Bonobo");
    CHECK(t.title == "Kiara");
    CHECK(t.raw == "Bonobo - Kiara");
    CHECK(t.display() == "Bonobo - Kiara");
}

TEST_CASE("an en dash separates too", "[track]")
{
    const TrackInfo t = parse_icy_title("Аквариум \xE2\x80\x93 Город золотой");

    CHECK(t.artist == "Аквариум");
    CHECK(t.title == "Город золотой");
}

TEST_CASE("a hyphen inside a name is not a separator", "[track]")
{
    const TrackInfo t = parse_icy_title("Jean-Michel Jarre");

    CHECK(t.artist.empty());
    CHECK(t.title == "Jean-Michel Jarre");
    CHECK(t.display() == "Jean-Michel Jarre");
}

TEST_CASE("only the first separator splits", "[track]")
{
    const TrackInfo t = parse_icy_title("Artist - Title - Remix");

    CHECK(t.artist == "Artist");
    CHECK(t.title == "Title - Remix");
}

TEST_CASE("a lopsided line is left whole", "[track]")
{
    CHECK(parse_icy_title(" - Orphaned").title == "- Orphaned");
    CHECK(parse_icy_title("Orphaned - ").title == "Orphaned -");
}

TEST_CASE("surrounding whitespace is stripped", "[track]")
{
    const TrackInfo t = parse_icy_title("  Bonobo - Kiara \r\n");

    CHECK(t.artist == "Bonobo");
    CHECK(t.title == "Kiara");
    CHECK(t.raw == "Bonobo - Kiara");
}

TEST_CASE("an empty line yields an empty TrackInfo", "[track]")
{
    const TrackInfo t = parse_icy_title("   ");

    CHECK(t.empty());
    CHECK(t.display().empty());
}
