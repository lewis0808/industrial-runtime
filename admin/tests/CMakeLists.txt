# admin 单元测试。复用 core/tests/test_util.hpp（零外部框架）。

add_executable(test_admin_command test_admin_command.cpp)
# 直接构造 PluginManager（需 PluginHost + RuntimeApi），故显式链 core 三件。
target_link_libraries(test_admin_command
    PRIVATE admin_server core_plugin_manager core_plugin_host core_runtime_engine)
# 经 core/ include 根解析 "tests/test_util.hpp" 与 core 头。
target_include_directories(test_admin_command PRIVATE ${CMAKE_SOURCE_DIR}/core)
add_test(NAME test_admin_command COMMAND test_admin_command)
