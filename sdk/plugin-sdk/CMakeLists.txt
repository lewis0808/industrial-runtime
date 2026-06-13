# Plugin SDK：插件开发 SDK（纯头文件）。
# 提供 plugin_abi.h（C ABI 契约）与 plugin.hpp（C++ IPlugin / Host 封装）。
# 宿主（core）与插件 DLL 均依赖此 INTERFACE 目标。

add_library(plugin_sdk INTERFACE)
target_include_directories(plugin_sdk INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(plugin_sdk INTERFACE cxx_std_20)
