#include "mradio/ipc/bus.hpp"

#include <string>
#include <utility>

namespace mradio::ipc {

core::Error from_sdbus(const sdbus::Error& error)
{
    std::string message = error.getName();
    if (!error.getMessage().empty()) {
        message += ": " + error.getMessage();
    }
    return core::make_error(core::Errc::backend_error, std::move(message));
}

core::Result<std::unique_ptr<Bus>> Bus::connect_session()
{
    try {
        // No name requested here: names are claimed explicitly afterwards, so
        // a failure to claim one is reported separately from a failure to
        // reach the bus at all.
        return std::unique_ptr<Bus>{new Bus{sdbus::createSessionBusConnection()}};
    }
    catch (const sdbus::Error& error) {
        return std::unexpected(from_sdbus(error));
    }
}

Bus::Bus(std::unique_ptr<sdbus::IConnection> connection)
    : connection_(std::move(connection))
{
}

Bus::~Bus() = default;

core::Status Bus::request_name(std::string_view name)
{
    try {
        connection_->requestName(sdbus::ServiceName{std::string{name}});
        return {};
    }
    catch (const sdbus::Error& error) {
        return std::unexpected(from_sdbus(error));
    }
}

core::Status Bus::run()
{
    try {
        connection_->enterEventLoop();
        return {};
    }
    catch (const sdbus::Error& error) {
        return std::unexpected(from_sdbus(error));
    }
}

void Bus::stop() noexcept
{
    try {
        connection_->leaveEventLoop();
    }
    catch (const sdbus::Error&) {
        // Nothing useful to do: either the loop is already gone or the
        // connection is dead, and both mean the caller's goal is met.
    }
}

}  // namespace mradio::ipc
