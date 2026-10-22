#include "android/evidence_report.h"
#include "build_info.h"
#include <cstdio>
namespace ps5droid {
namespace android {
std::string json_quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char ch : text) {
        if (ch == '\\' || ch == '"') {
            out += '\\';
            out += static_cast<char>(ch);
        } else if (ch < 0x20) {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
            out += escaped;
        } else
            out += static_cast<char>(ch);
    }
    return out + '"';
}
std::string evidence_json(const CapabilityReport& report, const EvidenceMetadata& metadata,
                          const std::vector<Observation>& observations) {
    std::string out = "{\n  \"schema_version\": 1,\n  \"backend\": " + json_quote(report.backend) +
                      ",\n  \"revision\": " + json_quote(PS5DROID_BUILD_REVISION) +
                      ",\n  \"sdk\": " + json_quote(PS5DROID_SDK_VERSION) +
                      ",\n  \"firmware\": " + json_quote(metadata.firmware) +
                      ",\n  \"loader\": " + json_quote(metadata.loader) +
                      ",\n  \"android_runtime\": \"blocked\",\n  \"platform_probes_passed\": " +
                      (report.all_passed ? "true" : "false") + ",\n  \"probes\": [";
    bool first = true;
    for (const auto& probe : report.results) {
        if (!first)
            out += ',';
        first = false;
        out += "\n    {\"name\": " + json_quote(probe.name) +
               ", \"passed\": " + (probe.passed ? "true" : "false") +
               ", \"detail\": " + json_quote(probe.detail) + '}';
    }
    out += "\n  ],\n  \"observations\": [";
    first = true;
    for (const auto& observation : observations) {
        if (!first)
            out += ',';
        first = false;
        out += "\n    {\"name\": " + json_quote(observation.name) +
               ", \"status\": " + json_quote(observation.status) +
               ", \"detail\": " + json_quote(observation.detail) + '}';
    }
    return out + "\n  ]\n}\n";
}
} // namespace android
} // namespace ps5droid
