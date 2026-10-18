# PS5Droid Architecture

Status: living document describing the implemented system and the planned
path toward native Android execution. Companion documents:
`platform-constraints.md` (what the PS5 allows), `waydroid-analysis.md`
(what upstream does), `development.md` (how to build/test).

## 1. Design goal and non-goals

**Goal**: a *native* Orbis homebrew payload that can eventually host
Android application components directly — no Linux boot, no VM, no
streaming, no conventional Waydroid install.

**Non-goals (explicitly)**: running a Linux kernel on the PS5, running
unmodified Android system images, Google Play / proprietary components.

## 2. Module map (implemented)

```
src/
├── platform/                 portable platform layer
│   ├── platform.h            logging/time/memory/threads/fs API
│   ├── log.cpp               logging + backend identity
│   ├── memory.cpp            mmap-backed page allocator (shared by both ABIs)
│   ├── threads.cpp           C11-threads core + futex-like registry
│   ├── filesystem.cpp        stdio/dirent file access
│   ├── display.h/.cpp        Framebuffer + DisplayBackend + host impl
│   ├── audio.h/.cpp          AudioBackend (explicitly unsupported today)
│   └── ps5/                  PS5-only code (compiled solely in SDK builds)
│       ├── sce_decls.h       hand-maintained firmware declarations (provenance documented)
│       ├── display_ps5.cpp   video-out + direct-memory + flip queue
│       └── input_ps5.cpp     scePad backend
├── compat/                   Linux ABI experiment (Milestone 3)
│   ├── linux_abi.h/.cpp      syscall constants + verified FreeBSD mapping
│   ├── memory_compat.cpp     Linux brk + anonymous mmap semantics
│   ├── syscall_dispatch.cpp  Linux syscall number -> emulated op dispatch
│   └── ipc_compat.cpp        SPSC ring + Binder-style block allocator
├── android/
│   ├── sha256.cpp            FIPS 180-4 (clean-room)
│   ├── image_manager.cpp     manifest parsing + ext4 superblock probe
│   ├── runtime_manager.cpp   capability probes + honest start attempts
│   └── binder/binder_protocol.h  research notes (no implementation)
├── ui/
│   ├── launcher.cpp          menu state machine (portable)
│   └── bitmap_font.cpp       8x8 font authored in-repo + renderer
└── app/
    ├── main.cpp              host CLI: probes + ABI demo + matrix
    └── ps5_main.cpp          PS5 payload entry (SDK builds only)
```

Build rules enforced by CMake:

* `ps5droid_core` is portable; PS5-only sources are added only when the
  ps5-payload-sdk toolchain is active (`PROSPERO`).
* The host binary (`ps5droid-host`) and tests are never built for PS5,
  and the PS5 ELF is never built without the SDK toolchain.

## 3. Layering and data flow

```
+--------------------------------------------------------------+
| app/main.cpp (host)          app/ps5_main.cpp (PS5 payload)  |
+--------------------------------------------------------------+
| ui/launcher (menu model)  |  ui/bitmap_font (rendering)      |
+---------------------------+----------------------------------+
| android/runtime_manager   |  android/image_manager           |
|  - capability probes      |   - manifest + ext4 probe        |
|  - blocker reporting      |   - sha256 verification          |
+---------------------------+----------------------------------+
| compat/syscall_dispatch (Linux ABI experiment)                |
|   uses compat/memory_compat + platform::Futex                 |
+---------------------------------------------------------------+
| platform/ (memory, threads, fs, display, input)               |
|   posix backend (hosts/CI)   |   ps5 backend (Orbis payload)  |
+---------------------------------------------------------------+
```

The **single source tree** compiles for both backends; divergence points
are small and explicit (sce_decls.h, the two ps5 backend files, and
per-file `#ifdef PS5DROID_PLATFORM_PS5` sections in the platform core).

## 4. The Linux ABI experiment (Milestone 3, scope and honesty)

`SyscallDispatch::invoke(linux_nr, args)` accepts Linux x86_64 syscall
numbers and executes a curated subset:

* anonymous `mmap`/`munmap`/`mprotect` and `brk` over
  `compat::CompatMemory`, which in turn uses `platform::map_anonymous`
  (real OS mappings on both backends);
* `clock_gettime`, `nanosleep`, `sched_yield` over platform clocks;
* `getpid`/`gettid` under the documented single-process model;
* `futex` WAIT/WAKE (private) over `platform::Futex` (registry-based,
  wake-atomicity mirrors Linux's kernel-side check-and-enqueue);
* `write` to the process streams (fd 1/2);
* `exit_group` flags the context.

Everything else returns `-ENOSYS` with a *stable, explanatory note*
sourced from the syscall table. The matrix (9 emulated / 3 partial /
65 unsupported at time of writing) is data, not prose:
`ps5droid-host --matrix` prints it and tests assert its integrity.

**Scope limitation (important)**: pointer arguments are host pointers.
This experiment demonstrates *ABI translation*, not address-space
isolation or multi-process hosting. Lifting that limitation is future
work and gated on process-model research.

## 5. Runtime manager and honesty model

`android::RuntimeManager` is the only component allowed to describe
overall system status:

* `probe()` runs eight real, in-process capability probes (page size,
  map/unmap, protect, threads, futex, filesystem, IPC ring, ABI mmap)
  and reports each result. Every "[OK]" corresponds to executed code.
* `attempt_start()` performs an Android-environment start attempt. It
  **always** ends in `kBlocked` today, with structured blockers:
  `process_model`, `binder`, `filesystem_layout`, `graphics_services`,
  `bionic_runtime`. The state machine cannot reach `kRunning` — asserted
  by tests (`phase_is_never_running`).

## 6. PS5 payload path

`ps5_main.cpp` hides the splash screen, initializes 1920x1080
double-buffered video-out (direct memory, vsync flips via event queue)
and the pad, renders the launcher with the bitmap font, and exits
cleanly releasing direct memory. The firmware declarations it needs are
hand-maintained in `sce_decls.h` with provenance notes (verified against
ps5-payload-sdk stub symbols and the SDL port's working usage).

Status: **implemented, not executed on a console** — this is stated in
the header, README and every status table. The build path exists and is
documented in `development.md`; CI builds it only when explicitly
requested.

## 7. What replaces what (Waydroid mapping)

| Waydroid component | PS5Droid equivalent |
|---|---|
| LXC container manager | none — single-process emulation context |
| loop-mounted ext4 rootfs | read-only image inspection (ext4 probe) |
| binder/binderfs | in-process dispatch + SPSC ring + block allocator (start) |
| Wayland hwcomposer HAL | direct video-out + bitmap-font UI |
| PulseAudio audio HAL | nothing yet (explicitly unsupported) |
| D-Bus/systemd orchestration | RuntimeManager state machine |
| OTA images + sha256 | ImageManifest + Sha256 (reimplemented) |
| waydroid.prop | ImageManifest properties |

## 8. Roadmap (from the milestone plan)

1. **M1 done** — host foundation, tests, CI, PS5 build path (ELF-only).
2. **M2 next** — run the same probes on a real console via elfldr and
   fill the [ASSUMPTION] items in `platform-constraints.md`.
3. **M3 partially done** — anonymous-memory subset demonstrated
   end-to-end; extend toward the fd table and kqueue-backed poll layer.
4. **M4** — bionic feasibility study (linker/TLS/auxv) with the ABI
   matrix as the checklist.
5. **M5** — scoped Binder re-hosting experiment (transaction semantics
   over the block allocator).
6. **M6** — APK launch research; only after M4/M5 evidence exists.
