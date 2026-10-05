// SPDX-License-Identifier: GPL-2.0
/*
 * forge_m681_marker.c - persistent DRAM stage markers for m681 (MT6755) bring-up.
 *
 * m681 attempt3: ported from kernel-nokia-5.1-m681/init/forge_m681_marker.c
 * (the surviving vgdn sibling) into the native Meizu 3.18.35 99degree tree.
 *
 * Background
 * ----------
 * On MT6755 the kernel dies in early boot before the MTK ram_console driver
 * registers a console.  Anything emitted with pr_emerg() in start_kernel()
 * before that lives only in the early printk ring buffer, which is wiped by the
 * WDT reset.  We need a few breadcrumb bytes that survive a WDT.
 *
 * Target: physical 0x46100000 (size 0x1000).  This lies inside the DTB
 * reserved-memory node "@46000000" (reg <0 0x46000000 0 0x400000>) in
 * arch/arm64/boot/dts/mt6755.dtsi, so it is reserved from the kernel allocator
 * and is the vgdn-proven WDT-surviving marker location.  We do NOT use
 * 0x44400000 (clean ram_console; overwritten by recovery) or SPM 0x10006000
 * (SPM driver ioremap conflict).
 *
 * Write offsets are within the FIRST 512 BYTES of the region.  Multiple guarded
 * slots are written so a reader can cross-check signature/raw/inverted guards
 * and ignore any stale value.
 *
 * Recovery inspection (requires CONFIG_DEVMEM=y kernel = TWRP devmem build):
 *   adb shell devmem 0x46100100      -> rolling stage (0xF681xx)
 *   adb shell devmem 0x46100104      -> last reached stage (raw u32)
 *   adb shell devmem 0x46100108      -> 0x46524745 ('FRGE') if our kernel ran
 *   adb shell dd if=/dev/mem of=/sdcard/dram-head.bin bs=4096 skip=287489 count=1
 *
 * COLD-SAFE channel: each marker ALSO emits pr_emerg("[FORGE_M681] stage 0xNN")
 * so if the kernel reaches printk + ram_console the stage is in the normal log
 * that MTK kedump flushes to the expdb partition (FLASH, survives cold/BROM).
 *
 * Timing
 * ------
 * forge_m681_marker_early_init() must run AFTER early_ioremap_init() (stage
 * A04 in setup_arch).  Before that forge_m681_mark() is a silent no-op.  After
 * mm_init() the marker switches to a permanent ioremap() so the late_initcall
 * "early ioremap leak" check stays quiet.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/moduleparam.h>	/* m681 P4b: forge_poweroff_veto knob */
#include <linux/notifier.h>	/* m681 v48: atomic_notifier_chain_register + panic_notifier_list */
#include <linux/notifier.h>	/* (kept; kernel.h pulls it but be explicit for the forge reader) */
#include <asm/early_ioremap.h>
#include <linux/of_fdt.h>	/* Rung C: of_flat_dt_is_compatible for the forge_virt guard */
#include <linux/console.h>	/* m681 v78: forge console -> survivable DRAM log */
#include <linux/string.h>
#include <linux/hrtimer.h>	/* m681 v86: irq-context WDT kicker (kthread never scheduled) */
#include <linux/ktime.h>	/* m681 v86: ns_to_ktime / NSEC_PER_SEC */

#include <linux/memblock.h>	/* G1.3 head log: reserve + linear-map check */
#include <linux/workqueue.h>	/* G1.5 boot deadline */
#include <linux/blkdev.h>	/* G1.6 deadline: BCB write to "para" */
#include <linux/genhd.h>
#include <linux/buffer_head.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/reboot.h>
#include <linux/delay.h>	/* G1.7 bootguard: msleep */
#include <linux/math64.h>	/* BCB: div_u64 / do_div */
#include <asm/cacheflush.h>	/* G1.3 head log: __flush_dcache_area */

#include "forge_m681_marker.h"

/*
 * m681 v78: forge console.  Routes EVERY kernel printk (including userspace
 * init's /dev/kmsg writes) into a ring buffer in the preloader-reserved DRAM
 * region @0x44801000 (just above the forge marker @0x44800000).  That region
 * survives a WDT warm-reset AND the subsequent TWRP boot (unlike the normal
 * ram_console @0x44400000, which TWRP overwrites).  So after the boot kernel
 * hangs in userspace and the armed WDT warm-resets back to TWRP, we read this
 * buffer from /dev/mem and see exactly how far init got and where it stuck.
 * Layout @0x44801000: u32 head, u32 magic 'FLOG', then the text ring.
 */
/* The log ring lives INSIDE the already-working forge marker page
 * (forge_spm_base2 @0x44800000, mapped via the marker's working ioremap).  The
 * preloader region has NO "no-map" so a fresh ioremap of 0x44801000 is rejected
 * as RAM -> NULL.  The marker slots end at 0x200, so 0x200..0xFFF (~3.5 KB) is
 * free.  Read from TWRP at phys 0x44800200.  Layout: u32 head, u32 'FLOG', ring.
 */
#define FORGE_LOG_OFF	0x200U				/* within the marker page */
#define FORGE_LOG_END	0x1000U				/* m681 v97: REVERTED v96's 0x8000 — ioremap(32KB) of the reserved RAM region aliases the linear map with mismatched attrs (device vs cacheable) -> early die (0xFE) in start_kernel. One page only. */
#define FORGE_LOG_HDR	8U				/* head + magic */
#define FORGE_LOG_RING	(FORGE_LOG_END - FORGE_LOG_OFF - FORGE_LOG_HDR)
#define FORGE_LOG_MAGIC	0x464C4F47U			/* 'FLOG' */
static u32 forge_log_head;

static void forge_console_write(struct console *con, const char *s,
				unsigned int count)
{
	void __iomem *b = forge_spm_base2;
	unsigned int i;

	if (!b)
		return;
	for (i = 0; i < count; i++) {
		writeb(s[i], b + FORGE_LOG_OFF + FORGE_LOG_HDR +
		       (forge_log_head % FORGE_LOG_RING));
		forge_log_head++;
	}
	writel(forge_log_head, b + FORGE_LOG_OFF + 0);
	writel(FORGE_LOG_MAGIC, b + FORGE_LOG_OFF + 4);
}

static struct console forge_console = {
	.name	= "forgelog",
	.write	= forge_console_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED | CON_ANYTIME,
	.index	= -1,
};

/* m681 v27: primary readout moved 0x46100000 -> 0x444f0000 (minirdump reserved
 * node, mt6755.dtsi "minirdump-reserved-memory@444f0000", size 0x10000). RATIONALE
 * (FACT): 0x46100000 lies INSIDE lk-reserved (0x46000000+0x400000) so the warm
 * WDT->recovery path RE-RUNS lk and overwrites it with lk rodata/strings before
 * we can read it (confirmed v26: read back lk "[LK]jump to K64" strings, not our
 * mark). minirdump@0x444f0000 is reserved by BOTH our kernel and TWRP and is
 * written by NOBODY except the minirdump driver on an actual crash-dump (we hang,
 * not crash), so a forge mark written here survives lk-rerun + TWRP recovery +
 * the of_platform_populate probe phase. Read from TWRP:
 *   dd if=/dev/mem bs=512 skip=2238336 (=0x444f0000/512). */
#define FORGE_MARKER_PHYS_BASE	0x444f0000UL
#define FORGE_MARKER_REGION_SIZE	0x1000UL	/* one page for the header */

/*
 * Secondary (TWRP-readable) marker region. attempt #5 (2026-06-17):
 * 0x46100000 (lk-reserved) is overwritten by TWRP's recovery ramdisk
 * (ramdisk_addr 0x45000000) on the warm WDT->recovery path, so it reads back as
 * TWRP runtime data and the stage is lost. The pstore reserved-memory node
 * @0x44410000 (size 0xe0000) sits BELOW the recovery ramdisk, is >= the DRAM
 * base (so STRICT_DEVMEM permits `dd if=/dev/mem` readback from TWRP), and was
 * observed all-zero / preserved across a TWRP boot. We dual-write here; read the
 * last stage after a hang with:  dd if=/dev/mem bs=4 skip=0x44410104/4 count=1
 * (stage @0x44410104, sig 'FRGE' @0x44410108).
 */
/* m681 v26: moved off 0x44410000 (the pstore region, which the pstore driver
 * overwrites during of_platform_populate -> marker reads went valid=False/garbage
 * in the driver-probe phase) to a dedicated no-map reserved node at 0x44500000
 * (see forge-marker-reserved-memory@44500000 in mt6755.dtsi). Read from TWRP:
 * dd if=/dev/mem bs=512 skip=2238464 (=0x44500000/512). */
/* m681 v27: secondary independent survivor moved 0x44410000(pstore) -> 0x44800000
 * (preloader reserved node, "preloader-reserved-memory@44800000", size 0x100000).
 * pstore@0x44410000 is overwritten by the pstore driver DURING of_platform_populate
 * -> useless precisely in the probe-phase where we now hang. preloader region is
 * written only by the preloader (pre-lk) then static, reserved by both kernels,
 * so it is a second readout that also survives the probe phase + recovery fallback.
 * Read from TWRP:  dd if=/dev/mem bs=512 skip=2244608 (=0x44800000/512). */
#define FORGE_MARKER2_PHYS_BASE	0x44800000UL
#define FORGE_MARKER2_MAP_SIZE	0x8000UL	/* m681 v96: late ioremap maps 32KB for the enlarged forge console (within the 1MB preloader-reserved region) */

/*
 * m681 bring-up v56: MTK toprgu watchdog (0x10007000) kick channel.
 *
 * Root cause of the ~95s boot death (4-agent RE consensus, 2026-06-19): the WDT
 * is armed at postcore_initcall (mtk_wdt_probe, level 2) with a 30s timeout and
 * kicked once, but the kicker kthread only starts at late_initcall (level 7).
 * Every level 4/5/6 initcall therefore runs with NO kick, so the HW dog fires
 * mid-boot at whatever initcall happened to be running (v55 marker was
 * xfrm6_tunnel_init = device_initcall entry 471/494, 95% through level 6) --
 * this is a watchdog cut of a PROGRESSING boot, not a genuine hang.
 *
 * forge_m681_wdt_kick() is called once per initcall from do_one_initcall() to
 * reload the counter, letting a progressing boot reach /init while still letting
 * a genuinely-wedged initcall (never returns -> never kicks) trip the dog after
 * ~30s so the device still warm-resets to TWRP and the marker localises it.
 */
#define FORGE_WDT_PHYS_BASE	0x10007000UL
#define FORGE_WDT_REGION_SIZE	0x100UL
#define FORGE_WDT_RESTART_OFF	0x08	/* MTK_WDT_RESTART */
#define FORGE_WDT_RESTART_KEY	0x1971U	/* MTK_WDT_RESTART_KEY */
#define FORGE_WDT_MODE_OFF	0x00	/* MTK_WDT_MODE */
#define FORGE_WDT_LENGTH_OFF	0x04	/* MTK_WDT_LENGTH */
#define FORGE_WDT_MODE_KEY	0x22000000U
#define FORGE_WDT_MODE_ENABLE	0x00000001U
#define FORGE_WDT_MODE_EXTEN	0x00000004U
#define FORGE_WDT_MODE_IRQ	0x00000008U
#define FORGE_WDT_MODE_DUAL_MODE 0x00000040U
#define FORGE_WDT_MODE_AUTO_RESTART 0x00000010U
#define FORGE_WDT_LENGTH_KEY	0x00000008U
#define FORGE_WDT_TIMEOUT_SEC	30U

/* m681 v47: manual SWRST backstop — see forge_m681_wdt_kick(). */
#define FORGE_WDT_SWRST_OFF	0x14U
#define FORGE_WDT_SWRST_KEY	0x1209U

/* m681 #113: RGU reset forensics + request-line quiesce (57s silent-reset
 * hunt, earlier silent-reset investigation).
 * WDT_STA latches WHICH source caused the LAST warm reset (it is the only
 * artifact that distinguishes a counter-timeout reset (= hang, kickers
 * starved, dog bit later) from a request-LINE reset (thermal-direct / SPM /
 * EINT / debug — all fire silently, no kernel involvement).  WDT_REQ_MODE
 * says which request lines are allowed to reset us RIGHT NOW — including
 * whatever the preloader/LK left enabled at handoff, which no kernel code on
 * this port ever audits.  Bit values match
 * drivers/watchdog/mediatek/wdt/mt6755/mt_wdt.h. */
#define FORGE_WDT_STA_OFF	0x0CU
#define FORGE_WDT_NONRST2_OFF	0x24U
#define FORGE_WDT_REQ_MODE_OFF	0x30U
#define FORGE_WDT_REQ_MODE_KEY	0x33000000U
/* Every RGU reset-request LINE that is not the petted counter dog itself:
 * SPM_THERMAL(b0) SPM_SCPSYS(b1) EINT(b2) SYSRST(b3) THERMAL_DIRECT(b18)
 * DEBUG(b19).  On this port NO requester is calibrated/trusted (tscpu is
 * uncalibrated, SPM fw is donor-vintage, EINT debug key unused), so a set
 * bit here is a silent-reboot generator, not protection. */
#define FORGE_WDT_REQ_LINES	(0x00000001U | 0x00000002U | 0x00000004U | \
				 0x00000008U | 0x00040000U | 0x00080000U)
/* 3.18.140 m6-graft has ~600 module initcalls; threshold is set well above that
 * so natural initcall flow never trips it, but a pathological loop that keeps
 * calling do_one_initcall (recursive initcall loop, broken module_init chain,
 * kthread that re-enters do_one_initcall) cannot keep the WDT pet forever. */
#define FORGE_WDT_FORCE_SWRESET_THRESHOLD 4000U

/* Offsets inside the first 512 bytes (never zeroed by ram_console memset_io,
 * which starts at off_linux >= 512). */
#define FORGE_MARKER_LEGACY_OFF	0x100
#define FORGE_MARKER_SHADOW_OFF	0x180
#define FORGE_SLOT_ROLLING_OFF	0x00	/* 0xF6810000 | stage */
#define FORGE_SLOT_STAGE_OFF	0x04	/* raw stage */
#define FORGE_SLOT_SIG_OFF	0x08	/* magic signature 'FRGE' */
#define FORGE_SLOT_AUX_OFF	0x0C	/* auxiliary u32 payload */
#define FORGE_SLOT_SIG_INV_OFF	0x10	/* inverted signature guard */
#define FORGE_SLOT_AUX_INV_OFF	0x14	/* inverted auxiliary guard */
#define FORGE_SLOT_STAGE_INV_OFF	0x18	/* inverted raw stage guard */
#define FORGE_SLOT_ROLLING_INV_OFF	0x1C	/* inverted rolling guard */

#define FORGE_MARKER_MAGIC	0xF6810000U	/* 'F6810000' | stage */
#define FORGE_SIGNATURE		0x46524745U	/* ASCII 'FRGE' */

/*
 * forge_spm_base is exported so the marker engine is reachable from the inline
 * call sites in init/main.c and arch/arm64/kernel/setup.c.  It is updated twice
 * during boot: first by the early_ioremap path, then by the permanent ioremap
 * path.  Until the first update it is NULL and forge_m681_mark() is a no-op.
 */
void __iomem *forge_spm_base;
EXPORT_SYMBOL(forge_spm_base);

/* Secondary marker base (pstore @0x44410000); TWRP-readable. */
void __iomem *forge_spm_base2;
EXPORT_SYMBOL(forge_spm_base2);

/*
 * Rung C (qemu -M virt): forge_virt guard.
 *
 * Under `-M virt` every MTK physical address the forge engine touches is
 * WRONG: 0x444f0000/0x44800000 are unreserved guest RAM (scribbling there
 * corrupts pages the allocator will hand out), 0x10006000/0x10007000/
 * 0x10008000/0x10200000 sit inside virt's PCIe MMIO window, and the raw
 * `smc #0` in forge_enable_cpuxgpt() is UNDEFINED at EL1 because default
 * virt has no EL3 (QEMU_VIRT_RUNG_B.md blockers 1-3).
 *
 * One guard kills all of it: forge_m681_virt_detect() is called from
 * setup_arch() immediately after setup_machine_fdt() — the earliest point
 * where the flat DT is readable (early_init runs BEFORE the FDT is mapped,
 * so it cannot self-detect).  If the root compatible is "linux,dummy-virt",
 * set forge_virt=1 and NULL both marker bases (the two early fixmap slots
 * are abandoned exactly like early_ioremap_reset() abandons them on the
 * device; the only writes that happened before detection — stages 0x1E and
 * 0x35 — landed in RAM that is still outside memblock at that instant, so
 * nothing is corrupted).  Every later forge_m681_mark*() no-ops on the NULL
 * bases and forge_m681_marker_late_init() returns before any MTK ioremap or
 * the cpuxgpt SMC.  Zero effect on the real device: mt6755 DTs never carry
 * the "linux,dummy-virt" compatible.
 */
int forge_virt;
EXPORT_SYMBOL(forge_virt);

/* m681 chg-diag P4b (2026-07-17): userspace power-off veto, consumed by
 * sys_reboot (kernel/reboot.c). 1 = reboot(2) POWER_OFF/HALT are logged and
 * refused with -EPERM — diag default for the flash21_chgfix T+90 power-off
 * hunt (an orderly Android shutdown is a live suspect and no charger-file
 * gate can see it). RESTART/RESTART2 are never vetoed. 0 = stock behaviour.
 * Kernel-internal charger shutdown sites have their own gate
 * (charging_hw_sm5414.forge_chg_poweroff). Runtime:
 * /sys/module/forge_m681_marker/parameters/forge_poweroff_veto. */
int forge_poweroff_veto = 1;
module_param(forge_poweroff_veto, int, 0644);

void __init forge_m681_virt_detect(void)
{
	if (!initial_boot_params)
		return;
	if (!of_flat_dt_is_compatible(of_get_flat_dt_root(),
				      "linux,dummy-virt"))
		return;

	forge_virt = 1;
	forge_spm_base = NULL;
	forge_spm_base2 = NULL;
	pr_info("[FORGE_M681] forge_virt: qemu -M virt detected -> MTK marker/WDT/GPT/cpuxgpt/SPM hw init DISABLED\n");
}

/* m681 v56: ioremap of the MTK toprgu watchdog block (see forge_m681_wdt_kick). */
static void __iomem *forge_wdt_base;

/* m681 #113: RGU state latched at boot, BEFORE forge_m681_wdt_arm() writes
 * MODE (a MODE write may clear STA).  Re-printed by the late quiesce so the
 * values survive expdb ring rollover. */
static u32 forge_rgu_boot_sta;
static u32 forge_rgu_boot_reqmode;

/* m681 v119: GPT2 free-run counter (phys 0x10008000, counter at +0x28) for the
 * "does 0x10008028 actually count?" probe in forge_m681_wdt_kick (runs every
 * initcall -> guaranteed to execute before any frozen-timer hang). */
static void __iomem *forge_gpt_base;

