/*
 * m681/mt6755 misc boot-milestone link stubs.
 *
 * No-op definitions for symbols whose implementing subsystems are deferred for the
 * boot milestone but whose (kept) consumers still reference them. None of these
 * code paths run before adb. The linker resolves by name, so simplified C
 * signatures (plain int/uint instead of the original enum/struct argument types)
 * are sufficient and avoid pulling each subsystem's headers; the stubs ignore
 * their arguments. Remove each stub when its real subsystem is un-deferred:
 *
 *   aee_nested_printf .............. AEE (mrdump) deferred
 *   cpuhvfs_*_dvfsp_semaphore ...... cpu-hybrid-DVFS deferred
 *   mt_get_charger_type ............ charger deferred (report UNKNOWN=0)
 *   mt_irq_get/set_* ............... MTK SYS_CIRQ deferred (suspend wakeup-source)
 *   mtk_is_pep30_en_unlock ......... USB-C PD charging deferred
 *   mtk_rgu_mcu_cache_preserve ..... RGU MCU-cache preserve
 *   mtk_wdt_dfd_* .................. WDT DFD (DRAM-flush-debug) deferred
 *   mt_pause_armpll ................ ARMPLL pause (cpufreq) deferred
 *   mt_ppm_sysboost_* .............. PPM (perf manager) deferred
 *   primary_display_* .............. display stack deferred
 *   bmi160_acc_i2c_client .......... BMI160 accel (sensors) deferred
 *
 * See docs MT6755_4.4_BUILD_PROGRESS.md for the full defer/re-enable checklist.
 */
#include <extd_info.h>
struct dprec_logger_event;
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/errno.h>	/* m681: ION stubs below return -ENODEV */

/* m681 (2026-06-29): aee_nested_printf stub removed — CONFIG_MTK_AEE_FEATURE is now
 * enabled so the real AEE driver provides it (the stub would duplicate-symbol at link). */
int cpuhvfs_get_dvfsp_semaphore(int user) { return 0; }
EXPORT_SYMBOL(cpuhvfs_get_dvfsp_semaphore);
void cpuhvfs_release_dvfsp_semaphore(int user) { }
EXPORT_SYMBOL(cpuhvfs_release_dvfsp_semaphore);

#ifndef CONFIG_MTK_SMART_BATTERY
/* m681 (2026-07-15): these two are stubs ONLY while the battery stack is
 * deferred; with CONFIG_MTK_SMART_BATTERY=y the real providers are
 * battery_common_fg_20.c (mt_get_charger_type) and switch_charging.c
 * (mtk_chr_is_charger_exist) — keeping the stubs would duplicate-symbol. */
int mt_get_charger_type(void) { return 0; }
EXPORT_SYMBOL(mt_get_charger_type);

/* mtk_chr_is_charger_exist — charger deferred; RTC power-off path reports no charger */
int mtk_chr_is_charger_exist(unsigned char *exist) { if (exist) *exist = 0; return 0; }
EXPORT_SYMBOL(mtk_chr_is_charger_exist);
#endif /* !CONFIG_MTK_SMART_BATTERY */

/* is_set_transport_mode / set_shippingmode — shipping-mode from battery/rt5081 deferred */
bool is_set_transport_mode(void) { return false; }
EXPORT_SYMBOL(is_set_transport_mode);
void set_shippingmode(void) { }
EXPORT_SYMBOL(set_shippingmode);

/* mt_irq_{get,set}_pending_{hw,vec} + mt_irq_get_pol_hw: provided by the
 * ALPS-lc vendor GICv2 (drivers/irqchip/irq-mt-gic.c:1497+), which this
 * tree builds for MACH_MT6755 — the 4.4 tree's GIC lacked them, hence the
 * stubs there. Dropped here to avoid duplicate definitions. */

bool mtk_is_pep30_en_unlock(void) { return false; }
EXPORT_SYMBOL(mtk_is_pep30_en_unlock);

int mtk_rgu_mcu_cache_preserve(int enable) { return 0; }
EXPORT_SYMBOL(mtk_rgu_mcu_cache_preserve);

int mtk_wdt_dfd_count_en(int value) { return 0; }
EXPORT_SYMBOL(mtk_wdt_dfd_count_en);
int mtk_wdt_dfd_timeout(int value) { return 0; }
EXPORT_SYMBOL(mtk_wdt_dfd_timeout);
int mtk_wdt_dfd_thermal1_dis(int value) { return 0; }
EXPORT_SYMBOL(mtk_wdt_dfd_thermal1_dis);
int mtk_wdt_dfd_thermal2_dis(int value) { return 0; }
EXPORT_SYMBOL(mtk_wdt_dfd_thermal2_dis);

