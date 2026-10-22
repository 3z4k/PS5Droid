#include "android/evidence_report.h"
#include "test_framework.h"
PS5DROID_TEST(evidence_json_escapes_control_characters_and_preserves_unknowns) {
    using namespace ps5droid::android;
    CHECK(json_quote("a\"b\\c\n\t") == "\"a\\\"b\\\\c\\u000a\\u0009\"");
    CapabilityReport report;
    report.backend = "posix";
    auto text = evidence_json(report, {});
    CHECK(text.find("\"android_runtime\": \"blocked\"") != std::string::npos);
    CHECK(text.find("\"firmware\": \"unrecorded\"") != std::string::npos);
    CHECK(text.find("\"platform_probes_passed\": false") != std::string::npos);
}
