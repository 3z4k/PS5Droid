// PS5Droid - Bionic Loader & TLS/Auxv Feasibility Inspector.
//
// Milestone 4: Inspection of x86_64 ELF Bionic binaries (headers, PT_LOAD,
// PT_TLS, PT_INTERP, PT_DYNAMIC) and construction of Linux/Bionic initial
// stack and auxiliary vector (auxv) structures.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps5droid {
namespace compat {

struct Elf64PhdrInfo {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

struct BionicElfInfo {
    bool valid = false;
    uint64_t entry_point = 0;
    uint64_t phdr_offset = 0;
    uint16_t elf_type = 0;
    std::string interp;
    bool is_bionic = false;
    uint64_t tls_filesz = 0;
    uint64_t tls_memsz = 0;
    uint64_t tls_align = 0;
    std::vector<Elf64PhdrInfo> phdrs;
    std::string error;
};

struct AuxvEntry {
    uint64_t type;
    uint64_t val;
};

struct InitialStack {
    std::vector<uint8_t> bytes;
    uint64_t stack_pointer = 0;
    std::string error;
};

class BionicLoader {
  public:
    static BionicElfInfo parse_header(const void* data, size_t size);
    static std::vector<AuxvEntry> build_auxv(uint64_t entry, uint64_t phdr, uint64_t phnum,
                                             uint64_t pagesz = 4096);
    // base=0 uses the returned vector's actual allocation; explicit bases stage guest addresses.
    // This minimal auxv is for the cooperative fixture, not a complete bionic startup contract.
    static InitialStack build_initial_stack(const BionicElfInfo& info,
                                            const std::vector<std::string>& args,
                                            const std::vector<std::string>& env,
                                            uint64_t guest_base, uint64_t phdr_address,
                                            uint64_t load_bias, uint64_t pagesz);
    static std::vector<uint8_t> setup_initial_stack(const BionicElfInfo& info,
                                                    const std::vector<std::string>& args,
                                                    const std::vector<std::string>& env);
};

} // namespace compat
} // namespace ps5droid
