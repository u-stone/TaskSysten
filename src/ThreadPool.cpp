#include "ThreadPool.h"
#include "Logger.h"

namespace task_engine {

ThreadPool::ThreadPool(const ThreadPoolConfig& config)
    : config_(config),
      stop_flag_(false),
      current_threads_count_(0),
      last_spawn_time_(std::chrono::steady_clock::now())
{
    config_.max_threads = std::max(config_.min_threads, config_.max_threads); // Ensure max >= min
    // Initialize worker threads up to min_threads
    for (size_t i = 0; i < config_.min_threads; ++i) {
        threads_.emplace_back(&ThreadPool::worker_thread, this);
        current_threads_count_++;
    }
}

ThreadPool::~ThreadPool() {
    LOG_INFO() << "ThreadPool shutting down. Stopping " << threads_.size() << " threads...";
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_flag_ = true; // Signal all threads to stop
    }
    condition_.notify_all(); // Wake up all waiting threads

    // Join all threads to ensure they complete their current task and exit cleanly
    for (std::thread& worker : threads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    LOG_INFO() << "ThreadPool destroyed. All threads joined.";
}

void ThreadPool::submit(std::function<void()> task, TaskPriority priority) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        auto now = std::chrono::steady_clock::now();
        tasks_queue_.push_back({std::move(task), now, priority}); // Add the task to the queue with timestamp
        std::push_heap(tasks_queue_.begin(), tasks_queue_.end());

        // Dynamic sizing logic: Check latency of the oldest task
        if (current_threads_count_ < config_.max_threads) {
            auto time_since_last_spawn = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_spawn_time_).count();

            bool should_grow = false;
            if (static_cast<size_t>(time_since_last_spawn) > config_.cooldown_ms) {
                if (config_.strategy == ScalingStrategy::QUEUE_LENGTH) {
                    if (tasks_queue_.size() > config_.queue_length_threshold) {
                        should_grow = true;
                    }
                } else { // WAIT_TIME
                    auto oldest_task_time = tasks_queue_.front().enqueue_time; // front() is the max element (highest priority)
                    auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest_task_time).count();
                    if (static_cast<size_t>(wait_duration) > config_.max_wait_time_ms) {
                        should_grow = true;
                    }
                }
            }

            if (should_grow) {
                threads_.emplace_back(&ThreadPool::worker_thread, this);
                current_threads_count_++;
                last_spawn_time_ = now;
            }
        }
    }
    condition_.notify_one(); // Notify one waiting thread that a new task is available
}

void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // Wait until there is a task or the stop flag is set
            condition_.wait(lock, [this] {
                return stop_flag_ || !tasks_queue_.empty();
            });

            // If stopping and no more tasks, exit the thread
            if (stop_flag_ && tasks_queue_.empty()) {
                current_threads_count_--; // Decrement count as this thread is exiting
                return;
            }

            std::pop_heap(tasks_queue_.begin(), tasks_queue_.end());
            task = std::move(tasks_queue_.back().task); // Retrieve the task
            tasks_queue_.pop_back(); // Remove it from the queue
        }

        // Execute the task
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                LOG_ERROR() << "ThreadPool task threw exception: " << e.what();
            } catch (...) {
                LOG_ERROR() << "ThreadPool task threw unknown exception.";
            }
        }
    }
}

} // namespace task_engine