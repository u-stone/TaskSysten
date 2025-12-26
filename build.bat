@echo off
setlocal

echo ==========================================
echo      TaskSystem Build Script
echo ==========================================

:: 1. 准备目录
if not exist "external" (
    echo [INFO] Creating external directory...
    mkdir "external"
)

if not exist "build" (
    echo [INFO] Creating build directory...
    mkdir "build"
)

cd build

:: 2. 配置 CMake
:: 指定 Visual Studio 2017 生成器
echo [INFO] Configuring project for Visual Studio 2022...
cmake .. -G "Visual Studio 17 2022" -A x64
if %errorlevel% neq 0 goto :error

:: 3. 编译项目 (Debug 配置)
echo [INFO] Building project...
cmake --build . --config Debug
if %errorlevel% neq 0 goto :error

:: 4. 运行单元测试
echo [INFO] Running Unit Tests...
:: -C Debug 指定配置，--output-on-failure 在失败时输出详细信息
ctest -C Debug --output-on-failure
if %errorlevel% neq 0 goto :error

:: 5. 运行示例程序
echo [INFO] Running Example...
:: Visual Studio 生成的可执行文件通常位于 Debug 子目录中，名称为 example_app.exe
if exist "examples\Debug\example_app.exe" (
    "examples\Debug\example_app.exe"
) else if exist "examples\example_app.exe" (
    "examples\example_app.exe"
) else (
    echo [WARN] Example executable not found in expected paths.
)

echo ==========================================
echo      Build and Run Successful!
echo ==========================================
cd ..
exit /b 0

:error
echo [ERROR] An error occurred during the build process.
cd ..
exit /b 1