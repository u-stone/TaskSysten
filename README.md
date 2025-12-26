# TaskEngine

A high-performance, thread-safe C++17 Task Execution Module.

## Features

*   **Task Management**: Add and cancel tasks dynamically.
*   **Flexibility**: Supports functions, lambdas, and functors with arbitrary arguments.
*   **Callbacks**: Built-in support for task completion callbacks.
*   **Concurrency**: Configurable thread pool size.
*   **Safety**: Thread-safe design preventing data races and deadlocks.

## Requirements

*   C++17 compliant compiler (GCC, Clang, MSVC)
*   CMake 3.14 or higher

## Build Instructions

1.  Clone the repository.
2.  Create a build directory:
    ```bash
    mkdir build && cd build
    ```
3.  Configure the project:
    ```bash
    cmake ..
    ```
4.  Build:
    ```bash
    cmake --build .
    ```

## Usage

```cpp
#include "TaskExecutor.h"

int main() {
    task_engine::TaskExecutor executor(2, 8, 3); // Min 2 threads, max 8 threads, grow if queue > 3

    // Add a simple task
    executor.add_task([]{ 
        std::cout << "Hello World" << std::endl; 
    });

    // Add task with callback
    executor.add_task_with_callback(
        []{ do_work(); },
        []{ on_complete(); }
    );
    
    return 0;
}
```

## Testing

Unit tests are implemented using Google Test. After building, run:
```bash
./unit_tests
```
