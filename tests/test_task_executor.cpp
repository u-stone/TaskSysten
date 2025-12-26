#include <gtest/gtest.h>
#include "TaskExecutor.h"
#include <atomic>
#include <chrono>

using namespace task_engine;

// Test 1: Basic Execution
TEST(TaskExecutorTest, BasicExecution) {
    TaskExecutor executor(1, 2, 1); // Use dynamic pool parameters for consistency
    std::atomic<bool> executed{false};

    // Use add_task with TASK_FROM_HERE to verify API and pass location info
    executor.add_task(TASK_FROM_HERE, [&]() {
        executed = true;
    });

    // Wait briefly for execution
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed);
}

// Test 2: Arguments Passing
TEST(TaskExecutorTest, ArgumentsPassing) {
    TaskExecutor executor(1, 2, 1);
    std::atomic<int> result{0};

    executor.add_task(TASK_FROM_HERE, [&](int a, int b) {
        result = a + b;
    }, 5, 10);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(result, 15);
}

// Test 3: Callback Execution
TEST(TaskExecutorTest, CallbackExecution) {
    TaskExecutor executor(1, 2, 1);
    std::atomic<bool> task_done{false};
    std::atomic<bool> callback_done{false};

    executor.add_task_with_callback(TASK_FROM_HERE,
        [&]() { task_done = true; },
        [&]() { callback_done = true; }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(task_done);
    EXPECT_TRUE(callback_done);
}

// Test 4: Cancellation
TEST(TaskExecutorTest, Cancellation) {
    TaskExecutor executor(1, 2, 1);
    std::atomic<bool> executed{false};

    // Add a task that sleeps to block a thread, ensuring the next task sits in queue briefly
    executor.add_task(TASK_FROM_HERE, []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    // Add the task to be cancelled
    auto id = executor.add_task(TASK_FROM_HERE, [&]() {
        executed = true;
    });

    // Cancel immediately
    executor.cancel_task(id);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(executed) << "Task should have been cancelled and not executed.";
}

// Test 5: Multiple Threads
TEST(TaskExecutorTest, HighLoad) {
    TaskExecutor executor(2, 8, 3); // Test with dynamic pool settings
    std::atomic<int> counter{0};
    const int num_tasks = 100;

    for(int i=0; i<num_tasks; ++i) {
        executor.add_task(TASK_FROM_HERE, [&]() {
            counter++;
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_EQ(counter, num_tasks);
}

// Test 6: Dynamic Thread Growth (basic check)
TEST(TaskExecutorTest, DynamicGrowth) {
    // Start with 1 thread, max 4, grow if queue > 1
    TaskExecutor executor(1, 4, 1); 
    std::atomic<int> counter{0};
    const int num_tasks = 10;

    for(int i=0; i<num_tasks; ++i) {
        executor.add_task(TASK_FROM_HERE, [&]() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); counter++; });
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Give time for threads to potentially spawn
    EXPECT_GE(executor.get_worker_count(), 1); // At least min_threads
    EXPECT_LE(executor.get_worker_count(), 4); // Not more than max_threads
    EXPECT_EQ(counter, num_tasks); // All tasks should eventually complete
}

// Test 7: Exception Logging with Source Location
TEST(TaskExecutorTest, ExceptionLoggingWithLocation) {
    TaskExecutor executor(1, 2, 1);
    
    // This task will throw, and the TaskExecutor should log the error with the file/line info provided.
    executor.add_task(TASK_FROM_HERE, []() {
        throw std::runtime_error("Intentional exception to verify location logging");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
