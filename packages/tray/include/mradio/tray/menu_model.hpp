#pragma once

#include "mradio/core/station.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mradio::tray {

// What activating a menu item should do.
struct MenuAction {
    enum class Kind { none, play, stop, quit, toggle_sort };

    Kind kind = Kind::none;
    core::StationId station;  // meaningful only when kind == play
};

struct MenuEntry {
    std::int32_t id = 0;
    std::string label;
    bool is_separator = false;
    bool is_current = false;  // the station that is on right now
    bool is_checked = false;  // the check mark on a toggle entry
};

// The tray menu, as plain data.
//
// Deliberately free of D-Bus types, so the parts that are easy to get wrong -
// which id means what, and which label carries the play glyph - can be tested
// without a bus.
//
// Ids are assigned once and never move: a station keeps the id its place in
// the file gave it, whatever order the menu is shown in. The playlist is read
// at startup and nothing adds to it, so a host that cached the layout can keep
// acting on the ids it already has - and "Sort by name" reorders the entries
// it hands back without ever making a cached id mean a different station.
//
// Not thread-safe, and does not need to be: every dbusmenu method arrives on
// the D-Bus loop thread, which is the only thread that reads or toggles this.
class MenuModel {
public:
    // The station list must outlive this model; it is owned by the controller,
    // which outlives the tray.
    explicit MenuModel(const core::StationList& stations);

    // The menu in display order: every station, a separator, Sort by name,
    // Stop, Quit.
    [[nodiscard]] std::vector<MenuEntry> entries(
        const std::optional<core::StationId>& playing) const;

    [[nodiscard]] MenuAction action_of(std::int32_t id) const;

    // Whether the stations come out sorted by name rather than in file order.
    // A view concern only: nothing below the tray is reordered, so nothing
    // that is playing is disturbed by the flip.
    [[nodiscard]] bool sorted_by_name() const noexcept { return sorted_by_name_; }
    void toggle_sort_by_name() noexcept { sorted_by_name_ = !sorted_by_name_; }

    [[nodiscard]] static std::int32_t root_id() noexcept { return 0; }

private:
    const core::StationList& stations_;

    // Indices into stations_, by name. Computed once: the playlist does not
    // change, so neither does the order it sorts into.
    std::vector<std::size_t> by_name_;

    std::int32_t separator_id_;
    std::int32_t sort_id_;
    std::int32_t stop_id_;
    std::int32_t quit_id_;

    bool sorted_by_name_ = false;
};

}  // namespace mradio::tray
