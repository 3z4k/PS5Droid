// PS5Droid - Binder protocol research notes.
//
// THIS HEADER CONTAINS NO IMPLEMENTATION AND NO CLAIMS.
//
// Binder is the kernel-mediated RPC system Android services require
// (servicemanager, system_server, HIDL/AIDL services, zygote...). Its
// Linux kernel driver exposes /dev/binder (or binderfs mounts), which does
// not exist on Orbis OS - a FreeBSD descendant. The Waydroid project itself
// depends on a real binder device (binderfs on the host kernel) and even
// adds a second, host-side hwbinder bridge (/dev/host_hwbinder); both are
// Linux-kernel facilities and cannot be ported as-is.
//
// Protocol facts a future experiment must respect (to be re-verified
// against AOSP sources before any implementation):
//
//   * Userspace talks to the driver with ioctl(BINDER_WRITE_READ) over two
//     command streams: BC_* (context -> driver) and BR_* (driver ->
//     context). Transactions travel as BC_TRANSACTION / BR_TRANSACTION
//     pairs, with handles for remote objects and offsets into a flat
//     buffer describing binder objects (flat_binder_object).
//   * The driver owns transaction buffers in a per-process mapped region
//     (binder_mmap) - this is why compat::BlockAllocator exists here as
//     the eventual stand-in for that model.
//   * Servicemanager performs name->handle lookups; Android frameworks
//     hard-code its presence and its semantics.
//   * bionic and libbinder embed SELinux/security assumptions; Waydroid
//     carries a long list of patches (see waydroid-patches/ in
//     android_vendor_waydroid) to disable them in a container.
//
// An Orbis "Binder" would therefore be an in-process re-implementation of
// the transaction/handle model (services as objects, marshalled calls), not
// a port of the kernel driver. That experiment is Milestone 5 and has NOT
// been started; the runtime manager reports it as a blocker.
#pragma once
