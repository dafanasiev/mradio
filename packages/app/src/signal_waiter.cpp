#include "signal_waiter.hpp"

#include <pthread.h>
#include <signal.h>

#include <atomic>
#include <utility>

namespace mradio::app {
namespace {

std::atomic<bool> g_shutting_down{false};

sigset_t termination_signals()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    return mask;
}

}  // namespace

SignalWaiter::SignalWaiter(std::function<void()> on_signal)
    : on_signal_(std::move(on_signal))
{
    sigset_t mask = termination_signals();

    // Blocked here, before any other thread exists, so every thread created
    // later inherits the mask and only the waiter below can receive these.
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);

    thread_ = std::thread([this, mask]() mutable {
        int received = 0;
        while (sigwait(&mask, &received) == 0) {
            if (g_shutting_down.load(std::memory_order_acquire)) {
                return;  // woken by the destructor, not by a real signal
            }
            if (on_signal_) {
                on_signal_();
            }
            return;  // one termination signal is all this needs to act on
        }
    });
}

SignalWaiter::~SignalWaiter()
{
    g_shutting_down.store(true, std::memory_order_release);

    // Wakes the waiter if it is still blocked. Sending to a thread that has
    // already returned is harmless: a thread id stays valid until the thread
    // is joined, which happens on the next line.
    pthread_kill(thread_.native_handle(), SIGTERM);

    if (thread_.joinable()) {
        thread_.join();
    }
}

}  // namespace mradio::app
