#pragma once

#include "mradio/core/error.hpp"

#include <sdbus-c++/sdbus-c++.h>

#include <memory>
#include <string_view>

namespace mradio::ipc {

// Converts an sdbus exception into our error type.
//
// The D-Bus error name (org.freedesktop.DBus.Error.*) is kept in the message
// because it is usually the only part that says what actually went wrong -
// "NameHasNoOwner" versus "AccessDenied" versus "ServiceUnknown".
core::Error from_sdbus(const sdbus::Error& error);

// Owns the session-bus connection and its I/O loop.
//
// Threading, as sdbus-c++ defines it: IConnection is "thread-aware, but not
// thread-safe", so this object is driven from a single thread - connect,
// request names, register objects, then run(). stop() is the one exception; it
// exists to be called from elsewhere.
//
// Emitting signals is a separate matter and is safe: sdbus-c++ documents
// IObject signal emission as thread-safe by design. That is what lets the mpv
// event thread publish PropertiesChanged without marshalling onto this loop.
class Bus {
public:
    static core::Result<std::unique_ptr<Bus>> connect_session();

    ~Bus();

    Bus(const Bus&) = delete;
    Bus& operator=(const Bus&) = delete;

    [[nodiscard]] sdbus::IConnection& connection() noexcept { return *connection_; }

    // Claims a well-known name. Fails if another process already holds it,
    // which is how a second instance of the program is turned away.
    core::Status request_name(std::string_view name);

    // Runs the I/O loop until stop(). This is the program's main loop.
    core::Status run();

    // Unblocks run(). Safe from a D-Bus callback (it runs on the loop thread)
    // and from another thread, which is what a termination signal needs.
    void stop() noexcept;

private:
    explicit Bus(std::unique_ptr<sdbus::IConnection> connection);

    std::unique_ptr<sdbus::IConnection> connection_;
};

}  // namespace mradio::ipc
