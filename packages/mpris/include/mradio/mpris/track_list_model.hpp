#pragma once

#include "mradio/core/station.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mradio::mpris {

// The station list, addressed the way MPRIS addresses tracks.
//
// MPRIS names a track by D-Bus object path, which a StationId cannot be: an id
// is derived from the station's name and keeps whatever non-ASCII bytes that
// name had, while an object path may hold nothing but [A-Za-z0-9_] between its
// slashes. The path therefore carries the station's position in the playlist
// instead, which is well defined because the playlist is read once at startup
// and never changes. The spec asks that an id never be reused for a different
// track, and nothing here ever gets the chance to.
//
// Deliberately free of D-Bus types, so the part that is easy to get wrong -
// which path means which station, and what to do with a path that means none -
// can be tested without a bus.
class TrackListModel {
public:
    // The station list must outlive this model; it is owned by the view model,
    // which outlives the MPRIS service.
    explicit TrackListModel(const core::StationList& stations) noexcept;

    // Every station's track id, in playlist order: the Tracks property.
    [[nodiscard]] std::vector<std::string> track_ids() const;

    // The track id of one station, or nullopt when it is not in the playlist.
    [[nodiscard]] std::optional<std::string> id_of(const core::StationId& station) const;

    // The station a track id names, or nullopt for anything else - the
    // reserved no_track() path, a path from some other player, an index past
    // the end of the playlist. Every method that takes a TrackId from a client
    // has to cope with all three.
    [[nodiscard]] std::optional<core::StationId> station_of(std::string_view track_id) const;

    // The path MPRIS reserves for "there is no track". It names no station, so
    // station_of() turns it down like any other stranger.
    [[nodiscard]] static std::string_view no_track() noexcept;

private:
    const core::StationList& stations_;
};

}  // namespace mradio::mpris
