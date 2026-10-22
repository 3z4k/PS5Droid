#include "compat/syscall_dispatch.h"
#include "platform/platform.h"
#include "test_framework.h"
#include <atomic>
#include <cerrno>
#include <cstring>
#include <thread>
#if defined(__linux__)
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#endif
using namespace ps5droid::compat;
namespace {
struct File {
    const std::string path = "fd_semantics.tmp";
    File() { ps5droid::platform::write_file(path, "abcdef", 6); }
    ~File() { ps5droid::platform::remove_file(path); }
};
} // namespace
PS5DROID_TEST(fd_write_only_does_not_truncate) {
    File f;
    CompatFdTable t;
    int fd = t.open_file(f.path, 1, 0);
    CHECK(fd >= 3);
    CHECK(t.write(fd, "X", 1) == 1);
    CHECK(t.close(fd) == 0);
    std::string text;
    CHECK(ps5droid::platform::read_file(f.path, &text));
    CHECK(text == "Xbcdef");
}
PS5DROID_TEST(fd_linux_create_truncate_append_and_exclusive) {
    File f;
    CompatFdTable t;
    CHECK(t.open_file(f.path, 0x40 | 0x80 | 2, 0600) == -1);
    CHECK(errno == EEXIST);
    int fd = t.open_file(f.path, 0x40 | 0x200 | 2, 0600);
    CHECK(fd >= 3);
    StatBuf st{};
    CHECK(t.fstat(fd, &st));
    CHECK(st.st_size == 0);
    CHECK(t.write(fd, "a", 1) == 1);
    t.close(fd);
    fd = t.open_file(f.path, 0x400 | 1, 0);
    CHECK(fd >= 3);
    CHECK(t.lseek(fd, 0, 0) == 0);
    CHECK(t.write(fd, "b", 1) == 1);
    t.close(fd);
    std::string text;
    CHECK(ps5droid::platform::read_file(f.path, &text));
    CHECK(text == "ab");
}
PS5DROID_TEST(fd_dup_survives_close_and_shares_position) {
    File f;
    CompatFdTable t;
    int fd = t.open_file(f.path, 2, 0);
    CHECK(fd >= 3);
    int copy = t.dup(fd);
    CHECK(copy >= 3);
    char c = 0;
    CHECK(t.read(fd, &c, 1) == 1 && c == 'a');
    CHECK(t.read(copy, &c, 1) == 1 && c == 'b');
    CHECK(t.close(fd) == 0);
    CHECK(t.read(copy, &c, 1) == 1 && c == 'c');
    CHECK(t.dup2(copy, copy) == copy);
    CHECK(t.read(copy, &c, 1) == 1 && c == 'd');
}
PS5DROID_TEST(fd_pread_preserves_offset_and_checks_negative_offset) {
    File f;
    CompatFdTable t;
    int fd = t.open_file(f.path, 0, 0);
    CHECK(fd >= 3);
    CHECK(t.lseek(fd, 2, 0) == 2);
    char c;
    CHECK(t.pread64(fd, &c, 1, 0) == 1 && c == 'a');
    CHECK(t.lseek(fd, 0, 1) == 2);
    CHECK(t.pread64(fd, &c, 1, -1) == -1);
    CHECK(errno == EINVAL);
}
PS5DROID_TEST(fd_reuses_lowest_descriptor_and_dup2_replaces_target) {
    File f;
    CompatFdTable t;
    int a = t.open_file(f.path, 0, 0), b = t.open_file(f.path, 0, 0);
    CHECK(a == 3 && b == 4);
    CHECK(t.lseek(a, 3, 0) == 3);
    CHECK(t.dup2(a, b) == b);
    char c;
    CHECK(t.read(b, &c, 1) == 1 && c == 'd');
    CHECK(t.close(a) == 0);
    CHECK(t.dup(b) == 3);
}
PS5DROID_TEST(fd_errors_are_translated_to_linux) {
    CompatMemory mem;
    SyscallDispatch d(&mem);
    const char* missing = "ps5droid_missing_directory/file";
    SyscallArgs a{};
    a.a[0] = reinterpret_cast<uint64_t>(missing);
    auto r = d.invoke(ps5droid::linux_abi::kOpen, a);
    CHECK(r.ret == -1 && r.err == 2);
    a.a[0] = 0x80000000;
    r = d.invoke(ps5droid::linux_abi::kEpollCreate1, a);
    CHECK(r.ret == -1 && r.err == 22);
    a = {};
    a.a[0] = 0;
    a.a[1] = reinterpret_cast<uint64_t>("relative");
    r = d.invoke(ps5droid::linux_abi::kOpenat, a);
    CHECK(r.ret == -1 && r.err == 38);
}
PS5DROID_TEST(eventfd_nonblocking_semaphore_and_overflow) {
    CompatFdTable t;
    int fd = t.create_eventfd(2, 1 | 0x800);
    CHECK(fd >= 3);
    uint64_t value = 0;
    CHECK(t.read(fd, &value, 8) == 8 && value == 1);
    CHECK(t.read(fd, &value, 8) == 8 && value == 1);
    CHECK(t.read(fd, &value, 8) == -1 && errno == EAGAIN);
    value = UINT64_MAX;
    CHECK(t.write(fd, &value, 8) == -1 && errno == EINVAL);
    value = UINT64_MAX - 1;
    CHECK(t.write(fd, &value, 8) == 8);
    value = 1;
    CHECK(t.write(fd, &value, 8) == -1 && errno == EAGAIN);
    CHECK(t.create_eventfd(0, 0x400) == -1 && errno == EINVAL);
}
PS5DROID_TEST(epoll_reports_readiness_not_registration) {
    CompatFdTable t;
    int fd = t.create_eventfd(0, 0x800), ep = t.create_epoll();
    EpollEvent in{1, 0x123456789abcdef0ull}, out{};
    CHECK(t.epoll_ctl(ep, 1, fd, &in));
    CHECK(t.epoll_wait(ep, &out, 1, 0) == 0);
    uint64_t value = 1;
    CHECK(t.write(fd, &value, 8) == 8);
    CHECK(t.epoll_wait(ep, &out, 1, 0) == 1 && out.events == 1 && out.data == in.data);
    CHECK(t.read(fd, &value, 8) == 8);
    CHECK(t.epoll_wait(ep, &out, 1, 0) == 0);
    CHECK(!t.epoll_ctl(ep, 1, fd, &in) && errno == EEXIST);
    in.events = 0x80000001u;
    CHECK(!t.epoll_ctl(ep, 3, fd, &in) && errno == EINVAL);
}
PS5DROID_TEST(epoll_wait_wakes_after_eventfd_write) {
    CompatFdTable t;
    int fd = t.create_eventfd(0, 0x800), ep = t.create_epoll();
    EpollEvent in{1, 99}, out{};
    CHECK(t.epoll_ctl(ep, 1, fd, &in));
    std::atomic<bool> entered{false};
    int result = -1;
    std::thread waiter([&] {
        entered = true;
        result = t.epoll_wait(ep, &out, 1, 2000);
    });
    while (!entered)
        std::this_thread::yield();
    uint64_t value = 1;
    auto written = t.write(fd, &value, 8);
    waiter.join();
    CHECK(written == 8);
    CHECK(result == 1 && out.data == 99);
}
PS5DROID_TEST(epoll_last_descriptor_close_removes_watch) {
    CompatFdTable t;
    int fd = t.create_eventfd(1, 0x800), ep = t.create_epoll();
    EpollEvent event{1, 1}, out{};
    CHECK(t.epoll_ctl(ep, 1, fd, &event));
    int copy = t.dup(fd);
    t.close(fd);
    CHECK(t.epoll_wait(ep, &out, 1, 0) == 1);
    t.close(copy);
    CHECK(t.epoll_wait(ep, &out, 1, 0) == 0);
}
PS5DROID_TEST(epoll_rejects_regular_files) {
    File f;
    CompatFdTable t;
    int fd = t.open_file(f.path, 0, 0), ep = t.create_epoll();
    EpollEvent event{1, 0};
    CHECK(!t.epoll_ctl(ep, 1, fd, &event));
    CHECK(errno == EPERM);
}
#if defined(__linux__)
PS5DROID_TEST(linux_differential_eventfd_and_epoll) {
    CompatFdTable t;
    int guest = t.create_eventfd(0, 0x800), gep = t.create_epoll();
    int native = eventfd(0, EFD_NONBLOCK), nep = epoll_create1(0);
    CHECK(native >= 0 && nep >= 0);
    EpollEvent g{1, 42}, gout{};
    epoll_event n{};
    n.events = EPOLLIN;
    n.data.u64 = 42;
    bool setup = t.epoll_ctl(gep, 1, guest, &g) && epoll_ctl(nep, EPOLL_CTL_ADD, native, &n) == 0;
    int empty_guest = t.epoll_wait(gep, &gout, 1, 0), empty_native = epoll_wait(nep, &n, 1, 0);
    uint64_t value = 7, gv = 0, nv = 0;
    auto gw = t.write(guest, &value, 8);
    auto nw = ::write(native, &value, 8);
    int ready_guest = t.epoll_wait(gep, &gout, 1, 0), ready_native = epoll_wait(nep, &n, 1, 0);
    auto gr = t.read(guest, &gv, 8);
    auto nr = ::read(native, &nv, 8);
    ::close(native);
    ::close(nep);
    CHECK(setup);
    CHECK(empty_guest == empty_native && empty_guest == 0);
    CHECK(gw == nw && gr == nr && gv == nv);
    CHECK(ready_guest == ready_native && ready_guest == 1);
    CHECK(gout.events == n.events && gout.data == n.data.u64);
}
#endif
PS5DROID_TEST(openat_accepts_both_register_encodings_of_at_fdcwd) {
    File file;
    CompatMemory memory;
    SyscallDispatch dispatch(&memory);
    for (uint64_t dirfd : {uint64_t(0xffffffffffffff9cull), uint64_t(0xffffff9cull)}) {
        SyscallArgs args{};
        args.a[0] = dirfd;
        args.a[1] = reinterpret_cast<uint64_t>(file.path.c_str());
        auto result = dispatch.invoke(ps5droid::linux_abi::kOpenat, args);
        CHECK(result.ret >= 3);
        CHECK(dispatch.fd_table()->close(static_cast<int>(result.ret)) == 0);
    }
}
PS5DROID_TEST(futex_rejects_unsupported_timeouts_without_waiting) {
    CompatMemory memory;
    SyscallDispatch dispatch(&memory);
    uint32_t word = 1;
    SyscallArgs args{};
    args.a[0] = reinterpret_cast<uint64_t>(&word);
    args.a[1] = 128;
    args.a[2] = 1;
    args.a[3] = 1;
    auto result = dispatch.invoke(ps5droid::linux_abi::kFutex, args);
    CHECK(result.ret == -1 && result.err == 22);
}
