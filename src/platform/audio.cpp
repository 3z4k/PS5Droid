// PS5Droid - host audio backend. Deliberately reports unsupported.
#include "platform/audio.h"

namespace ps5droid {
namespace platform {

namespace {

class HostAudio : public AudioBackend {
  public:
    AudioStatus open_output(uint32_t /*sample_rate_hz*/, uint32_t /*channels*/) override {
        return AudioStatus::kUnsupported;
    }
    const char* unsupported_reason() const override {
        return "audio output is not implemented on any PS5Droid backend; "
               "PS5 path requires a libSceAudioOut design that does not "
               "exist yet";
    }
    const char* name() const override { return "host-null"; }
};

} // namespace

AudioBackend* create_host_audio() {
    return new HostAudio();
}

} // namespace platform
} // namespace ps5droid
