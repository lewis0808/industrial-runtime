# IRP 单元测试。复用 core/tests 的极简断言头（零外部框架）。

add_executable(test_resp1_codec test_resp1_codec.cpp)
target_link_libraries(test_resp1_codec PRIVATE irp_codec)
# 复用 core/tests/test_util.hpp：通过 core 作为额外 include 根解析 "tests/test_util.hpp"
target_include_directories(test_resp1_codec PRIVATE ${CMAKE_SOURCE_DIR}/core)
add_test(NAME test_resp1_codec COMMAND test_resp1_codec)

add_executable(test_topic test_topic.cpp)
target_link_libraries(test_topic PRIVATE irp_semantic)
target_include_directories(test_topic PRIVATE ${CMAKE_SOURCE_DIR}/core)
add_test(NAME test_topic COMMAND test_topic)

add_executable(test_dispatcher test_dispatcher.cpp)
target_link_libraries(test_dispatcher PRIVATE irp_semantic)
target_include_directories(test_dispatcher PRIVATE ${CMAKE_SOURCE_DIR}/core)
add_test(NAME test_dispatcher COMMAND test_dispatcher)

add_executable(test_core_tag_source test_core_tag_source.cpp)
target_link_libraries(test_core_tag_source PRIVATE irp_server)
target_include_directories(test_core_tag_source PRIVATE ${CMAKE_SOURCE_DIR}/core)
add_test(NAME test_core_tag_source COMMAND test_core_tag_source)

add_executable(test_irp_server_smoke test_irp_server_smoke.cpp)
target_link_libraries(test_irp_server_smoke PRIVATE irp_server core_runtime_engine)
target_include_directories(test_irp_server_smoke PRIVATE ${CMAKE_SOURCE_DIR}/core)
add_test(NAME test_irp_server_smoke COMMAND test_irp_server_smoke)

# 端到端：真实 WS 客户端（需直接用 libwebsockets 与 codec）。
add_executable(test_irp_e2e test_irp_e2e.cpp)
target_link_libraries(test_irp_e2e
    PRIVATE irp_server core_runtime_engine irp_codec ${IRP_LWS_TARGET})
target_include_directories(test_irp_e2e PRIVATE ${CMAKE_SOURCE_DIR}/core)
add_test(NAME test_irp_e2e COMMAND test_irp_e2e)

# 注：SET 写回的端到端测试原依赖内置示例插件，随插件移出本仓库而移除；
# 该链路由独立插件工程（如 industrial-runtime-s7）的集成测试覆盖。
