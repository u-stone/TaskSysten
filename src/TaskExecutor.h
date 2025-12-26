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
 * @brief A thread-safe task execution module supporting cancellation and callbacks.
 */
class TaskExecutor {
public:
    using TaskID = size_t;

    /**
     * @brief Constructs the executor with a specific number of threads.
     * @param num_threads Number of worker threads (default: hardware concurrency).
     * @param min_threads Initial number of worker threads for the underlying thread pool.
     * @param max_threads Maximum number of worker threads for the underlying thread pool.
     * @param max_wait_time_ms If a task waits longer than this (ms), a new thread might be spawned.
     */
    explicit TaskExecutor(size_t min_threads = std::thread::hardware_concurrency(),
                          size_t max_threads = std::thread::hardware_concurrency() * 2,
                          size_t max_wait_time_ms = 100); // Default values

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
     * @return TaskID A unique identifier for the submitted task.
     */
    template <typename Func, typename... Args>
    TaskID add_task(const Location& location, Func&& f, Args&&... args) {
        // Create a wrapper that binds arguments to the function
        auto func = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
        return submit_internal([func]() { func(); }, nullptr, location.file_, location.line_);
    }

    /**
     * @brief Adds a task with a completion callback.
     * 
     * @tparam Func Type of the task callable.
     * @tparam Callback Type of the callback callable.
     * @param f The main task.
     * @param cb The callback to execute after the task finishes.
     * @return TaskID A unique identifier.
     */
    template <typename Func, typename Callback>
    TaskID add_task_with_callback(const Location& location, Func&& f, Callback&& cb) {
        return submit_internal(
            std::forward<Func>(f),
            std::forward<Callback>(cb),
            location.file_, location.line_
        );
    }

    /**
     * @brief Cancels a pending task.
     * If the task is already running or completed, this method does nothing.
     * @param id The ID of the task to cancel.
     */
    void cancel_task(TaskID id);

    /**
     * @brief Returns the current number of worker threads in the underlying pool.
     * @return Number of threads.
     */
    size_t get_worker_count() const;

private:
    // Internal submission logic
    TaskID submit_internal(std::function<void()> task, std::function<void()> callback, const char* file, int line);

    // Task Management
    std::atomic<TaskID> next_task_id_;
    std::mutex status_mutex_;
    std::unordered_map<TaskID, bool> cancelled_tasks_; // Tracks cancelled IDs

    // The underlying thread pool instance
    std::unique_ptr<ThreadPool> thread_pool_;
};

// Helper macro to pass current file and line number
#define TASK_FROM_HERE task_engine::Location(__FILE__, __LINE__)

} // namespace task_engine
