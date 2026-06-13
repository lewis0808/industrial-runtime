# 单元测试：每个测试是独立可执行文件，返回非零即失败。

# test_name -> 依赖的 core 库
function(ir_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

ir_add_test(test_types        core_common)
ir_add_test(test_mpmc_queue   core_event_bus)
ir_add_test(test_tag_engine   core_tag_engine)
ir_add_test(test_event_bus    core_event_bus)
ir_add_test(test_memory_store core_memory_store)
ir_add_test(test_config       core_config)
ir_add_test(test_scheduler    core_scheduler)

# 端到端：加载示例插件 DLL，验证数据经 RuntimeApi 进入 core。
add_executable(test_plugin_e2e test_plugin_e2e.cpp)
target_link_libraries(test_plugin_e2e
    PRIVATE core_runtime_engine core_plugin_host core_plugin_manager plugin_sdk)
add_dependencies(test_plugin_e2e example_plugin)
target_compile_definitions(test_plugin_e2e
    PRIVATE EXAMPLE_PLUGIN_PATH="$<TARGET_FILE:example_plugin>")
add_test(NAME test_plugin_e2e COMMAND test_plugin_e2e)
