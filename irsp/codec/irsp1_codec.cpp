#include "codec/resp1_codec.hpp"

#include <charconv>
#include <variant>

namespace irp {

namespace {

using Status = Resp1Codec::Status;

struct ParseState {
    Status status{Status::Error};
    RespValue value{RespNull{}};
    std::size_t pos{0};
};

/// 从 pos 起查找 CRLF，返回 '\r' 的下标；未找到返回 npos。
std::size_t findCrlf(std::string_view b, std::size_t pos) {
    for (std::size_t i = pos; i + 1 < b.size(); ++i) {
        if (b[i] == '\r' && b[i + 1] == '\n') {
            return i;
        }
    }
    return std::string_view::npos;
}

bool parseInt(std::string_view s, std::int64_t &out) {
    if (s.empty()) {
        return false;
    }
    const char *begin = s.data();
    const char *end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc() && ptr == end;
}

ParseState parseValue(std::string_view b, std::size_t pos) {
    if (pos >= b.size()) {
        return {Status::Incomplete, RespNull{}, pos};
    }
    const char type = b[pos];
    const std::size_t lineEnd = findCrlf(b, pos + 1);
    if (lineEnd == std::string_view::npos) {
        return {Status::Incomplete, RespNull{}, pos};
    }
    const std::string_view line = b.substr(pos + 1, lineEnd - (pos + 1));
    const std::size_t afterLine = lineEnd + 2;

    switch (type) {
    case '+':
        return {Status::Ok, RespSimple{std::string(line)}, afterLine};

    case '-': {
        const std::size_t sp = line.find(' ');
        std::string code =
            sp == std::string_view::npos ? std::string(line) : std::string(line.substr(0, sp));
        std::string msg =
            sp == std::string_view::npos ? std::string{} : std::string(line.substr(sp + 1));
        return {Status::Ok, RespError{std::move(code), std::move(msg)}, afterLine};
    }

    case ':': {
        std::int64_t v = 0;
        if (!parseInt(line, v)) {
            return {Status::Error, RespNull{}, pos};
        }
        return {Status::Ok, RespInteger{v}, afterLine};
    }

    case '$': {
        std::int64_t len = 0;
        if (!parseInt(line, len)) {
            return {Status::Error, RespNull{}, pos};
        }
        if (len < 0) {
            return {Status::Ok, RespNull{}, afterLine};
        }
        const std::size_t ulen = static_cast<std::size_t>(len);
        const std::size_t need = afterLine + ulen + 2;
        if (b.size() < need) {
            return {Status::Incomplete, RespNull{}, pos};
        }
        if (b[afterLine + ulen] != '\r' || b[afterLine + ulen + 1] != '\n') {
            return {Status::Error, RespNull{}, pos};
        }
        return {Status::Ok, RespBulk{std::string(b.substr(afterLine, ulen))}, need};
    }

    case '*': {
        std::int64_t count = 0;
        if (!parseInt(line, count)) {
            return {Status::Error, RespNull{}, pos};
        }
        if (count < 0) {
            return {Status::Ok, RespNull{}, afterLine}; // null array
        }
        RespArray arr;
        arr.items.reserve(static_cast<std::size_t>(count));
        std::size_t cur = afterLine;
        for (std::int64_t i = 0; i < count; ++i) {
            ParseState st = parseValue(b, cur);
            if (st.status != Status::Ok) {
                return {st.status, RespNull{}, pos};
            }
            arr.items.push_back(std::move(st.value));
            cur = st.pos;
        }
        return {Status::Ok, std::move(arr), cur};
    }

    case '%': {
        std::int64_t pairs = 0;
        if (!parseInt(line, pairs) || pairs < 0) {
            return {Status::Error, RespNull{}, pos};
        }
        RespMap m;
        m.entries.reserve(static_cast<std::size_t>(pairs));
        std::size_t cur = afterLine;
        for (std::int64_t i = 0; i < pairs; ++i) {
            ParseState k = parseValue(b, cur);
            if (k.status != Status::Ok) {
                return {k.status, RespNull{}, pos};
            }
            cur = k.pos;
            ParseState v = parseValue(b, cur);
            if (v.status != Status::Ok) {
                return {v.status, RespNull{}, pos};
            }
            cur = v.pos;
            m.entries.emplace_back(std::move(k.value), std::move(v.value));
        }
        return {Status::Ok, std::move(m), cur};
    }

    default:
        return {Status::Error, RespNull{}, pos};
    }
}

} // namespace

void Resp1Codec::encode(const RespValue &value, std::string &out) {
    std::visit(
        [&out](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, RespNull>) {
                out += "$-1\r\n";
            } else if constexpr (std::is_same_v<T, RespSimple>) {
                out += '+';
                out += v.text;
                out += "\r\n";
            } else if constexpr (std::is_same_v<T, RespError>) {
                out += '-';
                out += v.code;
                if (!v.message.empty()) {
                    out += ' ';
                    out += v.message;
                }
                out += "\r\n";
            } else if constexpr (std::is_same_v<T, RespInteger>) {
                out += ':';
                out += std::to_string(v.value);
                out += "\r\n";
            } else if constexpr (std::is_same_v<T, RespBulk>) {
                out += '$';
                out += std::to_string(v.data.size());
                out += "\r\n";
                out += v.data;
                out += "\r\n";
            } else if constexpr (std::is_same_v<T, RespArray>) {
                out += '*';
                out += std::to_string(v.items.size());
                out += "\r\n";
                for (const auto &item : v.items) {
                    encode(item, out);
                }
            } else if constexpr (std::is_same_v<T, RespMap>) {
                out += '%';
                out += std::to_string(v.entries.size());
                out += "\r\n";
                for (const auto &[key, val] : v.entries) {
                    encode(key, out);
                    encode(val, out);
                }
            }
        },
        value);
}

std::string Resp1Codec::encode(const RespValue &value) {
    std::string out;
    encode(value, out);
    return out;
}

Resp1Codec::DecodeResult Resp1Codec::decode(std::string_view buffer) {
    ParseState st = parseValue(buffer, 0);
    return DecodeResult{st.status, std::move(st.value), st.pos};
}

RespValue Resp1Codec::decodeInline(std::string_view line) {
    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    RespArray arr;
    std::size_t i = 0;
    const std::size_t n = line.size();
    while (i < n) {
        while (i < n && isSpace(line[i])) ++i;
        if (i >= n) break;
        const std::size_t start = i;
        while (i < n && !isSpace(line[i])) ++i;
        arr.items.push_back(makeBulk(std::string(line.substr(start, i - start))));
    }
    return arr;
}

} // namespace irp
