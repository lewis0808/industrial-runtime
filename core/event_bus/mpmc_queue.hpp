#pragma once

#include <atomic>
#include <cstddef>
#include <new>
#include <vector>

namespace core {

/// 有界无锁 MPMC 队列（Dmitry Vyukov 算法）。
///
/// 多生产者/多消费者均无锁。容量在构造时向上取整为 2 的幂。
/// 入队/出队为 wait-free 的单次尝试（失败即返回，不自旋阻塞）。
template <typename T>
class MpmcQueue {
public:
    explicit MpmcQueue(std::size_t capacity)
        : buffer_(roundUpPow2(capacity)),
          mask_(buffer_.size() - 1) {
        for (std::size_t i = 0; i < buffer_.size(); ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueuePos_.store(0, std::memory_order_relaxed);
        dequeuePos_.store(0, std::memory_order_relaxed);
    }

    MpmcQueue(const MpmcQueue&) = delete;
    MpmcQueue& operator=(const MpmcQueue&) = delete;

    /// 入队。队列满返回 false。
    template <typename U>
    bool tryEnqueue(U&& item) {
        Cell* cell;
        std::size_t pos = enqueuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & mask_];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) -
                              static_cast<std::intptr_t>(pos);
            if (diff == 0) {
                if (enqueuePos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // 队列满
            } else {
                pos = enqueuePos_.load(std::memory_order_relaxed);
            }
        }
        cell->data = std::forward<U>(item);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    /// 出队。队列空返回 false。
    bool tryDequeue(T& out) {
        Cell* cell;
        std::size_t pos = dequeuePos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & mask_];
            const std::size_t seq = cell->sequence.load(std::memory_order_acquire);
            const auto diff = static_cast<std::intptr_t>(seq) -
                              static_cast<std::intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeuePos_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // 队列空
            } else {
                pos = dequeuePos_.load(std::memory_order_relaxed);
            }
        }
        out = std::move(cell->data);
        cell->sequence.store(pos + mask_ + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return buffer_.size(); }

private:
    struct Cell {
        std::atomic<std::size_t> sequence;
        T data;
    };

    static std::size_t roundUpPow2(std::size_t n) noexcept {
        if (n < 2) {
            return 2;
        }
        std::size_t p = 1;
        while (p < n) {
            p <<= 1;
        }
        return p;
    }

#ifdef __cpp_lib_hardware_interference_size
    static constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
    static constexpr std::size_t kCacheLine = 64;
#endif

    std::vector<Cell> buffer_;
    const std::size_t mask_;
    alignas(kCacheLine) std::atomic<std::size_t> enqueuePos_;
    alignas(kCacheLine) std::atomic<std::size_t> dequeuePos_;
};

}  // namespace core