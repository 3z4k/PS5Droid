# Waydroid Upstream Analysis

Status: research document, verified against upstream source on 2026-10-18.
Sources were inspected by shallow-cloning each repository (commit state of
that date); file paths below refer to those checkouts. Nothing in this
project imports Waydroid code today; this document maps what exists, what
it depends on, and what could realistically be reused or adapted.

## 1. Repository inventory

| Repository | Default branch | Language | License (observed) |
|---|---|---|---|
| `waydroid/waydroid` | `main` | Python | **GPL-3.0** (LICENSE at repo root) |
| `waydroid/android_vendor_waydroid` | `lineage-20` | Shell/Make | **GPL-3.0** + "Navotpala-Tech-GPL-exception-1.0" + commercial-license notice (see `LICENSES/`) |
| `waydroid/android_hardware_waydroid` | `lineage-20` | C/C++ | **No repository-level license found** (no LICENSE/COPYING/NOTICE at root; GitHub API reports none). Treat as all-rights-reserved until verified per-file. |
| `waydroid/android_device_waydroid_waydroid` | `lineage-20` | Makefile | **No repository-level license found** (same caveat). |

Licensing notes:

* The vendor repo's `LICENSES/LICENSE.md` states GPL-3.0 and adds a NOTICE:
  closed-source use requires a commercial licensing agreement with the
  Waydroid project. There is a separate `Navotpala-Tech-GPL-exception`
  file that must be read before any code import.
* The two Android-side repos without a root license cannot be copied into
  PS5Droid at all under current findings. Reading them for architectural
  understanding is fine; redistributing their code is not.
* Waydroid's main repo README documents it as a container-based approach to
  boot a full Android (LineageOS, currently Android 13 base) system on a
  regular GNU/Linux system.

## 2. How Waydroid actually works (source-verified)

### 2.1 Orchestration layer (`waydroid/waydroid`, Python)

* `waydroid.py` + `tools/` form a Python CLI and a D-Bus service
  (`dbus/id.waydro.Container.service`, systemd unit in
  `systemd/waydroid-container.service`). Container lifecycle
  (start/stop/freeze/unfreeze) is exposed over D-Bus and implemented in
  `tools/actions/container_manager.py`.
* The session layer (`tools/actions/session_manager.py`) runs per user,
  requires a host **Wayland compositor** (it checks the
  `WAYLAND_DISPLAY` socket and errors out when absent) and talks to
  Android services over binder via gbinder-based Python bindings.
* Image provisioning (`tools/helpers/images.py`): fetches an OTA JSON
  manifest from a channel URL, downloads a zip, verifies **SHA-256**
  (`sha256sum` compared to the manifest `id`), extracts into
  `images_path`, and later `mount_rootfs()` loop-mounts `system.img` at
  the rootfs and `vendor.img` at `rootfs/vendor`, bind-mounts host EGL
  driver dirs (`/vendor/lib*/egl`) into the container, and bind-mounts a
  generated `waydroid.prop`.

### 2.2 Container technology (`data/configs/config_base`, `tools/helpers/lxc.py`)

The LXC config template shows the Linux facilities Waydroid requires:

* `lxc.rootfs.path` over a loop-mounted ext4 rootfs
* a long `lxc.cap.keep` list (`sys_admin`, `net_admin`, `sys_ptrace`, ...)
* `lxc.mount.auto = cgroup:ro sys:ro proc` (host cgroup/sys/proc mounts)
* a seccomp profile (`data/configs/waydroid.seccomp`) and AppArmor
  profiles (`data/configs/apparmor_profiles/{lxc-waydroid,adbd,android_app}`)
* a post-stop hook script and host networking glue
  (`data/scripts/waydroid-net.sh`)

### 2.3 Android-side glue (`android_vendor_waydroid`)

The vendor tree carries a large patch set (`waydroid-patches/base-patches-30|33/...`)
against AOSP components, which is the clearest map of the Android/kernel
assumptions being papered over:

* `bionic/` — linker path additions, text relocations, seccomp additions
* `system/core/` — init running **inside LXC without SELinux**, ueventd
  disabled, FUSE sdcard, memfd property, capability drops for zygote
* `frameworks/native/` — SurfaceFlinger/EventHub patches for **Wayland
  input**, halium binder permission bypasses, installd without SELinux
* `system/libhwbinder/` + `system/libhidl/` — support for a
  **`/dev/host_hwbinder`** bridge device, so Android HAL processes can
  call host-side binder services (the device tree's
  `configs/init.waydroid.vendor.rc` even stops the gralloc HAL when that
  device exists)
