#include "TaskEngine/TaskExecutor.h"
#include "TaskEngine/Logger.h"
#include <chrono>
#include <atomic>
#include <vector>
#include <random>

using namespace task_engine;

void run_comparison_benchmark() {
    LOG_INFO() << ">>> Starting TaskSystem Performance Benchmark <<<";

    const int num_tasks = 10000;
    std::vector<int> delays;
    delays.reserve(num_tasks);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 5); // Random delay between 0ms and 5ms
    for (int i = 0; i < num_tasks; ++i) {
        delays.push_back(dis(gen));
    }

    auto run_bench = [&](ScalingStrategy strategy, bool enable_stealing) {
        std::string strat_name = (strategy == ScalingStrategy::QUEUE_LENGTH ? "QUEUE_LENGTH" : "WAIT_TIME");
        LOG_INFO() << "Testing: Strategy=" << strat_name << ", Stealing=" << (enable_stealing ? "ON" : "OFF");
        
        ThreadPoolConfig config;
        config.min_threads = 4;
        config.max_threads = 16;
        config.strategy = strategy;
        config.queue_length_threshold = 100;
        config.max_wait_time_ms = 10; // Threshold for WAIT_TIME strategy
        config.enable_task_stealing = enable_stealing;
        config.enable_stealing_logs = false; // Ensure logs are disabled during benchmark

        TaskExecutor executor(config);
        std::atomic<int> counter{0};
        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < num_tasks; ++i) {
            int sleep_ms = delays[i];
            executor.add_task(TASK_FROM_HERE, [&counter, sleep_ms]() {
                if (sleep_ms > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                }
                counter++;
            });
        }

        while (counter < num_tasks) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        LOG_INFO() << "Result: " << duration << "ms (Peak threads: " << executor.get_worker_count() << ")";
        return duration;
    };

    auto q_stealing_on  = run_bench(ScalingStrategy::QUEUE_LENGTH, true);
    auto q_stealing_off = run_bench(ScalingStrategy::QUEUE_LENGTH, false);
    auto w_stealing_on  = run_bench(ScalingStrategy::WAIT_TIME, true);
    auto w_stealing_off = run_bench(ScalingStrategy::WAIT_TIME, false);

    LOG_INFO() << "=== Comparison Summary ===";
    LOG_INFO() << "1. QUEUE_LENGTH + Stealing ON:  " << q_stealing_on << "ms";
    LOG_INFO() << "2. QUEUE_LENGTH + Stealing OFF: " << q_stealing_off << "ms";
    LOG_INFO() << "3. WAIT_TIME    + Stealing ON:  " << w_stealing_on << "ms";
    LOG_INFO() << "4. WAIT_TIME    + Stealing OFF: " << w_stealing_off << "ms";
    
    double q_impact = (double)(q_stealing_off - q_stealing_on) / q_stealing_off * 100.0;
    double w_impact = (double)(w_stealing_off - w_stealing_on) / w_stealing_off * 100.0;
    LOG_INFO() << "Stealing Impact (QUEUE_LENGTH): " << q_impact << "% faster";
    LOG_INFO() << "Stealing Impact (WAIT_TIME):    " << w_impact << "% faster";
}

int main() {
    run_comparison_benchmark();
    LOG_INFO() << "Benchmark finished.";
    return 0;
}