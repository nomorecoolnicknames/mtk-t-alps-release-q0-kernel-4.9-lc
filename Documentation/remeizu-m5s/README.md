# M5s custom Linux 4.9: source used by the A4 compile candidate

This is the actual E0 kernel source plus the reviewed Yassy, own-board MSDC
binding and DDP physical-address patches. The kernel code matches that source
recipe; only the operational journal was replaced with a public record and
these evidence documents were added. See `source-provenance.json` for source,
patch and toolchain pins, and `cloud-a4-compile-proof.json` for the accepted
compile result and hashes.

The cloud A4 run completed `vmlinux` and generated `Image.gz` from the pinned
Kbuild objcopy/gzip path, with linked Yassy, MSDC and DDP checks. It did not
build a DTB or a boot image. The historical appended-DTB target still needs
its missing board-generation input. Do not infer bootability from the image
or bypass that missing input with a donor/dummy DTB.

The next gates are the own-board touch driver, TVDPLL/SMI/route/clock/PMIC/CPU
and bootloader resource checks, correct boot-image packaging, partition
readback and runtime tests. No flash was performed for this publication.

No toolchain or device firmware binaries are newly published here. The full
BSP inherits its existing public source and notices; this is not a blanket
licensing assertion. Hosting proposals should name the specific reviewed
source/test jobs and keep proprietary device inputs outside that job set.
