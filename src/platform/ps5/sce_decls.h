// PS5Droid - declarations of Orbis OS userland APIs used by the PS5 backend.
//
// PROVENANCE AND HONESTY NOTES (read before touching):
//
// PS5Droid does NOT ship proprietary Sony SDK headers, and ps5-payload-sdk
// does not ship struct definitions for SCE system libraries either. The
// established practice in the PS5 homebrew ecosystem is to declare the
// handful of firmware entry points a payload needs. Every declaration below
// mirrors verified public usage:
//
//   * Symbol existence of sceVideoOut*/scePad*/sceKernel* functions is
//     verified against ps5-payload-sdk sce_stubs (libSceVideoOut.c,
//     libScePad.c, libSceUserService.c, libSceSystemService.c).
//   * Signatures and the direct-memory allocation flow match the working
//     usage in ps5-payload-dev/SDL (src/video/ps5/SDL_ps5video.{h,c},
//     src/joystick/ps5/SDL_ps5joystick.{h,c}), zlib-licensed.
//   * Struct layouts (ScePadData, buffer attribute as opaque blob) are the
//     layouts used in production by the SDL port; the buffer attribute is
//     kept opaque on purpose.
//
// These declarations are only compiled inside PS5 toolchain builds
// (PS5DROID_PLATFORM_PS5). They have not yet been executed on a console;
// every caller reports this status. See docs/platform-constraints.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/event.h>
#include <sys/types.h>

#if defined(PS5DROID_PLATFORM_PS5)

namespace ps5droid {
namespace ps5 {

extern "C" {

// --- Kernel ----------------------------------------------------------------

// Direct (GPU-visible) memory allocation, as used by ps5-payload-dev/SDL:
// allocate from "main" memory with 0x20000 alignment, map RW into userland.
int sceKernelAllocateMainDirectMemory(size_t size, size_t alignment, int where,
                                      intptr_t* phys_addr_out);
int sceKernelMapDirectMemory(void** addr_out, size_t size, int prot, int flags, intptr_t phys_addr,
                             size_t alignment);
int sceKernelReleaseDirectMemory(intptr_t phys_addr, size_t size);

// Event queues are FreeBSD kqueue objects on Orbis (see sys/event.h in the
// ps5-payload-sdk freebsd headers).
int sceKernelCreateEqueue(struct kevent** queue_out, const char* name);
int sceKernelWaitEqueue(struct kevent* queue, struct kevent* events_out, int num_events,
                        int* count_out, unsigned timeout_ms);
int sceKernelDeleteEqueue(struct kevent* queue);

// System service: hide the splash screen once the payload is drawing.
int sceSystemServiceHideSplashScreen(void);

// Notification request (as used by the SDK hello_world sample).
typedef struct SceNotificationRequest {
    char reserved[45];
    char message[3075];
} SceNotificationRequest;
int sceKernelSendNotificationRequest(int unk, SceNotificationRequest* req, size_t size, int unk2);

// --- Video out ---------------------------------------------------------------

// The buffer attribute is kept opaque exactly like the SDL port does; the
// pixel format constant below is the A8R8G8B8 sRGB value used there.
typedef struct SceVideoOutBufferAttribute {
    uint8_t opaque[80];
} SceVideoOutBufferAttribute;

typedef struct SceVideoOutBuffer {
    void* data;
    uint64_t reserved[3];
} SceVideoOutBuffer;

static constexpr uint64_t kPixelFormatA8R8G8B8Srgb = 0x8000000022000000ull;

int sceVideoOutOpen(int user_id, int type, int index, const void* param);
void sceVideoOutClose(int handle);
int sceVideoOutAddFlipEvent(struct kevent* queue, int handle, void* user);
int sceVideoOutSetFlipRate(int handle, int rate);
int sceVideoOutSubmitFlip(int handle, int buffer_index, uint32_t flip_mode, int64_t flip_id);
int sceVideoOutDeleteFlipEvent(struct kevent* queue, int handle);
void sceVideoOutSetBufferAttribute2(SceVideoOutBufferAttribute* attribute, uint64_t pixel_format,
                                    uint32_t param1, uint32_t width, uint32_t height,
                                    uint64_t param2, uint32_t param3, uint64_t param4);
int sceVideoOutRegisterBuffers2(int handle, int start_index, int unk, SceVideoOutBuffer* buffers,
                                int num_buffers, SceVideoOutBufferAttribute* attribute, int flags,
                                void* unk2);

// --- Pad ---------------------------------------------------------------------

static constexpr int kPadPortTypeStandard = 0;
static constexpr int kUserIdSystem = 0xff; // SDL port: open as system user

static constexpr uint32_t kPadButtonL3 = 0x0002;
static constexpr uint32_t kPadButtonR3 = 0x0004;
static constexpr uint32_t kPadButtonOptions = 0x0008;
static constexpr uint32_t kPadButtonUp = 0x0010;
static constexpr uint32_t kPadButtonRight = 0x0020;
static constexpr uint32_t kPadButtonDown = 0x0040;
static constexpr uint32_t kPadButtonLeft = 0x0080;
static constexpr uint32_t kPadButtonL2 = 0x0100;
static constexpr uint32_t kPadButtonR2 = 0x0200;
static constexpr uint32_t kPadButtonL1 = 0x0400;
static constexpr uint32_t kPadButtonR1 = 0x0800;
static constexpr uint32_t kPadButtonTriangle = 0x1000;
static constexpr uint32_t kPadButtonCircle = 0x2000;
static constexpr uint32_t kPadButtonCross = 0x4000;
static constexpr uint32_t kPadButtonSquare = 0x8000;
static constexpr uint32_t kPadButtonTouchpad = 0x100000;

// Layout taken from ps5-payload-dev/SDL (SDL_ps5joystick.h), which works
// against current firmware through the ELF loader.
typedef struct ScePadTouch {
    uint16_t x;
    uint16_t y;
    uint8_t finger;
    uint8_t reserved[3];
} ScePadTouch;

typedef struct ScePadTouchData {
    uint8_t fingers;
    uint8_t reserved1[3];
    uint32_t reserved2;
    ScePadTouch touch[2];
} ScePadTouchData;

typedef struct ScePadData {
    uint32_t buttons;
    struct {
        uint8_t x;
        uint8_t y;
    } left_stick;
    struct {
        uint8_t x;
        uint8_t y;
    } right_stick;
    struct {
        uint8_t l2;
        uint8_t r2;
    } analog_buttons;
    uint16_t padding;
    struct {
        float x, y, z, w;
    } quaternion;
    struct {
        float x, y, z;
    } velocity;
    struct {
        float x, y, z;
    } acceleration;
    ScePadTouchData touch;
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t count;
    uint8_t unknown[15];
} ScePadData;

int scePadInit(void);
int scePadOpen(int user_id, int type, int index, void* param);
int scePadReadState(int handle, ScePadData* data);
int scePadClose(int handle);

} // extern "C"

} // namespace ps5
} // namespace ps5droid

#endif // PS5DROID_PLATFORM_PS5
