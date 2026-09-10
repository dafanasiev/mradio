#include "mradio/core/player_state.hpp"

namespace mradio::core {

std::string_view to_string(PlayerState state) noexcept
{
    switch (state) {
        case PlayerState::idle:       return "idle";
        case PlayerState::connecting: return "connecting";
        case PlayerState::playing:    return "playing";
        case PlayerState::failed:     return "failed";
    }
    return "unknown";
}

bool is_active(PlayerState state) noexcept
{
    switch (state) {
        case PlayerState::connecting:
        case PlayerState::playing:
            return true;
        case PlayerState::idle:
        case PlayerState::failed:
            return false;
    }
    return false;
}

}  // namespace mradio::core
