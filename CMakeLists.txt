cmake_minimum_required(VERSION 3.14)
project(TaskEngine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable threading support
find_package(Threads REQUIRED)

# --- Source Code ---
add_library(task_engine 
    src/TaskExecutor.cpp
)
target_include_directories(task_engine PUBLIC src)
target_link_libraries(task_engine PRIVATE Threads::Threads)

# --- Examples ---
add_executable(example_app examples/main.cpp)
target_link_libraries(example_app PRIVATE task_engine)

# --- Testing ---
enable_testing()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/googletest
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(unit_tests tests/test_task_executor.cpp)
target_link_libraries(unit_tests PRIVATE task_engine GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_tests)

# --- Installation (Optional) ---
include(GNUInstallDirs)
install(TARGETS task_engine
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES src/TaskExecutor.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
