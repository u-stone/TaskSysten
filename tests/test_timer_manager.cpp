#include <gtest/gtest.h>
#include "TaskEngine/TimerManager.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <unordered_set>

using namespace task_engine;

// Test 1: Basic timer trigger
TEST(TimerManagerTest, BasicTimerExecution) {
    TimerManager manager;
    std::atomic<bool> executed{false};
    
    manager.add_timer(std::chrono::milliseconds(50), [&]() {
        executed = true;
    });

    // Wait long enough for the timer to trigger
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed);
}

// Test 2: Timer cancellation
TEST(TimerManagerTest, CancelTimer) {
    TimerManager manager;
    std::atomic<bool> executed{false};
    
    auto id = manager.add_timer(std::chrono::milliseconds(100), [&]() {
        executed = true;
    });

    // Cancel immediately
    manager.cancel_timer(id);

    // Wait longer than the timer's set time
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_FALSE(executed);
}

// Test 3: Execution order of multiple timers
TEST(TimerManagerTest, MultipleTimersOrder) {
    TimerManager manager;
    std::vector<int> results;
    std::mutex mtx;

    auto add_res = [&](int val) {
        std::lock_guard<std::mutex> lock(mtx);
        results.push_back(val);
    };

    // Added in non-chronological order
    manager.add_timer(std::chrono::milliseconds(150), [&]() { add_res(3); });
    manager.add_timer(std::chrono::milliseconds(50), [&]() { add_res(1); });
    manager.add_timer(std::chrono::milliseconds(100), [&]() { add_res(2); });

    // Wait for all timers to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    ASSERT_EQ(results.size(), 3);
    EXPECT_EQ(results[0], 1); // 50ms executes first
    EXPECT_EQ(results[1], 2); // 100ms executes second
    EXPECT_EQ(results[2], 3); // 150ms executes last
}

// Test 4: Timer does not fire early
TEST(TimerManagerTest, NoEarlyFire) {
    TimerManager manager;
    std::atomic<bool> executed{false};
    auto start = std::chrono::steady_clock::now();

    manager.add_timer(std::chrono::milliseconds(100), [&]() {
        executed = true;
    });

    // Check at 50ms, should not have triggered
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(executed);

    // Check at 120ms, should have triggered
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    EXPECT_TRUE(executed);
    
    auto end = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(diff, 100);
}

// Test 5: Concurrent timer addition
TEST(TimerManagerTest, ConcurrentAdd) {
    TimerManager manager;
    std::atomic<int> count{0};
    const int num_threads = 10;
    const int timers_per_thread = 100;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&]() {
            for (int j = 0; j < timers_per_thread; ++j) {
                manager.add_timer(std::chrono::milliseconds(10), [&]() { count++; });
            }
        });
    }

    for (auto& t : threads) t.join();

    // Wait for all timers to trigger
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(count, num_threads * timers_per_thread);
}

// Test 6: Exception in callback should not kill the manager
TEST(TimerManagerTest, ExceptionInCallback) {
    TimerManager manager;
    std::atomic<bool> second_timer_executed{false};

    manager.add_timer(std::chrono::milliseconds(10), []() {
        throw std::runtime_error("Intentional exception");
    });

    manager.add_timer(std::chrono::milliseconds(50), [&]() {
        second_timer_executed = true;
    });

    // Wait long enough for both timers to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(second_timer_executed);
}

// Test 7: Cancel the earliest timer (head of the set)
TEST(TimerManagerTest, CancelHeadTimer) {
    TimerManager manager;
    std::atomic<bool> first_executed{false};
    std::atomic<bool> second_executed{false};

    auto id1 = manager.add_timer(std::chrono::milliseconds(50), [&]() { first_executed = true; });
    manager.add_timer(std::chrono::milliseconds(100), [&]() { second_executed = true; });

    // Cancel the 50ms timer (the one the worker is likely waiting on)
    manager.cancel_timer(id1);

    // Wait for the 100ms timer to trigger
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_FALSE(first_executed);
    EXPECT_TRUE(second_executed);
}

// Test 8: Rapid add and cancel
TEST(TimerManagerTest, RapidAddAndCancel) {
    TimerManager manager;
    const int count = 1000;
    std::atomic<int> executed_count{0};

    for (int i = 0; i < count; ++i) {
        auto id = manager.add_timer(std::chrono::milliseconds(100), [&]() { executed_count++; });
        manager.cancel_timer(id);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(executed_count, 0);
}

// Test 9: Destruction with pending timers
TEST(TimerManagerTest, DestructionWithPendingTimers) {
    {
        TimerManager manager;
        for (int i = 0; i < 100; ++i) {
            manager.add_timer(std::chrono::seconds(10), []() {});
        }
        // manager goes out of scope here, worker thread should join cleanly
    }
}

// Test 10: Timer ID uniqueness
TEST(TimerManagerTest, TimerIDUniqueness) {
    TimerManager manager;
    std::unordered_set<TimerManager::TimerID> ids;
    for (int i = 0; i < 1000; ++i) {
        auto id = manager.add_timer(std::chrono::milliseconds(100), []() {});
        EXPECT_TRUE(ids.insert(id).second) << "Duplicate TimerID detected: " << id;
    }
}