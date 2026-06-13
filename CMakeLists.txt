cmake_minimum_required(VERSION 3.20)
project(IndustrialRuntime VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 导出 compile_commands.json，供 clang-tidy / clangd 使用。
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 源码为 UTF-8。中文 locale 下 MSVC 默认按 GBK(936) 解释源码会破坏注释/解析，
# 显式声明 UTF-8 以保证跨平台一致。
if(MSVC)
    add_compile_options(/utf-8)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 第三方依赖（通过 vcpkg 清单模式提供）
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

option(IR_BUILD_TESTS "Build core unit tests" ON)

if(IR_BUILD_TESTS)
    enable_testing()
endif()

# 顺序：plugin-sdk 与示例插件须先于 core 定义
# （core 链接 plugin_sdk，core/tests 的端到端测试依赖 example_plugin 目标）。
add_subdirectory(sdk/plugin-sdk)
add_subdirectory(plugins/example)
add_subdirectory(core)
add_subdirectory(irp)

# 可执行入口：组合 core 运行时 + IRP WebSocket 服务（在 core/irp 之后定义）。
add_executable(IndustrialRuntime core/main.cpp)
target_link_libraries(IndustrialRuntime PRIVATE core_runtime_engine core_logger irp_server)