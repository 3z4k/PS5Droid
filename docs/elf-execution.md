# Cooperative ELF execution experiment

The experiment maps and executes a **trusted, freestanding x86-64 ELF fixture**.
LLVM compiles it with `--target=x86_64-linux-android28`; LLD emits ET_DYN with a
single RX load segment. The sample does not link bionic. It is not an APK launch,
an unmodified Android executable, an instruction interpreter or a general loader.

## Reproduce

```sh
python3 tools/build_elf_fixture.py --clang clang --linker ld.lld \
  --output build/fixtures/cooperative.elf
./build/ps5droid-host --run-cooperative-elf build/fixtures/cooperative.elf
```

Use upstream LLVM's `clang`, not Apple Clang, to build the Android-target object.
The runner must be an x86-64 process. An ARM host build returns an explicit
unsupported result. Building this existing x86-64 experiment does not add an
ARM translation strategy or narrow the project's intended application scope.

The CLI requires all three conditions: entry executed, `exit_group` requested,
and entry returned zero. It returns a nonzero exit status if any condition fails.
The ordinary runtime manager continues to report Android startup as blocked.

## Entry contract

```c
typedef long long (*gateway_fn)(void* context, int linux_nr,
                                const unsigned long long args[6]);
long long entry(void* context, gateway_fn gateway,
                const unsigned char* staged_initial_stack);
```

The entry is a normal SysV C function and returns to the runner. It receives
Linux-shaped `argc`/`argv`/`envp`/auxv bytes as its third argument; the runner does
not replace the process's live stack or thread pointer. The fixture checks its
staged argument, requests `getpid`, writes its message and requests `exit_group`.
The callback accepts those three operations and converts failures to `-errno`.

## Validation and limits

- ELF64 little-endian x86-64, valid header sizes, bounded program table and segment ranges.
- Correct subtraction-based bounds checks, unaligned-input support and validated entry coverage.
- Runner requires ET_DYN, nonoverlapping host-page-aligned segments and a mapped program table.
- Load image capped at 64 MiB. Argument/environment staging capped at 1 MiB and 4,096 strings per list.
- Pages transition from writable loading memory to final permissions. Writable/executable segments fail.
- Dynamic segments, interpreter segments and TLS execution fail explicitly; relocation processing is absent.
- Executable segments containing raw `0f 05` bytes are conservatively rejected. This is not instruction decoding or a syscall interceptor.
- The stack contains actual strings and valid staged addresses; auxv contains mapped program-header/entry addresses and the measured page size.
- The image, callback and host pointers share a process. **This is not a sandbox. Run only the authored fixture or equally trusted cooperative code.**

A complete bionic startup additionally needs TLS/thread-pointer coordination,
full auxiliary-vector requirements, dynamic linking/relocations, Android filesystem
layout, signals and a validated raw-syscall or rebuilt-libc strategy. These remain
explicit blockers rather than simulated successes.

`--report-json ... --probe-exec` separately checks whether RW → RX protection is
accepted. That result does not prove instruction execution, JIT support or ART
startup. Console execution remains unverified until hardware evidence is recorded.

Format references: [System V ELF ABI](https://gabi.xinuos.com/elf/07-pheader.html),
[Android ELF TLS](https://android.googlesource.com/platform/bionic/+/HEAD/docs/elf-tls.md).
