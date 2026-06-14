#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "semantic/dispatcher.hpp"
#include "server/core_tag_source.hpp"
#include "server/core_tag_writer.hpp"

// 仅前向声明 libwebsockets 类型，<libwebsockets.h> 只在 .cpp 引入。
struct lws;
struct lws_context;

namespace core {
class RuntimeEngine;
class TagValue;
class Event;
} // namespace core

namespace irp {

/// IRP WebSocket 服务端（基于 libwebsockets）。
///
/// 服务循环跑在独立线程。每条连接维护 Session + 发送队列；接收整帧后经 Dispatcher
/// 分发并回复。core 的 TagEngine 变更回调 / EventBus 事件经 lws_cancel_service 唤醒
/// 服务线程，在可写回调中推送给订阅连接。
///
/// IRP 单向依赖 core 只读接口；core 不依赖 irp。
class IrpServer {
  public:
    explicit IrpServer(core::RuntimeEngine &runtime, std::uint16_t port = 9777);
    ~IrpServer();

    IrpServer(const IrpServer &) = delete;
    IrpServer &operator=(const IrpServer &) = delete;

    /// 创建 LWS context、注册 core 回调、启动服务线程。
    void start();

    /// 停止服务线程并销毁 context。析构时自动调用。
    void stop();

    [[nodiscard]] std::uint16_t port() const noexcept { return port_; }

    /// libwebsockets 回调转发入口（由 .cpp 内的 C 回调调用，勿直接使用）。
    int dispatchCallback(lws *wsi, int reason, void *user, void *in, std::size_t len);

  private:
    struct Conn {
        std::uint64_t id{0};
        lws *wsi{nullptr};
        Session session;
        std::deque<std::string> outbox; ///< 待发送帧（payload，不含 LWS_PRE）
        std::string rx;                 ///< 接收分片累积
        bool closeAfterWrite{false};
    };

    void serviceLoop(std::stop_token stopToken);

    // core 线程回调：路由推送。
    void routeTag(const std::string &name);
    void onEvent(const core::Event &event);

    [[nodiscard]] Conn *connFor(void *user);
    void processFrame(Conn &conn, const std::string &frame);

    core::RuntimeEngine *runtime_;
    CoreTagSource tagSource_;
    CoreTagWriter writer_;
    Dispatcher dispatcher_;
    std::uint16_t port_;

    lws_context *context_{nullptr};
    std::jthread service_;
    std::atomic<bool> running_{false};

    // 递归锁：SET 写回会在持锁的 RECEIVE 内经 pushTag 触发变更回调 routeTag 再次入锁（同线程重入）。
    std::recursive_mutex mutex_; ///< 保护 dispatcher_ 与 conns_
    std::unordered_map<std::uint64_t, std::unique_ptr<Conn>> conns_;
    std::uint64_t nextConnId_{1};
    std::uint64_t eventSubId_{0};
};

} // namespace irp
