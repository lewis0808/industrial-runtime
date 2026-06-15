#pragma once

namespace admin {

/// admin 控制面默认端点：Windows 命名管道名 / POSIX AF_UNIX 路径。
/// 服务端（AdminServer）与客户端（irpcli）共享同一默认值，避免两边漂移。
inline const char *defaultEndpoint() noexcept {
#if defined(_WIN32)
    return "\\\\.\\pipe\\industrial-runtime-admin";
#else
    return "/tmp/industrial-runtime-admin.sock";
#endif
}

} // namespace admin
