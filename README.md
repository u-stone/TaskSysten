# TaskEngine

![Vcpkg CI](https://github.com/u-stone/TaskSystem/actions/workflows/vcpkg-ci.yml/badge.svg)
![License](https://img.shields.io/badge/license-MIT-blue.svg)

A high-performance, thread-safe C++17 Task Execution Module.

## Features

*   **Task Management**: Add and cancel tasks dynamically.
*   **Flexibility**: Supports functions, lambdas, and functors with arbitrary arguments.
*   **Callbacks**: Built-in support for task completion callbacks.
*   **Concurrency**: Configurable thread pool size and scaling strategy.
*   **Safety**: Thread-safe design preventing data races and deadlocks.
*   **Integration**: Easy integration via CMake, vcpkg, and Conan.

## Requirements

*   C++17 compliant compiler (GCC, Clang, MSVC)
*   CMake 3.14 or higher

## Integration

Detailed integration instructions can be found in the [Integration Guide](doc/IntegrationGuide.md).

### Quick Start (FetchContent)

```cmake
include(FetchContent)
FetchContent_Declare(
  TaskEngine
  GIT_REPOSITORY https://github.com/YourUsername/TaskSystem.git
  GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(TaskEngine)

target_link_libraries(MyApp PRIVATE task_engine::task_engine)
```

## Build Instructions

1.  Clone the repository.
2.  Create a build directory:
    ```bash
    mkdir build && cd build
    ```
3.  Configure the project:
    ```bash
    cmake .. -DTASK_ENGINE_BUILD_TESTS=ON -DTASK_ENGINE_BUILD_EXAMPLES=ON
    ```
4.  Build:
    ```bash
    cmake --build . --config Release
    ```

## Usage

```cpp
#include <TaskEngine/TaskExecutor.h>
#include <iostream>

int main() {
    // Configure the thread pool
    task_engine::ThreadPoolConfig config;
    config.min_threads = 2;
    config.max_threads = 8;

    // Initialize TaskExecutor
    task_engine::TaskExecutor executor(config); 

    // Add a simple task
    executor.add_task(TASK_FROM_HERE, []{ 
        std::cout << "Hello World" << std::endl; 
    });

    // Add task with callback
    executor.add_task_with_callback(
        TASK_FROM_HERE,
        []{ /* do work */ },
        []{ /* on complete */ }
    );
    
    return 0;
}
```

## Testing

Unit tests are implemented using Google Test. After building, run:
```bash
./unit_tests
```
