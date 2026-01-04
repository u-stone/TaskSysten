#include "TaskEngine/TaskExecutor.h"
#include "TaskEngine/version.h"
#include "TaskEngine/Logger.h"
#include <chrono>
#include <atomic>
#include <vector>
#include <random>

using namespace task_engine;

void simple_function(int id) {
    LOG_INFO() << "[Task " << id << "] Simple function executed on thread " << std::this_thread::get_id();
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate some work
}

void exception_demo() {
    LOG_INFO() << ">>> Starting Exception Propagation Demo <<<";
    TaskExecutor executor;

    executor.add_task(TASK_FROM_HERE, []() {
        LOG_INFO() << "Task 1: Attempting to connect to service...";
        throw std::runtime_error("Service Unavailable (HTTP 503)");
    }).then(TASK_FROM_HERE, []() {
        LOG_INFO() << "Task 2: This will be skipped due to Task 1 failure.";
    }).on_error(TASK_FROM_HERE, [](std::exception_ptr ex) {
        try {
            if (ex) std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            LOG_ERROR() << "Task Chain Error Handler: " << e.what();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void recovery_demo() {
    LOG_INFO() << ">>> Starting Recovery Demo <<<";
    TaskExecutor executor;

    // Case 1: Recover from failure by providing a fallback value
    executor.add_task(TASK_FROM_HERE, []() -> int {
        LOG_INFO() << "Recovery Task 1: Simulating a failure...";
        throw std::runtime_error("Database connection failed");
        return 0;
    }).recover(TASK_FROM_HERE, [](std::exception_ptr ex) -> int {
        try {
            if (ex) std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            LOG_WARN() << "Recovery Task 1 failed: " << e.what() << ". Using fallback value 42.";
        }
        return 42; // Fallback value
    }).then(TASK_FROM_HERE, [](int val) {
        LOG_INFO() << "Recovery Task 1 Result: " << val;
    });

    // Case 2: Pass-through when successful (recover logic is skipped)
    executor.add_task(TASK_FROM_HERE, []() -> int {
        LOG_INFO() << "Recovery Task 2: Success case...";
        return 100;
    }).recover(TASK_FROM_HERE, [](std::exception_ptr ex) -> int {
        return -1; // Won't be used
    }).then(TASK_FROM_HERE, [](int val) {
        LOG_INFO() << "Recovery Task 2 Result: " << val;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

void timeout_demo() {
    LOG_INFO() << ">>> Starting Timeout Demo <<<";
    TaskExecutor executor;

    auto h = executor.add_task(TASK_FROM_HERE, []() {
        LOG_INFO() << "Long task starting...";
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        LOG_INFO() << "Long task finished.";
    }).timeout(TASK_FROM_HERE, std::chrono::milliseconds(200));

    try {
        h.get();
    } catch (const std::exception& e) {
        LOG_ERROR() << "Caught expected timeout: " << e.what();
    }
}

int main() {
    LOG_INFO() << "Starting Task Engine v" << TASK_ENGINE_VERSION << " (" << TASK_ENGINE_GIT_HASH << ") with Dynamic Thread Pool...";

    // 1. Initialize Executor with custom configuration
    ThreadPoolConfig config;
    config.min_threads = 2;
    config.max_threads = 8;
    config.max_wait_time_ms = 50;
    TaskExecutor executor(config);

    LOG_INFO() << "Submitting 10 tasks to observe dynamic growth...";
    std::vector<TaskExecutor::TaskID> task_ids;
    for (int i = 0; i < 10; ++i) {
        task_ids.push_back(executor.add_task(TASK_FROM_HERE, simple_function, i));
    }

    executor.add_task(TASK_FROM_HERE, [](int a, int b) {
        LOG_INFO() << "[Task] Lambda sum: " << a << " + " << b << " = " << (a + b);
    }, 10, 20);

    // 4. Add a task with a callback
    executor.add_task_with_callback(TASK_FROM_HERE,
        []() {
            LOG_INFO() << "[Task] Heavy computation starting...";
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            LOG_INFO() << "[Task] Heavy computation finished.";
        },
        []() {
            LOG_INFO() << "[Callback] Task is done! Cleanup or notification here.";
        }
    );

    // Demonstrate Cancellation
    auto task_id_to_cancel = executor.add_task(TASK_FROM_HERE, []() {
        LOG_WARN() << "[Task] This should NOT appear.";
    });
    
    LOG_INFO() << "Cancelling Task ID: " << task_id_to_cancel;
    executor.cancel_task(task_id_to_cancel);

    // Give some time for tasks to process and for dynamic threads to potentially spawn
    std::this_thread::sleep_for(std::chrono::seconds(2));

    exception_demo();

    recovery_demo();

    timeout_demo();

    LOG_INFO() << "Shutting down...";
    // Destructor will handle cleanup

    return 0;
}
