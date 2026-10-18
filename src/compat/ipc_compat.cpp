// PS5Droid - IPC primitives (implementation).
#include "compat/ipc_compat.h"

#include <cstring>

namespace ps5droid {
namespace compat {

// ---------------------------------------------------------------------------
// SpscRing
// ---------------------------------------------------------------------------

SpscRing::SpscRing(size_t capacity) {
    size_t cap = 2;
    while (cap < capacity && cap < (size_t{1} << 24))
        cap <<= 1;
    capacity_ = cap;
    slots_.assign(capacity_, 0);
}

bool SpscRing::push(uint64_t value) {
    size_t t = tail_.load(std::memory_order_relaxed);
    size_t h = head_.load(std::memory_order_acquire);
    if (t - h == capacity_)
        return false; // full
    slots_[mask(t)] = value;
    tail_.store(t + 1, std::memory_order_release);
    return true;
}

bool SpscRing::pop(uint64_t* out) {
    size_t h = head_.load(std::memory_order_relaxed);
    size_t t = tail_.load(std::memory_order_acquire);
    if (h == t)
        return false; // empty
    *out = slots_[mask(h)];
    head_.store(h + 1, std::memory_order_release);
    return true;
}

size_t SpscRing::size_approx() const {
    return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
}

// ---------------------------------------------------------------------------
// BlockAllocator
// ---------------------------------------------------------------------------

BlockAllocator::BlockAllocator(void* base, size_t region_size, size_t block_size)
    : base_(static_cast<uint8_t*>(base)), region_size_(region_size), block_size_(block_size) {
    block_count_ = (block_size_ > 0) ? region_size_ / block_size_ : 0;
    used_.assign(block_count_, false);
}

void* BlockAllocator::alloc() {
    for (size_t i = 0; i < block_count_; ++i) {
        if (!used_[i]) {
            used_[i] = true;
            ++blocks_in_use_;
            void* p = base_ + i * block_size_;
            memset(p, 0, block_size_);
            return p;
        }
    }
    return nullptr;
}

bool BlockAllocator::free(void* block) {
    if (block == nullptr)
        return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(block);
    uintptr_t b = reinterpret_cast<uintptr_t>(base_);
    if (addr < b || addr >= b + block_count_ * block_size_)
        return false;
    if ((addr - b) % block_size_ != 0)
        return false;
    size_t idx = (addr - b) / block_size_;
    if (!used_[idx])
        return false; // double free
    used_[idx] = false;
    --blocks_in_use_;
    return true;
}

} // namespace compat
} // namespace ps5droid
