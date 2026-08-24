# m5c 4.9 GPU (Mali-T720, mali r7p0) — Phase E state

Worktree: `/srv/forge/android/m5c/k49-worktrees/gpu49` (branch `forge/gpu49`, base `fdeb90897`).
Logs: `/srv/forge/android/m5c/k49-worktrees/gpu49-logs/`.

## Status: BUILD-GREEN (offline criterion met), runtime probe untested

- Full `Image.gz-dtb` build: 0 errors (`gpu49-logs/full-build1.log`).
- GPU subtree: 0 errors / 0 warnings (`gpu-subtree-build3.log`).
- vmlinux strings verified: "Midgard r7p0-02rel0 DDK kernel device driver",
  "GPU identified as 0x%04x r%dp%d", "Probed as %s", "arm,mali-midgard".
- Built `k37mv1_bsp_k49.dtb` MALI node byte-matches stock DTB
  (`arm,malit720…`, reg 0x13040000/0x4000, IRQ 212/211/210 JOB/MMU/GPU,
  550 MHz). G3D_CONFIG@0x13000000 present. No DT change was needed.

## FACT: mali version parity

- 3.18 stock and this 4.9 tree both ship Mali Midgard DDK **r7p0-02rel0**
  (`MALI_RELEASE_NAME` in both Kbuilds; probe banner identical in both
  vmlinux binaries). ABI risk to NE1 gralloc/egl blobs: LOW (same UK
  ioctl ABI generation).

## FACT: what was missing / changed (commit d637bee3b)

- 4.9 BSP had dropped `platform/mt6735/` glue (only mt6757/mt6763
  remained); restored verbatim from 3.18 tree. Selected via
  `CONFIG_MALI_PLATFORM_THIRDPARTY_NAME = CONFIG_MTK_PLATFORM = "mt6735"`.
- `mt_gpufreq.h` (mt6735) vs `mtk_gpufreq.h` (mt6757+): includes guarded
  with `CONFIG_MACH_MT6735M` in `mali_kbase_core_linux.c`,
  `platform/mtk_platform_common.h`, `mtk_platform_common.c`.
- `mali_kbase_mem_linux.c`: 4.8+ dma_attrs removal adaptation
  (compat macros; no logic change).
- `gpu/ged/Makefile`, `gpu/hal/Makefile`: include paths for
  `mt-plat/eas_ctrl.h`, `mt-plat/mtk_gpu_utility.h`.
- CONFIG_MTK_CLKMGR=y in both 3.18 and 4.9 → glue uses
  `enable_clock(MT_CG_MFG_BG3D/MT_CG_DISP0_SMI_COMMON)`; consistent with
  stock DTB having no `clocks` property in the MALI node.

## CONFIG list for main-tree defconfig (not applied there by design)

```
CONFIG_MTK_GPU_SUPPORT=y
CONFIG_MTK_GPU_VERSION="mali midgard r7p0"
CONFIG_MTK_GPU_COMMON_DVFS_SUPPORT=y
```

## Runtime risks (untested on device)

- mt6735 glue reads chip hw code (`mt_get_chip_hw_code`); MT6737T code is
  not in the 0x321/0x335/0x337 list → falls to the `#else` branch which,
  under CONFIG_MTK_CLKMGR=y, still enables MFG_BG3D + SMI_COMMON clocks
  (same behaviour as 3.18 stock, which probed OK on this hardware).
- efuse core-mask path (`get_devinfo_with_index(3) >> 7`) — same as 3.18.
- GPU DVFS (mt_gpufreq, ENABLE_COMMON_DVFS/GED) untested at runtime;
  ged DVFS threads depend on vsync notify wiring from the display stack.
- Probe expected markers: "GPU identified as 0x0720 r1p0",
  "Probed as mali0", /dev/mali0.
