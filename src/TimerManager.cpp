#include "TaskEngine/TimerManager.h"
#include "TaskEngine/Logger.h"

namespace task_engine {
TimerManager::TimerManager() : worker_(&TimerManager::run, this) {}

TimerManager::~TimerManager() {
    stop_ = true;
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

TimerManager::TimerID TimerManager::add_timer(std::chrono::milliseconds delay, std::function<void()> callback) {
    TimerID id = next_id_++;
    auto expiry = std::chrono::steady_clock::now() + delay;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timers_.insert({id, expiry, std::move(callback)});
        id_to_expiry_[id] = expiry;
    }
    cv_.notify_all();
    return id;
}

void TimerManager::cancel_timer(TimerID id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (timers_.empty()) return;

    TimerID head_id = timers_.begin()->id;
    auto it = id_to_expiry_.find(id);
    if (it != id_to_expiry_.end()) {
        // Use expiry and id to construct a search key for O(log N) deletion
        timers_.erase({id, it->second, nullptr});
        id_to_expiry_.erase(it);
        // If we removed the earliest timer, wake the worker to re-calculate wait time
        if (id == head_id) cv_.notify_one();
    }
}

void TimerManager::run() {
    while (!stop_) {
        TimerEntry top;
        bool has_task = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (timers_.empty()) {
                cv_.wait(lock, [this] { return stop_ || !timers_.empty(); });
            } else {
                auto now = std::chrono::steady_clock::now();
                auto it = timers_.begin();
                if (it->expiry <= now) {
                    // Move the entry out of the set
                    top = std::move(const_cast<TimerEntry&>(*it));
                    timers_.erase(it);
                    id_to_expiry_.erase(top.id);
                    has_task = true;
                } else {
                    // Wait until the next timer expires or a new one is added
                    cv_.wait_until(lock, it->expiry);
                    continue;
                }
            }
        }
        
        if (stop_) return;
        
        if (has_task && top.callback) {
            try {
                top.callback();
            } catch (const std::exception& e) {
                LOG_ERROR() << "TimerManager callback threw exception: " << e.what();
            } catch (...) {
                LOG_ERROR() << "TimerManager callback threw unknown exception.";
            }
        }
    }
}
}