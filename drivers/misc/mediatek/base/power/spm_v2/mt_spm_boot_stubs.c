/*
 * m681/mt6755 SPM boot-milestone link stubs.
 *
 * The SPM deep-idle / SODI / dpidle / vcore-DVFS scenario implementations
 * (mt_spm_dpidle.c, mt_spm_sodi*.c, mt_spm_vcorefs_mt6755.c) are deferred for the
 * boot milestone (runtime PM is not needed to reach the early start_kernel /
 * setup_arch markers). The kept SPM core + suspend/sysfs files still reference
 * their scenario structs and a few helper entry points. Provide zeroed scenario
 * structs + no-op helpers so the kernel links; none of these code paths run at
 * boot (no suspend / idle / vcore-dvfs before adb). Drop this file when the SPM
 * low-power scenarios are un-deferred. See docs MT6755_4.4_BUILD_PROGRESS.md.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/types.h>
#include "mt_spm.h"
#include "mt_spm_internal.h"

/* Deferred low-power scenario descriptors (consumed by mt_spm_internal/_fs/_sleep
 * for sysfs + suspend; not entered at boot). Zeroed is safe because the entry
 * paths that would dereference .pwrctrl/.pcmdesc are never reached pre-adb. */
struct spm_lp_scen __spm_dpidle;
struct spm_lp_scen __spm_sodi;
struct spm_lp_scen __spm_sodi3;
struct spm_lp_scen __spm_vcore_dvfs;


wake_reason_t spm_go_to_sleep_dpidle(u32 spm_flags, u32 spm_data)
{
	return WR_NONE;
}

/* vcore-DVFS query/late-init — deferred; report a benign fixed state. */
int vcorefs_get_curr_ddr(void) { return 0; }
EXPORT_SYMBOL(vcorefs_get_curr_ddr);

int vcorefs_late_init_dvfs(void) { return 0; }
EXPORT_SYMBOL(vcorefs_late_init_dvfs);

/* clk_buf_init/clk_buf_write_afcdac stubs REMOVED (2026-07-17): mt_clkbuf_ctl.o
 * is un-deferred for the WiFi/BT consys bring-up and provides the real ones. */
