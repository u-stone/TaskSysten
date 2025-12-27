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

// Test 10: WhenAll
TEST(TaskExecutorTest, WhenAll) {
    ThreadPoolConfig config;
    config.min_threads = 2;
    config.max_threads = 4;
    TaskExecutor executor(config);

    std::atomic<int> counter{0};
    std::vector<TaskHandle<void>> handles;

    for (int i = 0; i < 5; ++i) {
        handles.push_back(executor.add_task(TASK_FROM_HERE, [&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            counter++;
        }));
    }

    std::atomic<bool> all_done{false};
    executor.when_all(TASK_FROM_HERE, handles).then(TASK_FROM_HERE, [&]() {
        all_done = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(counter, 5);
    EXPECT_TRUE(all_done);
}

// Test 11: WhenAny
TEST(TaskExecutorTest, WhenAny) {
    ThreadPoolConfig config;
    config.min_threads = 2;
    config.max_threads = 4;
    TaskExecutor executor(config);

    std::vector<TaskHandle<void>> handles;
    handles.push_back(executor.add_task(TASK_FROM_HERE, []() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }));
    handles.push_back(executor.add_task(TASK_FROM_HERE, []() { std::this_thread::sleep_for(std::chrono::milliseconds(200)); }));

    std::atomic<bool> any_done{false};
    executor.when_any(TASK_FROM_HERE, handles).then(TASK_FROM_HERE, [&]() {
        any_done = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Enough for first task, not second
    EXPECT_TRUE(any_done);
}

// Test 17: Return Value and Result Passing in .then()
TEST(TaskExecutorTest, ReturnValuePassing) {
    TaskExecutor executor;
    
    // Task returns an int, next task receives it and returns a string
    auto h = executor.add_task(TASK_FROM_HERE, []() -> int {
        return 42;
    });

    std::atomic<bool> finished{false};
    std::string final_val;
    std::mutex m;

    h.then(TASK_FROM_HERE, [](int result) -> std::string {
        return "Result: " + std::to_string(result);
    }).then(TASK_FROM_HERE, [&](std::string s) {
        std::lock_guard<std::mutex> lock(m);
        final_val = s;
        finished = true;
    });

    // Wait for chain completion
    for(int i=0; i<100 && !finished; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    EXPECT_TRUE(finished);
    EXPECT_EQ(final_val, "Result: 42");
}

// Test 18: Priority Inheritance in .then()
TEST(TaskExecutorTest, PriorityInheritanceInChain) {
    // Use 1 thread to strictly control execution order
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 1;
    TaskExecutor executor(config);

    std::vector<std::string> order;
    std::mutex m;

    // 1. Block the pool
    executor.add_task(TASK_FROM_HERE, []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    // 2. Submit a HIGH priority chain
    executor.add_task(TASK_FROM_HERE, TaskPriority::HIGH, []() { return "High"; })
            .then(TASK_FROM_HERE, [&](const char* s) {
                std::lock_guard<std::mutex> lock(m);
                order.push_back(std::string(s) + "Cont");
            });

    // 3. Submit a LOW priority task
    executor.add_task(TASK_FROM_HERE, TaskPriority::LOW, [&]() {
        std::lock_guard<std::mutex> lock(m);
        order.push_back("Low");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    ASSERT_GE(order.size(), 2);
    // HighCont should be before Low because it inherited HIGH priority from its parent
    EXPECT_EQ(order[0], "HighCont");
    EXPECT_EQ(order[1], "Low");
}

// Test 19: WhenAll with Mixed Return Types
TEST(TaskExecutorTest, WhenAllMixedTypes) {
    TaskExecutor executor;
    auto h1 = executor.add_task(TASK_FROM_HERE, []() -> int { return 1; });
    auto h2 = executor.add_task(TASK_FROM_HERE, []() -> std::string { return "2"; });

    std::atomic<bool> done{false};
    // TaskHandle<T> implicitly converts to TaskHandle<void> for when_all
    executor.when_all(TASK_FROM_HERE, {h1, h2}).then(TASK_FROM_HERE, [&]() {
        done = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(done);
}

// Test 12: Chain on already finished task
TEST(TaskExecutorTest, ChainOnFinishedTask) {
    TaskExecutor executor;
    std::atomic<bool> first_done{false};
    std::atomic<bool> second_done{false};

    auto h = executor.add_task(TASK_FROM_HERE, [&]() {
        first_done = true;
    });

    // Wait for the first task to definitely finish
    while (!first_done) { std::this_thread::yield(); }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Adding a continuation to a finished task should trigger it immediately (or via pool)
    h.then(TASK_FROM_HERE, [&]() {
        second_done = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(second_done);
}

// Test 13: Rapid Shutdown
TEST(TaskExecutorTest, RapidShutdown) {
    // Test if destroying the executor while tasks are being submitted/executed causes issues
    for (int i = 0; i < 5; ++i) {
        {
            TaskExecutor executor;
            for (int j = 0; j < 100; ++j) {
                executor.add_task(TASK_FROM_HERE, []() {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                });
            }
            // Executor goes out of scope here, triggering ThreadPool join
        }
    }
}

// Test 14: Concurrent Cancellation Stress
TEST(TaskExecutorTest, ConcurrentCancellationStress) {
    TaskExecutor executor;
    const int num_tasks = 50;
    std::vector<TaskHandle<void>> handles;

    for (int i = 0; i < num_tasks; ++i) {
        handles.push_back(executor.add_task(TASK_FROM_HERE, []() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }));
    }

    std::vector<std::thread> cancel_threads;
    for (int i = 0; i < 10; ++i) {
        cancel_threads.emplace_back([&]() {
            for (auto& h : handles) {
                executor.cancel_task(h.id());
            }
        });
    }

    for (auto& t : cancel_threads) t.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

// Test 15: Recursive Task Submission (Task Stealing check)
TEST(TaskExecutorTest, RecursiveSubmission) {
    ThreadPoolConfig config;
    config.min_threads = 2;
    TaskExecutor executor(config);
    std::atomic<int> total_executed{0};
    const int depth = 10;

    std::function<void(int)> submit_recursive;
    submit_recursive = [&](int d) {
        total_executed++;
        if (d > 0) {
            executor.add_task(TASK_FROM_HERE, submit_recursive, d - 1);
        }
    };

    executor.add_task(TASK_FROM_HERE, submit_recursive, depth);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(total_executed, depth + 1);
}

// Test 16: WhenAll with Empty Handles
TEST(TaskExecutorTest, WhenAllEmptyHandles) {
    TaskExecutor executor;
    std::atomic<bool> triggered{false};
    
    std::vector<TaskHandle<void>> empty_handles;
    executor.when_all(TASK_FROM_HERE, empty_handles).then(TASK_FROM_HERE, [&]() {
        triggered = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(triggered);
}