/* m681 #121/#124: topckgen + apmixedsys mappings for the GPU clock-state
 * snapshot.  Mapped in forge_m681_marker_late_init (start_kernel era) so
 * even the 0.75s kbase-probe-time power-on is observable; the keywatch
 * thread keeps a lazy-map fallback. */
static void __iomem *forge_topck_base;
static void __iomem *forge_apmix_base;

/* m681 #124 (port318to44 ask): the 5s keywatch cannot see the sub-second
 * first-job window, and the whole D3 question reduces to one bit — is
 * MMPLL_CON0.EN set while kbase_jd_submit runs?  Inline snapshot, callable
 * from any context (three AO readls + pr_emerg), invoked from inside the
 * existing first-N marker blocks at submit/MFG-enable time. */
void forge_m681_gpuclk_snap(const char *tag)
{
	u32 c1 = 0xDEAD, p0 = 0xDEAD, p1 = 0xDEAD;

	if (forge_topck_base)
		c1 = readl(forge_topck_base + 0x50);
	if (forge_apmix_base) {
		p0 = readl(forge_apmix_base + 0x240);
		p1 = readl(forge_apmix_base + 0x244);
	}
	pr_emerg("[FORGE_M681] #124 gpuclk@%s: CLK_CFG_1=0x%08x (mfg_sel=%u) MMPLL_CON0=0x%08x (EN=%u) CON1=0x%08x\n",
		 tag, c1, (c1 >> 24) & 0x3, p0, p0 & 0x1, p1);
}
EXPORT_SYMBOL(forge_m681_gpuclk_snap);

/*
 * m681 v57: the driver's own DT-mapped (of_iomap) toprgu base, set in
 * mtk_wdt_probe() at postcore_initcall (level 2).  This is the PROVEN-functional
 * mapping (the WDT is demonstrably armed via it).  Our private start_kernel
 * ioremap() of 0x10007000 (forge_wdt_base) may return NULL/non-functional that
 * early for SoC register space, so prefer toprgu_base once it is live.
 */
extern void __iomem *toprgu_base;

/* v57: per-initcall kick counter, mirrored into the TWRP-readable marker region
 * at offset 0x40 (count) and 0x44 (which bases were non-NULL) so a post-reset
 * readback proves whether the kick ran and how far the boot progressed. */
static u32 forge_wdt_kick_count;

/* Diagnostic offsets in the marker page (free; real slots start at 0x100). */
#define FORGE_DIAG_KICKCNT_OFF	0x40
#define FORGE_DIAG_KICKFLAGS_OFF	0x44
/* m681 v50: initcall-level phase tracking diag offsets. */
#define FORGE_DIAG_LEVEL_ENTER_BASE	0xB0
#define FORGE_DIAG_LEVEL_DONE_BASE	0xC0
#define FORGE_DIAG_LASTGOOD_SEQ_OFF	0xD0
#define FORGE_DIAG_LASTGOOD_SEQ_INV_OFF	0xD4
#define FORGE_DIAG_BOOTPHASE_OFF	0xD8
#define FORGE_DIAG_WDTK_HEARTBEAT_OFF	0xFC	/* m681 v87: kicker heartbeat — moved off 0xDC which COLLIDED with LEVEL_DONE_BASE(0xC0)+7*4=0xDC (level tracker clobbered it -> false heartbeat=0 in v81-v86) */
#define FORGE_DIAG_WDTK_INIT_OFF	0xE0	/* m681 v84: proof kicker initcall ran (A11E armed) */
#define FORGE_DIAG_EMMC_CTR_BASE	0xE4	/* m681 v87: eMMC checkpoint counters slot[0..5]=C7,C8,C9,CA,C3,C5 (0xE4..0xF8); free zone 0xE4..0xFC, before marker slots @0x100 */

static const unsigned long forge_m681_slot_offsets[] = {
	FORGE_MARKER_LEGACY_OFF,
	0x120, 0x140, 0x160, FORGE_MARKER_SHADOW_OFF,
	0x1a0, 0x1c0, 0x1e0,
};

static void forge_m681_write_slot(void __iomem *base, unsigned long off,
				  u8 stage, u32 aux)
{
	u32 raw = (u32)stage;
	u32 rolling = FORGE_MARKER_MAGIC | raw;

	/*
	 * Guard words come before the final rolling marker.  A reader trusts a
	 * slot only when signature, rolling, raw stage, and inverted guards agree.
	 */
	writel(FORGE_SIGNATURE, base + off + FORGE_SLOT_SIG_OFF);
	writel(aux, base + off + FORGE_SLOT_AUX_OFF);
	writel(~FORGE_SIGNATURE, base + off + FORGE_SLOT_SIG_INV_OFF);
	writel(~aux, base + off + FORGE_SLOT_AUX_INV_OFF);
	writel(raw, base + off + FORGE_SLOT_STAGE_OFF);
	writel(~raw, base + off + FORGE_SLOT_STAGE_INV_OFF);
	writel(rolling, base + off + FORGE_SLOT_ROLLING_OFF);
	writel(~rolling, base + off + FORGE_SLOT_ROLLING_INV_OFF);
}

void forge_m681_mark_aux(u8 stage, u32 aux)
{
	unsigned int i;

	/* m681 v12: write the marker slots FIRST and DO NOT pr_emerg in the hot
	 * path.  Previously this did pr_emerg() BEFORE the writels, so if printk
	 * ever deadlocks/blocks (console_sem, logbuf, an undrained console) the
	 * mark would (a) block the boot thread and (b) hide the true furthest
	 * stage (the next mark hangs in pr_emerg before recording).  v11 showed a
	 * deterministic stop at 0xD7 with NO exception and NO IRQ (count=0) and
	 * 0xC1 absent -- exactly what a pr_emerg hang on the NEXT mark looks like.
	 * Pure writel makes the marker truthful and cannot block boot. */
	if (forge_spm_base)
		for (i = 0; i < ARRAY_SIZE(forge_m681_slot_offsets); i++)
			forge_m681_write_slot(forge_spm_base,
					      forge_m681_slot_offsets[i], stage, aux);

	if (forge_spm_base2)
		for (i = 0; i < ARRAY_SIZE(forge_m681_slot_offsets); i++)
			forge_m681_write_slot(forge_spm_base2,
					      forge_m681_slot_offsets[i], stage, aux);
}
EXPORT_SYMBOL(forge_m681_mark_aux);

void forge_m681_mark(u8 stage)
{
	forge_m681_mark_aux(stage, 0);
}
EXPORT_SYMBOL(forge_m681_mark);

/* m681 v87: increment a TWRP-readable checkpoint COUNTER in the clean diag zone
 * (0xE4..0xF8).  Unlike the 8-deep rolling stage slots (which only keep the
 * last marks), these counters reveal HOW MANY times a checkpoint executed —
 * distinguishing "stuck at C7 (count 1, never C8)" from "C7->CA retry loop
 * cycling N times".  read-modify-write; bit-rot may perturb low bits but the
 * magnitude (1 vs hundreds) is the signal. */
void forge_m681_bump(unsigned int slot)
{
	u32 off = FORGE_DIAG_EMMC_CTR_BASE + (slot & 0x7u) * 4u;

	if (forge_spm_base)
		writel(readl(forge_spm_base + off) + 1u, forge_spm_base + off);
	if (forge_spm_base2)
		writel(readl(forge_spm_base2 + off) + 1u, forge_spm_base2 + off);
}
EXPORT_SYMBOL(forge_m681_bump);

/* m681 v95: write a raw u32 to a TWRP-readable diag word (clean zone, off masked
 * to the page).  Unlike the wrapping forge console, this survives reliably for a
 * value captured deep in an async kworker (e.g. the VEMC pwrap readback). */
void forge_m681_diag(unsigned int off, u32 val)
{
	off &= 0x3FCu;
	if (forge_spm_base2)
		writel(val, forge_spm_base2 + off);
	if (forge_spm_base)
		writel(val, forge_spm_base + off);
}
EXPORT_SYMBOL(forge_m681_diag);

/* m681: record a fatal-fault snapshot from die(). PC -> slots as stage 0xFE
 * (aux=pc low32); LR and ESR -> fixed diag offsets 0x48/0x4c of the marker
 * page so all three survive the reset and are readable from TWRP. */
void forge_m681_mark_fault(u32 pc, u32 lr, u32 esr)
{
	forge_m681_mark_aux(0xFE, pc);
	if (forge_spm_base) {
		writel(lr,  forge_spm_base  + 0x48);
		writel(esr, forge_spm_base  + 0x4c);
	}
	if (forge_spm_base2) {
		writel(lr,  forge_spm_base2 + 0x48);
		writel(esr, forge_spm_base2 + 0x4c);
	}
}
EXPORT_SYMBOL(forge_m681_mark_fault);

/*
 * m681 v10: universal fault/panic net.  die() (0xFE) only catches faults that
 * route through it; bad_mode->panic and direct panic() bypass it.  These hooks
 * snapshot the FIRST fatal event from ANY sink (do_mem_abort unhandled=0xFD,
 * bad_mode=0xFC, panic=0xFB) so we learn whether the 0xD7->reset is a die-able
 * fault, a bad_mode/panic, or (if NOTHING latches) a true IRQ-off hang.
 *
 * Raw slot writes ONLY -- no pr_emerg: we may be in a wedged/lock-held context
 * where printk would deadlock or re-fault.  A one-shot latch keeps the ROOT
 * event (first to fire) instead of a downstream panic overwriting it.
 *
 * diag layout for the latched snapshot:
 *   slot @0x100/@0x180 stage = sink id, aux = PC low32
 *   0x48 = PC low32   0x4c = ESR   0x50 = fault addr/aux2   0x54 = sink id
 */
static int forge_fault_latched;

void forge_m681_fault_snap(u8 stage, u32 pc, u32 esr, u32 addr)
{
	if (forge_fault_latched)
		return;
	forge_fault_latched = 1;

	if (forge_spm_base) {
		forge_m681_write_slot(forge_spm_base, FORGE_MARKER_LEGACY_OFF, stage, pc);
		forge_m681_write_slot(forge_spm_base, FORGE_MARKER_SHADOW_OFF, stage, pc);
		writel(pc,    forge_spm_base + 0x48);
		writel(esr,   forge_spm_base + 0x4c);
		writel(addr,  forge_spm_base + 0x50);
		writel(stage, forge_spm_base + 0x54);
	}
	if (forge_spm_base2) {
		forge_m681_write_slot(forge_spm_base2, FORGE_MARKER_LEGACY_OFF, stage, pc);
		forge_m681_write_slot(forge_spm_base2, FORGE_MARKER_SHADOW_OFF, stage, pc);
		writel(pc,    forge_spm_base2 + 0x48);
		writel(esr,   forge_spm_base2 + 0x4c);
		writel(addr,  forge_spm_base2 + 0x50);
		writel(stage, forge_spm_base2 + 0x54);
	}
}
EXPORT_SYMBOL(forge_m681_fault_snap);

/*
 * m681 v11: per-IRQ trace from the EL1 IRQ entry (gic_handle_irq).  Records a
 * free-running count, the last hwirq, and the INTERRUPTED pc into diag offsets
 * 0x58/0x5c/0x60.  Purpose: the 0xD7->silent-reset is not a CPU exception
 * (v10 proved no die/bad_mode/abort/panic), so the leading hypothesis is an
 * IRQ STORM (a board peripheral asserting an interrupt that no loaded driver
 * clears yet -- a classic MTK bring-up failure, and board/DTB-dependent which
 * matches m6==config-but-boots).  If post-reset count is enormous and the
 * interrupted pc sits in the 0xD7 window -> storm CONFIRMED + the exact irq.
 * No printk; writel only; no-op until the marker region is mapped.
 */
static u32 forge_irq_count;

void forge_m681_irq_trace(u32 irqnr, u32 pc)
{
	forge_irq_count++;
	if (forge_spm_base2) {
		writel(forge_irq_count, forge_spm_base2 + 0x58);
		writel(irqnr,           forge_spm_base2 + 0x5c);
		writel(pc,              forge_spm_base2 + 0x60);
	}
	if (forge_spm_base) {
		writel(forge_irq_count, forge_spm_base + 0x58);
		writel(irqnr,           forge_spm_base + 0x5c);
		writel(pc,              forge_spm_base + 0x60);
	}
}
EXPORT_SYMBOL(forge_m681_irq_trace);

/*
 * forge_m681_wdt_arm - arm the MTK toprgu HW watchdog into single-mode
 * hw-reset.  Called exactly once, from forge_m681_marker_late_init()
 * (post mm_init, before any initcalls), using the post-mm_init ioremap
 * of FORGE_WDT_PHYS_BASE — the path that l681 M18 proved live (kick_count
 * =676, kickflags=0xC0DE0011 = both base mappings valid).
 *
 * v44 WDT was in platform.c denylist and NOT armed → no reset path on hang.
 * v44b tried to arm it by direct writel from start_kernel (pre-mm_init) —
 * BROKE boot (kick_count=0): that mapping path is non-functional for SoC
 * register space that early.  v45 tried instead to let mtk_wdt_probe run
 * and apply mode_config there — but earlier board tests: probe calls
 * request_irq at mtk_wdt.c:769 BEFORE the v45 single-mode mode_config at
 * line 815 is reached, and on the graft tree that request_irq path appears
 * to wedge; WDT is left in preloader dual-mode+IRQ and AXI bus-hang later
 * cannot deliver the IRQ → no SWRST, dead device (battery pull = cold
 * reset = marker wiped).
 *
 * v47 root fix: leave mtk_wdt_probe in the denylist (it doesn't trust the
 * GIC request_irq path), and arm the WDT ourselves via the l681-proven post-
 * mm_init ioremap, in MODE read-modify-write ONLY — NO LENGTH reset.
 *
 * NO-LENGTH-WRITE is the critical v44b lesson: writing LENGTH resets the
 * preloader-running counter; if the WDT was already counting down, the reset
 * can corrupt the state (subsequent pet may not latch, watchdog might expire
 * immediately or never).  Instead READ the current MODE the preloader left,
 * clear ONLY DUAL_MODE (0x40) and IRQ (0x08), set KEY|ENABLE|EXTEN|
 * AUTO_RESTART, write back once.  Preloader's 30s LENGTH survives untouched
 * and is the timeout we want.  Then a single RESTART_KEY pet so any pre-boot
 * timeout count is reset to the full 30s window.
 *
 * Readback snapshot at 0xE6/0xE7 (mode/length) lets a post-reset marker
 * decode PROVE the armed state from recovery — the only evidence we had
 * before was the driver's pr_debug (invisible pre-console).
 */
static void forge_m681_wdt_arm(void)
{
	void __iomem *b = forge_wdt_base;
	u32 mode;

	if (!b)
		return;

	mode = readl(b + FORGE_WDT_MODE_OFF);
	/* Clear DUAL_MODE + IRQ (preloader arms dual-mode+IRQ).  Keep ENABLE
	 * (already set by preloader — we re-assert defensively below).  Do NOT
	 * touch LENGTH (FORGE_WDT_LENGTH_OFF): preloader set a 30s timeout and
	 * resetting it mid-count is the v44b root cause. */
	mode &= ~(FORGE_WDT_MODE_DUAL_MODE | FORGE_WDT_MODE_IRQ);
	mode |= FORGE_WDT_MODE_KEY | FORGE_WDT_MODE_ENABLE |
		FORGE_WDT_MODE_EXTEN | FORGE_WDT_MODE_AUTO_RESTART;
	writel(mode, b + FORGE_WDT_MODE_OFF);

	/* m681 session-4 #97 (TRACK-A1): extend the WDT timeout to the hardware
	 * maximum.  earlier kernel logs: our 4.4 kernel boots to
	 * t=22.8s + adb (SurfaceFlinger up, NVRAM restoring) but resets before
	 * boot_completed (bootreason=wdt_by_pass_pwk).  The preloader LENGTH (0x5000)
	 * gives only ~20s, too short for the slow mediaserver/agps/NVRAM userspace
	 * bring-up.  MTK_WDT_LENGTH = (count[10:0] << 5) | KEY(0x08); 0x7FF is the max
	 * count (~3x -> ~60-95s), buying the boot time to reach a stable state so the
	 * live pwrap -init probe can run.  Written on the PROVEN post-mm_init mapping
	 * (same one the MODE write above uses) and latched by the RESTART pet below --
	 * NOT the v44b hazard (that was a LENGTH write from a non-functional
	 * pre-mm_init mapping with no restart).  Reversible: delete this one writel. */
	writel((0x7FFU << 5) | FORGE_WDT_LENGTH_KEY, b + FORGE_WDT_LENGTH_OFF);

	/* pet once: reload the (now-extended) LENGTH counter to its full window */
	writel(FORGE_WDT_RESTART_KEY, b + FORGE_WDT_RESTART_OFF);

	/* snapshot armed state into the forge SRAM marker (rolling-stage
	 * channel); a post-reset readback from recovery decodes aux -> the
	 * raw MODE/LENGTH we just wrote, proving arm-before-hang. */
	forge_m681_mark_aux(0xE6, readl(b + FORGE_WDT_MODE_OFF));
	forge_m681_mark_aux(0xE7, readl(b + FORGE_WDT_LENGTH_OFF));

	pr_emerg("[FORGE_M681] v47 WDT armed single-mode-hwreset (RMW, no LENGTH reset): MODE=0x%x LENGTH=0x%x\n",
		 readl(b + FORGE_WDT_MODE_OFF),
		 readl(b + FORGE_WDT_LENGTH_OFF));
}

/*
 * forge_m681_wdt_disarm - DISABLE the MTK toprgu HW watchdog.
 *
 * m681 v59: forge_m681_wdt_kick() only pets the WDT once per do_one_initcall().
 * When do_initcalls() finishes the kicks stop, so the armed 30s HW watchdog
 * fires ~30s into userspace and resets before adbd/USB-gadget can come up
 * (mtk_wdt driver is denylisted, so nothing in userspace pets it).  Call this
 * right after the last initcall (POST_BASIC_SETUP, init/main.c) so userspace
 * runs watchdog-free and adb can enumerate.  Writes MODE with the KEY but the
 * ENABLE bit cleared -> watchdog disabled.  Trade-off: a userspace hang no
 * longer auto-resets (boot=TWRP harbor + manual/mtkclient recovery covers it).
 */
void forge_m681_wdt_disarm(void)
{
	void __iomem *b = forge_wdt_base ? forge_wdt_base : toprgu_base;

	if (!b)
		return;
	/* KEY only, ENABLE/EXTEN/AUTO_RESTART cleared -> WDT off. */
	writel(FORGE_WDT_MODE_KEY, b + FORGE_WDT_MODE_OFF);
	forge_m681_mark_aux(0xE5, readl(b + FORGE_WDT_MODE_OFF));
	pr_emerg("[FORGE_M681] v59 WDT DISARMED for userspace: MODE=0x%x\n",
		 readl(b + FORGE_WDT_MODE_OFF));
}
EXPORT_SYMBOL(forge_m681_wdt_disarm);