int mt_pause_armpll(unsigned int pll, unsigned int pause) { return 0; }
EXPORT_SYMBOL(mt_pause_armpll);

void mt_ppm_sysboost_core(int user, unsigned int core_num) { }
EXPORT_SYMBOL(mt_ppm_sysboost_core);
void mt_ppm_sysboost_freq(int user, unsigned int freq) { }
EXPORT_SYMBOL(mt_ppm_sysboost_freq);
/* m681 DVFS stage-3: mt_cpufreq.c reports its tables/limit-callback to PPM and
 * kicks PBM from its hotcpu path; ppm_v2 stays MACH_MT6757-gated (stage 4).
 * No PPM = no external limiter — mt_cpufreq's own idx_opp_ppm_limit stays -1,
 * which reads as "no limit" and is handled natively. */
void mt_ppm_set_dvfs_table(unsigned int cpu, void *tbl,
			   unsigned int num, int type) { }
EXPORT_SYMBOL(mt_ppm_set_dvfs_table);
void mt_ppm_register_client(int client, void (*limit)(void)) { }
EXPORT_SYMBOL(mt_ppm_register_client);
void mt_ppm_dlpt_kick_PBM(void *cluster_status, unsigned int cluster_num) { }
EXPORT_SYMBOL(mt_ppm_dlpt_kick_PBM);

/* primary_display_* stubs RESTORED — display stack (CONFIG_MTK_FB) disabled again for
 * the adb/devices milestone (display is genuine stage-2 RE; primary_display_init can't
 * build the DDP path on 4.4 -> NULL-handle crash cascade). These are referenced by
 * non-display code (ppm/perf) even with the display off. */

#ifndef CONFIG_MTK_FB
/* m681 #101: display off -> primary_display.c absent. SMI VR-slow-motion fps coupling
 * (smi_common.c:1619/1624/1625, added by 920db186) references these. Stub them; guarded
 * so they auto-disable when MTK_FB (real primary_display.c) is re-enabled. */
unsigned int primary_display_get_fps(void) { return 0; }
EXPORT_SYMBOL(primary_display_get_fps);
int primary_display_force_set_vsync_fps(unsigned int fps) { return 0; }
EXPORT_SYMBOL(primary_display_force_set_vsync_fps);
#endif

#ifndef CONFIG_MTK_AAL_SUPPORT
/* m681 #106: the LEDS backlight driver (leds/mt6755/mtk_leds.c) calls the display-PQ
 * backlight-sync hook, which is defined only by the AAL engine (CONFIG_MTK_AAL_SUPPORT,
 * off). Stub it so MTK_LEDS links; backlight still works, it just doesn't drive AAL
 * gamma/CABC adaptation. Auto-disabled when AAL is re-enabled. */
void disp_pq_notify_backlight_changed(int bl_1024) { }
EXPORT_SYMBOL(disp_pq_notify_backlight_changed);
#endif

/* rtc_read_pwron_alarm now from real CONFIG_MTK_RTC (mtk_rtc_common.c) */

void *bmi160_acc_i2c_client;
EXPORT_SYMBOL(bmi160_acc_i2c_client);

/* ION stubs — CONFIG_ION is not set; mtkfb_fence.c uses ION unconditionally.
 * These stubs allow linking; display fence/ion paths are not needed for boot. */

/* m4u/mt6755 config_mau — inside #if 0 in m4u_hw.c; referenced by m4u/2.0/m4u.c
 * via the MTK_M4U_T_CONFIG_MAU ioctl (never triggered at boot). */
int config_mau(void *mau) { return 0; }
EXPORT_SYMBOL(config_mau);

/* SPM SODI display low-power — mt6757-specific; deferred for mt6755 */
void spm_enable_sodi3(int vdo_mode) { }
EXPORT_SYMBOL(spm_enable_sodi3);
void spm_enable_sodi(int vdo_mode) { }
EXPORT_SYMBOL(spm_enable_sodi);
void spm_sodi_mempll_pwr_mode(int mode) { }
EXPORT_SYMBOL(spm_sodi_mempll_pwr_mode);
void spm_sodi_set_vdo_mode(int mode) { }
EXPORT_SYMBOL(spm_sodi_set_vdo_mode);

