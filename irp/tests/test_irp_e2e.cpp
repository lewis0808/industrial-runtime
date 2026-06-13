// 端到端集成测试：真实 WebSocket 客户端连到 IrpServer，跑通
// HELLO -> GET -> SUBSCRIBE -> 触发 Tag 变化 -> 收到 tag 推送。
//
// 客户端与服务端各自一个 libwebsockets context（同进程，分别在不同线程/循环）。

#include <chrono>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <libwebsockets.h>

#include "codec/resp1_codec.hpp"
#include "codec/resp_value.hpp"
#include "runtime_engine/runtime_engine.hpp"
#include "server/irp_server.hpp"
#include "tests/test_util.hpp"

using namespace irp;

namespace {

constexpr std::uint16_t TEST_PORT = 19790;

struct ClientState {
    core::RuntimeEngine *runtime{nullptr};
    struct lws *wsi{nullptr};
    std::deque<std::string> outbox;
    std::string rx;
    int step{0};
    bool helloOk{false};
    bool getOk{false};
    bool subOk{false};
    bool gotPush{false};
    bool done{false};
    bool failed{false};
};

std::string encodeCmd(const std::vector<std::string> &parts) {
    RespArray a;
    for (const auto &p : parts) {
        a.items.push_back(makeBulk(p));
    }
    return Resp1Codec::encode(a);
}

const std::string *mapGet(const RespValue &v, const std::string &key) {
    const auto *m = std::get_if<RespMap>(&v);
    if (m == nullptr) {
        return nullptr;
    }
    for (const auto &[k, val] : m->entries) {
        const auto *kb = std::get_if<RespBulk>(&k);
        if (kb != nullptr && kb->data == key) {
            const auto *vb = std::get_if<RespBulk>(&val);
            return vb != nullptr ? &vb->data : nullptr;
        }
    }
    return nullptr;
}

void sendCmd(ClientState &s, const std::vector<std::string> &parts) {
    s.outbox.push_back(encodeCmd(parts));
    lws_callback_on_writable(s.wsi);
}

void onReply(ClientState &s, const RespValue &v) {
    switch (s.step) {
    case 1: { // HELLO 回复
        const auto *enc = mapGet(v, "encoding");
        s.helloOk = (enc != nullptr && *enc == "resp1");
        s.step = 2;
        sendCmd(s, {"GET", "plant/temp"});
        break;
    }
    case 2: { // GET 回复
        const auto *type = mapGet(v, "type");
        const auto *val = mapGet(v, "value");
        if (type != nullptr && *type == "f64" && val != nullptr && val->size() == 8) {
            double d = 0;
            std::memcpy(&d, val->data(), 8);
            s.getOk = (d == 42.0);
        }
        s.step = 3;
        sendCmd(s, {"SUBSCRIBE", "plant/#"});
        break;
    }
    case 3: { // SUBSCRIBE 回复（整数）
        s.subOk = std::holds_alternative<RespInteger>(v);
        s.step = 4;
        // 触发 Tag 变化 -> 服务端应推送。
        s.runtime->pushTag(core::TagValue{"plant/temp", 43.0});
        break;
    }
    case 4: { // 期待 tag 推送
        const auto *push = mapGet(v, "push");
        const auto *name = mapGet(v, "name");
        if (push != nullptr && *push == "tag" && name != nullptr && *name == "plant/temp") {
            s.gotPush = true;
        }
        s.done = true;
        break;
    }
    default:
        break;
    }
}

int clientCallback(struct lws *wsi, enum lws_callback_reasons reason, void * /*user*/, void *in,
                   std::size_t len) {
    auto *s = static_cast<ClientState *>(lws_context_user(lws_get_context(wsi)));
    if (s == nullptr) {
        return 0;
    }
    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        s->wsi = wsi;
        s->step = 1;
        sendCmd(*s, {"HELLO", "1"});
        break;

    case LWS_CALLBACK_CLIENT_RECEIVE: {
        s->rx.append(static_cast<const char *>(in), len);
        if (lws_is_final_fragment(wsi) && lws_remaining_packet_payload(wsi) == 0) {
            auto dec = Resp1Codec::decode(s->rx);
            s->rx.clear();
            if (dec.status == Resp1Codec::Status::Ok) {
                onReply(*s, dec.value);
            } else {
                s->failed = true;
                s->done = true;
            }
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
        if (s->outbox.empty()) {
            break;
        }
        std::string msg = std::move(s->outbox.front());
        s->outbox.pop_front();
        std::vector<unsigned char> buf(LWS_PRE + msg.size());
        std::memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());
        lws_write(wsi, buf.data() + LWS_PRE, msg.size(), LWS_WRITE_BINARY);
        if (!s->outbox.empty()) {
            lws_callback_on_writable(wsi);
        }
        break;
    }

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        s->failed = true;
        s->done = true;
        break;

    default:
        break;
    }
    return 0;
}

} // namespace

int main() {
    core::RuntimeEngine runtime;
    runtime.start();
    runtime.pushTag(core::TagValue{"plant/temp", 42.0});

    IrpServer server(runtime, TEST_PORT);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 待监听就绪

    // 客户端 context。
    ClientState state;
    state.runtime = &runtime;

    static struct lws_protocols protocols[] = {
        {"irp", clientCallback, 0, 65536, 0, nullptr, 0},
        {nullptr, nullptr, 0, 0, 0, nullptr, 0},
    };

    struct lws_context_creation_info info;
    std::memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.user = &state;
    info.gid = -1;
    info.uid = -1;

    lws_set_log_level(LLL_ERR, nullptr);
    struct lws_context *ctx = lws_create_context(&info);
    IR_CHECK(ctx != nullptr);

    if (ctx != nullptr) {
        struct lws_client_connect_info cci;
        std::memset(&cci, 0, sizeof(cci));
        cci.context = ctx;
        cci.address = "127.0.0.1";
        cci.port = TEST_PORT;
        cci.path = "/";
        cci.host = "127.0.0.1";
        cci.origin = "127.0.0.1";
        cci.protocol = "irp";
        cci.ssl_connection = 0;
        cci.ietf_version_or_minus_one = -1;
        lws_client_connect_via_info(&cci);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!state.done && std::chrono::steady_clock::now() < deadline) {
            lws_service(ctx, 50);
        }
        lws_context_destroy(ctx);
    }

    server.stop();
    runtime.stop();

    IR_CHECK(!state.failed);
    IR_CHECK(state.helloOk);
    IR_CHECK(state.getOk);
    IR_CHECK(state.subOk);
    IR_CHECK(state.gotPush);

    IR_TEST_REPORT();
}
