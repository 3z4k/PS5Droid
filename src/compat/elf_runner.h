// Trusted cooperative ELF execution experiment. Not an unmodified bionic loader.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace ps5droid {
namespace compat {
struct ElfRunResult {
    bool executed = false;
    bool stop_requested = false;
    int64_t entry_result = 0;
    std::string note;
};
// Explicit opt-in only. Entry uses the C ABI documented in docs/elf-execution.md.
// Dynamic linking, relocations, TLS, raw syscalls and process isolation are absent.
ElfRunResult run_cooperative_elf(const void* file, size_t size);
bool probe_rx_memory(std::string* detail);
} // namespace compat
} // namespace ps5droid
