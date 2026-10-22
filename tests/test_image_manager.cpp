// PS5Droid - image manager tests (manifest, ext4 probe, SHA-256).
#include "test_framework.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "android/image_manager.h"
#include "android/sha256.h"
#include "platform/platform.h"

using namespace ps5droid::android;

PS5DROID_TEST(sha256_standard_vectors) {
    uint8_t digest[32];

    Sha256::hash("", 0, digest);
    CHECK_MSG(Sha256::hex(digest) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b78"
                                     "52b855",
              "empty vector mismatch");

    Sha256::hash("abc", 3, digest);
    CHECK_MSG(Sha256::hex(digest) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f2"
                                     "0015ad",
              "abc vector mismatch");

    const char* long_input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Sha256::hash(long_input, strlen(long_input), digest);
    CHECK_MSG(Sha256::hex(digest) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419"
                                     "db06c1",
              "448-bit vector mismatch");

    // Chunked updates must equal one-shot hashing.
    const char* chunked = "The quick brown fox jumps over the lazy dog";
    Sha256 h;
    h.update(chunked, 9);
    h.update(chunked + 9, 10);
    h.update(chunked + 19, strlen(chunked) - 19);
    h.finish(digest);
    Sha256::hash(chunked, strlen(chunked), digest);
    // (digest now holds the one-shot; recompute chunked for comparison)
    uint8_t d2[32];
    Sha256 h2;
    h2.update(chunked, 9);
    h2.update(chunked + 9, 10);
    h2.update(chunked + 19, strlen(chunked) - 19);
    h2.finish(d2);
    CHECK_MSG(Sha256::hex(d2) == Sha256::hex(digest), "chunked hashing differs from one-shot");
}

namespace {

bool write_bytes(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f)
        return false;
    bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

std::vector<uint8_t> make_ext4_fixture() {
    // Synthetic ext4-like image: superblock at offset 1024.
    std::vector<uint8_t> img(4096, 0);
    uint8_t* sb = img.data() + 1024;
    auto wr32 = [&](int off, uint32_t v) {
        sb[off] = v & 0xFF;
        sb[off + 1] = (v >> 8) & 0xFF;
        sb[off + 2] = (v >> 16) & 0xFF;
        sb[off + 3] = (v >> 24) & 0xFF;
    };
    wr32(0x00, 64);  // s_inodes_count
    wr32(0x04, 512); // s_blocks_count_lo
    wr32(0x18, 2);   // s_log_block_size -> 4096
    sb[0x38] = 0x53; // magic 0xEF53 little-endian
    sb[0x39] = 0xEF;
    for (int i = 0; i < 16; ++i)
        sb[0x68 + i] = static_cast<uint8_t>(0x10 + i);
    memcpy(sb + 0x78, "ps5droid", 8);
    return img;
}

} // namespace

PS5DROID_TEST(manifest_parse_ok) {
    const char* text =
        "# PS5Droid image manifest\n"
        "system_image = images/system.img\n"
        "vendor_image=images/vendor.img\n"
        "android_api_level = 33\n"
        "abi = x86_64\n"
        "system_sha256 = aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
        "description = test fixture\n";
    ImageManifest m;
    ParseStatus st = parse_manifest(text, &m);
    CHECK_MSG(st == ParseStatus::kOk, "parse failed");
    CHECK_MSG(m.system_image == "images/system.img", "system_image");
    CHECK_MSG(m.vendor_image == "images/vendor.img", "vendor_image");
    CHECK_MSG(m.android_api_level == 33, "api level");
    CHECK_MSG(m.abi == "x86_64", "abi");
    CHECK_MSG(m.system_sha256 == std::string(64, 'a'), "sha");
}

PS5DROID_TEST(manifest_parse_errors) {
    ImageManifest m;
    CHECK_MSG(parse_manifest("", &m) == ParseStatus::kEmpty, "empty");
    CHECK_MSG(parse_manifest("nonsense line\n", &m) == ParseStatus::kSyntaxError, "syntax");
    CHECK_MSG(parse_manifest("system_image = a.img\n", &m) == ParseStatus::kMissingKeys,
              "missing keys");
    // A non-numeric api level parses as 0 and must be rejected as missing.
    ImageManifest m2;
    ParseStatus st = parse_manifest("android_api_level = banana\n"
                                    "system_image = a\n"
                                    "vendor_image = b\n"
                                    "abi = x\n",
                                    &m2);
    CHECK_MSG(st == ParseStatus::kBadValue, "non-numeric api level rejected");
}

PS5DROID_TEST(ext4_probe_detects_fixture) {
    const std::string path = "ps5droid_test_ext4.img";
    CHECK_MSG(write_bytes(path, make_ext4_fixture()), "fixture write");

    Ext4Probe p = probe_ext4(path);
    CHECK_MSG(p.is_ext4, "magic not detected");
    CHECK_MSG(p.blocks_count == 512, "blocks count wrong");
    CHECK_MSG(p.block_size == 4096, "block size wrong");
    CHECK_MSG(p.volume_label == "ps5droid", "volume label wrong");

    // Non-ext4 file must be rejected.
    std::vector<uint8_t> junk(4096, 0x41);
    const std::string junk_path = "ps5droid_test_junk.img";
    CHECK_MSG(write_bytes(junk_path, junk), "junk write");
    Ext4Probe j = probe_ext4(junk_path);
    CHECK_MSG(!j.is_ext4, "junk must not pass");

    ps5droid::platform::remove_file(path);
    ps5droid::platform::remove_file(junk_path);
}

PS5DROID_TEST(verify_manifest_digests) {
    const std::string sys_path = "ps5droid_test_system.img";
    const std::string vnd_path = "ps5droid_test_vendor.img";
    std::vector<uint8_t> sys_img = make_ext4_fixture();
    std::vector<uint8_t> vnd_img(2048, 0x7E);

    CHECK_MSG(write_bytes(sys_path, sys_img), "system fixture");
    CHECK_MSG(write_bytes(vnd_path, vnd_img), "vendor fixture");

    uint8_t d[32];
    CHECK_MSG(Sha256::hash_file(sys_path, d), "hash file failed");
    std::string sys_hex = Sha256::hex(d);

    ImageManifest m;
    m.system_image = sys_path;
    m.vendor_image = vnd_path;
    m.android_api_level = 33;
    m.abi = "x86_64";
    m.system_sha256 = sys_hex;

    ManifestCheck r = verify_manifest(m, "");
    CHECK_MSG(r.system_exists && r.vendor_exists, "existence");
    CHECK_MSG(r.system_digest_ok, "system digest");
    CHECK_MSG(r.vendor_digest_ok, "vendor digest (unset = ok)");
    CHECK_MSG(r.failure_note.empty(), "no failure expected");

    // Corrupt digest must fail.
    m.system_sha256 = std::string(64, '0');
    r = verify_manifest(m, "");
    CHECK_MSG(!r.system_digest_ok, "digest mismatch must be detected");
    CHECK_MSG(!r.failure_note.empty(), "failure note required");

    ps5droid::platform::remove_file(sys_path);
    ps5droid::platform::remove_file(vnd_path);
}
