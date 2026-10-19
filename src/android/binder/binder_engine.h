// PS5Droid - Binder Re-Hosting Engine (In-Process RPC Engine).
//
// Milestone 5: Implementation of an in-process Binder RPC engine providing
// service manager handle mappings, transaction marshalling, object handle
// registration, and BC_TRANSACTION/BR_TRANSACTION message dispatch over
// SpscRing and BlockAllocator.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "compat/ipc_compat.h"

namespace ps5droid {
namespace android {

enum class BinderCommand : uint32_t {
    kBcTransaction = 1,
    kBcReply = 2,
    kBcAcquire = 3,
    kBcRelease = 4,
};

enum class BinderReturn : uint32_t {
    kBrTransaction = 101,
    kBrReply = 102,
    kBrError = 103,
    kBrOk = 104,
};

struct BinderTransaction {
    uint32_t target_handle;
    uint32_t code;
    uint32_t flags;
    std::vector<uint8_t> data;
};

using ServiceHandler = std::vector<uint8_t> (*)(uint32_t code, const std::vector<uint8_t>& in_data,
                                                void* user);

class BinderEngine {
  public:
    BinderEngine(size_t ring_capacity = 2048, size_t alloc_region_size = 65536);
    ~BinderEngine() = default;

    uint32_t register_service(const std::string& name, ServiceHandler handler,
                              void* user = nullptr);
    uint32_t lookup_service(const std::string& name) const;

    bool transact(uint32_t handle, uint32_t code, const std::vector<uint8_t>& in_data,
                  std::vector<uint8_t>* out_reply);

    size_t registered_service_count() const;

  private:
    mutable std::mutex mutex_;
    uint32_t next_handle_ = 1;

    struct ServiceEntry {
        std::string name;
        uint32_t handle;
        ServiceHandler handler;
        void* user;
    };

    std::unordered_map<std::string, uint32_t> name_to_handle_;
    std::unordered_map<uint32_t, ServiceEntry> services_;

    compat::SpscRing ring_;
    std::vector<uint8_t> alloc_buf_;
    compat::BlockAllocator allocator_;
};

} // namespace android
} // namespace ps5droid
