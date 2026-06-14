#pragma once

#include <atomic>
#include <stop_token>
#include <string>
#include <thread>

namespace core {
class PluginManager;
}

namespace admin {

/// 本机控制面（admin）通道：Windows **命名管道** / POSIX **AF_UNIX**，**仅本机可达**，
/// 与 IRSP 数据面（WebSocket）解耦——控制类（有副作用）操作不污染数据总线（见 README §7）。
///
/// 每条连接收一行命令（见 [admin_command.hpp]），调 `PluginManager` 执行后回复并关闭连接。
/// 服务循环跑在可中断 `jthread`；卸载/reload 在本线程同步执行（会阻塞至在途写回排空，低频可接受）。
class AdminServer {
  public:
    /// @param manager  须在本服务存活期间有效。
    /// @param endpoint 端点名（空 = 用平台默认）：
    ///                 Windows 命名管道名（默认 `\\.\pipe\industrial-runtime-admin`）/
    ///                 POSIX AF_UNIX 路径（默认 `/tmp/industrial-runtime-admin.sock`）。
    explicit AdminServer(core::PluginManager &manager, std::string endpoint = {});
    ~AdminServer();

    AdminServer(const AdminServer &) = delete;
    AdminServer &operator=(const AdminServer &) = delete;

    /// 建端点并启动服务线程。
    void start();

    /// 停止服务线程并清理端点。析构时自动调用。
    void stop();

    [[nodiscard]] const std::string &endpoint() const noexcept { return endpoint_; }

  private:
    void serve(std::stop_token stopToken);

    core::PluginManager *manager_;
    std::string endpoint_;
    std::jthread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> exited_{false}; ///< serve 循环已退出（Windows stop 据此结束唤醒重试）
#if !defined(_WIN32)
    int listenFd_{-1};
#endif
};

} // namespace admin
