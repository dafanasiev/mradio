#pragma once

#include <functional>
#include <thread>

namespace mradio::app {

// Turns SIGINT and SIGTERM into one call of a normal function.
//
// Doing this with a plain signal handler is not an option: almost nothing is
// safe to call from one, least of all leaving a D-Bus event loop. Instead the
// signals are blocked in every thread and a dedicated thread waits for them
// with sigwait, which runs in ordinary context where any call is fine.
//
// Construct this before starting any other thread, so that every thread
// inherits the blocked mask and the waiting thread is the only one that can
// receive these signals.
class SignalWaiter {
public:
    explicit SignalWaiter(std::function<void()> on_signal);
    ~SignalWaiter();

    SignalWaiter(const SignalWaiter&) = delete;
    SignalWaiter& operator=(const SignalWaiter&) = delete;

private:
    std::function<void()> on_signal_;
    std::thread thread_;
};

}  // namespace mradio::app
