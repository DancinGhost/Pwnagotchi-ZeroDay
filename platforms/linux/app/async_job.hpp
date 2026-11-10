#pragma once

// Minimal single-flight background job runner.
//
// The Linux backend shells out to nmcli/iw/airmon-ng/aireplay-ng, which can block for
// anywhere from tens of milliseconds to several seconds. Both front-ends (the Qt GUI and
// the ncurses TUI) drive their whole UI from one periodic tick, so any call that blocks
// that thread freezes the entire interface. `AsyncJob<T>` runs one such call on a worker
// thread and hands its result back via a non-blocking poll, so the UI thread never waits.
//
// Usage contract: all public methods are intended to be called only from a single
// "owner" thread (the UI thread). The worker thread touches nothing but its own captured
// state and this object's internal mutex-guarded slot — it must never read or write any
// shared object state directly; package whatever it needs to report as the returned `T`
// and let the owner thread apply it after collect() returns a value.

#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace zd {
namespace linux_app {

template <typename T>
class AsyncJob {
public:
    AsyncJob() = default;
    AsyncJob(const AsyncJob&) = delete;
    AsyncJob& operator=(const AsyncJob&) = delete;

    ~AsyncJob() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Starts `fn` on a worker thread. Returns false (and starts nothing) if a job from a
    // previous call is still running; otherwise reaps that previous (already-finished)
    // worker before launching the new one.
    template <typename Fn>
    bool start(Fn&& fn) {
        if (running_.load(std::memory_order_acquire)) {
            return false;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this, fn = std::forward<Fn>(fn)]() mutable {
            T value = fn();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                result_ = std::move(value);
            }
            running_.store(false, std::memory_order_release);
        });
        return true;
    }

    bool running() const { return running_.load(std::memory_order_acquire); }

    // Non-blocking: returns (and clears) the result exactly once, or nullopt if the job is
    // still running or there is nothing new to collect.
    std::optional<T> collect() {
        if (running_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!result_.has_value()) {
            return std::nullopt;
        }
        std::optional<T> out = std::move(result_);
        result_.reset();
        return out;
    }

private:
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::optional<T> result_;
};

}  // namespace linux_app
}  // namespace zd
