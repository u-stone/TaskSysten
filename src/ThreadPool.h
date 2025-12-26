#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <algorithm> // For std::max

namespace task_engine {

/**
 * @brief A dynamic thread pool that manages worker threads and a task queue.
 *        It can grow its thread count based on queue load.
 */
class ThreadPool {
public:
    /**
     * @brief Constructs a ThreadPool with dynamic sizing capabilities.
     * @param min_threads Initial number of worker threads.
     * @param max_threads Maximum number of worker threads the pool can grow to.
     * @param queue_grow_threshold If the task queue size exceeds this, a new thread might be spawned (up to max_threads).
     */
    explicit ThreadPool(size_t min_threads = std::thread::hardware_concurrency(),
                        size_t max_threads = std::thread::hardware_concurrency() * 2,
                        size_t queue_grow_threshold = 10);

    /**
     * @brief Destructor. Stops all threads and joins them.
     */
    ~ThreadPool();

    // Disable copying to prevent resource management issues
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /**
     * @brief Submits a task to the thread pool for execution.
     * @param task The callable object to execute.
     */
    void submit(std::function<void()> task);

    /**
     * @brief Returns the current number of active worker threads.
     * @return The count of currently running worker threads.
     */
    size_t get_current_thread_count() const {
        return current_threads_count_.load();
    }

private:
    // Worker thread loop function
    void worker_thread();

    std::vector<std::thread> threads_; // Collection of worker threads
    std::queue<std::function<void()>> tasks_queue_; // Queue of tasks to be executed

    std::mutex queue_mutex_; // Mutex to protect the task queue
    std::condition_variable condition_; // Condition variable to signal workers
    std::atomic<bool> stop_flag_; // Flag to signal threads to stop

    const size_t min_threads_; // Minimum number of threads in the pool
    const size_t max_threads_; // Maximum number of threads in the pool
    const size_t queue_grow_threshold_; // Threshold for queue size to consider spawning new threads
    std::atomic<size_t> current_threads_count_; // Tracks the actual number of running threads
};

} // namespace task_engine