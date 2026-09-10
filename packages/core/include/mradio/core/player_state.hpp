#pragma once

#include <string_view>

namespace mradio::core {

enum class PlayerState {
    idle,        // nothing loaded
    connecting,  // stream opened, still filling the buffer - no audio yet
    playing,
    failed,      // the last attempt gave up; PlaybackStatus::error has the detail
};

// There is deliberately no paused state. Pausing a live stream only means
// throwing away buffered audio and rejoining later, so this player offers
// play and stop and nothing in between.

std::string_view to_string(PlayerState state) noexcept;

// True while a station is on, whether or not audio is flowing yet.
// The tray marks such a station with its play glyph, so that clicking a
// station gives immediate feedback instead of waiting out the connection.
bool is_active(PlayerState state) noexcept;

}  // namespace mradio::core
