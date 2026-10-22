#include "compat/elf_runner.h"
#include "compat/bionic_loader.h"
#include "compat/syscall_dispatch.h"
#include "platform/platform.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <sys/mman.h>

namespace ps5droid {
namespace compat {
bool probe_rx_memory(std::string* detail) {
    size_t size = platform::page_size();
    void* page = platform::map_anonymous(size);
    if (!page) {
        if (detail)
            *detail = "RW allocation failed";
        return false;
    }
    bool result = mprotect(page, size, PROT_READ | PROT_EXEC) == 0;
    int error = errno;
    platform::unmap(page, size);
    if (detail)
        *detail = result ? "RW -> RX protection accepted; no JIT or guest execution verified"
                         : "RW -> RX protection refused (host errno " + std::to_string(error) + ")";
    return result;
}
namespace {
#if defined(__x86_64__)
struct GatewayContext {
    SyscallDispatch* dispatch;
    const uint8_t* image;
    size_t image_size;
    const uint8_t* stack;
    size_t stack_size;
};
bool contains(const uint8_t* base, size_t size, uint64_t address, uint64_t count) {
    uint64_t start = reinterpret_cast<uint64_t>(base);
    return address >= start && address - start <= size && count <= size - (address - start);
}
int64_t gateway(void* opaque, int number, const SyscallArgs* args) {
    auto& context = *static_cast<GatewayContext*>(opaque);
    if (!args)
        return -14;
    if (number == linux_abi::kWrite) {
        if (args->a[0] != 1 && args->a[0] != 2)
            return -9;
        if (!contains(context.image, context.image_size, args->a[1], args->a[2]) &&
            !contains(context.stack, context.stack_size, args->a[1], args->a[2]))
            return -14;
    } else if (number != linux_abi::kGetpid && number != linux_abi::kExitGroup)
        return -38;
    auto result = context.dispatch->invoke(number, *args);
    return result.err ? -result.err : result.ret;
}
#endif
} // namespace
ElfRunResult run_cooperative_elf(const void* file, size_t size) {
    ElfRunResult out;
    auto info = BionicLoader::parse_header(file, size);
    if (!info.valid) {
        out.note = info.error;
        return out;
    }
#if !defined(__x86_64__)
    out.note = "native x86_64 execution is unavailable in this host build";
    return out;
#else
    auto fail = [&](const char* note) {
        out.note = note;
        return out;
    };
    if (info.elf_type != 3)
        return fail("cooperative runner requires position-independent ET_DYN");
    const size_t page = platform::page_size();
    constexpr uint64_t limit = 64 * 1024 * 1024;
    uint64_t end = 0, phdr_address = 0;
    size_t loads = 0;
    for (const auto& p : info.phdrs) {
        if (p.type == 2 || p.type == 3 || p.type == 7)
            return fail("dynamic linking, PT_INTERP and TLS execution are not implemented");
        if (p.type != 1 || !p.memsz)
            continue;
        if ((p.flags & ~7u) || (p.flags & 3u) == 3u || p.vaddr % page || p.vaddr > limit ||
            p.memsz > limit - p.vaddr)
            return fail("unsupported PT_LOAD permissions, alignment or image size");
        uint64_t segment_end = (p.vaddr + p.memsz + page - 1) / page * page;
        for (const auto& other : info.phdrs) {
            if (&other == &p || other.type != 1 || !other.memsz)
                continue;
            if (other.vaddr < segment_end && p.vaddr < other.vaddr + other.memsz)
                return fail("overlapping PT_LOAD pages are not supported");
        }
        end = std::max(end, segment_end);
        ++loads;
        uint64_t table_size = info.phdrs.size() * 56;
        if (info.phdr_offset >= p.offset && info.phdr_offset - p.offset <= p.filesz &&
            table_size <= p.filesz - (info.phdr_offset - p.offset))
            phdr_address = p.vaddr + info.phdr_offset - p.offset;
        // Conservative rejection, not instruction decoding or syscall interception.
        if (p.flags & 1) {
            const auto* bytes = static_cast<const uint8_t*>(file) + p.offset;
            for (size_t i = 1; i < p.filesz; ++i)
                if (bytes[i - 1] == 0x0f && bytes[i] == 0x05)
                    return fail("raw Linux syscall bytes require a future interception backend");
        }
    }
    if (!loads || !end || !phdr_address)
        return fail("PT_LOAD must include the program header table");
    auto release = [end](void* p) {
        if (p)
            platform::unmap(p, end);
    };
    std::unique_ptr<void, decltype(release)> memory(platform::map_anonymous(end), release);
    if (!memory)
        return fail("cannot allocate ELF image");
    auto* image = static_cast<uint8_t*>(memory.get());
    for (const auto& p : info.phdrs)
        if (p.type == 1 && p.filesz)
            std::memcpy(image + p.vaddr, static_cast<const uint8_t*>(file) + p.offset, p.filesz);
    if (mprotect(image, end, PROT_NONE))
        return fail("cannot protect ELF image gaps");
    for (const auto& p : info.phdrs)
        if (p.type == 1 && p.memsz) {
            int prot = (p.flags & 4 ? PROT_READ : 0) | (p.flags & 2 ? PROT_WRITE : 0) |
                       (p.flags & 1 ? PROT_EXEC : 0);
            size_t length = (p.memsz + page - 1) / page * page;
            if (mprotect(image + p.vaddr, length, prot))
                return fail("cannot apply ELF segment protections");
        }
    auto stack = BionicLoader::build_initial_stack(info, {"ps5droid-fixture"}, {}, 0,
                                                   reinterpret_cast<uint64_t>(image) + phdr_address,
                                                   reinterpret_cast<uint64_t>(image), page);
    if (!stack.error.empty()) {
        out.note = stack.error;
        return out;
    }
    CompatMemory compat_memory;
    SyscallDispatch dispatch(&compat_memory);
    GatewayContext context{&dispatch, image, static_cast<size_t>(end), stack.bytes.data(),
                           stack.bytes.size()};
    using Entry = int64_t (*)(void*, int64_t (*)(void*, int, const SyscallArgs*), const uint8_t*);
    auto entry = reinterpret_cast<Entry>(image + info.entry_point);
    out.entry_result = entry(&context, gateway, stack.bytes.data());
    out.executed = true;
    out.stop_requested = dispatch.stop_requested();
    out.note =
        "cooperative ELF entry returned; unmodified bionic/Android startup remains unsupported";
    return out;
#endif
}
} // namespace compat
} // namespace ps5droid
