#include "mradio/tray/menu_model.hpp"

#include <catch2/catch_test_macros.hpp>

using mradio::core::Station;
using mradio::core::StationId;
using mradio::core::StationList;
using mradio::tray::MenuAction;
using mradio::tray::MenuEntry;
using mradio::tray::MenuModel;

namespace {

StationList two_stations()
{
    StationList list;
    list.add(Station{StationId{"jazz"}, "Радио Джаз", "http://example.org/1"}).value();
    list.add(Station{StationId{"rock"}, "Rock FM", "http://example.org/2"}).value();
    return list;
}

}  // namespace

TEST_CASE("the menu is the stations, a separator, Stop and Quit", "[tray]")
{
    const StationList stations = two_stations();
    const MenuModel model{stations};

    const std::vector<MenuEntry> entries = model.entries(std::nullopt);

    REQUIRE(entries.size() == 5);
    CHECK(entries[0].label == "Радио Джаз");
    CHECK(entries[1].label == "Rock FM");
    CHECK(entries[2].is_separator);
    CHECK(entries[3].label == "Стоп");
    CHECK(entries[4].label == "Выйти");
}

TEST_CASE("the station that is on carries the play glyph", "[tray]")
{
    const StationList stations = two_stations();
    const MenuModel model{stations};

    const std::vector<MenuEntry> entries = model.entries(StationId{"rock"});

    CHECK(entries[0].label == "Радио Джаз");
    CHECK_FALSE(entries[0].is_current);

    CHECK(entries[1].label == "\xE2\x96\xB6 Rock FM");
    CHECK(entries[1].is_current);
}

TEST_CASE("with nothing playing no label is marked", "[tray]")
{
    const StationList stations = two_stations();
    const MenuModel model{stations};

    for (const MenuEntry& entry : model.entries(std::nullopt)) {
        CHECK_FALSE(entry.is_current);
    }
}

TEST_CASE("ids map back to the action they stand for", "[tray]")
{
    const StationList stations = two_stations();
    const MenuModel model{stations};
    const std::vector<MenuEntry> entries = model.entries(std::nullopt);

    const MenuAction first = model.action_of(entries[0].id);
    REQUIRE(first.kind == MenuAction::Kind::play);
    CHECK(first.station.str() == "jazz");

    const MenuAction second = model.action_of(entries[1].id);
    REQUIRE(second.kind == MenuAction::Kind::play);
    CHECK(second.station.str() == "rock");

    CHECK(model.action_of(entries[3].id).kind == MenuAction::Kind::stop);
    CHECK(model.action_of(entries[4].id).kind == MenuAction::Kind::quit);
}

TEST_CASE("the root, the separator and unknown ids do nothing", "[tray]")
{
    const StationList stations = two_stations();
    const MenuModel model{stations};
    const std::vector<MenuEntry> entries = model.entries(std::nullopt);

    CHECK(model.action_of(MenuModel::root_id()).kind == MenuAction::Kind::none);
    CHECK(model.action_of(entries[2].id).kind == MenuAction::Kind::none);

    // What a host that cached a longer layout would send.
    CHECK(model.action_of(9999).kind == MenuAction::Kind::none);
    CHECK(model.action_of(-1).kind == MenuAction::Kind::none);
}

TEST_CASE("an empty playlist still offers Stop and Quit", "[tray]")
{
    const StationList stations;
    const MenuModel model{stations};

    const std::vector<MenuEntry> entries = model.entries(std::nullopt);

    REQUIRE(entries.size() == 3);
    CHECK(entries[0].is_separator);
    CHECK(model.action_of(entries[1].id).kind == MenuAction::Kind::stop);
    CHECK(model.action_of(entries[2].id).kind == MenuAction::Kind::quit);
}
