#include "ThreadPool.h"
#include "Logger.h"

namespace task_engine {

ThreadPool::ThreadPool(size_t min_threads, size_t max_threads, size_t max_wait_time_ms)
    : min_threads_(min_threads),
      max_threads_(std::max(min_threads, max_threads)), // Ensure max_threads >= min_threads
      max_wait_time_ms_(max_wait_time_ms),
      stop_flag_(false),
      current_threads_count_(0),
      last_spawn_time_(std::chrono::steady_clock::now())
{
    // Initialize worker threads up to min_threads
    for (size_t i = 0; i < min_threads_; ++i) {
        threads_.emplace_back(&ThreadPool::worker_thread, this);
        current_threads_count_++;
    }
}

ThreadPool::~ThreadPool() {
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
}

void ThreadPool::submit(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        auto now = std::chrono::steady_clock::now();
        tasks_queue_.push({std::move(task), now}); // Add the task to the queue with timestamp

        // Dynamic sizing logic: Check latency of the oldest task
        if (current_threads_count_ < max_threads_) {
            auto oldest_task_time = tasks_queue_.front().enqueue_time;
            auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest_task_time).count();
            auto time_since_last_spawn = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_spawn_time_).count();

            // If wait time exceeds threshold AND we haven't spawned recently (e.g., 200ms cooldown)
            if (wait_duration > max_wait_time_ms_ && time_since_last_spawn > 200) {
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

            task = std::move(tasks_queue_.front().task); // Retrieve the task
            tasks_queue_.pop(); // Remove it from the queue
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