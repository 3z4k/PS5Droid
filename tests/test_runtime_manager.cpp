// PS5Droid - runtime manager tests (probes + honest blocking).
#include "test_framework.h"

#include "android/runtime_manager.h"

using ps5droid::android::RuntimeManager;
using ps5droid::android::RuntimePhase;

PS5DROID_TEST(probe_all_capabilities_pass) {
    RuntimeManager m;
    auto report = m.probe();
    for (const auto& r : report.results) {
        char msg[128];
        snprintf(msg, sizeof(msg), "capability %s failed: %s", r.name.c_str(), r.detail.c_str());
        CHECK_MSG(r.passed, msg);
    }
    CHECK_MSG(report.results.size() >= 8, "too few probes");
    CHECK_MSG(report.all_passed, "aggregate must pass");
    CHECK_MSG(m.phase() == RuntimePhase::kPlatformVerified, "phase must advance");
}

PS5DROID_TEST(start_attempt_never_claims_running) {
    RuntimeManager m;
    m.probe();
    auto attempt = m.attempt_start();
    CHECK_MSG(attempt.phase_after == RuntimePhase::kBlocked, "start attempt must be blocked");
    CHECK_MSG(m.phase() == RuntimePhase::kBlocked, "manager phase blocked");
    CHECK_MSG(attempt.blockers.size() >= 5, "blocker set too small");

    bool has_process = false, has_binder = false, has_fs = false;
    for (const auto& b : attempt.blockers) {
        has_process |= b.component == "process_model";
        has_binder |= b.component == "binder";
        has_fs |= b.component == "filesystem_layout";
        CHECK_MSG(!b.reason.empty(), "blocker needs a reason");
    }
    CHECK_MSG(has_process, "process_model blocker required");
    CHECK_MSG(has_binder, "binder blocker required");
    CHECK_MSG(has_fs, "filesystem_layout blocker required");
}

PS5DROID_TEST(phase_is_never_running) {
    RuntimeManager m;
    CHECK_MSG(m.phase() == RuntimePhase::kNotInitialized, "init phase");
    m.probe();
    CHECK_MSG(m.phase() != RuntimePhase::kRunning, "probes != running");
    m.attempt_start();
    CHECK_MSG(m.phase() != RuntimePhase::kRunning, "HONESTY: runtime must never report Running");
}
