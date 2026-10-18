# PS5Droid

**Native Android runtime research for jailbroken PlayStation 5 consoles.**

PS5Droid is an open-source systems-research project that investigates —
step by step, with test evidence — what it would take to run Android
application components directly inside a native PS5 homebrew payload.
It is *not* a Linux boot, a VM, an emulator image, or a Waydroid port,
and it does **not** claim to run Android apps today. Every capability
this repository ships is either tested on the host, implemented against
verified console APIs but not yet console-executed, or honestly marked
not implemented / blocked.

Waydroid is used as an *architectural reference* only (see
[`docs/waydroid-analysis.md`](docs/waydroid-analysis.md)); no Waydroid
code is imported. Low-level console facts come from the open-source
[ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) ecosystem.

## Current status

| Area | Status |
|---|---|
| Portable platform layer (memory, threads, fs, IPC) | **Implemented and tested (host)** |
| Linux ABI dispatch experiment (mmap/brk/futex/... subset) | **Implemented and tested (host)** |
| Compatibility matrix (77 syscalls: 9 emulated / 3 partial / 65 unsupported) | **Implemented and tested (host)** |
| Capability probes + runtime manager with structured blockers | **Implemented and tested (host)** |
| Android image manifest + SHA-256 + read-only ext4 superblock probe | **Implemented and tested (host)** |
| Launcher UI model + 8x8 framebuffer font | **Implemented and tested (host)** |
| Native PS5 payload (`ps5droid.elf`, video-out + pad + launcher) | **Implemented, NOT tested on console** |
| PS5 PKG packaging | **Not implemented** (ELF-payload path only) |
| Linux binary loading / bionic re-hosting (Milestone 4) | **Not implemented** (research) |
| Binder re-hosting (Milestone 5) | **Not implemented** (protocol notes only) |
| APK launch (Milestone 6) | **Not implemented** |
| Audio output | **Not implemented** (explicitly reported unsupported) |
| Vulkan / GPU-accelerated Android graphics | **Blocked** (no Vulkan driver for homebrew; proprietary AGC/GNM only) |

Build status: host build + 40 unit tests green (gcc and clang, ASan/UBSan
clean). The PS5 target is cross-built in CI only when explicitly
requested, using the official open-source SDK.

## Architecture in one page

```
app/main.cpp (host CLI)          app/ps5_main.cpp (PS5 payload)
        ui/launcher + ui/bitmap_font
   android/runtime_manager + android/image_manager (+ sha256)
        compat/syscall_dispatch (Linux ABI experiment)
          compat/memory_compat + compat/ipc_compat
             platform/ (posix backend | ps5 backend)
```

* **platform/** — portable memory/thread/fs/display/input primitives;
  the PS5 backend uses ps5-payload-sdk facilities with hand-maintained,
  provenance-documented firmware declarations.
* **compat/** — accepts Linux x86_64 syscall numbers and executes a
  small, honestly classified subset (anonymous memory, clocks, futex
  wait/wake, streams); everything else returns `-ENOSYS` with an
  explanation. The verified Linux→FreeBSD number mapping (e.g. mmap
  9→477) is data in `src/compat/linux_abi.h`.
* **android/** — image handling (manifest, SHA-256, ext4 superblock
  probe; no mounting is possible on Orbis) and the runtime manager whose
  start attempt always reports structured, reasoned blockers today.
* **ui/** — pad/keyboard-driven launcher model and a framebuffer font.

Details: [`docs/architecture.md`](docs/architecture.md).

## Host build & test

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/ps5droid-host            # probes + Linux ABI demo
./build/ps5droid-host --matrix   # full compatibility matrix
```

## PS5 build (requires a jailbroken console + ps5-payload-sdk)

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
cmake -B build-ps5 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=$PS5_PAYLOAD_SDK/host/toolchain/prospero.cmake \
  -DPS5DROID_BUILD_PS5=ON
cmake --build build-ps5 --target ps5droid   # -> build-ps5/ps5droid.elf
```

Deploy with `prospero-deploy` over the ELF loader (port 9021). See
[`docs/development.md`](docs/development.md) for prerequisites and
console caveats. **`ps5droid.elf` is a payload, not a PKG**, and has not
been run on a console from this project yet.

## Licensing

PS5Droid is MIT-licensed (see [`LICENSE`](LICENSE)). Third-party
references and their licenses are tracked in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). The project never
bundles or downloads proprietary Sony SDKs, console keys, Google Play
Services, Widevine or proprietary codecs.
