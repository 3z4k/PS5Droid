# Third-Party Notices

PS5Droid ships no third-party code in its source tree today. This file
records the upstream projects used as references, their licenses, and the
terms that would apply if code were ever imported.

## Reference-only projects (no code imported)

### Waydroid

* Repository: https://github.com/waydroid/waydroid
* License: **GPL-3.0-or-later** (repo `LICENSE`).
* Used for: architectural analysis only (`docs/waydroid-analysis.md`).
  If any code is ever imported, it must keep its copyright headers and be
  redistributed under GPL-3.0-or-later with full corresponding source.

### android_vendor_waydroid

* Repository: https://github.com/waydroid/android_vendor_waydroid
* License (observed in `LICENSES/`): **GPL-3.0** plus a
  "Navotpala-Tech-GPL-exception" document and a NOTICE stating that
  closed-source use requires a commercial license from the Waydroid
  project.
* Used for: analysis only. Any import additionally requires honoring the
  exception text and the commercial-use NOTICE; treat as restricted.

### android_hardware_waydroid / android_device_waydroid_waydroid

* Repositories under https://github.com/waydroid/
* License: **none found** at repository root (GitHub license detection
  empty). These repositories are treated as all-rights-reserved; no code
  may be imported from them.

### ps5-payload-sdk

* Repository: https://github.com/ps5-payload-dev/sdk
* License: **GPL-3.0-or-later**, except `include/freebsd` which is BSD
  (per the SDK README).
* Used for: verified platform facts (syscall numbers in
  `crt/syscall.h`, stub symbol lists in `sce_stubs/`, libc surface).
  Function/constant *values* of a firmware ABI are facts; PS5Droid does
  not copy SDK files. If SDK code were ever needed, GPL-3.0 terms would
  apply to that code.

### ps5-payload-elfldr

* Repository: https://github.com/ps5-payload-dev/elfldr
* License: **GPL-3.0-or-later** (repo `LICENSE`).
* Used for: understanding the payload loading process (README, cited in
  docs). Not imported.

### ps5-payload-dev/SDL (PS5 SDL port)

* Repository: https://github.com/ps5-payload-dev/SDL
* License: **zlib**.
* Used for: verified usage patterns of Orbis APIs (video-out, direct
  memory, pad). PS5Droid's `src/platform/ps5/sce_decls.h` mirrors the
  *signatures and struct layouts* needed to call the same firmware
  entry points; it is not a derivative of SDL code. Should any SDL code
  be imported later, zlib terms (copyright + notice retention) apply.

### AOSP / Linux kernel UAPI facts

* Binder protocol knowledge (`src/android/binder/binder_protocol.h`) is
  documented as facts (command constants, driver model) with pointers to
  upstream sources; no code copied. The UAPI headers are GPL-2.0 WITH
  the Linux-syscall-note; AOSP frameworks are Apache-2.0. Any future
  import must pick the matching license path.

### ext4 / SHA-256

* ext4 superblock offsets in `image_manager.cpp` are facts of the
  on-disk format (documented in the header).
* `sha256.cpp` is a clean-room implementation from FIPS 180-4, validated
  against the standard NIST test vectors.

## Authored in this repository

* `src/ui/bitmap_font.cpp` — 8x8 font glyphs authored for PS5Droid
  (no third-party font data).
* Test framework `tests/test_framework.h` — authored for PS5Droid.

## Policy

Any future vendored dependency must add an entry here, keep upstream
copyright notices, and record the commit/version imported. Proprietary
material (Sony SDKs, console keys, Google Play Services, Widevine,
proprietary codecs) is never accepted.
