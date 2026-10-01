# Meizu M5c — Android 13 kernel

Linux 4.9.188 with the MediaTek MT6735-family platform drivers and the M5c
verified M5c stock-board device tree. The board and driver selection follows
the tested kernel configuration; chipset names are not interchangeable device IDs.

| Component | Driver | Status |
| --- | --- | --- |
| Display | MT6735 DDP/CMDQ/DSI, board LCM | Android 13 boots and renders; smoothness is still being investigated. |
| GPU | Mali Midgard r7p0 | Hardware rendering observed. Fence timing diagnostics are new and disabled by default. |
| Touch | MTK TPD, selected GT9XXTB HotKnot driver | Input wrapper present; complete physical controller acceptance remains open. |
| Wi-Fi | MediaTek WLAN gen2 and WMT/STP | Scanning observed; full connection and sleep testing remains open. |
| CPU accounting | CPUFreq core and statistics | Real time-in-state counters enabled; CPUFreq core/statistics objects compile. Kernel integration and runtime verification pending. |
| PMIC and battery | Board MediaTek power drivers | Existing telemetry retained; full charging and suspend validation remains open. |
| Bluetooth, GPS, modem | WMT/STP and board transport drivers | Source present; complete runtime acceptance remains open. |
| Audio, camera, sensors | Board MediaTek drivers | Source present; complete runtime acceptance remains open. |

## Build

Use an AArch64 Android GCC 4.9 toolchain. The tested compiler reports
`4.9 20150123`; newer toolchains require separate validation. Use an open
host `dtc` supporting `-@` (verified version 1.6.1). The stock-board hardware
description is included as text in `mediatek/m5c-stock.dts`: recompilation
reproduces the accepted stock DTB byte for byte. The donor-generated
`k37mv1_bsp_k49` DTS remains separate and is not the M5c deployment target.
No proprietary DCT executable is needed.

The matching public toolchain is AOSP `aarch64-linux-android-4.9`, commit
`6ca6746f5fafd7d1b162451dce2ae40741529e9e` (`android-7.0.0_r1`). Select
its `ld.bfd` explicitly; its default `ld` differs from the tested linker.

```sh
mkdir -p out
cp arch/arm64/configs/m5c_a13_verified.config out/.config
make O="$PWD/out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- \
    LD=aarch64-linux-android-ld.bfd \
    TARGET_BUILD_VARIANT=userdebug HOSTCFLAGS=-fcommon olddefconfig
make O="$PWD/out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- \
    LD=aarch64-linux-android-ld.bfd \
    TARGET_BUILD_VARIANT=userdebug HOSTCFLAGS=-fcommon -j4 Image.gz-dtb
```

The output contains the compressed kernel followed by
`arch/arm64/boot/dts/mediatek/m5c-stock.dtb` (expected SHA-256
`671d3410a1cc33eaf22917d1c5408713c1dbf6ec1c6672f77d309408c22c5c6f`). A successful compilation is
not a flashable-ROM or hardware-acceptance claim. Check the generated configuration,
linked drivers and board DTB before packaging with the device's own boot ramdisk.
The upstream kernel documentation and licensing are retained in `README` and `COPYING`.
