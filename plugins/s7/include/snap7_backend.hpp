#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <snap7/snap7_libmain.h>

#include "s7_backend.hpp"

namespace s7 {

/// 真实 S7 后端：用 snap7 C 客户端（Cli_*）经 ISO-on-TCP 连 PLC，DBRead/DBWrite。
/// 懒连接：首次读写时连接；失败则下次重连。线程安全（单连接串行化）。
class Snap7Backend final : public S7Backend {
  public:
    Snap7Backend(std::string ip, int rack, int slot)
        : ip_(std::move(ip)), rack_(rack), slot_(slot), client_(Cli_Create()) {}

    ~Snap7Backend() override {
        if (client_ != 0) {
            Cli_Disconnect(client_);
            Cli_Destroy(client_); // 形参为 S7Object&
        }
    }

    Snap7Backend(const Snap7Backend &) = delete;
    Snap7Backend &operator=(const Snap7Backend &) = delete;

    std::vector<std::uint8_t> readDb(int db, int start, int size) override {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnected()) {
            return {};
        }
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(size), 0);
        if (Cli_DBRead(client_, db, start, size, buf.data()) != 0) {
            connected_ = false; // 触发下次重连
            return {};
        }
        return buf;
    }

    bool writeDb(int db, int start, const std::vector<std::uint8_t> &data) override {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!ensureConnected()) {
            return false;
        }
        if (Cli_DBWrite(client_, db, start, static_cast<int>(data.size()),
                        const_cast<std::uint8_t *>(data.data())) != 0) {
            connected_ = false;
            return false;
        }
        return true;
    }

  private:
    bool ensureConnected() {
        if (connected_) {
            return true;
        }
        if (client_ == 0) {
            return false;
        }
        if (Cli_ConnectTo(client_, ip_.c_str(), rack_, slot_) == 0) {
            connected_ = true;
        }
        return connected_;
    }

    std::string ip_;
    int rack_;
    int slot_;
    S7Object client_{0};
    bool connected_{false};
    std::mutex mutex_;
};

} // namespace s7
