// PS5Droid - minimal test framework (no third-party dependencies).
#pragma once

#include <cstdio>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
    const char* name;
    void (*fn)();
    bool failed;
    std::string failure_msg;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

// Index of the currently executing test inside registry().
inline int& current_index() {
    static int i = -1;
    return i;
}

inline bool run_all() {
    size_t passed = 0;
    for (size_t i = 0; i < registry().size(); ++i) {
        TestCase& t = registry()[i];
        current_index() = static_cast<int>(i);
        t.failed = false;
        t.failure_msg.clear();
        t.fn();
        if (t.failed) {
            printf("[FAIL] %s: %s\n", t.name, t.failure_msg.c_str());
        } else {
            ++passed;
            printf("[ OK ] %s\n", t.name);
        }
    }
    printf("%zu/%zu tests passed\n", passed, registry().size());
    return passed == registry().size();
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back(TestCase{name, fn, false, ""});
    }
};

inline void fail_current(const std::string& msg) {
    int i = current_index();
    if (i < 0 || i >= static_cast<int>(registry().size()))
        return;
    registry()[static_cast<size_t>(i)].failed = true;
    registry()[static_cast<size_t>(i)].failure_msg = msg;
}

} // namespace testfw

#define PS5DROID_TEST(name)                                                                        \
    static void name##_impl();                                                                     \
    static testfw::Registrar name##_reg(#name, name##_impl);                                       \
    static void name##_impl()

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            testfw::fail_current(std::string("CHECK failed: ") + #cond + " at " + __FILE__ + ":" + \
                                 std::to_string(__LINE__));                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                       \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            testfw::fail_current(std::string(msg) + " (" #cond ") at " + __FILE__ + ":" +          \
                                 std::to_string(__LINE__));                                        \
            return;                                                                                \
        }                                                                                          \
    } while (0)
