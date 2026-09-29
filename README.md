# Meizu M5s · Linux 4.9.188

Custom kernel development for the **Meizu M5s / MT6753**. This branch contains
the M5s Yassy panel driver, board-specific MSDC binding and display register-map
changes on a MediaTek 4.9 base. It uses the BSP's `mt6735` platform code with
`CONFIG_MACH_MT6753_M5S=y`; that shared platform name does not make board resources
interchangeable with M5c or other MT6735-family phones.

**Current milestone:** `vmlinux` and `Image.gz` compiled; the Yassy, MSDC and DDP
changes were checked in the resulting objects and linked kernel. **No M5s DTB,
boot image or hardware boot has been accepted from this branch.**

## Hardware and source map

Configuration below refers to the generated `m5s_yassy_defconfig` build, not
merely to the presence of a driver in the repository. “Not checked” is not a
claim that hardware is broken or absent.

| Component | Source implementation | Selected configuration | Compilation | Hardware status |
|---|---|---|---|---|
| 720×1280 Yassy panel | [ILI9881 Yassy](drivers/misc/mediatek/lcm/ili9881_CA_hd720_dsi_vdo_yassy/ili9881_CA_hd720_dsi_vdo_yassy.c) | `CUSTOM_KERNEL_LCM=ili9881_CA_hd720_dsi_vdo_yassy` | Object and linked tables verified | Not boot-tested |
| Display pipeline | [MT6735M DDP map](drivers/misc/mediatek/video/mt6735/dispsys/mt6735m/ddp_reg.h) | `MTK_FB=y`, M5s-specific addresses | 16 linked register tables checked | Clocks, SMI and routing still need board validation |
| eMMC / SD host | [MSDC board resources](drivers/mmc/host/mediatek/ComboA/mt6735/msdc_cust.c) | `MMC_MTK_PRO=y` | Object and binding table verified | I/O and suspend not tested |
| Goodix touch candidate | [GT9XXTB driver](drivers/input/touchscreen/mediatek/GT9XXTB_hotknot/gt9xx_driver.c) | `TOUCHSCREEN_MTK_GT9XXTB_HOTKNOT=y` | Selected; no separate A4 object acceptance | Board matching, payload and probe remain open |
| FocalTech touch variant | [Touch driver collection](drivers/input/touchscreen/mediatek) | No accepted M5s-specific selection | Not accepted | Power sequence and chip identification remain open |
| GPU | [Mali Midgard r7p0](drivers/misc/mediatek/gpu/gpu_mali/mali_midgard/mali-r7p0) | `MTK_GPU_SUPPORT=y`; MT6735 platform glue | Selected; not separately verified | Rendering and DVFS not tested |
| Wi-Fi / Bluetooth transport | [Connectivity drivers](drivers/misc/mediatek/connectivity) | `CONSYS_6735`, `MTK_COMBO_WIFI=y`, `MTK_BTIF=y` | Selected; not separately verified | Firmware, radio operation and suspend not tested |
| PMIC / power | [MT6735 power drivers](drivers/misc/mediatek/power/mt6735) | `MTK_PMIC=y`, `MTK_PMIC_WRAP_HAL=y` | Selected; not separately verified | Rail map, charging and thermal policy need board validation |
| Cameras | [Image-sensor drivers](drivers/misc/mediatek/imgsensor) | `CUSTOM_KERNEL_IMGSENSOR` is empty | No board sensor selected | Not integrated |
| Audio | [MT6735 audio sources](sound/soc/mediatek/mt6735) | Not enabled in the accepted configuration | Not built for this configuration | Not integrated |

## Build and validation

- Configuration: [`arch/arm64/configs/m5s_yassy_defconfig`](arch/arm64/configs/m5s_yassy_defconfig).
- Compiler: [AOSP AArch64 GCC 4.9](https://android.googlesource.com/platform/prebuilts/gcc/linux-x86/aarch64/aarch64-linux-android-4.9/+/7a28c220c2e9001825328dca6188ef0077a80a88), pinned to `7a28c220c2e9001825328dca6188ef0077a80a88`.
  The accepted modern-host setup changes only the `gcc`/`g++` wrapper shebangs to Python 3.
- Kernel build targets checked so far: `vmlinux` and `Image.gz`. The appended-DTB
  target still needs the correct board-generation inputs; `Image.gz` alone is not a boot image.
- [ReMeizu build infrastructure](https://github.com/ReMeizu/build-infra) provides
  the Forge-based `kernel` job. Its current M5s recipe compiles the Yassy object,
  not a complete boot image.

The accepted full compilation used source commit
[`e2a0b2e`](https://github.com/nomorecoolnicknames/mtk-t-alps-release-q0-kernel-4.9-lc/commit/e2a0b2e51b87ce7aca65775d92c9269df8f27201).
The later [Blacksmith run](https://github.com/ReMeizu/build-infra/actions/runs/36553547835)
rebuilt the Yassy object byte-for-byte (`SHA256 2e5164dcaf871fc0a39362995930efec356442becb18b257b5b3eee0f56b488a`).
Subsequent Goodix selector experiments are separate work and are not included in this branch.

## Next steps

Complete the M5s DTS/DCT and touch selection, validate power and display clocks,
then package and test a board-specific boot image. The accepted configuration
still contains inherited `LCM_WIDTH=1080` / `LCM_HEIGHT=1920`; review that geometry
against the 720×1280 Yassy driver before display bring-up. Camera, audio and
remaining board peripherals need their own integration and tests.

## Credits and licensing

Based on Linux and MediaTek BSP work, with device adaptations recorded in Git
history. Preserve the original authors and per-file notices. The kernel's
[COPYING](COPYING) and individual file licenses apply; firmware and Android
userspace components have separate requirements.

[ReMeizu project status](https://github.com/nomorecoolnicknames/remeizu/blob/main/PROJECT_STATUS.md)
