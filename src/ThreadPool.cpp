#include "ThreadPool.h"
#include "Logger.h"

namespace task_engine {

ThreadPool::ThreadPool(size_t min_threads, size_t max_threads, size_t queue_grow_threshold)
    : min_threads_(min_threads),
      max_threads_(std::max(min_threads, max_threads)), // Ensure max_threads >= min_threads
      queue_grow_threshold_(queue_grow_threshold),
      stop_flag_(false),
      current_threads_count_(0)
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
        tasks_queue_.push(std::move(task)); // Add the task to the queue

        // Dynamic sizing logic: If queue is too long, consider adding a new thread
        if (tasks_queue_.size() > queue_grow_threshold_ && current_threads_count_ < max_threads_) {
            threads_.emplace_back(&ThreadPool::worker_thread, this);
            current_threads_count_++;
            // std::cout << "DEBUG: Spawning new thread. Total threads: " << current_threads_count_ << std::endl;
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

            task = std::move(tasks_queue_.front()); // Retrieve the task
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