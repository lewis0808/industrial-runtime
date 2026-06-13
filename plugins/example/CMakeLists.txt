# 示例插件：动态加载的 MODULE 库（.dll / .so）。仅依赖 plugin_sdk。

add_library(example_plugin MODULE example_plugin.cpp)
target_link_libraries(example_plugin PRIVATE plugin_sdk)

set_target_properties(example_plugin PROPERTIES
    PREFIX ""          # 不加 lib 前缀，输出 example_plugin.dll/.so
    CXX_VISIBILITY_PRESET hidden
)
