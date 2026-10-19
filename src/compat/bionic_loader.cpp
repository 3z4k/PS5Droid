// PS5Droid - Bionic Loader & TLS/Auxv implementation.
#include "compat/bionic_loader.h"

#include <cstring>

namespace ps5droid {
namespace compat {

namespace {

struct Elf64Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct RawElf64Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

constexpr uint32_t PT_INTERP = 3;
constexpr uint32_t PT_TLS = 7;

constexpr uint64_t AT_NULL = 0;
constexpr uint64_t AT_PHDR = 3;
constexpr uint64_t AT_PHENT = 4;
constexpr uint64_t AT_PHNUM = 5;
constexpr uint64_t AT_PAGESZ = 6;
constexpr uint64_t AT_ENTRY = 9;

} // namespace

BionicElfInfo BionicLoader::parse_header(const void* data, size_t size) {
    BionicElfInfo res;
    if (!data || size < sizeof(Elf64Ehdr)) {
        res.error = "buffer too small for ELF header";
        return res;
    }

    const auto* ehdr = static_cast<const Elf64Ehdr*>(data);
    if (ehdr->e_ident[0] != 0x7f || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F') {
        res.error = "invalid ELF magic";
        return res;
    }

    if (ehdr->e_ident[4] != 2 /* 64-bit */ || ehdr->e_machine != 62 /* x86_64 */) {
        res.error = "not ELF64 x86_64";
        return res;
    }

    res.valid = true;
    res.entry_point = ehdr->e_entry;

    if (ehdr->e_phoff + static_cast<uint64_t>(ehdr->e_phnum) * sizeof(RawElf64Phdr) > size) {
        res.error = "program headers exceed file buffer";
        res.valid = false;
        return res;
    }

    const auto* phdrs =
        reinterpret_cast<const RawElf64Phdr*>(static_cast<const uint8_t*>(data) + ehdr->e_phoff);

    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        Elf64PhdrInfo info;
        info.type = phdrs[i].p_type;
        info.flags = phdrs[i].p_flags;
        info.offset = phdrs[i].p_offset;
        info.vaddr = phdrs[i].p_vaddr;
        info.paddr = phdrs[i].p_paddr;
        info.filesz = phdrs[i].p_filesz;
        info.memsz = phdrs[i].p_memsz;
        info.align = phdrs[i].p_align;
        res.phdrs.push_back(info);

        if (phdrs[i].p_type == PT_INTERP) {
            if (phdrs[i].p_offset + phdrs[i].p_filesz <= size) {
                const char* interp_str = reinterpret_cast<const char*>(
                    static_cast<const uint8_t*>(data) + phdrs[i].p_offset);
                res.interp.assign(interp_str, phdrs[i].p_filesz);
                if (!res.interp.empty() && res.interp.back() == '\0') {
                    res.interp.pop_back();
                }
                if (res.interp.find("linker") != std::string::npos) {
                    res.is_bionic = true;
                }
            }
        } else if (phdrs[i].p_type == PT_TLS) {
            res.tls_filesz = phdrs[i].p_filesz;
            res.tls_memsz = phdrs[i].p_memsz;
            res.tls_align = phdrs[i].p_align;
        }
    }

    return res;
}

std::vector<AuxvEntry> BionicLoader::build_auxv(uint64_t entry, uint64_t phdr, uint64_t phnum,
                                                uint64_t pagesz) {
    std::vector<AuxvEntry> auxv;
    auxv.push_back({AT_PHDR, phdr});
    auxv.push_back({AT_PHENT, sizeof(RawElf64Phdr)});
    auxv.push_back({AT_PHNUM, phnum});
    auxv.push_back({AT_PAGESZ, pagesz});
    auxv.push_back({AT_ENTRY, entry});
    auxv.push_back({AT_NULL, 0});
    return auxv;
}

std::vector<uint8_t> BionicLoader::setup_initial_stack(const BionicElfInfo& info,
                                                       const std::vector<std::string>& args,
                                                       const std::vector<std::string>& env) {
    std::vector<uint8_t> stack;
    auto auxv = build_auxv(info.entry_point, 0x400000, info.phdrs.size());

    // Basic layout simulation: argc, argv pointers, env pointers, auxv pairs
    uint64_t argc = args.size();
    stack.resize(4096, 0);
    uint64_t* ptr = reinterpret_cast<uint64_t*>(stack.data());

    *ptr++ = argc;
    for (size_t i = 0; i < args.size(); ++i) {
        *ptr++ = 0x7fffffffe000 + i * 32;
    }
    *ptr++ = 0; // NULL argv terminator

    for (size_t i = 0; i < env.size(); ++i) {
        *ptr++ = 0x7fffffffe800 + i * 32;
    }
    *ptr++ = 0; // NULL env terminator

    for (const auto& a : auxv) {
        *ptr++ = a.type;
        *ptr++ = a.val;
    }

    return stack;
}

} // namespace compat
} // namespace ps5droid
