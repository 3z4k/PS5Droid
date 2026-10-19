// PS5Droid - Milestone 5 & 6 unit tests.
#include "test_framework.h"

#include "android/binder/binder_engine.h"
#include "android/image_manager.h"
#include "platform/platform.h"

using namespace ps5droid::android;

namespace {
std::vector<uint8_t> echo_handler(uint32_t code, const std::vector<uint8_t>& in_data, void*) {
    (void)code;
    return in_data;
}
} // namespace

PS5DROID_TEST(binder_engine_registration_and_tx) {
    BinderEngine engine;
    uint32_t h = engine.register_service("test_service", echo_handler);
    CHECK_MSG(h > 0, "service registration failed");
    CHECK_MSG(engine.lookup_service("test_service") == h, "lookup failed");

    std::vector<uint8_t> in = {'P', 'S', '5'};
    std::vector<uint8_t> out;
    bool ok = engine.transact(h, 1, in, &out);
    CHECK_MSG(ok, "transaction failed");
    CHECK_MSG(out == in, "reply mismatch");
}

PS5DROID_TEST(apk_inspection_basic) {
    std::string path = "test_app.apk";
    const uint8_t zip_hdr[30] = {'P', 'K', 0x03, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0,   0,   0,    0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    ps5droid::platform::write_file(path, zip_hdr, sizeof(zip_hdr));

    ApkInspection apk = inspect_apk(path);
    CHECK_MSG(apk.is_apk, "APK detection failed");

    ps5droid::platform::remove_file(path);
}
