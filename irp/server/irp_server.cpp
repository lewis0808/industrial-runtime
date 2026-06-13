#include "server/irp_server.hpp"

#include <cctype>
#include <chrono>
#include <cstring>
#include <variant>
#include <vector>

#include <libwebsockets.h>

#include "codec/resp1_codec.hpp"
#include "common/event.hpp"
#include "common/tag_value.hpp"
#include "logger/logger.hpp"
#include "runtime_engine/runtime_engine.hpp"

namespace irp {

namespace {

/// libwebsockets 每连接数据：仅存连接 id。
struct PerSession {
    std::uint64_t id;
};

const char *severityLower(core::EventSeverity s) {
    switch (s) {
    case core::EventSeverity::Warning:
        return "warning";
    case core::EventSeverity::Alarm:
        return "alarm";
    case core::EventSeverity::Critical:
        return "critical";
    case core::EventSeverity::Info:
    default:
        return "info";
    }
}

std::int64_t eventNs(const core::Event &e) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(e.timestamp.time_since_epoch())
        .count();
}

std::string encodePushTag(const TagRecord &rec) {
    RespMap m;
    m.entries.emplace_back(makeBulk("push"), makeBulk("tag"));
    m.entries.emplace_back(makeBulk("name"), makeBulk(rec.name));
    m.entries.emplace_back(makeBulk("type"), makeBulk(rec.type));
    m.entries.emplace_back(makeBulk("ts"), makeInteger(rec.ts_ns));
    m.entries.emplace_back(makeBulk("value"), makeBulk(rec.value));
    return Resp1Codec::encode(m);
}

std::string encodePushEvent(const core::Event &e) {
    RespMap m;
    m.entries.emplace_back(makeBulk("push"), makeBulk("event"));
    m.entries.emplace_back(makeBulk("source"), makeBulk(e.source));
    m.entries.emplace_back(makeBulk("category"), makeBulk(e.category));
    m.entries.emplace_back(makeBulk("severity"), makeBulk(severityLower(e.severity)));
    m.entries.emplace_back(makeBulk("ts"), makeInteger(eventNs(e)));
    m.entries.emplace_back(makeBulk("message"), makeBulk(e.message));
    return Resp1Codec::encode(m);
}

bool isByeCommand(const RespValue &request) {
    const auto *arr = std::get_if<RespArray>(&request);
    if (arr == nullptr || arr->items.empty()) {
        return false;
    }
    const auto *b = std::get_if<RespBulk>(&arr->items[0]);
    if (b == nullptr) {
        return false;
    }
    std::string name = b->data;
    for (auto &c : name) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return name == "BYE";
}

/// C 回调：转发到 IrpServer 实例。
int irpLwsCallback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in,
                   std::size_t len) {
    auto *self = static_cast<IrpServer *>(lws_context_user(lws_get_context(wsi)));
    if (self == nullptr) {
        return 0;
    }
    return self->dispatchCallback(wsi, static_cast<int>(reason), user, in, len);
}

} // namespace

IrpServer::IrpServer(core::RuntimeEngine &runtime, std::uint16_t port)
    : runtime_(&runtime), tagSource_(runtime.tags()), writer_(runtime), dispatcher_(tagSource_),
      port_(port) {
    dispatcher_.setWriter(&writer_); // SET 写回出口（路由由 RuntimeEngine.writeHandler 决定）
}

IrpServer::~IrpServer() { stop(); }

IrpServer::Conn *IrpServer::connFor(void *user) {
    if (user == nullptr) {
        return nullptr;
    }
    const auto id = static_cast<PerSession *>(user)->id;
    auto it = conns_.find(id);
    return it == conns_.end() ? nullptr : it->second.get();
}

void IrpServer::processFrame(Conn &conn, const std::string &frame) {
    if (frame.empty()) {
        return;
    }
    RespValue request;
    if (frame[0] == '*') {
        // resp1 编码帧（SDK 正常路径）。
        auto dec = Resp1Codec::decode(frame);
        if (dec.status != Resp1Codec::Status::Ok) {
            conn.outbox.push_back(Resp1Codec::encode(makeError("PROTOCOL_ERROR", "bad frame")));
            conn.closeAfterWrite = true;
            return;
        }
        request = std::move(dec.value);
    } else {
        // 非 '*' 开头：按 Redis 风格 inline 命令解析（调试用，如 wscat 直接敲 "HELLO 1"）。
        request = Resp1Codec::decodeInline(frame);
        const auto *arr = std::get_if<RespArray>(&request);
        if (arr != nullptr && arr->items.empty()) {
            return; // 空行忽略
        }
    }
    const bool bye = isByeCommand(request);
    RespValue reply = dispatcher_.handle(conn.session, request);
    conn.outbox.push_back(Resp1Codec::encode(reply));
    if (bye) {
        conn.closeAfterWrite = true;
    }
}

