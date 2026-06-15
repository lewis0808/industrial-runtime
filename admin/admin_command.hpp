#pragma once

#include <string>
#include <string_view>

namespace core {
class PluginManager;
}

namespace admin {

/// 解析并执行一条 admin 命令行，返回回复文本（以 `\n` 结尾）。**纯逻辑、无 I/O**，可单测。
///
/// 命令（命令名/子命令大小写不敏感，按空白分词；尾随 `\r` 视为空白）：
///   - `PLUGIN LIST`         → `OK <n>\n` 后跟 n 行 `<id>\t<name>\t<version>\t<0|1>\n`
///   - `PLUGIN UNLOAD <id>`  → `OK\n` / `ERR <reason>\n`
///   - `PLUGIN RELOAD <id>`  → `OK\n` / `ERR <reason>\n`
///   - 其它/参数错           → `ERR <reason>\n`
std::string handleAdminCommand(core::PluginManager &pm, std::string_view line);

} // namespace admin
