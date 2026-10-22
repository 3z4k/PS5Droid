# Validation record — 2026-10-22

This is a **local development validation record**, separate from console evidence.
The machine was macOS on ARM64. The x86-64 host build ran as an x86-64 process
under Rosetta. No PS5 hardware was connected.

| Check | Observed result |
| --- | --- |
| ARM64 host suite, Apple Clang 21 | 78/78 registered tests passed |
| x86-64 host suite, Apple Clang 21 | 78/78 registered tests passed |
| ARM64 AddressSanitizer + UndefinedBehaviorSanitizer | Suite passed; no reported sanitizer errors |
| ARM64 ThreadSanitizer | Suite passed; no reported data races |
| Trusted cooperative ELF | Entry executed, gateway write succeeded, exit requested, return value 0 |
| Seeded ELF/text-manifest fuzz smoke, LLVM 23.1.2 | 10,000 executions completed without a crash |
| AAPT2 fixture comparison | Actual package `org.ps5droid.inspection`, minimum SDK 28, target SDK 35 |
| Host JSON report contract | Schema/backend/architecture/compiler/revision/run identifier validated |
| Console report annotation guard | Host report rejected; original measurements preserved |
| PS5 cross-build | `ps5droid.elf` linked using SDK v0.45 and upstream Clang/LLD 23.1.2 |
| SDK archive | SHA-256 matched `.github/ps5-sdk.json` |
| Formatting and Markdown links | Checks passed |
| Linux native differential test / GCC build | Configured in CI; not executed locally |
| Fork GitHub Actions | [Jobs did not start](https://github.com/3z4k/PS5Droid/actions/runs/39769366161); no CI pass claimed |
| PS5 hardware | Pending; no verified console result |

Architecture-dependent runner rejection tests execute in the x86-64 suite. The
Linux differential test is compiled only on Linux. These counts do not imply that
both cases ran on ARM64 macOS.

The cross-build used the unmodified public SDK archive, its supplied CMake
toolchain/wrappers and pinned zlib 1.3.2 source. A local `LLVM_CONFIG` host-tool
selection supplied upstream Clang/LLD; no SDK declarations or success results were
substituted. CI's Ubuntu/Clang 18 configuration remains unexecuted while fork
runners are unavailable.

## Cooperative execution output

```text
PS5Droid cooperative ELF: entry -> write -> exit_group
executed=yes stop_requested=yes entry_result=0
cooperative ELF entry returned; unmodified bionic/Android startup remains unsupported
```

This proves the cooperative entry/gateway path under the tested host configuration.
It does not establish unmodified bionic, Android app startup, Binder compatibility,
JIT/ART operation or execution on PS5 hardware.

Reproduce using [development.md](development.md) and
[elf-execution.md](elf-execution.md). Hardware runs must follow the separate
[console procedure](console-validation.md) and preserve firmware/loader provenance.