/* External display (HDMI/MHL) — not present on m681; deferred */
int external_display_frame_cfg(void *p) { return 0; }
EXPORT_SYMBOL(external_display_frame_cfg);
void external_display_control_init(void) { }
EXPORT_SYMBOL(external_display_control_init);
int ext_disp_get_max_layer(void) { return 0; }
EXPORT_SYMBOL(ext_disp_get_max_layer);
int external_display_switch_mode(int mode, void *session, int need_lock) { return 0; }
EXPORT_SYMBOL(external_display_switch_mode);
int external_display_wait_for_vsync(void *config, void *session) { return 0; }
EXPORT_SYMBOL(external_display_wait_for_vsync);

int mt_usb2jtag_resume(void) { return 0; }
EXPORT_SYMBOL(mt_usb2jtag_resume);

const struct EXTD_DRIVER *EXTD_HDMI_Driver(void) { return NULL; }
EXPORT_SYMBOL(EXTD_HDMI_Driver);
const struct EXTD_DRIVER *EXTD_EPD_Driver(void) { return NULL; }
EXPORT_SYMBOL(EXTD_EPD_Driver);
int external_display_config_input(void *cfg, unsigned int idx, unsigned int sid) { return 0; }



void dprec_logger_event_init(struct dprec_logger_event *p, char *name, unsigned int level, unsigned int enable, unsigned int type) {}
EXPORT_SYMBOL(dprec_logger_event_init);
void dprec_logger_dump_reset(void) {}
EXPORT_SYMBOL(dprec_logger_dump_reset);
char *dprec_logger_get_dump_addr(void) { return NULL; }
EXPORT_SYMBOL(dprec_logger_get_dump_addr);
unsigned int dprec_logger_get_dump_len(void) { return 0; }
EXPORT_SYMBOL(dprec_logger_get_dump_len);
int dprec_logger_get_result_string_all(char *buf, int len) { return 0; }
EXPORT_SYMBOL(dprec_logger_get_result_string_all);
int dprec_logger_get_buf(unsigned int type, char *buf, int len) { return 0; }
EXPORT_SYMBOL(dprec_logger_get_buf);
char debug_buffer[16 * 1024];
EXPORT_SYMBOL(debug_buffer);
unsigned int gCaptureWdmaLayerEnable;
EXPORT_SYMBOL(gCaptureWdmaLayerEnable);
unsigned int gCaptureRdmaLayerEnable;
EXPORT_SYMBOL(gCaptureRdmaLayerEnable);
void dprec_mmp_dump(void) {}
EXPORT_SYMBOL(dprec_mmp_dump);
int dprec_logger_pr(unsigned int type, char *fmt, ...) { return 0; }
EXPORT_SYMBOL(dprec_logger_pr);
void dprec_reg_op(void *cmdq, unsigned int reg, unsigned int val, unsigned int mask) {}
EXPORT_SYMBOL(dprec_reg_op);
void init_log_buffer(void) {}
EXPORT_SYMBOL(init_log_buffer);
void PanelMaster_Init(void) {}
EXPORT_SYMBOL(PanelMaster_Init);
void dprec_logger_dump(char *string) {}
EXPORT_SYMBOL(dprec_logger_dump);
void dprec_logger_init(unsigned int buf_size, unsigned int log_size) {}
EXPORT_SYMBOL(dprec_logger_init);
void dprec_logger_reset(unsigned int source) {}
EXPORT_SYMBOL(dprec_logger_reset);

int is_buffer_init;
EXPORT_SYMBOL(is_buffer_init);
void dprec_start(struct dprec_logger_event *event, unsigned int val1, unsigned int val2) {}
EXPORT_SYMBOL(dprec_start);
void dprec_done(struct dprec_logger_event *event, unsigned int val1, unsigned int val2) {}
EXPORT_SYMBOL(dprec_done);
void dprec_trigger(struct dprec_logger_event *event, unsigned int val1, unsigned int val2) {}
EXPORT_SYMBOL(dprec_trigger);
#ifndef CONFIG_MTK_SYNC	/* m681 #129: real defs live in mediatek/sync/mtk_sync.c when MTK_SYNC=y */
void *fence_create(int fd) { return NULL; }
EXPORT_SYMBOL(fence_create);
#endif

