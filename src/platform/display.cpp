// PS5Droid - host display + host input implementations (portable, testable).
#include "platform/display.h"

#include <cstring>
#include <vector>

namespace ps5droid {
namespace platform {

// ---------------------------------------------------------------------------
// Framebuffer
// ---------------------------------------------------------------------------

Framebuffer::Framebuffer(uint32_t width, uint32_t height)
    : width_(width), height_(height), owns_(true) {
    pixels_ = new Rgba[static_cast<size_t>(width) * height];
}

Framebuffer::~Framebuffer() {
    if (owns_)
        delete[] pixels_;
}

void Framebuffer::clear(Rgba color) {
    size_t n = static_cast<size_t>(width_) * height_;
    for (size_t i = 0; i < n; ++i)
        pixels_[i] = color;
}

bool Framebuffer::set_pixel(uint32_t x, uint32_t y, Rgba color) {
    if (x >= width_ || y >= height_ || pixels_ == nullptr)
        return false;
    pixels_[static_cast<size_t>(y) * width_ + x] = color;
    return true;
}

// ---------------------------------------------------------------------------
// Host display
// ---------------------------------------------------------------------------

namespace {

class HostDisplay : public DisplayBackend {
  public:
    explicit HostDisplay(uint32_t w, uint32_t h) : fb_(w, h), inited_(false) {}
    bool init(uint32_t* w, uint32_t* h) override {
        inited_ = true;
        if (w)
            *w = fb_.width();
        if (h)
            *h = fb_.height();
        return true;
    }
    Framebuffer* front_buffer() override { return &fb_; }
    bool present() override { return inited_; }
    void shutdown() override { inited_ = false; }
    const char* name() const override { return "host-memory"; }

  private:
    Framebuffer fb_;
    bool inited_;
};

} // namespace

DisplayBackend* create_host_display(uint32_t width, uint32_t height) {
    return new HostDisplay(width, height);
}

// ---------------------------------------------------------------------------
// Host input
// ---------------------------------------------------------------------------

bool HostInput::init() {
    return true;
}

bool HostInput::poll(PadState* out) {
    if (out == nullptr)
        return false;
    if (!has_state_)
        return false;
    *out = state_;
    return true;
}

void HostInput::shutdown() {
    has_state_ = false;
}

void HostInput::inject(const PadState& state) {
    state_ = state;
    state_.connected = true;
    has_state_ = true;
}

} // namespace platform
} // namespace ps5droid
