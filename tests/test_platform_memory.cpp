// PS5Droid - platform memory tests.
#include "test_framework.h"

#include <cstring>
#include <vector>

#include "platform/platform.h"

using namespace ps5droid::platform;

PS5DROID_TEST(page_size_sane) {
    size_t ps = page_size();
    CHECK_MSG(ps >= 512 && ps <= 1024 * 1024, "page size out of range");
    CHECK_MSG((ps & (ps - 1)) == 0, "page size not a power of two");
}

PS5DROID_TEST(map_write_read_unmap) {
    size_t ps = page_size();
    uint8_t* p = static_cast<uint8_t*>(map_anonymous(ps * 2));
    CHECK_MSG(p != nullptr, "map_anonymous failed");

    // Fresh anonymous memory must be zero-filled.
    for (size_t i = 0; i < ps * 2; i += 4096) {
        if (p[i] != 0)
            CHECK_MSG(false, "mapped memory not zeroed");
    }

    memset(p, 0xAB, ps * 2);
    CHECK_MSG(p[0] == 0xAB && p[ps] == 0xAB, "write/readback failed");
    CHECK_MSG(unmap(p, ps * 2), "unmap failed");
}

PS5DROID_TEST(protect_roundtrip) {
    size_t ps = page_size();
    uint8_t* p = static_cast<uint8_t*>(map_anonymous(ps));
    CHECK_MSG(p != nullptr, "map failed");
    p[0] = 1;
    CHECK_MSG(protect_memory(p, ps, true, false), "R-only protect failed");
    CHECK_MSG(protect_memory(p, ps, true, true), "RW restore failed");
    p[0] = 2; // still writable after restore
    CHECK_MSG(p[0] == 2, "restore did not re-enable writes");
    CHECK_MSG(unmap(p, ps), "unmap failed");
}

PS5DROID_TEST(map_zero_size_fails) {
    CHECK_MSG(map_anonymous(0) == nullptr, "zero-size map must fail");
}