void dprec_logger_start(struct dprec_logger_event *event) {}
EXPORT_SYMBOL(dprec_logger_start);
void dprec_logger_done(struct dprec_logger_event *event) {}
EXPORT_SYMBOL(dprec_logger_done);
#ifndef CONFIG_MTK_SYNC	/* m681 #129: real defs live in mediatek/sync/mtk_sync.c when MTK_SYNC=y */
void *timeline_create(const char *name) { return NULL; }
EXPORT_SYMBOL(timeline_create);
void timeline_inc(void *timeline, unsigned int val) {}
EXPORT_SYMBOL(timeline_inc);
#endif
void dprec_logger_reset_all(void) {}
EXPORT_SYMBOL(dprec_logger_reset_all);
void dprec_handle_option(const char *opt) {}
EXPORT_SYMBOL(dprec_handle_option);
void dprec_mmp_init(void) {}
EXPORT_SYMBOL(dprec_mmp_init);
void disp_init_ui_logging(void) {}
EXPORT_SYMBOL(disp_init_ui_logging);
unsigned int mmp_log_call_ratio;
EXPORT_SYMBOL(mmp_log_call_ratio);

unsigned int gCapturePriLayerDownX, gCapturePriLayerDownY, gCapturePriLayerNum, gCapturePriLayerEnable;
EXPORT_SYMBOL(gCapturePriLayerDownX);
EXPORT_SYMBOL(gCapturePriLayerDownY);
EXPORT_SYMBOL(gCapturePriLayerNum);
EXPORT_SYMBOL(gCapturePriLayerEnable);

void dprec_submit(struct dprec_logger_event *event, unsigned int val1, unsigned int val2) {}
EXPORT_SYMBOL(dprec_submit);
int external_display_get_info(void *info) { return 0; }
EXPORT_SYMBOL(external_display_get_info);
void dprec_mmp_dump_wdma_layer(void *cfg) {}
EXPORT_SYMBOL(dprec_mmp_dump_wdma_layer);
void dprec_mmp_dump_ovl_layer(void *cfg) {}
EXPORT_SYMBOL(dprec_mmp_dump_ovl_layer);

int fbconfig_get_esd_check(void) { return 0; }
EXPORT_SYMBOL(fbconfig_get_esd_check);
void dprec_logger_trigger(struct dprec_logger_event *event) {}
EXPORT_SYMBOL(dprec_logger_trigger);
void Panel_Master_DDIC_config(void) {}
EXPORT_SYMBOL(Panel_Master_DDIC_config);
void m6_led_dump_backlight_truth(void) {}
EXPORT_SYMBOL(m6_led_dump_backlight_truth);
void app_info_set(const char *name, unsigned int val) {}
EXPORT_SYMBOL(app_info_set);
void external_display_trigger(void *cfg) {}
EXPORT_SYMBOL(external_display_trigger);

void dprec_mmp_dump_rdma_layer(void *cfg) {}
EXPORT_SYMBOL(dprec_mmp_dump_rdma_layer);
void exit_pd_by_cmdq(void *handle) {}
EXPORT_SYMBOL(exit_pd_by_cmdq);
void enter_pd_by_cmdq(void *handle) {}
EXPORT_SYMBOL(enter_pd_by_cmdq);
void dprec_init(void) {}
EXPORT_SYMBOL(dprec_init);
int dprec_logger_get_result_value(unsigned int source, void *fps) { return 0; }
EXPORT_SYMBOL(dprec_logger_get_result_value);

void dprec_logger_frame_seq_end(struct dprec_logger_event *event) {}
EXPORT_SYMBOL(dprec_logger_frame_seq_end);
int dprec_option_enabled(unsigned int option) { return 0; }
EXPORT_SYMBOL(dprec_option_enabled);
unsigned int dprec_get_vsync_count(void) { return 0; }
EXPORT_SYMBOL(dprec_get_vsync_count);
void dprec_logger_frame_seq_begin(struct dprec_logger_event *event) {}
EXPORT_SYMBOL(dprec_logger_frame_seq_begin);
void dprec_stub_irq(unsigned int irq) {}
EXPORT_SYMBOL(dprec_stub_irq);
void dprec_stub_event(unsigned int event) {}
EXPORT_SYMBOL(dprec_stub_event);

/* m681 (2026-07-16): PBM (power budget manager) stub — needed by the eccci modem.
 * The 4.4 eccci core calls init_md_section_level(KR_MD1) once (modem_cldma.c:2891)
 * to register the modem with the power-budget manager. PBM is deferred on this
 * route: pbm_v1/ is gated on CONFIG_MACH_MT6757 (base/power/Makefile:23) and
 * mt6755's own mt_pbm.o is commented out (mt6755/Makefile:47), so nothing defines
 * it -> undefined reference at link. No-op it: the modem simply does not
 * participate in power budgeting, which is fine for modem stage 1 (image load +
 * handshake). Signature simplified to int per this file's convention (the linker
 * resolves by name; the real prototype takes enum pbm_kicker).
 * REMOVE this stub when PBM is un-deferred, or it will duplicate-symbol at link. */
