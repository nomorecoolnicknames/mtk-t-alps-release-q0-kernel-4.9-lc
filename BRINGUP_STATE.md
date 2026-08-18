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
