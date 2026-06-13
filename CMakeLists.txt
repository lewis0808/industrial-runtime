cmake_minimum_required(VERSION 3.20)
project(IndustrialRuntime VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_subdirectory(core)