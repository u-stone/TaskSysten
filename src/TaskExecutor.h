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

namespace task_engine {

/**
 * @brief A thread-safe task execution module supporting cancellation and callbacks.
 */
class TaskExecutor {
public:
    using TaskID = size_t;

    /**
     * @brief Constructs the executor with a specific number of threads.
     * @param num_threads Number of worker threads (default: hardware concurrency).
     * @param min_threads Initial number of worker threads.
     * @param max_threads Maximum number of worker threads the pool can grow to.
     * @param queue_grow_threshold If the task queue size exceeds this, a new thread might be spawned (up to max_threads).
     */
    explicit TaskExecutor(size_t min_threads = std::thread::hardware_concurrency(),
                          size_t max_threads = std::thread::hardware_concurrency() * 2,
                          size_t queue_grow_threshold = 10); // Default values

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
    TaskID add_task(Func&& f, Args&&... args) {
        // Create a wrapper that binds arguments to the function
        auto func = std::bind(std::forward<Func>(f), std::forward<Args>(args)...);
        return submit_internal([func]() { func(); }, nullptr);
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
    TaskID add_task_with_callback(Func&& f, Callback&& cb) {
        return submit_internal(
            std::forward<Func>(f),
            std::forward<Callback>(cb)
        );
    }

    /**
     * @brief Cancels a pending task.
     * If the task is already running or completed, this method does nothing.
     * @param id The ID of the task to cancel.
     */
    void cancel_task(TaskID id);

private:
    // Internal structure to hold task details
    struct TaskWrapper {
        TaskID id;
        std::function<void()> task_func;
        std::function<void()> callback_func;
    };

    // Internal submission logic
    TaskID submit_internal(std::function<void()> task, std::function<void()> callback);

    // Worker thread loop
    void worker_thread();

    // Thread pool components
    std::vector<std::thread> threads_;
    std::queue<TaskWrapper> tasks_queue_;
    
    // Synchronization
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_flag_;

    // Task Management
    std::atomic<TaskID> next_task_id_;
    std::mutex status_mutex_;
    std::unordered_map<TaskID, bool> cancelled_tasks_; // Tracks cancelled IDs

    // Dynamic Sizing Parameters
    const size_t min_threads_;
    const size_t max_threads_;
    const size_t queue_grow_threshold_;
    std::atomic<size_t> current_threads_count_; // Tracks actual number of running threads
};

} // namespace task_engine
