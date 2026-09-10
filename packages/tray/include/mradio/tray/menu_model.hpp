#pragma once

#include "mradio/core/station.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mradio::tray {

// What activating a menu item should do.
struct MenuAction {
    enum class Kind { none, play, stop, quit };

    Kind kind = Kind::none;
    core::StationId station;  // meaningful only when kind == play
};

struct MenuEntry {
    std::int32_t id = 0;
    std::string label;
    bool is_separator = false;
    bool is_current = false;  // the station that is on right now
};

// The tray menu, as plain data.
//
// Deliberately free of D-Bus types, so the parts that are easy to get wrong -
// which id means what, and which label carries the play glyph - can be tested
// without a bus.
//
// Ids are assigned once and never move. The playlist is read at startup and
// does not change while the program runs, so a host that cached the layout can
// keep acting on the ids it already has.
class MenuModel {
public:
    // The station list must outlive this model; it is owned by the controller,
    // which outlives the tray.
    explicit MenuModel(const core::StationList& stations);

    // The menu in display order: every station, a separator, Stop, Quit.
    [[nodiscard]] std::vector<MenuEntry> entries(
        const std::optional<core::StationId>& playing) const;

    [[nodiscard]] MenuAction action_of(std::int32_t id) const;

    [[nodiscard]] static std::int32_t root_id() noexcept { return 0; }

private:
    const core::StationList& stations_;
    std::int32_t separator_id_;
    std::int32_t stop_id_;
    std::int32_t quit_id_;
};

}  // namespace mradio::tray
