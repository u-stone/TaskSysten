#include "TaskExecutor.h"
#include <iostream>
#include <chrono>

using namespace task_engine;

void simple_function(int id) {
    std::cout << "[Task " << id << "] Simple function executed on thread " << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate some work
}

int main() {
    std::cout << "Starting Task Engine Example with Dynamic Thread Pool..." << std::endl;

    // 1. Initialize Executor with min_threads=2, max_threads=8, queue_grow_threshold=3
    // This means it starts with 2 threads, and will add more if the queue has >3 tasks, up to 8 threads.
    TaskExecutor executor(2, 8, 3); // These parameters are now passed to the internal ThreadPool

    std::cout << "Submitting 10 tasks to observe dynamic growth..." << std::endl;
    std::vector<TaskExecutor::TaskID> task_ids;
    for (int i = 0; i < 10; ++i) {
        task_ids.push_back(executor.add_task(simple_function, i));
    }

    executor.add_task([](int a, int b) {
        std::cout << "[Task] Lambda sum: " << a << " + " << b << " = " << (a + b) << std::endl;
    }, 10, 20);

    // 4. Add a task with a callback
    executor.add_task_with_callback(
        []() {
            std::cout << "[Task] Heavy computation starting..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::cout << "[Task] Heavy computation finished." << std::endl;
        },
        []() {
            std::cout << "[Callback] Task is done! Cleanup or notification here." << std::endl;
        }
    );

    // Demonstrate Cancellation
    auto task_id_to_cancel = executor.add_task([]() {
        std::cout << "[Task] This should NOT appear." << std::endl;
    });
    
    std::cout << "Cancelling Task ID: " << task_id_to_cancel << std::endl;
    executor.cancel_task(task_id_to_cancel);

    // Give some time for tasks to process and for dynamic threads to potentially spawn
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Shutting down..." << std::endl;
    // Destructor will handle cleanup

    return 0;
}
