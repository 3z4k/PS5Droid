// PS5Droid - IPC primitives shared by the compat layer and the (future)
// Binder experiment.
//
// These are REAL, tested in-process mechanisms: a lock-free single-producer/
// single-consumer ring for cross-thread message passing, and a fixed-size
// block allocator for Binder-style transaction buffers. They deliberately
// do NOT claim to be Binder, ashmem or binderfs - see
// src/android/binder/binder_protocol.h and docs/architecture.md.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ps5droid {
namespace compat {

// Single-producer/single-consumer ring of 64-bit messages over host memory.
// Wait-free push/pop; safe when exactly one thread pushes and one pops.
class SpscRing {
  public:
    // capacity is rounded up to the next power of two; 2..(1<<24).
    explicit SpscRing(size_t capacity);
    ~SpscRing() = default;
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    bool push(uint64_t value);
    bool pop(uint64_t* out);
    size_t capacity() const { return capacity_; }
    size_t size_approx() const;

  private:
    size_t mask(size_t v) const { return v & (capacity_ - 1); }
    std::vector<uint64_t> slots_;
    size_t capacity_;
    std::atomic<size_t> head_{0}; // consumer reads
    std::atomic<size_t> tail_{0}; // producer writes
};

// Fixed-size block allocator over a pre-mapped region, suitable for
// Binder-style transaction buffers: fixed slots, O(1) alloc/free, no
// compaction. Not thread-safe by itself; callers synchronize.
class BlockAllocator {
  public:
    BlockAllocator(void* base, size_t region_size, size_t block_size);
    ~BlockAllocator() = default;
    BlockAllocator(const BlockAllocator&) = delete;
    BlockAllocator& operator=(const BlockAllocator&) = delete;

    // Returns a pointer to a zero-initialized block, or nullptr when full.
    void* alloc();
    // Frees a block previously returned by alloc(). Invalid pointers are
    // reported (returns false) rather than silently accepted.
    bool free(void* block);

    size_t block_size() const { return block_size_; }
    size_t block_count() const { return block_count_; }
    size_t blocks_in_use() const { return blocks_in_use_; }

  private:
    uint8_t* base_;
    size_t region_size_;
    size_t block_size_;
    size_t block_count_;
    size_t blocks_in_use_ = 0;
    std::vector<bool> used_;
};

} // namespace compat
} // namespace ps5droid
