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
