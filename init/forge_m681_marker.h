/* SPDX-License-Identifier: GPL-2.0 */
/*
 * forge_m681_marker.h - persistent DRAM stage markers for m681 (MT6755).
 *
 * m681 attempt3: ported from the vgdn FORCE saga (BRINGUP_STATE.md) via the
 * surviving sibling kernel-nokia-5.1-m681/init/forge_m681_marker.{c,h}.
 * The mechanism leaves breadcrumb words in a DRAM carveout that survives a
 * WDT reset so a single flash localises the early-boot hang stage.
 *
 * Channel: physical 0x46100000.  This address lies inside the DTB
 * reserved-memory node "@46000000" (reg <0 0x46000000 0 0x400000>) in
 * arch/arm64/boot/dts/mt6755.dtsi, so it is reserved from the kernel
 * allocator and is the vgdn-proven WDT-surviving location.  The clean MTK
 * ram_console DRAM region @0x44400000 is overwritten by the recovery boot,
 * and SPM 0x10006000 conflicts with the SPM driver ioremap, so neither is a
 * stable cross-reboot marker location (see POSTMORTEM_native_boot_attempt1.md).
 *
 * COLD-SAFE readback: 0x46100000 is DRAM, lost on a true cold/BROM recovery.
 * Each marker ALSO emits pr_emerg("[FORGE_M681] stage 0xNN") so that if the
 * kernel ever reaches printk + ram_console, the stage lands in the normal log
 * that MTK kedump flushes to the expdb partition (FLASH, survives cold). See
 * the readback recipe at the bottom of init/forge_m681_marker.c.
 *
 * After a WDT reset, read from TWRP (CONFIG_DEVMEM kernel) via:
 *   adb shell devmem 0x46100100      -> rolling stage (0xF681xx)
 *   adb shell devmem 0x46100104      -> last reached stage (raw u32)
 *   adb shell devmem 0x46100108      -> 0x46524745 ('FRGE') if our kernel ran
 *   adb shell dd if=/dev/mem of=/sdcard/dram.bin bs=4096 skip=287489 count=1
 */

#ifndef _FORGE_M681_MARKER_H
#define _FORGE_M681_MARKER_H

#include <linux/types.h>

/*
 * Stage numbers shared by start_kernel() (init/main.c) and setup_arch()
 * (arch/arm64/kernel/setup.c).  Kept unique across both so the recovered
 * stage byte identifies the last reached point without ambiguity.
 *
 *   01..18  -> start_kernel() body (init/main.c)
 *   1E..1F  -> forge_m681_marker.c bookkeeping
 *   32..46  -> setup_arch() body (arch/arm64/kernel/setup.c)
 *   47..69  -> early time_init / clocksource / CCF split (FORCE7..FORCE11)
 *   B0..B1  -> MTK power-management init bypass (FORCE16)
 *   CA      -> single-core cpu bring-up flag (FORCE10/14)
 *   F0..F1  -> MTK cpuidle governor bypass (FORCE14/15)
 */
#define FORGE_STAGE_START_KERNEL_ENTRY		0x01
#define FORGE_STAGE_POST_SETUP_ARCH		0x07
#define FORGE_STAGE_POST_MM_INIT		0x0C
#define FORGE_STAGE_POST_INIT_IRQ		0x0F
#define FORGE_STAGE_PRE_TIME_INIT		0x10
#define FORGE_STAGE_POST_TIME_INIT		0x11
#define FORGE_STAGE_POST_CONSOLE_INIT		0x12
#define FORGE_STAGE_ABOUT_TO_REST_INIT		0x18

#define FORGE_STAGE_MARKER_EARLY_INIT		0x1E	/* 30 */
#define FORGE_STAGE_MARKER_LATE_INIT		0x1F	/* 31 */

