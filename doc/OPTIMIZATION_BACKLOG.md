# TaskSystem Optimization Backlog

This document tracks pending optimizations, architectural considerations, and feature enhancements for the `TaskSystem`.

## 1. ThreadPool & Scheduling

### 1.1 Lock-Free Local Queues
*   **Description**: Replace the current `std::deque` and `std::mutex` in `LocalQueue` with a lock-free work-stealing deque (e.g., Chase-Lev deque). This is essential to further minimize lock contention and synchronization overhead during high-concurrency task submission and stealing operations.
*   **Goal**: Achieve near-zero contention for local task management.

### 1.2 Atomic Global Priority Flag
*   **Description**: Maintain a global atomic flag or counter indicating the presence of `HIGH` priority tasks.
*   **Goal**: Allow worker threads to skip acquiring the global `queue_mutex_` if they only need to check for high-priority work when their local queues are full.

### 1.3 Task Preemption
*   **Description**: Explore mechanisms to allow `HIGH` priority tasks to "preempt" or signal long-running `NORMAL` tasks.
*   **Consideration**: C++ threads cannot be forcibly interrupted safely; this might require tasks to periodically check an interruption point.

## 2. TimerManager Enhancements

### 2.1 Hashed Hierarchical Wheel Timer
*   **Description**: Transition from the current `std::set` (O(log N)) implementation to a Hashed Hierarchical Wheel Timer. This architectural shift will provide O(1) complexity for both timer insertion and cancellation.
*   **Goal**: Optimize performance for scenarios involving a massive volume of short-lived timeouts.

### 2.2 Microsecond Precision
*   **Description**: Refine the `TimerManager` loop and use high-resolution sleep primitives to support microsecond-level timing accuracy.

## 3. Task Orchestration & API

### 3.1 Advanced Retry Mechanism
*   **Description**: Implement a `.retry()` interface for `TaskHandle` that automatically re-triggers task execution upon failure or timeout.
*   **Features**: Support for configurable retry limits, exception-based filtering, and the ability to schedule retries with exponential backoff using the `TimerManager`.

### 3.2 Aggregate Exception Collection
*   **Description**: Enhance the `when_all` implementation to aggregate exceptions from all failing sub-tasks. Instead of only propagating the first encountered error, the system should collect all `std::exception_ptr` instances into a container.
*   **Goal**: Provide comprehensive visibility into partial failures during parallel task execution.

### 3.3 Recovery for Aggregates
*   **Description**: Extend `when_all` to support typed results (e.g., `TaskHandle<std::vector<T>>`) and allow `.recover()` to provide a unified fallback value for the entire collection if any sub-task fails. This ensures that a single failure doesn't invalidate the entire aggregate operation, allowing the system to return a default state for the group instead of propagating an exception.
*   **Goal**: Enhance the resilience and data-flow capabilities of aggregate task operations.

## 4. Observability & Tooling

### 4.1 Work Stealing Visualization
*   **Description**: Create a telemetry module or a simple CLI tool to visualize task movement between threads.
*   **Metrics**: Track "steal success rate," "local hit rate," and "global contention frequency."

### 4.2 Performance Benchmarking Suite
*   **Description**: Expand the `benchmark` tool to use libraries like Google Benchmark for more statistical rigor.
*   **Scenarios**: Test with various "Work-to-Sync" ratios and different hardware concurrency levels.

---

## Implementation Considerations

*   **C++17 Compatibility**: All optimizations must remain compatible with the C++17 standard, avoiding C++20 features like `std::atomic<std::shared_ptr>` or advanced lambda captures unless guarded by macros.
*   **Strict Priority**: Any optimization to the `LocalQueue` must ensure that the global priority contract (HIGH > NORMAL > LOW) is never violated.
*   **MSVC Encoding**: Maintain UTF-8 encoding without BOM and use English comments to avoid `C4819` warnings on Windows.