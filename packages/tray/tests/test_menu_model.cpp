#include "mradio/tray/menu_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

TEST_CASE("the menu is the stations, a separator, Sort by name, Stop and Quit", "[tray]")
{
    const StationList stations = two_stations();
    const MenuModel model{stations};

    const std::vector<MenuEntry> entries = model.entries(std::nullopt);

    REQUIRE(entries.size() == 6);
    CHECK(entries[0].label == "Радио Джаз");
    CHECK(entries[1].label == "Rock FM");
    CHECK(entries[2].is_separator);
    CHECK(entries[3].label == "\xE2\x98\x90 Sort by name");
    CHECK(entries[4].label == "Stop");
    CHECK(entries[5].label == "Quit");
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

    CHECK(model.action_of(entries[3].id).kind == MenuAction::Kind::toggle_sort);
    CHECK(model.action_of(entries[4].id).kind == MenuAction::Kind::stop);
    CHECK(model.action_of(entries[5].id).kind == MenuAction::Kind::quit);
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

TEST_CASE("an empty playlist still offers Sort by name, Stop and Quit", "[tray]")
{
    const StationList stations;
    const MenuModel model{stations};

    const std::vector<MenuEntry> entries = model.entries(std::nullopt);

    REQUIRE(entries.size() == 4);
    CHECK(entries[0].is_separator);
    CHECK(model.action_of(entries[1].id).kind == MenuAction::Kind::toggle_sort);
    CHECK(model.action_of(entries[2].id).kind == MenuAction::Kind::stop);
    CHECK(model.action_of(entries[3].id).kind == MenuAction::Kind::quit);
}

// ---------------------------------------------------------------- sorting

namespace {

StationList four_stations()
{
    StationList list;
    list.add(Station{StationId{"zeta"}, "zeta", "http://example.org/1"}).value();
    list.add(Station{StationId{"alpha"}, "Alpha", "http://example.org/2"}).value();
    list.add(Station{StationId{"jazz"}, "Радио Джаз", "http://example.org/3"}).value();
    list.add(Station{StationId{"beta"}, "beta", "http://example.org/4"}).value();
    return list;
}

std::vector<std::string> labels(const std::vector<MenuEntry>& entries, std::size_t count)
{
    std::vector<std::string> result;
    for (std::size_t i = 0; i < count; ++i) {
        result.push_back(entries[i].label);
    }
    return result;
}

}  // namespace

TEST_CASE("the sort entry starts unchecked and flips with each toggle", "[tray]")
{
    const StationList stations = two_stations();
    MenuModel model{stations};

    CHECK_FALSE(model.sorted_by_name());
    CHECK(model.entries(std::nullopt)[3].label == "\xE2\x98\x90 Sort by name");
    CHECK_FALSE(model.entries(std::nullopt)[3].is_checked);

    model.toggle_sort_by_name();

    CHECK(model.sorted_by_name());
    CHECK(model.entries(std::nullopt)[3].label == "\xE2\x98\x91 Sort by name");
    CHECK(model.entries(std::nullopt)[3].is_checked);

    model.toggle_sort_by_name();

    CHECK_FALSE(model.sorted_by_name());
    CHECK(model.entries(std::nullopt)[3].label == "\xE2\x98\x90 Sort by name");
}

TEST_CASE("sorting puts the stations in name order, ignoring ASCII case", "[tray]")
{
    const StationList stations = four_stations();
    MenuModel model{stations};

    CHECK(labels(model.entries(std::nullopt), 4) ==
          std::vector<std::string>{"zeta", "Alpha", "Радио Джаз", "beta"});

    model.toggle_sort_by_name();

    // Non-ASCII sorts after ASCII: these are UTF-8 bytes, so it is code point
    // order above 0x7f.
    CHECK(labels(model.entries(std::nullopt), 4) ==
          std::vector<std::string>{"Alpha", "beta", "zeta", "Радио Джаз"});

    model.toggle_sort_by_name();

    CHECK(labels(model.entries(std::nullopt), 4) ==
          std::vector<std::string>{"zeta", "Alpha", "Радио Джаз", "beta"});
}

TEST_CASE("an id still means the same station once the menu is sorted", "[tray]")
{
    const StationList stations = four_stations();
    MenuModel model{stations};

    const std::vector<MenuEntry> in_file_order = model.entries(std::nullopt);

    model.toggle_sort_by_name();
    const std::vector<MenuEntry> by_name = model.entries(std::nullopt);

    // The ids travel with their stations rather than with the menu position,
    // so a host acting on a layout it cached before the flip still starts the
    // station the user picked.
    for (const MenuEntry& before : in_file_order) {
        CHECK(model.action_of(before.id).station == MenuModel{stations}
                                                        .action_of(before.id)
                                                        .station);
    }

    CHECK(model.action_of(by_name[0].id).station.str() == "alpha");
    CHECK(model.action_of(by_name[3].id).station.str() == "jazz");

    // The tail keeps its ids as well.
    CHECK(model.action_of(by_name[5].id).kind == MenuAction::Kind::toggle_sort);
    CHECK(model.action_of(by_name[6].id).kind == MenuAction::Kind::stop);
    CHECK(model.action_of(by_name[7].id).kind == MenuAction::Kind::quit);
}

TEST_CASE("sorting does not disturb the play glyph", "[tray]")
{
    const StationList stations = four_stations();
    MenuModel model{stations};

    model.toggle_sort_by_name();
    const std::vector<MenuEntry> entries = model.entries(StationId{"zeta"});

    REQUIRE(entries[2].label == "\xE2\x96\xB6 zeta");
    CHECK(entries[2].is_current);

    std::size_t marked = 0;
    for (const MenuEntry& entry : entries) {
        marked += entry.is_current ? 1u : 0u;
    }
    CHECK(marked == 1);
}

TEST_CASE("two stations of the same name keep their file order when sorted", "[tray]")
{
    StationList stations;
    stations.add(Station{StationId{"b"}, "Same", "http://example.org/b"}).value();
    stations.add(Station{StationId{"a"}, "Same", "http://example.org/a"}).value();

    MenuModel model{stations};
    model.toggle_sort_by_name();

    const std::vector<MenuEntry> entries = model.entries(std::nullopt);
    CHECK(model.action_of(entries[0].id).station.str() == "b");
    CHECK(model.action_of(entries[1].id).station.str() == "a");
}

TEST_CASE("the sort entry can be left out of the menu altogether", "[tray]")
{
    // What --sort-by-name asks for: it sorted the playlist itself, so the
    // menu shows that order and offers no switch for it.
    const StationList stations = four_stations();
    const MenuModel model{stations, false};

    CHECK_FALSE(model.offers_sort());
    CHECK_FALSE(model.sorted_by_name());

    const std::vector<MenuEntry> entries = model.entries(std::nullopt);

    REQUIRE(entries.size() == 7);
    CHECK(labels(entries, 4) ==
          std::vector<std::string>{"zeta", "Alpha", "Радио Джаз", "beta"});
    CHECK(entries[4].is_separator);
    CHECK(entries[5].label == "Stop");
    CHECK(entries[6].label == "Quit");

    for (const MenuEntry& entry : entries) {
        CHECK(entry.label.find("Sort by name") == std::string::npos);
    }
}

TEST_CASE("with the entry gone nothing can flip the order", "[tray]")
{
    const StationList stations = four_stations();
    MenuModel model{stations, false};

    // The id is still spoken for, so that Stop and Quit do not move with the
    // flag - but it stands for nothing and a host sending it gets nothing.
    const std::vector<MenuEntry> entries = model.entries(std::nullopt);
    const std::int32_t sort_id = entries[5].id - 1;
    CHECK(model.action_of(sort_id).kind == MenuAction::Kind::none);

    model.toggle_sort_by_name();

    CHECK_FALSE(model.sorted_by_name());
    CHECK(labels(model.entries(std::nullopt), 4) ==
          std::vector<std::string>{"zeta", "Alpha", "Радио Джаз", "beta"});
}