#define FORGE_STAGE_ARCH_SETUP_ENTRY		0x32	/* 50 A01 */
#define FORGE_STAGE_ARCH_CMDLINE_PARSED		0x33	/* 51 A02 */
#define FORGE_STAGE_ARCH_POST_MACHINE_FDT	0x34	/* 52 A03 */
#define FORGE_STAGE_ARCH_POST_EARLY_IOREMAP	0x35	/* 53 A04 */
#define FORGE_STAGE_ARCH_POST_PAGING_INIT	0x39	/* 57 A08 */
#define FORGE_STAGE_ARCH_POST_PSCI_INIT		0x3B	/* 59 A10 */
#define FORGE_STAGE_ARCH_SETUP_EXIT		0x3D	/* 61 A12 */
#define FORGE_STAGE_RAM_CONSOLE_INIT		0x46	/* 70 */

/* FORCE8: time_init() entry / of_clk_init() split (the BIGGEST vgdn wall). */
#define FORGE_STAGE_TIME_INIT_ENTRY		0x51	/* time_init() entered   */
#define FORGE_STAGE_TIME_POST_OF_CLK_INIT	0x52	/* of_clk_init returned  */
#define FORGE_STAGE_TIME_PRE_CLOCKSOURCE_OF_INIT	0x53
#define FORGE_STAGE_TIME_POST_CLOCKSOURCE_OF_INIT	0x54
#define FORGE_STAGE_TIME_INIT_EXIT		0x57

/* FORCE9/FORCE11: fixed-clock-only of_clk_init; skip full MTK CCF early. */
#define FORGE_STAGE_TIME_PRE_FIXED_CLK_INIT	0x67
#define FORGE_STAGE_TIME_POST_FIXED_CLK_INIT	0x68
#define FORGE_STAGE_TIME_SKIP_FULL_OF_CLK_INIT	0x69

/* FORCE8: APXGPT / mtk_timer clocksource bypass (use ARM arch timer). */
#define FORGE_STAGE_MTK_TIMER_INIT_ENTRY	0x58
#define FORGE_STAGE_MTK_TIMER_BYPASS		0x59

/* FORCE16: MTK power/SPM/freqhopping arch-initcall bypass. */
#define FORGE_STAGE_MTK_PM_INIT_ENTRY		0xB0
#define FORGE_STAGE_MTK_PM_INIT_BYPASS		0xB1

/* FORCE10/FORCE14: single-core bring-up flag (setup_max_cpus=1). */
#define FORGE_STAGE_KERNEL_FORCE_SINGLE_CPU	0xCA

/* attempt #6: fine marks across kernel_init_freeable() to pinpoint the late
 * hang now that the 0x44410000 readback channel works (last stage was 0xCA). */
#define FORGE_STAGE_POST_SMP_PREPARE		0xCB	/* after smp_prepare_cpus */
#define FORGE_STAGE_POST_PRE_SMP_INITCALLS	0xCC	/* after do_pre_smp_initcalls+lockup */
#define FORGE_STAGE_POST_SMP_INIT		0xCD	/* after smp_init */
#define FORGE_STAGE_POST_SCHED_INIT_SMP		0xCE	/* after sched_init_smp */
#define FORGE_STAGE_POST_BASIC_SETUP		0xCF	/* after do_basic_setup (all initcalls) */
#define FORGE_STAGE_PRE_INIT_EXEC		0xD0	/* about to exec /init (userspace) */

/* FORCE14/FORCE15: bypass MTK cpuidle governor / deep-idle framework. */
#define FORGE_STAGE_CPUIDLE_GOV_ENTRY		0xF0
#define FORGE_STAGE_CPUIDLE_GOV_BYPASS		0xF1

/* m681 v50: initcall-level phase markers (0xE8 enter / 0xE9 done).
 * aux = level number 0-7 (early/core/postcore/arch/subsys/fs/device/late).
 * Per-level completion counts in diag region 0xB0+level*4. */
#define FORGE_STAGE_INITCALL_LEVEL_ENTER	0xE8
#define FORGE_STAGE_INITCALL_LEVEL_DONE		0xE9

