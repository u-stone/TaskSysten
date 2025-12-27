#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>
#include <type_traits>

#include "ThreadPool.h" // Include the new ThreadPool header
namespace task_engine {

class TaskExecutor; // Forward declaration

/**
 * @brief Internal structure to manage task dependencies.
 */
struct TaskNode {
    std::mutex mutex;
    bool is_finished = false;
    std::vector<std::function<void()>> continuations;

    void run_continuations() {
        std::vector<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex);
            is_finished = true;
            pending = std::move(continuations);
        }
        for (auto& task : pending) {
            task();
        }
    }

    void add_continuation(std::function<void()> task) {
        bool run_now = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (is_finished) {
                run_now = true;
            } else {
                continuations.push_back(std::move(task));
            }
        }
        if (run_now) {
            task();
        }
    }
};

/**
 * @brief Represents a source code location (file and line).
 */
struct Location {
    const char* file_ = "unknown";
    int line_ = 0;
    Location(const char* file, int line) : file_(file), line_(line) {}
    Location() = default;
};

/**
 * @brief A handle to a submitted task, allowing for chaining via .then().
 */
class TaskHandle {
public:
    TaskHandle() = default;
    TaskHandle(size_t id, std::shared_ptr<TaskNode> node, TaskExecutor* exec, TaskPriority priority)
        : id_(id), node_(std::move(node)), exec_(exec), priority_(priority) {}

    // Implicit conversion to TaskID (size_t) for backward compatibility
    operator size_t() const { return id_; }
    size_t id() const { return id_; }

    // Chain a new task to run after this one completes
    template <typename Func, typename... Args>
    TaskHandle then(const Location& location, TaskPriority priority, Func&& f, Args&&... args);

    template <typename Func, typename... Args, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, TaskPriority>>>
    TaskHandle then(const Location& location, Func&& f, Args&&... args);

private:
    friend class TaskExecutor;
    size_t id_ = 0;
    std::shared_ptr<TaskNode> node_;
    TaskExecutor* exec_ = nullptr;
    TaskPriority priority_ = TaskPriority::NORMAL;
};

/**
 * @brief A thread-safe task execution module supporting cancellation and callbacks.
 */
class TaskExecutor {
public:
    using TaskID = size_t;

    /**
     * @brief Constructs the executor with a specific number of threads.
     * @param config Configuration for the underlying thread pool.
     */
    explicit TaskExecutor(const ThreadPoolConfig& config = ThreadPoolConfig());

    /**
     * @brief Destructor. Stops all threads and joins them.
     */
    ~TaskExecutor();

    // Disable copying to prevent resource management issues
    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;

    /**
     * @brief Adds a task to the execution queue.
     * 
     * @tparam Func Type of the callable object.
     * @tparam Args Types of the arguments.
     * @param f The callable object (function, lambda, etc.).
     * @param args Arguments to be passed to the callable.
     * @return TaskHandle A handle for the submitted task.
     */
    template <typename Func, typename... Args>
    TaskHandle add_task(const Location& location, TaskPriority priority, Func&& f, Args&&... args) {
        // Create a wrapper that binds arguments to the function
        auto func = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
        return submit_internal([func]() { func(); }, nullptr, location.file_, location.line_, priority);
    }

    // Overload for default priority (NORMAL)
    template <typename Func, typename... Args, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, TaskPriority>>>
    TaskHandle add_task(const Location& location, Func&& f, Args&&... args) {
        return add_task(location, TaskPriority::NORMAL, std::forward<Func>(f), std::forward<Args>(args)...);
    }

    /**
     * @brief Adds a task with a completion callback.
     * 
     * @tparam Func Type of the task callable.
     * @tparam Callback Type of the callback callable.
     * @param f The main task.
     * @param cb The callback to execute after the task finishes.
     * @return TaskHandle A handle for the submitted task.
     */
    template <typename Func, typename Callback>
    TaskHandle add_task_with_callback(const Location& location, TaskPriority priority, Func&& f, Callback&& cb) {
        return submit_internal(
            std::forward<Func>(f),
            std::forward<Callback>(cb),
            location.file_, location.line_,
            priority
        );
    }

    template <typename Func, typename Callback, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, TaskPriority>>>
    TaskHandle add_task_with_callback(const Location& location, Func&& f, Callback&& cb) {
        return add_task_with_callback(location, TaskPriority::NORMAL, std::forward<Func>(f), std::forward<Callback>(cb));
    }

    /**
     * @brief Cancels a pending task.
     * If the task is already running or completed, this method does nothing.
     * @param id The ID of the task to cancel.
     */
    void cancel_task(TaskID id);

    /**
     * @brief Creates a task that completes when all of the supplied tasks have completed.
     */
    TaskHandle when_all(const Location& location, const std::vector<TaskHandle>& handles, TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief Creates a task that completes when any of the supplied tasks have completed.
     */
    TaskHandle when_any(const Location& location, const std::vector<TaskHandle>& handles, TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief Returns the current number of worker threads in the underlying pool.
     * @return Number of threads.
     */
    size_t get_worker_count() const;

private:
    friend class TaskHandle;

    // Internal submission logic
    TaskHandle submit_internal(std::function<void()> task, std::function<void()> callback, const char* file, int line, TaskPriority priority);

    // Common wrapper for task execution logic
    std::function<void()> create_task_wrapper(TaskID id, std::shared_ptr<TaskNode> node, std::function<void()> task, std::function<void()> callback, const char* file, int line);

    // Helper to create a task but not submit it immediately (for .then())
    std::pair<TaskHandle, std::function<void()>> submit_deferred(std::function<void()> task, std::function<void()> callback, const char* file, int line, TaskPriority priority);

    // Task Management
    std::atomic<TaskID> next_task_id_;
    std::atomic<bool> accepting_tasks_{true};
    std::mutex status_mutex_;
    std::unordered_map<TaskID, bool> cancelled_tasks_; // Tracks cancelled IDs

    // The underlying thread pool instance
    std::unique_ptr<ThreadPool> thread_pool_;
};

// Helper macro to pass current file and line number
#define TASK_FROM_HERE task_engine::Location(__FILE__, __LINE__)

// Implementation of TaskHandle templates
template <typename Func, typename... Args>
TaskHandle TaskHandle::then(const Location& location, TaskPriority priority, Func&& f, Args&&... args) {
    if (!exec_) return {};
    auto func = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
    
    // Create the next task but don't submit it to the thread pool yet.
    // Instead, we get a submit_fn that we attach to the current task's node.
    auto [handle, submit_fn] = exec_->submit_deferred([func](){ func(); }, nullptr, location.file_, location.line_, priority);
    
    if (node_) {
        node_->add_continuation(std::move(submit_fn));
    }
    return handle;
}

template <typename Func, typename... Args, typename>
TaskHandle TaskHandle::then(const Location& location, Func&& f, Args&&... args) {
    return then(location, this->priority_, std::forward<Func>(f), std::forward<Args>(args)...);
}

} // namespace task_engine
