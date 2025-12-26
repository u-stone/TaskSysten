# TaskSystem 开发演进与使用指南

本文档详细记录了 `TaskSystem` 模块如何从一个简单的多线程任务执行器，逐步演进为具备动态伸缩、优先级调度、任务编排（链式/聚合）以及完善日志监控的高级任务系统。

## 1. 演进过程 (Evolution Process)

### 第一阶段：基础原型 (Basic Prototype)
最初的目标是构建一个简单的线程池来执行任务。
*   **功能**: 支持 `std::function<void()>` 任务提交，固定数量的 `std::thread` 工作线程。
*   **局限**: 线程数固定，无法应对突发流量；功能单一，仅支持“发射后不管”或简单的回调。

### 第二阶段：模块解耦 (Decoupling)
为了提高代码的可维护性和复用性，将任务调度逻辑与线程管理逻辑分离。
*   **`ThreadPool`**: 专注于线程的生命周期管理（创建、销毁、休眠、唤醒）和底层任务队列。
*   **`TaskExecutor`**: 专注于业务层的任务封装（ID分配、取消机制、回调包装）。

### 第三阶段：动态伸缩 (Dynamic Scaling)
为了应对波动的负载，引入了动态扩容机制。
*   **策略**: 支持基于 **队列长度 (QUEUE_LENGTH)** 或 **任务等待延迟 (WAIT_TIME)** 的扩容策略。
*   **配置**: 引入 `ThreadPoolConfig`，允许配置最小/最大线程数、冷却时间等。

### 第四阶段：可观测性与调试 (Observability)
为了方便排查问题，增强了系统的透明度。
*   **日志模块**: 实现了线程安全的 `Logger` 单例，替代 `std::cout`。
*   **位置追踪**: 引入 `TASK_FROM_HERE` 宏和 `Location` 结构，记录任务提交的源码位置。
*   **性能监控**: 自动检测执行时间超过 500ms 的任务并输出警告。

### 第五阶段：优先级调度 (Priority Scheduling)
为了保障关键任务的执行，引入了优先级机制。
*   **实现**: 底层队列从 `std::queue` 升级为基于 `std::vector` + `std::push_heap` 的优先级队列。
*   **接口**: 支持 `TaskPriority::HIGH`, `NORMAL`, `LOW`。

### 第六阶段：任务编排 (Orchestration)
为了支持复杂的业务流，引入了任务依赖管理。
*   **`TaskHandle`**: 替代简单的 `TaskID`，作为任务的句柄。
*   **链式调用**: 实现 `.then()` 接口，允许任务 A 完成后自动执行任务 B。
*   **聚合控制**: 实现 `when_all()` (等待一组任务全部完成) 和 `when_any()` (等待一组任务任一完成)。

---

## 2. 核心架构 (Architecture)

*   **`TaskExecutor`**: 用户主要交互的门面类。负责生成 `TaskHandle`，管理任务状态节点 (`TaskNode`)。
*   **`ThreadPool`**: 底层引擎。维护工作线程集合和优先级任务队列。根据配置策略自动增减线程。
*   **`TaskHandle`**: 轻量级对象，持有 `TaskNode` 的引用，用于构建任务依赖图。
*   **`Logger`**: 基础设施，提供统一的日志输出。

---

## 3. 使用指南 (Usage Guide)

### 3.1 初始化
```cpp
#include "TaskExecutor.h"

using namespace task_engine;

// 配置线程池
ThreadPoolConfig config;
config.min_threads = 2;
config.max_threads = 8;
config.strategy = ScalingStrategy::WAIT_TIME; // 基于延迟扩容
config.max_wait_time_ms = 50;

TaskExecutor executor(config);
```

### 3.2 提交任务
推荐使用 `TASK_FROM_HERE` 宏以便于调试。

```cpp
// 1. 简单任务
executor.add_task(TASK_FROM_HERE, [](){
    LOG_INFO() << "Hello Task";
});

// 2. 带优先级的任务
executor.add_task(TASK_FROM_HERE, TaskPriority::HIGH, [](){
    LOG_INFO() << "Important Task";
});

// 3. 带参数的任务
executor.add_task(TASK_FROM_HERE, [](int a, int b){
    LOG_INFO() << "Sum: " << a + b;
}, 10, 20);
```

### 3.3 任务编排 (Chaining)
使用 `.then()` 串联任务。

```cpp
executor.add_task(TASK_FROM_HERE, [](){
    // Step 1
    DoDatabaseQuery();
}).then(TASK_FROM_HERE, [](){
    // Step 2 (Runs after Step 1 finishes)
    ProcessData();
}).then(TASK_FROM_HERE, TaskPriority::HIGH, [](){
    // Step 3 (High priority)
    UpdateUI();
});
```

### 3.4 任务聚合 (Aggregation)

**等待所有任务 (`when_all`)**:
```cpp
std::vector<TaskHandle> handles;
for(int i=0; i<5; ++i) {
    handles.push_back(executor.add_task(TASK_FROM_HERE, [i](){ DownloadPart(i); }));
}

executor.when_all(TASK_FROM_HERE, handles).then(TASK_FROM_HERE, [](){
    LOG_INFO() << "All downloads finished. Merging file...";
});
```

**等待任一任务 (`when_any`)**:
```cpp
std::vector<TaskHandle> handles;
handles.push_back(executor.add_task(TASK_FROM_HERE, TryServerA));
handles.push_back(executor.add_task(TASK_FROM_HERE, TryServerB));

executor.when_any(TASK_FROM_HERE, handles).then(TASK_FROM_HERE, [](){
    LOG_INFO() << "One server responded. Proceeding...";
});
```

### 3.5 取消任务
```cpp
auto handle = executor.add_task(TASK_FROM_HERE, LongRunningJob);
// ...
executor.cancel_task(handle.id());
```

---

## 4. 构建与测试 (Build & Test)

项目使用 CMake 构建，并提供了 Windows 批处理脚本。

**构建并运行**:
直接运行根目录下的 `build.bat` 即可完成配置、编译、测试和示例运行。

**手动构建**:
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Debug
ctest -C Debug --output-on-failure
```