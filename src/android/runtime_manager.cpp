// PS5Droid - runtime manager (implementation).
#include "android/runtime_manager.h"

#include <atomic>
#include <cstring>

#include "compat/ipc_compat.h"
#include "compat/memory_compat.h"
#include "compat/syscall_dispatch.h"
#include "platform/platform.h"

namespace ps5droid {
namespace android {

using platform::log;

namespace {

struct ProbeCtx {
    std::atomic<int>* counter;
};

void probe_thread_fn(void* raw) {
    ProbeCtx* ctx = static_cast<ProbeCtx*>(raw);
    ctx->counter->fetch_add(1, std::memory_order_relaxed);
}

} // namespace

CapabilityReport RuntimeManager::probe() {
    phase_ = RuntimePhase::kProbing;
    CapabilityReport report;
    report.backend = platform::backend_name();

    auto add = [&](const char* name, bool passed, const std::string& detail) {
        report.results.push_back(CapabilityResult{name, passed, detail});
    };

    // 1. page size
    {
        size_t ps = platform::page_size();
        add("page_size", ps >= 512 && ps <= 1024 * 1024, std::to_string(ps) + " bytes");
    }

    // 2. anonymous mapping + unmap
    {
        size_t ps = platform::page_size();
        void* p = platform::map_anonymous(ps * 2);
        bool passed = p != nullptr;
        if (passed) {
            memset(p, 0xAB, ps * 2);
            auto* bytes = static_cast<uint8_t*>(p);
            passed = bytes[0] == 0xAB && bytes[ps * 2 - 1] == 0xAB;
            platform::unmap(p, ps * 2);
        }
        add("map_anonymous", passed, passed ? "2 pages mapped/verified" : "map failed");
    }

    // 3. mprotect
    {
        size_t ps = platform::page_size();
        void* p = platform::map_anonymous(ps);
        bool passed = p != nullptr;
        if (passed) {
            passed = platform::protect_memory(p, ps, true, false) &&
                     platform::protect_memory(p, ps, true, true);
            platform::unmap(p, ps);
        }
        add("protect_memory", passed, passed ? "RW -> R -> RW transitions applied" : "failed");
    }

    // 4. threads
    {
        std::atomic<int> counter{0};
        ProbeCtx ctx{&counter};
        platform::Thread threads[4];
        bool ok = true;
        for (auto& t : threads)
            ok = ok && t.start(probe_thread_fn, &ctx);
        for (auto& t : threads)
            ok = ok && t.join();
        ok = ok && counter.load() == 4;
        add("threads", ok, ok ? "4 threads started and joined" : "thread create/join failed");
    }

    // 5. futex
    {
        platform::Futex futex;
        uint32_t word = 42;
        int wait_r = futex.wait(&word, 100); // expect immediate EWOULDBLOCK
        word = 42;
        int wake_r = futex.wake(&word, 1); // no waiters -> 0
        add("futex", wait_r == 1 && wake_r == 0, "wait/wake semantics verified (no waiter)");
    }

    // 6. filesystem write/read
    {
        std::string tmp = "ps5droid_probe.tmp";
        const char* payload = "ps5droid fs probe";
        bool passed = platform::write_file(tmp, payload, strlen(payload));
        if (passed) {
            std::string readback;
            passed = platform::read_file(tmp, &readback) && readback == payload;
            platform::remove_file(tmp);
        }
        add("filesystem", passed, passed ? "write/read/remove ok" : "failed");
    }

    // 7. cross-thread IPC ring
    {
        compat::SpscRing ring(1024);
        std::atomic<bool> producer_done{false};
        std::atomic<int> received{0};

        struct Ctx {
            compat::SpscRing* ring;
            std::atomic<bool>* done;
            std::atomic<int>* received;
        };
        Ctx ctx{&ring, &producer_done, &received};

        platform::Thread consumer;
        auto consumer_fn = [](void* raw) {
            Ctx* c = static_cast<Ctx*>(raw);
            uint64_t v;
            int expect = 0;
            while (true) {
                if (c->ring->pop(&v)) {
                    if (v == static_cast<uint64_t>(expect)) {
                        c->received->fetch_add(1);
                        ++expect;
                    }
                } else if (c->done->load()) {
                    // Producer finished; drain anything still queued.
                    while (c->ring->pop(&v)) {
                        if (v == static_cast<uint64_t>(expect)) {
                            c->received->fetch_add(1);
                            ++expect;
                        }
                    }
                    break;
                } else {
                    platform::yield_thread();
                }
            }
        };

        // The consumer thread MUST be joined before ctx goes out of scope,
        // regardless of push failures; never short-circuit the join.
        bool ok = consumer.start(consumer_fn, &ctx);
        if (ok) {
            for (int i = 0; i < 10000; ++i) {
                while (!ring.push(static_cast<uint64_t>(i))) {
                    platform::yield_thread();
                }
            }
            producer_done.store(true);
            ok = consumer.join();
        }
        ok = ok && received.load() == 10000;
        add("ipc_ring", ok, ok ? "10k messages across threads, in order" : "ring handoff failed");
    }

    // 8. Linux compat dispatch self-check (mmap via Linux ABI)
    {
        compat::CompatMemory memory;
        compat::SyscallDispatch dispatch(&memory);
        compat::SyscallArgs args{};
        args.a[1] = 8192;                      // len
        args.a[2] = 0x3;                       // PROT_READ|WRITE
        args.a[3] = 0x22;                      // MAP_PRIVATE|ANON
        args.a[4] = static_cast<uint64_t>(-1); // fd
        auto r = dispatch.invoke(9 /* mmap */, args);
        bool passed = r.ret > 0;
        if (passed) {
            memset(reinterpret_cast<void*>(r.ret), 0x5A, 8192);
            compat::SyscallArgs un{};
            un.a[0] = static_cast<uint64_t>(r.ret);
            un.a[1] = 8192;
            passed = dispatch.invoke(11 /* munmap */, un).ret == 0;
        }
        add("linux_abi_mmap", passed,
            passed ? "Linux mmap(9)/munmap(11) end-to-end" : "dispatch failed");
    }

    report.all_passed = true;
    for (const auto& r : report.results) {
        if (!r.passed)
            report.all_passed = false;
    }

    phase_ = report.all_passed ? RuntimePhase::kPlatformVerified : RuntimePhase::kNotInitialized;
    return report;
}

StartAttempt RuntimeManager::attempt_start() {
    StartAttempt attempt;
    attempt.phase_after = RuntimePhase::kStarting;

    if (phase_ != RuntimePhase::kPlatformVerified) {
        probe();
    }
    if (phase_ == RuntimePhase::kPlatformVerified) {
        phase_ = RuntimePhase::kCompatReady;
    }

    // Structured, honest blockers. Each entry is a researched platform
    // fact; see docs/platform-constraints.md for the evidence trail.
    attempt.blockers.push_back(Blocker{
        "process_model", "Android requires fork()/clone() and a zygote. Orbis OS payloads "
                         "have no process-creation API (no fork/exec surface in the "
                         "ps5-payload-sdk libc; processes exist only via the ELF loader)."});
    attempt.blockers.push_back(
        Blocker{"binder", "The Android service world requires the kernel binder driver "
                          "(/dev/binder, binderfs). Orbis is a FreeBSD descendant with no "
                          "binder device; see src/android/binder/binder_protocol.h."});
    attempt.blockers.push_back(Blocker{
        "filesystem_layout", "Android images are ext4 (system.img/vendor.img). Orbis exposes "
                             "no loop devices, no FUSE and no ext4 driver; images can be "
                             "inspected read-only (image_manager) but not mounted."});
    attempt.blockers.push_back(
        Blocker{"graphics_services", "SurfaceFlinger and the Waydroid hwcomposer HAL assume a "
                                     "compositor/wayland display server. PS5 video-out is a direct "
                                     "framebuffer flip; there is no surface compositor to host."});
    attempt.blockers.push_back(
        Blocker{"bionic_runtime", "bionic's dynamic linker and linker namespaces have not been "
                                  "re-hosted; Milestone 4 (userspace feasibility) not started."});

    attempt.phase_after = RuntimePhase::kBlocked;
    phase_ = RuntimePhase::kBlocked;
    last_blockers_ = attempt.blockers;
    return attempt;
}

} // namespace android
} // namespace ps5droid
