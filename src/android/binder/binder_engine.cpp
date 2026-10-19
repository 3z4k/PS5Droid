// PS5Droid - Binder Re-Hosting Engine (implementation).
#include "android/binder/binder_engine.h"

#include <cstring>

namespace ps5droid {
namespace android {

BinderEngine::BinderEngine(size_t ring_capacity, size_t alloc_region_size)
    : ring_(ring_capacity), alloc_buf_(alloc_region_size, 0),
      allocator_(alloc_buf_.data(), alloc_region_size, 256) {
    // Handle 0 is implicitly reserved for ServiceManager
    name_to_handle_["servicemanager"] = 0;
}

uint32_t BinderEngine::register_service(const std::string& name, ServiceHandler handler,
                                        void* user) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = name_to_handle_.find(name);
    if (it != name_to_handle_.end() && it->second != 0) {
        return it->second;
    }

    uint32_t handle = next_handle_++;
    name_to_handle_[name] = handle;
    services_[handle] = ServiceEntry{name, handle, handler, user};
    return handle;
}

uint32_t BinderEngine::lookup_service(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = name_to_handle_.find(name);
    if (it != name_to_handle_.end()) {
        return it->second;
    }
    return 0; // 0 or invalid
}

bool BinderEngine::transact(uint32_t handle, uint32_t code, const std::vector<uint8_t>& in_data,
                            std::vector<uint8_t>* out_reply) {
    ServiceHandler handler = nullptr;
    void* user = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (handle == 0) {
            // ServiceManager built-in ping/lookup
            if (out_reply) {
                out_reply->assign({'S', 'M', 'O', 'K'});
            }
            return true;
        }

        auto it = services_.find(handle);
        if (it == services_.end()) {
            return false;
        }
        handler = it->second.handler;
        user = it->second.user;
    }

    if (!handler)
        return false;

    // Allocate transaction buffer via BlockAllocator
    void* block = allocator_.alloc();
    if (block) {
        size_t copy_len = std::min(in_data.size(), allocator_.block_size());
        if (copy_len > 0) {
            std::memcpy(block, in_data.data(), copy_len);
        }
    }

    // Execute in-process transaction
    std::vector<uint8_t> reply = handler(code, in_data, user);
    if (out_reply) {
        *out_reply = reply;
    }

    if (block) {
        allocator_.free(block);
    }

    // Push completion transaction signal through SpscRing
    ring_.push(static_cast<uint64_t>(BinderReturn::kBrReply));
    uint64_t dummy;
    ring_.pop(&dummy);

    return true;
}

size_t BinderEngine::registered_service_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return services_.size();
}

} // namespace android
} // namespace ps5droid
