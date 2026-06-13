# ============================================================
# Core Runtime —— 工业数据运行时核心
# 每个模块编译为独立静态库，依赖边在此集中表达，保持模块低耦合。
# ============================================================

find_package(Threads REQUIRED)

# ---- 共享类型（仅头文件） ----
add_library(core_common INTERFACE)
target_include_directories(core_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(core_common INTERFACE cxx_std_20)

# ---- Logger ----
add_library(core_logger STATIC logger/logger.cpp)
target_link_libraries(core_logger PUBLIC core_common spdlog::spdlog)

# ---- Config ----
add_library(core_config STATIC config/config.cpp)
target_link_libraries(core_config PUBLIC core_common nlohmann_json::nlohmann_json)

# ---- TagEngine ----
add_library(core_tag_engine STATIC tag_engine/tag_engine.cpp)
target_link_libraries(core_tag_engine PUBLIC core_common Threads::Threads)

# ---- EventBus ----
add_library(core_event_bus STATIC event_bus/event_bus.cpp)
target_link_libraries(core_event_bus PUBLIC core_common Threads::Threads)

# ---- MemoryStore ----
add_library(core_memory_store STATIC memory_store/memory_store.cpp)
target_link_libraries(core_memory_store PUBLIC core_common Threads::Threads)

# ---- Scheduler ----
add_library(core_scheduler STATIC scheduler/scheduler.cpp)
target_link_libraries(core_scheduler PUBLIC core_common Threads::Threads)

# ---- RuntimeEngine（编排层，依赖全部子系统） ----
add_library(core_runtime_engine STATIC runtime_engine/runtime_engine.cpp)
target_link_libraries(core_runtime_engine
    PUBLIC
        core_common
        core_tag_engine
        core_event_bus
        core_memory_store
        core_scheduler
        core_config
        core_logger
)

# ---- PluginHost（RuntimeApi -> C-ABI 宿主接口适配） ----
add_library(core_plugin_host STATIC plugin_host/plugin_host.cpp)
target_link_libraries(core_plugin_host PUBLIC core_common core_runtime_engine plugin_sdk)

# ---- PluginManager（跨平台 DLL 加载 + 生命周期） ----
add_library(core_plugin_manager STATIC plugin_manager/plugin_manager.cpp)
target_link_libraries(core_plugin_manager PUBLIC core_common plugin_sdk PRIVATE core_logger)
if(NOT WIN32)
    target_link_libraries(core_plugin_manager PRIVATE ${CMAKE_DL_LIBS})
endif()

# ---- 可执行入口 ----
add_executable(IndustrialRuntime main.cpp)
target_link_libraries(IndustrialRuntime PRIVATE core_runtime_engine core_logger)

# ---- 单元测试 ----
if(IR_BUILD_TESTS)
    add_subdirectory(tests)
endif()
