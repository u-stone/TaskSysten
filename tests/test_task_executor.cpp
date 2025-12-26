#include <gtest/gtest.h>
#include "TaskExecutor.h"
#include <atomic>
#include <chrono>

using namespace task_engine;

// Test 1: Basic Execution
TEST(TaskExecutorTest, BasicExecution) {
    TaskExecutor executor(2);
    std::atomic<bool> executed{false};

    executor.add_task([&]() {
        executed = true;
    });

    // Wait briefly for execution
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed);
}

// Test 2: Arguments Passing
TEST(TaskExecutorTest, ArgumentsPassing) {
    TaskExecutor executor(2);
    std::atomic<int> result{0};

    executor.add_task([&](int a, int b) {
        result = a + b;
    }, 5, 10);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(result, 15);
}

// Test 3: Callback Execution
TEST(TaskExecutorTest, CallbackExecution) {
    TaskExecutor executor(2);
    std::atomic<bool> task_done{false};
    std::atomic<bool> callback_done{false};

    executor.add_task_with_callback(
        [&]() { task_done = true; },
        [&]() { callback_done = true; }
    );

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(task_done);
    EXPECT_TRUE(callback_done);
}

// Test 4: Cancellation
TEST(TaskExecutorTest, Cancellation) {
    TaskExecutor executor(2);
    std::atomic<bool> executed{false};

    // Add a task that sleeps to block a thread, ensuring the next task sits in queue briefly
    executor.add_task([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });

    // Add the task to be cancelled
    auto id = executor.add_task([&]() {
        executed = true;
    });

    // Cancel immediately
    executor.cancel_task(id);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(executed) << "Task should have been cancelled and not executed.";
}

// Test 5: Multiple Threads
TEST(TaskExecutorTest, HighLoad) {
    TaskExecutor executor(4);
    std::atomic<int> counter{0};
    const int num_tasks = 100;

    for(int i=0; i<num_tasks; ++i) {
        executor.add_task([&]() {
            counter++;
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_EQ(counter, num_tasks);
}
