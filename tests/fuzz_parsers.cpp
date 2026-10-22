#include "android/image_manager.h"
#include "compat/bionic_loader.h"
#include <cstddef>
#include <cstdint>
#include <string>
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    ps5droid::compat::BionicLoader::parse_header(data, size);
    ps5droid::android::ImageManifest manifest;
    ps5droid::android::parse_manifest(std::string(reinterpret_cast<const char*>(data), size),
                                      &manifest);
    return 0;
}
