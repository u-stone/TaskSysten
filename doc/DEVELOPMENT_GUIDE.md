# TaskSystem 开发演进与使用指南

本文档详细记录了 `TaskSystem` 从一个基础线程池演进为高性能、工业级任务编排系统的全过程。重点介绍了每个核心功能的**设计思路**与**实现细节**。

## 1. 演进过程 (Evolution Process)

### 第一阶段：基础原型 (Basic Prototype)
**设计思路**: 实现最基本的异步执行能力。
*   **实现**: 使用 `std::vector<std::thread>` 维护固定数量的线程，通过 `std::queue` 存储任务，利用 `std::condition_variable` 进行生产者-消费者同步。
*   **局限**: 无法处理负载波动，缺乏任务状态追踪。

### 第二阶段：模块解耦 (Decoupling)
**设计思路**: 遵循单一职责原则（SRP）。
*   **`ThreadPool`**: 核心引擎，处理线程生命周期、物理调度和资源伸缩。
*   **`TaskExecutor`**: 业务门面，处理任务 ID 分配、逻辑包装、取消状态管理及回调触发。
*   **收益**: 允许底层引擎独立优化（如后续引入任务窃取）而不影响上层 API。

### 第三阶段：动态伸缩 (Dynamic Scaling)
**设计思路**: 自动平衡资源消耗与处理延迟。
*   **实现细节**: 
    *   **QUEUE_LENGTH**: 当积压任务超过阈值时触发 `emplace_back` 新线程。
    *   **WAIT_TIME**: 记录任务入队时间戳，若队首任务等待时间超过阈值，判定为处理能力不足，触发扩容。
    *   **冷却机制**: 引入 `cooldown_ms` 防止在短时间内创建过多线程。

### 第四阶段：可观测性与调试 (Observability)
**设计思路**: 解决多线程环境下“任务是谁提交的”和“为什么执行慢”的问题。
*   **实现细节**: 
    *   **`TASK_FROM_HERE`**: 利用 `__FILE__` 和 `__LINE__` 宏捕获提交位置。
    *   **耗时监控**: 在 `create_task_wrapper` 中包裹计时逻辑，自动识别并警告“长任务”，防止其阻塞线程池。

### 第五阶段：优先级调度 (Priority Scheduling)
**设计思路**: 确保紧急任务（如 UI 响应）优先于背景任务（如日志清理）。
*   **实现细节**: 
    *   使用 `std::vector` 配合 `std::push_heap` / `std::pop_heap` 维护最大堆。
    *   **排序准则**: 优先级高者优先；若优先级相同，则入队时间早者（FIFO）优先。

### 第六阶段：任务编排 (Orchestration)
**设计思路**: 从“单任务提交”转向“异步工作流管理”。
*   **`TaskNode`**: 核心状态容器，存储 `is_finished` 状态和 `continuations`（后续任务列表）。
*   **`.then()`**: 采用延迟提交（Deferred Submission）机制。若父任务未完成，将子任务存入 `continuations`；若已完成，则立即提交子任务。
*   **`when_all` / `when_any`**: 
    *   `when_all`: 使用原子计数器，每个子任务完成后递减，减至 0 时触发聚合任务。
    *   `when_any`: 使用原子布尔标志（CAS 操作），第一个完成的任务负责触发聚合任务。

### 第七阶段：健壮性与线程安全 (Robustness)
**设计思路**: 消除析构竞争、内存泄漏和编码警告。
*   **析构保护**: 引入 `accepting_tasks_` 原子变量。在 `TaskExecutor` 析构开始时立即拒绝新任务，防止在销毁期间访问已释放资源。
*   **内存清理**: 在任务执行逻辑中增加 `cancelled_tasks_.erase(id)`，确保取消映射表不会随时间无限增长。
*   **编码规范**: 将所有中文注释替换为英文，并确保文件以 UTF-8 格式保存，解决 MSVC `C4819` 警告。

