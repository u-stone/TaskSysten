#include "TaskExecutor.h"
#include <iostream>
#include <utility> // For std::move

namespace task_engine {
 
TaskExecutor::TaskExecutor(size_t min_threads, size_t max_threads, size_t queue_grow_threshold)
    : next_task_id_(1),
      thread_pool_(std::make_unique<ThreadPool>(min_threads, max_threads, queue_grow_threshold))
{
    // The ThreadPool constructor handles its own thread initialization.
}

TaskExecutor::~TaskExecutor() {
    // The unique_ptr to ThreadPool will automatically call its destructor,
    // which handles stopping and joining threads.
}

TaskExecutor::TaskID TaskExecutor::submit_internal(std::function<void()> task, std::function<void()> callback) {
    TaskID id = next_task_id_++;
    
    // Create a wrapper lambda that includes cancellation logic and task/callback execution
    auto wrapped_task_for_pool = [this, id, task = std::move(task), callback = std::move(callback)]() mutable {
        // Check for cancellation
        bool is_cancelled = false;
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            auto it = cancelled_tasks_.find(id);
            if (it != cancelled_tasks_.end()) {
                is_cancelled = true;
                // Cleanup the cancellation map to prevent memory growth
                cancelled_tasks_.erase(it);
            }
        }

        if (is_cancelled) {
            // Skip execution if cancelled
            return;
        }

        // Execute Task
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "Task " << id << " threw exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Task " << id << " threw unknown exception." << std::endl;
            }
        }

        // Execute Callback (if any)
        if (callback) {
            callback();
        }
    };

    // Submit the wrapped task to the underlying thread pool
    thread_pool_->submit(std::move(wrapped_task_for_pool));
    return id;
}

void TaskExecutor::cancel_task(TaskID id) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    // Mark the ID as cancelled. 
    // Note: If the task is already popped by a worker, this might be too late,
    // but we check this flag right before execution in the worker loop.
    cancelled_tasks_[id] = true;
}

size_t TaskExecutor::get_worker_count() const {
    return thread_pool_->get_current_thread_count();
}

} // namespace task_engine
