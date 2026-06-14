#include "admin_command.hpp"

#include <cctype>
#include <sstream>
#include <vector>

#include "plugin_manager/plugin_manager.hpp"

namespace admin {

namespace {

std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (i > start) {
            out.emplace_back(line.substr(start, i - start));
        }
    }
    return out;
}

std::string toUpper(std::string s) {
    for (auto &c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

} // namespace

std::string handleAdminCommand(core::PluginManager &pm, std::string_view line) {
    const auto t = tokenize(line);
    if (t.empty()) {
        return "ERR empty command\n";
    }
    if (toUpper(t[0]) != "PLUGIN") {
        return "ERR unknown command (only PLUGIN supported)\n";
    }
    if (t.size() < 2) {
        return "ERR usage: PLUGIN LIST | UNLOAD <id> | RELOAD <id>\n";
    }
    const std::string sub = toUpper(t[1]);
    if (sub == "LIST") {
        if (t.size() != 2) {
            return "ERR usage: PLUGIN LIST\n";
        }
        const auto plugins = pm.list();
        std::ostringstream os;
        os << "OK " << plugins.size() << "\n";
        for (const auto &p : plugins) {
            os << p.id << "\t" << p.name << "\t" << p.version << "\t" << (p.started ? 1 : 0)
               << "\n";
        }
        return os.str();
    }
    if (sub == "UNLOAD") {
        if (t.size() != 3) {
            return "ERR usage: PLUGIN UNLOAD <id>\n";
        }
        return pm.unload(t[2]) ? "OK\n" : "ERR not found\n";
    }
    if (sub == "RELOAD") {
        if (t.size() != 3) {
            return "ERR usage: PLUGIN RELOAD <id>\n";
        }
        return pm.reload(t[2]) ? "OK\n" : "ERR reload failed or not found\n";
    }
    if (sub == "SCAN") {
        if (t.size() != 2) {
            return "ERR usage: PLUGIN SCAN\n";
        }
        return "OK " + std::to_string(pm.scan()) + "\n"; // 新加载数量
    }
    return "ERR unknown PLUGIN subcommand\n";
}

} // namespace admin