/*
 * Pointer to the ioremap()ed marker region; NULL until
 * forge_m681_marker_early_init() has run.  Exposed so other early code in this
 * tree may share the mapping if it ever needs to without re-ioremapping.
 */
extern void __iomem *forge_spm_base;
extern void __iomem *forge_spm_base2;	/* TWRP-readable mirror @0x44410000 */

/* qemu-virt guard (Rung C): 1 when the root DT compatible is "linux,dummy-virt"
 * (qemu -M virt).  When set, ALL forge MTK-hardware touching is skipped: the
 * raw SMC in forge_enable_cpuxgpt() (no EL3 on virt -> UNDEF), the DRAM marker
 * scribbles/ioremaps (0x444f0000/0x44800000 are unreserved guest RAM on virt
 * -> silent allocator corruption), and the wdt/gpt/mcusys/spm ioremaps
 * (0x1000xxxx sits inside virt's PCIe MMIO window).  Zero effect on the real
 * device: detection is a DT-root compatible check mt6755 can never match. */
extern int forge_virt;
/* Called from setup_arch() right after setup_machine_fdt() (the earliest point
 * where the flat DT is readable).  Sets forge_virt and NULLs the marker bases
 * so every later forge_m681_mark*() is a no-op under -M virt. */
void forge_m681_virt_detect(void);

void forge_m681_mark(u8 stage);
void forge_m681_mark_aux(u8 stage, u32 aux);
void forge_m681_bump(unsigned int slot);	/* m681 v87: eMMC checkpoint counters 0xE4.. */
void forge_m681_marker_early_init(void);
void forge_m681_marker_late_init(void);
void forge_m681_marker_early_console(void);	/* G1.2: console + panic net in setup_arch */
void forge_m681_headlog_reserve(void);	/* G1.3: head log window (after memblock init) */
void forge_m681_headlog_init(void);	/* G1.3: head log console (after paging_init) */
/* m681-49-disp: per-build NONRST2 display gate. Returns 1 when the display
 * stack must be skipped this boot (a previous boot of this image never reached
 * DONE). The first caller decides; @who names the caller in the log. */
int forge_display_gate_skip(const char *who);
void forge_m681_wdt_kick(void);	/* m681 v56: pet MTK WDT per-initcall */
void forge_m681_wdt_disarm(void); /* m681 v59: disable HW WDT before userspace */
/* m681 v34: non-overwritable "current initcall fn" tracker (diag 0x64..0x78).
 * Survives inner platform-probe marks (0xC0/0xCD) so the wedged initcall is
 * named even when it does platform probing. */
void forge_m681_set_initcall(u32 fn);
void forge_m681_set_initcall_done(u32 fn);
u32 forge_m681_get_initcall_seq(void);
/* m681 v35: name every of_platform node reaching device-create (diag 0x80..0xac). */
void forge_m681_mark_ofnode(const char *name);

/* m681 v50: initcall-level phase markers.  do_initcall_level() calls
 * mark_level_enter() before the level loop and mark_level_done() after.
 * Stage 0xE8 = level entered (aux=level 0-7), 0xE9 = level complete.
 * Per-level initcall completion counts are kept in diag 0xB0+level*4.
 * Level names: 0=early, 1=core, 2=postcore, 3=arch, 4=subsys, 5=fs,
 * 6=device, 7=late.  This lets a single SRAM marker capture identify
 * which initcall LEVEL the boot wall sits in, complementing the
 * per-initcall fn tracker (0x64/0x6c) that names the exact initcall. */
void forge_m681_mark_level_enter(int level);
void forge_m681_mark_level_done(int level);
/* m681 v51: direct SWRST_KEY write to toprgu+0x14 — belt-and-suspenders
 * with the v48 panic_notifier.  Used in the v51 tripwire (init/main.c)
 * so the reset fires even if the notifier chain is broken.  No-op if
 * neither toprgu_base nor forge_wdt_base is mapped. */
void forge_m681_wdt_swrst(void);

#endif /* _FORGE_M681_MARKER_H */
