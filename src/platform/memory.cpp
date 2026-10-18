// PS5Droid - memory platform implementation.
//
// One implementation serves both backends: POSIX and Orbis both expose
// mmap/munmap/mprotect through their libc (verified for the PS5 backend in
// ps5-payload-dev/sdk libc/mman.c, which implements mmap over FreeBSD
// syscall 477). Only flags/modes common to both ABIs are used.
#include "platform/platform.h"

#include <cerrno>
#include <cstring>

#if !defined(PS5DROID_PLATFORM_PS5)
#include <sys/mman.h>
#include <unistd.h>
#else
#include <sys/mman.h>
#include <unistd.h>
// Orbis payloads: mmap/munmap/mprotect come from the ps5-payload-sdk libc.
// page size: sysconf(_SC_PAGESIZE) is expected to work (FreeBSD libc); the
// fallback below is verified by the capability probe at runtime.
#endif

namespace ps5droid {
namespace platform {

size_t page_size() {
    long value = sysconf(_SC_PAGESIZE);
    if (value <= 0) {
        // Conservative default; probe_platform() reports the real value.
        return 4096;
    }
    return static_cast<size_t>(value);
}

void* map_anonymous(size_t size) {
    if (size == 0)
        return nullptr;
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return nullptr;
    return p;
}

bool unmap(void* address, size_t size) {
    if (address == nullptr || size == 0)
        return false;
    return munmap(address, size) == 0;
}

bool protect_memory(void* address, size_t size, bool readable, bool writable) {
    if (address == nullptr || size == 0)
        return false;
    int prot = 0;
    if (readable)
        prot |= PROT_READ;
    if (writable)
        prot |= PROT_WRITE;
    // Note: PROT_EXEC is deliberately never requested here; JIT pages are a
    // later research topic (see docs/platform-constraints.md).
    return mprotect(address, size, prot) == 0;
}

} // namespace platform
} // namespace ps5droid
