#pragma once
#include <cstdint>
#include <vector>
namespace fixture {
inline void put(std::vector<uint8_t>& data, size_t at, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i)
        data[at + i] = static_cast<uint8_t>(value >> (i * 8));
}
inline std::vector<uint8_t> elf() {
    std::vector<uint8_t> out(256, 0);
    out[0] = 0x7f;
    out[1] = 'E';
    out[2] = 'L';
    out[3] = 'F';
    out[4] = 2;
    out[5] = 1;
    out[6] = 1;
    put(out, 16, 3, 2);
    put(out, 18, 62, 2);
    put(out, 20, 1, 4);
    put(out, 24, 128, 8);
    put(out, 32, 64, 8);
    put(out, 52, 64, 2);
    put(out, 54, 56, 2);
    put(out, 56, 1, 2);
    put(out, 64, 1, 4);
    put(out, 68, 5, 4);
    put(out, 96, 256, 8);
    put(out, 104, 256, 8);
    put(out, 112, 4096, 8);
    // xor eax,eax; ret -- used only by inspection tests.
    out[128] = 0x31;
    out[129] = 0xc0;
    out[130] = 0xc3;
    return out;
}
} // namespace fixture
