# Meizu M3 Note — unified Linux 4.9 sources

Linux 4.9 diagnostic source baseline for M681 and L681. Both revisions target
one kernel core with separate board DTBs. Android 13 / LineageOS 20 is the
bring-up target. The primary [mt6755-4.9 branch](https://github.com/nomorecoolnicknames/mtk-t-alps-release-q0-kernel-4.9-lc/tree/mt6755-4.9)
retains the separate component integration work.

This branch exports the publishable source changes selected for the October 5,
2026 unified kernel build. It is a partial source export, not a complete
corresponding-source package or an independently buildable ROM release. The
selected compile used additional touch-controller and board-generation inputs
and a per-unit factory identity table. These inputs are not distributed here;
the public table is empty. The published source therefore cannot reproduce the
selected boot images byte-for-byte or admit those physical boards unchanged.

| Component | Driver / interface | Source status and remaining work |
|---|---|---|
| Board selection | `m3note_board.c`, `m3note_factory.h` | Shared M681/L681 identity gates; public factory table empty |
| Display / backlight | MT6755 DDP, ILI9885 and HX8399 panels | Both board descriptions and panel source included; complete device acceptance pending |
| Keys | `m681-keys.c`, MediaTek KPD | M681 read-only key poller and stock `mtk-kpd` input name included |
| Charging / battery | MT6755 AUXADC, PMIC wrapper and charge observer | M681 observer selected only for an admitted M681; L681/unknown remain gated |
| Touch | Goodix GT9XX | Modified restricted driver and embedded firmware omitted; external source/licensing prerequisite |
| GPU / storage | Mali Midgard, MediaTek MSDC | Baseline runtime source retained; performance and complete hardware acceptance pending |
| Wi-Fi, Bluetooth, modem | Board connectivity and modem interfaces | Complete transport, power sequencing and runtime support pending |
| Audio, sensors, camera, fingerprint and suspend | Board drivers and vendor interfaces | Incomplete bring-up; primary branch contains additional component ports |

The generated configuration is retained at
`arch/arm64/configs/m3note_u3.config`, SHA-256
`2aff78c293a4d07f787cadb6d83ded52d801810a02a24622fac6711f002bb0dc`.
It selects the common diagnostic kernel, both panel drivers and the guarded
factory-pair selector. A blank selector table returns an unknown board and
keeps board-specific operations gated. A successful historical compile does
not establish that this public subset is buildable, stable or fully supported.

## Build dependencies

Use an AArch64 Android GCC 4.9 toolchain, `ARCH=arm64`,
`MTK_PLATFORM=mt6755` and `TARGET_BUILD_VARIANT=userdebug`. The source-defined
starting configuration is `m3note_49_diagnostic_defconfig`; `m3note_u3.config`
records the generated configuration selected for the retained build.

Board targets are `wt6755_66_sz_l.dtb` (M681) and `hq6755_66_b1a_l.dtb`
(L681). Their generated `cust.dtsi` files require matching board data and a
compatible DCT generator. Opaque DWS inputs, modified DCT executables and
precompiled DTB merge files are excluded. The modified Goodix controller source
has a restrictive disclosure notice and is withheld; firmware headers are also
omitted. These prerequisites must be resolved before treating the public source
as a standalone build target. Do not substitute donor board data or bypass the
factory identity gate.

No kernel image, device identity, calibration archive or firmware payload is
included. Original authorship and copyright/license notices in retained source
files are preserved. GPL information is in [COPYING](COPYING); upstream notes
remain in [README](README).

## USB reconnection integration

A narrow M681 controller correction restarts the MU3D controller after VBUS returns and resets the cable-out state. The three controller files are integrated over the existing unified branch without removing L681 panel or board support. This is a partial patch integration, not an export of the complete newer M681 development snapshot. The matching USB-fix candidate was compiled; physical reconnection acceptance remains pending. Existing external source dependencies and both-revision validation limitations still apply.
