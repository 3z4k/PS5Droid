// Bounded ELF inspection and address-correct initial stack staging.
#include "compat/bionic_loader.h"
#include <algorithm>
#include <cstring>
#include <limits>

namespace ps5droid {
namespace compat {
namespace {
struct Header {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct Phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
static_assert(sizeof(Header) == 64 && sizeof(Phdr) == 56, "ELF64 layouts");
bool range(uint64_t offset, uint64_t length, uint64_t size) {
    return offset <= size && length <= size - offset;
}
} // namespace
BionicElfInfo BionicLoader::parse_header(const void* data, size_t size) {
    BionicElfInfo out;
    auto fail = [&](const char* note) {
        out.valid = false;
        out.error = note;
        return out;
    };
    if (!data || size < sizeof(Header))
        return fail("truncated ELF header");
    Header header{};
    std::memcpy(&header, data, sizeof(header));
    if (std::memcmp(header.ident, "\177ELF", 4))
        return fail("invalid ELF magic");
    if (header.ident[4] != 2 || header.ident[5] != 1 || header.ident[6] != 1 ||
        header.machine != 62 || header.version != 1)
        return fail("requires little-endian ELF64 x86_64 version 1");
    if ((header.type != 2 && header.type != 3) || header.ehsize != sizeof(Header) ||
        header.phentsize != sizeof(Phdr) || !header.phnum || header.phnum > 1024)
        return fail("invalid ELF type or program header layout");
    if (!range(header.phoff, uint64_t(header.phnum) * sizeof(Phdr), size))
        return fail("program headers exceed file buffer");
    out.entry_point = header.entry;
    out.phdr_offset = header.phoff;
    out.elf_type = header.type;
    const auto* bytes = static_cast<const uint8_t*>(data);
    bool entry_mapped = false;
    for (uint16_t i = 0; i < header.phnum; ++i) {
        Phdr p{};
        std::memcpy(&p, bytes + header.phoff + i * sizeof(p), sizeof(p));
        if (!range(p.offset, p.filesz, size) || p.memsz < p.filesz ||
            p.memsz > UINT64_MAX - p.vaddr)
            return fail("invalid segment range");
        if (p.align > 1 && ((p.align & (p.align - 1)) || p.vaddr % p.align != p.offset % p.align))
            return fail("invalid segment alignment");
        out.phdrs.push_back(
            {p.type, p.flags, p.offset, p.vaddr, p.paddr, p.filesz, p.memsz, p.align});
        if (p.type == 1 && (p.flags & 1) && header.entry >= p.vaddr &&
            header.entry - p.vaddr < p.memsz)
            entry_mapped = true;
        if (p.type == 3) {
            if (!out.interp.empty() || p.filesz < 2 || p.filesz > 4096 ||
                bytes[p.offset + p.filesz - 1] != 0)
                return fail("invalid PT_INTERP string");
            out.interp.assign(reinterpret_cast<const char*>(bytes + p.offset), p.filesz - 1);
            if (out.interp.find('\0') != std::string::npos)
                return fail("embedded NUL in interpreter");
            out.is_bionic = out.interp == "/system/bin/linker64" ||
                            out.interp == "/apex/com.android.runtime/bin/linker64";
        } else if (p.type == 7) {
            out.tls_filesz = p.filesz;
            out.tls_memsz = p.memsz;
            out.tls_align = p.align;
        }
    }
    if (!entry_mapped)
        return fail("entry point is outside executable PT_LOAD segments");
    out.valid = true;
    return out;
}
std::vector<AuxvEntry> BionicLoader::build_auxv(uint64_t entry, uint64_t phdr, uint64_t phnum,
                                                uint64_t pagesz) {
    return {{3, phdr}, {4, 56}, {5, phnum}, {6, pagesz}, {9, entry}, {0, 0}};
}
InitialStack BionicLoader::build_initial_stack(const BionicElfInfo& info,
                                               const std::vector<std::string>& args,
                                               const std::vector<std::string>& env,
                                               uint64_t guest_base, uint64_t phdr_address,
                                               uint64_t load_bias, uint64_t pagesz) {
    InitialStack out;
    if (!info.valid || !pagesz || (pagesz & (pagesz - 1)) ||
        load_bias > UINT64_MAX - info.entry_point) {
        out.error = "invalid ELF or stack addresses";
        return out;
    }
    constexpr size_t limit = 1024 * 1024;
    if (args.size() > 4096 || env.size() > 4096) {
        out.error = "argument count exceeds limit";
        return out;
    }
    auto auxv = build_auxv(load_bias + info.entry_point, phdr_address, info.phdrs.size(), pagesz);
    size_t total = (1 + args.size() + 1 + env.size() + 1 + auxv.size() * 2) * 8;
    const size_t strings_begin = total;
    for (const auto* list : {&args, &env})
        for (const auto& text : *list) {
            if (text.find('\0') != std::string::npos || text.size() >= limit - total) {
                out.error = "invalid or oversized argument data";
                return out;
            }
            total += text.size() + 1;
        }
    total = (total + 15) & ~size_t(15);
    out.bytes.resize(total, 0);
    uint64_t base = guest_base ? guest_base : reinterpret_cast<uint64_t>(out.bytes.data());
    if (base % 16 || base > UINT64_MAX - total) {
        out.bytes.clear();
        out.error = "stack base must be aligned and non-overflowing";
        return out;
    }
    out.stack_pointer = base;
    size_t word = 0, string_pos = strings_begin;
    auto push = [&](uint64_t value) {
        std::memcpy(out.bytes.data() + word, &value, 8);
        word += 8;
    };
    push(args.size());
    for (const auto* list : {&args, &env}) {
        for (const auto& text : *list) {
            push(base + string_pos);
            std::memcpy(out.bytes.data() + string_pos, text.c_str(), text.size() + 1);
            string_pos += text.size() + 1;
        }
        push(0);
    }
    for (auto a : auxv) {
        push(a.type);
        push(a.val);
    }
    return out;
}
std::vector<uint8_t> BionicLoader::setup_initial_stack(const BionicElfInfo& info,
                                                       const std::vector<std::string>& args,
                                                       const std::vector<std::string>& env) {
    return build_initial_stack(info, args, env, 0, 0, 0, 4096).bytes;
}
} // namespace compat
} // namespace ps5droid
