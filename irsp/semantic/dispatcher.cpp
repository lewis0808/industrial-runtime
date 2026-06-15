#include "semantic/dispatcher.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <variant>

#include "semantic/topic.hpp"

namespace irsp {

namespace {

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

/// 严重级别字符串 -> 排名；非法返回 -1。
int severityRank(const std::string &s) {
    if (s == "info")
        return 0;
    if (s == "warning")
        return 1;
    if (s == "alarm")
        return 2;
    if (s == "critical")
        return 3;
    return -1;
}

bool parseCount(const std::string &s, std::size_t &out) {
    const char *begin = s.data();
    const char *end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr == end;
}

IrspValue tagToMap(const TagRecord &rec) {
    IrspMap m;
    m.entries.emplace_back(makeBulk("name"), makeBulk(rec.name));
    m.entries.emplace_back(makeBulk("type"), makeBulk(rec.type));
    m.entries.emplace_back(makeBulk("ts"), makeInteger(rec.ts_ns));
    m.entries.emplace_back(makeBulk("value"), makeTypedValue(rec.type, rec.value));
    return m;
}

} // namespace

IrspValue Dispatcher::handle(Session &session, const IrspValue &request) {
    const auto *arr = std::get_if<IrspArray>(&request);
    if (arr == nullptr || arr->items.empty()) {
        return makeError("PROTOCOL_ERROR", "expected non-empty command array");
    }
    std::vector<std::string> parts;
    parts.reserve(arr->items.size());
    for (const auto &item : arr->items) {
        const auto *b = std::get_if<IrspBulk>(&item);
        if (b == nullptr) {
            return makeError("PROTOCOL_ERROR", "command elements must be bulk strings");
        }
        parts.push_back(b->data);
    }

    const std::string name = toUpper(parts[0]);
    const std::size_t argc = parts.size() - 1;
    const auto arg = [&](std::size_t i) -> const std::string & { return parts[1 + i]; };

    // HELLO 强制：握手前仅允许 HELLO。
    if (name != "HELLO" && !session.hello) {
        return makeError("NOT_READY", "HELLO required first");
    }

    if (name == "HELLO") {
        if (argc < 1) {
            return makeError("WRONG_ARITY", "HELLO requires <version>");
        }
        if (arg(0) != "1") {
            return makeError("UNSUPPORTED_VERSION", "only version 1 is supported");
        }
        // 可选编码协商：HELLO <version> ENCODING <irsp1|msgpack>。
        // 缺省沿用 session.encoding（由 server 按客户端成帧方式预置，默认 irsp1）。
        if (argc >= 3 && toUpper(arg(1)) == "ENCODING") {
            Encoding enc{};
            if (!parseEncoding(arg(2), enc)) {
                return makeError("UNSUPPORTED_ENCODING", "unknown encoding");
            }
            session.encoding = enc;
        }
        session.hello = true;
        IrspMap m;
        m.entries.emplace_back(makeBulk("server"), makeBulk("industrial-runtime/1.0.0"));
        m.entries.emplace_back(makeBulk("version"), makeBulk("1"));
        m.entries.emplace_back(makeBulk("encoding"),
                               makeBulk(std::string(encodingName(session.encoding))));
        m.entries.emplace_back(makeBulk("encodings"), makeBulk("irsp1,msgpack"));
        m.entries.emplace_back(makeBulk("transports"), makeBulk("websocket"));
        m.entries.emplace_back(makeBulk("capabilities"), makeBulk("tag,event"));
        return m;
    }

    if (name == "PING") {
        return argc >= 1 ? makeBulk(arg(0)) : makeSimple("PONG");
    }
    if (name == "BYE") {
        return makeSimple("OK");
    }
    if (name == "AUTH") {
        return makeError("NOT_IMPLEMENTED", "auth is reserved");
    }

    if (name == "GET") {
        if (argc != 1) {
            return makeError("WRONG_ARITY", "GET requires <topic>");
        }
        auto rec = tags_->read(arg(0));
        return rec ? tagToMap(*rec) : makeNull();
    }
    if (name == "MGET") {
        if (argc < 1) {
            return makeError("WRONG_ARITY", "MGET requires <topic>...");
        }
        IrspArray out;
        out.items.reserve(argc);
        for (std::size_t i = 0; i < argc; ++i) {
            auto rec = tags_->read(arg(i));
            out.items.push_back(rec ? tagToMap(*rec) : makeNull());
        }
        return out;
    }
    if (name == "EXISTS") {
        if (argc != 1) {
            return makeError("WRONG_ARITY", "EXISTS requires <topic>");
        }
        return makeInteger(tags_->exists(arg(0)) ? 1 : 0);
    }
    if (name == "SCAN") {
        // SCAN <cursor> <pattern> [COUNT <n>]
        if (argc != 2 && argc != 4) {
            return makeError("WRONG_ARITY", "SCAN <cursor> <pattern> [COUNT <n>]");
        }
        std::size_t count = 0;
        if (argc == 4) {
            if (toUpper(arg(2)) != "COUNT" || !parseCount(arg(3), count)) {
                return makeError("ERR", "expected COUNT <n>");
            }
        }
        if (!TopicMatcher::isValidPattern(arg(1))) {
            return makeError("ERR", "invalid scan pattern");
        }
        ScanResult res = tags_->scan(arg(0), arg(1), count);
        IrspArray names;
        names.items.reserve(res.names.size());
        for (auto &n : res.names) {
            names.items.push_back(makeBulk(n));
        }
        IrspArray out;
        out.items.push_back(makeBulk(res.nextCursor));
        out.items.push_back(std::move(names));
        return out;
    }

    if (name == "WATCH") {
        if (argc < 1) {
            return makeError("WRONG_ARITY", "WATCH requires <topic>...");
        }
        for (std::size_t i = 0; i < argc; ++i) {
            if (!TopicMatcher::isConcrete(arg(i))) {
                return makeError("ERR", "WATCH requires concrete topics");
            }
        }
        for (std::size_t i = 0; i < argc; ++i) {
            tagSubs_.subscribe(arg(i), session.id);
        }
        return makeInteger(static_cast<std::int64_t>(tagSubs_.countFor(session.id)));
    }
    if (name == "SUBSCRIBE") {
        if (argc < 1) {
            return makeError("WRONG_ARITY", "SUBSCRIBE requires <pattern>...");
        }
        for (std::size_t i = 0; i < argc; ++i) {
            if (!TopicMatcher::isValidPattern(arg(i))) {
                return makeError("ERR", "invalid subscribe pattern");
            }
        }
        for (std::size_t i = 0; i < argc; ++i) {
            tagSubs_.subscribe(arg(i), session.id);
        }
        return makeInteger(static_cast<std::int64_t>(tagSubs_.countFor(session.id)));
    }
    if (name == "UNWATCH" || name == "UNSUBSCRIBE") {
        if (argc == 0) {
            tagSubs_.unsubscribeAll(session.id);
        } else {
            for (std::size_t i = 0; i < argc; ++i) {
                tagSubs_.unsubscribe(arg(i), session.id);
            }
        }
        return makeInteger(static_cast<std::int64_t>(tagSubs_.countFor(session.id)));
    }

    if (name == "SUBEVENT") {
        EventFilter filter;
        if (argc >= 1) {
            const int rank = severityRank(arg(0));
            if (rank < 0) {
                return makeError("ERR", "unknown severity");
            }
            filter.minSeverityRank = rank;
        }
        if (argc >= 2) {
            filter.category = arg(1);
        }
        eventSubs_[session.id] = filter;
        return makeInteger(1);
    }
    if (name == "UNSUBEVENT") {
        eventSubs_.erase(session.id);
        return makeInteger(0);
    }

    if (name == "SET") {
        // SET <topic> <type> <value-bulk>：写回设备（同步"已受理"语义）。
        if (argc != 3) {
            return makeError("WRONG_ARITY", "SET <topic> <type> <value>");
        }
        if (writer_ == nullptr) {
            return makeError("NOT_IMPLEMENTED", "write-back not available");
        }
        switch (writer_->write(arg(0), arg(1), arg(2))) {
        case WriteResult::Accepted:
            return makeSimple("OK");
        case WriteResult::NotHandled:
            return makeError("NOT_FOUND", "no writer for topic");
        case WriteResult::Error:
        default:
            return makeError("ERR", "write failed");
        }
    }

    if (name == "SUBSTREAM" || name == "UNSUBSTREAM") {
        return makeError("NOT_IMPLEMENTED", "stream subscription is reserved for V2");
    }

    return makeError("UNKNOWN_COMMAND", parts[0]);
}

void Dispatcher::onSessionClosed(std::uint64_t id) {
    tagSubs_.unsubscribeAll(id);
    eventSubs_.erase(id);
}

std::vector<std::uint64_t> Dispatcher::eventSubscribers(int severityRank,
                                                        const std::string &category) const {
    std::vector<std::uint64_t> out;
    for (const auto &[id, filter] : eventSubs_) {
        if (severityRank < filter.minSeverityRank) {
            continue;
        }
        if (!filter.category.empty() && filter.category != category) {
            continue;
        }
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace irsp
