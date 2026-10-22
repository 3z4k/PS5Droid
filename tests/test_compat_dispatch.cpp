// PS5Droid - Linux ABI dispatch tests.
#include "test_framework.h"

#include <cstring>

#include "compat/memory_compat.h"
#include "compat/syscall_dispatch.h"
#include "platform/platform.h"

using namespace ps5droid::compat;
using namespace ps5droid::linux_abi;

namespace {
// A single mapping returned by the dispatch (Linux mmap=9).
void* mmap_via(SyscallDispatch* d, size_t len) {
    SyscallArgs a{};
    a.a[1] = len;
    a.a[2] = kProtRead | kProtWrite;
    a.a[3] = kMapPrivate | kMapAnonymous;
    a.a[4] = static_cast<uint64_t>(-1);
    DispatchResult r = d->invoke(kMmap, a);
    return r.ret > 0 ? reinterpret_cast<void*>(r.ret) : nullptr;
}
} // namespace

PS5DROID_TEST(mmap_munmap_end_to_end) {
    CompatMemory mem;
    SyscallDispatch d(&mem);

    void* p = mmap_via(&d, 8192);
    CHECK_MSG(p != nullptr, "mmap(9) failed");

    uint8_t* bytes = static_cast<uint8_t*>(p);
    memset(bytes, 0x5A, 8192);
    CHECK_MSG(bytes[0] == 0x5A && bytes[8191] == 0x5A, "memory not usable");

    SyscallArgs u{};
    u.a[0] = reinterpret_cast<uint64_t>(p);
    u.a[1] = 8192;
    DispatchResult r = d.invoke(kMunmap, u);
    CHECK_MSG(r.ret == 0, "munmap(11) failed");
}

PS5DROID_TEST(mmap_validates_arguments) {
    CompatMemory mem;
    SyscallDispatch d(&mem);

    SyscallArgs a{};
    a.a[1] = 4096;
    a.a[3] = kMapPrivate; // missing MAP_ANONYMOUS
    a.a[4] = static_cast<uint64_t>(-1);
    DispatchResult r = d.invoke(kMmap, a);
    CHECK_MSG(r.ret == -1 && r.err == kEinval, "must reject file-backed");

    SyscallArgs b{};
    b.a[1] = 0; // zero length
    b.a[3] = kMapPrivate | kMapAnonymous;
    r = d.invoke(kMmap, b);
    CHECK_MSG(r.ret == -1 && r.err == kEinval, "must reject zero length");

    SyscallArgs c{};
    c.a[1] = 4096;
    c.a[3] = kMapPrivate | kMapAnonymous | kMapFixed;
    c.a[4] = static_cast<uint64_t>(-1);
    r = d.invoke(kMmap, c);
    CHECK_MSG(r.ret == -1 && r.err == kEinval, "must reject MAP_FIXED");
}

PS5DROID_TEST(brk_semantics) {
    CompatMemory mem;
    SyscallDispatch d(&mem);

    SyscallArgs a{};
    DispatchResult r = d.invoke(kBrk, a); // brk(0) initializes heap
    CHECK_MSG(r.ret > 0, "brk(0) must return the initial break");
    uintptr_t start = static_cast<uintptr_t>(r.ret);

    SyscallArgs b{};
    b.a[0] = start + 4096;
    r = d.invoke(kBrk, b);
    CHECK_MSG(static_cast<uintptr_t>(r.ret) == start + 4096, "brk must grow");

    // The grown region must be writable.
    uint8_t* p = reinterpret_cast<uint8_t*>(start + 8);
    p[0] = 0x42;
    p[4000] = 0x43;
    CHECK_MSG(p[0] == 0x42 && p[4000] == 0x43, "brk region not writable");

    // Overflowing request returns the current break (Linux behavior).
    SyscallArgs c{};
    c.a[0] = start + (1ull << 40);
    r = d.invoke(kBrk, c);
    CHECK_MSG(static_cast<uintptr_t>(r.ret) == start + 4096,
              "failed brk must return current break");
}

