#include "TaskExecutor.h"
#include "Logger.h"
#include <chrono>

using namespace task_engine;

void simple_function(int id) {
    LOG_INFO() << "[Task " << id << "] Simple function executed on thread " << std::this_thread::get_id();
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate some work
}

int main() {
    LOG_INFO() << "Starting Task Engine Example with Dynamic Thread Pool...";

    // 1. Initialize Executor with min_threads=2, max_threads=8, max_wait_time_ms=50
    // This means it starts with 2 threads, and will add more if a task waits > 50ms.
    TaskExecutor executor(2, 8, 50); // These parameters are now passed to the internal ThreadPool

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

    LOG_INFO() << "Shutting down...";
    // Destructor will handle cleanup

    return 0;
}
