# PS5 platform constraints

Evidence has three distinct levels:

- **Source:** an API/signature appears in a cited public SDK or working port.
- **Host:** this repository executes a corresponding host behavior/test.
- **Console:** a PS5 run records the result for a firmware/loader/build tuple.

**Console evidence is pending.** Source availability and a cross-build are not
proof of runtime behavior or privilege availability.

## CPU, kernel and executable boundary

The open-source [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) targets
x86-64 and a FreeBSD-derived userspace/kernel ABI. Public SDK sources are the
reference for syscall numbers and native payload loading. Its installed toolchain
is `$PS5_PAYLOAD_SDK/toolchain/prospero.cmake`.

Linux and FreeBSD syscall numbers differ. For example, Linux x86-64 `read=0`,
`write=1`, `mmap=9` correspond to the project's source-verified FreeBSD mapping
`3`, `4`, `477`. `src/compat/linux_abi.cpp` contains the verified mapping subset;
unknown mappings are never guessed. Direct Linux syscall instructions cannot be
made compatible merely by mapping an ELF image.

The cooperative execution experiment explicitly replaces syscall stubs with a
callback and has a [limited entry contract](elf-execution.md). Unmodified bionic
TLS, dynamic linking, relocations, raw-syscall routing, signals and a full process
model remain unresolved.

## Memory and synchronization

Anonymous mapping and protection have host tests. Guest `mmap` currently accepts
private anonymous non-executable mappings. Alignment uses the backend's measured
page size; rounding and range arithmetic are overflow-checked. Whole-mapping
unmap is supported; arbitrary interior unmaps are not.

Guest requests for `PROT_EXEC` fail rather than silently succeeding without
executable permission. The separate opt-in host RX probe and the console payload
report whether native RW → RX protection is accepted. That report is not evidence
of JIT execution or ART startup.

Thread creation and mutex/condition-variable behavior are host-tested. The futex
experiment supports private wait/wake, not the complete Linux futex ABI or signal
semantics. A real console must validate page sizes, thread behavior and privileges.

## Files, flags and readiness

Linux open flags, errno, `stat` and packed `epoll_event` layouts must be translated
explicitly. The guest descriptor layer shares open-file lifetimes across `dup`,
uses native positioned reads, and preserves file positions. File paths are host
paths; no guest rootfs or address-space isolation is provided.

`openat` supports absolute paths or `AT_FDCWD`; relative non-current-directory
fds are rejected. Eventfd supports counter/semaphore modes, nonblocking errors,
overflow checks and blocking wakeups. Epoll supports level-triggered eventfd
readiness, deadlines and wakeups. Nested epoll, edge/one-shot modes, files,
pipes and sockets are outside this implementation's readiness contract.

The live `./build/ps5droid-host --matrix` output is authoritative for support
labels. Partial operations remain partial even when their supported subset passes.

Android images can be inspected read-only but are not mounted. APKs can be
inspected without mounting an image. Android filesystem layout/services remain
absent; inability to mount ext4 is not proof that userspace extraction is impossible.

## Binder and Android services

An internal [synchronous RPC experiment](binder-scope.md) is available. It does
not implement Android Binder wire compatibility, a Binder device, ServiceManager,
zygote, system_server or Android graphics services. Those remain startup blockers.

## Display, input, audio and packaging

Firmware declarations in `src/platform/ps5/sce_decls.h` cite public SDK symbols
and the [SDL PS5 port](https://github.com/ps5-payload-dev/SDL). Video-out uses
GPU-visible direct memory and flip events; DualSense input uses `scePad`.
These paths remain experimental until a hardware run is recorded.

The event-wait timeout is a pointer as declared in the SDL port, not an integer
value. The backend bounds flip waits, tracks allocation ownership independently
of physical address zero, unmaps its view, and closes input on shutdown. API
return/error behavior and repeat-launch cleanup still require console validation.

The project currently has no Android GPU integration, SurfaceFlinger services,
audio output backend or PKG packaging. The CPU framebuffer launcher does not
establish Android rendering support. Public API stubs/headers alone do not
establish working GLES or Vulkan drivers.

## Console test requirements

Record firmware, exploit/loader/version, SDK pin, exact revision and ELF checksum.
Run probes, exercise controller navigation and repeated flips, exit, then relaunch.
Preserve failures and unrecorded observations. Follow the
[hardware evidence procedure](console-validation.md); do not convert host or CI
results into console status labels.