PS5DROID_TEST(clock_gettime_advances) {
    CompatMemory mem;
    SyscallDispatch d(&mem);

    static uint64_t buf[2];
    SyscallArgs a{};
    a.a[0] = kClockMonotonic;
    a.a[1] = reinterpret_cast<uint64_t>(buf);
    DispatchResult r = d.invoke(kClockGettime, a);
    CHECK_MSG(r.ret == 0, "clock_gettime failed");
    uint64_t first = static_cast<uint64_t>(buf[0]) * 1000000000ull + static_cast<uint64_t>(buf[1]);
    CHECK_MSG(first > 0, "monotonic clock must be positive");

    ps5droid::platform::sleep_ns(2000000); // 2 ms
    r = d.invoke(kClockGettime, a);
    uint64_t second = static_cast<uint64_t>(buf[0]) * 1000000000ull + static_cast<uint64_t>(buf[1]);
    CHECK_MSG(second > first, "monotonic clock must advance");
}

PS5DROID_TEST(getpid_gettid) {
    CompatMemory mem;
    SyscallDispatch d(&mem);
    DispatchResult pid = d.invoke(kGetpid, {});
    DispatchResult tid = d.invoke(kGettid, {});
    CHECK_MSG(pid.ret > 0, "getpid must return the context id");
    CHECK_MSG(tid.ret > 0, "gettid must return a thread id");

    CompatMemory mem2;
    SyscallDispatch d2(&mem2);
    DispatchResult pid2 = d2.invoke(kGetpid, {});
    CHECK_MSG(pid2.ret != pid.ret, "contexts must have distinct ids");
}

PS5DROID_TEST(futex_via_dispatch) {
    CompatMemory mem;
    SyscallDispatch d(&mem);

    static uint32_t word;
    word = 100;

    // EWOULDBLOCK: value mismatch
    SyscallArgs a{};
    a.a[0] = reinterpret_cast<uint64_t>(&word);
    a.a[1] = kFutexWait | kFutexPrivateFlag;
    a.a[2] = 999;
    DispatchResult r = d.invoke(kFutex, a);
    CHECK_MSG(r.ret == 1, "expected EWOULDBLOCK");

    // No-waiter wake returns 0
    SyscallArgs b{};
    b.a[0] = reinterpret_cast<uint64_t>(&word);
    b.a[1] = kFutexWake | kFutexPrivateFlag;
    b.a[2] = 1;
    r = d.invoke(kFutex, b);
    CHECK_MSG(r.ret == 0, "expected zero waiters");

    // Non-private and unsupported ops must be refused
    SyscallArgs c{};
    c.a[0] = reinterpret_cast<uint64_t>(&word);
    c.a[1] = kFutexWake; // missing PRIVATE flag
    r = d.invoke(kFutex, c);
    CHECK_MSG(r.ret == -1 && r.err == kEinval, "must refuse non-private");
}

PS5DROID_TEST(write_passthrough) {
    CompatMemory mem;
    SyscallDispatch d(&mem);
    SyscallArgs a{};
    const char* msg = "";
    a.a[0] = 1;
    a.a[1] = reinterpret_cast<uint64_t>(msg);
    a.a[2] = 0;
    DispatchResult r = d.invoke(kWrite, a);
    CHECK_MSG(r.ret == 0, "empty write should write 0 bytes");

    // fd 3 does not exist in the emulation model
    a.a[0] = 3;
    r = d.invoke(kWrite, a);
    CHECK_MSG(r.ret == -1 && r.err == kEbadf, "fd 3 must be EBADF");
}

PS5DROID_TEST(honest_enosys_for_blockers) {
    CompatMemory mem;
    SyscallDispatch d(&mem);
    const int blocked[] = {kClone, kFork, kExecve, kSocket, kUname, kPrctl, kGetuid};
    for (int nr : blocked) {
        SyscallArgs a{};
        DispatchResult r = d.invoke(nr, a);
        char msg[96];
        snprintf(msg, sizeof(msg), "syscall %d must be ENOSYS", nr);
        CHECK_MSG(r.ret == -1 && r.err == kEnosys && !r.supported, msg);
    }
}

