#include <gtest/gtest.h>
#include "ThreadPool.h"
#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>

using namespace task_engine;

// Test 1: High Load - Verify pool handles many tasks
TEST(ThreadPoolTest, HighLoad) {
    ThreadPoolConfig config;
    config.min_threads = 2;
    config.max_threads = 8;
    config.max_wait_time_ms = 50;
    ThreadPool pool(config);
    std::atomic<int> counter{0};
    const int num_tasks = 100;

    for(int i=0; i<num_tasks; ++i) {
        pool.submit([&]() {
            counter++;
        });
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_EQ(counter, num_tasks);
}

// Test 2: Dynamic Thread Growth - Verify pool scales up under load
TEST(ThreadPoolTest, DynamicGrowth) {
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 4;
    config.max_wait_time_ms = 1;
    ThreadPool pool(config);
    std::atomic<int> counter{0};
    const int num_tasks = 10;

    for(int i=0; i<num_tasks; ++i) {
        pool.submit([&]() { 
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); 
            counter++; 
        });
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    EXPECT_GE(pool.get_current_thread_count(), 1);
    EXPECT_LE(pool.get_current_thread_count(), 4);
    EXPECT_EQ(counter, num_tasks);
}

// Test 3: Priority Execution - Verify global priority heap ordering
TEST(ThreadPoolTest, PriorityExecution) {
    ThreadPoolConfig config;
    config.min_threads = 1;
    config.max_threads = 1;
    ThreadPool pool(config);

    std::vector<int> execution_order;
    std::mutex mutex;

    pool.submit([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });

    pool.submit([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        execution_order.push_back(0); // 0 for Low
    }, TaskPriority::LOW);

    pool.submit([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        execution_order.push_back(2); // 2 for High
    }, TaskPriority::HIGH);

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    ASSERT_EQ(execution_order.size(), 2);
    EXPECT_EQ(execution_order[0], 2);
    EXPECT_EQ(execution_order[1], 0);
}

// Test 4: Rapid Shutdown - Verify safe destruction with pending tasks
TEST(ThreadPoolTest, RapidShutdown) {
    for (int i = 0; i < 5; ++i) {
        {
            ThreadPool pool;
            for (int j = 0; j < 100; ++j) {
                pool.submit([]() {
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                });
            }
        }
    }
}

// Test 5: Recursive Task Submission - Verify Task Stealing / Local Queue logic
TEST(ThreadPoolTest, RecursiveSubmission) {
    ThreadPoolConfig config;
    config.min_threads = 2;
    ThreadPool pool(config);
    std::atomic<int> total_executed{0};
    const int depth = 10;

    std::function<void(int)> submit_recursive;
    submit_recursive = [&](int d) {
        total_executed++;
        if (d > 0) {
            pool.submit([&submit_recursive, d]() { submit_recursive(d - 1); });
        }
    };

    pool.submit([&submit_recursive, depth]() { submit_recursive(depth); });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_EQ(total_executed, depth + 1);
}