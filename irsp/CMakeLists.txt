# ============================================================
# IRP — Industrial Runtime Protocol（对外访问协议）
# 增量 1：协议核心（编解码 / 语义），不依赖网络库，可离线单测。
# 规格见本目录文档（README.md 等）。IRP 单向依赖 core 只读接口；core 不依赖 irp。
# ============================================================

# ---- resp1 编解码 ----
add_library(irp_codec STATIC codec/resp1_codec.cpp)
target_include_directories(irp_codec PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(irp_codec PUBLIC cxx_std_20)

# ---- 语义层：Topic 匹配 / 订阅 Trie / 命令分发 ----
add_library(irp_semantic STATIC
    semantic/topic.cpp
    semantic/topic_trie.cpp
    semantic/dispatcher.cpp)
target_include_directories(irp_semantic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(irp_semantic PUBLIC cxx_std_20)

# ---- 网络依赖（libwebsockets，经 vcpkg） ----
find_package(libwebsockets CONFIG REQUIRED)
# 静态/动态 triplet 导出的目标名不同，择一。
if(TARGET websockets_shared)
    set(IRP_LWS_TARGET websockets_shared)
elseif(TARGET websockets)
    set(IRP_LWS_TARGET websockets)
else()
    message(FATAL_ERROR "未找到 libwebsockets 目标（websockets / websockets_shared）")
endif()

# ---- 服务端：core 绑定 + IRP WebSocket 服务 ----
add_library(irp_server STATIC
    server/core_tag_source.cpp
    server/core_tag_writer.cpp
    server/irp_server.cpp)
target_link_libraries(irp_server
    PUBLIC irp_semantic core_tag_engine
    PRIVATE irp_codec core_runtime_engine core_logger ${IRP_LWS_TARGET})
target_include_directories(irp_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(IR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