PS5DROID_TEST(fd_table_and_epoll_dispatch) {
    CompatMemory mem;
    CompatFdTable fdt;
    SyscallDispatch d(&mem, &fdt);

    std::string tmp = "ps5droid_fd_test.tmp";
    SyscallArgs open_args{};
    open_args.a[0] = reinterpret_cast<uint64_t>(tmp.c_str());
    open_args.a[1] = 0x40 | 0x200 | 0x2; // O_CREAT|O_TRUNC|O_RDWR
    DispatchResult r = d.invoke(kOpen, open_args);
    CHECK_MSG(r.ret >= 3, "open should return valid guest fd");
    int fd = static_cast<int>(r.ret);

    const char* data = "hello fd_table";
    SyscallArgs write_args{};
    write_args.a[0] = fd;
    write_args.a[1] = reinterpret_cast<uint64_t>(data);
    write_args.a[2] = strlen(data);
    r = d.invoke(kWrite, write_args);
    CHECK_MSG(r.ret == static_cast<int64_t>(strlen(data)), "write to fd failed");

    SyscallArgs seek_args{};
    seek_args.a[0] = fd;
    seek_args.a[1] = 0;
    seek_args.a[2] = 0; // SEEK_SET
    r = d.invoke(kLseek, seek_args);
    CHECK_MSG(r.ret == 0, "lseek failed");

    char buf[64] = {0};
    SyscallArgs read_args{};
    read_args.a[0] = fd;
    read_args.a[1] = reinterpret_cast<uint64_t>(buf);
    read_args.a[2] = sizeof(buf);
    r = d.invoke(kRead, read_args);
    CHECK_MSG(r.ret == static_cast<int64_t>(strlen(data)) && strcmp(buf, data) == 0,
              "read from fd failed");

    SyscallArgs ep_create{};
    r = d.invoke(kEpollCreate1, ep_create);
    CHECK_MSG(r.ret >= 3, "epoll_create1 failed");
    int epfd = static_cast<int>(r.ret);

    int eventfd = fdt.create_eventfd(1, 0x800);
    CHECK(eventfd >= 3);
    EpollEvent ev{1 /* EPOLLIN */, 12345};
    SyscallArgs ep_ctl{};
    ep_ctl.a[0] = epfd;
    ep_ctl.a[1] = 1; // EPOLL_CTL_ADD
    ep_ctl.a[2] = eventfd;
    ep_ctl.a[3] = reinterpret_cast<uint64_t>(&ev);
    r = d.invoke(kEpollCtl, ep_ctl);
    CHECK_MSG(r.ret == 0, "epoll_ctl failed");

    EpollEvent out_evs[4];
    SyscallArgs ep_wait{};
    ep_wait.a[0] = epfd;
    ep_wait.a[1] = reinterpret_cast<uint64_t>(out_evs);
    ep_wait.a[2] = 4;
    ep_wait.a[3] = 0;
    r = d.invoke(kEpollWait, ep_wait);
    CHECK_MSG(r.ret == 1 && out_evs[0].data == 12345, "epoll_wait failed");

    SyscallArgs close_args{};
    close_args.a[0] = fd;
    d.invoke(kClose, close_args);
    close_args.a[0] = epfd;
    d.invoke(kClose, close_args);
    fdt.close(eventfd);
    ps5droid::platform::remove_file(tmp);
}

PS5DROID_TEST(exit_group_sets_flag) {
    CompatMemory mem;
    SyscallDispatch d(&mem);
    CHECK_MSG(!d.stop_requested(), "must start unrequested");
    DispatchResult r = d.invoke(kExitGroup, {});
    CHECK_MSG(r.ret == 0, "exit_group returns 0");
    CHECK_MSG(d.stop_requested(), "exit_group must flag the context");
}
