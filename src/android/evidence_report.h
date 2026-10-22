#pragma once
#include "android/runtime_manager.h"
#include <string>
#include <vector>
namespace ps5droid {
namespace android {
struct Observation {
    std::string name;
    std::string status; // passed, failed, not_run, or not_observed
    std::string detail;
};
struct EvidenceMetadata {
    std::string firmware = "unrecorded";
    std::string loader = "unrecorded";
};
std::string json_quote(const std::string& text);
// Unrecorded metadata and unexecuted checks stay explicit; host != console evidence.
std::string evidence_json(const CapabilityReport& report, const EvidenceMetadata& metadata,
                          const std::vector<Observation>& observations = {});
} // namespace android
} // namespace ps5droid
