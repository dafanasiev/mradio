#include "mradio/tray/menu_model.hpp"

#include <utility>

namespace mradio::tray {
namespace {

// U+25B6 BLACK RIGHT-POINTING TRIANGLE, then a space. Prepended to the label
// of the station that is on, which is the only playback indicator this program
// has - there is no window to show state in.
constexpr std::string_view kPlayingGlyph = "\xE2\x96\xB6 ";

}  // namespace

MenuModel::MenuModel(const core::StationList& stations)
    : stations_(stations),
      separator_id_(static_cast<std::int32_t>(stations.size()) + 1),
      stop_id_(separator_id_ + 1),
      quit_id_(stop_id_ + 1)
{
}

std::vector<MenuEntry> MenuModel::entries(const std::optional<core::StationId>& playing) const
{
    std::vector<MenuEntry> result;
    result.reserve(stations_.size() + 3);

    std::int32_t id = 1;  // 0 is the root; stations start at 1
    for (const core::Station& station : stations_) {
        const bool current = playing.has_value() && *playing == station.id;

        result.push_back(MenuEntry{
            .id = id,
            .label = current ? std::string{kPlayingGlyph} + station.name : station.name,
            .is_separator = false,
            .is_current = current,
        });
        ++id;
    }

    result.push_back(MenuEntry{.id = separator_id_, .label = {}, .is_separator = true});
    result.push_back(MenuEntry{.id = stop_id_, .label = "Стоп"});
    result.push_back(MenuEntry{.id = quit_id_, .label = "Выйти"});

    return result;
}

MenuAction MenuModel::action_of(std::int32_t id) const
{
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
