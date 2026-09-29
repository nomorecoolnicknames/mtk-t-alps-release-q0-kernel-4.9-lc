# Meizu M5s kernel

Linux 4.9 development tree for the MT6753-based Meizu M5s, including the Yassy
panel, MSDC binding and display register map changes.

Use `arch/arm64/configs/m5s_yassy_defconfig` with the AOSP AArch64 GCC 4.9
toolchain at commit `7a28c220c2e9001825328dca6188ef0077a80a88`.
The compiler's Python wrapper requires Python 3 on current build hosts.

Kernel compilation has been checked; board DTB generation and boot-image
integration remain incomplete. This branch is not ready to flash.
