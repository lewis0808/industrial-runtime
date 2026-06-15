# ============================================================
# admin —— 本机控制面通道（Windows 命名管道 / POSIX AF_UNIX）
# 控制类（有副作用）操作与 IRSP 数据面解耦，不依赖 irsp / 网络库。
# ============================================================

add_library(admin_server STATIC
    admin_command.cpp
    admin_server.cpp)
target_include_directories(admin_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(admin_server PUBLIC cxx_std_20)
# core_plugin_manager 为 PUBLIC core_plugin_host/core_common，提供 core/ include 根。
target_link_libraries(admin_server PRIVATE core_plugin_manager core_logger)

# ---- irpcli：控制面 CLI（admin 通道客户端，独立可执行，无 core 依赖） ----
add_executable(irpcli cli/irpcli.cpp)
target_include_directories(irpcli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}) # admin_endpoint.hpp
target_compile_features(irpcli PRIVATE cxx_std_20)

if(IR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
