#include "mradio/ipc/bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <optional>
#include <string>
#include <thread>

using mradio::ipc::Bus;

namespace {

// Starts a dbus-daemon of its own and points DBUS_SESSION_BUS_ADDRESS at it.
//
// A private bus, never the developer's real session bus: these tests claim
// well-known names and stop event loops, and doing that on a live desktop
// session would be rude at best.
class PrivateSessionBus {
public:
    PrivateSessionBus()
    {
        if (const char* const previous = std::getenv(kAddressVar)) {
            saved_address_ = previous;
        }

        // --fork daemonises, so the command exits as soon as it has printed
        // the address and the pid, in that order.
        std::FILE* const pipe =
            ::popen("dbus-daemon --session --print-address --print-pid --fork 2>/dev/null", "r");
        REQUIRE(pipe != nullptr);

        address_ = read_line(pipe);
        const std::string pid_text = read_line(pipe);
        ::pclose(pipe);

        REQUIRE_FALSE(address_.empty());
        pid_ = std::stoi(pid_text);

        ::setenv(kAddressVar, address_.c_str(), 1);
    }

    ~PrivateSessionBus()
    {
        if (pid_ > 0) {
            ::kill(pid_, SIGTERM);
        }
        if (saved_address_) {
            ::setenv(kAddressVar, saved_address_->c_str(), 1);
        }
        else {
            ::unsetenv(kAddressVar);
        }
    }

    PrivateSessionBus(const PrivateSessionBus&) = delete;
    PrivateSessionBus& operator=(const PrivateSessionBus&) = delete;

private:
    static constexpr const char* kAddressVar = "DBUS_SESSION_BUS_ADDRESS";

    static std::string read_line(std::FILE* pipe)
    {
        std::string line;
        int ch = 0;
        while ((ch = std::fgetc(pipe)) != EOF && ch != '\n') {
            line.push_back(static_cast<char>(ch));
        }
        return line;
    }

    std::string address_;
    std::optional<std::string> saved_address_;
    int pid_ = 0;
};

}  // namespace

TEST_CASE("connects to the session bus and claims a name", "[ipc]")
{
    const PrivateSessionBus daemon;

    auto bus = Bus::connect_session();
    REQUIRE(bus.has_value());

    CHECK((*bus)->request_name("org.mradio.test").has_value());
}

TEST_CASE("a name another connection already holds is refused", "[ipc]")
{
    const PrivateSessionBus daemon;

    auto first = Bus::connect_session();
    REQUIRE(first.has_value());
    REQUIRE((*first)->request_name("org.mradio.test").has_value());

    // This is how a second instance of the program is turned away.
    auto second = Bus::connect_session();
    REQUIRE(second.has_value());

    const auto claimed = (*second)->request_name("org.mradio.test");

    REQUIRE_FALSE(claimed.has_value());
    CHECK_FALSE(claimed.error().message.empty());
}

TEST_CASE("the loop runs until it is stopped from another thread", "[ipc]")
{
    const PrivateSessionBus daemon;

    auto bus = Bus::connect_session();
    REQUIRE(bus.has_value());

    std::thread stopper{[&bus] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        (*bus)->stop();
    }};

    // Returns only because stop() was called; a hang here fails the test by
    // hitting the ctest timeout.
    const auto ran = (*bus)->run();
    stopper.join();

    CHECK(ran.has_value());
}

TEST_CASE("connecting reports failure when there is no bus", "[ipc]")
{
    const PrivateSessionBus daemon;  // saves and restores the real value
    ::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/mradio-no-such-bus", 1);

    const auto bus = Bus::connect_session();

    REQUIRE_FALSE(bus.has_value());
    CHECK_FALSE(bus.error().message.empty());
}