### 第八阶段：任务窃取机制 (Task Stealing)
**设计思路**: 解决多核环境下由于任务耗时极度不均导致的“忙闲不均”问题。
*   **实现细节**: 
    *   **本地队列**: 每个线程拥有一个私有的 `std::deque` (Local Queue)。
    *   **提交策略**: 工作线程内部产生的子任务（如 `.then`）优先进入本地队列，绕过全局锁。
    *   **窃取算法**: 当线程空闲时，先检查全局队列，再检查本地队列（LIFO 顺序，利于缓存），最后随机轮询其他线程的本地队列并从前端窃取（FIFO 顺序，减少冲突）。
    *   **优先级一致性**:
        *   **挑战**: 若所有内部任务都进入本地队列，会导致优先级反转（例如：本地的高优先级任务被全局的低优先级任务阻塞，因为线程优先检查全局队列）。
        *   **解决方案**: 仅允许 `NORMAL` 优先级的任务进入本地队列。`HIGH` 和 `LOW` 优先级任务强制提交到全局优先级队列，以确保严格的全局执行顺序。

### 第九阶段：泛型任务与数据流 (Generic Tasks & Data Flow)
**设计思路**: 打破任务间的数据孤岛，允许上游任务的输出直接作为下游任务的输入。
*   **实现细节**: 
    *   **`TaskHandle<T>`**: 引入泛型句柄，`T` 代表任务返回值的类型。
    *   **类型推导**: `.then()` 方法利用 `std::invoke_result_t` 自动推导后续任务的参数类型，实现类型安全的链式调用。
    *   **数据传递**: 内部使用 `std::optional<T>` 存储结果，并通过 `std::move` 高效传递给后续任务。
    *   **优先级继承**: 子任务默认继承父任务的优先级，保持调度一致性。
    *   **异构聚合**: `TaskHandle<T>` 支持隐式转换为 `TaskHandle<void>`，使得 `when_all` 和 `when_any` 能够聚合不同返回类型的任务。
    *   **C++17 兼容性**: 针对 MSVC 编译器在 C++17 标准下的限制（如 lambda init-capture 包展开问题），采用了 `std::bind` 配合显式 `std::function` 类型转换的策略，确保了泛型任务链的跨平台兼容性。

### 第十阶段：异常传播与处理 (Exception Propagation)
**设计思路**: 允许异步任务链像同步代码一样处理异常，避免异常在后台线程中“无声消失”。
*   **实现细节**: 
    *   **异常捕获**: 任务包装器捕获所有未处理异常并存储在 `TaskNode` 的 `std::exception_ptr` 中。
    *   **自动传播**: `.then()` 任务在执行前检查上游异常，若存在则自动重新抛出，实现异常在链条中的“冒泡”。
    *   **异常捕获接口**: 提供 `.on_error()` 接口，允许用户注册专门的错误处理逻辑。

### 第十一阶段：同步阻塞获取 (Synchronous Blocking)
**设计思路**: 在某些场景下，调用方需要等待异步任务结果才能继续，引入类似 `std::future` 的阻塞机制。
*   **实现细节**: 
    *   **状态同步**: 在 `TaskNodeBase` 中增加 `std::condition_variable`，任务完成时通过 `notify_all` 唤醒阻塞线程。
    *   **阻塞接口**: `TaskHandle<T>` 提供 `wait()`（仅等待完成）和 `get()`（等待并获取结果/抛出异常）。

---

## 2. 核心架构 (Architecture)

| 组件 | 职责 |
| :--- | :--- |
| **`TaskExecutor`** | 负责任务生命周期管理、ID 分配、取消逻辑及 `TaskHandle` 的生成。 |
| **`ThreadPool`** | 负责物理线程调度、动态扩容策略及任务窃取逻辑。 |
| **`TaskNode`** | 存储任务完成状态，作为任务链（Continuations）的触发器。 |
| **`TaskHandle<T>`** | 泛型任务句柄，支持返回值传递和 `.then()` 链式操作。 |
| **`LocalQueue`** | 线程私有队列，配合 `t_worker_id` (thread_local) 减少全局锁竞争。 |

---

## 3. 使用指南 (Usage Guide)

### 3.1 初始化
```cpp
#include "TaskExecutor.h"

using namespace task_engine;

// 推荐配置：基于等待时间动态扩容
ThreadPoolConfig config;
config.min_threads = 2;
config.max_threads = 8;
config.strategy = ScalingStrategy::WAIT_TIME; 
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

### 3.6 同步阻塞 (Blocking)
在必要时同步等待任务结果。

```cpp
auto handle = executor.add_task(TASK_FROM_HERE,  {
    return 42;
});

int result = handle.get(); // 阻塞直到任务完成
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