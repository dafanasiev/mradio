#include "mradio/core/error.hpp"

namespace mradio::core {

std::string_view to_string(Errc code) noexcept
{
    switch (code) {
        case Errc::ok:               return "ok";
        case Errc::invalid_argument: return "invalid argument";
        case Errc::not_found:        return "not found";
        case Errc::already_exists:   return "already exists";
        case Errc::parse_error:      return "parse error";
        case Errc::io_error:         return "I/O error";
        case Errc::backend_error:    return "backend error";
    }
    return "unknown error";
}

}  // namespace mradio::core
