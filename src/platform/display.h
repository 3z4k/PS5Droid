// PS5Droid - display and input abstractions.
//
// These interfaces let the launcher UI and the PS5 entry point share one
// code path. Implementations:
//   * host   : Framebuffer is plain host memory; present() is a no-op that
//              exists so the UI logic is unit-testable end to end.
//   * ps5    : sceVideoOut + scePad implementations live in
//              src/platform/ps5/display_ps5.cpp and input_ps5.cpp and are
//              only compiled inside ps5-payload-sdk builds.
//
// Status: the PS5 display path is written against API usage verified in
// ps5-payload-dev/sdk sce_stubs and the ps5-payload-dev/SDL port, but has
// NOT been executed on a console. It is classified "implemented, not
// tested on console" everywhere in this repository.
#pragma once

#include <cstdint>
#include <string>

namespace ps5droid {
namespace platform {

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

struct Rgba {
    uint8_t r, g, b, a;
};

class Framebuffer {
  public:
    Framebuffer(uint32_t width, uint32_t height); // owning
    ~Framebuffer();
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // Non-owning view over externally owned pixels (e.g. a registered
    // video-out buffer on PS5). Copies share nothing; lifetime of `pixels`
    // is managed by the caller.
    static Framebuffer wrap(Rgba* pixels, uint32_t width, uint32_t height) {
        Framebuffer fb;
        fb.pixels_ = pixels;
        fb.width_ = width;
        fb.height_ = height;
        fb.owns_ = false;
        return fb;
    }

    Framebuffer(Framebuffer&& other) noexcept { move_from(other); }
    Framebuffer& operator=(Framebuffer&& other) noexcept {
        if (this != &other) {
            if (owns_)
                delete[] pixels_;
            move_from(other);
        }
        return *this;
    }

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t stride_pixels() const { return width_; } // tightly packed
    Rgba* pixels() { return pixels_; }
    const Rgba* pixels() const { return pixels_; }

    void clear(Rgba color);
    // Blend-free pixel write; returns false when out of bounds.
    bool set_pixel(uint32_t x, uint32_t y, Rgba color);

  private:
    Framebuffer() = default;
    void move_from(Framebuffer& other) {
        width_ = other.width_;
        height_ = other.height_;
        pixels_ = other.pixels_;
        owns_ = other.owns_;
        other.pixels_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
        other.owns_ = false;
    }
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    Rgba* pixels_ = nullptr;
    bool owns_ = false;
};

class DisplayBackend {
  public:
    virtual ~DisplayBackend() = default;
    // Initializes the display; returns false and leaves *w/*h untouched on
    // failure. On success *w/*h receive the selected resolution.
    virtual bool init(uint32_t* w, uint32_t* h) = 0;
    virtual Framebuffer* front_buffer() = 0;
    // Presents the front buffer (flip on PS5; no-op on host).
    virtual bool present() = 0;
    virtual void shutdown() = 0;
    virtual const char* name() const = 0;
};

// In-memory display for host builds and tests. present() always succeeds.
DisplayBackend* create_host_display(uint32_t width, uint32_t height);

// PS5 display backend (only linked in PS5 builds; declared here so app code
// can reference it unconditionally and CMake chooses the implementation).
DisplayBackend* create_ps5_display();

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

struct PadState {
    bool connected = false;
    bool up = false, down = false, left = false, right = false;
    bool cross = false, circle = false, square = false, triangle = false;
    bool options = false, share = false, ps_button = false;
    bool l1 = false, r1 = false, l2 = false, r2 = false;
    bool l3 = false, r3 = false, touchpad = false;
    uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80;
    uint8_t l2_value = 0, r2_value = 0;
};

class InputBackend {
  public:
    virtual ~InputBackend() = default;
    virtual bool init() = 0;
    // Polls the current pad state; false means no input available.
    virtual bool poll(PadState* out) = 0;
    virtual void shutdown() = 0;
    virtual const char* name() const = 0;
};

// Host input backend: state is injected programmatically (tests and host
// simulation). poll() returns false until inject() has been called.
class HostInput : public InputBackend {
  public:
    bool init() override;
    bool poll(PadState* out) override;
    void shutdown() override;
    const char* name() const override { return "host-synthetic"; }
    void inject(const PadState& state);

  private:
    bool has_state_ = false;
    PadState state_;
};

// PS5 pad backend (only linked in PS5 builds).
InputBackend* create_ps5_input();

} // namespace platform
} // namespace ps5droid