/*
 * m681 v120: jiffies-TICK-driven self-rearming WDT kicker. The forge per-initcall
 * kick (forge_m681_wdt_kick) only fires while do_initcalls() runs; once the kernel
 * hands to userspace the kicks stop and the ~30s HW dog warm-resets ~30s in =
 * the bootloop. Prior userspace kickers failed on this graft: a kthread is never
 * cleanly scheduled and an hrtimer never fires (frozen ktime). BUT the timer WHEEL
 * (timer_list) is serviced by the GPT jiffies tick, which WORKS here -> a
 * self-rearming timer_list keeps the dog petted through userspace WITHOUT relying
 * on the frozen arch timer. This is NOT a disarm: on a genuine HARD hang (tick
 * stops / IRQs off) the timer stops firing and the dog still warm-resets, so the
 * DRAM marker lifeline is preserved ([[feedback_never_disarm_wdt]]). Kick every
 * 8s (dog timeout ~30s). Lazily armed on the first forge_m681_wdt_kick (i.e. the
 * first initcall, after time_init() so the tick is live). */
static struct timer_list forge_wdt_ticker;
static int forge_wdt_ticker_armed;

static void __maybe_unused forge_wdt_ticker_fn(unsigned long data)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;

	if (b) {
		/* m681 #126: RECLAIM the dog from the stock WDK. #125 device
		 * (expdb125, WDTRSTB_STATUS=1) proves the ~56s reset is the RGU
		 * dog, NOT any DISP/cmdq software timeout (all #125 probes silent).
		 * The stock WDK arms dual-mode+IRQ (RGU_MODE 0x15->0x5d) ~26s in and
		 * the dog then bites ~30s later despite THIS ticker's RESTART — the
		 * WDK's dual/IRQ MODE (and its own shorter LENGTH) defeat a plain
		 * restart. Re-assert forge single-hwreset MODE + max LENGTH every
		 * tick so the RESTART below reliably reloads the counter. Dog stays
		 * ENABLE'd -> a genuine tick-stop hang still warm-resets to TWRP
		 * with markers intact ([[feedback_never_disarm_wdt]]) — this is a
		 * reclaim, NOT a disarm. */
		u32 mode = readl(b + FORGE_WDT_MODE_OFF);

		{
			static int _hb126;

			if (_hb126 < 16) {
				_hb126++;
				pr_emerg("[FORGE_M681] #126 wdt-ticker tick#%d preMODE=0x%x preLEN=0x%x\n",
					 _hb126, mode, readl(b + FORGE_WDT_LENGTH_OFF));
			}
		}

		mode &= ~(FORGE_WDT_MODE_DUAL_MODE | FORGE_WDT_MODE_IRQ);
		mode |= FORGE_WDT_MODE_KEY | FORGE_WDT_MODE_ENABLE |
			FORGE_WDT_MODE_EXTEN | FORGE_WDT_MODE_AUTO_RESTART;
		writel(mode, b + FORGE_WDT_MODE_OFF);
		writel((0x7FFU << 5) | FORGE_WDT_LENGTH_KEY, b + FORGE_WDT_LENGTH_OFF);
		writel(FORGE_WDT_RESTART_KEY, b + FORGE_WDT_RESTART_OFF);
	}
	/* breadcrumb: ticker heartbeat into the kicker-init diag slot (0xE0). */
	if (forge_spm_base2)
		writel(0x71C0E000u | (forge_wdt_kick_count & 0xFFFFu),
		       forge_spm_base2 + FORGE_DIAG_WDTK_INIT_OFF);
	mod_timer(&forge_wdt_ticker, jiffies + msecs_to_jiffies(8000));
}

static void __maybe_unused forge_wdt_ticker_start(void)
{
	if (forge_wdt_ticker_armed)
		return;
	forge_wdt_ticker_armed = 1;
	setup_timer(&forge_wdt_ticker, forge_wdt_ticker_fn, 0);
	mod_timer(&forge_wdt_ticker, jiffies + msecs_to_jiffies(8000));
	pr_emerg("[FORGE_M681] v120 timer_list WDT ticker armed (jiffies tick, 8s)\n");
}

/*
 * m681 #105: arm the v120 tick-driven WDT ticker at late_initcall so the forge dog stays
 * petted across the kernel->userspace handoff. Boot #104 (SMP-PSCI + display) reaches
 * SurfaceFlinger/BootAnimation, but the forge dog is only kicked per-initcall (v122), so
 * once do_initcalls() ends it warm-resets ~30s in = the observed bootloop (adb up ~42s
 * then reset). The ticker pets via the GPT jiffies tick (verified live on this graft), so a
 * HARD hang (tick stops / IRQs off) still warm-resets to TWRP with DRAM markers intact —
 * this is NOT a disarm ([[feedback_never_disarm_wdt]]). Userspace boot can now progress to
 * boot_completed; re-enable stock mtk_wdt later for proper userspace WDT management.
 */
static int __init forge_wdt_ticker_late_init(void)
{
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	forge_wdt_ticker_start();
	return 0;
}
late_initcall(forge_wdt_ticker_late_init);

/*
 * forge_m681_wdt_kick - pet the MTK toprgu watchdog from do_one_initcall().
 *
 * No-op until forge_m681_marker_late_init() maps the toprgu block (which happens
 * in start_kernel(), before any do_initcalls() runs).  Writing the restart key
 * reloads the WDT counter (same effect as the driver's mtk_wdt_restart()), which
 * keeps a progressing boot alive across the kicker-less level 4/5/6 initcall
 * window without masking a real per-initcall hang.
 */
void forge_m681_wdt_kick(void)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;
	u32 flags;

	forge_wdt_kick_count++;
	if (b)
		writel(FORGE_WDT_RESTART_KEY, b + FORGE_WDT_RESTART_OFF);

	/* v122 REMOVED: forge_wdt_ticker_start() — with the timer now FIXED (cpuxgpt
	 * enabled, jiffies/hrtimers live), the tick-driven ticker actually FIRES and
	 * keeps the dog kicked even during a HANG, which DEFEATS the marker-preserving
	 * warm reset -> a hung boot hard-locks and needs a battery pull (the v122
	 * mistake). Leave the dog UNKICKED past initcalls so a hang warm-resets to the
	 * TWRP harbor (markers survive, no battery pull). [[feedback_never_disarm_wdt]]
	 * Proper userspace WDT mgmt = re-enable stock mtk_wdt later, now that the
	 * timer works. */

	/* m681 v47: manual SWRST backstop (early-boot reset handling).  If the
	 * initcall path is recursing oddly (kick_count climbing past the total
	 * realistic initcall count of ~600 for 3.18.140 m6-graft, threshold
	 * well above that) while system_state is still pre-RUNNING, the chip
	 * must NOT be left alive on WDT pets from a broken/looping do_one_initcall
	 * caller.  Write the MTK_WDT_SWRST key directly — same hardware reset
	 * path as wdt_arch_reset(): a chip-wide warm reset that PRESERVES the
	 * preloader reserved SRAM marker @0x44800000 (verified since l681 M1).
	 * A 0xEA mark right before the SWRST records in the next-recovery
	 * marker that we forced the reset (vs the natural 30s self-arm timeout
	 * which would leave the last-initcall aux as the wedged fn).  DIY only:
	 * the natural 30s self-arm expiry handles the common hang case; this is
	 * a guarantee for the rare "kicks keep coming but boot never progresses"
	 * failure mode that a single WDT would otherwise pet forever. */
	if (system_state < SYSTEM_RUNNING &&
	    forge_wdt_kick_count >= FORGE_WDT_FORCE_SWRESET_THRESHOLD && b) {
		forge_m681_mark(0xEA);
		writel(FORGE_WDT_SWRST_KEY, b + FORGE_WDT_SWRST_OFF);
	}

	/* TWRP-readable breadcrumb: how many initcalls kicked, and which base
	 * was live (bit0=our ioremap, bit4=driver toprgu_base). */
	flags = 0xC0DE0000U | (toprgu_base ? 0x10 : 0) | (forge_wdt_base ? 0x01 : 0);
	if (forge_spm_base2) {
		writel(forge_wdt_kick_count, forge_spm_base2 + FORGE_DIAG_KICKCNT_OFF);
		writel(flags, forge_spm_base2 + FORGE_DIAG_KICKFLAGS_OFF);
	}
	if (forge_spm_base) {
		writel(forge_wdt_kick_count, forge_spm_base + FORGE_DIAG_KICKCNT_OFF);
		writel(flags, forge_spm_base + FORGE_DIAG_KICKFLAGS_OFF);
	}

	/* m681 v119: GPT2-counts probe. Runs every initcall (so it executes well
	 * before any frozen-timer hang during device_initcalls). Latches the first
	 * GPT2 reading, then on every later kick writes the delta to diag 0xDC,
	 * sentinel 0xD2 in the high byte (proves it ran), bit23 = "counter moved",
	 * low 22 bits = delta. Decisive answer to "does 0x10008028 actually count?"
	 * — if frozen, the whole GPT2-as-clocksource (TIMERFIX) approach is dead and
	 * only the firmware path (Flyme preloader/LK) can unfreeze the real timer. */
	if (forge_gpt_base) {
		static u32 g0;
		static int gset;
		u32 gn = readl(forge_gpt_base + 0x28);
		u32 pk;
		if (!gset) { g0 = gn; gset = 1; }
		pk = 0xD2000000u | (((gn - g0) != 0) ? (1u << 23) : 0u) |
		     ((gn - g0) & 0x3FFFFFu);
		if (forge_spm_base2)
			writel(pk, forge_spm_base2 + 0xDC);
		if (forge_spm_base)
			writel(pk, forge_spm_base + 0xDC);
	}
}
EXPORT_SYMBOL(forge_m681_wdt_kick);

/*
 * m681 v86: userspace-surviving WDT kicker via HRTIMER (was a kthread v81-v85).
 *
 * Earlier kernel logs showed: the kthread variant was CREATED ok (arm-proof@0xE0 = A11E)
 * but its loop body NEVER ran (heartbeat@0xDC stayed 0).  On this SMP-disabled,
 * HPS-skipped single-CPU graft the kicker kthread never got a timeslice, so
 * the armed 30s HW-WDT still guillotined userspace before adbd/eMMC could
 * settle (boot looped on the dog).  An hrtimer fires from the timer-interrupt
 * path, INDEPENDENT of thread scheduling, so it pets the dog even when no
 * thread yields.  Safety property PRESERVED: a genuine AXI/APB bus wedge stalls
 * the CPU on the un-acked MMIO access and takes NO interrupts, so the hrtimer
 * also stops firing -> dog no longer petted -> ~30s warm-reset to TWRP, exactly
 * as before.  Pets the dog DIRECTLY (not via forge_m681_wdt_kick) to avoid that
 * path's pre-RUNNING SWRST backstop.  heartbeat -> diag @0xDC (proves the timer
 * actually fires); arm-proof -> diag @0xE0 (A11E).
 * Rollback: delete the core_initcall(forge_wdt_hrtimer_init) line.
 */
static struct hrtimer forge_wdt_hrtimer;
static u32 forge_wdt_beat;
#define FORGE_WDT_KICK_NS	(5ULL * NSEC_PER_SEC)	/* 5s, ~6x margin under 30s WDT */

static enum hrtimer_restart forge_wdt_hrtimer_fn(struct hrtimer *t)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;

	if (b)
		writel(FORGE_WDT_RESTART_KEY, b + FORGE_WDT_RESTART_OFF);
	forge_wdt_beat++;
	if (forge_spm_base2)
		writel(0xC0DE0000U | (forge_wdt_beat & 0xFFFFU),
		       forge_spm_base2 + FORGE_DIAG_WDTK_HEARTBEAT_OFF);
	if (forge_spm_base)
		writel(0xC0DE0000U | (forge_wdt_beat & 0xFFFFU),
		       forge_spm_base + FORGE_DIAG_WDTK_HEARTBEAT_OFF);
	hrtimer_forward_now(t, ns_to_ktime(FORGE_WDT_KICK_NS));
	return HRTIMER_RESTART;
}

