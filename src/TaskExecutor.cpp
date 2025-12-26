#include "TaskExecutor.h"
#include "Logger.h"
#include <utility> // For std::move
#include <string>
#include <chrono>

namespace task_engine {
 
TaskExecutor::TaskExecutor(const ThreadPoolConfig& config)
    : next_task_id_(1),
      thread_pool_(std::make_unique<ThreadPool>(config))
{
    // The ThreadPool constructor handles its own thread initialization.
}

TaskExecutor::~TaskExecutor() {
    LOG_INFO() << "TaskExecutor shutting down...";
    // The unique_ptr to ThreadPool will automatically call its destructor,
    // which handles stopping and joining threads.
}

TaskExecutor::TaskID TaskExecutor::submit_internal(std::function<void()> task, std::function<void()> callback, const char* file, int line, TaskPriority priority) {
    TaskID id = next_task_id_++;
    
    // Create a wrapper lambda that includes cancellation logic and task/callback execution
    auto wrapped_task_for_pool = [this, id, task = std::move(task), callback = std::move(callback), file = std::string(file), line]() mutable {
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
                auto start_time = std::chrono::steady_clock::now();
                task();
                auto end_time = std::chrono::steady_clock::now();
                auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                if (duration_ms > 500) {
                    LOG_WARN() << "Task " << id << " (" << file << ":" << line << ") execution time " << duration_ms << "ms exceeded 500ms threshold";
                }
            } catch (const std::exception& e) {
                LOG_ERROR() << "Task " << id << " (" << file << ":" << line << ") threw exception: " << e.what();
            } catch (...) {
                LOG_ERROR() << "Task " << id << " (" << file << ":" << line << ") threw unknown exception.";
            }
        }

        // Execute Callback (if any)
        if (callback) {
            callback();
        }
    };

    // Submit the wrapped task to the underlying thread pool
    thread_pool_->submit(std::move(wrapped_task_for_pool), priority);
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
