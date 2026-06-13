#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace s7 {

/// S7 设备后端抽象：以 DB 区字节读写为单位（对齐 snap7 的 DBRead/DBWrite）。
/// 真实实现（Snap7Backend）用 snap7 TS7Client 连接 PLC；此处提供内存模拟实现。
class S7Backend {
  public:
    virtual ~S7Backend() = default;

    /// 读 DBn 的 [start, start+size) 字节（S7 为大端存储）。返回 size 字节。
    [[nodiscard]] virtual std::vector<std::uint8_t> readDb(int db, int start, int size) = 0;

    /// 写 DBn 从 start 起的字节。成功返回 true。
    virtual bool writeDb(int db, int start, const std::vector<std::uint8_t> &data) = 0;

    /// 推进模拟（真实后端为空实现）。
    virtual void tick() {}
};

/// 内存模拟 S7 后端：仅模拟 DB1。tick() 推进温度/计数器；写回持久化到 DB 字节。
class SimulatedS7Backend final : public S7Backend {
  public:
    SimulatedS7Backend() : db1_(64, 0) {}

    std::vector<std::uint8_t> readDb(int db, int start, int size) override {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<std::uint8_t> out(static_cast<std::size_t>(size), 0);
        if (db == 1 && start >= 0 && start + size <= static_cast<int>(db1_.size())) {
            std::copy(db1_.begin() + start, db1_.begin() + start + size, out.begin());
        }
        return out;
    }

    bool writeDb(int db, int start, const std::vector<std::uint8_t> &data) override {
        std::lock_guard<std::mutex> lk(mutex_);
        if (db != 1 || start < 0 ||
            start + static_cast<int>(data.size()) > static_cast<int>(db1_.size())) {
            return false;
        }
        std::copy(data.begin(), data.end(), db1_.begin() + start);
        return true;
    }

    void tick() override {
        std::lock_guard<std::mutex> lk(mutex_);
        ++counter_;
        // DB1.DBD0 = REAL 温度（20 + counter%10）；DB1.DBD4 = DINT 计数器。
        // DB1.DBD8 = REAL 设定点：tick 不改，由写回设置后回读。
        writeRealBE(0, 20.0F + static_cast<float>(counter_ % 10));
        writeI32BE(4, counter_);
    }

  private:
    void writeU32BE(int off, std::uint32_t u) {
        db1_[off] = static_cast<std::uint8_t>(u >> 24);
        db1_[off + 1] = static_cast<std::uint8_t>(u >> 16);
        db1_[off + 2] = static_cast<std::uint8_t>(u >> 8);
        db1_[off + 3] = static_cast<std::uint8_t>(u);
    }
    void writeI32BE(int off, std::int32_t v) { writeU32BE(off, static_cast<std::uint32_t>(v)); }
    void writeRealBE(int off, float f) {
        std::uint32_t u = 0;
        std::memcpy(&u, &f, 4);
        writeU32BE(off, u);
    }

    std::mutex mutex_;
    std::vector<std::uint8_t> db1_;
    std::int32_t counter_{0};
};

} // namespace s7
