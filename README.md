# Meizu M5c · Linux 4.9.188

ARM64 kernel development for **Meizu M5c / MT6737M**, using the MediaTek
MT6735-family 4.9 base. This branch contains the M5c defconfig and Mali r7p0
platform integration. It is distinct from the newer M5s board-resource branch.

## Hardware and source map

Values below come from [m5c_defconfig](arch/arm64/configs/m5c_defconfig).
They are requested settings, not a freshly generated configuration. Earlier
development records report full-image and GPU compilation; no retained device
result has been matched to this exact public tip for this documentation update.
Separate M5c runtime progress must not be attributed to this branch automatically.

| Component | Source implementation | Configuration / integration | Compiled | Working on this branch |
|---|---|---|---|---|
| Display panels | [LCM drivers](drivers/misc/mediatek/lcm) | `ili9881c_dsi_vdo_dj_hd720 jd9365_dsi_vdo_holitech_hd720` | Not verified for this tip | Panel variant needs matching |
| Touchscreen | [Goodix GT9XXTB](drivers/input/touchscreen/mediatek/GT9XXTB_hotknot) | `TOUCHSCREEN_MTK_GT9XXTB_HOTKNOT=y` | Not verified for this tip | Board matching / payload need checking |
| GPU | [Mali Midgard r7p0](drivers/misc/mediatek/gpu/gpu_mali/mali_midgard/mali-r7p0) | `MTK_GPU_SUPPORT=y`; MT6735 platform glue | Earlier GPU compilation recorded | No matched runtime test for this tip |
| eMMC / SD | [MSDC](drivers/mmc/host/mediatek/ComboA/mt6735) | `MMC_MTK_PRO=y` | Not verified for this tip | I/O / suspend need testing |
| PMIC / charging | [MT6735 power](drivers/misc/mediatek/power/mt6735) | `MTK_PMIC_WRAP_HAL=y`, `MTK_FAN5405_SUPPORT=y` | Not verified for this tip | Board rail / charger policy need checking |
| USB | [MediaTek USB 2.0](drivers/misc/mediatek/usb20) | `USB_MTK_HDRC=y` | Not verified for this tip | Role / resume tests needed |
| Camera / audio / radio stack | [BSP driver collection](drivers/misc/mediatek) | Separate board integration required; presence is not support | Not established here | Not established for this tip |

## Build inputs

Use the kernel in the repository root, `ARCH=arm64`, and an absolute
`CROSS_COMPILE` prefix for AArch64 Android GCC 4.9. Select
[m5c_defconfig](arch/arm64/configs/m5c_defconfig) and use a separate Kbuild output
directory. The BSP image target is `Image.gz-dtb`; matching M5c board
generation inputs, ramdisk, command line and boot-image geometry are still
required for device integration. A kernel image alone is not a ROM.

No new build or device test was run for this source-map update. Use the matching
Android device tree and board firmware; a common chipset is not a substitute
for matching panel, touch, power and storage resources.

The base selects `CONFIG_MACH_MT6735M`; that is a BSP implementation choice,
not permission to reuse another phone's DTB, panel data or power policy.

## Credits and licensing

Built on Linux, Android and MediaTek BSP work. Original vendor and downstream
authors remain credited in Git history and per-file notices. See
[COPYING](COPYING); individual files may carry additional terms.
Device firmware, calibration and Android vendor libraries are separate inputs.

[ReMeizu project status](https://github.com/nomorecoolnicknames/remeizu/blob/main/PROJECT_STATUS.md)
