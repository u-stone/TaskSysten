#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <set>
#include <thread>
#include <atomic>
#include <unordered_map>

namespace task_engine {
class TimerManager {
public:
    using TimerID = uint64_t;
    TimerManager();
    ~TimerManager();

    // Adds a timer and returns a unique ID for cancellation
    TimerID add_timer(std::chrono::milliseconds delay, std::function<void()> callback);
    
    // Cancels a timer by its ID
    void cancel_timer(TimerID id);

private:
    struct TimerEntry {
        TimerID id;
        std::chrono::steady_clock::time_point expiry;
        std::function<void()> callback;

        // Sorted by expiry time, then by ID for uniqueness in the set
        bool operator<(const TimerEntry& other) const {
            if (expiry != other.expiry) return expiry < other.expiry;
            return id < other.id;
        }
    };

    void run();
    std::set<TimerEntry> timers_;
    std::unordered_map<TimerID, std::chrono::steady_clock::time_point> id_to_expiry_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::atomic<bool> stop_{false};
    std::atomic<TimerID> next_id_{1};
};
}