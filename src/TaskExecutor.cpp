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
    accepting_tasks_ = false;
    LOG_INFO() << "TaskExecutor shutting down...";
    // The unique_ptr to ThreadPool will automatically call its destructor,
    // which handles stopping and joining threads.
}

std::function<void()> TaskExecutor::create_task_wrapper(TaskID id, std::shared_ptr<TaskNode> node, std::function<void()> task, std::function<void()> callback, const char* file, int line) {
    return [this, id, node, task = std::move(task), callback = std::move(callback), file = std::string(file), line]() mutable {
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

        // Trigger continuations
        node->run_continuations();
    };
}

TaskHandle TaskExecutor::submit_internal(std::function<void()> task, std::function<void()> callback, const char* file, int line, TaskPriority priority) {
    if (!accepting_tasks_) return {};

    TaskID id = next_task_id_++;
    auto node = std::make_shared<TaskNode>();
    auto wrapped = create_task_wrapper(id, node, std::move(task), std::move(callback), file, line);

    // Submit the wrapped task to the underlying thread pool
    thread_pool_->submit(std::move(wrapped), priority);
    return TaskHandle(id, node, this, priority);
}

std::pair<TaskHandle, std::function<void()>> TaskExecutor::submit_deferred(std::function<void()> task, std::function<void()> callback, const char* file, int line, TaskPriority priority) {
    if (!accepting_tasks_) return {{}, nullptr};

    TaskID id = next_task_id_++;
    auto node = std::make_shared<TaskNode>();
    auto wrapped = create_task_wrapper(id, node, std::move(task), std::move(callback), file, line);

    auto submit_fn = [this, wrapped = std::move(wrapped), priority]() mutable {
        thread_pool_->submit(std::move(wrapped), priority);
    };

    return { TaskHandle(id, node, this, priority), std::move(submit_fn) };
}

TaskHandle TaskExecutor::when_all(const Location& location, const std::vector<TaskHandle>& handles, TaskPriority priority) {
    if (handles.empty()) {
        return add_task(location, priority, [](){});
    }

    auto counter = std::make_shared<std::atomic<size_t>>(handles.size());
    
    // Create the aggregate task, deferred.
    auto [agg_handle, submit_fn] = submit_deferred([](){}, nullptr, location.file_, location.line_, priority);
    auto shared_submit = std::make_shared<std::function<void()>>(std::move(submit_fn));

    for (const auto& h : handles) {
        if (h.node_) {
            h.node_->add_continuation([counter, shared_submit]() {
                if (counter->fetch_sub(1) == 1) {
                    (*shared_submit)();
                }
            });
        } else {
            // Handle invalid/empty handle? Treat as done?
            if (counter->fetch_sub(1) == 1) {
                (*shared_submit)();
            }
        }
    }
    return agg_handle;
}

TaskHandle TaskExecutor::when_any(const Location& location, const std::vector<TaskHandle>& handles, TaskPriority priority) {
    if (handles.empty()) {
        return add_task(location, priority, [](){});
    }

    auto flag = std::make_shared<std::atomic<bool>>(false);
    
    // Create the aggregate task, deferred.
    auto [agg_handle, submit_fn] = submit_deferred([](){}, nullptr, location.file_, location.line_, priority);
    auto shared_submit = std::make_shared<std::function<void()>>(std::move(submit_fn));

    for (const auto& h : handles) {
        if (h.node_) {
            h.node_->add_continuation([flag, shared_submit]() {
                bool expected = false;
                if (flag->compare_exchange_strong(expected, true)) {
                    (*shared_submit)();
                }
            });
        } else {
            bool expected = false;
            if (flag->compare_exchange_strong(expected, true)) {
                (*shared_submit)();
            }
        }
    }
    return agg_handle;
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
