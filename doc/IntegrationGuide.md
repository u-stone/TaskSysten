# C++ Library Integration and Publishing Guide (Based on TaskEngine)

This document aims to help developers understand how to build modern C++ libraries so that they can be easily integrated by other projects via CMake (FetchContent, find_package), Conan, and vcpkg. This guide is written based on the practical experience of the `TaskEngine` project.

## 1. Source Code Organization

To support `FetchContent` and maintain a clear interface, the following directory structure is recommended. This structure separates public headers from private implementations, facilitating installation and referencing.

```text
TaskEngine/
├── cmake/                  # Helper scripts (version generation, Config templates, module files)
├── include/
│   └── TaskEngine/         # Public headers (with namespace directory to prevent header conflicts)
│       ├── TaskExecutor.h
│       └── ...
├── src/                    # Private source files (invisible to external projects)
│   ├── TaskExecutor.cpp
│   └── ...
├── examples/               # Example code (usually disabled by default when used as a subproject)
├── tests/                  # Unit tests (usually disabled by default when used as a subproject)
├── CMakeLists.txt          # Main build script
├── vcpkg.json              # vcpkg manifest file
└── conanfile.py            # Conan recipe file
```

## 2. CMakeLists.txt 编写最佳实践

### 2.1 支持 FetchContent 与 ALIAS
始终定义 `ALIAS` (别名) 目标。这使得外部项目无论是通过源码 (`add_subdirectory`) 还是安装包 (`find_package`) 引入，都可以使用统一的名称 `task_engine::task_engine` 进行链接。

```cmake
add_library(task_engine src/TaskExecutor.cpp ...)

# 定义别名，格式通常为 库名::库名
add_library(task_engine::task_engine ALIAS task_engine)
```

### 2.2 头文件路径与生成器表达式
使用 `BUILD_INTERFACE` 和 `INSTALL_INTERFACE` 分离构建树和安装树的头文件路径。这确保了在开发时引用源码目录，在安装后引用安装目录。

```cmake
target_include_directories(task_engine 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
```

### 2.3 支持 find_package (安装与导出)
为了支持 `find_package(TaskEngine)`，需要生成并安装 `TaskEngineConfig.cmake` 和 `TaskEngineTargets.cmake`。

```cmake
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# 1. 安装目标并导出 Export Set
install(TARGETS task_engine
    EXPORT TaskEngineTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} # Windows DLL 需要此项
)

# 2. 安装头文件 (排除 .in 模板文件)
install(DIRECTORY include/ 
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "*.in" EXCLUDE
)

# 3. 生成并安装 Config 文件
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TaskEngineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/TaskEngineConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TaskEngine
)

install(EXPORT TaskEngineTargets
    NAMESPACE task_engine::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TaskEngine
)
```

### 2.4 支持 pkg-config (可选)
虽然 CMake Config 是 C++ 首选，但为了支持非 CMake 构建系统，可以生成 `.pc` 文件。

```cmake
# 需准备 task_engine.pc.in 模板
configure_file(cmake/task_engine.pc.in task_engine.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/task_engine.pc 
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
```

## 3. 包管理器集成

### 3.1 Conan (conanfile.py)
编写 `conanfile.py` 以支持 Conan Center。关键点是使用 `CMakeToolchain` 和 `CMakeDeps`，并在 `package_info` 中定义库名称。

```python
from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout

class TaskEngineConan(ConanFile):
    name = "task-engine"
    version = "1.0.0"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def layout(self):
        cmake_layout(self)

    def package_info(self):
        self.cpp_info.libs = ["task_engine"]
        # 确保 find_package 和 Conan 引用名称一致
        self.cpp_info.set_property("cmake_target_name", "task_engine::task_engine")

#### 发布说明
*   **Conan Center**: 不支持 CI 自动同步。需向 [conan-center-index](https://github.com/conan-io/conan-center-index) 仓库提交 Pull Request。
*   **私有/自建 Remote**: 可使用 GitHub Actions 自动构建并上传 (参考 `.github/workflows/conan-publish.yml`)。
```

### 3.2 vcpkg (vcpkg.json)
使用 `vcpkg.json` 启用清单模式，自动管理依赖。

```json
{
  "name": "task-engine",
  "version-string": "1.0.0",
  "dependencies": [ "vcpkg-cmake" ]
}
```

## 4. CI/CD 自动化 (GitHub Actions)

参考 `vcpkg-ci.yml` 实现自动化构建与发布：
1.  **多平台测试**: 使用 Matrix 策略同时在 Ubuntu, Windows, macOS 上构建。
2.  **vcpkg 缓存**: 缓存 `vcpkg_cache` 目录以加速依赖安装。
3.  **自动发布**: 监听 `v*` 标签，构建后自动打包并上传到 GitHub Releases。

## 5. CPack 二进制打包
若需生成 `.deb`, `.rpm` 或 `.msi` 安装包，可在 `CMakeLists.txt` 末尾添加 CPack 配置：

```cmake
# CPack 配置示例
set(CPACK_PACKAGE_NAME "TaskEngine")
set(CPACK_PACKAGE_VENDOR "MyOrganization")
set(CPACK_PACKAGE_CONTACT "maintainer@example.com")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# 生成器选择
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS") # 需要安装 NSIS
elseif(APPLE)
    set(CPACK_GENERATOR "TGZ;DragNDrop")
else()
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
endif()

include(CPack)
```

## 6. 其他项目如何集成 TaskEngine

### 方式一：FetchContent (推荐用于源码依赖)
这是最简单的集成方式，无需预先安装库。

```cmake
include(FetchContent)
FetchContent_Declare(
  TaskEngine
  GIT_REPOSITORY https://github.com/YourUsername/TaskSystem.git
  GIT_TAG        v1.0.0
)
FetchContent_MakeAvailable(TaskEngine)

# 链接库
target_link_libraries(MyApp PRIVATE task_engine::task_engine)
```

### 方式二：find_package (推荐用于已安装库/vcpkg/Conan)
适用于系统级安装或包管理器管理的依赖。

```cmake
find_package(TaskEngine REQUIRED)
target_link_libraries(MyApp PRIVATE task_engine::task_engine)
```