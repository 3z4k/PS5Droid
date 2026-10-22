# PS5Droid - build system.
#
# Host build (development, CI):
#   cmake -B build && cmake --build build && ctest --test-dir build
#
# PS5 payload build (requires ps5-payload-sdk, see docs/development.md):
#   cmake -B build-ps5 \
#     -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/host/toolchain/prospero.cmake \
#     -DPS5DROID_BUILD_PS5=ON
#   cmake --build build-ps5 --target ps5droid
#
# The PS5 target is opt-in and produces ps5droid.elf (an ELF payload). It
# does NOT produce a PKG; packaging is not implemented.
cmake_minimum_required(VERSION 3.16)

project(ps5droid
    VERSION 0.1.0
    DESCRIPTION "PS5Droid - native Android runtime research for PS5"
    LANGUAGES C CXX)

set(PS5DROID_SDK_VERSION "unrecorded" CACHE STRING "Pinned SDK version used by this build")
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE PS5DROID_BUILD_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE PS5DROID_TREE_STATUS ERROR_QUIET)
if(NOT PS5DROID_BUILD_REVISION)
    set(PS5DROID_BUILD_REVISION "unrecorded")
elseif(PS5DROID_TREE_STATUS)
    string(APPEND PS5DROID_BUILD_REVISION "-dirty")
endif()
configure_file(cmake/build_info.h.in generated/build_info.h @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(PS5DROID_BUILD_PS5 "Build the native PS5 payload (needs
ps5-payload-sdk toolchain)" OFF)
set(PS5DROID_SANITIZERS "none" CACHE STRING "Host sanitizers: none, address,undefined, or thread")
option(PS5DROID_BUILD_FUZZER "Build LLVM libFuzzer parser harness (host Clang only)" OFF)
option(PS5DROID_BUILD_HOST "Build host runtime and tests" ON)
option(PS5DROID_BUILD_TESTS "Build host unit tests" ON)

# The ps5-payload-sdk toolchain file sets PROSPERO/PS5 and CMAKE_SYSROOT.
if(PS5DROID_BUILD_PS5 AND NOT PROSPERO)
    message(FATAL_ERROR
        "PS5DROID_BUILD_PS5=ON requires the ps5-payload-sdk toolchain file: "
        "-DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/host/toolchain/prospero.cmake")
endif()

if(PROSPERO)
    set(PS5DROID_PLATFORM_PS5_VALUE ON)
else()
    set(PS5DROID_PLATFORM_PS5_VALUE OFF)
endif()

# APK manifests use ZIP DEFLATE. Host builds use system zlib; console builds
# build the same pinned upstream source with the PS5 toolchain.
if(PROSPERO)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    include(FetchContent)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz
        URL_HASH SHA256=b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1)
    FetchContent_MakeAvailable(zlib)
    set(PS5DROID_ZLIB zlibstatic)
else()
    find_package(ZLIB REQUIRED)
    set(PS5DROID_ZLIB ZLIB::ZLIB)
endif()

# ---------------------------------------------------------------------------
# Portable core library (no OS headers leak through this target's API).
# ---------------------------------------------------------------------------
add_library(ps5droid_core STATIC
    src/platform/log.cpp
    src/platform/memory.cpp
    src/platform/threads.cpp
    src/platform/filesystem.cpp
    src/platform/display.cpp
    src/platform/audio.cpp
    src/compat/linux_abi.cpp
    src/compat/memory_compat.cpp
    src/compat/syscall_dispatch.cpp
    src/compat/ipc_compat.cpp
    src/compat/fd_table.cpp
    src/compat/bionic_loader.cpp
    src/compat/elf_runner.cpp
    src/android/sha256.cpp
    src/android/image_manager.cpp
    src/android/apk_inspector.cpp
    src/android/runtime_manager.cpp
    src/android/evidence_report.cpp
    src/android/binder/binder_engine.cpp
    src/ui/launcher.cpp
    src/ui/bitmap_font.cpp
)

