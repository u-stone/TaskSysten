#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <type_traits>
#include <exception>
#include <optional>

#include "ThreadPool.h" // Include the new ThreadPool header
#include "Logger.h" // Required for LOG_WARN, LOG_ERROR macros
namespace task_engine {

class TaskExecutor; // Forward declaration

/**
 * @brief Internal structure to manage task dependencies.
 */
struct TaskNodeBase {
    std::mutex mutex;
    std::condition_variable cv;
    bool is_finished = false;
    std::vector<std::function<void()>> continuations;
    std::exception_ptr exception;

    virtual ~TaskNodeBase() = default;

    void run_continuations() {
        std::vector<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex);
            is_finished = true;
            pending = std::move(continuations);
        }
        cv.notify_all();
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

template <typename T>
struct TaskNode : public TaskNodeBase {
    std::optional<T> result;

    void set_result(T&& val) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            result = std::move(val);
        }
        run_continuations();
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

namespace detail {
    template <typename T, typename Func, typename... Args>
    struct GetResultType {
        using type = std::invoke_result_t<Func, T, Args...>;
    };

    template <typename Func, typename... Args>
    struct GetResultType<void, Func, Args...> {
        using type = std::invoke_result_t<Func, Args...>;
    };
}

/**
 * @brief A handle to a submitted task, allowing for chaining via .then().
 */
template <typename T = void>
class TaskHandle {
public:
    using NodeType = std::conditional_t<std::is_void_v<T>, TaskNodeBase, TaskNode<T>>;

    TaskHandle() = default;
    TaskHandle(size_t id, std::shared_ptr<NodeType> node, TaskExecutor* exec, TaskPriority priority)
        : id_(id), node_(std::move(node)), exec_(exec), priority_(priority) {}

    // Implicit conversion to TaskHandle<void> for grouping (e.g. when_all)
    operator TaskHandle<void>() const {
        return TaskHandle<void>(id_, std::static_pointer_cast<TaskNodeBase>(node_), exec_, priority_);
    }

    operator size_t() const { return id_; }
    size_t id() const { return id_; }

    // Chain a new task to run after this one completes
    template <typename Func, typename... Args>
    auto then(const Location& location, TaskPriority priority, Func&& f, Args&&... args);

    template <typename Func, typename... Args, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, TaskPriority>>>
    auto then(const Location& location, Func&& f, Args&&... args);

    // Catch and handle exceptions from upstream
    template <typename Func>
    TaskHandle<void> on_error(const Location& location, Func&& f);

    // Recover from an exception by providing a fallback value or logic
    template <typename Func>
    TaskHandle<T> recover(const Location& location, Func&& f);

    // Set a timeout for the task. If it expires, the handle will carry a timeout exception.
    TaskHandle<T> timeout(const Location& location, std::chrono::milliseconds duration);

    // Synchronously wait for the task to complete
    void wait() const;

    // Synchronously wait and return the result (or throw exception)
    T get();

private:
    friend class TaskExecutor;
    size_t id_ = 0;
    std::shared_ptr<NodeType> node_;
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
    auto add_task(const Location& location, TaskPriority priority, Func&& f, Args&&... args) {
        // Explicitly cast std::bind result to std::function for C++17 compatibility with MSVC
        using ReturnType = std::invoke_result_t<Func, Args...>;
        std::function<ReturnType()> bound_task = static_cast<std::function<ReturnType()>>(
            std::bind(std::forward<Func>(f), std::forward<Args>(args)...)
        );
        return submit_internal<ReturnType>([bound_task = std::move(bound_task)]() mutable -> ReturnType {
            return bound_task();
        }, nullptr, location.file_, location.line_, priority);
    }

