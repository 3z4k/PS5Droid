// PS5Droid - Linux ABI tables (see linux_abi.h for sources and rules).
#include "compat/linux_abi.h"

#include <cstddef>

namespace ps5droid {
namespace linux_abi {

const SyscallInfo kSyscallTable[] = {
    // --- emulated, real in-process behavior -------------------------------
    {kWrite, "write", kEmulated, "fd 1/2 passthrough to the process streams"},
    {kMmap, "mmap", kEmulated, "anonymous private mappings only (arena-backed)"},
    {kMunmap, "munmap", kPartial,
     "whole-mapping release only; partial interior ranges return EINVAL"},
    {kMprotect, "mprotect", kPartial,
     "R/W adjustments honored; PROT_EXEC is accepted but not granted "
     "(no JIT path yet)"},
    {kBrk, "brk", kEmulated, "heap over an internal anonymous arena"},
    {kSchedYield, "sched_yield", kEmulated, "maps to platform yield"},
    {kNanosleep, "nanosleep", kEmulated, "maps to platform sleep"},
    {kGetpid, "getpid", kEmulated, "single-process model: returns the emulation context id"},
    {kGettid, "gettid", kEmulated, "maps to the platform thread id"},
    {kClockGettime, "clock_gettime", kEmulated,
     "CLOCK_MONOTONIC / CLOCK_REALTIME from platform clocks"},
    {kFutex, "futex", kPartial, "FUTEX_WAIT/WAKE with PRIVATE flag only, registry-based"},
    {kExitGroup, "exit_group", kEmulated, "flags the emulation context as stopping"},

    // --- partial / restricted ---------------------------------------------
    {kRead, "read", kUnsupported, "no guest fd table yet; stdin not wired"},

    // --- honestly unsupported; these are the hard blockers -----------------
    {kOpen, "open", kUnsupported, "guest fd table not implemented"},
    {kClose, "close", kUnsupported, "guest fd table not implemented"},
    {kStat, "stat", kUnsupported, "guest fd table not implemented"},
    {kFstat, "fstat", kUnsupported, "guest fd table not implemented"},
    {kLseek, "lseek", kUnsupported, "guest fd table not implemented"},
    {kRtSigaction, "rt_sigaction", kUnsupported, "Linux signal ABI not implemented on Orbis"},
    {kRtSigprocmask, "rt_sigprocmask", kUnsupported, "Linux signal ABI not implemented on Orbis"},
    {kIoctl, "ioctl", kUnsupported, "Linux ioctl subsys vary wildly; none wired"},
    {kAccess, "access", kUnsupported, "guest path layer not implemented"},
    {kMremap, "mremap", kUnsupported, "not implemented"},
    {kMadvise, "madvise", kUnsupported, "not implemented"},
    {kDup, "dup", kUnsupported, "guest fd table not implemented"},
    {kDup2, "dup2", kUnsupported, "guest fd table not implemented"},
    {kSocket, "socket", kUnsupported, "BSD socket domains/families differ from Linux; not wired"},
    {kConnect, "connect", kUnsupported, "socket layer not implemented"},
    {kAccept, "accept", kUnsupported, "socket layer not implemented"},
    {kBind, "bind", kUnsupported, "socket layer not implemented"},
    {kListen, "listen", kUnsupported, "socket layer not implemented"},
    {kClone, "clone", kUnsupported,
     "HARD BLOCKER: no process/thread creation via Linux clone() on Orbis"},
    {kFork, "fork", kUnsupported, "HARD BLOCKER: no process creation API for Orbis payloads"},
    {kVfork, "vfork", kUnsupported, "HARD BLOCKER: see fork"},
    {kExecve, "execve", kUnsupported, "HARD BLOCKER: no ELF exec loader exposed to payloads"},
    {kExit, "exit", kUnsupported, "thread exit path not implemented"},
    {kWait4, "wait4", kUnsupported, "no child processes exist in the emulation model"},
    {kKill, "kill", kUnsupported, "no signal delivery between processes"},
    {kUname, "uname", kUnsupported, "would require synthesizing a fake kernel identity; refused"},
    {kFcntl, "fcntl", kUnsupported, "guest fd table not implemented"},
    {kFlock, "flock", kUnsupported, "guest fd table not implemented"},
    {kFsync, "fsync", kUnsupported, "guest fd table not implemented"},
    {kGetcwd, "getcwd", kUnsupported, "guest path layer not implemented"},
    {kChdir, "chdir", kUnsupported, "guest path layer not implemented"},
    {kMkdir, "mkdir", kUnsupported, "guest path layer not implemented"},
    {kUnlink, "unlink", kUnsupported, "guest path layer not implemented"},
    {kReadlink, "readlink", kUnsupported, "guest path layer not implemented"},
    {kGettimeofday, "gettimeofday", kUnsupported, "not implemented"},
    {kGetuid, "getuid", kUnsupported, "identity model undecided; refusing to fake a uid"},
    {kGetgid, "getgid", kUnsupported, "identity model undecided"},
    {kGeteuid, "geteuid", kUnsupported, "identity model undecided"},
    {kGetppid, "getppid", kUnsupported, "no parent in emulation model"},
    {kSigaltstack, "sigaltstack", kUnsupported, "signal ABI not implemented"},
    {kPersonality, "personality", kUnsupported, "not implemented"},
    {kPrctl, "prctl", kUnsupported, "bionic relies on prctl during init; not implemented"},
    {kArchPrctl, "arch_prctl", kUnsupported, "not implemented"},
    {kSchedSetaffinity, "sched_setaffinity", kUnsupported, "not implemented"},
    {kSchedGetaffinity, "sched_getaffinity", kUnsupported, "not implemented"},
    {kGetdents64, "getdents64", kUnsupported, "guest path layer not implemented"},
    {kSetTidAddress, "set_tid_address", kUnsupported, "not implemented"},
    {kClockGetres, "clock_getres", kUnsupported, "not implemented"},
    {kClockNanosleep, "clock_nanosleep", kUnsupported, "not implemented"},
    {kEpollWait, "epoll_wait", kUnsupported, "epoll API absent on Orbis; would need a kqueue shim"},
    {kEpollCtl, "epoll_ctl", kUnsupported, "epoll API absent on Orbis"},
    {kTgkill, "tgkill", kUnsupported, "signal ABI not implemented"},
    {kOpenat, "openat", kUnsupported, "guest fd table not implemented"},
    {kNewfstatat, "newfstatat", kUnsupported, "guest path layer missing"},
    {kUnlinkat, "unlinkat", kUnsupported, "guest path layer missing"},
    {kPpoll, "ppoll", kUnsupported, "poll layer not implemented"},
    {kSetRobustList, "set_robust_list", kUnsupported, "not implemented"},
    {kEventfd2, "eventfd2", kUnsupported, "not implemented"},
    {kEpollCreate1, "epoll_create1", kUnsupported, "epoll API absent"},
    {kDup3, "dup3", kUnsupported, "guest fd table not implemented"},
    {kPipe2, "pipe2", kUnsupported, "no guest fd table"},
    {kInotifyInit1, "inotify_init1", kUnsupported, "not implemented"},
    {kMemfdCreate, "memfd_create", kUnsupported,
     "Linux memfd has no Orbis equivalent; unimplemented"},
    {kGetrandom, "getrandom", kUnsupported, "entropy source available via libc; API not wired yet"},
};

const int kSyscallTableSize = static_cast<int>(sizeof(kSyscallTable) / sizeof(kSyscallTable[0]));

const char* syscall_name(int number) {
    for (int i = 0; i < kSyscallTableSize; ++i) {
        if (kSyscallTable[i].number == number && kSyscallTable[i].name) {
            return kSyscallTable[i].name;
        }
    }
    return nullptr;
}

const FreebsdMapping kFreebsdVerified[] = {
    {kRead, 3, "ps5-payload-sdk crt/syscall.h SYS_read"},
    {kWrite, 4, "ps5-payload-sdk crt/syscall.h SYS_write"},
    {kOpen, 5, "ps5-payload-sdk crt/syscall.h SYS_open"},
    {kClose, 6, "ps5-payload-sdk crt/syscall.h SYS_close"},
    {kGetpid, 20, "ps5-payload-sdk crt/syscall.h SYS_getpid"},
    {kAccess, 33, "ps5-payload-sdk crt/syscall.h SYS_access"},
    {kMunmap, 73, "ps5-payload-sdk crt/syscall.h SYS_munmap"},
    {kMprotect, 74, "ps5-payload-sdk crt/syscall.h SYS_mprotect"},
    {kMmap, 477, "ps5-payload-sdk crt/syscall.h SYS_mmap"},
    {kLseek, 478, "ps5-payload-sdk crt/syscall.h SYS_lseek"},
};

const int kFreebsdVerifiedSize =
    static_cast<int>(sizeof(kFreebsdVerified) / sizeof(kFreebsdVerified[0]));

const FreebsdMapping* freebsd_mapping(int linux_nr) {
    for (int i = 0; i < kFreebsdVerifiedSize; ++i) {
        if (kFreebsdVerified[i].linux_nr == linux_nr) {
            return &kFreebsdVerified[i];
        }
    }
    return nullptr;
}

} // namespace linux_abi
} // namespace ps5droid