target_include_directories(ps5droid_core PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(ps5droid_core PUBLIC ${PS5DROID_ZLIB})
target_compile_features(ps5droid_core PUBLIC cxx_std_17)
target_compile_options(ps5droid_core PRIVATE -Wall -Wextra)

if(NOT PROSPERO AND NOT PS5DROID_SANITIZERS STREQUAL "none")
    if(NOT PS5DROID_SANITIZERS MATCHES "^(address,undefined|thread)$")
        message(FATAL_ERROR "Unsupported sanitizer selection")
    endif()
    target_compile_options(ps5droid_core PUBLIC -fsanitize=${PS5DROID_SANITIZERS} -fno-omit-frame-pointer)
    target_link_options(ps5droid_core PUBLIC -fsanitize=${PS5DROID_SANITIZERS})
endif()
if(PS5DROID_BUILD_FUZZER)
    if(PROSPERO OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "libFuzzer requires host Clang")
    endif()
    target_compile_options(ps5droid_core PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    target_link_options(ps5droid_core INTERFACE -fsanitize=address,undefined)
    add_executable(ps5droid_fuzz tests/fuzz_parsers.cpp)
    target_link_libraries(ps5droid_fuzz PRIVATE ps5droid_core)
    target_compile_options(ps5droid_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(ps5droid_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

if(PS5DROID_PLATFORM_PS5_VALUE)
    target_compile_definitions(ps5droid_core PUBLIC PS5DROID_PLATFORM_PS5)
endif()

# ---------------------------------------------------------------------------
# Host runtime (CLI research harness). Never built for the PS5.
# ---------------------------------------------------------------------------
if(PS5DROID_BUILD_HOST AND NOT PROSPERO)
    add_executable(ps5droid-host src/app/main.cpp)
    target_link_libraries(ps5droid-host PRIVATE ps5droid_core)
    target_compile_options(ps5droid-host PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# Unit tests (host only).
# ---------------------------------------------------------------------------
if(PS5DROID_BUILD_TESTS AND PS5DROID_BUILD_HOST AND NOT PROSPERO)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fixtures/stamp"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_test_fixtures.py"
                "${CMAKE_CURRENT_BINARY_DIR}/fixtures"
        DEPENDS tools/make_test_fixtures.py tests/fixtures/aapt2-manifest.apk)
    add_custom_target(ps5droid_fixtures DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/fixtures/stamp")


    add_executable(ps5droid_tests
        tests/host_main.cpp
        tests/test_platform_memory.cpp
        tests/test_platform_threads.cpp
        tests/test_compat_dispatch.cpp
        tests/test_compat_matrix.cpp
        tests/test_ipc_ring.cpp
        tests/test_image_manager.cpp
        tests/test_runtime_manager.cpp
        tests/test_ui.cpp
        tests/test_bionic_and_fd.cpp
        tests/test_fd_semantics.cpp
        tests/test_elf_validation.cpp
        tests/test_rpc_semantics.cpp
        tests/test_apk_inspection.cpp
        tests/test_evidence_report.cpp
        tests/test_binder_and_apk.cpp
    )
    add_dependencies(ps5droid_tests ps5droid_fixtures)
    target_compile_definitions(ps5droid_tests PRIVATE
        PS5DROID_TEST_FIXTURES="${CMAKE_CURRENT_BINARY_DIR}/fixtures"
        PS5DROID_SOURCE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    target_include_directories(ps5droid_tests PRIVATE tests src)
    target_link_libraries(ps5droid_tests PRIVATE ps5droid_core)
    target_compile_options(ps5droid_tests PRIVATE -Wall -Wextra)

    add_test(NAME ps5droid_unit_tests
             COMMAND ps5droid_tests
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(ps5droid_unit_tests PROPERTIES TIMEOUT 30)
endif()

# ---------------------------------------------------------------------------
# Native PS5 payload (ps5droid.elf). Built ONLY with the SDK toolchain.
# ---------------------------------------------------------------------------
if(PROSPERO)
    add_executable(ps5droid
        src/app/ps5_main.cpp
        src/platform/ps5/display_ps5.cpp
        src/platform/ps5/input_ps5.cpp
    )
    set_target_properties(ps5droid PROPERTIES OUTPUT_NAME "ps5droid.elf")
    target_link_libraries(ps5droid PRIVATE ps5droid_core)
    target_compile_options(ps5droid PRIVATE -Wall -Wextra)
    # Firmware stub libraries provided by ps5-payload-sdk (sce_stubs).
    target_link_libraries(ps5droid PRIVATE
        kernel SceVideoOut ScePad SceSystemService SceUserService)
    message(STATUS "PS5Droid: PS5 payload target enabled (ELF, not PKG)")
else()
    message(STATUS "PS5Droid: PS5 payload disabled "
                   "(configure with the ps5-payload-sdk toolchain to enable)")
endif()