void init_md_section_level(int kicker) {}
EXPORT_SYMBOL(init_md_section_level);
/* Same deal: the grafted mt6755 CLDMA platform calls this on modem power on/off
 * (eccci/mt6755/cldma_platform.c:740,835) to raise/drop the modem's power budget. */
void kicker_pbm_by_md(int kicker, bool status) {}
EXPORT_SYMBOL(kicker_pbm_by_md);

/* Camera sensor stubs — needed by imgsensor kd_camera_hw + kd_sensorlist.
 * m681 (2026-07-20): when the imgsensor stack is built, the REAL MCLK
 * provider (imgsensor/src/mt6755/mt_cam_mclk.c: SENINF+0x200/0x600 bit29
 * behind the oracle SCP_SYS_ISP clock chain) owns these symbols; the no-ops
 * here would silently starve every sensor of its master clock and turn each
 * detect into a fake "wrong sensor" NAK. Keep the no-ops ONLY for
 * configs without the camera stack, where nothing references them anyway. */
#if !IS_ENABLED(CONFIG_MTK_IMGSENSOR)
void ISP_MCLK1_EN(int en) {}
void ISP_MCLK2_EN(int en) {}
#endif
unsigned int abist_meter(unsigned int meter) { return 0; }
unsigned int mmdvfs_get_stable_isp_clk(void) { return 0; }

/* m681-49 skeleton (2026-08-27): deferred-subsystem stubs for the first
 * link milestone. REMOVE each when its real provider is un-deferred,
 * or it will duplicate-symbol at link. */
/* clk_buf_init: real one lives in mt_clkbuf_ctl.o (deferred — needs the
 * spm_v2 reg set; re-enable at the connectivity phase like the 4.4 lane did). */
bool clk_buf_init(void) { return false; }
EXPORT_SYMBOL(clk_buf_init);
/* pwrap_read: real one is pmic_wrap pwrap_hal_v1.o (Phase 2 pwrap carry).
 * mt_vcorefs_stub.o reads PMIC_VCORE_ADDR through it.
 * m681-49-disp: built (read-only WACS2) with CONFIG_MTK_PMIC_WRAP_HAL. */
#ifndef CONFIG_MTK_PMIC_WRAP_HAL
signed int pwrap_read(unsigned int adr, unsigned int *rdata)
{
	if (rdata)
		*rdata = 0;
	return -ENODEV;
}
EXPORT_SYMBOL(pwrap_read);
#endif

/* m681-49 skeleton: deferred-subsystem link stubs (signatures simplified
 * per this file's convention — the linker resolves by name; REMOVE each
 * when its real provider lands):
 *  - clk_buf_write_afcdac: clkbuf (mt_clkbuf_ctl.o, deferred to conn phase)
 *  - mt_ppm_userlimit_cpu_freq / update_userlimit_cpu_{core,freq} /
 *    mt_cpufreq_get_freq_by_idx: ppm/cpufreq (deferred to the DVFS phase)
 *  - usb_cable_connected: charger (deferred to the power phase); the
 *    mu3d UDC (CONFIG_USB_MU3D_DRV, mtk_usb.c) provides the real one
 */
void clk_buf_write_afcdac(void) {}
EXPORT_SYMBOL(clk_buf_write_afcdac);
unsigned int mt_ppm_userlimit_cpu_freq(unsigned int cluster_num, void *data) { return 0; }
EXPORT_SYMBOL(mt_ppm_userlimit_cpu_freq);
int update_userlimit_cpu_core(int kicker, int num, void *data) { return 0; }
EXPORT_SYMBOL(update_userlimit_cpu_core);
int update_userlimit_cpu_freq(int kicker, int num, void *data) { return 0; }
EXPORT_SYMBOL(update_userlimit_cpu_freq);
unsigned int mt_cpufreq_get_freq_by_idx(int id, int idx) { return 0; }
EXPORT_SYMBOL(mt_cpufreq_get_freq_by_idx);
#ifndef CONFIG_USB_MU3D_DRV
bool usb_cable_connected(void) { return false; }
EXPORT_SYMBOL(usb_cable_connected);
#endif

/* dump_i2c_cg_clk: the 4.4 lane has it in the unified i2c-mtk.c; this tree
 * runs the vendor i2c (misc/mediatek/i2c/mt6755) which lacks it. Debug-only
 * dump hook called from systracker fault dumps — no-op until the i2c path
 * is unified (or the vendor i2c grows the dump). */
void dump_i2c_cg_clk(void) {}
EXPORT_SYMBOL(dump_i2c_cg_clk);
