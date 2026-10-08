# Meizu M5c — LOS20 kernel baseline

Linux 4.9.188 for the Meizu M5c / MT6737. This branch preserves the runtime
source and configuration selected by the September 30, 2026 Android 13 ROM
kernel. The primary [m5c-4.9-a13 branch](https://github.com/nomorecoolnicknames/mtk-t-alps-release-q0-kernel-4.9-lc/tree/m5c-4.9-a13)
continues separately with newer fence instrumentation, CPU accounting and
build improvements. This baseline does not replace that work.

| Component | Driver / interface | Status |
|---|---|---|
| Display | MT6735 DDP/CMDQ/DSI, M5c panel | Earlier Android 13 boot/rendering observed; smoothness needs work |
| GPU | Mali Midgard r7p0 | Hardware rendering observed on the baseline; no performance claim |
| Input | MediaTek TPD and board controller | Physical controller and full gesture acceptance remain open |
| Wi-Fi | MediaTek WLAN gen2, WMT/STP | Earlier scanning observed; association and traffic need verification |
| Interrupts and power | CIRQ, SPM, CPUFreq | CIRQ match/ack corrections included; deep power states gated by default |
| Bluetooth, GPS and modem | Board transport and vendor interfaces | Complete runtime acceptance pending |
| Audio, camera and sensors | Board drivers and vendor interfaces | Complete runtime acceptance pending |

`forge_spm_lowpower=0` deliberately gates suspend, deep idle and SODI while
PCM firmware can still load. It is a bring-up limitation, not a working
low-power implementation. The baseline does not include the primary branch's
later disabled-by-default fence diagnostics or CPU_FREQ_STAT selection.

## Build inputs

The exact generated configuration is provided as
`arch/arm64/configs/m5c_a13_rom.config` (SHA-256
`07a6d72905dab646ba0015f8d22ac93a4f244f3789c438cd2637fd529465e323`).
Use the AOSP Android GCC 4.9 toolchain at
[6ca6746f5fafd7d1b162451dce2ae40741529e9e](https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/+/6ca6746f5fafd7d1b162451dce2ae40741529e9e/),
with its `ld.bfd`; the tested compiler reports `4.9 20150123`.

```sh
mkdir -p out
cp arch/arm64/configs/m5c_a13_rom.config out/.config
make O="$PWD/out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- \
    LD=aarch64-linux-android-ld.bfd TARGET_BUILD_VARIANT=userdebug \
    HOSTCFLAGS=-fcommon olddefconfig
make O="$PWD/out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-android- \
    LD=aarch64-linux-android-ld.bfd TARGET_BUILD_VARIANT=userdebug \
    HOSTCFLAGS=-fcommon -j4 Image
```

Build `Image` and append the board's original stock DTB, rather than the donor
DTS selected by the historical defconfig. The stock-board description is also
included as text at `arch/arm64/boot/dts/mediatek/m5c-stock.dts`; its DTB must
match SHA-256 `671d3410a1cc33eaf22917d1c5408713c1dbf6ec1c6672f77d309408c22c5c6f`.
Generated board files and the stock description retained from the primary branch
are supporting inputs. They do not change the baseline's compiled runtime sources.
No binary kernel, firmware, signing key or device capture is included.

Source/configuration correspondence was checked against the retained ROM
kernel inputs. This publication does not establish a new kernel rebuild,
flash/readback, complete hardware acceptance or daily-driver readiness.
Original authors, copyright notices and GPL licensing in [COPYING](COPYING)
are preserved; upstream notes remain in [README](README).

## Updated integration

This source selection adds guarded CPU idle and suspend handling, cpufreq and watchdog recovery support, CMDQ compatibility, camera power/OTP ioctl corrections and the S5K4H8 sensor LSC correction. It also contains charger and battery integration changes. These source changes do not establish deep sleep, complete camera operation or thermal safety. Stock-derived calibration and other restricted board inputs are supplied separately; this subset is not a complete standalone source checkout. Matching Android device sources are maintained on the M5c `lineage-20-treble` branch.
