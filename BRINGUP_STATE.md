# BRINGUP_STATE.md — Meizu m5c (MT6737T) kernel 4.9 port

Per-device state for the m5c 4.9 kernel bring-up (repo: kernel-m5c-4.9-lc,
worktrees under /srv/forge/android/m5c/k49-worktrees/). Root
/srv/forge/android/BRINGUP_STATE.md is index-only; this file is the
canonical journal for m5c 4.9 work.

Reference (READ-ONLY) 3.18 source: /home/valakas/m5c/android_kernel_meizu_m5c
Stock DTB: /srv/forge/android/m5c/los14.1-m5c-patched/device/meizu/m5c/captures/20260817-los-first-boot/dtb_stock.dtb

## Phase H-1 (2026-08-18, branch forge/conn49): connectivity wmt/stp/consys + GPS

FACT — ported in-tree from 3.18 (4.9 BSP moved connectivity to a standalone
repo that does not exist here; connadp shims kept on disk but not linked,
they duplicate gConEmiPhyBase):

- common/common_detect (wmt_detect /dev/wmtdetect maj 154, wmt_gpio,
  wmt_stp_exp, mtk_wcn_stub_alps, drv_init) — commit c9c1fcd96
- common/conn_soc (wmt/stp core + linux + mt6735 platform) — commit 50b813850
  /dev/stpwmt maj 190, /dev/stpbt maj 192, /dev/wmtWifi maj 153
- gps/ (/dev/stpgps maj 191, /dev/gps "mt3326-gps" dynamic) — commit 3fb499e92
- common/combo is NOT needed for CONSYS_6735 (MT662x/6630-only in 3.18 too).

4.9 adaptations (all additive compat, no logic changes):
- mt-plat/mtk_wcn_cmb_stub.h, btif mtk_btif_exp.h: typedef aliases for
  enum-tag-only 4.9 headers.
- conn_md/Makefile + connectivity/Makefile: eccci1 include paths (m5c 4.9
  uses eccci1 CCCI, CONFIG_MTK_ECCCI_DRIVER=n).
- wmt_idc/wmt_dev/wmt_lib: ipc_ilm_t -> struct ipc_ilm (4.9 conn_md API).
- wmt_chrdev_wifi.c: export-name typo fix (mtk_wcn_wmt_wifi_init/exit),
  __weak g_IsNeedDoChipReset fallback until wlan gen2 is ported.

CONFIG list used (worktree .config; main defconfig NOT touched):
  CONFIG_MTK_COMBO=y, CONFIG_MTK_COMBO_CHIP_CONSYS_6735=y,
  CONFIG_MTK_COMBO_BT=y, CONFIG_MTK_COMBO_GPS=y, CONFIG_MTK_COMBO_WIFI=n,
  CONFIG_MTK_CONN_LTE_IDC_SUPPORT=y (selects MTK_CONN_MD=y),
  CONFIG_GPS=y, CONFIG_MTK_GPS=y, CONFIG_MTK_GPS_SUPPORT=y,
  CONFIG_MTK_BTIF=y (was off in 4.9 base config; required by stp_btif).

Build: Image.gz-dtb 0 errors, log k49-worktrees/conn49-logs/full-build-3.log.
DT: consys@18070000 + consys-reserve-memory nodes byte-identical to stock
DTB (only phandle numbers differ); no "mediatek,connectivity-combo" node in
either DTB (driver warns and continues, same as 3.18 stock).

NOT ported (next phases):
- FM: 4.9 tree has no fmradio driver at all (3.18:
  drivers/misc/mediatek/fmradio, CONFIG_MTK_FMRADIO=y, MT6625_FM);
  fm_drv_init.c compiles with the mtk_wcn_fm_init() call ifdef'd out.
- BT userspace: /dev/stpbt present; Bluedroid runtime untested.

Runtime risks (untested, no flash yet): consys power-on sequence vs 4.9
PMIC/regulator API, EMI reserved-memory handoff, LTE IDC path vs eccci1.

