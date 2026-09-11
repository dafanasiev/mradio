#pragma once

#include <functional>
#include <thread>

namespace mradio::app {

// Blocks SIGINT and SIGTERM in the calling thread.
//
// This has to be the first thing main() does, before anything at all creates a
// thread. A thread inherits the mask of whoever created it, and a
// process-directed signal is delivered to any one thread that does not block
// it - so a single unblocked thread is enough for the default disposition to
// kill the process instead of the waiter below seeing the signal. libmpv alone
// brings up a dozen threads, which is exactly how this went wrong while the
// blocking lived in SignalWaiter's constructor: the waiter was built last, and
// SIGTERM ended the process with 143 and no destructor run.
//
// A signal arriving between this call and the SignalWaiter is not lost: it is
// blocked, so it stays pending until the waiter starts and picks it up.
void block_termination_signals();

// Turns SIGINT and SIGTERM into one call of a normal function.
//
// Doing this with a plain signal handler is not an option: almost nothing is
// safe to call from one, least of all leaving a D-Bus event loop. Instead the
// signals are blocked (above) and a dedicated thread waits for them with
// sigwait, which runs in ordinary context where any call is fine.
class SignalWaiter {
public:
    // Blocks the signals in the calling thread as well, since sigwait requires
    // it - but that is not a substitute for block_termination_signals() having
    // been called before the program's other threads existed.
    explicit SignalWaiter(std::function<void()> on_signal);
    ~SignalWaiter();

    SignalWaiter(const SignalWaiter&) = delete;
    SignalWaiter& operator=(const SignalWaiter&) = delete;

private:
    std::function<void()> on_signal_;
    std::thread thread_;
};

}  // namespace mradio::app