static int __init forge_wdt_hrtimer_init(void)
{
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	/* v123 DISABLED: do NOT arm the hrtimer WDT kicker. It was added (v86) as a
	 * frozen-timer workaround, but the arch timer is NOW FIXED (cpuxgpt enabled),
	 * so this CLOCK_MONOTONIC hrtimer actually FIRES every 5s and keeps the dog
	 * kicked through userspace AND during a HANG -> defeats the marker-preserving
	 * warm reset -> hung boot hard-locks -> battery pull (the v122/v123 mistake;
	 * THIS hrtimer, not just the timer_list ticker, was the 2nd kicker the user
	 * caught). Leave the dog UNKICKED past initcalls so a hang warm-resets to the
	 * TWRP harbor (markers survive, no battery pull). [[m6graft_timer_FIXED]],
	 * [[feedback_never_disarm_wdt]]. (void) the fn/struct to dodge -Werror. */
	/* m681 v184: RE-ENABLE the hrtimer WDT kicker. The display controller now
	 * boots (fb0 up, v183) but surfaceflinger cascades (no EGL/Mali yet) so the
	 * boot never completes -> userspace watchdog never kicks -> the armed HW-WDT
	 * (MODE=0x15) guillotines the boot at ~16s. User goal: "start must hold
	 * long" so the kernel stays alive long enough to (a) be stable and (b) be
	 * inspected LIVE (read /dev/mali, dmesg) to bring up the Mali GPU. Pets the
	 * dog every 5s from the timer-IRQ path. Tradeoff (accepted): a SOFT hang no
	 * longer warm-resets to TWRP; a genuine bus wedge still does (CPU stalled ->
	 * hrtimer stops). [[feedback_never_disarm_wdt]] [[m6graft_timer_FIXED]] */
	if (forge_virt)
		return 0;	/* Rung C: no MTK toprgu on virt — nothing to pet */
	hrtimer_init(&forge_wdt_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	forge_wdt_hrtimer.function = forge_wdt_hrtimer_fn;
	hrtimer_start(&forge_wdt_hrtimer, ns_to_ktime(FORGE_WDT_KICK_NS),
		      HRTIMER_MODE_REL);
	pr_emerg("[FORGE_M681] v184 wdt hrtimer kicker RE-ENABLED (5s pet -> start holds long for Mali bring-up)\n");
	return 0;
}
core_initcall(forge_wdt_hrtimer_init);

/*
 * m681 v169: SPM power-domain state dump (hardware state inspection, reliable channel).
 *
 * Both recovery-side forensic channels are dead on this setup (observed during earlier testing):
 *   - DRAM marker 0x444f0000 is CLOBBERED by TWRP (reads back ELF magic
 *     7f 45 4c 46 after a wedge+recovery dwell), so post-mortem marker reads
 *     from recovery are garbage.
 *   - /dev/mem from TWRP cannot read SPM IO (STRICT_DEVMEM): dd of 0x10006180
 *     returns empty.
 * So the ONLY trustworthy channel is forge_klog on p4, which needs the build
 * to BOOT to late_initcall. This dump runs as a late_initcall in a *booting*
 * (display-denied) build and prints the DIS MTCMOS power state, settling the
 * refuted "DIS power-domain never comes up" theory as observed: mt_scpsys_init
 * (CLK_OF_DECLARE "mediatek,mt6755-scpsys") UNCONDITIONALLY powers DIS on at
 * of_clk_init (clk-mt6755-pg.c:2110), long before any deny gate, so a build
 * that boots at all has already powered DIS. This confirms the actual state.
 */
static int __init forge_spm_dump(void)
{
	void __iomem *spm;
	u32 cfg, sta, sta2, dis, mfg, isp, mm;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	if (forge_virt)
		return 0;	/* Rung C: 0x10006000 = virt PCIe MMIO window */
	spm = ioremap(0x10006000UL, 0x1000);

	if (!spm) {
		pr_emerg("[FORGE_M681] v169 spm_dump: ioremap(0x10006000) FAILED\n");
		return 0;
	}
	cfg  = readl(spm + 0x000);	/* POWERON_CONFIG_EN */
	sta  = readl(spm + 0x180);	/* PWR_STATUS      */
	sta2 = readl(spm + 0x184);	/* PWR_STATUS_2ND  */
	dis  = readl(spm + 0x30c);	/* DIS_PWR_CON     */
	mm   = readl(spm + 0x308);	/* (MM_PWR_CON neighbor, informational) */
	mfg  = readl(spm + 0x214);	/* MFG_PWR_CON (informational) */
	isp  = readl(spm + 0x238);	/* ISP_PWR_CON (informational) */
	pr_emerg("[FORGE_M681] v169 SPM cfg=%08x PWR_STATUS=%08x/2ND=%08x\n",
		 cfg, sta, sta2);
	pr_emerg("[FORGE_M681] v169 DIS_PWR_CON=%08x dis_on(sta b3)=%d sram_ack(b12)=%d\n",
		 dis, !!((sta & (1u<<3)) && (sta2 & (1u<<3))),
		 !!(dis & (1u<<12)));
	pr_emerg("[FORGE_M681] v169 neigh MM(0x308)=%08x MFG(0x214)=%08x ISP(0x238)=%08x\n",
		 mm, mfg, isp);
	iounmap(spm);
	return 0;
}
late_initcall(forge_spm_dump);

/*
 * ===== m681 #117: panic black-box (printk-ring tail into reserved DRAM) =====
 *
 * #113..#116 established: the ~56s death is silent on EVERY console and ends
 * as an RGU/PMIC-mediated reset.  The v48 panic net (below) SWRSTs from the
 * FIRST panic notifier (INT_MAX — higher priority runs FIRST; the original
 * "run last" comment was wrong), and panic() only calls kmsg_dump() AFTER
 * the notifier chain — so a panic whose text is stuck behind a held console
 * lock (the DISP VSync chorus holds it for long stretches) leaves its
 * backtrace ONLY in __log_buf, which dies with the reset.  Black-box: BEFORE
 * the SWRST, pull the last ~32KB of the printk ring via the kmsg_dump cursor
 * API (dumper.active set manually; the locked variant — if the panic
 * happened inside the logbuf_lock holder this can deadlock, in which case
 * the armed dog still resets us and the 0xF1 marker still says "panic") and
 * copy it into preloader-reserved DRAM at 0x44810000 (+64KB from the marker
 * page; no other forge user).  memremap(MEMREMAP_WB), NOT ioremap: a second
 * Device-attr alias of this cacheable linear-mapped region is the v96 0xFE
 * die; WB matches the linear map, and __flush_dcache_area pushes the bytes
 * to DRAM so they survive the warm reset.
 * TWRP readback:
 *   dd if=/dev/mem of=/tmp/panic.bin bs=4096 skip=$((0x44810000/4096)) count=8
 *   header 'F681PANC' + u32 len + u32 ms; text starts at +16.
 */
#include <linux/kmsg_dump.h>
#include <linux/io.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <asm/cacheflush.h>

#define FORGE_PANIC_DUMP_PHYS	0x44810000UL
#define FORGE_PANIC_DUMP_SIZE	0x8000UL
/* m681 #118: last 256B of the box = orderly-reboot record ('F681REBT'):
 * u32 magic x2, u32 ms, u32 action, comm[16] at +16, cmd[64] at +32.
 * The #117 panic dump is capped at FORGE_REBT_OFF so they cannot collide. */
#define FORGE_REBT_OFF		0x7F00UL
static void *forge_panic_dump_base;	/* WB mapping -> plain memcpy */

static void forge_panic_blackbox_dump(void)
{
	struct kmsg_dumper d = { .active = true };
	size_t len = 0;
	char *dst = forge_panic_dump_base;
	u32 *hdr = (u32 *)dst;

	if (!dst)
		return;
	kmsg_dump_rewind(&d);
	kmsg_dump_get_buffer(&d, false, dst + 16,
			     FORGE_REBT_OFF - 16, &len);
	hdr[0] = 0x31383646U;	/* 'F681' (LE) */
	hdr[1] = 0x434E4150U;	/* 'PANC' (LE) */
	hdr[2] = (u32)len;
	hdr[3] = (u32)(ktime_to_ns(ktime_get()) / 1000000);
	__flush_dcache_area(dst, len + 16);
}

/*
 * m681 #118: orderly-reboot black-box.  #117 device verdict: the ~56s death
 * is NOT a panic (armed box stayed all-zero), NOT a dog expiry (kicks
 * current at RT=40.9s, USB gadget vanished ~14s before the earliest bite),
 * and the device lands in TWRP autonomously (3x observed) — leading
 * hypothesis: a USERSPACE-COMMANDED `reboot,recovery` (Android 8.1 init
 * critical-service escalation / RescueParty), whose `reboot: Restarting
 * system with command 'recovery'` print dies unflushed behind the held
 * console lock exactly like a panic text would.  kernel_restart_prepare()
 * passes the cmd string through the reboot notifier chain
 * (kernel/reboot.c) IN THE CALLER'S CONTEXT, so one record gives:
 * who (comm), what (cmd: NULL / "recovery" / "bootloader"), when (ms).
 * emergency_restart() bypasses the chain — kernel/reboot.c calls
 * forge_m681_emergency_mark() on that path (action=0xEE).
 */
static void forge_rebt_record(unsigned long action, const char *cmd)
{
	char *dst = forge_panic_dump_base;
	u32 *h;

	if (!dst)
		return;
	h = (u32 *)(dst + FORGE_REBT_OFF);
	h[0] = 0x31383646U;	/* 'F681' (LE) */
	h[1] = 0x54424552U;	/* 'REBT' (LE) */
	h[2] = (u32)(ktime_to_ns(ktime_get()) / 1000000);
	h[3] = (u32)action;
	memset(dst + FORGE_REBT_OFF + 16, 0, 80);
	strncpy(dst + FORGE_REBT_OFF + 16, current->comm, 15);
	if (cmd)
		strncpy(dst + FORGE_REBT_OFF + 32, cmd, 63);
	__flush_dcache_area(dst + FORGE_REBT_OFF, 96);
	pr_emerg("[FORGE_M681] #118 reboot black-box: action=0x%lx comm=%s cmd='%s' ms=%u\n",
		 action, current->comm, cmd ? cmd : "(null)", h[2]);
}

static int forge_reboot_notifier_fn(struct notifier_block *nb,
				    unsigned long action, void *data)
{
	forge_rebt_record(action, (const char *)data);
	return NOTIFY_DONE;
}

static struct notifier_block forge_reboot_nb = {
	.notifier_call = forge_reboot_notifier_fn,
	.priority = INT_MAX,	/* record before any other shutdown work */
};

/* Called from kernel/reboot.c emergency_restart() — that path skips the
 * reboot notifier chain entirely. */
void forge_m681_emergency_mark(void)
{
	forge_rebt_record(0xEE, "emergency_restart");
}

static int __init forge_panic_blackbox_init(void)
{
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	if (forge_virt)
		return 0;	/* reserved region is MTK-hw only */
	forge_panic_dump_base = memremap(FORGE_PANIC_DUMP_PHYS,
					 FORGE_PANIC_DUMP_SIZE, MEMREMAP_WB);
	register_reboot_notifier(&forge_reboot_nb);
	pr_emerg("[FORGE_M681] #117 panic black-box %s @0x%lx (32KB, WB) + #118 reboot record @+0x%lx\n",
		 forge_panic_dump_base ? "armed" : "MAP FAILED",
		 FORGE_PANIC_DUMP_PHYS, FORGE_REBT_OFF);
	return 0;
}
late_initcall(forge_panic_blackbox_init);

/*
 * m681 v48: panic-notifier direct-SWRST recovery safety net.
 *
 * Earlier reset tests showed: the toprgu HW timer-WDT expired-time
 * SWRST does NOT happen reliably on mt6755 graft — preloader bin-string
 * `"WDT does not trigger reboot"` and the v45/v46/v47 self-arm / SWRST-
 * backstop changes never produced a warm return.  The ONLY recovery path
 * that has demonstrably worked on m681 graft so far is the **direct
 * SWRST_KEY write** that v43's mtu3d BUG_ON(1) -> panic -> wdt_arch_reset()
 * exercised.  v48 (a) restores that exact SWRST-write path by registering
 * on the kernel panic_notifier chain, so ANY panic (BUG_ON, OOPS, MCE, OOM)
 * fires it instead of relying on machine_restart plumbing; v48 (b) keeps
 * the v47 self-arm + manual SWRST backstop as belt-and-suspenders.
 *
 * The notifier is atomic-context safe: it issues only a single mmio writel
 * to toprgu+0x14 with the MTK_WDT_SWRST_KEY (0x1209) — same constant the
 * driver's wdt_arch_reset() uses — preceded by a forge_m681_mark(0xF1)
 * so the next-recovery marker decodes show "0xF1: paniked-and-forced-reset".
 * A one-shot latch keeps the FIRST panic from being shadowed by a later
 * call from atomic_notifier_call_chain iterating down several registered
 * notifiers (us + others).  pr_emerg is best-effort; printk in panic is
 * safe (logbuf lock held).
 *
 * Rollback: comment out the atomic_notifier_chain_register line in the
 * late_init function below.  No behaviour change on a progressing boot
 * because panic "doesn't happen" on a healthy boot — pointless to disable.
 */
static int forge_panic_latched;

static int forge_m681_panic_handler(struct notifier_block *this,
				    unsigned long ev, void *ptr)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;

	if (forge_panic_latched)
		return NOTIFY_DONE;
	forge_panic_latched = 1;

	forge_m681_mark(0xF1);
	pr_emerg("[FORGE_M681] panic rv48: writing SWRST_KEY to toprgu+0x14 to force warm reset\n");

	/* m681 #117: black-box the printk ring tail into reserved DRAM before
	 * the reset — the ONLY copy of a console-lock-silenced panic text.
	 * 0xF1 is already latched above, so even a wedge inside the dump
	 * still classifies the death as a panic. */
	forge_panic_blackbox_dump();

	if (b)
		writel(FORGE_WDT_SWRST_KEY, b + FORGE_WDT_SWRST_OFF);

	/* If the SWRST_KEY write worked we never return; if it didn't (toprgu
	 * unmapped, write blocked), at least the marker stuck and the next
	 * reset attempt (manual SWRST backstop in forge_m681_wdt_kick, or
	 * physical battery pull) carries the 0xF1 evidence. */
	return NOTIFY_DONE;
}

static struct notifier_block forge_m681_panic_nb = {
	.notifier_call = forge_m681_panic_handler,
	.priority = INT_MAX,	/* run last so other panic handlers log first */
};

/*
 * m681 v34: dedicated, non-overwritable "current initcall fn" tracker.
 *
 * The rolling slots only ever hold the LAST mark, and the inner platform-probe
 * marks (0xC0 attempt / 0xCD denylist-skip) clobber the per-initcall 0xE0 aux.
 * So after a hang inside an initcall that itself probes platform drivers, the
 * marker names the last PROBE, not the wedged INITCALL.  These fixed diag
 * offsets are written ONLY here, so they always name the initcall we entered
 * last (and whether it returned).  fn is mirrored to four offsets because the
 * preloader survivor region (0x44800000) bit-rots single copies; the reader
 * OR-reconstructs the true fn.  seq is the do_one_initcall ordinal.
 *
 * diag layout:  0x64/0x70/0x74/0x78 = current fn (OR these)   0x68 = seq
 *               0x6c = last fn that RETURNED (entered != returned => wedged)
 */
static u32 forge_initcall_seq;

static void forge_init_write(void __iomem *b, u32 fn)
{
	writel(fn, b + 0x64);
	writel(fn, b + 0x70);
	writel(fn, b + 0x74);
	writel(fn, b + 0x78);
	writel(forge_initcall_seq, b + 0x68);
}

void forge_m681_set_initcall(u32 fn)
{
	forge_initcall_seq++;
	if (forge_spm_base)
		forge_init_write(forge_spm_base, fn);
	if (forge_spm_base2)
		forge_init_write(forge_spm_base2, fn);
}
EXPORT_SYMBOL(forge_m681_set_initcall);

u32 forge_m681_get_initcall_seq(void)
{
	return forge_initcall_seq;
}
EXPORT_SYMBOL(forge_m681_get_initcall_seq);

void forge_m681_set_initcall_done(u32 fn)
{
	/* m681 v50: mirror last-good seq (0xD0) + guard (0xD4) so a reader
	 * gets the seq of the last initcall that returned without needing
	 * to cross-reference 0x68 (current seq) vs 0x6c (last done fn). */
	u32 seq = forge_initcall_seq;
	if (forge_spm_base) {
		writel(fn, forge_spm_base + 0x6c);
		writel(seq, forge_spm_base + FORGE_DIAG_LASTGOOD_SEQ_OFF);
		writel(~seq, forge_spm_base + FORGE_DIAG_LASTGOOD_SEQ_INV_OFF);
	}
	if (forge_spm_base2) {
		writel(fn, forge_spm_base2 + 0x6c);
		writel(seq, forge_spm_base2 + FORGE_DIAG_LASTGOOD_SEQ_OFF);
		writel(~seq, forge_spm_base2 + FORGE_DIAG_LASTGOOD_SEQ_INV_OFF);
	}
}
EXPORT_SYMBOL(forge_m681_set_initcall_done);

/*
 * m681 v35: of_platform_populate node tracker.  arm64_device_init wedges inside
 * of_platform_populate AFTER mtk_wdt's probe was denied, with NO subsequent
 * 0xC0 (so the hang is in of-core device CREATION, not a driver probe).  This
 * marks every node reaching of_platform_device_create_pdata so the post-reset
 * field names the exact wedged node.
 *
 * diag layout:  0x80/0x84/0x88/0x8c = DFS node counter (OR the 4 copies)
 *               0x90..0x9c = 16 ASCII bytes of full_name (copy A)
 *               0xa0..0xac = same 16 ASCII bytes (copy B; reader ORs A|B)
 */
static u32 forge_ofnode_seq;

static void forge_ofnode_write(void __iomem *b, const u32 *w)
{
	writel(forge_ofnode_seq, b + 0x80);
	writel(forge_ofnode_seq, b + 0x84);
	writel(forge_ofnode_seq, b + 0x88);
	writel(forge_ofnode_seq, b + 0x8c);
	writel(w[0], b + 0x90); writel(w[1], b + 0x94);
	writel(w[2], b + 0x98); writel(w[3], b + 0x9c);
	writel(w[0], b + 0xa0); writel(w[1], b + 0xa4);
	writel(w[2], b + 0xa8); writel(w[3], b + 0xac);
}

void forge_m681_mark_ofnode(const char *name)
{
	const char *s = name ? name : "";
	u8 buf[16];
	u32 w[4];
	int i;

	forge_ofnode_seq++;
	for (i = 0; i < 16; i++) {
		buf[i] = *s ? (u8)*s : 0;
		if (*s)
			s++;
	}
	for (i = 0; i < 4; i++)
		w[i] = buf[i*4] | (buf[i*4+1] << 8) |
		       (buf[i*4+2] << 16) | (buf[i*4+3] << 24);

	if (forge_spm_base)
		forge_ofnode_write(forge_spm_base, w);
	if (forge_spm_base2)
		forge_ofnode_write(forge_spm_base2, w);
}
EXPORT_SYMBOL(forge_m681_mark_ofnode);

/*
 * m681 v50: initcall-level phase markers + per-level completion counters.
 *
 * do_initcall_level() calls mark_level_enter(level) before the level's
 * initcall loop and mark_level_done(level) after it.  The rolling-stage
 * channel records 0xE8/0xE9 (aux=level) so a post-reset marker decode
 * immediately names the LEVEL the boot wall sits in — early/core/postcore/
 * arch/subsys/fs/device/late — complementing the per-initcall fn tracker
 * (0x64/0x6c) that names the exact initcall.
 *
 * Per-level initcall completion counts are kept in diag offsets
 *   0xB0+level*4 (enter count, incremented on each enter)
 *   0xC0+level*4 (done count, incremented on each done)
 * so a reader can tell whether the wall is at the START of a level
 * (enter>0, done=0) or MIDDLE (done>0, enter>done+1).
 *
 * Additionally, the "last-good seq" (seq of the last initcall that
 * returned) is mirrored to diag 0xD0 so the reader doesn't need to
 * cross-reference 0x68 (current seq) with 0x6c (last done fn) to
 * compute it.
 *
 * diag layout (v50 additions):
 *   0xB0..0xB7 = per-level enter count (u8 each, 8 levels packed)
 *   0xC0..0xC7 = per-level done count  (u8 each, 8 levels packed)
 *   0xD0       = last-good initcall seq (u32, written in set_initcall_done)
 *   0xD4       = last-good initcall seq guard (~seq)
 *   0xD8       = boot-phase owner: current initcall level (u32)
 */
static u8 forge_level_enter_count[8];
static u8 forge_level_done_count[8];

static void forge_level_write_counts(void __iomem *b)
{
	int i;
	for (i = 0; i < 8; i++) {
		writel(forge_level_enter_count[i],
		       b + FORGE_DIAG_LEVEL_ENTER_BASE + i * 4);
		writel(forge_level_done_count[i],
		       b + FORGE_DIAG_LEVEL_DONE_BASE + i * 4);
	}
}

void forge_m681_mark_level_enter(int level)
{
	if (level < 0 || level >= 8)
		return;
	forge_level_enter_count[level]++;
	forge_m681_mark_aux(FORGE_STAGE_INITCALL_LEVEL_ENTER, (u32)level);
	if (forge_spm_base) {
		writel((u32)level, forge_spm_base + FORGE_DIAG_BOOTPHASE_OFF);
		forge_level_write_counts(forge_spm_base);
	}
	if (forge_spm_base2) {
		writel((u32)level, forge_spm_base2 + FORGE_DIAG_BOOTPHASE_OFF);
		forge_level_write_counts(forge_spm_base2);
	}
}
EXPORT_SYMBOL(forge_m681_mark_level_enter);

void forge_m681_mark_level_done(int level)
{
	if (level < 0 || level >= 8)
		return;
	forge_level_done_count[level]++;
	forge_m681_mark_aux(FORGE_STAGE_INITCALL_LEVEL_DONE, (u32)level);
	if (forge_spm_base)
		forge_level_write_counts(forge_spm_base);
	if (forge_spm_base2)
		forge_level_write_counts(forge_spm_base2);
}
EXPORT_SYMBOL(forge_m681_mark_level_done);

/* m681 v51: direct SWRST_KEY write for belt-and-suspenders recovery.
 * Uses the same toprgu_base/forge_wdt_base fallback as the kick path. */
void forge_m681_wdt_swrst(void)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;
	if (b)
		writel(FORGE_WDT_SWRST_KEY, b + FORGE_WDT_SWRST_OFF);
}
EXPORT_SYMBOL(forge_m681_wdt_swrst);

/*
 * Called from arch/arm64/kernel/setup.c right after early_ioremap_init().
 * Maps the marker region via the fixmap so we can mark stages from A04 onward,
 * well before the SPM/ram_console drivers register.
 */
void __init forge_m681_marker_early_init(void)
{
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return;

	if (forge_spm_base || forge_spm_base2)
		return;	/* already mapped */

	forge_spm_base = early_ioremap(FORGE_MARKER_PHYS_BASE,
				       FORGE_MARKER_REGION_SIZE);
	if (!forge_spm_base)
		pr_emerg("[FORGE_M681] marker early_ioremap(0x%lx) FAILED\n",
			 FORGE_MARKER_PHYS_BASE);

	forge_spm_base2 = early_ioremap(FORGE_MARKER2_PHYS_BASE,
					FORGE_MARKER_REGION_SIZE);
	if (!forge_spm_base2)
		pr_emerg("[FORGE_M681] marker2 early_ioremap(0x%lx) FAILED\n",
			 FORGE_MARKER2_PHYS_BASE);

	pr_emerg("[FORGE_M681] marker early base=%p base2=%p\n",
		 forge_spm_base, forge_spm_base2);


	/* sentinel so we know the marker engine itself is alive */
	forge_m681_mark(FORGE_STAGE_MARKER_EARLY_INIT);
}

/*
 * m681 v121: NATIVE timer fix — enable the MTK cpuxgpt (the ARM architected
 * system counter) via the secure SMC, exactly as stock setup_syscnt() would if
 * the calls weren't commented out (mt_gpt.c:497-499). The donor-graft kernel
 * never enables cpuxgpt, so CNTVCT_EL0 is frozen. The cpuxgpt CTL is in
 * write-protected MCUSYS (0x10200000): direct EL1 MMIO writes are rejected (v91),
 * but writes routed through SMC MTK_SIP_KERNEL_MCUSYS_WRITE (0x82000201) are
 * serviced by EL3/ATF (the device runs the m681's OWN MT6755 ATF, which stock
 * Flyme uses with a working CNTVCT). Sequence (= __cpuxgpt_set_clk(CLK_DIV2) +
 * __cpuxgpt_enable() from mtk_cpuxgpt_mt6755.c): write INDEX_CTL(0) then CTL with
 * clk=13MHz/DIV2 and EN bit0. Reads of CTL (0x10200670) are plain MMIO (only
 * writes are protected). Verifies by reading CNTVCT before/after — a nonzero
 * delta proves the system counter now free-runs. Result -> diag 0x90 (sentinel
 * 0xCC | CNTVCT-delta-nonzero<<23 | low bits) and 0xA4 (final CTL readback). */
static noinline int forge_smc(u64 fid, u64 a0, u64 a1, u64 a2)
{
	register u64 r0 __asm__("x0") = fid;
	register u64 r1 __asm__("x1") = a0;
	register u64 r2 __asm__("x2") = a1;
	register u64 r3 __asm__("x3") = a2;

	asm volatile ("smc    #0\n" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3));
	return (int)r0;
}

static inline u64 forge_read_cntvct(void)
{
	u64 v;

	asm volatile ("mrs %0, cntvct_el0" : "=r"(v));
	return v;
}

