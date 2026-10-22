// PS5Droid - PS5 display backend (Orbis video-out over direct memory).
//
// API flow mirrors the working implementation in ps5-payload-dev/SDL:
//   1. open video-out as the system user,
//   2. allocate 64 MiB of aligned direct memory for two 1920x1080 x32bpp
//      buffers, map it read/write,
//   3. register buffers with an A8R8G8B8-sRGB attribute,
//   4. attach a flip event to a kqueue-based event queue,
//   5. per frame: draw into the back buffer, submit a vsync flip, wait for
//      the flip event, swap.
//
// STATUS: implemented against verified API usage (sce_stubs symbols +
// SDL port), but never executed on a console. Treat as experimental until
// a console run is documented. See docs/platform-constraints.md.
#include "platform/display.h"
#include "platform/platform.h"
#include "platform/ps5/sce_decls.h"

#include <cstring>
#include <sys/event.h>

namespace ps5droid {
namespace platform {
namespace {

// 1920x1080 x 4 bytes = 8,294,400 bytes; double buffered with generous
// slack, exactly matching the 0x4000000 / 0x20000 constants the SDL port
// uses.
constexpr size_t kBufferSize = 0x2000000; // 32 MiB per buffer
constexpr size_t kTotalSize = 0x4000000;  // 64 MiB
constexpr size_t kDirectAlign = 0x20000;
constexpr int kDirectMemoryWhereMain = 3;
constexpr int kMapProtReadWrite = 0x33; // as used by the SDL port
constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;

class Ps5Display : public DisplayBackend {
  public:
    ~Ps5Display() override { shutdown(); }

    bool init(uint32_t* w, uint32_t* h) override {
        int user_id = ps5::kUserIdSystem;
        int initial_user = -1;
        if (ps5::sceUserServiceGetInitialUser(&initial_user) == 0 && initial_user >= 0) {
            user_id = initial_user;
        }

        handle_ = ps5::sceVideoOutOpen(user_id, 0, 0, nullptr);
        if (handle_ < 0 && user_id != ps5::kUserIdSystem) {
            // Fallback to system user if initial user failed
            handle_ = ps5::sceVideoOutOpen(ps5::kUserIdSystem, 0, 0, nullptr);
        }
        if (handle_ < 0)
            return false;

        if (ps5::sceKernelAllocateMainDirectMemory(kTotalSize, kDirectAlign, kDirectMemoryWhereMain,
                                                   &phys_addr_) != 0) {
            shutdown();
            return false;
        }

        allocated_ = true;
        void* vaddr = nullptr;
        if (ps5::sceKernelMapDirectMemory(&vaddr, kTotalSize, kMapProtReadWrite, 0, phys_addr_,
                                          kDirectAlign) != 0) {
            shutdown();
            return false;
        }

        mapped_ = vaddr;
        buffers_[0].data = vaddr;
        buffers_[1].data = reinterpret_cast<uint8_t*>(vaddr) + kBufferSize;

        ps5::sceVideoOutSetBufferAttribute2(&attribute_, ps5::kPixelFormatA8R8G8B8Srgb, 0, kWidth,
                                            kHeight, 0, 0, 0);
        if (ps5::sceVideoOutRegisterBuffers2(handle_, 0, 0, buffers_, 2, &attribute_, 0, nullptr) !=
            0) {
            shutdown();
            return false;
        }

        if (ps5::sceKernelCreateEqueue(&event_queue_, "ps5droid-flip") != 0) {
            shutdown();
            return false;
        }
        if (ps5::sceVideoOutAddFlipEvent(event_queue_, handle_, nullptr) != 0) {
            shutdown();
            return false;
        }
        ps5::sceVideoOutSetFlipRate(handle_, 0); // vsync

        // The Framebuffer must view the *first* registered buffer; we wrap
        // host-side by drawing into our own buffer and copying? No - we
        // draw directly into buffer 0 to avoid a copy, exposing it as a
        // Framebuffer that does not own its pixels.
        frame_ =
            Framebuffer::wrap(reinterpret_cast<Rgba*>(buffers_[current_].data), kWidth, kHeight);
        if (w)
            *w = kWidth;
        if (h)
            *h = kHeight;
        inited_ = true;
        return true;
    }

    Framebuffer* front_buffer() override { return &frame_; }

    bool present() override {
        if (!inited_)
            return false;
        int64_t flip_id = ++flip_count_;
        if (ps5::sceVideoOutSubmitFlip(handle_, current_, 1 /*vsync*/, flip_id) != 0) {
            return false;
        }
        struct kevent evt;
        int count = 0;
        uint32_t timeout_us = 1000000;
        if (ps5::sceKernelWaitEqueue(event_queue_, &evt, 1, &count, &timeout_us) != 0 ||
            count != 1) {
            return false;
        }
        current_ ^= 1;
        frame_ =
            Framebuffer::wrap(reinterpret_cast<Rgba*>(buffers_[current_].data), kWidth, kHeight);
        return true;
    }

    void shutdown() override {
        if (event_queue_ != nullptr && handle_ >= 0) {
            ps5::sceVideoOutDeleteFlipEvent(event_queue_, handle_);
        }
        if (event_queue_ != nullptr) {
            ps5::sceKernelDeleteEqueue(event_queue_);
            event_queue_ = nullptr;
        }
        if (handle_ >= 0) {
            ps5::sceVideoOutClose(handle_);
            handle_ = -1;
        }
        frame_ = Framebuffer::wrap(nullptr, 0, 0);
        if (mapped_) {
            platform::unmap(mapped_, kTotalSize);
            mapped_ = nullptr;
        }
        if (allocated_) {
            ps5::sceKernelReleaseDirectMemory(phys_addr_, kTotalSize);
            phys_addr_ = 0;
            allocated_ = false;
        }
        inited_ = false;
    }

    const char* name() const override { return "ps5-videoout"; }

  private:
    int handle_ = -1;
    intptr_t phys_addr_ = 0;
    bool allocated_ = false;
    void* mapped_ = nullptr;
    struct kevent* event_queue_ = nullptr;
    ps5::SceVideoOutBuffer buffers_[2]{};
    ps5::SceVideoOutBufferAttribute attribute_{};
    Framebuffer frame_ = Framebuffer::wrap(nullptr, 0, 0);
    int current_ = 0;
    int64_t flip_count_ = 0;
    bool inited_ = false;
};

} // namespace

DisplayBackend* create_ps5_display() {
    return new Ps5Display();
}

} // namespace platform
} // namespace ps5droid
