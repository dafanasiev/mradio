#include "mradio/core/station.hpp"

#include <catch2/catch_test_macros.hpp>

using mradio::core::Errc;
using mradio::core::Station;
using mradio::core::StationId;
using mradio::core::StationList;

namespace {

Station make(std::string id, std::string name, std::string url = "http://example.org/s")
{
    return Station{StationId{std::move(id)}, std::move(name), std::move(url)};
}

}  // namespace

TEST_CASE("an id is derived from a display name", "[station]")
{
    const auto id = StationId::from_name("SomaFM Groove Salad");

    REQUIRE(id.has_value());
    CHECK(id->str() == "somafm-groove-salad");
}

TEST_CASE("punctuation runs collapse to a single dash", "[station]")
{
    CHECK(StationId::from_name("Jazz, Blues & Soul")->str() == "jazz-blues-soul");
    CHECK(StationId::from_name("Radio    101")->str() == "radio-101");
}

TEST_CASE("leading and trailing separators are trimmed", "[station]")
{
    CHECK(StationId::from_name("  ***Radio!!!  ")->str() == "radio");
}

TEST_CASE("a non-ASCII name keeps its characters", "[station]")
{
    // Slugifying to ASCII would turn every Cyrillic station name into an empty
    // id, which is exactly the case this rule exists for.
    const auto id = StationId::from_name("Радио Джаз");

    REQUIRE(id.has_value());
    CHECK(id->str() == "Радио-Джаз");
}

TEST_CASE("a name with nothing usable in it has no id", "[station]")
{
    CHECK_FALSE(StationId::from_name("!!! ???").has_value());
    CHECK_FALSE(StationId::from_name("").has_value());
}

TEST_CASE("a station needs an id, a name and a URL", "[station]")
{
    CHECK(make("id", "Name").validate().has_value());

    CHECK_FALSE(Station{StationId{}, "Name", "http://x"}.validate().has_value());
    CHECK_FALSE(make("id", "").validate().has_value());
    CHECK_FALSE(make("id", "Name", "").validate().has_value());
}

TEST_CASE("the list keeps file order and looks up by id", "[station]")
{
    StationList list;

    REQUIRE(list.add(make("first", "First")).has_value());
    REQUIRE(list.add(make("second", "Second")).has_value());

    REQUIRE(list.size() == 2);
    CHECK(list[0].name == "First");
    CHECK(list[1].name == "Second");

    const Station* found = list.find(StationId{"second"});
    REQUIRE(found != nullptr);
    CHECK(found->name == "Second");

    CHECK(list.contains(StationId{"first"}));
    CHECK_FALSE(list.contains(StationId{"third"}));
    CHECK(list.find(StationId{"third"}) == nullptr);
}

TEST_CASE("a duplicate id is rejected and leaves the list untouched", "[station]")
{
    StationList list;
    REQUIRE(list.add(make("dup", "Original")).has_value());

    const auto result = list.add(make("dup", "Impostor"));

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Errc::already_exists);
    REQUIRE(list.size() == 1);
    CHECK(list[0].name == "Original");
}

TEST_CASE("an invalid station is rejected", "[station]")
{
    StationList list;

    const auto result = list.add(make("id", "Name", ""));

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == Errc::invalid_argument);
    CHECK(list.empty());
}

TEST_CASE("names order case-insensitively for ASCII, by byte above it", "[station]")
{
    using mradio::core::name_precedes;

    CHECK(name_precedes("Alpha", "beta"));
    CHECK(name_precedes("alpha", "Beta"));
    CHECK_FALSE(name_precedes("zeta", "Alpha"));

    // Equal names precede neither way, which is what makes the sort stable.
    CHECK_FALSE(name_precedes("Same", "same"));
    CHECK_FALSE(name_precedes("same", "Same"));

    // A prefix comes first, and UTF-8 bytes put Cyrillic after Latin.
    CHECK(name_precedes("Jazz", "Jazz FM"));
    CHECK(name_precedes("zeta", "Радио Джаз"));
    CHECK(name_precedes("Радио Джаз", "Радио Рок"));
}

TEST_CASE("sorting the list moves the stations and keeps the lookups right", "[station]")
{
    StationList list;
    list.add(make("zeta", "zeta")).value();
    list.add(make("alpha", "Alpha")).value();
    list.add(make("jazz", "Радио Джаз")).value();
    list.add(make("beta", "beta")).value();

    list.sort_by_name();

    REQUIRE(list.size() == 4);
    CHECK(list[0].name == "Alpha");
    CHECK(list[1].name == "beta");
    CHECK(list[2].name == "zeta");
    CHECK(list[3].name == "Радио Джаз");

    // by_id_ holds positions, so every one of them has just changed.
    CHECK(list.index_of(StationId{"alpha"}) == 0);
    CHECK(list.index_of(StationId{"jazz"}) == 3);
    CHECK(list.find(StationId{"zeta"})->name == "zeta");
    CHECK(list.contains(StationId{"beta"}));
    CHECK(list.find(StationId{"nobody"}) == nullptr);
}

TEST_CASE("sorting is stable and an empty list survives it", "[station]")
{
    StationList list;
    list.add(make("second", "Same", "http://example.org/2")).value();
    list.add(make("first", "Same", "http://example.org/1")).value();

    list.sort_by_name();

    CHECK(list[0].id.str() == "second");
    CHECK(list[1].id.str() == "first");

    StationList empty;
    empty.sort_by_name();
    CHECK(empty.empty());
}

TEST_CASE("a sorted list still accepts stations, and still rejects duplicates", "[station]")
{
    StationList list;
    list.add(make("zeta", "zeta")).value();
    list.add(make("alpha", "Alpha")).value();

    list.sort_by_name();

    REQUIRE(list.add(make("mu", "Mu")));
    CHECK_FALSE(list.add(make("zeta", "zeta again")));

    // Appended, not inserted in order: sorting is a one-off, not a promise
    // the list goes on keeping.
    CHECK(list[2].name == "Mu");
    CHECK(list.index_of(StationId{"mu"}) == 2);
}
