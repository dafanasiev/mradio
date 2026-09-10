#pragma once

#include <compare>

namespace mradio::core {

// Playback volume, normalised to [0, 1] and clamped on construction.
//
// The normalisation is the point of this type. MPRIS speaks 0..1, mpv speaks
// 0..100, and the tray adjusts by percentage-point steps on a scroll event;
// with three scales in play a bare double is a standing invitation to a
// hundredfold bug. Conversions happen only at the edges, through the named
// accessors below.
class Volume {
public:
    constexpr Volume() noexcept = default;

    constexpr explicit Volume(double normalised) noexcept
        : value_(clamp(normalised))
    {
    }

    static constexpr Volume from_percent(double percent) noexcept
    {
        return Volume{percent / 100.0};
    }

    [[nodiscard]] constexpr double normalised() const noexcept { return value_; }
    [[nodiscard]] constexpr double percent() const noexcept { return value_ * 100.0; }

    [[nodiscard]] constexpr bool is_silent() const noexcept { return value_ <= 0.0; }

    // Returns this volume shifted by `delta` (in normalised units), clamped.
    // Used by the tray's scroll handler, which nudges by a fixed step.
    [[nodiscard]] constexpr Volume adjusted(double delta) const noexcept
    {
        return Volume{value_ + delta};
    }

    friend constexpr bool operator==(const Volume&, const Volume&) = default;
    friend constexpr auto operator<=>(const Volume&, const Volume&) = default;

private:
    static constexpr double clamp(double v) noexcept
    {
        // Also folds NaN to 0: NaN fails both comparisons, and letting it
        // through would poison mpv's volume property.
        if (!(v > 0.0)) {
            return 0.0;
        }
        return v > 1.0 ? 1.0 : v;
    }

    double value_ = 1.0;
};

}  // namespace mradio::core
