#include "compat/bionic_loader.h"
#include "compat/elf_runner.h"
#include "compat/memory_compat.h"
#include "elf_fixture.h"
#include "test_framework.h"
#include <algorithm>
#include <cstring>
using namespace ps5droid::compat;
PS5DROID_TEST(elf_rejects_overflowing_table_and_invalid_layout) {
    auto bytes = fixture::elf();
    fixture::put(bytes, 32, UINT64_MAX - 15, 8);
    CHECK(!BionicLoader::parse_header(bytes.data(), bytes.size()).valid);
    bytes = fixture::elf();
    fixture::put(bytes, 54, 1, 2);
    CHECK(!BionicLoader::parse_header(bytes.data(), bytes.size()).valid);
    bytes = fixture::elf();
    bytes[5] = 2;
    CHECK(!BionicLoader::parse_header(bytes.data(), bytes.size()).valid);
}
PS5DROID_TEST(elf_rejects_invalid_segment_ranges_alignment_and_entry) {
    auto bytes = fixture::elf();
    fixture::put(bytes, 72, UINT64_MAX, 8);
    CHECK(!BionicLoader::parse_header(bytes.data(), bytes.size()).valid);
    bytes = fixture::elf();
    fixture::put(bytes, 104, 100, 8);
    CHECK(!BionicLoader::parse_header(bytes.data(), bytes.size()).valid);
    bytes = fixture::elf();
    fixture::put(bytes, 112, 3, 8);
    CHECK(!BionicLoader::parse_header(bytes.data(), bytes.size()).valid);
    bytes = fixture::elf();
    fixture::put(bytes, 24, 1000, 8);
    CHECK(!BionicLoader::parse_header(bytes.data(), bytes.size()).valid);
}
PS5DROID_TEST(elf_accepts_unaligned_input_without_casting_structs) {
    auto bytes = fixture::elf();
    bytes.insert(bytes.begin(), 0);
    CHECK(BionicLoader::parse_header(bytes.data() + 1, bytes.size() - 1).valid);
}
PS5DROID_TEST(initial_stack_contains_strings_valid_pointers_and_auxv) {
    auto bytes = fixture::elf();
    auto info = BionicLoader::parse_header(bytes.data(), bytes.size());
    auto stack = BionicLoader::build_initial_stack(info, {"hello", "world"}, {"A=B"}, 0x100000,
                                                   0x200040, 0x200000, 16384);
    CHECK(stack.error.empty());
    CHECK(stack.stack_pointer % 16 == 0);
    auto word = [&](size_t i) {
        uint64_t x;
        std::memcpy(&x, stack.bytes.data() + i * 8, 8);
        return x;
    };
    CHECK(word(0) == 2 && word(3) == 0 && word(5) == 0);
    CHECK(std::string(reinterpret_cast<char*>(stack.bytes.data() + word(1) - 0x100000)) == "hello");
    CHECK(std::string(reinterpret_cast<char*>(stack.bytes.data() + word(4) - 0x100000)) == "A=B");
    CHECK(word(6) == 3 && word(7) == 0x200040);   // AT_PHDR
    CHECK(word(12) == 6 && word(13) == 16384);    // AT_PAGESZ
    CHECK(word(14) == 9 && word(15) == 0x200080); // AT_ENTRY
    CHECK(word(16) == 0 && word(17) == 0);
}
PS5DROID_TEST(initial_stack_rejects_oversized_arguments_and_bad_addresses) {
    auto bytes = fixture::elf();
    auto info = BionicLoader::parse_header(bytes.data(), bytes.size());
    auto make = [&](const std::vector<std::string>& args, uint64_t base) {
        return BionicLoader::build_initial_stack(info, args, {}, base, 0x200040, 0, 4096);
    };
    CHECK(!make({std::string(1024 * 1024, 'x')}, 0x100000).error.empty());
    CHECK(!make(std::vector<std::string>(4097, "x"), 0x100000).error.empty());
    CHECK(!make({std::string("a\0b", 3)}, 0x100000).error.empty());
    CHECK(!make({"a"}, UINT64_MAX - 15).error.empty());
    CHECK(!make({"a"}, 1).error.empty());
}
PS5DROID_TEST(compat_memory_rounds_unmap_and_rejects_overflow_and_exec) {
    CompatMemory memory;
    auto mapped = memory.mmap(0, 8192, 3, 0x22, -1, 0);
    CHECK(mapped.address);
    CHECK(memory.munmap(reinterpret_cast<uintptr_t>(mapped.address), 8192).linux_errno == 0);
    CHECK(!memory.mmap(0, SIZE_MAX, 3, 0x22, -1, 0).address);
    CHECK(!memory.mmap(0, 4096, 5, 0x22, -1, 0).address);
    CHECK(!memory.is_mapped(UINTPTR_MAX - 4, 100));
}
PS5DROID_TEST(elf_runner_rejects_invalid_input) {
    const char junk[] = "not an ELF";
    auto result = run_cooperative_elf(junk, sizeof(junk));
    CHECK(!result.executed && !result.note.empty());
}
PS5DROID_TEST(elf_runner_rejects_executable_contract_violations) {
#if defined(__x86_64__)
    auto bytes = fixture::elf();
    fixture::put(bytes, 68, 7, 4);
    CHECK(!run_cooperative_elf(bytes.data(), bytes.size()).executed);
    bytes = fixture::elf();
    bytes[130] = 0x0f;
    bytes[131] = 0x05;
    auto raw = run_cooperative_elf(bytes.data(), bytes.size());
    CHECK(!raw.executed && raw.note.find("syscall") != std::string::npos);
    bytes = fixture::elf();
    fixture::put(bytes, 56, 2, 2);
    std::fill(bytes.begin() + 120, bytes.begin() + 176, 0);
    fixture::put(bytes, 120, 2, 4);
    auto dynamic = run_cooperative_elf(bytes.data(), bytes.size());
    CHECK(!dynamic.executed && dynamic.note.find("dynamic") != std::string::npos);
#endif
}
