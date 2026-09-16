#include "mradio/tray/menu_model.hpp"

#include <algorithm>
#include <numeric>
#include <string_view>
#include <utility>

namespace mradio::tray {
namespace {

// U+25B6 BLACK RIGHT-POINTING TRIANGLE, then a space. Prepended to the label
// of the station that is on, which is the only playback indicator this program
// has - there is no window to show state in.
constexpr std::string_view kPlayingGlyph = "\xE2\x96\xB6 ";

// U+2611 BALLOT BOX WITH CHECK and U+2610 BALLOT BOX. dbusmenu does have a
// checkmark of its own ("toggle-type"), but the panel draws that one, and a
// menu whose play mark is a character in the label and whose check mark is not
// would line up differently in every host. One indicator, one mechanism.
constexpr std::string_view kCheckedGlyph = "\xE2\x98\x91 ";
constexpr std::string_view kUncheckedGlyph = "\xE2\x98\x90 ";

}  // namespace

MenuModel::MenuModel(const core::StationList& stations, bool sort_by_name_visible)
    : stations_(stations),
      by_name_(sort_by_name_visible ? stations.size() : 0),
      separator_id_(static_cast<std::int32_t>(stations.size()) + 1),
      sort_id_(separator_id_ + 1),
      stop_id_(sort_id_ + 1),
      quit_id_(stop_id_ + 1),
      sort_by_name_visible_(sort_by_name_visible)
{
    std::iota(by_name_.begin(), by_name_.end(), std::size_t{0});

    // Stable, so two stations sharing a name keep their playlist order.
    std::stable_sort(by_name_.begin(), by_name_.end(), [&](std::size_t a, std::size_t b) {
        return core::name_precedes(stations_[a].name, stations_[b].name);
    });
}

std::vector<MenuEntry> MenuModel::entries(const std::optional<core::StationId>& playing) const
{
    std::vector<MenuEntry> result;
    result.reserve(stations_.size() + 4);

    for (std::size_t position = 0; position < stations_.size(); ++position) {
        // The id follows the station, not its place in the menu.
        const std::size_t index = sorted_by_name_ ? by_name_[position] : position;
        const core::Station& station = stations_[index];
        const bool current = playing.has_value() && *playing == station.id;

        result.push_back(MenuEntry{
            .id = static_cast<std::int32_t>(index) + 1,  // 0 is the root
            .label = current ? std::string{kPlayingGlyph} + station.name : station.name,
            .is_separator = false,
            .is_current = current,
        });
    }

    result.push_back(MenuEntry{.id = separator_id_, .label = {}, .is_separator = true});

    if (sort_by_name_visible_) {
        result.push_back(MenuEntry{
            .id = sort_id_,
            .label =
                std::string{sorted_by_name_ ? kCheckedGlyph : kUncheckedGlyph} + "Sort by name",
            .is_separator = false,
            .is_current = false,
            .is_checked = sorted_by_name_,
        });
    }

    result.push_back(MenuEntry{.id = stop_id_, .label = "Stop"});
    result.push_back(MenuEntry{.id = quit_id_, .label = "Quit"});

    return result;
}

MenuAction MenuModel::action_of(std::int32_t id) const
{
    // sort_id_ keeps its number even when the entry is not visible, so that
    // the ids either side of it do not depend on the flag; nothing shows it,
    // and nothing acts on it.
    if (sort_by_name_visible_ && id == sort_id_) {
        return MenuAction{.kind = MenuAction::Kind::toggle_sort, .station = {}};
    }
    if (id == stop_id_) {
        return MenuAction{.kind = MenuAction::Kind::stop, .station = {}};
    }
    if (id == quit_id_) {
        return MenuAction{.kind = MenuAction::Kind::quit, .station = {}};
    }

    const auto count = static_cast<std::int32_t>(stations_.size());
    if (id >= 1 && id <= count) {
        return MenuAction{
            .kind = MenuAction::Kind::play,
            .station = stations_[static_cast<std::size_t>(id - 1)].id,
        };
    }

    // The root, the separator, or an id from a layout the host cached from an
    // older revision. Doing nothing is the right answer for all of them.
    return {};
}

}  // namespace mradio::tray
