// PS5Droid - threads, synchronization and time platform implementation.
//
// Portable core built on C11 threads (threads.h). This was chosen because
// both backends provide it:
//   * glibc since 2.28 on Linux hosts.
//   * ps5-payload-sdk libc ships thrd.c / mtx.c / cnd.c (verified in the
//     SDK source tree).
//
// platform::Futex is implemented on top of a waiter registry (mutex + condvar
// per address) on both backends. This is fully functional for the in-process
// Linux ABI experiment; a kernel-native path (Linux futex syscall host-side,
// _umtx_op on Orbis) is a later, separately probed optimization.
#include "platform/platform.h"

#include <condition_variable>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

extern "C" {
#include <threads.h>
#include <time.h>
}

namespace ps5droid {
namespace platform {

uint64_t monotonic_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t wallclock_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

void sleep_ns(uint64_t ns) {
    if (ns == 0)
        return;
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(ns / 1000000000ull);
    ts.tv_nsec = static_cast<long>(ns % 1000000000ull);
    thrd_sleep(&ts, nullptr);
}

void yield_thread() {
    thrd_yield();
}

uint32_t current_thread_id() {
    // thrd_t is an opaque handle on both backends; we only need a value that
    // is distinct per live thread. Fold the pointer bits into 32 bits.
    thrd_t self = thrd_current();
    uintptr_t raw = 0;
    memcpy(&raw, &self, sizeof(raw) < sizeof(self) ? sizeof(raw) : sizeof(self));
    raw ^= raw >> 33;
    raw *= 0xff51afd7ed558ccdull;
    raw ^= raw >> 33;
    return static_cast<uint32_t>(raw);
}

// ---------------------------------------------------------------------------
// Thread
// ---------------------------------------------------------------------------

namespace {

struct ThreadCtx {
    Thread::Fn fn;
    void* ctx;
};

int thread_trampoline(void* raw) {
    ThreadCtx* tc = static_cast<ThreadCtx*>(raw);
    Thread::Fn fn = tc->fn;
    void* ctx = tc->ctx;
    delete tc;
    fn(ctx);
    return 0;
}

} // namespace

Thread::~Thread() {
    // Intentionally do not join in the destructor: callers must join or leak.
    (void)this;
}

bool Thread::start(Fn fn, void* ctx) {
    if (started_ || fn == nullptr)
        return false;
    fn_ = fn;
    ctx_ = ctx;
    ThreadCtx* tc = new ThreadCtx{fn, ctx};
    thrd_t* t = new thrd_t;
    if (thrd_create(t, thread_trampoline, tc) != thrd_success) {
        delete t;
        delete tc;
        return false;
    }
    handle_ = t;
    started_ = true;
    return true;
}

bool Thread::join() {
    if (!started_ || joined_)
        return false;
    thrd_t* t = static_cast<thrd_t*>(handle_);
    if (thrd_join(*t, nullptr) != thrd_success) {
        return false;
    }
    delete t;
    handle_ = nullptr;
    joined_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// Mutex / CondVar
// ---------------------------------------------------------------------------

struct Mutex::Impl {
    mtx_t mtx;
};

Mutex::Mutex() : impl_(new Impl) {
    mtx_init(&impl_->mtx, mtx_plain);
}

Mutex::~Mutex() {
    mtx_destroy(&impl_->mtx);
    delete impl_;
}

void Mutex::lock() {
    mtx_lock(&impl_->mtx);
}
void Mutex::unlock() {
    mtx_unlock(&impl_->mtx);
}

struct CondVar::Impl {
    cnd_t cnd;
};

CondVar::CondVar() : impl_(new Impl) {
    cnd_init(&impl_->cnd);
}
CondVar::~CondVar() {
    cnd_destroy(&impl_->cnd);
    delete impl_;
}

void CondVar::wait(Mutex& mutex) {
    cnd_wait(&impl_->cnd, &mutex.impl_->mtx);
}

void CondVar::notify_one() {
    cnd_signal(&impl_->cnd);
}
void CondVar::notify_all() {
    cnd_broadcast(&impl_->cnd);
}

// ---------------------------------------------------------------------------
// Futex
// ---------------------------------------------------------------------------

namespace {

struct Waiter {
    std::mutex mtx;
    std::condition_variable cv;
    bool woken = false;
};

std::mutex g_futex_registry_mutex;
std::map<uint32_t*, std::vector<Waiter*>> g_futex_registry;

} // namespace

struct Futex::Impl {
    // stateless; registry is global for simplicity of cross-Futex wakes
};

Futex::Futex() : impl_(new Impl) {}
Futex::~Futex() {
    delete impl_;
}

int Futex::wait(uint32_t* addr, uint32_t expected) {
    // Correctness note: the value check and the waiter enqueue happen under
    // one registry lock, mirroring the atomicity guarantees the Linux futex
    // syscall provides in-kernel. wake() pops under the same lock, so a wake
    // cannot be lost between the check and the sleep.
    Waiter waiter;
    {
        std::lock_guard<std::mutex> guard(g_futex_registry_mutex);
        if (*addr != expected) {
            return 1; // EWOULDBLOCK semantics
        }
        g_futex_registry[addr].push_back(&waiter);
    }

    {
        std::unique_lock<std::mutex> lk(waiter.mtx);
        waiter.cv.wait(lk, [&] { return waiter.woken; });
    }

    {
        std::lock_guard<std::mutex> guard(g_futex_registry_mutex);
        auto it = g_futex_registry.find(addr);
        if (it != g_futex_registry.end()) {
            for (size_t i = 0; i < it->second.size(); ++i) {
                if (it->second[i] == &waiter) {
                    it->second.erase(it->second.begin() + static_cast<long>(i));
                    break;
                }
            }
            if (it->second.empty())
                g_futex_registry.erase(it);
        }
    }
    return waiter.woken ? 0 : 1;
}

int Futex::wake(uint32_t* addr, uint32_t count) {
    std::vector<Waiter*> to_wake;
    {
        std::lock_guard<std::mutex> guard(g_futex_registry_mutex);
        auto it = g_futex_registry.find(addr);
        if (it == g_futex_registry.end())
            return 0;
        uint32_t n = 0;
        while (n < count && !it->second.empty()) {
            to_wake.push_back(it->second.back());
            it->second.pop_back();
            ++n;
        }
    }
    for (Waiter* w : to_wake) {
        std::lock_guard<std::mutex> lk(w->mtx);
        w->woken = true;
        w->cv.notify_one();
    }
    return static_cast<int>(to_wake.size());
}

} // namespace platform
} // namespace ps5droid
