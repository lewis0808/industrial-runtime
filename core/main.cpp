//
// Created by lewis on 2026/6/13.
//
// 工业数据运行时入口：引导 RuntimeEngine，演示 Tag/Event 数据流，
// 支持 Ctrl+C 优雅退出。

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <thread>

#include "config/config.hpp"
#include "logger/logger.hpp"
#include "runtime_engine/runtime_engine.hpp"
#include "server/irp_server.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

std::atomic<bool> g_running{true};

void handleSignal(int) {
    g_running.store(false, std::memory_order_relaxed);
}

}  // namespace

int main(int argc, char** argv) {
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
    runtime.events().subscribe([](const core::Event& e) {
        IR_LOG_INFO("[事件] [{}] {}: {}", core::eventSeverityName(e.severity),
                    e.category, e.message);
    });

    runtime.start();

    // 启动对外 IRP WebSocket 服务（端口可经配置 irp.port 覆盖，默认 9777）。
    const auto irpPort = static_cast<std::uint16_t>(config.get<int>("irp.port", 9777));
    irp::IrpServer irpServer(runtime, irpPort);
    irpServer.start();

    // 示例：每秒采集一个心跳 Tag 并发布一次状态事件。
    runtime.scheduler().addPeriodicTask(
        "heartbeat", std::chrono::seconds(1), [&runtime] {
            static std::int64_t counter = 0;
            ++counter;
            runtime.pushTag(core::TagValue{"system/heartbeat", counter});
            runtime.pushEvent(core::Event{"runtime", "state",
                                          "heartbeat #" + std::to_string(counter),
                                          core::EventSeverity::Info});
        });

    IR_LOG_INFO("运行时已就绪，按 Ctrl+C 退出");
    while (g_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    IR_LOG_INFO("收到退出信号，正在停止...");
    irpServer.stop();
    runtime.stop();
    core::Logger::flush();
    return 0;
}
