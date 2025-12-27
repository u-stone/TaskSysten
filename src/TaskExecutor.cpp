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

TaskHandle<void> TaskExecutor::when_all(const Location& location, const std::vector<TaskHandle<void>>& handles, TaskPriority priority) {
    if (handles.empty()) {
        return add_task(location, priority, [](){});
    }

    auto counter = std::make_shared<std::atomic<size_t>>(handles.size());
    
    // Create the aggregate task, deferred.
    auto [agg_handle, submit_fn] = submit_deferred<void>([](){}, nullptr, location.file_, location.line_, priority);
    auto shared_submit = std::make_shared<std::function<void()>>(std::move(submit_fn));

    for (const auto& h : handles) {
        auto node = h.node_;
        if (node) {
            node->add_continuation([counter, shared_submit]() {
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

TaskHandle<void> TaskExecutor::when_any(const Location& location, const std::vector<TaskHandle<void>>& handles, TaskPriority priority) {
    if (handles.empty()) {
        return add_task(location, priority, [](){});
    }

    auto flag = std::make_shared<std::atomic<bool>>(false);
    
    // Create the aggregate task, deferred.
    auto [agg_handle, submit_fn] = submit_deferred<void>([](){}, nullptr, location.file_, location.line_, priority);
    auto shared_submit = std::make_shared<std::function<void()>>(std::move(submit_fn));

    for (const auto& h : handles) {
        auto node = h.node_;
        if (node) {
            node->add_continuation([flag, shared_submit]() {
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