* `frameworks/base/` + `lineage-sdk/` — WayDroid system services
  (clipboard, hardware, user monitor, notifications) that proxy to the
  host over that bridge

### 2.4 Hardware abstraction (`android_hardware_waydroid`)

HAL implementations that make Android believe it runs on real hardware:

* `hwcomposer/` — a **Wayland client**: `wayland-hwc.cpp` +
  custom `wayland-android.xml` protocol + `egl-tools.cpp` render each
  Android surface as a Wayland surface of the host compositor
* `audio/audio_hw.c` — ALSA PCM over the **PulseAudio** plugin
  (`snd_pcm_open(&pcm, "pulse", ...)`), with `PULSE_RUNTIME_PATH` taken
  from the `waydroid.pulse_runtime_path` property
* `gralloc/` (ashmem forced via vendor patches), `sensors/`, `lights/`,
  `power/`, `health/`, `gatekeeper/`, `vibrator/`, `memtrack/`

### 2.5 Device shapes (`android_device_waydroid_waydroid`)

Per-architecture boards: `waydroid_arm`, `waydroid_arm64`,
`waydroid_arm64_only`, `waydroid_x86`, `waydroid_x86_64` and TV variants,
each with `BoardConfig.mk` + product makefiles, plus shared `configs/`
(media codecs/policies, `init.waydroid.vendor.rc`, empty VINTF manifest).

## 3. Dependency classification

| Facility | Used by | Present on Orbis OS (PS5) |
|---|---|---|
| Linux namespaces (pid/net/mnt/ipc/uts/user) via LXC | container isolation | No — FreeBSD kernel, no LXC |
| cgroups | `lxc.mount.auto`, app scheduling | No |
| loop devices + ext4/ext4 overlayfs | image mounting | No — no loop/FUSE/ext4; internal disk is UFS |
| binder / binderfs kernel driver (`/dev/binder`) | all Android IPC | No — no binder in Orbis kernel |
| host_hwbinder bridge | vendor HAL bridge | No (and needs a kernel module + host services) |
| ashmem, memfd | gralloc, media | No |
| Wayland compositor | hwcomposer + input | No — PS5 video-out is a direct framebuffer flip |
| PulseAudio / ALSA pulse plugin | audio HAL | No — Orbis audio is libSceAudioOut |
| D-Bus + systemd | host-side services | No |
| AppArmor, seccomp, minijail | sandboxing | No (Orbis uses its own privilege model) |
| dnsmasq/net scripts | container networking | No — Orbis networking is SceNet/BSD sockets |
| OTA JSON + SHA-256 + zip | image distribution | **Portable** (pure logic) |
| `waydroid.prop` property convention | host/container config | Portable as a concept (plain properties) |

## 4. Reuse assessment for PS5Droid

**Directly reusable (license permitting, concept-level):**

* OTA manifest + SHA-256 verification flow (`tools/helpers/images.py`) —
  reimplemented natively in `src/android/image_manager.cpp` + `sha256.cpp`
  (no Waydroid code copied).
* Image metadata conventions (system.img / vendor.img split,
  `waydroid.prop` keys) — documented and used in our manifest format.
* Knowledge of *which* Android components need *which* kernel facilities
  (the patch set in `android_vendor_waydroid`) — used throughout
  `docs/platform-constraints.md`.

**Adaptable with major rework:**

* The "HAL bridging" idea: Waydroid bridges Android HALs to host services
  over hwbinder; PS5Droid would need an equivalent *in-process* service
  layer (no kernel mediation available).
* Input injection: Waydroid feeds synthetic evdev/wayland input into
  EventHub; PS5Droid would map `scePad` state into Android input events
  inside the emulated environment.
* Image handling: our read-only ext4 superblock probe replaces Waydroid's
  loop-mount step; a full read-only ext4 parser would be the next step.

**Not reusable on Orbis (hard blockers):**

* LXC container manager, cgroup/sys/proc mounts, seccomp/AppArmor profiles
* binder/binderfs and the host_hwbinder bridge (kernel drivers)
* the Wayland-based hwcomposer HAL and gralloc stack
* PulseAudio/ALSA audio HAL
* systemd/D-Bus orchestration and dnsmasq networking

## 5. Compliance rules adopted by PS5Droid

1. No Waydroid code is imported today; this document is analysis only.
2. If any component is ever imported, it will be added as a documented git
   submodule or vendored copy WITH its license file, copyright headers and
   a note in `THIRD_PARTY_NOTICES.md`, and the GPL obligations (including
   the vendor repo's commercial-use NOTICE) will be honored.
3. Repos without a discoverable license (`android_hardware_waydroid`,
   `android_device_waydroid_waydroid`) are **not** candidates for code
   import at all.
