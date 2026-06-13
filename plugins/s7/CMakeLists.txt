# S7 设备插件：独立、动态加载的 MODULE 库。
# 依赖 plugin_sdk（C-ABI 头）与 snap7（真实 S7 通信）。core 不依赖 snap7。
# 标准布局：include/ 头文件，src/ 源码。

find_package(snap7 CONFIG REQUIRED)

add_library(s7_plugin MODULE src/s7_plugin.cpp)
target_link_libraries(s7_plugin PRIVATE plugin_sdk snap7::snap7)
target_include_directories(s7_plugin PRIVATE include)

set_target_properties(s7_plugin PROPERTIES
    PREFIX ""          # 输出 s7_plugin.dll/.so
    CXX_VISIBILITY_PRESET hidden
)
