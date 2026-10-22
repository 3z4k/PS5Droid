# Binder research: internal RPC contract

`BinderEngine` retains its research name and is a **bounded synchronous in-process
RPC engine**. It does not implement Android Binder IPC. In particular, it does not
provide `/dev/binder`, `BINDER_WRITE_READ`, BC/BR command streams, Parcel/object or
file-descriptor transfer, refcount/death notifications, one-way dispatch or an
Android ServiceManager. The previous invented command-number enums were removed.

## Implemented behavior

| Operation | Behavior |
| --- | --- |
| Register | Nonempty name and valid handler required; duplicate names retain their existing service |
| Reserved handle | Handle zero has no success stub; `servicemanager` cannot be registered |
| Missing lookup | Returns `BinderEngine::kInvalidHandle`, distinct from zero |
| Transaction | Copies input into an exclusively owned allocation; executes a synchronous callback |
| Limits | 256-byte input/reply limit, fixed allocation region and maximum active transaction count |
| Failures | Typed invalid-handle, unsupported-flags, oversized-payload, capacity/allocation and handler-error results |
| Legacy boolean API | Clears output first and succeeds only for `RpcStatus::kOk` |

## Ownership and concurrency

The engine owns its service registry and transaction buffers. One mutex protects
registry/allocation changes. It is released before callbacks run, allowing nested
calls and simultaneous handlers without an engine-wide callback lock. Each call
has a private staging block released through RAII, including exception paths.
The former unsynchronized allocator/SPSC completion sequence is removed.

The caller owns `user` data and must keep it alive until all transactions finish.
Handlers must synchronize their own shared data. Engine destruction requires all
calls to have completed. There is no unregister API, asynchronous lifetime model
or cancellation guarantee.

Tests cover invalid/reserved handles, rejected flags, oversized data, allocation
exhaustion, exception cleanup, nested calls with sufficient/insufficient capacity,
and eight concurrent callers. ThreadSanitizer runs this behavior in CI.

Android compatibility remains a separate implementation/research gate. Its protocol
must be validated against [AOSP Binder](https://source.android.com/docs/core/architecture/hidl/binder-ipc)
and actual libbinder clients before any compatibility claim is made.
