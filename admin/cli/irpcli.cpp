// irpcli —— 工业运行时控制面 CLI（industrial runtime process cli）。
//
// 连接本机 admin 通道（Windows 命名管道 / POSIX AF_UNIX），把命令原样转发给运行时的
// AdminServer 并打印回复。服务端负责命令语义（大小写不敏感），CLI 只做薄转发。
//
// 用法：
//   irpcli plugin list
//   irpcli plugin reload <id>
//   irpcli plugin unload <id>
//   irpcli [--endpoint <name|path>] <command...>
//
// 退出码：0 成功；1 服务端返回 ERR；2 用法错误；3 连接/通信失败。

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "admin_endpoint.hpp"

#if defined(_WIN32)
#define NOMINMAX // 避免 <windows.h> 的 max/min 宏破坏 std::max
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {

void printUsage() {
    std::fputs("irpcli —— 工业运行时控制面 CLI\n"
               "用法:\n"
               "  irpcli plugin list                列出已加载插件\n"
               "  irpcli plugin reload <id>         重载插件\n"
               "  irpcli plugin unload <id>         卸载插件\n"
               "  irpcli plugin scan                重新扫描插件目录，加载新插件\n"
               "选项:\n"
               "  --endpoint <name|path>            覆盖 admin 端点（默认本机管道/socket）\n"
               "  -h, --help                        显示帮助\n",
               stderr);
}

bool iequals(const std::string &a, const char *b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t p = s.find(delim, start);
        if (p == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

/// 把 `PLUGIN LIST` 的回复（首行 `OK <n>`，其后每行 id\tname\tversion\tstarted）渲染为对齐表格。
/// 列宽按字节计（CJK 名称可能略不齐，可接受）。
void printPluginTable(const std::string &reply) {
    const char *hdr[4] = {"ID", "NAME", "VERSION", "STARTED"};
    std::size_t w[4];
    for (int i = 0; i < 4; ++i) {
        w[i] = std::strlen(hdr[i]);
    }
    std::vector<std::vector<std::string>> rows;
    const auto lines = split(reply, '\n');
    for (std::size_t i = 1; i < lines.size(); ++i) { // 跳过首行 OK <n>
        if (lines[i].empty()) {
            continue;
        }
        auto f = split(lines[i], '\t');
        f.resize(4);
        f[3] = (f[3] == "1") ? "yes" : "no";
        for (int c = 0; c < 4; ++c) {
            w[c] = std::max(w[c], f[c].size());
        }
        rows.push_back(std::move(f));
    }
    if (rows.empty()) {
        std::printf("无已加载插件。\n");
        return;
    }
    for (int i = 0; i < 4; ++i) {
        std::printf("%-*s  ", static_cast<int>(w[i]), hdr[i]);
    }
    std::printf("\n");
    for (const auto &r : rows) {
        for (int i = 0; i < 4; ++i) {
            std::printf("%-*s  ", static_cast<int>(w[i]), r[i].c_str());
        }
        std::printf("\n");
    }
    std::printf("\n%zu plugin(s)\n", rows.size());
}

/// 连接 admin 端点，发送一行命令，返回服务端回复全文。失败时填 err 返回空串。
std::string sendCommand(const std::string &endpoint, const std::string &line, std::string &err);

#if defined(_WIN32)

std::string sendCommand(const std::string &endpoint, const std::string &line, std::string &err) {
    HANDLE h = ::CreateFileA(endpoint.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE && ::GetLastError() == ERROR_PIPE_BUSY) {
        if (::WaitNamedPipeA(endpoint.c_str(), 2000) != 0) {
            h = ::CreateFileA(endpoint.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
        }
    }
    if (h == INVALID_HANDLE_VALUE) {
        err = "无法连接 admin 通道（运行时是否在运行？端点: " + endpoint + "）";
        return {};
    }
    const std::string req = line + "\n";
    DWORD written = 0;
    ::WriteFile(h, req.data(), static_cast<DWORD>(req.size()), &written, nullptr);
    ::FlushFileBuffers(h);
    std::string out;
    char buf[512];
    DWORD n = 0;
    while (::ReadFile(h, buf, sizeof(buf), &n, nullptr) != 0 && n > 0) {
        out.append(buf, n);
    }
    ::CloseHandle(h);
    return out;
}

#else

std::string sendCommand(const std::string &endpoint, const std::string &line, std::string &err) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        err = "无法创建 socket";
        return {};
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, endpoint.c_str(), sizeof(addr.sun_path) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        err = "无法连接 admin 通道（运行时是否在运行？端点: " + endpoint + "）";
        return {};
    }
    const std::string req = line + "\n";
    (void)::write(fd, req.data(), req.size());
    std::string out;
    char buf[512];
    ssize_t n = 0;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);
    return out;
}

#endif

} // namespace

int main(int argc, char **argv) {
#if defined(_WIN32)
    ::SetConsoleOutputCP(CP_UTF8);
#endif
    std::string endpoint = admin::defaultEndpoint();
    std::vector<std::string> parts;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--endpoint") && i + 1 < argc) {
            endpoint = argv[++i];
        } else if (a == "-h" || a == "--help") {
            printUsage();
            return 0;
        } else {
            parts.push_back(a);
        }
    }
    if (parts.empty()) {
        printUsage();
        return 2;
    }

    std::string line;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            line += ' ';
        }
        line += parts[i];
    }

    std::string err;
    const std::string reply = sendCommand(endpoint, line, err);
    if (!err.empty()) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 3;
    }
    if (reply.rfind("ERR", 0) == 0) { // 服务端 ERR -> stderr + 退出码 1
        std::fputs(reply.c_str(), stderr);
        return 1;
    }
    // list 渲染为对齐表格、scan 渲染为计数；其它命令（reload/unload）回复短，原样打印（OK）。
    const bool isPlugin = parts.size() >= 2 && iequals(parts[0], "plugin");
    if (isPlugin && iequals(parts[1], "list")) {
        printPluginTable(reply);
    } else if (isPlugin && iequals(parts[1], "scan")) {
        const auto toks = split(split(reply, '\n')[0], ' '); // "OK <n>"
        std::printf("已加载 %s 个新插件\n", (toks.size() >= 2 ? toks[1] : "0").c_str());
    } else {
        std::fputs(reply.c_str(), stdout);
    }
    return 0;
}
