// Binder research: bounded synchronous IN-PROCESS RPC, not Android Binder IPC.
// No ioctl/BC_*/BR_* wire protocol, Parcel/object transfer, death notifications,
// one-way calls or Android ServiceManager is implemented. See docs/binder-scope.md.
#pragma once
#include "compat/ipc_compat.h"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ps5droid {
namespace android {
using ServiceHandler = std::vector<uint8_t> (*)(uint32_t, const std::vector<uint8_t>&, void*);
enum class RpcStatus {
    kOk,
    kInvalidHandle,
    kUnsupportedFlags,
    kTooLarge,
    kNoMemory,
    kHandlerError
};
struct RpcResult {
    RpcStatus status = RpcStatus::kInvalidHandle;
    std::vector<uint8_t> reply;
    bool ok() const { return status == RpcStatus::kOk; }
};
class BinderEngine {
  public:
    static constexpr uint32_t kInvalidHandle = UINT32_MAX;
    static constexpr size_t kMaxPayload = 256;
    explicit BinderEngine(size_t max_transactions = 2048, size_t alloc_region_size = 65536);
    // The caller owns user data and must keep it alive until all calls finish.
    uint32_t register_service(const std::string& name, ServiceHandler handler,
                              void* user = nullptr);
    uint32_t lookup_service(const std::string& name) const;
    RpcResult transact_checked(uint32_t handle, uint32_t code, const std::vector<uint8_t>& input,
                               uint32_t flags = 0);
    bool transact(uint32_t handle, uint32_t code, const std::vector<uint8_t>& input,
                  std::vector<uint8_t>* output);
    size_t registered_service_count() const;

  private:
    struct ServiceEntry {
        ServiceHandler handler;
        void* user;
    };
    mutable std::mutex mutex_;
    uint32_t next_handle_ = 1; // 0 remains reserved; it has no success stub.
    std::unordered_map<std::string, uint32_t> names_;
    std::unordered_map<uint32_t, ServiceEntry> services_;
    std::vector<uint8_t> storage_;
    compat::BlockAllocator allocator_;
    size_t max_transactions_;
    size_t active_ = 0;
};
} // namespace android
} // namespace ps5droid
