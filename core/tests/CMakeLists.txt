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
ir_add_test(test_plugin_host  core_plugin_host)

# 注：插件加载 / 写回 / S7 的端到端测试随设备插件移出本仓库（插件改为独立工程开发）。
# 内核侧插件加载能力（plugin_host / plugin_manager）仍在，由独立插件工程的集成测试覆盖。
# test_plugin_host 仅覆盖宿主封送/写回路由与跨 C-ABI 边界的异常隔离（无需真实 DLL）。

# 测试夹具插件：供 sdk/irsp-client/JS/examples 写回端到端测试用。
add_subdirectory(fixtures/demo-writeback)
