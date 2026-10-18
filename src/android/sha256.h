// PS5Droid - SHA-256 (FIPS 180-4), used for Android image validation.
//
// Clean-room implementation from the NIST specification; no third-party
// code. Validated against the standard test vectors in tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ps5droid {
namespace android {

class Sha256 {
  public:
    Sha256();
    void update(const void* data, size_t len);
    // Finalizes and writes 32 raw bytes to `out`.
    void finish(uint8_t out[32]);

    // One-shot helpers.
    static void hash(const void* data, size_t len, uint8_t out[32]);
    static std::string hex(const uint8_t digest[32]);

    // Streams a file through the hash; returns false on I/O failure.
    static bool hash_file(const std::string& path, uint8_t out[32]);

  private:
    void process_block(const uint8_t block[64]);
    uint32_t state_[8];
    uint8_t buffer_[64];
    size_t buffer_len_;
    uint64_t total_len_;
};

} // namespace android
} // namespace ps5droid
