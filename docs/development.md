# Development and reproducible validation

## Host builds

Use CMake ≥ 3.16, a C++17 compiler and zlib. Python 3 generates test fixtures;
Ninja is optional. On Ubuntu, install `cmake ninja-build g++ clang zlib1g-dev`.
macOS uses its SDK zlib and a pthread adapter because Apple does not ship C11
`threads.h`; Linux and the PS5 SDK retain the existing C11 thread backend.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/ps5droid_tests
```

CTest has a 30-second suite timeout. Tests exercise behaviors including failed
operations and malformed input, not just successful allocation or nonempty output.
The exact current test count is printed by the executable rather than duplicated
across documents.

## Sanitizers and fuzzing

```sh
CC=clang CXX=clang++ cmake -S . -B build-asan -G Ninja \
  -DPS5DROID_SANITIZERS=address,undefined
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure

CC=clang CXX=clang++ cmake -S . -B build-tsan -G Ninja \
  -DPS5DROID_SANITIZERS=thread
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure

CC=clang CXX=clang++ cmake -S . -B build-fuzz -G Ninja \
  -DPS5DROID_BUILD_FUZZER=ON
cmake --build build-fuzz --target ps5droid_fuzz
mkdir -p build-fuzz/corpus
python3 tools/build_elf_fixture.py --output build-fuzz/corpus/cooperative.elf
cp tests/fixtures/image-manifest.txt build-fuzz/corpus/image-manifest.txt
./build-fuzz/ps5droid_fuzz build-fuzz/corpus -runs=10000 -max_len=65536
```

Address/UndefinedBehaviorSanitizer and ThreadSanitizer run separately. Seeded fuzzing also requires upstream LLVM Clang and LLD. The
libFuzzer target instruments ELF header and text image-manifest parsing. APK
inspection has deterministic malformed-container/AXML regression tests; this
harness does not claim APK fuzz coverage. Do not execute mutated ELF inputs.

Linux additionally compares supported eventfd/epoll behavior with native Linux
APIs. Regular files are explicitly rejected as epoll targets, matching Linux's
behavior. Only level-triggered eventfd readiness is currently implemented.

## Formatting and documentation

```sh
python3 -m pip install clang-format==23.1.3
git ls-files '*.cpp' '*.h' '*.c' | xargs clang-format --dry-run --Werror
python3 tools/check_docs.py
```

Documentation hygiene validates relative links and rejects stale execution claims.
Keep source/API contracts and capability labels aligned. Use the live
`ps5droid-host --matrix` output rather than hard-coded syscall counts.

## PS5 cross-build

The release URL, version and checksum are tracked in `.github/ps5-sdk.json`.
CI verifies the archive before installing it. Changing the pin requires fetching
that exact release and verifying its digest; `releases/latest` is not used.

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
cmake -S . -B build-ps5 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PS5_PAYLOAD_SDK/toolchain/prospero.cmake" \
  -DPS5DROID_BUILD_PS5=ON -DPS5DROID_SDK_VERSION=v0.45
cmake --build build-ps5 --target ps5droid
```

PS5 builds compile upstream zlib 1.3.2 from a URL and SHA-256 pinned in CMake.
The first configure needs network access; an offline build may prepopulate
`FETCHCONTENT_SOURCE_DIR_ZLIB`. Its source is under the zlib license.
Host binaries/tests are excluded from PS5 builds. The output is an ELF payload,
not a PKG. See [console validation](console-validation.md) for deployment.

## CI evidence

The workflow runs formatting/docs checks; Linux GCC/Clang builds; separate
ASan/UBSan and TSan suites; macOS portability; a bounded parser fuzz smoke;
cooperative x86-64 ELF execution; CLI/JSON metadata checks; and the pinned PS5
cross-build. It uploads logs, host JSON reports and the PS5 ELF checksum.

A job's configured existence does not establish a pass. Inspect the actual run
and artifacts for the tested revision. No CI job claims to execute on a PS5.

## Contribution rules

- Report errors and unsupported semantics explicitly; do not silently grant capabilities.
- Keep host C++ results (`ret=-1`, separate Linux errno) distinct from raw guest `-errno`.
- Tests involving threads must join before returning, including failure paths.
- Preserve fixture provenance and dependency licenses.
- Source every firmware declaration and record hardware evidence separately.
