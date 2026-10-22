// Read-only ZIP/APK and Android binary XML inspection. Format references:
// PKWARE APPNOTE 6.3.10; AOSP androidfw/ResourceTypes.h. No archive extraction.
#include "android/image_manager.h"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <zlib.h>

namespace ps5droid {
namespace android {
namespace {
constexpr size_t kManifestLimit = 4 * 1024 * 1024;
constexpr size_t kDirectoryLimit = 16 * 1024 * 1024;
struct View {
    const uint8_t* p;
    size_t size;
    bool has(size_t at, size_t count) const { return at <= size && count <= size - at; }
    uint16_t u16(size_t at) const { return uint16_t(p[at]) | (uint16_t(p[at + 1]) << 8); }
    uint32_t u32(size_t at) const { return uint32_t(u16(at)) | (uint32_t(u16(at + 2)) << 16); }
};
bool read_at(FILE* file, uint64_t offset, void* data, size_t size) {
    return offset <= LONG_MAX && fseek(file, static_cast<long>(offset), SEEK_SET) == 0 &&
           fread(data, 1, size, file) == size;
}
bool utf8(const uint8_t* p, size_t size, size_t expected_units) {
    size_t units = 0;
    for (size_t i = 0; i < size;) {
        uint32_t ch = p[i++];
        size_t extra = 0;
        uint32_t minimum = 0;
        if (ch >= 0xc2 && ch <= 0xdf) {
            ch &= 31;
            extra = 1;
            minimum = 0x80;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            ch &= 15;
            extra = 2;
            minimum = 0x800;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            ch &= 7;
            extra = 3;
            minimum = 0x10000;
        } else if (!ch || ch >= 0x80)
            return false;
        if (extra > size - i)
            return false;
        while (extra--) {
            if ((p[i] & 0xc0) != 0x80)
                return false;
            ch = (ch << 6) | (p[i++] & 63);
        }
        if (ch < minimum || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff))
            return false;
        units += ch > 0xffff ? 2 : 1;
    }
    return expected_units == SIZE_MAX || units == expected_units;
}
void append_utf8(std::string& out, uint32_t ch) {
    if (ch < 0x80)
        out += static_cast<char>(ch);
    else if (ch < 0x800) {
        out += static_cast<char>(0xc0 | (ch >> 6));
        out += static_cast<char>(0x80 | (ch & 63));
    } else if (ch < 0x10000) {
        out += static_cast<char>(0xe0 | (ch >> 12));
        out += static_cast<char>(0x80 | ((ch >> 6) & 63));
        out += static_cast<char>(0x80 | (ch & 63));
    } else {
        out += static_cast<char>(0xf0 | (ch >> 18));
        out += static_cast<char>(0x80 | ((ch >> 12) & 63));
        out += static_cast<char>(0x80 | ((ch >> 6) & 63));
        out += static_cast<char>(0x80 | (ch & 63));
    }
}
bool pool_strings(View v, std::vector<std::string>& strings) {
    if (!v.has(0, 28) || v.u16(2) < 28)
        return false;
    size_t count = v.u32(8), styles = v.u32(12), start = v.u32(20), style_start = v.u32(24);
    size_t header = v.u16(2);
    if (count > 65535 || styles > 65535 || !v.has(header, (count + styles) * 4) ||
        start < header + (count + styles) * 4 || start > v.size ||
        (style_start && (style_start < start || style_start > v.size)))
        return false;
    size_t end = style_start ? style_start : v.size;
    bool is_utf8 = v.u32(16) & 0x100;
    size_t decoded_total = 0;
    for (size_t i = 0; i < count; ++i) {
        size_t offset = v.u32(header + i * 4);
        if (offset >= end - start)
            return false;
        size_t at = start + offset;
        std::string text;
        if (is_utf8) {
            auto length = [&](size_t& n) {
                if (at >= end)
                    return false;
                n = v.p[at++];
                if (n & 0x80) {
                    if (at >= end)
                        return false;
                    n = ((n & 127) << 8) | v.p[at++];
                }
                return true;
            };
            size_t units, bytes;
            if (!length(units) || !length(bytes) || bytes >= end - at || v.p[at + bytes] ||
                !utf8(v.p + at, bytes, units))
                return false;
            if (bytes > kManifestLimit - decoded_total)
                return false;
            text.assign(reinterpret_cast<const char*>(v.p + at), bytes);
        } else {
            if (end - at < 2)
                return false;
            size_t units = v.u16(at);
            at += 2;
            if (units & 0x8000) {
                if (end - at < 2)
                    return false;
                units = ((units & 0x7fff) << 16) | v.u16(at);
                at += 2;
            }
            if (units > (end - at) / 2 || end - at - units * 2 < 2 || v.u16(at + units * 2))
                return false;
            for (size_t j = 0; j < units; ++j) {
                uint32_t ch = v.u16(at + j * 2);
                if (!ch)
                    return false;
                if (ch >= 0xd800 && ch <= 0xdbff) {
                    if (++j == units)
                        return false;
                    uint32_t low = v.u16(at + j * 2);
                    if (low < 0xdc00 || low > 0xdfff)
                        return false;
                    ch = 0x10000 + ((ch - 0xd800) << 10) + low - 0xdc00;
                } else if (ch >= 0xdc00 && ch <= 0xdfff)
                    return false;
                append_utf8(text, ch);
                if (text.size() > kManifestLimit - decoded_total)
                    return false;
            }
        }
        decoded_total += text.size();
        strings.push_back(std::move(text));
    }
    return true;
}
bool manifest_xml(const std::vector<uint8_t>& data, ApkInspection& out) {
    View v{data.data(), data.size()};
    if (!v.has(0, 8) || v.u16(0) != 3 || v.u16(2) != 8 || v.u32(4) != v.size)
        return false;
    std::vector<std::string> strings;
    std::vector<uint32_t> elements;
    bool pool_seen = false, root_seen = false, sdk_seen = false;
    auto string = [&](uint32_t index) -> const std::string* {
        return index < strings.size() ? &strings[index] : nullptr;
    };
    for (size_t at = 8; at < v.size;) {
        if (!v.has(at, 8))
            return false;
        size_t header = v.u16(at + 2), size = v.u32(at + 4);
        uint16_t type = v.u16(at);
        if (header < 8 || size < header || !v.has(at, size))
            return false;
        if (type == 1) {
            if (pool_seen || root_seen || !pool_strings({v.p + at, size}, strings))
                return false;
            pool_seen = true;
        } else if (type == 0x102) {
            if (!pool_seen || header != 16 || size < 36)
                return false;
            uint32_t ns = v.u32(at + 16), name_index = v.u32(at + 20);
            const auto* name = string(name_index);
            size_t attr_start = v.u16(at + 24), attr_size = v.u16(at + 26), count = v.u16(at + 28);
            if (!name || attr_start < 20 || attr_size < 20 || attr_start > size - 16 ||
                count > (size - 16 - attr_start) / attr_size || elements.size() > 256)
                return false;
            bool root = elements.empty();
            if (root && (root_seen || *name != "manifest" || ns != UINT32_MAX))
                return false;
            if (root)
                root_seen = true;
            bool sdk = elements.size() == 1 && *name == "uses-sdk" && ns == UINT32_MAX;
            if (sdk && sdk_seen)
                return false;
            if (sdk)
                sdk_seen = true;
            std::set<std::pair<uint32_t, uint32_t>> attributes;
            for (size_t i = 0; i < count; ++i) {
                size_t a = at + 16 + attr_start + i * attr_size;
                uint32_t ans = v.u32(a), aname = v.u32(a + 4), raw = v.u32(a + 8),
                         value = v.u32(a + 16);
                const auto* key = string(aname);
                if (!key || !attributes.insert({ans, aname}).second || v.u16(a + 12) != 8)
                    return false;
                uint8_t value_type = v.p[a + 15];
                if (raw != UINT32_MAX && !string(raw))
                    return false;
                if (value_type == 3 && !string(value))
                    return false;
                if (root && *key == "package" && ans == UINT32_MAX) {
                    if (value_type != 3)
                        return false;
                    out.package_name = *string(value);
                }
                const auto* space = ans == UINT32_MAX ? nullptr : string(ans);
                if (ans != UINT32_MAX && !space)
                    return false;
                if (sdk && space && *space == "http://schemas.android.com/apk/res/android" &&
                    (*key == "minSdkVersion" || *key == "targetSdkVersion")) {
                    // Resource references/codenames cannot be resolved without resources.arsc.
                    if (value_type != 0x10 && value_type != 0x11)
                        return false;
                    if (!value || value > INT32_MAX)
                        return false;
                    if (*key == "minSdkVersion")
                        out.min_sdk = static_cast<int>(value);
                    else
                        out.target_sdk = static_cast<int>(value);
                }
            }
            elements.push_back(name_index);
        } else if (type == 0x103) {
            if (header != 16 || size != 24 || elements.empty() || v.u32(at + 20) != elements.back())
                return false;
            elements.pop_back();
        } else if (type == 0x100 || type == 0x101) {
            if (header != 16 || size != 24 || !string(v.u32(at + 20)) ||
                (v.u32(at + 16) != UINT32_MAX && !string(v.u32(at + 16))))
                return false;
        } else if (type == 0x180) {
            if (header != 8 || (size - header) % 4)
                return false;
        } else if (type == 0x104) {
            if (header != 16 || size != 28 || elements.empty() || !string(v.u32(at + 16)))
                return false;
        } else
            return false;
        at += size;
    }
    if (!root_seen || !elements.empty() || out.package_name.empty())
        return false;
    for (char c : out.package_name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_' || c == '.'))
            return false;
    if (!out.target_sdk)
        out.target_sdk = out.min_sdk;
    return true;
}
struct ZipEntry {
    std::string name;
    uint16_t flags, method;
    uint32_t crc, compressed, uncompressed, offset;
};
} // namespace
ApkInspection inspect_apk(const std::string& path) {
    ApkInspection out;
    auto fail = [&](const char* note) {
        out.is_apk = false;
        out.failure_note = note;
        return out;
    };
    std::unique_ptr<FILE, decltype(&fclose)> fp(fopen(path.c_str(), "rb"), fclose);
    if (!fp)
        return fail("cannot open APK file");
    if (fseek(fp.get(), 0, SEEK_END))
        return fail("cannot seek APK");
    long length = ftell(fp.get());
    if (length < 22 || static_cast<uint64_t>(length) > UINT32_MAX)
        return fail("unsupported ZIP size (ZIP64 is not supported)");
    size_t tail_size = std::min<size_t>(length, 65557);
    std::vector<uint8_t> tail(tail_size);
    if (!read_at(fp.get(), length - tail_size, tail.data(), tail.size()))
        return fail("cannot read ZIP directory trailer");
    View t{tail.data(), tail.size()};
    size_t end = tail_size;
    for (size_t i = tail_size - 22;; --i) {
        if (t.u32(i) == 0x06054b50 && t.u16(i + 20) == tail_size - i - 22) {
            end = i;
            break;
        }
        if (!i)
            break;
    }
    if (end == tail_size)
        return fail("missing ZIP end-of-central-directory record");
    size_t entries = t.u16(end + 10), dir_size = t.u32(end + 12), dir_offset = t.u32(end + 16);
    if (t.u16(end + 4) || t.u16(end + 6) || t.u16(end + 8) != entries || entries == 0xffff ||
        dir_size == UINT32_MAX || dir_offset == UINT32_MAX || dir_size > kDirectoryLimit ||
        uint64_t(dir_offset) + dir_size != uint64_t(length) - tail_size + end)
        return fail("unsupported or inconsistent ZIP directory");
    std::vector<uint8_t> directory(dir_size);
    if (!read_at(fp.get(), dir_offset, directory.data(), directory.size()))
        return fail("truncated ZIP directory");
    View d{directory.data(), directory.size()};
    size_t at = 0;
    std::set<std::string> names;
    std::set<std::string> abis;
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    ZipEntry manifest{};
    bool found_manifest = false;
    for (size_t i = 0; i < entries; ++i) {
        if (!d.has(at, 46) || d.u32(at) != 0x02014b50)
            return fail("invalid ZIP directory entry");
        size_t name_size = d.u16(at + 28), extra = d.u16(at + 30), comment = d.u16(at + 32);
        if (!name_size || !d.has(at + 46, name_size + extra + comment) || d.u16(at + 34))
            return fail("invalid ZIP entry lengths");
        ZipEntry entry{std::string(reinterpret_cast<const char*>(d.p + at + 46), name_size),
                       d.u16(at + 8),
                       d.u16(at + 10),
                       d.u32(at + 16),
                       d.u32(at + 20),
                       d.u32(at + 24),
                       d.u32(at + 42)};
        if (entry.name.find('\0') != std::string::npos || !names.insert(entry.name).second ||
            entry.flags & (1 | 0x40 | 0x2000) || (entry.method != 0 && entry.method != 8) ||
            entry.compressed == UINT32_MAX || entry.uncompressed == UINT32_MAX ||
            entry.offset >= dir_offset)
            return fail("unsupported or duplicate ZIP entry");
        const auto* name_bytes = reinterpret_cast<const uint8_t*>(entry.name.data());
        if ((entry.flags & 0x800) && !utf8(name_bytes, name_size, SIZE_MAX))
            return fail("invalid UTF-8 ZIP filename");
        if (!(entry.flags & 0x800))
            for (unsigned char ch : entry.name)
                if (ch >= 0x80)
                    return fail("legacy non-ASCII ZIP filenames are not supported");
        uint8_t local[30];
        if (!read_at(fp.get(), entry.offset, local, 30))
            return fail("truncated ZIP local header");
        View l{local, 30};
        uint64_t data_at = uint64_t(entry.offset) + 30 + l.u16(26) + l.u16(28);
        if (l.u32(0) != 0x04034b50 || l.u16(6) != entry.flags || l.u16(8) != entry.method ||
            l.u16(26) != name_size || data_at > dir_offset ||
            entry.compressed > dir_offset - data_at)
            return fail("inconsistent ZIP local header");
        std::vector<char> local_name(name_size);
        if (!read_at(fp.get(), uint64_t(entry.offset) + 30, local_name.data(), name_size) ||
            std::memcmp(local_name.data(), entry.name.data(), name_size))
            return fail("ZIP local filename mismatch");
        if (!(entry.flags & 8) && (l.u32(14) != entry.crc || l.u32(18) != entry.compressed ||
                                   l.u32(22) != entry.uncompressed))
            return fail("ZIP local size or CRC mismatch");
        ranges.push_back({entry.offset, data_at + entry.compressed});
        if (entry.name.back() != '/')
            ++out.file_count;
        if (entry.name == "AndroidManifest.xml") {
            manifest = entry;
            found_manifest = true;
        }
        if (entry.name == "classes.dex" ||
            (entry.name.size() > 11 && entry.name.substr(0, 7) == "classes" &&
             entry.name.substr(entry.name.size() - 4) == ".dex" &&
             entry.name.find_first_not_of("0123456789", 7) == entry.name.size() - 4))
            out.dex_files.push_back(entry.name);
        if (entry.name.substr(0, 4) == "lib/" && entry.name.size() > 7 &&
            entry.name.substr(entry.name.size() - 3) == ".so") {
            size_t slash = entry.name.find('/', 4);
            if (slash != std::string::npos && slash > 4 &&
                entry.name.find('/', slash + 1) == std::string::npos)
                abis.insert(entry.name.substr(4, slash - 4));
        }
        at += 46 + name_size + extra + comment;
    }
    if (at != directory.size())
        return fail("ZIP directory entry count mismatch");
    std::sort(ranges.begin(), ranges.end());
    for (size_t i = 1; i < ranges.size(); ++i)
        if (ranges[i].first < ranges[i - 1].second)
            return fail("overlapping ZIP entries");
    if (!found_manifest)
        return fail("AndroidManifest.xml is absent");
    if (!manifest.uncompressed || manifest.uncompressed > kManifestLimit ||
        manifest.compressed > kManifestLimit)
        return fail("manifest exceeds inspection limits");
    uint8_t local[30];
    if (!read_at(fp.get(), manifest.offset, local, 30))
        return fail("cannot read manifest header");
    View l{local, 30};
    uint64_t data_at = uint64_t(manifest.offset) + 30 + l.u16(26) + l.u16(28);
    std::vector<uint8_t> compressed(manifest.compressed), xml(manifest.uncompressed);
    if (!read_at(fp.get(), data_at, compressed.data(), compressed.size()))
        return fail("truncated manifest payload");
    if (!manifest.method) {
        if (compressed.size() != xml.size())
            return fail("stored manifest size mismatch");
        xml = std::move(compressed);
    } else {
        z_stream stream{};
        stream.next_in = compressed.data();
        stream.avail_in = compressed.size();
        stream.next_out = xml.data();
        stream.avail_out = xml.size();
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            return fail("cannot initialize DEFLATE");
        int result = inflate(&stream, Z_FINISH);
        bool complete = result == Z_STREAM_END && stream.total_in == compressed.size() &&
                        stream.total_out == xml.size();
        inflateEnd(&stream);
        if (!complete)
            return fail("invalid compressed manifest");
    }
    if (crc32(0, xml.data(), xml.size()) != manifest.crc)
        return fail("manifest CRC mismatch");
    if (!manifest_xml(xml, out))
        return fail("invalid or unsupported Android binary XML manifest");
    out.has_manifest = true;
    out.has_classes_dex = !out.dex_files.empty();
    out.native_abis.assign(abis.begin(), abis.end());
    out.has_native_libs = !out.native_abis.empty();
    out.is_apk = true;
    return out;
}
} // namespace android
} // namespace ps5droid