#define FORGE_SMC_MCUSYS_WRITE	0x82000201ULL
#define FORGE_CPUXGPT_INDEX_PHY	0x10200674ULL	/* MCUSYS + 0x674 */
#define FORGE_CPUXGPT_CTL_PHY	0x10200670ULL	/* MCUSYS + 0x670 */
#define FORGE_CPUXGPT_IDX_CTL	0x000U		/* INDEX_CTL_REG */
#define FORGE_CPUXGPT_EN	0x01U		/* EN_CPUXGPT */
#define FORGE_CPUXGPT_DIV2	(0x2U << 8)	/* CLK_DIV2 = 13MHz/2 */
#define FORGE_CPUXGPT_DIV_MASK	(~(0x7U << 8))	/* CLK_DIV_MASK */

static void forge_cpuxgpt_wr(u32 addr, u32 val)
{
	forge_smc(FORGE_SMC_MCUSYS_WRITE, addr, val, 0);
}

void __init forge_enable_cpuxgpt(void)
{
	void __iomem *mcusys = ioremap(0x10200000UL, 0x1000);
	u64 c1, c2;
	u32 ctl0 = 0, ctlf = 0, tmp;
	volatile int spin;

	c1 = forge_read_cntvct();

	/* set cpuxgpt clk = 13MHz / DIV2 (read-modify-write the CTL) */
	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	if (mcusys)
		ctl0 = readl(mcusys + 0x670);
	tmp = (ctl0 & FORGE_CPUXGPT_DIV_MASK) | FORGE_CPUXGPT_DIV2;
	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	forge_cpuxgpt_wr(FORGE_CPUXGPT_CTL_PHY, tmp);

	/* set EN_CPUXGPT -> system counter free-runs -> CNTVCT ticks */
	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	if (mcusys)
		tmp = readl(mcusys + 0x670);
	tmp |= FORGE_CPUXGPT_EN;
	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	forge_cpuxgpt_wr(FORGE_CPUXGPT_CTL_PHY, tmp);

	for (spin = 0; spin < 3000000; spin++)
		cpu_relax();
	c2 = forge_read_cntvct();

	forge_cpuxgpt_wr(FORGE_CPUXGPT_INDEX_PHY, FORGE_CPUXGPT_IDX_CTL);
	if (mcusys) {
		ctlf = readl(mcusys + 0x670);
		iounmap(mcusys);
	}

	/* 0x90 = sentinel 0xCC | (CNTVCT moved)<<23 | low22 of the CNTVCT delta.
	 * 0xA4 = (initial CTL << 16) | final CTL & 0xFFFF (bit0 should be 1 = EN). */
	if (forge_spm_base2) {
		writel(0xCC000000u | (((c2 - c1) != 0) ? (1u << 23) : 0u) |
		       ((u32)(c2 - c1) & 0x3FFFFFu), forge_spm_base2 + 0x90);
		writel(((ctl0 & 0xFFFFu) << 16) | (ctlf & 0xFFFFu),
		       forge_spm_base2 + 0xA4);
	}
	pr_emerg("[FORGE_M681] v121 cpuxgpt: CTL 0x%x->0x%x, CNTVCT %llu->%llu (d=%llu) %s\n",
		 ctl0, ctlf, c1, c2, c2 - c1,
		 (c2 != c1) ? "RUNNING!" : "still frozen");
}

/*
 * Called from init/main.c after mm_init() when vmalloc and the regular
 * ioremap() are available.  We swap the fixmap mapping for a permanent
 * ioremap() so the late_initcall leak check stays quiet and the mapping
 * survives the early_ioremap_reset() flag flip.
 */
/*
 * m681 4.9 G1.5/G1.6: boot deadline. G1.3a passed the initcalls, ran /init
 * and then sat in userspace with nothing armed to reset it: the phone stayed
 * dark until someone pressed buttons, and a long Power press decays the DRAM
 * logs. Unless a host configures the USB gadget (forge_usb_configured,
 * android.c / configfs.c) within forge_boot_deadline seconds of
 * late_initcall, send the phone to TWRP. 0 disables it; the parameter is
 * writable at runtime (/sys/module/kernel/parameters/).
 *
 * G1.6: how. G1.5a reset with BUG() (die -> exception reboot -> PSCI) at
 * 120 s and the LK booted the normal image again, 9+ times in a row. The
 * m681 LK reads the bootloader message from the "para" partition (device LK
 * strings "[mboot_recovery_load_misc]", "para", "boot-recovery"; ROM fstab
 * /dev/block/mmcblk0p2 /misc emmc; GPT p2 = para, 0x80000), and FACT
 *  "boot-recovery" there sends a plain reboot
 * to TWRP. The bootguard thread below arms that message as soon as para
 * exists (only after checking its GPT name); userspace clears it on
 * sys.boot_completed. USB configuration switches the deadline off; if the
 * host then loses it for forge_usb_lost_deadline seconds while the BCB is
 * still armed, the phone restarts into TWRP as well (A13s).
 */
extern int forge_usb_configured __attribute__((weak));
static int forge_boot_deadline = 120;
core_param(forge_boot_deadline, forge_boot_deadline, int, 0644);

#define FORGE_BCB_PARTNO	2
#define FORGE_BCB_PARTNAME	"para"
/* The eMMC; a parameter only so the path can be exercised on a qemu disk. */
static char *forge_bcb_disk = "mmcblk0";
core_param(forge_bcb_disk, forge_bcb_disk, charp, 0444);
static int forge_bcb_clear_on_usb;
core_param(forge_bcb_clear_on_usb, forge_bcb_clear_on_usb, int, 0644);
/* Seconds the host may stay without a USB configuration, once it had one,
 * before the bootguard restarts into TWRP; 0 disables it at runtime. */
static int forge_usb_lost_deadline = 60;
core_param(forge_usb_lost_deadline, forge_usb_lost_deadline, int, 0644);
extern void forge_usb_log_state(const char *why) __attribute__((weak));

/*
 * Where the bootloader message lives. m681 GPT (board geometry checked with
 * sgdisk + fastboot getvar): p2 "para" = LBA 32832, 1024 sectors (512 KiB),
 * whole eMMC 30535680 sectors.
 *
 *  1. p2 whose GPT name is "para" - the normal case (sector 0 of p2);
 *  2. p2 without a name but with exactly that geometry (sector 0 of p2);
 *  3. no p2 at all for 2 s on a disk of exactly that capacity: absolute
 *     sector 32832 of the whole disk - the same physical sector the LK reads.
 * 2 and 3 additionally require the first 64 bytes there to be empty or plain
 * ASCII (an old message), so they cannot scribble over foreign data.
 *
 * blk_lookup_devt() hands out the dev_t of a partition that does not exist
 * yet ("return the right devno, even if the partition doesn't exist yet"),
 * so a missing partition table shows up as disk_get_part() == NULL. (A13j
 * logged that as 'part 2 is ""' forty times: no partition at all, the GPT had
 * not been read through the HS200 CRC errors.)
 */
#define FORGE_BCB_START_LBA	32832ULL
#define FORGE_BCB_NR_SECT	1024ULL
#define FORGE_M681_EMMC_SECT	30535680ULL

struct forge_bcb_target {
	dev_t devt;
	sector_t sector;	/* 512-byte sector within devt */
	bool check_content;
	const char *how;
};

static unsigned long forge_bcb_nopart_since;

static int forge_bcb_locate(struct forge_bcb_target *t)
{
	static bool reported;
	struct gendisk *disk;
	struct hd_struct *part;
	const char *name = "";
	dev_t devt;
	int partno, ret = -ENOENT;

	devt = blk_lookup_devt(forge_bcb_disk, FORGE_BCB_PARTNO);
	if (!devt)
		return -ENODEV;
	disk = get_gendisk(devt, &partno);
	if (!disk)
		return -ENODEV;
	part = disk_get_part(disk, partno);
	if (part) {
		forge_bcb_nopart_since = 0;
		if (part->info)
			name = (const char *)part->info->volname;
		if (!strcmp(name, FORGE_BCB_PARTNAME)) {
			*t = (struct forge_bcb_target){ devt, 0, false, "GPT name" };
			ret = 0;
		} else if (!name[0] && part->start_sect == FORGE_BCB_START_LBA &&
			   part->nr_sects == FORGE_BCB_NR_SECT) {
			*t = (struct forge_bcb_target){ devt, 0, true, "p2 geometry" };
			ret = 0;
		} else {
			if (!reported)
				pr_emerg("[FORGE_M681] BCB: %s part %d is \"%s\" @%llu+%llu - not the m681 para, not writing\n",
					 forge_bcb_disk, FORGE_BCB_PARTNO, name,
					 (unsigned long long)part->start_sect,
					 (unsigned long long)part->nr_sects);
			reported = true;
			ret = -EINVAL;
		}
		disk_put_part(part);
	} else if (get_capacity(disk) == FORGE_M681_EMMC_SECT) {
		if (!forge_bcb_nopart_since)
			forge_bcb_nopart_since = jiffies ? jiffies : 1;
		else if (time_after(jiffies, forge_bcb_nopart_since + 2 * HZ)) {
			*t = (struct forge_bcb_target){ disk_devt(disk),
				FORGE_BCB_START_LBA, true, "whole disk" };
			ret = 0;
		}
	}
	put_disk(disk);
	return ret;
}

static bool forge_bcb_plausible(const u8 *b)
{
	int i;

	for (i = 0; i < 64; i++)
		if (b[i] && b[i] != '\n' && (b[i] < 0x20 || b[i] > 0x7e))
			return false;
	return true;
}

/*
 * Write the bootloader message (cmd == NULL clears it), or with armed != NULL
 * only read it back and report whether it still says boot-recovery.
 */
static int forge_bcb_rw(const char *cmd, const char *status, bool *armed)
{
	static const char *last_how;
	fmode_t mode = armed ? FMODE_READ : FMODE_READ | FMODE_WRITE;
	struct forge_bcb_target t;
	struct block_device *bdev;
	struct buffer_head *bh;
	unsigned int bs;
	char *msg;
	u64 pos;
	int err;

	err = forge_bcb_locate(&t);
	if (err)
		return err;
	if (t.how != last_how) {
		pr_emerg("[FORGE_M681] BCB target: %s (dev %u:%u sector %llu)\n",
			 t.how, MAJOR(t.devt), MINOR(t.devt),
			 (unsigned long long)t.sector);
		last_how = t.how;
	}
	bdev = blkdev_get_by_dev(t.devt, mode, NULL);
	if (IS_ERR(bdev))
		return PTR_ERR(bdev);
	/* Userspace may have cleared the message through another node
	 * (mmcblk0p2, by-name/para): drop our clean cached copy first. */
	if (armed)
		invalidate_bdev(bdev);
	/*
	 * __bread() must be called with the device's current block size
	 * (i_blkbits): __find_get_block_slow() indexes the page cache with it,
	 * grow_buffers() with the size passed in. A 512-byte read of block
	 * 32832 on a whole disk whose block size is 4096 spins forever
	 * (qemu, whole-disk fallback; block 0 of p2 only worked because both
	 * indices are 0). So read the containing block and edit 512 bytes.
	 */
	bs = block_size(bdev);
	pos = (u64)t.sector * 512;
	bh = __bread(bdev, div_u64(pos, bs), bs);
	if (!bh) {
		err = -EIO;
		goto out;
	}
	msg = bh->b_data + do_div(pos, bs);
	if (armed) {
		*armed = !strncmp(msg, "boot-recovery", 13);
		brelse(bh);
		err = 0;
		goto out;
	}
	if (t.check_content && !forge_bcb_plausible(msg)) {
		static bool foreign_reported;

		if (!foreign_reported)
			pr_emerg("[FORGE_M681] BCB: %s sector %llu holds foreign data - not writing\n",
				 t.how, (unsigned long long)t.sector);
		foreign_reported = true;
		brelse(bh);
		err = -EEXIST;
		goto out;
	}
	memset(msg, 0, 512);
	if (cmd) {
		strcpy(msg, cmd);			/* command[32] */
		strcpy(msg + 32, status);		/* status[32] */
		strcpy(msg + 64, "recovery\n");	/* recovery[768] */
	}
	mark_buffer_dirty(bh);
	err = sync_dirty_buffer(bh);
	brelse(bh);
	if (!err)
		err = blkdev_issue_flush(bdev, GFP_KERNEL, NULL);
out:
	blkdev_put(bdev, mode);
	return err;
}

static int forge_bcb_write(const char *cmd, const char *status)
{
	return forge_bcb_rw(cmd, status, NULL);
}

/*
 * Earlier USB recovery tests showed: adb came up, then `adb root` re-bound the
 * UDC, the gadget never came back and the phone sat alive and unreachable,
 * because the first configuration had switched the deadline off for good.
 * So keep watching while the BCB is still armed (userspace clears it on
 * sys.boot_completed): if the host loses the configuration and it does not
 * return within forge_usb_lost_deadline seconds, restart into TWRP. The warm
 * restart also keeps the DRAM logs that a long Power press decays.
 */
static void forge_bootguard_usb_watch(void)
{
	unsigned long lost_since = 0;
	bool armed = true;
	int err;

	while (!kthread_should_stop()) {
		msleep(1000);
		if (forge_usb_configured) {
			if (lost_since)
				pr_emerg("[FORGE_M681] bootguard: USB configured again after %u ms\n",
					 jiffies_to_msecs(jiffies - lost_since));
			lost_since = 0;
			continue;
		}
		if (!lost_since) {
			lost_since = jiffies;
			forge_m681_mark_aux(0xDE, 0);
			pr_emerg("[FORGE_M681] bootguard: USB configuration lost - TWRP in %d s unless it returns\n",
				 forge_usb_lost_deadline);
			if (forge_usb_log_state)
				forge_usb_log_state("configuration lost");
			continue;
		}
		if (forge_usb_lost_deadline <= 0 ||
		    time_before(jiffies, lost_since + forge_usb_lost_deadline * HZ))
			continue;
		err = forge_bcb_rw(NULL, NULL, &armed);
		if (!err && !armed) {
			pr_emerg("[FORGE_M681] bootguard: USB lost, but userspace cleared the BCB (boot completed) - watch off\n");
			return;
		}
		if (forge_usb_log_state)
			forge_usb_log_state("restart");
		forge_m681_mark_aux(0xDF, (u32)err);
		pr_emerg("[FORGE_M681] bootguard: USB lost for %d s, BCB %s (%d) - restarting\n",
			 forge_usb_lost_deadline, err ? "unreadable" : "armed", err);
		emergency_restart();
		BUG();	/* restart declined: the G1.2 exception-reboot path */
	}
}

/*
 * Boot guard (earlier Android 13 boots looped every
 * 20.4 s, dying before the 120 s deadline ever armed the BCB): arm the BCB
 * as soon as the eMMC's para partition exists, so ANY death after that -
 * panic, init's fatal reboot, WDT - lands in TWRP (observed:
 * this LK honours "boot-recovery" in p2 on a plain reboot; TWRP does not
 * clear it). Userspace clears the message on sys.boot_completed; a host
 * configuring the USB gadget only switches the deadline off; without it,
 * reset at forge_boot_deadline seconds.
 */
static int forge_bootguard_fn(void *unused)
{
	unsigned long deadline = jiffies + forge_boot_deadline * HZ;
	int armed = 0, waited_logged = 0, err = -ENODEV;

	while (!kthread_should_stop()) {
		if (!armed && time_before(jiffies, deadline)) {
			err = forge_bcb_write("boot-recovery", "forge-bootguard");
			if (!err) {
				armed = 1;
				forge_m681_mark_aux(0xDB, 0);
				pr_emerg("[FORGE_M681] bootguard: BCB boot-recovery armed in %s p%d\n",
					 forge_bcb_disk, FORGE_BCB_PARTNO);
			}
		}
		if (&forge_usb_configured && forge_usb_configured) {
			/* Success milestone for the deadline only: the BCB stays
			 * armed until userspace clears it on sys.boot_completed
			 * , so a later death before
			 * that - init's fatal reboot, a crash loop - still ends
			 * in TWRP. forge_bcb_clear_on_usb=1 restores the old
			 * clear-here behaviour for images without that rc. */
			err = (armed && forge_bcb_clear_on_usb) ?
				forge_bcb_write(NULL, NULL) : 0;
			forge_m681_mark_aux(0xDC, (u32)err);
			pr_emerg("[FORGE_M681] bootguard: USB configured - deadline off, BCB %s (%d)\n",
				 !armed ? "was never armed" :
				 forge_bcb_clear_on_usb ? "cleared" :
				 "left for userspace (boot_completed)", err);
			forge_bootguard_usb_watch();
			return 0;
		}
		if (forge_boot_deadline <= 0) {
			pr_emerg("[FORGE_M681] bootguard: disabled at runtime, BCB %s\n",
				 armed ? "left armed" : "not armed");
			return 0;
		}
		if (time_after_eq(jiffies, deadline)) {
			if (!armed)
				err = forge_bcb_write("boot-recovery", "forge-deadline");
			forge_m681_mark_aux(0xDD, (u32)(armed ? 0 : err));
			pr_emerg("[FORGE_M681] boot deadline %ds: no USB configuration (BCB %s, %d) - restarting\n",
				 forge_boot_deadline, armed ? "armed" : "late write", err);
			emergency_restart();
			BUG();	/* restart declined: the G1.2 exception-reboot path */
		}
		if (!armed && !waited_logged &&
		    time_after(jiffies, deadline - (forge_boot_deadline - 5) * HZ)) {
			pr_emerg("[FORGE_M681] bootguard: no %s p%d (\"%s\") after 5 s (%d), still waiting\n",
				 forge_bcb_disk, FORGE_BCB_PARTNO, FORGE_BCB_PARTNAME, err);
			waited_logged = 1;
		}
		msleep(armed ? 1000 : 100);
	}
	return 0;
}

/*
 * Restart with an explicit target, in process context (reboot_notifier_list
 * runs in kernel_restart_prepare(), before device_shutdown()). "recovery"
 * and "bootloader" re-arm the BCB: on this kernel arch_reset() hands those
 * to rtc_mark_recovery()/rtc_mark_fast(), and the RTC driver is not built,
 * so without this `reboot recovery` and init's fatal reboot (target
 * "bootloader" on A13) both come back to the normal image - the 20.4 s loop
 * of a13d. This LK has no usable fastboot (board bootloader: "fastboot boot" =
 * unknown command), so TWRP is the useful landing for "bootloader" too.
 */
static int forge_bcb_reboot_notify(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	const char *cmd = data;
	int err;

	if (action != SYS_RESTART || !cmd ||
	    (strcmp(cmd, "recovery") && strcmp(cmd, "bootloader")))
		return NOTIFY_DONE;
	err = forge_bcb_write("boot-recovery", "forge-reboot");
	pr_emerg("[FORGE_M681] reboot %s: BCB boot-recovery %d\n", cmd, err);
	return NOTIFY_DONE;
}

static struct notifier_block forge_bcb_reboot_nb = {
	.notifier_call = forge_bcb_reboot_notify,
};

