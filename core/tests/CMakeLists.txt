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
