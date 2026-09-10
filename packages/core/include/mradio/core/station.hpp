#pragma once

#include "mradio/core/error.hpp"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mradio::core {

// A short, readable key for one station.
//
// The tray menu and the D-Bus API both need to name a station in a way that is
// stable for the lifetime of the process and pleasant to type in busctl, which
// a bare list index is not. Nothing is persisted between runs, so an id only
// has to be unique within one load of the playlist.
class StationId {
public:
    StationId() = default;

    explicit StationId(std::string value) : value_(std::move(value)) {}

    // Derives an id from a display name: ASCII letters are lowercased, ASCII
    // runs that are neither letters nor digits collapse to a single '-', and
    // leading and trailing '-' are trimmed.
    //
    // Bytes at or above 0x80 are passed through untouched, so a Cyrillic or
    // otherwise non-ASCII station name keeps its characters instead of
    // slugifying down to nothing. Case folding those would need Unicode
    // tables for no benefit here, so it is not attempted.
    //
    // Returns nullopt when nothing usable is left, e.g. a name of only
    // punctuation.
    static std::optional<StationId> from_name(std::string_view name);

    [[nodiscard]] const std::string& str() const noexcept { return value_; }
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }

    friend bool operator==(const StationId&, const StationId&) = default;
    friend auto operator<=>(const StationId&, const StationId&) = default;

private:
    std::string value_;
};

// One entry of the user's playlist, in the form the rest of the app uses.
struct Station {
    StationId id;
    std::string name;  // what the tray menu shows
    std::string url;   // handed to the audio engine as-is

    // Checks the invariants everything downstream relies on. It does not, and
    // cannot, say whether the URL actually resolves to a stream.
    [[nodiscard]] Status validate() const;
};

// The playlist, in file order.
//
// Order is meaningful: it is what the tray menu shows, so it follows the file
// rather than being sorted. Lookup by id is O(1) because the tray and the
// D-Bus layer both resolve ids on every user action.
class StationList {
public:
    using const_iterator = std::vector<Station>::const_iterator;

    // Fails on an invalid station or an id already present.
    Status add(Station station);

    [[nodiscard]] const Station* find(const StationId& id) const noexcept;
    [[nodiscard]] bool contains(const StationId& id) const noexcept;

    // Position in file order, which is what stepping to the next or previous
    // station works on.
    [[nodiscard]] std::optional<std::size_t> index_of(const StationId& id) const noexcept;

    [[nodiscard]] const Station& operator[](std::size_t index) const { return stations_[index]; }
    [[nodiscard]] std::size_t size() const noexcept { return stations_.size(); }
    [[nodiscard]] bool empty() const noexcept { return stations_.empty(); }

    [[nodiscard]] const_iterator begin() const noexcept { return stations_.begin(); }
    [[nodiscard]] const_iterator end() const noexcept { return stations_.end(); }

private:
    std::vector<Station> stations_;
    std::unordered_map<std::string, std::size_t> by_id_;
};

}  // namespace mradio::core

template <>
struct std::hash<mradio::core::StationId> {
    std::size_t operator()(const mradio::core::StationId& id) const noexcept
    {
        return std::hash<std::string>{}(id.str());
    }
};
