#include "admin_server.hpp"

#include <string>
#include <utility>

#include "admin_command.hpp"
#include "admin_endpoint.hpp"
#include "logger/logger.hpp"
#include "plugin_manager/plugin_manager.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace admin {

namespace {

/// 取请求中的首行（去掉换行及之后内容）。
std::string firstLine(const std::string &req) { return req.substr(0, req.find('\n')); }

constexpr std::size_t kMaxRequest = 64 * 1024; // 命令短，防御性上限

} // namespace

AdminServer::AdminServer(core::PluginManager &manager, std::string endpoint)
    : manager_(&manager), endpoint_(endpoint.empty() ? defaultEndpoint() : std::move(endpoint)) {}

AdminServer::~AdminServer() { stop(); }

void AdminServer::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
#if !defined(_WIN32)
    listenFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        IR_LOG_ERROR("admin: 创建 AF_UNIX socket 失败");
        running_.store(false);
        return;
    }
    ::unlink(endpoint_.c_str()); // 清理上次残留的 socket 文件
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, endpoint_.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0 ||
        ::listen(listenFd_, 4) < 0) {
        IR_LOG_ERROR("admin: 绑定/监听 AF_UNIX 失败: {}", endpoint_);
        ::close(listenFd_);
        listenFd_ = -1;
        running_.store(false);
        return;
    }
#endif
    exited_.store(false, std::memory_order_release);
    thread_ = std::jthread([this](std::stop_token st) { serve(st); });
    IR_LOG_INFO("admin 通道监听: {}", endpoint_);
}

void AdminServer::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }
    thread_.request_stop();
#if defined(_WIN32)
    // 反复连哑客户端唤醒阻塞在 ConnectNamedPipe 的服务循环，直到其确实退出（exited_）。
    // 单次哑连接与「serve 新建管道实例」之间有竞态，故重试至 exited_ 为真，避免停机挂死。
    while (!exited_.load(std::memory_order_acquire)) {
        HANDLE c = ::CreateFileA(endpoint_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
        if (c != INVALID_HANDLE_VALUE) {
            ::CloseHandle(c);
        } else {
            ::Sleep(10); // 管道实例尚未就绪/正忙，稍后重试
        }
    }
#endif
    if (thread_.joinable()) {
        thread_.join();
    }
#if !defined(_WIN32)
    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
        ::unlink(endpoint_.c_str());
    }
#endif
    IR_LOG_INFO("admin 通道已停止");
}

#if defined(_WIN32)

void AdminServer::serve(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        HANDLE pipe = ::CreateNamedPipeA(endpoint_.c_str(), PIPE_ACCESS_DUPLEX,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                                         PIPE_UNLIMITED_INSTANCES, 4096, 4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            IR_LOG_ERROR("admin: 创建命名管道失败: {}", endpoint_);
            break;
        }
        const BOOL ok = ::ConnectNamedPipe(pipe, nullptr);
        if (!ok && ::GetLastError() != ERROR_PIPE_CONNECTED) {
            ::CloseHandle(pipe);
            continue;
        }
        if (stopToken.stop_requested()) { // stop() 的哑连接唤醒了我们
            ::DisconnectNamedPipe(pipe);
            ::CloseHandle(pipe);
            break;
        }
        std::string req;
        char buf[512];
        DWORD n = 0;
        while (req.find('\n') == std::string::npos && req.size() < kMaxRequest) {
            if (::ReadFile(pipe, buf, sizeof(buf), &n, nullptr) == 0 || n == 0) {
                break;
            }
            req.append(buf, n);
        }
        const std::string reply = handleAdminCommand(*manager_, firstLine(req));
        DWORD written = 0;
        ::WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &written, nullptr);
        ::FlushFileBuffers(pipe);
        ::DisconnectNamedPipe(pipe);
        ::CloseHandle(pipe);
    }
    exited_.store(true, std::memory_order_release); // 通知 stop() 停止唤醒重试
}

#else

void AdminServer::serve(std::stop_token stopToken) {
    while (!stopToken.stop_requested()) {
        pollfd pfd{};
        pfd.fd = listenFd_;
        pfd.events = POLLIN;
        const int r = ::poll(&pfd, 1, 200); // 200ms 超时以周期性检查 stop
        if (r <= 0) {
            continue; // 超时 / EINTR / 错误：回到循环检查 stop
        }
        const int fd = ::accept(listenFd_, nullptr, nullptr);
        if (fd < 0) {
            continue;
        }
        std::string req;
        char buf[512];
        while (req.find('\n') == std::string::npos && req.size() < kMaxRequest) {
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            req.append(buf, static_cast<std::size_t>(n));
        }
        const std::string reply = handleAdminCommand(*manager_, firstLine(req));
        const ssize_t wn = ::write(fd, reply.data(), reply.size());
        (void)wn;
        ::close(fd);
    }
    exited_.store(true, std::memory_order_release);
}

#endif

} // namespace admin
