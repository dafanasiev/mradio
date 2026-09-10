#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace mradio::core {

enum class Errc {
    ok = 0,
    invalid_argument,
    not_found,
    already_exists,
    parse_error,
    io_error,
    backend_error,
};

std::string_view to_string(Errc code) noexcept;

// A machine-readable code plus a human-readable message.
//
// Deliberately not std::error_code: the failures in this program are one-off
// diagnostics coming out of mpv, D-Bus and a hand-edited playlist file, and
// they are shown to a person rather than dispatched on. A category-based
// taxonomy would be ceremony without a payoff.
struct Error {
    Errc code = Errc::ok;
    std::string message;

    friend bool operator==(const Error&, const Error&) = default;
};

inline Error make_error(Errc code, std::string message)
{
    return Error{code, std::move(message)};
}

template <typename T>
using Result = std::expected<T, Error>;

using Status = std::expected<void, Error>;

// Shorthand for the failure side of a Result/Status.
inline std::unexpected<Error> fail(Errc code, std::string message)
{
    return std::unexpected(make_error(code, std::move(message)));
}

}  // namespace mradio::core
