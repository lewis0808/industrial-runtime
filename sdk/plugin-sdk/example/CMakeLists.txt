# 示例插件 —— SDK 自带的最小参考实现。
# 演示如何用 irplugin SDK 写一个插件：推送 Tag/事件 + 注册写回。无第三方依赖。
#
# 可单独构建（无需 vcpkg / 不依赖主项目）：
#   cmake -S . -B build && cmake --build build
# 本示例即按 ../README.md 的「拷头」方式组织：SDK 头就在本目录 irplugin/ 下，
# include 根设为当前目录，使 #include "irplugin/..." 前缀生效。

cmake_minimum_required(VERSION 3.20)
project(example_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 把本目录的 SDK 头包装成 irplugin::plugin_sdk。
# include 根设为本目录，使 #include "irplugin/..." 的前缀生效。
add_library(irplugin_sdk INTERFACE)
add_library(irplugin::plugin_sdk ALIAS irplugin_sdk)
target_include_directories(irplugin_sdk INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(irplugin_sdk INTERFACE cxx_std_20)

# 插件本体：动态库（MODULE）。
add_library(example_plugin MODULE src/example_plugin.cpp)
target_link_libraries(example_plugin PRIVATE irplugin::plugin_sdk)

set_target_properties(example_plugin PROPERTIES
    PREFIX ""          # 输出 example_plugin.dll / .so
    CXX_VISIBILITY_PRESET hidden)
