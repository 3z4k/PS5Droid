// PS5Droid - IPC primitive tests (ring + block allocator).
#include "test_framework.h"

#include <atomic>
#include <cstring>
#include <set>
#include <thread>

#include "compat/ipc_compat.h"
#include "platform/platform.h"

using namespace ps5droid::compat;
using ps5droid::platform::map_anonymous;
using ps5droid::platform::page_size;
using ps5droid::platform::Thread;
using ps5droid::platform::unmap;

PS5DROID_TEST(ring_basic_fifo) {
    SpscRing ring(8);
    CHECK_MSG(ring.capacity() == 8, "capacity must be honored");
    for (uint64_t i = 0; i < 8; ++i) {
        CHECK_MSG(ring.push(i), "push into empty ring must succeed");
    }
    CHECK_MSG(!ring.push(99), "push into full ring must fail");
    for (uint64_t i = 0; i < 8; ++i) {
        uint64_t v = 0xFFFF;
        CHECK_MSG(ring.pop(&v), "pop from full ring must succeed");
        CHECK_MSG(v == i, "fifo order broken");
    }
    uint64_t v;
    CHECK_MSG(!ring.pop(&v), "pop from empty ring must fail");
}

PS5DROID_TEST(ring_cross_thread_100k) {
    SpscRing ring(1024);
    static constexpr int kCount = 100000;
    std::atomic<int> received{0};
    std::atomic<bool> done{false};

    struct Ctx {
        SpscRing* ring;
        std::atomic<int>* received;
        std::atomic<bool>* done;
    } ctx{&ring, &received, &done};

    Thread consumer;
    auto consumer_fn = [](void* raw) {
        Ctx* c = static_cast<Ctx*>(raw);
        int expect = 0;
        uint64_t v;
        while (true) {
            if (c->ring->pop(&v)) {
                if (v == static_cast<uint64_t>(expect)) {
                    c->received->fetch_add(1);
                    ++expect;
                }
            } else if (c->done->load()) {
                // drain remaining
                while (c->ring->pop(&v)) {
                    if (v == static_cast<uint64_t>(expect)) {
                        c->received->fetch_add(1);
                        ++expect;
                    }
                }
                break;
            } else {
                ps5droid::platform::yield_thread();
            }
        }
    };
    CHECK_MSG(consumer.start(consumer_fn, &ctx), "consumer start failed");
    for (int i = 0; i < kCount; ++i) {
        while (!ring.push(static_cast<uint64_t>(i))) {
            ps5droid::platform::yield_thread();
        }
    }
    done.store(true);
    CHECK_MSG(consumer.join(), "consumer join failed");
    CHECK_MSG(received.load() == kCount, "messages lost");
}

PS5DROID_TEST(block_allocator_roundtrip) {
    size_t ps = page_size();
    void* region = map_anonymous(ps * 4);
    CHECK_MSG(region != nullptr, "region map failed");

    BlockAllocator alloc(region, ps * 4, 256);
    CHECK_MSG(alloc.block_count() == (ps * 4) / 256, "block count wrong");

    std::vector<void*> blocks;
    while (true) {
        void* b = alloc.alloc();
        if (b == nullptr)
            break;
        memset(b, 0x11, 256);
        blocks.push_back(b);
    }
    CHECK_MSG(blocks.size() == alloc.block_count(), "must fill exactly");
    CHECK_MSG(alloc.blocks_in_use() == alloc.block_count(), "in_use wrong");

    // Free half, realloc must reuse.
    for (size_t i = 0; i < blocks.size(); i += 2) {
        CHECK_MSG(alloc.free(blocks[i]), "free failed");
    }
    CHECK_MSG(alloc.blocks_in_use() == blocks.size() / 2, "in_use wrong");
    int reused = 0;
    while (alloc.alloc() != nullptr)
        ++reused;
    CHECK_MSG(reused == static_cast<int>(blocks.size() / 2), "reuse count wrong");

    unmap(region, ps * 4);
}

PS5DROID_TEST(block_allocator_rejects_bad_free) {
    size_t ps = page_size();
    void* region = map_anonymous(ps);
    CHECK_MSG(region != nullptr, "map failed");
    BlockAllocator alloc(region, ps, 128);
    void* b = alloc.alloc();
    CHECK_MSG(b != nullptr, "alloc failed");
    CHECK_MSG(!alloc.free(static_cast<char*>(b) + 1), "misaligned free must fail");
    CHECK_MSG(!alloc.free(static_cast<char*>(region) + (ps - 64)), "out-of-range free must fail");
    CHECK_MSG(alloc.free(b), "valid free failed");
    CHECK_MSG(!alloc.free(b), "double free must fail");
    CHECK_MSG(!alloc.free(nullptr), "null free must fail");
    unmap(region, ps);
}