## Phase H-2 (2026-08-18, branch forge/conn49): wlan gen2 + cfg80211 3.18->4.9

FACT — driver selection: 3.18 wlan/Makefile routes CONSYS_% chips to gen2/
(obj-y, built-in — NOT a .ko); gen3 is MT6630/CONSYS_6797-only. m5c is
CONSYS_6735 => gen2, built-in, same as 3.18. 69 .c files imported verbatim
from the 3.18 tree (common/ nic/ mgmt/ os/linux/ + hif/ahb/mt6735).

Commits:
- aae484074 "wlan: port gen2 driver skeleton + build wiring (Phase H-2)"
  (pristine import; builds only with MTK_COMBO_WIFI=n)
- dc7cc3833 "wlan: cfg80211 4.9 adaptation for gen2 (Phase H-2)"

cfg80211 3.18->4.9 adaptations (mechanical 1:1 API mappings, no logic
rewrites; every hunk tagged "forge: 4.9"):
- cfg80211_scan_done(bool) -> cfg80211_scan_done(struct cfg80211_scan_info)
  (4 sites: gl_kal.c, gl_p2p_kal.c, gl_init.c, gl_p2p.c)
- cfg80211_disconnected() gained locally_generated -> FALSE
  (firmware-indicated teardown; ath6kl/wil6210 precedent) (gl_kal.c,
  gl_p2p_kal.c)
- del_station(const u8 *mac) -> del_station(struct station_del_parameters *)
  (gl_cfg80211.c stub, gl_p2p_cfg80211.c real + 2 prototypes)
- cfg80211_vendor_event_alloc() gained wdev arg -> NULL (gl_vendor.c, 6
  GSCAN/packet-filter event sites)
- sched_scan_request.interval removed -> scan_plans[0].interval
  (gl_cfg80211.c mtk_cfg80211_sched_scan_start)
- STATION_INFO_* -> NL80211_STA_INFO_* (8 sites); the ASSOC_REQ_IES filled
  bit is gone in 4.9 (cfg80211_new_sta consumes assoc_req_ies
  unconditionally) -> filled=0 (gl_p2p_kal.c)
- IEEE80211_BAND_* -> NL80211_BAND_* (33 sites, 7 files)
- linux/wakelock.h removed -> local compat shim
  os/linux/include/linux/wakelock.h (wake_lock wrapper struct over
  wakeup_source; init/trash, stay_awake/relax, timeout)
- linux/ftrace_event.h include dropped (gl_kal.h; MET sites only need
  met_drv.h); event_trace_printk -> trace_printk (gl_kal.c, 2 sites)
- strnicmp removed -> strncasecmp (kalStrniCmp macro + gl_wext_priv.c)
- gen2/Makefile: mt-plat include paths added for standalone dir builds.

Nothing left as TODO-H-2 stub: the whole gen2 feature set (P2P, GSCAN
vendor cmds, packet filter, PNO/sched-scan, WEXT priv, MET profiling)
compiles; no functionality was guarded out.

CONFIG list used (worktree .config; main defconfig NOT touched):
  Phase H-1 list, plus:
  CONFIG_MTK_COMBO_WIFI=y, CONFIG_MTK_WIFI_MCC_SUPPORT=y,
  CONFIG_MTK_DHCPV6C_WIFI=y, CONFIG_NL80211_TESTMODE=y,
  CONFIG_WIRELESS_EXT=y, CONFIG_WEXT_CORE=y, CONFIG_WEXT_PROC=y,
  CONFIG_WEXT_PRIV=y (MTK_COMBO_WIFI selects WIRELESS_EXT+WEXT_PRIV;
  matches 3.18 .config exactly).

Build: full Image.gz-dtb 0 errors / 0 warnings in wlan, log
k49-worktrees/conn49-logs/full-build-h2-04.log (earlier iterations
wlan-h2-build-01..06, full-build-h2-01..03).
vmlinux contains: wlanProbe, wiphy_register, mtk_wlan_ops,
mtk_wlan_vendor_ops/events, "wlan0", /etc/firmware/WIFI_RAM_CODE_.
g_IsNeedDoChipReset: strong symbol from gen2 gl_rst.c now overrides the
__weak fallback in wmt_chrdev_wifi.c (H-1 bridge closed, FACT via nm).

