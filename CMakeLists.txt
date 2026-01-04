cmake_minimum_required(VERSION 3.14)
project(TaskEngine VERSION v0.0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable threading support
find_package(Threads REQUIRED)

# Options for package managers and developers
option(TASK_ENGINE_BUILD_EXAMPLES "Build example programs" ON)
option(TASK_ENGINE_BUILD_TESTS "Build unit tests" ON)

# Provide a cache variable to allow manual override of the Git hash (useful for non-Git environments)
set(TASK_ENGINE_GIT_HASH "" CACHE STRING "Override Git commit hash if Git is not available")

# Define a custom target to run the version generation script during build time
add_custom_target(generate_version_header ALL
    COMMAND ${CMAKE_COMMAND}
        "-DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/include/TaskEngine/version.h.in"
        "-DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/include/TaskEngine/version.h"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DPROJECT_VERSION=${PROJECT_VERSION}"
        "-DTASK_ENGINE_GIT_HASH=${TASK_ENGINE_GIT_HASH}"
        "-DPROJECT_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
        "-DPROJECT_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
        "-DPROJECT_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateVersion.cmake"
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/include/TaskEngine/version.h
    COMMENT "Checking Git hash and updating version.h"
    VERBATIM
)

# --- Source Code ---
add_library(task_engine 
    src/TaskExecutor.cpp
    src/ThreadPool.cpp
    src/Logger.cpp
    src/TimerManager.cpp
)

# Generate the export header for shared library support
include(GenerateExportHeader)
generate_export_header(task_engine
    BASE_NAME TASK_ENGINE
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/TaskEngine/TaskEngineExport.h"
)

# Ensure the version header is generated/updated before compiling the library
add_dependencies(task_engine generate_version_header)

target_include_directories(task_engine 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(task_engine PUBLIC Threads::Threads)

add_library(task_engine::task_engine ALIAS task_engine)

# --- Examples ---
if(TASK_ENGINE_BUILD_EXAMPLES)
    # --- Examples ---
    add_executable(example examples/main.cpp)
    target_link_libraries(example PRIVATE task_engine)

    # --- Benchmarks ---
    add_executable(benchmark benchmarks/benchmark_main.cpp)
    target_link_libraries(benchmark PRIVATE task_engine)
endif()

if(TASK_ENGINE_BUILD_TESTS)
    enable_testing()

    # Try to find GTest via package manager first
    find_package(GTest QUIET)

    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
          googletest
          URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
          SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/googletest
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(unit_tests 
        tests/test_task_executor.cpp 
        tests/test_thread_pool.cpp
        tests/test_timer_manager.cpp
    )
    target_link_libraries(unit_tests PRIVATE task_engine GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()

# --- Installation (Optional) ---
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Generate the version file for the config package
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/TaskEngineConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Configure the main config file from the template
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TaskEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TaskEngineConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TaskEngine
)

install(TARGETS task_engine
    EXPORT TaskEngineTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install public headers, excluding CMake template files (.in)
install(DIRECTORY include/ 
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} 
    PATTERN "*.in" EXCLUDE)

install(FILES 
    ${CMAKE_CURRENT_BINARY_DIR}/include/TaskEngine/version.h 
    ${CMAKE_CURRENT_BINARY_DIR}/include/TaskEngine/TaskEngineExport.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/TaskEngine
)

# Install the export set (this creates TaskEngineTargets.cmake)
install(EXPORT TaskEngineTargets
    FILE TaskEngineTargets.cmake
    NAMESPACE task_engine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TaskEngine
)

# Install the generated config and version files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/TaskEngineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/TaskEngineConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TaskEngine
)