static int __init forge_bootguard_init(void)
{
	struct task_struct *t;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	if (!forge_virt)
		register_reboot_notifier(&forge_bcb_reboot_nb);

	if (forge_virt || forge_boot_deadline <= 0)
		return 0;
	t = kthread_run(forge_bootguard_fn, NULL, "forge_bootguard");
	pr_emerg("[FORGE_M681] bootguard started (%s), deadline %ds\n",
		 IS_ERR(t) ? "FAILED" : "ok", forge_boot_deadline);
	return 0;
}
late_initcall(forge_bootguard_init);

/*
 * m681 4.9 G1.3: head log. The 3.5 KB FLOG ring keeps only the tail: the G1.2
 * capture lost the WARN that named the /dev/console driver, the command line
 * and the SMP bring-up. This console copies printk from the first line
 * (CON_PRINTBUFFER replays the log buffer to it alone at registration) into
 * 252 KB of reserved, linear-mapped RAM and stops when full. Each write is
 * cleaned to DRAM so a warm reset keeps it. The window sits next to the m5c
 * scratch page A, which survived LK + TWRP in the G1.2 capture.
 * Layout: 0x7f000C00 u32 'HLOG', u32 length; text from 0x7f001000.
 * Read from TWRP: dd if=/dev/mem bs=4096 skip=$((0x7f000000/4096)) count=64
 */
#define FORGE_HLOG_HDR_PHYS	0x7f000c00UL
#define FORGE_HLOG_PHYS		0x7f001000UL
#define FORGE_HLOG_SIZE		0x3f000UL
#define FORGE_HLOG_MAGIC	0x474f4c48U	/* 'HLOG' */

static u32 *forge_hlog_hdr;
static char *forge_hlog;
static u32 forge_hlog_len;

static void forge_head_write(struct console *con, const char *s,
			     unsigned int count)
{
	u32 room;

	if (!forge_hlog)
		return;
	room = FORGE_HLOG_SIZE - forge_hlog_len;
	if (count > room)
		count = room;
	if (!count)
		return;
	memcpy(forge_hlog + forge_hlog_len, s, count);
	__flush_dcache_area(forge_hlog + forge_hlog_len, count);
	forge_hlog_len += count;
	forge_hlog_hdr[1] = forge_hlog_len;
	__flush_dcache_area(forge_hlog_hdr, 8);
}

static struct console forge_head_console = {
	.name	= "forgehead",
	.write	= forge_head_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED | CON_ANYTIME,
	.index	= -1,
};

/* setup_arch, after arm64_memblock_init(): keep the window from the allocator. */
void __init forge_m681_headlog_reserve(void)
{
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return;

	memblock_reserve(FORGE_HLOG_PHYS, FORGE_HLOG_SIZE);
}

/* setup_arch, after paging_init(): the window is reachable through the
 * linear map only if the LK handed it over as plain RAM. */
void __init forge_m681_headlog_init(void)
{
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return;

	if (forge_virt ||
	    !memblock_is_map_memory(FORGE_HLOG_HDR_PHYS) ||
	    !memblock_is_map_memory(FORGE_HLOG_PHYS) ||
	    !memblock_is_map_memory(FORGE_HLOG_PHYS + FORGE_HLOG_SIZE - 1))
		return;
	forge_hlog_hdr = phys_to_virt(FORGE_HLOG_HDR_PHYS);
	forge_hlog = phys_to_virt(FORGE_HLOG_PHYS);
	forge_hlog_hdr[0] = FORGE_HLOG_MAGIC;
	forge_hlog_hdr[1] = 0;
	__flush_dcache_area(forge_hlog_hdr, 8);
	register_console(&forge_head_console);
}

/*
 * m681 4.9 G1.2: bring the two TWRP-readable death channels up at the start
 * of setup_arch instead of after mm_init. G1 died inside setup_arch, before
 * either existed, so its only trace was one stage byte. Called right after
 * forge_m681_virt_detect() (the qemu-virt guard is settled by then):
 *  - registers the forge console on the early_ioremap'd marker page, so every
 *    printk from here on (incl. an Oops) lands in the 0x44800200 ring;
 *  - early_ioremaps toprgu into forge_wdt_base and registers the panic->SWRST
 *    notifier, so an early Oops warm-resets with the 0xF1 mark instead of
 *    hanging dark (the G1 27.08 end state).
 * forge_m681_marker_late_init() swaps in its permanent mappings and skips
 * both registrations when they already happened here.
 */
static bool forge_console_on;
static bool forge_panic_nb_on;

void __init forge_m681_marker_early_console(void)
{
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return;

	if (forge_virt)
		return;
	if (forge_spm_base2 && !forge_console_on) {
		register_console(&forge_console);
		forge_console_on = true;
	}
	if (!forge_wdt_base)
		forge_wdt_base = early_ioremap(FORGE_WDT_PHYS_BASE,
					       FORGE_WDT_REGION_SIZE);
	if (forge_wdt_base && !forge_panic_nb_on) {
		atomic_notifier_chain_register(&panic_notifier_list,
					       &forge_m681_panic_nb);
		forge_panic_nb_on = true;
	}
	pr_emerg("[FORGE_M681] G1.2 early channels: console=%d panic-swrst=%d\n",
		 forge_console_on, forge_panic_nb_on);
}

void __init forge_m681_marker_late_init(void)
{
	void __iomem *nb;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return;

	/* Rung C: under qemu -M virt skip EVERYTHING here — marker ioremaps
	 * (unreserved guest RAM), forge console, wdt/gpt ioremaps (PCIe MMIO
	 * window), the v91 direct cpuxgpt write and the forge_enable_cpuxgpt()
	 * SMC (no EL3 -> UNDEF).  See forge_m681_virt_detect(). */
	if (forge_virt) {
		pr_info("[FORGE_M681] forge_virt: late_init skipped (no MTK hw)\n");
		return;
	}

	nb = ioremap(FORGE_MARKER_PHYS_BASE, FORGE_MARKER_REGION_SIZE);
	if (nb)
		forge_spm_base = nb;
	else
		pr_emerg("[FORGE_M681] marker late ioremap(0x%lx) FAILED, keeping early\n",
			 FORGE_MARKER_PHYS_BASE);

	nb = ioremap(FORGE_MARKER2_PHYS_BASE, FORGE_MARKER_REGION_SIZE);	/* m681 v97: back to one page (v96 32KB ioremap aliased linear map -> early die) */
	if (nb)
		forge_spm_base2 = nb;
	else
		pr_emerg("[FORGE_M681] marker2 late ioremap(0x%lx) FAILED, keeping early\n",
			 FORGE_MARKER2_PHYS_BASE);

	/* m681 v121: NATIVE timer fix — enable cpuxgpt via the secure SMC NOW, before
	 * time_init()/arch_counter_register() reads CNTVCT as the clocksource. Done
	 * after forge_spm_base2 is mapped so the verification diag (0x90/0xA4) lands. */
	forge_enable_cpuxgpt();

	/* m681 v79: register the forge console.  It logs into the marker page
	 * (forge_spm_base2 @0x44800000 + 0x200), already mapped above, so all
	 * printk -- including userspace init's /dev/kmsg writes -- lands in
	 * TWRP-surviving DRAM.  Read from TWRP at phys 0x44800200. */
	if (forge_spm_base2 && !forge_console_on) {
		register_console(&forge_console);
		forge_console_on = true;
	}
	if (forge_console_on)
		pr_emerg("[FORGE_M681] forge console registered, log@0x%lx\n",
			 FORGE_MARKER2_PHYS_BASE + FORGE_LOG_OFF);

	/* m681 v56: map the toprgu watchdog so do_one_initcall() can kick it
	 * across the level 4/5/6 window where no kicker kthread runs yet. */
	forge_wdt_base = ioremap(FORGE_WDT_PHYS_BASE, FORGE_WDT_REGION_SIZE);
	if (!forge_wdt_base)
		pr_emerg("[FORGE_M681] wdt ioremap(0x%lx) FAILED\n",
			 FORGE_WDT_PHYS_BASE);

	/* m681 #113 RGU boot forensics: latch + decode the PREVIOUS reset's
	 * cause and the request-line arming the preloader/LK handed us.  Must
	 * run BEFORE forge_m681_wdt_arm() below (MODE write may clear STA).
	 * In the #112 bootloop the SAME kernel boots again after the silent
	 * ~57s death, so this line classifies its own previous death:
	 *   HWWDT          -> counter timeout = the box HUNG, dog bit later
	 *   THERMAL_DIRECT -> RGU thermal line fired (re-armed despite guard)
	 *   SPMWDT/SPM_THERMAL/SECURITY/DEBUG -> that requester is the killer
	 *   0x0            -> non-RGU reset (PMIC-level) OR LK cleared STA. */
	if (forge_wdt_base) {
		u32 sta = readl(forge_wdt_base + FORGE_WDT_STA_OFF);
		u32 rq  = readl(forge_wdt_base + FORGE_WDT_REQ_MODE_OFF);

		forge_rgu_boot_sta = sta;
		forge_rgu_boot_reqmode = rq;
		pr_emerg("[FORGE_M681] #113 RGU boot forensics: STA=0x%08x [%s%s%s%s%s%s%s%s] REQ_MODE=0x%08x MODE=0x%x LEN=0x%x NONRST2=0x%08x\n",
			 sta,
			 (sta & 0x80000000U) ? "HWWDT " : "",
			 (sta & 0x40000000U) ? "SWWDT " : "",
			 (sta & 0x20000000U) ? "IRQWDT " : "",
			 (sta & 0x10000000U) ? "SECURITY " : "",
			 (sta & 0x00080000U) ? "DEBUG " : "",
			 (sta & 0x00040000U) ? "THERMAL_DIRECT " : "",
			 (sta & 0x00000002U) ? "SPMWDT " : "",
			 (sta & 0x00000001U) ? "SPM_THERMAL " : "",
			 rq,
			 readl(forge_wdt_base + FORGE_WDT_MODE_OFF),
			 readl(forge_wdt_base + FORGE_WDT_LENGTH_OFF),
			 readl(forge_wdt_base + FORGE_WDT_NONRST2_OFF));
		/* TWRP-decodable stash on the proven aux-marker channel. */
		forge_m681_mark_aux(0xEB, sta);
		forge_m681_mark_aux(0xEC, rq);
	}

	/* m681 v119: map the GPT block so forge_m681_wdt_kick can sample the GPT2
	 * free-run counter (0x10008028) and answer "does it count?". */
	forge_gpt_base = ioremap(0x10008000UL, 0x100);

	/* m681 #124: map topckgen/apmixed HERE (not just lazily in the keywatch)
	 * so gpuclk snapshots cover the kbase-probe-era power-on too. */
	forge_topck_base = ioremap(0x10000000UL, 0x1000);
	forge_apmix_base = ioremap(0x1000C000UL, 0x1000);

	/* m681 v91: PROPER CNTVCT fix attempt — directly enable the cpuxgpt system
	 * counter (MCUSYS 0x10200000 + CTL 0x670, bit0 = EN_CPUXGPT).  The ARM
	 * architected counter that feeds get_cycles()/ktime is started by
	 * enable_cpuxgpt(), which the m6-graft never calls (timer DT node binds the
	 * generic mtk_timer.c via "mediatek,mt6577-timer" instead of the mt6755
	 * apxgpt driver that runs setup_syscnt()).  v89's SMC enable did NOT take;
	 * try a DIRECT write here (works iff MCUSYS is not ATF write-protected on
	 * this graft).  If it sticks, CNTVCT ticks -> ktime advances -> udelay AND
	 * hrtimers become accurate and heartbeat@0xFC goes >0.  The __delay()
	 * cpu_relax fallback (arch/arm64/lib/delay.c) stays as backstop if dropped. */
	{
		void __iomem *cx = ioremap(0x10200000UL, 0x1000);

		if (cx) {
			u32 ctl = readl(cx + 0x670);

			writel(ctl | 0x1U, cx + 0x670);
			pr_emerg("[FORGE_M681] v91 cpuxgpt CTL 0x%x -> 0x%x (direct EN_CPUXGPT)\n",
				 ctl, readl(cx + 0x670));
			iounmap(cx);
		} else {
			pr_emerg("[FORGE_M681] v91 cpuxgpt ioremap(0x10200000) FAILED\n");
		}
	}

	pr_emerg("[FORGE_M681] marker late base=%p base2=%p wdt=%p\n",
		 forge_spm_base, forge_spm_base2, forge_wdt_base);
	forge_m681_mark(FORGE_STAGE_MARKER_LATE_INIT);

	/* m681 v47: arm the toprgu HW watchdog ourselves using the just-mapped
	 * post-mm_init forge_wdt_base, applying MODE read-modify-write ONLY
	 * (no LENGTH reset — the v44b killer).  This restores the warm-reboot
	 * safety net that v44 lost (mtu3d BUG_ON panic path disabled) and that
	 * v45 could not recover through mtk_wdt_probe (request_irq wedges before
	 * the v45 single-mode mode_config is reached).  See forge_m681_wdt_arm()
	 * header for the full root-cause chain.  forge_wdt_base ioremap above is
	 * the l681-M18-proven live mapping path; do not call before it succeeds. */
	forge_m681_wdt_arm();

	/* m681 v48: register the panic-notifier SWRST safety net AFTER arming
	 * the WDT to guarantee the marker engine + toprgu mapping is live
	 * before any panic handler can fire.  See forge_m681_panic_handler()
	 * header for rationale.  Uses the SAME forge_wdt_base mapping the
	 * arm routine just established (the l681-M18-proven post-mm_init
	 * ioremap path); toprgu_base from the (denied) driver probe is NULL. */
	if (!forge_panic_nb_on) {
		atomic_notifier_chain_register(&panic_notifier_list,
					       &forge_m681_panic_nb);
		forge_panic_nb_on = true;
	}
	pr_emerg("[FORGE_M681] v48 panic-SWRST recovery net registered\n");

	/* early_ioremap_reset() already retired the fixmap path before this
	 * point; the stale early mappings are abandoned, not iounmapped. */
}

/*
 * m681 v118: TIMER-FIX verification (late_initcall — guaranteed reached if the
 * kernel completes its initcalls, and runs BEFORE userspace can clobber the
 * marker page). Measures whether the GPT2 free-run counter (phys 0x10008028)
 * counts and whether ktime now ADVANCES (i.e. TIMERFIX-C1/C2 made the apxgpt
 * clocksource win over the frozen arch_sys_counter). Result -> diag 0xDC, packed
 * with a 0xD2 sentinel in the high byte so the read is unambiguous:
 *   bits31..24 = 0xD2  (sentinel: this late_initcall ran)
 *   bit23      = 1 if 0x10008028 delta != 0  (GPT2 counter COUNTS)
 *   bit22      = 1 if ktime delta != 0        (ktime ADVANCES => fix worked)
 *   bits21..0  = low 22 bits of the ktime delta in ns (magnitude)
 */
static int __init forge_timer_verify(void)
{
	void __iomem *g;
	u64 k1, k2;
	u32 c1 = 0, c2 = 0, dk, packed;
	volatile int spin;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	if (forge_virt)
		return 0;	/* Rung C: 0x10008000 = virt PCIe MMIO window */
	g = ioremap(0x10008000UL, 0x100);

	k1 = ktime_to_ns(ktime_get());
	if (g)
		c1 = readl(g + 0x28);
	for (spin = 0; spin < 3000000; spin++)
		cpu_relax();
	k2 = ktime_to_ns(ktime_get());
	if (g) {
		c2 = readl(g + 0x28);
		iounmap(g);
	}
	dk = (u32)(k2 - k1);
	packed = 0xD2000000u
		| ((c2 != c1) ? (1u << 23) : 0u)
		| ((dk != 0)  ? (1u << 22) : 0u)
		| (dk & 0x3FFFFFu);
	forge_m681_diag(0xDCu, packed);
	pr_emerg("[FORGE_M681] v118 timer_verify: GPT2 %u->%u (d=%u) ktime d=%u ns packed=0x%x\n",
		 c1, c2, c2 - c1, dk, packed);
	return 0;
}
late_initcall(forge_timer_verify);

/*
 * m681 v126: PERSISTENT kernel-log dump to eMMC (survives warm-reset AND battery
 * pull / TWRP, unlike the small DRAM forge console). A kthread periodically writes
 * the whole printk log buffer (log_buf_addr_get/len) to the expdb partition
 * (/dev/block/mmcblk0p4 — MTK exception-debug, safe to overwrite for bring-up),
 * prefixed with an 'FLOG' magic + length so it's findable from TWRP:
 *   adb shell "dd if=/dev/block/mmcblk0p4 bs=1M count=2 of=/sdcard/klog.bin" ; pull ; strings.
 * Retries filp_open until eMMC enumerates. Boot stalls (e.g. zygote) are captured
 * up to the last 2s flush. Remove for shipping. */
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/printk.h>

/* forge m681 2026-07-31: this dumper is the "Remove for shipping" scaffold named
 * four lines above, and it was never removed. MEASURED on device, all5 image,
 * fresh boot, screen on, idle:
 *
 *   /proc/diskstats mmcblk0p4: 229 488 sectors written in 10 s = ~11.5 MB/s,
 *   CONTINUOUSLY. loadavg 14 with 719% idle CPU and 32% iowait; mmcqd/0 and a
 *   D-state forge_klog at the top of `top`; SwapTotal=0; 40 MB free of 1780.
 *
 * Mechanism, from the loop below: every 400 ms it rewrites the ENTIRE kernel log
 * ring (log_buf_len, 8 MB on our cmdline) to expdb and vfs_fsync()s it. The write
 * is unconditional and whole-ring, so it does not matter what the log contains --
 * and the fsync serialises the eMMC queue, which is why the whole system waits on
 * storage rather than on CPU. ~1 TB/day into a 10 MB partition is also a flash-wear
 * problem independent of the lag.
 *
 * There was no way to stop it: no knob, and nothing ever calls kthread_stop().
 *
 * Why it existed: it is the post-mortem channel from the era when this port had no
 * adb and a wedge left no other trace (see the header above, and the FLOG/FEMK
 * notes below). That era is over -- adb works, the device boots to UI -- so the
 * default flips. The thread is KEPT and can be re-armed live for a bring-up
 * session; only the flushing is gated, so the forensic capability is not lost.
 *
 * forge_klog_ms: 0 (default) = do not touch eMMC at all. >0 = flush period in ms;
 * write 400 to restore the historical behaviour exactly. Runtime-writable, so a
 * lane chasing a wedge can arm it without a rebuild.
 */
static int forge_klog_ms;
module_param(forge_klog_ms, int, 0644);
MODULE_PARM_DESC(forge_klog_ms,
		 "expdb klog flush period in ms (0 = off, default; 400 = historical bring-up behaviour)");

