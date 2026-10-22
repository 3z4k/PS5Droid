#include "android/binder/binder_engine.h"
#include <cstring>
#include <memory>

namespace ps5droid {
namespace android {
BinderEngine::BinderEngine(size_t max_transactions, size_t region)
    : storage_(region, 0), allocator_(storage_.data(), region, kMaxPayload),
      max_transactions_(max_transactions) {}
uint32_t BinderEngine::register_service(const std::string& name, ServiceHandler handler,
                                        void* user) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (name.empty() || name == "servicemanager" || !handler || next_handle_ == kInvalidHandle)
        return kInvalidHandle;
    auto found = names_.find(name);
    if (found != names_.end())
        return found->second;
    uint32_t handle = next_handle_++;
    names_[name] = handle;
    services_[handle] = {handler, user};
    return handle;
}
uint32_t BinderEngine::lookup_service(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = names_.find(name);
    return it == names_.end() ? kInvalidHandle : it->second;
}
RpcResult BinderEngine::transact_checked(uint32_t handle, uint32_t code,
                                         const std::vector<uint8_t>& input, uint32_t flags) {
    if (flags)
        return {RpcStatus::kUnsupportedFlags, {}};
    if (input.size() > kMaxPayload)
        return {RpcStatus::kTooLarge, {}};
    ServiceEntry service{};
    void* block;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = services_.find(handle);
        if (it == services_.end())
            return {RpcStatus::kInvalidHandle, {}};
        service = it->second;
        if (active_ >= max_transactions_ || !(block = allocator_.alloc()))
            return {RpcStatus::kNoMemory, {}};
        ++active_;
    }
    auto release = [this](void* p) {
        std::lock_guard<std::mutex> lock(mutex_);
        allocator_.free(p);
        --active_;
    };
    std::unique_ptr<void, decltype(release)> owned(block, release);
    try {
        if (!input.empty())
            std::memcpy(block, input.data(), input.size());
        const auto* first = static_cast<const uint8_t*>(block);
        std::vector<uint8_t> staged(first, first + input.size());
        // No engine lock is held across user code: nested calls are allowed.
        auto reply = service.handler(code, staged, service.user);
        if (reply.size() > kMaxPayload)
            return {RpcStatus::kTooLarge, {}};
        return {RpcStatus::kOk, std::move(reply)};
    } catch (...) {
        return {RpcStatus::kHandlerError, {}};
    }
}
bool BinderEngine::transact(uint32_t handle, uint32_t code, const std::vector<uint8_t>& input,
                            std::vector<uint8_t>* output) {
    if (output)
        output->clear();
    auto result = transact_checked(handle, code, input);
    if (output && result.ok())
        *output = std::move(result.reply);
    return result.ok();
}
size_t BinderEngine::registered_service_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.size();
}
} // namespace android
} // namespace ps5droid
