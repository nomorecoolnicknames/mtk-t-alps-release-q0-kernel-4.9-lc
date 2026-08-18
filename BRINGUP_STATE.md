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
- wlan/ gen2 driver + cfg80211 3.18->4.9 (Phase H-2; CONFIG_MTK_COMBO_WIFI
  stays n until then; /dev/wmtWifi already present).
- FM: 4.9 tree has no fmradio driver at all (3.18:
  drivers/misc/mediatek/fmradio, CONFIG_MTK_FMRADIO=y, MT6625_FM);
  fm_drv_init.c compiles with the mtk_wcn_fm_init() call ifdef'd out.
- BT userspace: /dev/stpbt present; Bluedroid runtime untested.

Runtime risks (untested, no flash yet): consys power-on sequence vs 4.9
PMIC/regulator API, EMI reserved-memory handoff, LTE IDC path vs eccci1.
