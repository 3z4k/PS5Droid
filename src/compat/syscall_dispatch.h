// PS5Droid - Linux syscall dispatch experiment.
//
// WHAT THIS IS: an in-process translation layer that accepts Linux x86_64
// syscall numbers and executes a small, honestly classified subset on top
// of the platform layer. Supported operations have REAL behavior and are
// unit tested; everything else returns -ENOSYS with an explanation.
//
// WHAT THIS IS NOT: not a Linux binary loader, not process isolation, and
// not a claim that Android userspace can run. Pointer arguments are host
// pointers (single-process emulation model).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "compat/linux_abi.h"
#include "compat/memory_compat.h"

namespace ps5droid {
namespace compat {

struct SyscallArgs {
    uint64_t a[6];
};

struct DispatchResult {
    int64_t ret;      // syscall return value (negative => -errno)
    int err;          // linux errno when ret < 0, else 0
    bool supported;   // false => -ENOSYS was returned
    const char* note; // never nullptr; explains the outcome
};

class SyscallDispatch {
  public:
    explicit SyscallDispatch(CompatMemory* memory);
    ~SyscallDispatch() = default;
    SyscallDispatch(const SyscallDispatch&) = delete;
    SyscallDispatch& operator=(const SyscallDispatch&) = delete;

    DispatchResult invoke(int linux_nr, const SyscallArgs& args);

    // Support matrix snapshot (from linux_abi table + live handler state).
    struct MatrixEntry {
        int number;
        const char* name;
        linux_abi::Support support;
        const char* note;
    };
    std::vector<MatrixEntry> matrix() const;

    uint64_t context_id() const { return context_id_; }
    bool stop_requested() const { return stop_requested_; }
    CompatMemory* memory() { return memory_; }

  private:
    DispatchResult ok(int64_t ret, const char* note);
    DispatchResult fail(int err, const char* note);
    DispatchResult nosys(const char* note);

    CompatMemory* memory_;
    uint64_t context_id_;
    bool stop_requested_ = false;
};

} // namespace compat
} // namespace ps5droid