/* ⛔ 2026-07-31, learned the hard way, same day: gating this OFF removed the
 * project's only post-mortem channel, and hours later a boot-breaking change
 * needed exactly that channel. expdb held a STALE flush from before the gate,
 * /proc/last_kmsg was 0 bytes, and the device dropped off the bus before the
 * DRAM ring could be pulled. Net result: a failed boot with no picture of why.
 *
 * The gate itself was right — 13.9 MB/s of continuous whole-ring rewrites was
 * starving the whole system (iowait 36%, loadavg 14) and wearing the flash.
 * What was missing is a way to ARM it for a risky boot, before that boot.
 * A module_param cannot do that: it is only writable once userspace is up, and
 * a boot that dies never gets there.
 *
 * core_param gives a cmdline name with no module prefix. Short alias because
 * LK truncates this device's cmdline at ~99 chars and long names get eaten:
 *     fklog=400
 * arms the historical behaviour from the bootloader for one boot.
 *
 * RULE this encodes: before changing anything that can prevent boot, arm a
 * channel that survives the failure. Do not rely on a knob that only exists
 * after the failure would have happened. */
core_param(fklog, forge_klog_ms, int, 0644);

static int forge_log_dump_fn(void *arg)
{
	const char *path = "/dev/block/mmcblk0p4";	/* expdb */
	char hdr[16];
	while (!kthread_should_stop()) {
		struct file *f;
		char *lb = log_buf_addr_get();
		u32 ll = log_buf_len_get();
		loff_t pos = 0;
		int period = forge_klog_ms;

		/* Gated OFF by default. Poll the knob cheaply so it can be armed
		 * at runtime; do NOT open or touch eMMC while disabled. */
		if (period <= 0) {
			msleep(1000);
			continue;
		}

		msleep(period);	/* v152: faster flush to capture the final ~1s before reset */
		f = filp_open(path, O_RDWR | O_LARGEFILE, 0);
		if (IS_ERR(f))
			continue;	/* eMMC not ready yet -> retry */
		memcpy(hdr, "FLOGm681", 8);
		memcpy(hdr + 8, &ll, 4);
		memset(hdr + 12, 0, 4);
		kernel_write(f, hdr, sizeof(hdr), pos);
		pos = sizeof(hdr);
		if (lb && ll)
			kernel_write(f, lb, ll, pos);
		vfs_fsync(f, 0);
		filp_close(f, NULL);
	}
	return 0;
}

static int __init forge_log_dump_init(void)
{
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	if (forge_virt)
		return 0;	/* Rung C: no expdb/mmcblk0p4 on virt — endless filp_open retry */
	kthread_run(forge_log_dump_fn, NULL, "forge_klog");
	pr_emerg("[FORGE_M681] v126 eMMC klog dumper thread up, flushing GATED OFF (forge_klog_ms=%d; set >0 to arm, 400 = historical)\n",
		 forge_klog_ms);
	return 0;
}
late_initcall(forge_log_dump_init);

/*
 * m681 v171: SYNCHRONOUS eMMC probe marker — the only post-mortem channel that
 * survives a display BUS-HANG wedge on this device.
 *
 * Why: both DRAM marker regions are 100% owned by TWRP in recovery (PROVEN: the
 * post-wedge read of 0x444f0000 is byte-identical across DIFFERENT wedge builds
 * — seq=6208, names "%%Sb-h$bc"/"mepB-h$pa" — i.e. it is TWRP's own footprint,
 * not our kernel's marks), and SPM IO is unreadable via /dev/mem (STRICT_DEVMEM).
 * forge_klog (async kthread) only flushes at late_initcall, which a pre-
 * late_initcall display wedge never reaches. So we write a tiny record
 * SYNCHRONOUSLY (filp_open + kernel_write + fsync) to a FIXED offset (3MB) of
 * expdb/p4 — far past the klog ring dump (offset 0) and past the 2MB the test
 * harness zeroes — right before each DISPLAY driver's ->probe (called from
 * really_probe, drivers/base/dd.c). After a wedge+recovery, read it back:
 *   dd if=/dev/block/mmcblk0p4 bs=1 skip=$((0x300000)) count=2048 | strings
 * The LAST "FEMK <seq> <name>" record = the driver whose probe hung.
 *
 * Filtered to display-family driver names so non-display probes (thousands) pay
 * only a strstr loop, never eMMC IO. eMMC is core and enumerates long before the
 * display stack probes, so filp_open succeeds by then (if it ever fails we just
 * skip — meaning the wedge would be earlier than eMMC, which display is not).
 */
static u32 forge_emmc_seq;
void forge_m681_emmc_mark(const char *tag)
{
	static const char * const disp_kw[] = {
		"disp", "DISP", "dsi", "mtkfb", "lcm", "smi", "m4u", "M4U",
		"mali", "kbase", "ged", "ddp", "mdp", "ovl", "rdma", "wdma",
		NULL };
	struct file *f;
	char buf[64];
	int n, i, hit = 0;
	loff_t pos;

	if (!tag || !*tag)
		return;
	for (i = 0; disp_kw[i]; i++)
		if (strstr(tag, disp_kw[i])) { hit = 1; break; }
	if (!hit)
		return;

	f = filp_open("/dev/block/mmcblk0p4", O_RDWR | O_LARGEFILE, 0);
	if (IS_ERR(f))
		return;			/* eMMC not up yet -> skip */
	pos = (loff_t)0x300000 + (loff_t)((forge_emmc_seq & 31u) * 64u);
	n = snprintf(buf, sizeof(buf), "FEMK %05u %s", forge_emmc_seq, tag);
	if (n < 0)
		n = 0;
	if (n > (int)sizeof(buf))
		n = sizeof(buf);
	if (n < (int)sizeof(buf))
		memset(buf + n, 0, sizeof(buf) - n);
	forge_emmc_seq++;
	kernel_write(f, buf, sizeof(buf), pos);
	vfs_fsync(f, 0);
	filp_close(f, NULL);
}
EXPORT_SYMBOL(forge_m681_emmc_mark);

/*
 * m681 v152: death-time probe. expdb shows the boot reaching zygote and then the
 * kernel log goes quiet -> ambiguous: did the SoC reset, or did the kernel just
 * stop printing (framework logs to logcat, not dmesg)? A kernel timer fires in
 * softirq every 500ms and stamps a monotonic ktime ms + the CPU it ran on. The
 * timer keeps firing until the kernel is truly wedged/reset, so the LAST "HB ms="
 * in expdb pins the exact instant + CPU where execution stops. Correlate against
 * BOOTPROF ms (same ktime scale). Remove for shipping.
 */
static struct timer_list forge_hb_timer;
static void forge_hb_fn(unsigned long data)
{
	u32 ms = (u32)(ktime_to_ns(ktime_get()) / 1000000);
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;

	pr_emerg("[FORGE_M681] HB ms=%u cpu=%d\n", ms, raw_smp_processor_id());

	/* m681 #113: TWRP-surviving last-HB slot (0xFB tag | uptime in 100ms
	 * units) — pins the death instant even if every console page is lost. */
	if (forge_spm_base2)
		writel(0xFB000000U | ((ms / 100U) & 0x00FFFFFFU),
		       forge_spm_base2 + 0x7C);

	/* m681 #113 RGU request-line watch: if ANY reset-request line got
	 * (re-)armed since the last beat, log WHO/WHEN and clear it again.
	 * The 500ms cadence timestamps a late armer (e.g. a userspace-driven
	 * trip rewrite) that a single late_initcall clear would miss. */
	if (b) {
		u32 rq = readl(b + FORGE_WDT_REQ_MODE_OFF);

		if (rq & FORGE_WDT_REQ_LINES) {
			pr_emerg("[FORGE_M681] #113 RGU watch: REQ_MODE=0x%08x armed at ms=%u — clearing 0x%08x\n",
				 rq, ms, rq & FORGE_WDT_REQ_LINES);
			writel((rq & ~FORGE_WDT_REQ_LINES) | FORGE_WDT_REQ_MODE_KEY,
			       b + FORGE_WDT_REQ_MODE_OFF);
		}
	}
	mod_timer(&forge_hb_timer, jiffies + msecs_to_jiffies(500));
}
static int __init forge_hb_init(void)
{
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	init_timer(&forge_hb_timer);
	forge_hb_timer.function = forge_hb_fn;
	forge_hb_timer.data = 0;
	forge_hb_timer.expires = jiffies + msecs_to_jiffies(500);
	add_timer(&forge_hb_timer);
	pr_emerg("[FORGE_M681] v152 heartbeat timer armed\n");
	return 0;
}
late_initcall(forge_hb_init);

/*
 * m681 #113: RGU request-line quiesce.  The #109/#112 ~57s reset is SILENT
 * (6MB expdb: zero reboot/panic/init lines) and survives the cdc652dc tscpu
 * guard, so every remaining RGU reset-REQUEST line is distrusted until the
 * killer is identified: none of them is needed during bring-up (thermal is
 * uncalibrated and already guarded; SPM fw is donor-vintage; the EINT debug
 * key and DEBUG/DFD paths are unused; SYSRST is MD-side and the modem is not
 * built).  The petted counter dog stays armed — a genuine hang still
 * warm-resets to TWRP with markers intact ([[feedback_never_disarm_wdt]]:
 * this clears request LINES, never the dog).  Runs late_initcall_sync so it
 * lands AFTER tscpu_init's own WD_REQ_DIS and prints the pre-clear value =
 * exactly what LK + all initcalls left armed.  The HB watch (500ms) re-clears
 * and timestamps any later re-armer.
 */
static int __init forge_rgu_quiesce_init(void)
{
	void __iomem *b = toprgu_base ? toprgu_base : forge_wdt_base;
	u32 rq;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	if (forge_virt || !b)
		return 0;
	rq = readl(b + FORGE_WDT_REQ_MODE_OFF);
	writel((rq & ~FORGE_WDT_REQ_LINES) | FORGE_WDT_REQ_MODE_KEY,
	       b + FORGE_WDT_REQ_MODE_OFF);
	pr_emerg("[FORGE_M681] #113 RGU quiesce: REQ_MODE 0x%08x -> 0x%08x (boot: STA=0x%08x REQ_MODE=0x%08x)\n",
		 rq, readl(b + FORGE_WDT_REQ_MODE_OFF),
		 forge_rgu_boot_sta, forge_rgu_boot_reqmode);
	return 0;
}
late_initcall_sync(forge_rgu_quiesce_init);

/*
 * ===== m681 #115: MT6351 PWRKEY/HOMEKEY long-press-reset gate + keywatch =====
 *
 * #113 device verdict (expdb113_deathloop): RGU STA=0x00000000 across the loop
 * => the ~43s/57s killer is NOT the RGU at all — it is a PMIC-level reset
 * (a full MT6351 restart also wipes RGU STA).  Timeline FACTs:
 *   32.3–32.8 s  [FORGE_PMIC] F2 PWRKEY PRESS int (deb=0)  — deb read via
 *                TOPSTATUS 0x0220, which FORGE v256 already remapped to the
 *                MT6351-true address, so "debounced level = pressed" is a
 *                REAL read, not a wrong-chip artifact.  (INT_STATUS0/INT_CON0
 *                sit at 0x02E0/0x02C2 on BOTH MT6351 and MT6353 — the int
 *                decode is address-correct too.)  No RELEASE int ever fires.
 *                Simultaneously the AP keypad matrix floods "pressed" on
 *                KP_COL0/COL1 (keycodes 0,9,18..63 / 1,10,19..64) — one
 *                electrical event hitting both key nets.
 *   43.3–43.8 s  death (last HB ms=43300) = press + 11.0 s
 * 11 s is exactly the MT6351 RG_PWRKEY_RST_TD long-press-reset delay
 * (TOP_RST_MISC 0x02B6: HOMEKEY_RST_EN=bit8, PWRKEY_RST_EN=bit9, TD=bits12-13
 * per the authentic 3.10 stocktruth upmu_hw.h).  INFERENCE (strong): the PMIC
 * hardware long-press reset fired.  The WORKING 3.18 (#230) does not reset on
 * the same board+LK (and also skips long_press_reboot_function_setting), so
 * the PWRKEY net reading "held" is 4.4-specific (pinmux/rail/KP-block state —
 * measured by the keywatch below), while the LPRST itself is just PMIC
 * default behavior doing its designed job on a bogus input.
 *
 * Gate: clear ONLY bits 8+9 via the TOP_RST_MISC_CLR (0x02BA) write-1-to-
 * clear register — atomic, cannot disturb the WDTRSTB bits that the RGU dog
 * needs.  This is the "gate kpd longpress first" step the 4.4 port plan
 * already demanded.  Trade-off (documented): while this kernel runs, holding
 * the physical power button will NOT hardware-reset; the petted RGU dog
 * remains the hang-recovery path, and BROM/preloader key combos are
 * unaffected (PMIC defaults are restored by the reset itself on next boot).
 * Root fix tracked separately: find WHY the PWRKEY net reads pressed at ~30s+
 * (the keywatch's 5 s TOPSTATUS samples time-bracket it for free).
 *
 * All addresses are authentic MT6351 (stocktruth upmu_hw.h), accessed via raw
 * pwrap_read/pwrap_write — deliberately NOT the pmic_* accessors and their
 * MT6353-vintage tables.  Runs from a kthread (pwrap ops may sleep); first
 * gate write lands at late_initcall (~10 s), well before the ~32 s press
 * window.
 */
#define MT6351_TOPSTATUS_ADR		0x0220U	/* PWRKEY_DEB=bit1, HOMEKEY_DEB=bit2 (0 = pressed) */
#define MT6351_TOP_RST_MISC_ADR		0x02B6U
#define MT6351_TOP_RST_MISC_CLR_ADR	0x02BAU	/* W1C companion */
#define MT6351_RST_EN_BITS		0x0300U	/* bit8 HOMEKEY_RST_EN | bit9 PWRKEY_RST_EN */
#define MT6351_INT_STATUS0_ADR		0x02E0U	/* bit0 = PWRKEY press latch (both chips) */

/* m681 #116: PMIC reset-reason latches + rail probe (57s wall, phase 2).
 * #115 device data: no PWRKEY press at all this boot (pwr_deb=1 throughout),
 * LPRST gate held (RST_MISC 0x1205->0x1005, no re-arm), death STILL at ~57s
 * with RGU STA=0.  Decode of pre=0x1205 also showed WDTRSTB_EN=1(b0) and the
 * STICKY WDTRSTB_STATUS=1(b2) that nobody ever clears — meaning an RGU dog
 * bite is DELIVERED to the PMIC and ends as a PMIC-level restart that wipes
 * RGU STA.  So "STA=0" does NOT exclude hang->dog for the 57s death.  Stock
 * 3.10 itself reads PMIC_WDTRSTB_STATUS as its "is_wdt_reboot" flag
 * (pmic_initial_setting.c:149).  Probe: READ the latches, PRINT, then CLEAR
 * them (stock's own mt6311.c pulse pattern) — the NEXT boot after a death
 * then reports which mechanism killed THIS boot:
 *   WDTRSTB_STATUS=1   -> RGU dog bite (= the box HUNG; hunt the wedge)
 *   JUST_PWRKEY_RST=1  -> PMIC key long-press reset (should be impossible now)
 *   both 0             -> PMIC restarted on its own: UVLO/brownout/OC class
 *                         (team-lead's rail-collapse hypothesis) — then the
 *                         per-5s VCORE/VGPU/OC samples below guide the fix.
 * All addresses authentic MT6351 (stocktruth upmu_hw.h). */
#define MT6351_STRUP_CON1_ADR		0x0002U	/* PMU_THR_STATUS = bits 8..10 */
#define MT6351_STRUP_CON12_ADR		0x0018U	/* CLR_JUST_RST=b4 (pulse), JUST_PWRKEY_RST=b14 */
#define MT6351_TOP_RST_MISC_SET_ADR	0x02B8U	/* W1S companion */
#define MT6351_WDTRSTB_STATUS_CLR_BIT	0x0008U	/* TOP_RST_MISC b3: pulse to clear b2 */
#define MT6351_BUCK_OC_CON0_ADR		0x041EU	/* per-buck OC latches (VGPU=b1) */
#define MT6351_BUCK_VCORE_CON5_ADR	0x060AU	/* VCORE VOSEL_ON */
#define MT6351_BUCK_VGPU_CON2_ADR	0x0618U	/* VGPU EN */
#define MT6351_BUCK_VGPU_CON5_ADR	0x061EU	/* VGPU VOSEL_ON */

extern s32 pwrap_read(u32 adr, u32 *rdata);
extern s32 pwrap_write(u32 adr, u32 wdata);

