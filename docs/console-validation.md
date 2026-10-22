# Console validation and evidence

> **Hardware execution is pending.** This document is a procedure and report
> contract, not a record of a successful PS5 run.

## Before deployment

Use a console already capable of running an ELF loader. Record its firmware,
exploit/loader name and version, SDK release, exact source revision and ELF SHA-256.
Do not infer support for another firmware from one successful run. Use the
open-source SDK and loader documentation for the actual supported setup.

Build as described in [development.md](development.md), then deploy:

```sh
export PS5_HOST=your-console-address
"$PS5_PAYLOAD_SDK/bin/prospero-deploy" -h "$PS5_HOST" -p 9021 \
  build-ps5/ps5droid.elf
```

## What the payload records

The payload probes platform behavior using unique temporary files in `/data` and
writes `/data/ps5droid-evidence.json` before video initialization, after subsequent
initialization stages, and after a normal launcher exit. Failure to persist the
report is logged; it must not be mistaken for an empty successful report.

| Field / observation | Interpretation |
| --- | --- |
| `schema_version` | Report format version, currently 1 |
| `backend` | Distinguishes host and console runs |
| `run_id`, `architecture`, `compiler` | Per-probe process/time identifier and actual build architecture/compiler |
| `revision` | Configured source revision, with `-dirty` when applicable |
| `sdk` | Explicit build pin, or `unrecorded` |
| `firmware`, `loader` | `unrecorded` until supplied by the tester |
| `platform_probes_passed` | Only the aggregate platform probe result |
| `rx_memory` | Native RW → RX protection accepted/refused; no JIT or ART claim |
| `video_init`, `pad_init` | Actual initialization return results |
| `video_flips` | Count of successful submit/wait operations; failed waits stop the loop |
| `connected_pad`, `button_edges` | Observed connection polls and navigation/exit edges |
| `cleanup` | Whether shutdown was reached; resource release requires external/repeat-launch checking |
| `android_runtime` | `blocked`, regardless of lower-level probe success |

API declarations and struct layouts remain subject to hardware validation. A
failed RX probe may be a genuine privilege limitation; preserve it as a failure.
The payload performs no kernel privilege changes or JIT permission workaround.

## Hardware procedure

1. Save a copy of the report immediately after startup if available.
2. Confirm stable video output and open the probe screen. Exercise D-pad, Cross,
   Circle and Options; note disconnect/reconnect behavior separately.
3. Let multiple frames present, then exit through Options.
4. Retrieve the report with your existing console file-transfer tooling. A report
   left from a previous process is not evidence for a new run; compare the embedded
   `run_id`, revision and externally recorded run identifier/time.
5. Relaunch at least twice and verify that display and input initialize again.
   Observe available memory/resource diagnostics if your setup provides them.
6. Preserve logs, screenshots, failed operations and any missing observations.

Use the annotation tool on the copied report; it refuses a host report and keeps
the original measurements intact:

```sh
python3 tools/annotate_console_report.py copied-console-report.json \
  --firmware YOUR_VERSION --loader 'loader-name VERSION' \
  --elf build-ps5/ps5droid.elf --run-id YOUR_RUN_ID \
  --output console-report.annotated.json
```

The annotations record tester-supplied provenance, not automatically measured
firmware or loader detection. Add repeat-launch/cleanup observations to a separate
run note and link both artifacts in the PR or issue. Do not commit console
addresses, credentials or unrelated private information.

## Host report for comparison

```sh
./build/ps5droid-host --report-json build/host-evidence.json --probe-exec
```

This checks the schema and host implementation. It is never eligible for promotion
to console evidence. No verified console report is shipped with this change.
