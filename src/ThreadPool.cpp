#include "ThreadPool.h"
#include "Logger.h"

namespace task_engine {

// Thread-local identifier to help workers find their own local queue
static thread_local int t_worker_id = -1;

ThreadPool::ThreadPool(const ThreadPoolConfig& config)
    : config_(config),
      stop_flag_(false),
      current_threads_count_(0),
      last_spawn_time_(std::chrono::steady_clock::now())
{
    config_.max_threads = std::max(config_.min_threads, config_.max_threads); // Ensure max >= min
    
    // Pre-allocate local queues for all possible threads
    for (size_t i = 0; i < config_.max_threads; ++i) {
        local_queues_.push_back(std::make_unique<LocalQueue>());
    }

    // Initialize worker threads up to min_threads
    for (size_t i = 0; i < config_.min_threads; ++i) {
        threads_.emplace_back(&ThreadPool::worker_thread, this, i);
        current_threads_count_++;
    }
}

ThreadPool::~ThreadPool() {
    LOG_INFO() << "ThreadPool shutting down. Stopping " << threads_.size() << " threads...";
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_flag_ = true; // Signal all threads to stop
    }
    condition_.notify_all(); // Wake up all waiting threads

    // Join all threads to ensure they complete their current task and exit cleanly
    for (std::thread& worker : threads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    LOG_INFO() << "ThreadPool destroyed. All threads joined.";
}

void ThreadPool::submit(std::function<void()> task, TaskPriority priority) {
    if (stop_flag_.load(std::memory_order_relaxed)) {
        LOG_WARN() << "ThreadPool is stopping, rejecting task submission.";
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // Optimization: If submitted from within a worker thread, push to its local queue to bypass the global lock.
    if (t_worker_id != -1 && static_cast<size_t>(t_worker_id) < local_queues_.size()) {
        auto& lq = local_queues_[t_worker_id];
        {
            std::lock_guard<std::mutex> l_lock(lq->mutex);
            lq->queue.emplace_back(std::move(task), now, priority);
        }
        condition_.notify_one();
        return;
    }

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_flag_.load(std::memory_order_relaxed)) return;

        tasks_queue_.emplace_back(std::move(task), now, priority); // Add the task to the queue with timestamp
        std::push_heap(tasks_queue_.begin(), tasks_queue_.end());

        // Dynamic sizing logic: Check latency of the oldest task
        if (current_threads_count_ < config_.max_threads) {
            auto time_since_last_spawn = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_spawn_time_).count();

            bool should_grow = false;
            if (static_cast<size_t>(time_since_last_spawn) > config_.cooldown_ms) {
                if (config_.strategy == ScalingStrategy::QUEUE_LENGTH) {
                    if (tasks_queue_.size() > config_.queue_length_threshold) {
                        should_grow = true;
                    }
                } else { // WAIT_TIME
                    auto oldest_task_time = tasks_queue_.front().enqueue_time; // front() is the max element (highest priority)
                    auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest_task_time).count();
                    if (static_cast<size_t>(wait_duration) > config_.max_wait_time_ms) {
                        should_grow = true;
                    }
                }
            }

            if (should_grow) {
                threads_.emplace_back(&ThreadPool::worker_thread, this, current_threads_count_.load());
                current_threads_count_++;
                last_spawn_time_ = now;
            }
        }
    }
    condition_.notify_one(); // Notify one waiting thread that a new task is available
}

void ThreadPool::worker_thread(size_t id) {
    t_worker_id = static_cast<int>(id);

    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // Wait until there is a task or the stop flag is set
            condition_.wait(lock, [this, id] {
                if (stop_flag_.load(std::memory_order_relaxed) || !tasks_queue_.empty()) {
                    return true;
                }
                // Fix: Must hold local lock to check empty()
                std::lock_guard<std::mutex> l_lock(local_queues_[id]->mutex);
                return !local_queues_[id]->queue.empty();
            });

            // 1. Try to get task from global priority queue
            if (!tasks_queue_.empty()) {
                std::pop_heap(tasks_queue_.begin(), tasks_queue_.end());
                task = std::move(tasks_queue_.back().task);
                tasks_queue_.pop_back();
            } 
            // If stopping and no more global tasks, check local before exiting
            else if (stop_flag_.load(std::memory_order_relaxed)) {
                std::lock_guard<std::mutex> l_lock(local_queues_[id]->mutex);
                if (local_queues_[id]->queue.empty()) {
                    current_threads_count_--;
                    return;
                }
                // If local queue is not empty, continue to drain local tasks
            }
        }

        // 2. If no global task, try local queue (LIFO)
        if (!task) {
            auto& lq = local_queues_[id];
            std::lock_guard<std::mutex> l_lock(lq->mutex);
            if (!lq->queue.empty()) {
                task = std::move(lq->queue.back().task);
                lq->queue.pop_back();
            }
        }

        // 3. If still no task, try to steal from others (FIFO)
        if (!task) {
            for (size_t i = 1; i < config_.max_threads; ++i) {
                size_t victim_id = (id + i) % config_.max_threads;
                auto& vq = local_queues_[victim_id];
                std::unique_lock<std::mutex> v_lock(vq->mutex, std::try_to_lock);
                if (v_lock.owns_lock() && !vq->queue.empty()) {
                    task = std::move(vq->queue.front().task);
                    vq->queue.pop_front();
                    break;
                }
            }
        }

        // Execute the task
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                LOG_ERROR() << "ThreadPool task threw exception: " << e.what();
            } catch (...) {
                LOG_ERROR() << "ThreadPool task threw unknown exception.";
            }
        }
    }
}

} // namespace task_engine