static int forge_mt6351_keywatch_fn(void *unused)
{
	u32 pre = 0xDEAD, post = 0xDEAD, rst = 0, ts = 0, is0 = 0;
	s32 r1, r2;

	/* ENTER marker BEFORE the first pwrap op: if pwrap wedges (the 3.18
	 * v247 failure mode), the capture shows exactly where we died. */
	pr_emerg("[FORGE_M681] #115 MT6351 LPRST gate ENTER (raw pwrap, authentic MT6351 addrs)\n");
	r1 = pwrap_read(MT6351_TOP_RST_MISC_ADR, &pre);
	r2 = pwrap_write(MT6351_TOP_RST_MISC_CLR_ADR, MT6351_RST_EN_BITS);
	pwrap_read(MT6351_TOP_RST_MISC_ADR, &post);
	pr_emerg("[FORGE_M681] #115 MT6351 LPRST gate: TOP_RST_MISC pre=0x%04x -> post=0x%04x (rd=%d wr=%d) PWRKEY_RST_EN was %u, HOMEKEY_RST_EN was %u, TD=%u\n",
		 pre, post, r1, r2,
		 (pre >> 9) & 0x1, (pre >> 8) & 0x1, (pre >> 12) & 0x3);

	/* m681 #116: report WHY the previous boot ended (see defines above),
	 * then CLEAR the latches so the NEXT boot reports THIS boot's death.
	 * `pre` still holds TOP_RST_MISC from before any of our writes. */
	{
		u32 s12 = 0, s1 = 0, oc0 = 0;

		pwrap_read(MT6351_STRUP_CON12_ADR, &s12);
		pwrap_read(MT6351_STRUP_CON1_ADR, &s1);
		pwrap_read(MT6351_BUCK_OC_CON0_ADR, &oc0);
		pr_emerg("[FORGE_M681] #116 PMIC reset-reason: WDTRSTB_STATUS=%u (1=prev reset was RGU-dog via WDTRSTB) JUST_PWRKEY_RST=%u THR_STATUS=0x%x BUCK_OC_CON0=0x%04x (STRUP_CON12=0x%04x STRUP_CON1=0x%04x)\n",
			 (pre >> 2) & 0x1, (s12 >> 14) & 0x1,
			 (s1 >> 8) & 0x7, oc0, s12, s1);
		/* pulse WDTRSTB_STATUS_CLR (b3) via the W1S/W1C companions */
		pwrap_write(MT6351_TOP_RST_MISC_SET_ADR, MT6351_WDTRSTB_STATUS_CLR_BIT);
		pwrap_write(MT6351_TOP_RST_MISC_CLR_ADR, MT6351_WDTRSTB_STATUS_CLR_BIT);
		/* pulse CLR_JUST_RST (STRUP_CON12 b4), RMW-preserving (no W1S/W1C
		 * companions on STRUP_CONs; stock mt6311.c uses the same pulse) */
		pwrap_write(MT6351_STRUP_CON12_ADR, s12 | (1u << 4));
		pwrap_write(MT6351_STRUP_CON12_ADR, s12 & ~(1u << 4));
	}

	/* m681 #121 (port318to44 D1 readback): the 4.4 build links only the
	 * gpufreq BRINGUP stub, so nothing ever programs MMPLL (the GPU PLL)
	 * or the topckgen mfg_sel mux — the first real GPU job would run on
	 * LK/reset clock state.  Stream CLK_CFG_1 (0x10000050, mfg_sel bits
	 * 25:24) + APMIXED MMPLL_CON0/CON1 (0x1000C240/244) through the death
	 * window.  Both blocks are AO; reads are side-effect-free. */
	{
		if (!forge_topck_base)
			forge_topck_base = ioremap(0x10000000UL, 0x1000);
		if (!forge_apmix_base)
			forge_apmix_base = ioremap(0x1000C000UL, 0x1000);
	}

	while (!kthread_should_stop()) {
		u32 vcore5 = 0, vgpu2 = 0, vgpu5 = 0, oc0 = 0;
		u32 rsta = 0xDEAD, rmode = 0xDEAD;
		u32 clkcfg1 = 0xDEAD, mmpll0 = 0xDEAD, mmpll1 = 0xDEAD;
		void __iomem *rb = toprgu_base ? toprgu_base : forge_wdt_base;

		pwrap_read(MT6351_TOPSTATUS_ADR, &ts);
		pwrap_read(MT6351_TOP_RST_MISC_ADR, &rst);
		pwrap_read(MT6351_INT_STATUS0_ADR, &is0);
		/* #116 rail probe: VCORE/VGPU VOSEL_ON + VGPU EN + OC latches —
		 * if a rail is mis-set or an OC latch pops as GPU load ramps
		 * (~57s), it shows here 5 s before the lights go out. */
		pwrap_read(MT6351_BUCK_VCORE_CON5_ADR, &vcore5);
		pwrap_read(MT6351_BUCK_VGPU_CON2_ADR, &vgpu2);
		pwrap_read(MT6351_BUCK_VGPU_CON5_ADR, &vgpu5);
		pwrap_read(MT6351_BUCK_OC_CON0_ADR, &oc0);
		/* m681 #120: stream the RGU state through the death window —
		 * the #119 death left box+latches clean, so the last 5 s
		 * sample before silence is the closest look at the RGU we
		 * get without a bus analyzer. */
		if (rb) {
			rsta = readl(rb + FORGE_WDT_STA_OFF);
			rmode = readl(rb + FORGE_WDT_MODE_OFF);
		}
		/* #121: GPU clock-state snapshot (see block comment above loop) */
		if (forge_topck_base)
			clkcfg1 = readl(forge_topck_base + 0x50);
		if (forge_apmix_base) {
			mmpll0 = readl(forge_apmix_base + 0x240);
			mmpll1 = readl(forge_apmix_base + 0x244);
		}
		pr_emerg("[FORGE_M681] #115 keywatch ms=%u TOPSTATUS=0x%04x (pwr_deb=%u home_deb=%u; 0=pressed) RST_MISC=0x%04x INT_STA0=0x%04x VCORE_ON=0x%04x VGPU_EN=0x%04x VGPU_ON=0x%04x OC0=0x%04x RGU_STA=0x%08x RGU_MODE=0x%x CLK_CFG_1=0x%08x (mfg_sel=%u) MMPLL=0x%08x/0x%08x\n",
			 (u32)(ktime_to_ns(ktime_get()) / 1000000),
			 ts, (ts >> 1) & 0x1, (ts >> 2) & 0x1, rst, is0,
			 vcore5, vgpu2, vgpu5, oc0, rsta, rmode,
			 clkcfg1, (clkcfg1 >> 24) & 0x3, mmpll0, mmpll1);
		if (rst & MT6351_RST_EN_BITS) {
			/* m681-49-disp: on 4.9 PMIC writes are dropped (read-only
			 * WACS2), so the clear never sticks - say it once */
			static bool rearm_told;
			s32 wr = pwrap_write(MT6351_TOP_RST_MISC_CLR_ADR, MT6351_RST_EN_BITS);

			if (!rearm_told || wr == 0)
				pr_emerg("[FORGE_M681] #115 LPRST RE-ARMED (RST_MISC=0x%04x) — clearing again (wr=%d)\n",
					 rst, wr);
			rearm_told = true;
		}
		msleep(5000);
	}
	return 0;
}

static int __init forge_mt6351_keywatch_init(void)
{
	struct task_struct *t;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	if (forge_virt)
		return 0;	/* no pwrap/PMIC under qemu -M virt */
	t = kthread_run(forge_mt6351_keywatch_fn, NULL, "forge_keywatch");
	if (IS_ERR(t))
		pr_emerg("[FORGE_M681] #115 keywatch kthread FAILED (%ld)\n", PTR_ERR(t));
	return 0;
}
late_initcall(forge_mt6351_keywatch_init);

/*
 * m681 v155: EXPLICITLY online secondary CPUs. On this platform boot-time
 * smp_init does NOT bring up secondaries; the MTK HPS governor was the only
 * caller of cpu_up(N>0), and HPS is disabled (hps_init returns early, v76).
 * So removing the _cpu_up guard alone changed nothing -- nobody called cpu_up.
 * Drive it ourselves from late_initcall_sync (after SMP is fully set up, before
 * userspace/zygote). STEP 1: only CPU1 (the _cpu_up gate still caps at >=2).
 * Logs the cpu masks + cpu_up() return so one boot tells us exactly where it
 * stands: present(1)=0 -> smp_prepare_cpus/cpu_prepare skipped it; cpu_up<0 ->
 * the mt-boot/PSCI/spm_mtcmos power path failed (and which errno). On success,
 * heartbeat shows cpu=1 and the box survives past the ~11s wdk-starvation reset.
 */
#include <linux/cpu.h>
/* m681 4.9 G1.2: off by default. The 4.9 cpu_up path is the m5c (mt6735)
 * mt-boot/PSCI cpu_ops, never exercised on MT6755; bring-up runs single-core
 * (maxcpus=1) until the kernel reaches userspace. forge_smp_up=1 re-enables
 * the 4.4 v159 force-up of CPU1..7. */
static int forge_smp_up;
core_param(forge_smp_up, forge_smp_up, int, 0444);

static int __init forge_bringup_secondary(void)
{
	unsigned int cpu;
	int ret;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	if (!forge_smp_up) {
		pr_emerg("[FORGE_M681] secondary bring-up skipped (forge_smp_up=0), online=%u\n",
			 num_online_cpus());
		return 0;
	}

	pr_emerg("[FORGE_M681] v159 pre-bringup: possible=%u present=%u online=%u\n",
		 num_possible_cpus(), num_present_cpus(), num_online_cpus());
	/* v159: bring up ALL secondaries CPU1..7. Cluster0 (1-3) proven in v158.
	 * Cluster1 big cores (4-7) are a separate power domain (bypass_cl1_armpll=1)
	 * -- untested; each cpu_up is logged BEFORE and AFTER so if a big core wedges
	 * the bus, the last "cpu_up(N) ->" line (no matching return) pinpoints it.
	 * dbgregs CoreSight panic fixed (mt_dbg.c NULL-guard). */
	for (cpu = 1; cpu <= 7; cpu++) {
		pr_emerg("[FORGE_M681] v159 cpu_up(%u) ->\n", cpu);
		ret = cpu_up(cpu);
		pr_emerg("[FORGE_M681] v159 cpu_up(%u)=%d online_now=%u\n",
			 cpu, ret, num_online_cpus());
	}
	/* m681 v167: PIN all 8 online. All secondaries DO come up here (online_now=8),
	 * but HPS is active on our 4.4 (unlike the inert HPS on the working 3.18) and
	 * offlines the idle secondaries -> the neofetch caught only 1 core at idle.
	 * The working 3.18 keeps 0-7 online. Match it decisively: freeze the online set
	 * after force-up so nothing (HPS/PPM/idle/thermal) can cpu_down them.
	 * (Frequencies stay PPM-managed exactly like 3.18; the cluster-1 DVFS-table
	 * error is benign — the 3.18 reference shows the identical message.) */
	cpu_hotplug_disable();
	pr_emerg("[FORGE_M681] v167 hotplug frozen, online=%u (pinned all cores)\n",
		 num_online_cpus());
	return 0;
}
late_initcall_sync(forge_bringup_secondary);

/*
 * m681 v164: SELF-HEALING display bring-up gate. Re-enabling the whole display
 * stack (M4U/SMI/Mali/DDP/DSI/mtkfb) at once can wedge the boot. To make that
 * SAFE without a manual fastboot/recovery, a DRAM flag at forge_spm_base2+0x1A0
 * (survives a WDT warm reset; do NOT fastboot-reboot, that wipes it) records the
 * attempt across the reset:
 *   OKAY / cleared -> this boot ATTEMPTS the display (flag := TRY1)
 *   TRY1           -> previous boot set TRY1 and never cleared it = it WEDGED
 *                     -> this boot SKIPS the display (flag := HUNG), reaches adb
 *   HUNG           -> sticky skip (stay on adb) until the flag is cleared
 * forge_disp_mark_ok() (late_initcall) clears the flag to OKAY only if we
 * ATTEMPTED and survived to late init. So: 1 wedged boot, then adb boots with
 * the display skipped and the wedge captured in expdb. Retry from adb:
 *   busybox devmem 0x448001A0 32 0   (then reboot)
 */
/*
 * v165: the gate flag MOVED from DRAM (0x44800000 = preloader-reserved region)
 * to the WDT NONRST_REG2 register (toprgu 0x10007024). The DRAM region is
 * CLOBBERED by the display's own memory buffers the moment the display is
 * enabled (that's why the v164 flag read back as 0xe28100c0 garbage and logs
 * turned to mush -- NOT fastboot, NOT M4U-scrambles-DRAM). NONRST_REG2 is a
 * register in the always-on toprgu block: it SURVIVES a WDT warm reset (that's
 * its purpose) and the display cannot touch it. The active mt6755 wdt driver
 * only READS it, so it is ours. Retry the display from adb:
 *   busybox devmem 0x10007024 32 0   (clear -> next boot ATTEMPTS), then reboot.
 * (FORGE_WDT_NONRST2_OFF now defined once at the top with the other #113 RGU
 * offsets — the #113 boot forensics also PRINTS this register, so the display
 * gate state shows up in the forensics line for free.)
 */
#define FORGE_DISP_ATTEMPT	0xD15B0001u	/* attempting display this boot */
#define FORGE_DISP_SKIP		0xD15B0002u	/* last boot wedged -> skip (sticky) */
#define FORGE_DISP_DONE		0xD15BD09Eu	/* attempted and survived to late init */
int forge_skip_display;
EXPORT_SYMBOL(forge_skip_display);
static void __iomem *forge_disp_wdt(void)
{
	return forge_wdt_base ? forge_wdt_base : toprgu_base;
}
/*
 * v167: the persistent-flag auto-recovery is SHELVED -- it can't read the WDT
 * reliably this early (of_platform runs in setup_arch, before a usable toprgu
 * mapping) and the flag doesn't survive the preloader reset path. The wedging
 * display component is instead found by BINARY SEARCH over flashes, using the
 * operator's screen + adb as the oracle: whatever is listed in the gated
 * forge_display_deny[] (of/platform.c) stays denied, the rest probes. So this
 * always returns "deny the gated set". (forge_disp_decided / the NONRST2 flag
 * defines / forge_disp_wdt() are retained for forge_disp_mark_ok and a future
 * reliable-store harness.) */
int forge_should_skip_display(void)
{
	return 1;
}
EXPORT_SYMBOL(forge_should_skip_display);
/*
 * m681 4.9 (branch m681-49-disp): the display stack is back, so the gate is
 * live again -- per BUILD, in NONRST2 (earlier kernel logs read
 * NONRST2=0xd15bd09e written by the previous 4.9 boot, i.e. the register
 * survived reset -> preloader -> LK -> TWRP 3.10 -> LK).
 *
 * The first display-stack driver init (SMI/CMDQ/dispsys/mtkfb/disp_mgr)
 * calls forge_display_gate_skip():
 *   NONRST2 == ATTEMPT(tag) or SKIP(tag) (tag = 16-bit hash of this
 *     kernel's banner): a previous boot of THIS image entered the display
 *     and never reached FORGE_DISP_DONE -> skip the display stack
 *     (NONRST2 := SKIP(tag), sticky for this image), mark 0xDB aux=previous.
 *   anything else (DONE, 0, another image's tag):
 *     attempt (NONRST2 := ATTEMPT(tag)), mark 0xDA aux=tag.
 * ATTEMPT(tag) = 0xDA000000|tag<<8, SKIP(tag) = 0xDC000000|tag<<8: bits 0-7
 * stay 0 (MTK reboot-mode bits of NONRST2 on other SoCs; m681's LK ignored
 * them in G1.5a with 0xd15bd09e, but there is no reason to set them).
 * FORGE_DISP_DONE is written FORGE_DISP_OK_UPTIME_S after boot if the kernel
 * is still alive (below the 120 s boot deadline, so a USB/userspace deadline
 * does not blame the display; the deadline itself uses emergency_restart),
 * or on a clean kernel_restart/halt/power-off before that (adb reboot,
 * init's reboot): the kernel was alive, the display did not wedge it.
 * Panic, emergency_restart and HW-WDT resets leave ATTEMPT in place.
 * A new image (new banner) always retries.
 * Re-arm the same image from adb:
 *   echo 1 > /sys/module/forge_m681_marker/parameters/disp_gate_rearm
 * State: /sys/module/forge_m681_marker/parameters/disp_gate (-1 not asked,
 * 0 attempted, 1 skipped).
 */
#define FORGE_DISP_ATTEMPT_T(t)	(0xDA000000u | ((u32)(t) << 8))
#define FORGE_DISP_SKIP_T(t)	(0xDC000000u | ((u32)(t) << 8))
#define FORGE_DISP_OK_UPTIME_S	90
static int forge_disp_gate = -1;
module_param_named(disp_gate, forge_disp_gate, int, 0444);
static DEFINE_SPINLOCK(forge_disp_gate_lock);

static u32 forge_disp_build_tag(void)
{
	const char *p = linux_banner;
	u32 h = 0x811c9dc5u;	/* FNV-1a over the banner: #N, date and tag */

	while (*p)
		h = (h ^ (u8)*p++) * 0x01000193u;
	return (h ^ (h >> 16)) & 0xFFFFu;
}

int forge_display_gate_skip(const char *who)
{
	void __iomem *w;
	u32 prev = 0, tag;
	unsigned long flags;
	bool first = false;

	spin_lock_irqsave(&forge_disp_gate_lock, flags);
	if (forge_disp_gate < 0) {
		first = true;
		tag = forge_disp_build_tag();
		w = forge_virt ? NULL : forge_disp_wdt();
		if (w)
			prev = readl(w + FORGE_WDT_NONRST2_OFF);
		if (w && (prev == FORGE_DISP_ATTEMPT_T(tag) ||
			  prev == FORGE_DISP_SKIP_T(tag))) {
			forge_disp_gate = 1;
			forge_skip_display = 1;
			writel(FORGE_DISP_SKIP_T(tag), w + FORGE_WDT_NONRST2_OFF);
		} else {
			forge_disp_gate = 0;
			if (w)
				writel(FORGE_DISP_ATTEMPT_T(tag),
				       w + FORGE_WDT_NONRST2_OFF);
		}
	}
	spin_unlock_irqrestore(&forge_disp_gate_lock, flags);

	if (first) {
		if (forge_disp_gate)
			forge_m681_mark_aux(0xDB, prev);
		else
			forge_m681_mark_aux(0xDA, forge_disp_build_tag());
		pr_emerg("[FORGE_M681] display gate(NONRST2): prev=0x%08x tag=0x%04x -> %s (first: %s)\n",
			 prev, forge_disp_build_tag(),
			 forge_disp_gate ? "SKIP display stack" : "ATTEMPT",
			 who);
	} else if (forge_disp_gate) {
		pr_emerg("[FORGE_M681] display gate: %s skipped\n", who);
	}
	return forge_disp_gate;
}
EXPORT_SYMBOL(forge_display_gate_skip);

static void forge_disp_ok_fn(struct work_struct *work)
{
	void __iomem *w = forge_disp_wdt();

	if (w && forge_disp_gate == 0)
		writel(FORGE_DISP_DONE, w + FORGE_WDT_NONRST2_OFF);
	pr_emerg("[FORGE_M681] display gate(NONRST2): alive at %ds -> DONE\n",
		 FORGE_DISP_OK_UPTIME_S);
}
static DECLARE_DELAYED_WORK(forge_disp_ok_work, forge_disp_ok_fn);

/* a clean restart/halt/power-off (not panic, not a HW-WDT reset) proves the
 * kernel was alive: the attempted display did not wedge it */
static int forge_disp_reboot_nb_fn(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	void __iomem *w = forge_disp_wdt();

	if (w && forge_disp_gate == 0)
		writel(FORGE_DISP_DONE, w + FORGE_WDT_NONRST2_OFF);
	return NOTIFY_DONE;
}
static struct notifier_block forge_disp_reboot_nb = {
	.notifier_call = forge_disp_reboot_nb_fn,
};

static int forge_disp_gate_rearm_set(const char *val, const struct kernel_param *kp)
{
	void __iomem *w = forge_disp_wdt();

	if (w)
		writel(0, w + FORGE_WDT_NONRST2_OFF);
	pr_emerg("[FORGE_M681] display gate(NONRST2): re-armed (cleared) from userspace\n");
	return 0;
}
static const struct kernel_param_ops forge_disp_gate_rearm_ops = {
	.set = forge_disp_gate_rearm_set,
};
module_param_cb(disp_gate_rearm, &forge_disp_gate_rearm_ops, NULL, 0200);

static int __init forge_disp_mark_ok(void)
{
	void __iomem *w;
	long left;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC))
		return 0;

	w = forge_disp_wdt();
	if (forge_disp_gate == 0) {
		/* the display was attempted: DONE only once it has run a while */
		left = FORGE_DISP_OK_UPTIME_S * HZ - (long)(jiffies - INITIAL_JIFFIES);
		schedule_delayed_work(&forge_disp_ok_work, left > 0 ? left : 0);
		register_reboot_notifier(&forge_disp_reboot_nb);
	} else if (forge_disp_gate < 0 && !forge_skip_display && w) {
		/* no display stack in this image: old late-init DONE */
		writel(FORGE_DISP_DONE, w + FORGE_WDT_NONRST2_OFF);
	}
	pr_emerg("[FORGE_M681] display gate(NONRST2): late-init reached, gate=%d skip=%d\n",
		 forge_disp_gate, forge_skip_display);
	return 0;
}
late_initcall(forge_disp_mark_ok);
