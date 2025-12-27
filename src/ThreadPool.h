#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <algorithm> // For std::max
#include <chrono>
#include <deque>

namespace task_engine {

enum class TaskPriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2
};

enum class ScalingStrategy {
    QUEUE_LENGTH, // Grow based on number of pending tasks
    WAIT_TIME     // Grow based on task wait time (latency)
};

struct ThreadPoolConfig {
    size_t min_threads = std::thread::hardware_concurrency();
    size_t max_threads = std::thread::hardware_concurrency() * 2;
    ScalingStrategy strategy = ScalingStrategy::WAIT_TIME;
    size_t queue_length_threshold = 10;
    size_t max_wait_time_ms = 100;
    size_t cooldown_ms = 200;
    bool enable_task_stealing = true;
    bool enable_stealing_logs = false;
};

/**
 * @brief A dynamic thread pool that manages worker threads and a task queue.
 *        It can grow its thread count based on queue load.
 */
class ThreadPool {
public:
    /**
     * @brief Constructs a ThreadPool with dynamic sizing capabilities.
     * @param config Configuration for the thread pool.
     */
    explicit ThreadPool(const ThreadPoolConfig& config = ThreadPoolConfig());

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
    void submit(std::function<void()> task, TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief Returns the current number of active worker threads.
     * @return The count of currently running worker threads.
     */
    size_t get_current_thread_count() const {
        return current_threads_count_.load();
    }

private:
    // Worker thread loop function
    void worker_thread(size_t id);

    struct TaskEntry {
        std::function<void()> task;
        std::chrono::steady_clock::time_point enqueue_time;
        TaskPriority priority;

        // Default constructor for deque
        TaskEntry() : task(nullptr), priority(TaskPriority::NORMAL) {}
        TaskEntry(std::function<void()> t, std::chrono::steady_clock::time_point time, TaskPriority p)
            : task(std::move(t)), enqueue_time(time), priority(p) {}

        // Max heap: largest element at top.
        bool operator<(const TaskEntry& other) const {
            if (priority != other.priority) {
                return priority < other.priority;
            }
            return enqueue_time > other.enqueue_time; // Older time (smaller) means higher priority (greater) in heap
        }
    };

    struct LocalQueue {
        std::deque<TaskEntry> queue;
        std::mutex mutex;
    };

    std::vector<std::thread> threads_; // Collection of worker threads
    std::vector<TaskEntry> tasks_queue_; // Priority Queue of tasks to be executed (using heap)
    std::vector<std::unique_ptr<LocalQueue>> local_queues_; // Per-thread local queues
    
    std::mutex queue_mutex_; // Mutex to protect the task queue
    std::condition_variable condition_; // Condition variable to signal workers
    std::atomic<bool> stop_flag_; // Atomic for lock-free checks

    ThreadPoolConfig config_; // Configuration
    std::atomic<size_t> current_threads_count_; // Tracks the actual number of running threads
    std::chrono::steady_clock::time_point last_spawn_time_; // For rate limiting thread creation
};

} // namespace task_engine