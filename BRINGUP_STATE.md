# Meizu M5s / MT6753 custom Linux 4.9 source publication

FACT (2026-09-29): this public lane starts from the existing M5c public
ancestor 56ec99e970f1a8da2f2a42bae0ef9536ca766962 and imports the exact E0
kernel source at ba33cd9dbb88995da0dbd5cc6c8c67d653d686c1. Only the private
operational BRINGUP_STATE journal is replaced by this public state record.
The private history and worktrees are preserved. No unrelated baseline is
claimed to be the compiled M5s kernel.

FACT: the connectivity restore in E0 reuses objects already reachable from
that public ancestor before its revert; publication does not add proprietary
firmware packages, boot images, ROM archives, toolchain binaries or captures.
Per-file upstream licenses apply; this statement does not relicense the BSP
or assert that every old upstream object has a resolved licensing status.

The reviewed source series below converges on the kernel code used for the
cloud A4 compile-only run. That run produced vmlinux, Image.gz and selected
objects with hash receipts. It built no DTB and no boot.img; flash readiness
and device runtime remain false. Evidence is added with the final patch.

## Imported source changes

- E0: M5s defconfig/device marker, 3 GB marker windows and out-of-tree DTC include fix.
