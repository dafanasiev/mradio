#include "mradio/core/station.hpp"

#include <algorithm>
#include <utility>

namespace mradio::core {

bool name_precedes(std::string_view lhs, std::string_view rhs) noexcept
{
    const auto folded = [](char c) {
        const auto ch = static_cast<unsigned char>(c);
        return (ch >= 'A' && ch <= 'Z') ? static_cast<unsigned char>(ch - 'A' + 'a') : ch;
    };

    return std::lexicographical_compare(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
        [&](char a, char b) { return folded(a) < folded(b); });
}

std::optional<StationId> StationId::from_name(std::string_view name)
{
    std::string slug;
    slug.reserve(name.size());

    // Set when a separator run is seen; the dash is only emitted once a
    // keepable character follows it, which drops trailing and leading dashes
    // and collapses runs without a second pass.
    bool pending_dash = false;

    for (const char byte : name) {
        // Explicit: char is signed here, and the classification below
        // relies on non-ASCII bytes comparing as >= 0x80.
        const auto ch = static_cast<unsigned char>(byte);

        char mapped = 0;
        if (ch >= 0x80) {
            mapped = static_cast<char>(ch);  // non-ASCII byte, passed through
        }
        else if (ch >= 'A' && ch <= 'Z') {
            mapped = static_cast<char>(ch - 'A' + 'a');
        }
        else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            mapped = static_cast<char>(ch);
        }
        else {
            pending_dash = !slug.empty();
            continue;
        }

        if (pending_dash) {
            slug.push_back('-');
            pending_dash = false;
        }
        slug.push_back(mapped);
    }

    if (slug.empty()) {
        return std::nullopt;
    }
    return StationId{std::move(slug)};
}

Status Station::validate() const
{
    if (id.empty()) {
        return fail(Errc::invalid_argument, "station has no id");
    }
    if (name.empty()) {
        return fail(Errc::invalid_argument, "station '" + id.str() + "' has no name");
    }
    if (url.empty()) {
        return fail(Errc::invalid_argument, "station '" + id.str() + "' has no URL");
    }
    return {};
}

Status StationList::add(Station station)
{
    if (const Status valid = station.validate(); !valid) {
        return valid;
    }

    // Taken before the move: station.id is not readable afterwards.
    std::string key = station.id.str();

    if (by_id_.contains(key)) {
        return fail(Errc::already_exists, "duplicate station id '" + key + "'");
    }

    by_id_.emplace(std::move(key), stations_.size());
    stations_.push_back(std::move(station));
    return {};
}

void StationList::sort_by_name()
{
    // Stable, so two stations sharing a name keep their file order.
    std::stable_sort(stations_.begin(), stations_.end(), [](const Station& a, const Station& b) {
        return name_precedes(a.name, b.name);
    });

    // by_id_ maps an id to a position, and every position just moved.
    for (std::size_t index = 0; index < stations_.size(); ++index) {
        by_id_[stations_[index].id.str()] = index;
    }
}

const Station* StationList::find(const StationId& id) const noexcept
{
    const auto it = by_id_.find(id.str());
    if (it == by_id_.end()) {
        return nullptr;
    }
    return &stations_[it->second];
}

bool StationList::contains(const StationId& id) const noexcept
{
    return by_id_.contains(id.str());
}

std::optional<std::size_t> StationList::index_of(const StationId& id) const noexcept
{
    const auto it = by_id_.find(id.str());
    if (it == by_id_.end()) {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace mradio::core
