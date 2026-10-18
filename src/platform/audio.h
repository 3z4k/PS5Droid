// PS5Droid - audio abstraction (explicitly unimplemented backend).
//
// PS5Droid does not currently produce audio on any platform. This interface
// exists so that the runtime manager can *report* audio capability honestly
// instead of pretending. The host implementation returns Unsupported with a
// reason; a future PS5 implementation would go through libSceAudioOut (stub
// symbols verified in ps5-payload-dev/sdk sce_stubs/libSceAudioOut.c) and
// must be separately designed and tested.
#pragma once

#include <cstdint>
#include <string>

namespace ps5droid {
namespace platform {

enum class AudioStatus {
    kUnsupported,
    kOk, // reserved for a future working implementation
};

class AudioBackend {
  public:
    virtual ~AudioBackend() = default;
    virtual AudioStatus open_output(uint32_t sample_rate_hz, uint32_t channels) = 0;
    virtual const char* unsupported_reason() const = 0;
    virtual const char* name() const = 0;
};

AudioBackend* create_host_audio();

} // namespace platform
} // namespace ps5droid
