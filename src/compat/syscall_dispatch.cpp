// PS5Droid - Linux syscall dispatch experiment (implementation).
#include "compat/syscall_dispatch.h"

#include <cstdio>
#include <cstring>

#include "platform/platform.h"

namespace ps5droid {
namespace compat {

namespace {

struct TimespecView { // Linux/Orbis layout, 8+8 bytes on x86_64
    int64_t tv_sec;
    int64_t tv_nsec;
};

} // namespace

SyscallDispatch::SyscallDispatch(CompatMemory* memory)
    : memory_(memory), context_id_(0x50534400ull | (platform::monotonic_ns() & 0xffffull)) {}

DispatchResult SyscallDispatch::ok(int64_t ret, const char* note) {
    return DispatchResult{ret, 0, true, note};
}

DispatchResult SyscallDispatch::fail(int err, const char* note) {
    return DispatchResult{-1, err, true, note};
}

DispatchResult SyscallDispatch::nosys(const char* note) {
    return DispatchResult{-1, linux_abi::kEnosys, false, note};
}

DispatchResult SyscallDispatch::invoke(int linux_nr, const SyscallArgs& args) {
    using namespace linux_abi;

    switch (linux_nr) {
    case kMmap: {
        uintptr_t hint = static_cast<uintptr_t>(args.a[0]);
        size_t len = static_cast<size_t>(args.a[1]);
        int prot = static_cast<int>(args.a[2]);
        int flags = static_cast<int>(args.a[3]);
        int fd = static_cast<int>(static_cast<int64_t>(args.a[4]));
        int64_t off = static_cast<int64_t>(args.a[5]);
        MapResult r = memory_->mmap(hint, len, prot, flags, fd, off);
        if (r.address == nullptr)
            return fail(r.linux_errno, r.note);
        return ok(reinterpret_cast<int64_t>(r.address), r.note);
    }

    case kMunmap: {
        uintptr_t addr = static_cast<uintptr_t>(args.a[0]);
        size_t len = static_cast<size_t>(args.a[1]);
        MapResult r = memory_->munmap(addr, len);
        if (r.address == nullptr && r.linux_errno != 0)
            return fail(r.linux_errno, r.note);
        return ok(0, r.note);
    }

    case kMprotect: {
        uintptr_t addr = static_cast<uintptr_t>(args.a[0]);
        size_t len = static_cast<size_t>(args.a[1]);
        int prot = static_cast<int>(args.a[2]);
        MapResult r = memory_->mprotect(addr, len, prot);
        if (r.address == nullptr && r.linux_errno != 0)
            return fail(r.linux_errno, r.note);
        return ok(0, r.note);
    }

    case kBrk: {
        uintptr_t req = static_cast<uintptr_t>(args.a[0]);
        BrkResult r = memory_->brk(req);
        return ok(static_cast<int64_t>(r.new_break), r.note);
    }

    case kWrite: {
        int fd = static_cast<int>(static_cast<int64_t>(args.a[0]));
        const void* buf = reinterpret_cast<const void*>(args.a[1]);
        size_t count = static_cast<size_t>(args.a[2]);
        if (buf == nullptr)
            return fail(kEfault, "write buf is null");
        if (fd != 1 && fd != 2) {
            return fail(kEbadf, "only the process streams (fd 1/2) exist in "
                                "the emulation model");
        }
        FILE* out = (fd == 1) ? stdout : stderr;
        size_t n = fwrite(buf, 1, count, out);
        fflush(out);
        return ok(static_cast<int64_t>(n), "stream passthrough");
    }

    case kGetpid:
        return ok(static_cast<int64_t>(context_id_), "single-process emulation context id");

    case kGettid:
        return ok(static_cast<int64_t>(platform::current_thread_id()), "platform thread id");

    case kClockGettime: {
        int clk = static_cast<int>(static_cast<int64_t>(args.a[0]));
        TimespecView* ts = reinterpret_cast<TimespecView*>(args.a[1]);
        if (ts == nullptr)
            return fail(kEfault, "timespec is null");
        uint64_t ns;
        if (clk == kClockRealtime) {
            ns = platform::wallclock_ns();
        } else if (clk == kClockMonotonic) {
            ns = platform::monotonic_ns();
        } else {
            return fail(kEinval, "unsupported clock id");
        }
        ts->tv_sec = static_cast<int64_t>(ns / 1000000000ull);
        ts->tv_nsec = static_cast<int64_t>(ns % 1000000000ull);
        return ok(0, "clock read");
    }

    case kNanosleep: {
        const TimespecView* req = reinterpret_cast<const TimespecView*>(args.a[0]);
        TimespecView* rem = reinterpret_cast<TimespecView*>(args.a[1]);
        if (req == nullptr)
            return fail(kEfault, "timespec is null");
        if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec > 999999999) {
            return fail(kEinval, "invalid timespec");
        }
        platform::sleep_ns(static_cast<uint64_t>(req->tv_sec) * 1000000000ull +
                           static_cast<uint64_t>(req->tv_nsec));
        if (rem != nullptr) {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
        return ok(0, "slept");
    }

    case kSchedYield:
        platform::yield_thread();
        return ok(0, "yielded");

    case kFutex: {
        uint32_t* uaddr = reinterpret_cast<uint32_t*>(args.a[0]);
        uint32_t op = static_cast<uint32_t>(args.a[1]);
        uint32_t val = static_cast<uint32_t>(args.a[2]);
        if (uaddr == nullptr)
            return fail(kEfault, "uaddr is null");
        uint32_t base = op & ~kFutexPrivateFlag;
        bool priv = (op & kFutexPrivateFlag) != 0;
        if (base == kFutexWait && priv) {
            int r = platform::Futex().wait(uaddr, val);
            return ok(r, r == 0 ? "woke" : "EWOULDBLOCK");
        }
        if (base == kFutexWake && priv) {
            int r = platform::Futex().wake(uaddr, val);
            return ok(r, "waiters woken");
        }
        return fail(kEinval, "only FUTEX_WAIT/WAKE with PRIVATE flag supported");
    }

    case kExitGroup:
        stop_requested_ = true;
        return ok(0, "context stop requested");

    default:
        break;
    }

    // Known syscall: surface the table's static note (stable storage;
    // never return pointers to stack-local buffers).
    for (int i = 0; i < linux_abi::kSyscallTableSize; ++i) {
        if (linux_abi::kSyscallTable[i].number == linux_nr) {
            return nosys(linux_abi::kSyscallTable[i].note);
        }
    }
    return nosys("unknown Linux syscall number for x86_64");
}

std::vector<SyscallDispatch::MatrixEntry> SyscallDispatch::matrix() const {
    std::vector<MatrixEntry> out;
    for (int i = 0; i < linux_abi::kSyscallTableSize; ++i) {
        const auto& e = linux_abi::kSyscallTable[i];
        out.push_back(MatrixEntry{e.number, e.name, e.support, e.note});
    }
    return out;
}

} // namespace compat
} // namespace ps5droid