int IrpServer::dispatchCallback(lws *wsi, int reason, void *user, void *in, std::size_t len) {
    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED: {
        std::lock_guard<std::recursive_mutex> lk(mutex_);
        auto *ps = static_cast<PerSession *>(user);
        const std::uint64_t id = nextConnId_++;
        ps->id = id;
        auto conn = std::make_unique<Conn>();
        conn->id = id;
        conn->wsi = wsi;
        conn->session.id = id;
        conns_[id] = std::move(conn);
        break;
    }

    case LWS_CALLBACK_RECEIVE: {
        std::lock_guard<std::recursive_mutex> lk(mutex_);
        Conn *conn = connFor(user);
        if (conn == nullptr) {
            break;
        }
        conn->rx.append(static_cast<const char *>(in), len);
        if (lws_is_final_fragment(wsi) && lws_remaining_packet_payload(wsi) == 0) {
            std::string frame = std::move(conn->rx);
            conn->rx.clear();
            processFrame(*conn, frame);
            lws_callback_on_writable(wsi);
        }
        break;
    }

    case LWS_CALLBACK_SERVER_WRITEABLE: {
        std::lock_guard<std::recursive_mutex> lk(mutex_);
        Conn *conn = connFor(user);
        if (conn == nullptr) {
            break;
        }
        if (conn->outbox.empty()) {
            return conn->closeAfterWrite ? -1 : 0;
        }
        std::string msg = std::move(conn->outbox.front());
        conn->outbox.pop_front();
        std::vector<unsigned char> buf(LWS_PRE + msg.size());
        std::memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());
        const int n = lws_write(wsi, buf.data() + LWS_PRE, msg.size(), LWS_WRITE_BINARY);
        if (n < static_cast<int>(msg.size())) {
            return -1; // 写失败
        }
        if (!conn->outbox.empty()) {
            lws_callback_on_writable(wsi);
        } else if (conn->closeAfterWrite) {
            return -1;
        }
        break;
    }

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
        // 被 lws_cancel_service 唤醒：为有积压的连接请求可写。
        std::lock_guard<std::recursive_mutex> lk(mutex_);
        for (auto &[id, conn] : conns_) {
            if (!conn->outbox.empty()) {
                lws_callback_on_writable(conn->wsi);
            }
        }
        break;
    }

    case LWS_CALLBACK_CLOSED: {
        std::lock_guard<std::recursive_mutex> lk(mutex_);
        if (user != nullptr) {
            const auto id = static_cast<PerSession *>(user)->id;
            dispatcher_.onSessionClosed(id);
            conns_.erase(id);
        }
        break;
    }

    default:
        break;
    }
    return 0;
}

void IrpServer::serviceLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested() && running_.load(std::memory_order_relaxed)) {
        lws_service(context_, 0);
    }
}

void IrpServer::routeTag(const std::string &name) {
    {
        std::lock_guard<std::recursive_mutex> lk(mutex_);
        const auto ids = dispatcher_.tagSubscribers(name);
        if (ids.empty()) {
            return;
        }
        auto rec = tagSource_.read(name);
        if (!rec) {
            return;
        }
        const std::string frame = encodePushTag(*rec);
        for (const auto id : ids) {
            auto it = conns_.find(id);
            if (it != conns_.end()) {
                it->second->outbox.push_back(frame);
            }
        }
    }
    if (context_ != nullptr) {
        lws_cancel_service(context_);
    }
}

void IrpServer::onEvent(const core::Event &event) {
    {
        std::lock_guard<std::recursive_mutex> lk(mutex_);
        const auto ids =
            dispatcher_.eventSubscribers(static_cast<int>(event.severity), event.category);
        if (ids.empty()) {
            return;
        }
        const std::string frame = encodePushEvent(event);
        for (const auto id : ids) {
            auto it = conns_.find(id);
            if (it != conns_.end()) {
                it->second->outbox.push_back(frame);
            }
        }
    }
    if (context_ != nullptr) {
        lws_cancel_service(context_);
    }
}

void IrpServer::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    lws_set_log_level(LLL_ERR | LLL_WARN, nullptr);

    static struct lws_protocols protocols[] = {
        {"irp", irpLwsCallback, sizeof(PerSession), 65536, 0, nullptr, 0},
        {nullptr, nullptr, 0, 0, 0, nullptr, 0},
    };

    struct lws_context_creation_info info;
    std::memset(&info, 0, sizeof(info));
    info.port = port_;
    info.protocols = protocols;
    info.user = this;
    info.gid = -1;
    info.uid = -1;

    context_ = lws_create_context(&info);
    if (context_ == nullptr) {
        running_.store(false, std::memory_order_relaxed);
        IR_LOG_ERROR("IRP: 创建 libwebsockets context 失败（端口 {}）", port_);
        return;
    }

    service_ = std::jthread([this](std::stop_token st) { serviceLoop(st); });

    // 注册 core 回调（推送来源）。
    runtime_->tags().setChangeCallback([this](const core::TagValue &tag) { routeTag(tag.name); });
    eventSubId_ = runtime_->events().subscribe([this](const core::Event &e) { onEvent(e); });

    IR_LOG_INFO("IRP server 监听 ws://0.0.0.0:{}", port_);
}

void IrpServer::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    // 注销 core 回调，避免停机途中回调进来。
    runtime_->tags().setChangeCallback(nullptr);
    if (eventSubId_ != 0) {
        runtime_->events().unsubscribe(eventSubId_);
        eventSubId_ = 0;
    }

    service_.request_stop();
    if (context_ != nullptr) {
        lws_cancel_service(context_); // 唤醒服务线程以便退出
    }
    if (service_.joinable()) {
        service_.join();
    }
    if (context_ != nullptr) {
        lws_context_destroy(context_);
        context_ = nullptr;
    }
    IR_LOG_INFO("IRP server 已停止");
}

} // namespace irp
