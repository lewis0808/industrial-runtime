#include <chrono>
#include <thread>

#include "runtime_engine/runtime_engine.hpp"
#include "server/irp_server.hpp"
#include "tests/test_util.hpp"

// 冒烟测试：验证 IrpServer 能与 libwebsockets 正常链接，并完成
// 创建 context -> 启动服务线程 -> 停止销毁 的完整生命周期而不崩溃。
// 端口用 0（由 LWS 选取临时端口），避免与真实 9777 冲突。
int main() {
    core::RuntimeEngine runtime;
    runtime.start();

    {
        irp::IrpServer server(runtime, 0);
        server.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        server.stop();
    }

    runtime.stop();

    IR_CHECK(true); // 走到这里即未崩溃
    IR_TEST_REPORT();
}
