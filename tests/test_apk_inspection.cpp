#include "android/image_manager.h"
#include "platform/platform.h"
#include "test_framework.h"
#include <string>
using namespace ps5droid::android;
#ifndef PS5DROID_TEST_FIXTURES
#define PS5DROID_TEST_FIXTURES "fixtures"
#endif
namespace {
std::string fixture(const std::string& name) {
    return std::string(PS5DROID_TEST_FIXTURES) + "/" + name;
}
} // namespace
PS5DROID_TEST(apk_aapt2_manifest_reports_real_metadata) {
    auto apk = inspect_apk(std::string(PS5DROID_SOURCE_FIXTURES) + "/aapt2-manifest.apk");
    CHECK_MSG(apk.is_apk, apk.failure_note);
    CHECK(apk.package_name == "org.ps5droid.inspection");
    CHECK(apk.min_sdk == 28 && apk.target_sdk == 35);
    CHECK(apk.has_manifest && !apk.has_native_libs && !apk.has_classes_dex);
}
PS5DROID_TEST(apk_stored_and_deflated_metadata_match) {
    for (const auto* name : {"stored.apk", "deflated.apk"}) {
        auto apk = inspect_apk(fixture(name));
        CHECK_MSG(apk.is_apk, apk.failure_note);
        CHECK(apk.package_name == "org.ps5droid.inspection");
        CHECK(apk.file_count == 5);
        CHECK(apk.dex_files == std::vector<std::string>({"classes.dex", "classes2.dex"}));
        CHECK(apk.native_abis == std::vector<std::string>({"x86_64"}));
    }
}
PS5DROID_TEST(apk_rejects_malformed_and_ambiguous_archives) {
    for (const auto* name :
         {"plain.zip", "bad-manifest.apk", "duplicate.apk", "large-manifest.apk", "bad-crc.apk",
          "truncated.apk", "bad-chunk.apk", "bad-pool.apk", "bad-attributes.apk"}) {
        auto apk = inspect_apk(fixture(name));
        CHECK_MSG(!apk.is_apk, name);
        CHECK(!apk.failure_note.empty());
    }
}
PS5DROID_TEST(ext4_rejects_unbounded_shift) {
    std::string path = "bad_ext4.img";
    std::vector<uint8_t> data(2048, 0);
    data[1024 + 0x38] = 0x53;
    data[1024 + 0x39] = 0xef;
    data[1024 + 0x18] = 0xff;
    CHECK(ps5droid::platform::write_file(path, data.data(), data.size()));
    auto result = probe_ext4(path);
    ps5droid::platform::remove_file(path);
    CHECK(!result.is_ext4 && !result.failure_note.empty());
}

PS5DROID_TEST(apk_utf8_and_utf16_string_pools_match) {
    for (const auto* name : {"utf8.apk", "utf16.apk"}) {
        auto apk = inspect_apk(fixture(name));
        CHECK_MSG(apk.is_apk, apk.failure_note);
        CHECK(apk.package_name == "org.ps5droid.encoding");
        CHECK(apk.min_sdk == 28 && apk.target_sdk == 35);
    }
}