    // Overload for default priority (NORMAL)
    template <typename Func, typename... Args, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, TaskPriority>>>
    auto add_task(const Location& location, Func&& f, Args&&... args) {
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
    auto add_task_with_callback(const Location& location, TaskPriority priority, Func&& f, Callback&& cb) {
        // Explicitly cast std::bind result to std::function for C++17 compatibility with MSVC
        using ReturnType = std::invoke_result_t<Func>;
        std::function<ReturnType()> bound_task = static_cast<std::function<ReturnType()>>(
            std::bind(std::forward<Func>(f))
        );
        return submit_internal<ReturnType>([bound_task = std::move(bound_task)]() mutable -> ReturnType {
            return bound_task();
        }, std::forward<Callback>(cb), location.file_, location.line_, priority);
    }

    template <typename Func, typename Callback, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Func>, TaskPriority>>>
    auto add_task_with_callback(const Location& location, Func&& f, Callback&& cb) {
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
    TaskHandle<void> when_all(const Location& location, const std::vector<TaskHandle<void>>& handles, TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief Creates a task that completes when any of the supplied tasks have completed.
     */
    TaskHandle<void> when_any(const Location& location, const std::vector<TaskHandle<void>>& handles, TaskPriority priority = TaskPriority::NORMAL);

    /**
     * @brief Returns the current number of worker threads in the underlying pool.
     * @return Number of threads.
     */
    size_t get_worker_count() const;

private:
    template <typename T> friend class TaskHandle;

    // Internal submission logic
    template <typename T>
    TaskHandle<T> submit_internal(std::function<T()> task, std::function<void()> callback, const char* file, int line, TaskPriority priority) {
        if (!accepting_tasks_) return {};
        TaskID id = next_task_id_++;
        using NodeType = std::conditional_t<std::is_void_v<T>, TaskNodeBase, TaskNode<T>>;
        auto node = std::make_shared<NodeType>();
        auto wrapped = create_task_wrapper<T>(id, node, std::move(task), std::move(callback), file, line);
        thread_pool_->submit(std::move(wrapped), priority);
        return TaskHandle<T>(id, node, this, priority);
    }

    // Common wrapper for task execution logic
    template <typename T>
    std::function<void()> create_task_wrapper(TaskID id, std::shared_ptr<std::conditional_t<std::is_void_v<T>, TaskNodeBase, TaskNode<T>>> node, std::function<T()> task, std::function<void()> callback, const char* file, int line) {
        return [this, id, node, task = std::move(task), callback = std::move(callback), file = std::string(file), line]() mutable {
            try {
                bool is_cancelled = false;
                {
                    std::lock_guard<std::mutex> lock(status_mutex_);
                    auto it = cancelled_tasks_.find(id);
                    if (it != cancelled_tasks_.end()) {
                        is_cancelled = true;
                        cancelled_tasks_.erase(it);
                    }
                }
                if (is_cancelled) return;

                if (task) {
                    auto start_time = std::chrono::steady_clock::now();
                    if constexpr (std::is_void_v<T>) {
                        task();
                    } else {
                        node->set_result(task());
                    }
                    auto end_time = std::chrono::steady_clock::now();
                    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
                    if (duration_ms > 500) {
                        LOG_WARN() << "Task " << id << " (" << file << ":" << line << ") execution time " << duration_ms << "ms exceeded 500ms threshold";
                    }
                }

                if (callback) callback();
                if constexpr (std::is_void_v<T>) node->run_continuations();
            } catch (const std::exception& e) {
                node->exception = std::current_exception();
                LOG_ERROR() << "Task " << id << " (" << file << ":" << line << ") threw exception: " << e.what();
                node->run_continuations();
            } catch (...) {
                node->exception = std::current_exception();
                LOG_ERROR() << "Task " << id << " (" << file << ":" << line << ") threw unknown exception.";
                node->run_continuations();
            }
        };
    }

    // Helper to create a task but not submit it immediately (for .then())
    template <typename T>
    std::pair<TaskHandle<T>, std::function<void()>> submit_deferred(std::function<T()> task, std::function<void()> callback, const char* file, int line, TaskPriority priority) {
        if (!accepting_tasks_) return {{}, nullptr};
        TaskID id = next_task_id_++;
        using NodeType = std::conditional_t<std::is_void_v<T>, TaskNodeBase, TaskNode<T>>;
        auto node = std::make_shared<NodeType>();
        auto wrapped = create_task_wrapper<T>(id, node, std::move(task), std::move(callback), file, line);
        auto submit_fn = [this, wrapped = std::move(wrapped), priority]() mutable {
            thread_pool_->submit(std::move(wrapped), priority);
        };
        return { TaskHandle<T>(id, node, this, priority), std::move(submit_fn) };
    }

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
template <typename T>
template <typename Func, typename... Args>
auto TaskHandle<T>::then(const Location& location, TaskPriority priority, Func&& f, Args&&... args) {
    using ResultType = typename detail::GetResultType<T, Func, Args...>::type;

    if (!exec_) return TaskHandle<ResultType>();

    auto prev_node = node_;
    std::function<ResultType()> final_task_func;

    auto check_exception = [prev_node]() {
        if (prev_node && prev_node->exception) std::rethrow_exception(prev_node->exception);
    };

    if constexpr (std::is_void_v<T>) {
        // Case 1: Previous task returned void.
        std::function<ResultType()> bound_f = static_cast<std::function<ResultType()>>(
            std::bind(std::forward<Func>(f), std::forward<Args>(args)...)
        );
        final_task_func = [check_exception, bound_f = std::move(bound_f)]() mutable -> ResultType {
            check_exception();
            return bound_f();
        };
    } else {
        // Case 2: Previous task returned T.
        std::function<ResultType(T)> bound_f = static_cast<std::function<ResultType(T)>>(
            std::bind(std::forward<Func>(f), std::placeholders::_1, std::forward<Args>(args)...)
        );
        final_task_func = [prev_node, check_exception, bound_f = std::move(bound_f)]() mutable -> ResultType {
            check_exception();
            return bound_f(std::move(*(prev_node->result)));
        };
    }

    auto [handle, submit_fn] = exec_->template submit_deferred<ResultType>(std::move(final_task_func), nullptr, location.file_, location.line_, priority);

    if (node_) {
        node_->add_continuation(std::move(submit_fn));
    }
    return handle;
}

template <typename T>
template <typename Func, typename... Args, typename>
auto TaskHandle<T>::then(const Location& location, Func&& f, Args&&... args) {
    return then(location, this->priority_, std::forward<Func>(f), std::forward<Args>(args)...);
}

template <typename T>
template <typename Func>
TaskHandle<void> TaskHandle<T>::on_error(const Location& location, Func&& f) {
    if (!exec_) return TaskHandle<void>();

    auto prev_node = node_;
    auto deferred_task = [prev_node, f = std::forward<Func>(f)]() mutable {
        if (prev_node && prev_node->exception) {
            f(prev_node->exception);
        }
    };

    auto [handle, submit_fn] = exec_->template submit_deferred<void>(std::move(deferred_task), nullptr, location.file_, location.line_, this->priority_);

    if (node_) {
        node_->add_continuation(std::move(submit_fn));
    }
    return handle;
}

template <typename T>
template <typename Func>
TaskHandle<T> TaskHandle<T>::recover(const Location& location, Func&& f) {
    if (!exec_) return TaskHandle<T>();

    auto prev_node = node_;
    auto deferred_task = [prev_node, f = std::forward<Func>(f)]() mutable -> T {
        if (prev_node && prev_node->exception) {
            if constexpr (std::is_void_v<T>) {
                f(prev_node->exception);
                return;
            } else {
                return f(prev_node->exception);
            }
        }
        if constexpr (!std::is_void_v<T>) {
            return std::move(*(prev_node->result));
        }
    };

    auto [handle, submit_fn] = exec_->template submit_deferred<T>(std::move(deferred_task), nullptr, location.file_, location.line_, this->priority_);

    if (node_) {
        node_->add_continuation(std::move(submit_fn));
    }
    return handle;
}

template <typename T>
void TaskHandle<T>::wait() const {
    if (!node_) return;
    std::unique_lock<std::mutex> lock(node_->mutex);
    node_->cv.wait(lock, [this] { return node_->is_finished; });
}

template <typename T>
T TaskHandle<T>::get() {
    wait();
    if (node_->exception) {
        std::rethrow_exception(node_->exception);
    }
    if constexpr (!std::is_void_v<T>) {
        std::lock_guard<std::mutex> lock(node_->mutex);
        if (!node_->result.has_value()) {
            throw std::runtime_error("Task result already consumed or not set");
        }
        return std::move(*(node_->result));
    }
}

template <typename T>
TaskHandle<T> TaskHandle<T>::timeout(const Location& location, std::chrono::milliseconds duration) {
    if (!exec_) return TaskHandle<T>();

    auto original_node = node_;
    auto original_id = id_;
    auto executor = exec_;

    // Create a new node for the timed result using submit_deferred to get a valid ID
    auto [timeout_handle, _] = exec_->template submit_deferred<T>(nullptr, nullptr, location.file_, location.line_, this->priority_);
    auto timeout_node = timeout_handle.node_;

    // Timer thread (Watchdog)
    // In a production system, a centralized TimerManager would be more efficient than one thread per timeout.
    std::thread([timeout_node, executor, original_id, duration]() {
        std::this_thread::sleep_for(duration);
        bool should_trigger = false;
        {
            std::lock_guard<std::mutex> lock(timeout_node->mutex);
            if (timeout_node->is_finished) return;
            timeout_node->exception = std::make_exception_ptr(std::runtime_error("Task timed out"));
            should_trigger = true;
        }
        if (should_trigger) {
            // Try to cancel the original task to save resources
            executor->cancel_task(original_id);
            timeout_node->run_continuations();
        }
    }).detach();

    // Original task completion continuation
    node_->add_continuation([original_node, timeout_node]() {
        bool should_trigger = false;
        {
            std::lock_guard<std::mutex> lock(timeout_node->mutex);
            if (timeout_node->is_finished) return; // Already timed out

            std::lock_guard<std::mutex> lock_orig(original_node->mutex);
            timeout_node->exception = original_node->exception;
            if constexpr (!std::is_void_v<T>) {
                if (original_node->result.has_value()) {
                    timeout_node->result = std::move(original_node->result);
                }
            }
            should_trigger = true;
        }
        if (should_trigger) {
            timeout_node->run_continuations();
        }
    });

    return timeout_handle;
}

} // namespace task_engine
