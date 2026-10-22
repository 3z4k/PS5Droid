// PS5Droid - runtime manager: capability probing and honest start attempts.
//
// The runtime manager owns the startup state machine and is the single
// place that reports what works and what does not. It can never claim a
// running Android environment unless every gate passes; today every gate
// reports structured blockers, and that is the correct behavior.
#pragma once

#include <string>
#include <vector>

namespace ps5droid {
namespace android {

enum class RuntimePhase {
    kNotInitialized,
    kProbing,
    kPlatformVerified, // capability probes all passed
    kCompatReady,      // Linux ABI dispatch constructed and self-checked
    kStarting,         // an Android environment start was attempted
    kBlocked,          // start attempted; structured blockers present
    kRunning,          // Android environment live (never reached today)
};

struct CapabilityResult {
    std::string name;
    bool passed;
    std::string detail;
};

struct CapabilityReport {
    std::vector<CapabilityResult> results;
    bool all_passed = false;
    const char* backend = "";
};

struct Blocker {
    std::string component;
    std::string reason;
};

struct StartAttempt {
    RuntimePhase phase_after = RuntimePhase::kBlocked;
    std::vector<Blocker> blockers;
};

class RuntimeManager {
  public:
    RuntimeManager() = default;

    // Runs real, in-process capability probes (memory, threads, futex,
    // filesystem, IPC). Every result reflects an executed test.
    CapabilityReport probe(const std::string& writable_dir = ".");

    // Performs an honest Android-environment start attempt. On every
    // current platform this returns kBlocked with the concrete reasons.
    StartAttempt attempt_start();

    RuntimePhase phase() const { return phase_; }
    const std::vector<Blocker>& last_blockers() const { return last_blockers_; }

  private:
    RuntimePhase phase_ = RuntimePhase::kNotInitialized;
    std::vector<Blocker> last_blockers_;
};

} // namespace android
} // namespace ps5droid
