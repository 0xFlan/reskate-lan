#pragma once
#include <memory>
#include <utility>

namespace dingosdk {
// std::atomic<std::shared_ptr<T>> is an MSVC extension rather than standard C++: libc++ (clang on
// Linux and macOS) has no such specialization and rejects it outright. This wraps the standard
// free-function atomics on a shared_ptr, so call sites keep their load()/store() spelling and
// still get the reference-counted guarantee that a reader never sees a half-swapped value.
template <class T>
class atomic_shared_ptr {
  public:
    atomic_shared_ptr() = default;
    explicit atomic_shared_ptr(std::shared_ptr<T> value) : value_(std::move(value)) {}
    atomic_shared_ptr(const atomic_shared_ptr &) = delete;
    atomic_shared_ptr &operator=(const atomic_shared_ptr &) = delete;

    std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const {
        return std::atomic_load_explicit(&value_, order);
    }
    void store(std::shared_ptr<T> value, std::memory_order order = std::memory_order_seq_cst) {
        std::atomic_store_explicit(&value_, std::move(value), order);
    }
    std::shared_ptr<T> exchange(std::shared_ptr<T> value, std::memory_order order = std::memory_order_seq_cst) {
        return std::atomic_exchange_explicit(&value_, std::move(value), order);
    }
    // Swaps in a new value only where the current one is still `expected`; on success `expected`
    // receives the old value, as the standard's compare_exchange does.
    bool compare_exchange(std::shared_ptr<T> &expected, std::shared_ptr<T> desired,
                          std::memory_order success = std::memory_order_seq_cst,
                          std::memory_order failure = std::memory_order_seq_cst) {
        return std::atomic_compare_exchange_strong_explicit(&value_, &expected, std::move(desired), success, failure);
    }

  private:
    mutable std::shared_ptr<T> value_;
};
} // namespace dingosdk
