#include "mradio/mpris/track_list_model.hpp"

#include <charconv>
#include <cstddef>
#include <system_error>

namespace mradio::mpris {
namespace {

// Everything in a track id before the station's index. It sits under mradio's
// own path rather than under /org/mpris/MediaPlayer2/TrackList/, which the
// spec spends on the one reserved name below.
constexpr std::string_view kPrefix = "/org/mpris/MediaPlayer2/mradio/station/";

constexpr std::string_view kNoTrack = "/org/mpris/MediaPlayer2/TrackList/NoTrack";

std::string id_for_index(std::size_t index)
{
    return std::string{kPrefix} + std::to_string(index);
}

}  // namespace

TrackListModel::TrackListModel(const core::StationList& stations) noexcept
    : stations_(stations)
{
}

std::string_view TrackListModel::no_track() noexcept
{
    return kNoTrack;
}

std::vector<std::string> TrackListModel::track_ids() const
{
    std::vector<std::string> ids;
    ids.reserve(stations_.size());

    for (std::size_t index = 0; index < stations_.size(); ++index) {
        ids.push_back(id_for_index(index));
    }

    return ids;
}

std::optional<std::string> TrackListModel::id_of(const core::StationId& station) const
{
    const std::optional<std::size_t> index = stations_.index_of(station);
    if (!index) {
        return std::nullopt;
    }

    return id_for_index(*index);
}

std::optional<core::StationId> TrackListModel::station_of(std::string_view track_id) const
{
    if (!track_id.starts_with(kPrefix)) {
        return std::nullopt;
    }

    const std::string_view digits = track_id.substr(kPrefix.size());

    // from_chars turns down a sign and leading space on its own; a leading
    // zero it would accept, and that would be a second id for a station that
    // already has one. An id has to be exactly what track_ids() handed out.
    if (digits.empty() || (digits.size() > 1 && digits.front() == '0')) {
        return std::nullopt;
    }

    std::size_t index = 0;
    const auto [stopped_at, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), index);

    if (error != std::errc{} || stopped_at != digits.data() + digits.size()) {
        return std::nullopt;
    }

    if (index >= stations_.size()) {
        return std::nullopt;
    }

    return stations_[index].id;
}

}  // namespace mradio::mpris
