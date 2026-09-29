# MediaTek MT6755 — Meizu M3 Note

Linux 4.9 development kernel for Meizu M3 Note M681. The intended board family
includes L681, but this branch has no L681 hardware acceptance. Android 13 is the
current bring-up target. This is a kernel source tree, not a complete ROM release.

The combined image from this source export has not been accepted on hardware.
Earlier component builds establish compilation of individual drivers; they do
not establish a working modem, audio route, suspend cycle or complete image.

| Component | Source | Current evidence / remaining work |
| --- | --- | --- |
| Display / backlight | `video/mt6755`, `cmdq/v2_mt6755`, `lcm/mt6755` | Board and display paths integrated; this combined image awaits device validation. |
| Touch | `input/touchscreen/mediatek/GT9XX_MZ` | Board driver retained. Embedded firmware payload omitted; automatic firmware update and GT9xxF compatibility remain off. |
| Audio | `sound/soc/mediatek/mt6755`, `codec/mt6351` | Earlier normal ASoC graph compiled. Codec/card defer until real clock readiness. Audio PMIC permission and external amplifier gates remain closed. |
| Clock / PMIC | `base/power/mt6755`, `pmic_wrap/mt6755` | Checked PMIC writes and real clock provider integrated. CW15 access is not permitted; linking a provider does not make it ready. |
| Modem | `eccci1/mt6755`, `ccci_util`, `ccmni` | Earlier ECCCI component graph compiled. Secure firmware, power sequencing and live network acceptance remain separate. PBM hooks still need a real implementation. |
| ALS / proximity | `m681-sensors/alsps` | LTR579 and PA122 drivers and error propagation integrated; board binding and runtime measurements pending. |
| Magnetometer | `m681-sensors/magnetometer` | AKM09911/ST480 drivers integrated; runtime validation pending. |
| Accelerometer / gyro | `iio/imu/inv_mpu_m681` | ICM20608 IIO source and GPL address-lookup helper retained; earlier composite compiled. Device measurements pending. |
| Fingerprint / TEE | `input/fingerprint/goodix`, `gud/302c` | Earlier GF516M and legacy Trustonic302c objects compiled. Secure-world compatibility and fingerprint operation are unverified. GP-only trusted memory is not selected for302c. |
| Wi-Fi / Bluetooth | `connectivity/common/conn_soc/mt6755`, `connectivity/wlan` | Driver port retained; connection power-on remains gated and runtime acceptance is pending. |
| Camera / charging / suspend | `imgsensor`, `base/power/mt6755`, `power/mt6755` | Incomplete bring-up; no daily-use acceptance. |

Paths below `drivers/misc/mediatek` are abbreviated in the table. Factory
calibration that writes persistent device data remains unavailable. Existing
public tools retain their original provenance; no newly modified opaque DCT
binary or embedded touch firmware payload is introduced by this export.

## Build

Use an AArch64 Android GCC4.9 toolchain. Start with the source-defined A13 board
configuration and append the compile-only component fragment:

```sh
make O=out ARCH=arm64 MTK_PLATFORM=mt6755 CROSS_COMPILE=/path/to/aarch64-linux-android- m681_49_a13_defconfig
cat arch/arm64/configs/m681_49_components.config >> out/.config
make O=out ARCH=arm64 MTK_PLATFORM=mt6755 CROSS_COMPILE=/path/to/aarch64-linux-android- olddefconfig
make O=out ARCH=arm64 MTK_PLATFORM=mt6755 CROSS_COMPILE=/path/to/aarch64-linux-android- -j8 Image.gz-dtb
```

Inspect the generated `out/.config`; the fragment selects nine component
objects alongside the image. The HotKnot firmware update path remains disabled. A successful build still needs exact image/readback identity
and device logs before any hardware-support claim.

Original copyright notices and license terms are retained. See `COPYING` and
individual source headers. The omitted Goodix firmware needs verified redistribution
terms or a real external-firmware loading path before firmware updates can return.
