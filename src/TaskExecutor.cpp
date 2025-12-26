#include "TaskExecutor.h"
#include <iostream>

namespace task_engine {
 
TaskExecutor::TaskExecutor(size_t min_threads, size_t max_threads, size_t queue_grow_threshold)
    : min_threads_(min_threads),
      max_threads_(std::max(min_threads, max_threads)), // Ensure max_threads >= min_threads
      queue_grow_threshold_(queue_grow_threshold),
      stop_flag_(false),
      next_task_id_(1),
      current_threads_count_(0) // Initialize to 0, will be incremented as threads start
{
    // Initialize worker threads up to min_threads
    for (size_t i = 0; i < min_threads_; ++i) {
        threads_.emplace_back(&TaskExecutor::worker_thread, this);
        current_threads_count_++;
    }
}

TaskExecutor::~TaskExecutor() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_flag_ = true;
    }
    // Wake up all threads to allow them to exit
    condition_.notify_all();

    for (std::thread& worker : threads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

TaskExecutor::TaskID TaskExecutor::submit_internal(std::function<void()> task, std::function<void()> callback) {
    TaskID id = next_task_id_++;
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        // Push the task into the queue
        tasks_queue_.push(TaskWrapper{id, std::move(task), std::move(callback)});

        // Dynamic sizing logic: If queue is too long, consider adding a new thread
        if (tasks_queue_.size() > queue_grow_threshold_ && current_threads_count_ < max_threads_) {
            threads_.emplace_back(&TaskExecutor::worker_thread, this);
            current_threads_count_++;
            // std::cout << "DEBUG: Spawning new thread. Total threads: " << current_threads_count_ << std::endl; // For debugging
        }
    }
    
    // Notify one waiting thread
    condition_.notify_one();
    return id;
}

void TaskExecutor::cancel_task(TaskID id) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    // Mark the ID as cancelled. 
    // Note: If the task is already popped by a worker, this might be too late,
    // but we check this flag right before execution in the worker loop.
    cancelled_tasks_[id] = true;
}

void TaskExecutor::worker_thread() {
    while (true) {
        TaskWrapper current_task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // Wait until there is a task or we are stopping
            condition_.wait(lock, [this] {
                return stop_flag_ || !tasks_queue_.empty();
            });

            if (stop_flag_ && tasks_queue_.empty()) {
                // Thread is exiting. Decrement count.
                // This ensures current_threads_count_ accurately reflects running threads at shutdown.
                current_threads_count_--; 
                return; // Exit thread
            }

            // Retrieve task
            current_task = std::move(tasks_queue_.front());
            tasks_queue_.pop();
        }

        // Check for cancellation
        bool is_cancelled = false;
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            auto it = cancelled_tasks_.find(current_task.id);
            if (it != cancelled_tasks_.end()) {
                is_cancelled = true;
                // Cleanup the cancellation map to prevent memory growth
                cancelled_tasks_.erase(it);
            }
        }

        if (is_cancelled) {
            // Skip execution
            continue;
        }

        // Execute Task
        if (current_task.task_func) {
            try {
                current_task.task_func();
            } catch (const std::exception& e) {
                // In a real system, we might log this or invoke an error callback
                std::cerr << "Task " << current_task.id << " threw exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Task " << current_task.id << " threw unknown exception." << std::endl;
            }
        }

        // Execute Callback (if any)
        // Note: Callbacks are executed even if the main task threw, 
        // unless we want to change that policy. Here we assume "finally" semantics.
        if (current_task.callback_func) {
            current_task.callback_func();
        }
    }
}

} // namespace task_engine
