# demo-writeback 测试夹具：SET 写回 echo 插件。
# 输出 demo_writeback.dll/.so 到 IndustrialRuntime 可执行文件同级 plugins/，runtime 自动发现。

add_library(irplugin_demo INTERFACE)
target_include_directories(irplugin_demo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(irplugin_demo INTERFACE cxx_std_20)

add_library(demo_writeback MODULE src/demo_writeback.cpp)
target_link_libraries(demo_writeback PRIVATE irplugin_demo)
set_target_properties(demo_writeback PROPERTIES
    PREFIX ""                                    # 输出 demo_writeback.dll（无 lib 前缀）
    CXX_VISIBILITY_PRESET hidden)

# 把 .dll/.so 装到 IndustrialRuntime 输出目录的 plugins/ 子目录，runtime 启动时自动加载
add_custom_command(TARGET demo_writeback POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:IndustrialRuntime>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:demo_writeback>"
            "$<TARGET_FILE_DIR:IndustrialRuntime>/plugins/$<TARGET_FILE_NAME:demo_writeback>")
