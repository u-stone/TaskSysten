#include "TaskExecutor.h"
#include "Logger.h"
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

    auto run_bench = [&](bool enable_stealing) {
        LOG_INFO() << "Testing with enable_task_stealing = " << (enable_stealing ? "true" : "false");
        
        ThreadPoolConfig config;
        config.min_threads = 4;
        config.max_threads = 16;
        config.strategy = ScalingStrategy::QUEUE_LENGTH;
        config.queue_length_threshold = 100;
        config.enable_task_stealing = enable_stealing;

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

    auto time_with_stealing = run_bench(true);
    auto time_without_stealing = run_bench(false);

    LOG_INFO() << "=== Comparison Summary ===";
    LOG_INFO() << "Time with Stealing:    " << time_with_stealing << "ms";
    LOG_INFO() << "Time without Stealing: " << time_without_stealing << "ms";
    if (time_without_stealing > 0) {
        double diff = (double)(time_without_stealing - time_with_stealing) / time_without_stealing * 100.0;
        LOG_INFO() << "Task Stealing impact: " << diff << "% faster";
    }
}

int main() {
    run_comparison_benchmark();
    LOG_INFO() << "Benchmark finished.";
    return 0;
}