Runtime risks (untested, no flash yet): firmware load via
/etc/firmware/WIFI_RAM_CODE_ (rootfs must ship it, same as 3.18 LOS);
wmt wifi power-on ioctl path vs 4.9; P2P interface creation; sched-scan
interval semantics (3.18 interval units vs 4.9 scan_plans seconds);
wakeup_source shim behaviour under suspend (wake_lock_timeout jiffies->
msecs conversion).

## Phase H-3: FM radio (mt6627-variant) port — 2026-08-18

Commit: 7be1cca09 "fmradio: port mt6627 fm driver from 3.18"

FACT (3.18 recon): stock .config builds fmradio with
CONFIG_MTK_FMRADIO=y, CONFIG_MTK_FM=y, CONFIG_MTK_FM_SUPPORT=y,
CONFIG_MTK_FM_CHIP="MT6625_FM"; the 3.18 fmradio/Makefile maps
MT6625_FM onto the mt6627 code path (-DMT6627_FM -DMT6625_FM,
mt6627/pub/* objects). So the "MT6627 FM config" dmesg line and the
MT6625_FM config name are the SAME driver — no contradiction. FM on
m5c is the MT6627-compatible block inside consys SOC 0x0335, reached
only via wmt/stp (no combo-chip SDIO path involved).

Port shape: verbatim import of drivers/misc/mediatek/fmradio/ (46
files, core+inc+mt6627+mt6630) minus build artifacts; exactly ONE 4.9
adaptation — fm_module.c fm_ops_ioctl: filp->f_dentry->d_inode ->
file_inode(filp). No wakelock/PDE_DATA/create_proc_entry/misc/timer
issues exist in this driver (grep-verified pre-import). Build wiring:
obj-$(CONFIG_MTK_FMRADIO) in mediatek/Makefile, source fmradio/Kconfig
in mediatek/Kconfig (CONN menu). fm_drv_init.c (conn_soc
common_detect/drv_init) needed NO edit: it calls mtk_wcn_fm_init()
under CONFIG_MTK_FMRADIO and the ported driver exports that symbol
(MTK_WCN_REMOVE_KERNEL_MODULE path) — H-1 bridge closed, FACT via
System.map (T mtk_wcn_fm_init / T mtk_wcn_fm_exit).

Char-device ABI (same source as 3.18 => identical): /dev/fm char dev
"fm", dynamic major (alloc_chrdev_region + class_create/device_create),
/proc/fm, ioctl magic 0xf5 cmds 0..49, compat_ioctl for 32-bit
userspace.

CONFIG list (worktree .config; main defconfig NOT touched):
  CONFIG_MTK_FMRADIO=y, CONFIG_MTK_FM=y, CONFIG_MTK_FM_SUPPORT=y,
  CONFIG_MTK_FM_CHIP="MT6625_FM" (exactly the 3.18 set).

Build: full Image.gz-dtb 0 errors, log
k49-worktrees/conn49-logs/full-build-fm-1.log (subdir iterations
fm-subdir-1..3.log). vmlinux strings: mtk_wcn_fm_init/exit,
"[FM-MOD-INIT]", "MT6627 FM cust/default config". modpost reports 4
section mismatches — pre-existing from H-1/H-2 connectivity/wlan, not
fmradio (fmradio objects add none; verified in fm-subdir-3.log with 0
warnings).

Runtime risks (untested, no flash): wmt func_on(FM) path on real
consys firmware; /dev/fm vs LOS14.1 userspace (fmradio.driver.enable=0
by default — driver registers but stays off, same as 3.18); FM antenna
EINT is a no-op stub in this variant (fm_eint.c returns 0, events come
via stp event cb — same as 3.18, not a regression).

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
