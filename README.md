# MediaTek Linux 4.9.188 base

MediaTek ALPS kernel source used as a base for the ReMeizu 4.9 work. `master`
is a shared BSP snapshot, not a complete M5c or M5s board port. Hardware support
depends on the selected configuration, driver bindings and board resources.

## Source map

| Component | Source implementation | Configuration / integration | Compiled | Working on this branch |
|---|---|---|---|---|
| ARM / ARM64 platform support | [Architecture sources](arch) | Choose a matching SoC and board configuration | Board-dependent | No device claim on this base |
| Display / panels | [LCM drivers](drivers/misc/mediatek/lcm), [Display pipelines](drivers/misc/mediatek/video) | BSP collection; no single phone selected | Board-dependent | Requires board validation |
| Touchscreen | [Touch drivers](drivers/input/touchscreen/mediatek) | Controller, pinctrl and payload are board-specific | Board-dependent | Requires board validation |
| GPU | [Mali / platform sources](drivers/misc/mediatek/gpu) | Select a DDK and matching platform glue | Board-dependent | Userspace ABI and DVFS need matching |
| Camera | [Image-sensor drivers](drivers/misc/mediatek/imgsensor) | Select actual sensors and board power sequence | Board-dependent | Requires camera-stack integration |
| Connectivity | [CONSYS / WLAN / GPS](drivers/misc/mediatek/connectivity) | SoC selection plus external firmware | Board-dependent | Requires radio testing |
| Storage | [MediaTek MMC](drivers/mmc/host/mediatek) | Board controller / timing / pin mapping | Board-dependent | Requires I/O and suspend testing |
| Audio / power | [ASoC](sound/soc/mediatek), [PMIC](drivers/misc/mediatek/pmic) | Separate board configuration required | Board-dependent | Requires board validation |

## Choose a device branch

- [M5c ARM64 development](https://github.com/nomorecoolnicknames/mtk-t-alps-release-q0-kernel-4.9-lc/tree/m5c-arm64)
  contains the M5c configuration and GPU platform work.
- [M5s Linux 4.9](https://github.com/nomorecoolnicknames/mtk-t-alps-release-q0-kernel-4.9-lc/tree/m5s-linux-4.9)
  contains the Yassy panel, MSDC binding and display-map changes, with a checked
  kernel compilation and an explicitly unfinished boot-image integration.

## Build basics

Select a board defconfig from the appropriate device branch and use its stated
compiler version. The repository contains multiple platforms and driver
generations; a successful generic build is not proof of support for a phone.
Keep output separate from sources and inspect the generated configuration,
selected objects and DTB before integrating a boot image.

The upstream [README](README) and [Documentation](Documentation) cover the
Linux build system. ReMeizu's isolated build tooling is in
[build-infra](https://github.com/ReMeizu/build-infra).

## Credits and licensing

Built on Linux, Android and MediaTek BSP work. Original vendor and downstream
authors remain credited in Git history and per-file notices. See
[COPYING](COPYING); individual files may carry additional terms.
Device firmware, calibration and Android vendor libraries are separate inputs.

[ReMeizu project status](https://github.com/nomorecoolnicknames/remeizu/blob/main/PROJECT_STATUS.md)
