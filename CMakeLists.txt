cmake_minimum_required(VERSION 3.20)
project(IndustrialRuntime VERSION 0.2.0 LANGUAGES CXX)

# 发版预发布标记：留空 = 正式版；填 beta.1 / rc.1 等 = 预发布。
# 合并到 main 时，release workflow 据此组合 tag（vX.Y.Z 或 vX.Y.Z-beta.1）并标记 GitHub prerelease。
set(IR_VERSION_PRERELEASE "")

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

# sdk/plugin-sdk 是面向外部开发者的交付物（同 sdk/irsp-client，仅头 + 示例），不进主构建。
# core 作为宿主自带一份 ABI 头副本（core/irplugin/）。设备插件作为独立工程开发（拷 SDK 头），
# 编出的动态库放进运行时同级 plugins/ 即可。
add_subdirectory(core)
add_subdirectory(irsp)
add_subdirectory(admin) # 本机控制面通道（依赖 core，独立于 irsp）

# 可执行入口：组合 core 运行时 + IRSP 数据面 + admin 控制面（在各模块之后定义）。
add_executable(IndustrialRuntime core/main.cpp)
target_link_libraries(IndustrialRuntime
    PRIVATE core_runtime_engine core_logger core_plugin_host core_plugin_manager
            irsp_server admin_server)