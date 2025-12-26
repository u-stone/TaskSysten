#include <gtest/gtest.h>
#include "TaskExecutor.h"
#include <atomic>
#include <chrono>

using namespace task_engine;

// Test 1: Basic Execution
TEST(TaskExecutorTest, BasicExecution) {
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 2;
    TaskExecutor executor(config);
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
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 2;
    TaskExecutor executor(config);
    std::atomic<int> result{0};

    executor.add_task(TASK_FROM_HERE, [&](int a, int b) {
        result = a + b;
    }, 5, 10);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(result, 15);
}

// Test 3: Callback Execution
TEST(TaskExecutorTest, CallbackExecution) {
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 2;
    TaskExecutor executor(config);
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
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 2;
    TaskExecutor executor(config);
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
    ThreadPoolConfig config;
    config.min_threads = 2;
    config.max_threads = 8;
    config.max_wait_time_ms = 50;
    TaskExecutor executor(config);
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
    // Start with 1 thread, max 4, grow if wait time > 1ms
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 4;
    config.max_wait_time_ms = 1;
    TaskExecutor executor(config);
    std::atomic<int> counter{0};
    const int num_tasks = 10;

    for(int i=0; i<num_tasks; ++i) {
        executor.add_task(TASK_FROM_HERE, [&]() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); counter++; });
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1)); // Give time for threads to potentially spawn and tasks to complete
    EXPECT_GE(executor.get_worker_count(), 1); // At least min_threads
    EXPECT_LE(executor.get_worker_count(), 4); // Not more than max_threads
    EXPECT_EQ(counter, num_tasks); // All tasks should eventually complete
}

// Test 7: Exception Logging with Source Location
TEST(TaskExecutorTest, ExceptionLoggingWithLocation) {
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 2;
    TaskExecutor executor(config);
    
    // This task will throw, and the TaskExecutor should log the error with the file/line info provided.
    executor.add_task(TASK_FROM_HERE, []() {
        throw std::runtime_error("Intentional exception to verify location logging");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// Test 8: Priority Execution
TEST(TaskExecutorTest, PriorityExecution) {
    // 1 thread to ensure serialization and order check
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 1;
    TaskExecutor executor(config);

    std::vector<int> execution_order;
    std::mutex mutex;

    // Block the thread first
    executor.add_task(TASK_FROM_HERE, []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    // Add Low priority task
    executor.add_task(TASK_FROM_HERE, TaskPriority::LOW, [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        execution_order.push_back(0); // 0 for Low
    });

    // Add High priority task
    executor.add_task(TASK_FROM_HERE, TaskPriority::HIGH, [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        execution_order.push_back(2); // 2 for High
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Expect High (2) then Low (0) because the thread was blocked when they were added
    ASSERT_EQ(execution_order.size(), 2);
    EXPECT_EQ(execution_order[0], 2);
    EXPECT_EQ(execution_order[1], 0);
}

// Test 9: Chain Execution (then)
TEST(TaskExecutorTest, ChainExecution) {
    ThreadPoolConfig config;
    config.min_threads = 2;
    config.max_threads = 4;
    TaskExecutor executor(config);

    std::atomic<int> stage{0};

    auto h1 = executor.add_task(TASK_FROM_HERE, [&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stage = 1;
    });

    h1.then(TASK_FROM_HERE, [&]() {
        if (stage == 1) stage = 2;
    }).then(TASK_FROM_HERE, [&]() {
        if (stage == 2) stage = 3;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(stage, 3);
}
