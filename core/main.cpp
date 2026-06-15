//
// Created by lewis on 2026/6/13.
//
// 工业数据运行时入口：引导 RuntimeEngine，演示 Tag/Event 数据流，
// 支持 Ctrl+C 优雅退出。

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>

#include "admin_server.hpp"
#include "config/config.hpp"
#include "logger/logger.hpp"
#include "plugin_host/plugin_host.hpp"
#include "plugin_manager/plugin_manager.hpp"
#include "runtime_engine/runtime_engine.hpp"
#include "server/irsp_server.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) { g_running.store(false, std::memory_order_relaxed); }

/// 返回当前可执行文件所在目录。用于定位同级 plugins/ 插件目录。
std::filesystem::path exeDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::current_path() : self.parent_path();
#endif
}

} // namespace

int main(int argc, char **argv) {
#if defined(_WIN32)
    // 控制台按 UTF-8 渲染日志（源码与日志均为 UTF-8，避免中文乱码）。
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    core::Config config;
    if (argc > 1) {
        if (!config.loadFile(argv[1])) {
            core::Logger::get()->warn("配置文件加载失败，使用默认配置: {}", argv[1]);
        }
    }

    core::RuntimeEngine runtime;
    runtime.init(config);

    // 订阅事件用于演示。
    runtime.events().subscribe([](const core::Event &e) {
        IR_LOG_INFO("[事件] [{}] {}: {}", core::eventSeverityName(e.severity), e.category,
                    e.message);
    });

    runtime.start();

    // 装配设备插件与写回出口：写回按 topic 前缀路由到对应插件。
    core::PluginHost pluginHost(runtime);
    runtime.setWriteHandler([&pluginHost](const core::TagValue &t) { return pluginHost.write(t); });
    core::PluginManager pluginManager(pluginHost);
    // 自动发现：扫描可执行文件同级的 plugins/ 加载所有插件动态库；各插件的配置取自
    // 同级 config/<dll basename>.json（plugins 与 config 分目录）。设备配置不进主配置，
    // 宿主只把配置路径透传给插件，由插件自行解析（缺失则用内置默认）。
    const auto pluginDir = (exeDir() / "plugins").string();
    const auto configDir = (exeDir() / "config").string();
    const auto loaded = pluginManager.loadDirectory(pluginDir, configDir);
    IR_LOG_INFO("插件目录 {}，配置目录 {}，已加载 {} 个插件", pluginDir, configDir, loaded);
    pluginManager.startAll();

    // 启动对外 IRSP WebSocket 服务（端口可经配置 irsp.port 覆盖，默认 9777）。
    const auto irspPort = static_cast<std::uint16_t>(config.get<int>("irsp.port", 9777));
    irsp::IrspServer irspServer(runtime, irspPort);
    irspServer.start();

    // 本机控制面：admin 通道（Windows 命名管道 / POSIX AF_UNIX），运行期管理插件生命周期
    // （PLUGIN LIST/UNLOAD/RELOAD），与 IRSP 数据面解耦、仅本机可达。
    admin::AdminServer adminServer(pluginManager);
    adminServer.start();

    // 示例：每秒采集一个心跳 Tag 并发布一次状态事件。
    runtime.scheduler().addPeriodicTask("heartbeat", std::chrono::seconds(1), [&runtime] {
        static std::int64_t counter = 0;
        ++counter;
        runtime.pushTag(core::TagValue{"system/heartbeat", counter});
        runtime.pushEvent(core::Event{"runtime", "state", "heartbeat #" + std::to_string(counter),
                                      core::EventSeverity::Info});
    });

    IR_LOG_INFO("运行时已就绪，按 Ctrl+C 退出");
    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    IR_LOG_INFO("收到退出信号，正在停止...");
    adminServer.stop();
    irspServer.stop();
    pluginManager.stopAll();
    runtime.stop();
    core::Logger::flush();
    return 0;
}
