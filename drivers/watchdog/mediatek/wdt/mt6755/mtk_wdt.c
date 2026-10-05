/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/watchdog.h>
#include <linux/platform_device.h>

#include <asm/uaccess.h>
#include <linux/types.h>
#include "mt_wdt.h"
#include <linux/delay.h>

#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#endif
#include <mt-plat/aee.h>
#include <mt-plat/sync_write.h>
#include <ext_wd_drv.h>

#include <mach/wd_api.h>
#ifdef CONFIG_HUAWEI_BFM
/* m681: this Huawei-BFMR (Qualcomm) header was carried unguarded from the 3.18
 * graft, where the chipset_common/ tree existed. It is only used under
 * CONFIG_HUAWEI_BFM (see the bfm usage further down); guard the include to match
 * so the MTK build (no chipset_common/) compiles. */
#include <chipset_common/bfmr/bfm/chipsets/qcom/bfm_qcom.h>
#endif
#ifdef CONFIG_OF
void __iomem *toprgu_base = 0;
int wdt_irq_id = 0;
int ext_debugkey_io = -1;

static const struct of_device_id rgu_of_match[] = {
	{.compatible = "mediatek,toprgu",},
	{},
};
#endif

/**---------------------------------------------------------------------
 * Sub feature switch region
 *----------------------------------------------------------------------
 */
#define NO_DEBUG 1

/*----------------------------------------------------------------------
 *   IRQ ID
 *--------------------------------------------------------------------*/
#ifdef CONFIG_OF
#define AP_RGU_WDT_IRQ_ID    wdt_irq_id
#else
#define AP_RGU_WDT_IRQ_ID    WDT_IRQ_BIT_ID
#endif

/*
 * internal variables
 */
/* static char expect_close; // Not use */
/* static spinlock_t rgu_reg_operation_spinlock = SPIN_LOCK_UNLOCKED; */
static DEFINE_SPINLOCK(rgu_reg_operation_spinlock);
#ifndef CONFIG_KICK_SPM_WDT
static unsigned int timeout;
#endif
static bool rgu_wdt_intr_has_trigger;	/* For test use */
static int g_last_time_time_out_value;
static int g_wdt_enable = 1;
#ifdef CONFIG_KICK_SPM_WDT
#include <mach/mt_spm.h>
static void spm_wdt_init(void);

#endif

#ifndef __USING_DUMMY_WDT_DRV__	/* FPGA will set this flag */
/*
    this function set the timeout value.
    value: second
*/
void mtk_wdt_set_time_out_value(unsigned int value)
{
	/*
	 * TimeOut = BitField 15:5
	 * Key     = BitField  4:0 = 0x08
	 */
	/* Rung C: no RGU under qemu -M virt — never deref a NULL base. */
	if (!toprgu_base)
		return;
	spin_lock(&rgu_reg_operation_spinlock);

#ifdef CONFIG_KICK_SPM_WDT
	spm_wdt_set_timeout(value);
#else

	/* 1 tick means 512 * T32K -> 1s = T32/512 tick = 64 */
	/* --> value * (1<<6) */
	timeout = (unsigned int)(value * (1 << 6));
	timeout = timeout << 5;
	mt_reg_sync_writel((timeout | MTK_WDT_LENGTH_KEY), MTK_WDT_LENGTH);
#endif
	spin_unlock(&rgu_reg_operation_spinlock);
}

/*
    watchdog mode:
    debug_en:   debug module reset enable.
    irq:        generate interrupt instead of reset
    ext_en:     output reset signal to outside
    ext_pol:    polarity of external reset signal
    wdt_en:     enable watch dog timer
*/
void mtk_wdt_mode_config(bool dual_mode_en, bool irq, bool ext_en, bool ext_pol, bool wdt_en)
{
#ifndef CONFIG_KICK_SPM_WDT
	unsigned int tmp;
#endif
	/* Rung C: no RGU under qemu -M virt — never deref a NULL base. */
	if (!toprgu_base)
		return;
	spin_lock(&rgu_reg_operation_spinlock);
#ifdef CONFIG_KICK_SPM_WDT
	if (wdt_en == TRUE) {
		pr_debug("wdt enable spm timer.....\n");
		spm_wdt_enable_timer();
	} else {
		pr_debug("wdt disable spm timer.....\n");
		spm_wdt_disable_timer();
	}
#else
	/* pr_debug(" mtk_wdt_mode_config  mode value=%x,pid=%d\n",DRV_Reg32(MTK_WDT_MODE),current->pid); */
	tmp = __raw_readl(MTK_WDT_MODE);
	tmp |= MTK_WDT_MODE_KEY;

	/* Bit 0 : Whether enable watchdog or not */
	if (wdt_en == TRUE)
		tmp |= MTK_WDT_MODE_ENABLE;
	else
		tmp &= ~MTK_WDT_MODE_ENABLE;

	/* Bit 1 : Configure extern reset signal polarity. */
	if (ext_pol == TRUE)
		tmp |= MTK_WDT_MODE_EXT_POL;
	else
		tmp &= ~MTK_WDT_MODE_EXT_POL;

	/* Bit 2 : Whether enable external reset signal */
	if (ext_en == TRUE)
		tmp |= MTK_WDT_MODE_EXTEN;
	else
		tmp &= ~MTK_WDT_MODE_EXTEN;

	/* Bit 3 : Whether generating interrupt instead of reset signal */
	if (irq == TRUE)
		tmp |= MTK_WDT_MODE_IRQ;
	else
		tmp &= ~MTK_WDT_MODE_IRQ;

	/* Bit 6 : Whether enable debug module reset */
	if (dual_mode_en == TRUE)
		tmp |= MTK_WDT_MODE_DUAL_MODE;
	else
		tmp &= ~MTK_WDT_MODE_DUAL_MODE;

	/* Bit 4: WDT_Auto_restart, this is a reserved bit, we use it as bypass powerkey flag. */
	/* Because HW reboot always need reboot to kernel, we set it always. */
	tmp |= MTK_WDT_MODE_AUTO_RESTART;

	mt_reg_sync_writel(tmp, MTK_WDT_MODE);
	/* dual_mode(1); //always dual mode */
	/* mdelay(100); */
	/* pr_debug(" mtk_wdt_mode_config  mode value=%x, tmp:%x,pid=%d\n", __raw_readl(MTK_WDT_MODE),
		 tmp, current->pid); */
#endif
	spin_unlock(&rgu_reg_operation_spinlock);
}

/* EXPORT_SYMBOL(mtk_wdt_mode_config); */

int mtk_wdt_enable(enum wk_wdt_en en)
{
	unsigned int tmp = 0;

	/* Rung C: no RGU under qemu -M virt — never deref a NULL base. */
	if (!toprgu_base)
		return -ENODEV;
	spin_lock(&rgu_reg_operation_spinlock);
#ifdef CONFIG_KICK_SPM_WDT
	if (WK_WDT_EN == en) {
		spm_wdt_enable_timer();
		pr_debug("wdt enable spm timer\n");

		tmp = __raw_readl(MTK_WDT_REQ_MODE);
		tmp |= MTK_WDT_REQ_MODE_KEY;
		tmp |= (MTK_WDT_REQ_MODE_SPM_SCPSYS);
		mt_reg_sync_writel(tmp, MTK_WDT_REQ_MODE);
		g_wdt_enable = 1;
	}
	if (WK_WDT_DIS == en) {
		spm_wdt_disable_timer();
		pr_debug("wdt disable spm timer\n ");
		tmp = __raw_readl(MTK_WDT_REQ_MODE);
		tmp |= MTK_WDT_REQ_MODE_KEY;
		tmp &= ~(MTK_WDT_REQ_MODE_SPM_SCPSYS);
		mt_reg_sync_writel(tmp, MTK_WDT_REQ_MODE);
		g_wdt_enable = 0;
	}
#else

	tmp = __raw_readl(MTK_WDT_MODE);

	tmp |= MTK_WDT_MODE_KEY;
	if (WK_WDT_EN == en) {
		tmp |= MTK_WDT_MODE_ENABLE;
		g_wdt_enable = 1;
	}
	if (WK_WDT_DIS == en) {
		tmp &= ~MTK_WDT_MODE_ENABLE;
		g_wdt_enable = 0;
	}
	pr_debug("mtk_wdt_enable value=%x,pid=%d\n", tmp, current->pid);
	mt_reg_sync_writel(tmp, MTK_WDT_MODE);
#endif
	spin_unlock(&rgu_reg_operation_spinlock);
	return 0;
}

int mtk_wdt_confirm_hwreboot(void)
{
	/* aee need confirm wd can hw reboot */
	/* pr_debug("mtk_wdt_probe : Initialize to dual mode\n"); */
	mtk_wdt_mode_config(TRUE, TRUE, TRUE, FALSE, TRUE);
	return 0;
}


void mtk_wdt_restart(enum wd_restart_type type)
{

#ifdef CONFIG_OF
	struct device_node *np_rgu;

	np_rgu = of_find_compatible_node(NULL, NULL, rgu_of_match[0].compatible);

	if (!toprgu_base) {
		toprgu_base = of_iomap(np_rgu, 0);
		if (!toprgu_base)
			pr_debug("RGU iomap failed\n");
		/* pr_debug("RGU base: 0x%p  RGU irq: %d\n", toprgu_base, wdt_irq_id); */
	}
	/* Rung C: same NULL-guard as wdt_arch_reset — no RGU under qemu -M virt. */
	if (!toprgu_base)
		return;
#endif

	/* pr_debug("WDT:[mtk_wdt_restart] type  =%d, pid=%d\n",type,current->pid); */

	/* m681 v141: dog-pet ON. The mcregistry-exec wall is fixed (v140) and the
	 * boot now PROGRESSES cleanly past post-fs-data to zygote-start with
	 * android_usb ready -> it needs >30s (zygote preload + system_server) to
	 * reach adbd/boot_completed, so the kicker-OFF 30s WDT was killing a healthy
	 * boot. Re-enable the kicker to give the progressing boot time to reach adb.
	 * [[feedback_never_disarm_wdt]] */
	if (type == WD_TYPE_NORMAL) {
		/* printk("WDT:ext restart\n" ); */
		spin_lock(&rgu_reg_operation_spinlock);
#ifdef CONFIG_KICK_SPM_WDT
		spm_wdt_restart_timer();
#else
		mt_reg_sync_writel(MTK_WDT_RESTART_KEY, MTK_WDT_RESTART);
#endif
		spin_unlock(&rgu_reg_operation_spinlock);
	} else if (type == WD_TYPE_NOLOCK) {
#ifdef CONFIG_KICK_SPM_WDT
		spm_wdt_restart_timer_nolock();
#else
		mt_reg_sync_writel(MTK_WDT_RESTART_KEY, MTK_WDT_RESTART);
#endif
	} else
		pr_debug("WDT:[mtk_wdt_restart] type=%d error pid =%d\n", type, current->pid);
}

void wdt_dump_reg(void)
{
	pr_alert("****************dump wdt reg start*************\n");
	pr_alert("MTK_WDT_MODE:0x%x\n", __raw_readl(MTK_WDT_MODE));
	pr_alert("MTK_WDT_LENGTH:0x%x\n", __raw_readl(MTK_WDT_LENGTH));
	pr_alert("MTK_WDT_RESTART:0x%x\n", __raw_readl(MTK_WDT_RESTART));
	pr_alert("MTK_WDT_STATUS:0x%x\n", __raw_readl(MTK_WDT_STATUS));
	pr_alert("MTK_WDT_INTERVAL:0x%x\n", __raw_readl(MTK_WDT_INTERVAL));
	pr_alert("MTK_WDT_SWRST:0x%x\n", __raw_readl(MTK_WDT_SWRST));
	pr_alert("MTK_WDT_NONRST_REG:0x%x\n", __raw_readl(MTK_WDT_NONRST_REG));
	pr_alert("MTK_WDT_NONRST_REG2:0x%x\n", __raw_readl(MTK_WDT_NONRST_REG2));
	pr_alert("MTK_WDT_REQ_MODE:0x%x\n", __raw_readl(MTK_WDT_REQ_MODE));
	pr_alert("MTK_WDT_REQ_IRQ_EN:0x%x\n", __raw_readl(MTK_WDT_REQ_IRQ_EN));
	pr_alert("MTK_WDT_DRAMC_CTL:0x%x\n", __raw_readl(MTK_WDT_DRAMC_CTL));
	pr_alert("****************dump wdt reg end*************\n");

}

void aee_wdt_dump_reg(void)
{
/*
	aee_wdt_printf("***dump wdt reg start***\n");
	aee_wdt_printf("MODE:0x%x\n", __raw_readl(MTK_WDT_MODE));
	aee_wdt_printf("LENGTH:0x%x\n", __raw_readl(MTK_WDT_LENGTH));
	aee_wdt_printf("RESTART:0x%x\n", __raw_readl(MTK_WDT_RESTART));
	aee_wdt_printf("STATUS:0x%x\n", __raw_readl(MTK_WDT_STATUS));
	aee_wdt_printf("INTERVAL:0x%x\n", __raw_readl(MTK_WDT_INTERVAL));
	aee_wdt_printf("SWRST:0x%x\n", __raw_readl(MTK_WDT_SWRST));
	aee_wdt_printf("NONRST_REG:0x%x\n", __raw_readl(MTK_WDT_NONRST_REG));
	aee_wdt_printf("NONRST_REG2:0x%x\n", __raw_readl(MTK_WDT_NONRST_REG2));
	aee_wdt_printf("REQ_MODE:0x%x\n", __raw_readl(MTK_WDT_REQ_MODE));
	aee_wdt_printf("REQ_IRQ_EN:0x%x\n", __raw_readl(MTK_WDT_REQ_IRQ_EN));
	aee_wdt_printf("DRAMC_CTL:0x%x\n", __raw_readl(MTK_WDT_DRAMC_CTL));
	aee_wdt_printf("***dump wdt reg end***\n");
*/
}

/* m681 v194: standard PSCI SYSTEM_RESET (fn id 0x84000009) issued as a raw
 * SMC. noinline so the call boundary saves caller-saved regs the ATF may
 * clobber (same trick mt_secure_call() relies on). The DT declares psci 0.1
 * so the kernel never wired arm_pm_restart=psci_sys_reset; but m681's ATF
 * honors custom SMCs (cpuxgpt 0x82000201 is proven), so the standard PSCI 0.2
 * reset is very likely implemented and gives a clean instant SoC reset. */
static noinline void forge_psci_system_reset(void)
{
#ifdef CONFIG_ARM64
	register u64 reg0 __asm__("x0") = 0x84000009UL;
	register u64 reg1 __asm__("x1") = 0;
	register u64 reg2 __asm__("x2") = 0;
	register u64 reg3 __asm__("x3") = 0;

	__asm__ __volatile__("smc #0\n" : "+r"(reg0)
		: "r"(reg1), "r"(reg2), "r"(reg3) : "memory");
#endif
}

void wdt_arch_reset(char mode)
{
	unsigned int wdt_mode_val;
#ifdef CONFIG_OF
	struct device_node *np_rgu;
#endif
	pr_debug("wdt_arch_reset called@Kernel mode =%c\n", mode);
#ifdef CONFIG_OF
	np_rgu = of_find_compatible_node(NULL, NULL, rgu_of_match[0].compatible);

	if (!toprgu_base) {
		toprgu_base = of_iomap(np_rgu, 0);
		if (!toprgu_base)
			pr_err("RGU iomap failed\n");
		pr_debug("RGU base: 0x%p  RGU irq: %d\n", toprgu_base, wdt_irq_id);
	}
	/* Rung C: no RGU node under qemu -M virt -> toprgu_base stays NULL and
	 * the writel below would NULL-deref INSIDE the panic path (virt boot #5
	 * crash-in-crash, FAR 0x8 right after "RGU iomap failed").  Bail out;
	 * the generic machine_restart path handles the reboot.  Strictly safer
	 * on the device too (never deref a failed iomap). */
	if (!toprgu_base)
		return;
#endif
	spin_lock(&rgu_reg_operation_spinlock);
	/* Watchdog Rest */
	mt_reg_sync_writel(MTK_WDT_RESTART_KEY, MTK_WDT_RESTART);
	wdt_mode_val = __raw_readl(MTK_WDT_MODE);
	pr_debug("wdt_arch_reset called MTK_WDT_MODE =%x\n", wdt_mode_val);
	/* clear autorestart bit: autoretart: 1, bypass power key, 0: not bypass power key */
	wdt_mode_val &= (~MTK_WDT_MODE_AUTO_RESTART);
	/* make sure WDT mode is hw reboot mode, can not config isr mode  */
	wdt_mode_val &= (~(MTK_WDT_MODE_IRQ | MTK_WDT_MODE_ENABLE | MTK_WDT_MODE_DUAL_MODE));
	if (mode)
		/* mode != 0 means by pass power key reboot, We using auto_restart bit as by pass power key flag */
		wdt_mode_val =
		    wdt_mode_val | (MTK_WDT_MODE_KEY | MTK_WDT_MODE_EXTEN |
				    MTK_WDT_MODE_AUTO_RESTART);
	else
		wdt_mode_val = wdt_mode_val | (MTK_WDT_MODE_KEY | MTK_WDT_MODE_EXTEN);

	mt_reg_sync_writel(wdt_mode_val, MTK_WDT_MODE);
	pr_debug("wdt_arch_reset called end MTK_WDT_MODE =%x\n", wdt_mode_val);
	udelay(100);
	mt_reg_sync_writel(MTK_WDT_SWRST_KEY, MTK_WDT_SWRST);
	pr_debug("wdt_arch_reset: SW_reset happen\n");

	/* m681 v186: the SWRST above is INEFFECTIVE on this m6-graft (the RGU
	 * SW-reset is not honored), so every reboot otherwise dead-hangs in the
	 * while(1) below with the dog DISABLED (ENABLE was just cleared) and IRQs
	 * off -> the SoC never resets and needs a battery pull (user-confirmed
	 * 2026-06-28). The HW-WDT *timeout* reset DOES work here (it guillotined
	 * boot at ~16s pre-kicker; the forge 5s hrtimer kicker pets this very
	 * block, proving the writes land). So arm a SHORT (~2s) HW-reboot-mode
	 * timeout and let the dog reset us. IRQs are already disabled
	 * (machine_restart -> local_irq_disable) so the forge kicker cannot
	 * re-pet during the spin. rtc_mark_*() already ran in arch_reset() so
	 * 'reboot recovery' still lands in TWRP. Rollback: delete this block.
	 * See the board-specific register comments. */
	/* m681 v193: v186 (short LENGTH + MODE without AUTO_RESTART) did NOT reset
	 * (backlight stayed lit). Use the PROVEN dog config that forge_m681_wdt_arm()
	 * arms at late_init and that historically guillotines the boot: keep the
	 * preloader's ~30s LENGTH UNTOUCHED (writing a short LENGTH likely broke v186),
	 * clear DUAL_MODE|IRQ, set KEY|ENABLE|EXTEN|AUTO_RESTART, then pet RESTART. With
	 * IRQs off (machine_restart) the kicker can't pet -> dog times out (~30s) and
	 * HW-resets. rtc_mark_*() already ran in arch_reset() -> 'reboot recovery'. */
	/* m681 v194: v186 (short LENGTH, NO AUTO_RESTART) and v193 (AUTO_RESTART +
	 * 30s LENGTH) both failed to reset -- v193's 30s dog is out-paced by the
	 * forge 5s hrtimer kicker (RESTART). Two-pronged fix:
	 *  (1) PRIMARY: PSCI SYSTEM_RESET SMC 0x84000009 (forge_psci_system_reset).
	 *      ATF honors custom SMCs here, so a clean instant reset is expected.
	 *  (2) FALLBACK: arm the dog with AUTO_RESTART *and* a SHORT ~2s LENGTH
	 *      (count=2*64=128 -> (128<<5)|KEY=0x1008). 2s < the 5s kicker period,
	 *      so the dog fires before the next pet even if a secondary CPU's
	 *      kicker survived smp_send_stop (SMP stop is unreliable on graft).
	 * Arm dog FIRST (insurance), then SMC, then spin. rtc_mark_*() already ran
	 * in arch_reset() so 'reboot recovery' still lands in TWRP.
	 * Rollback: restore the v193 block. See the board-specific register comments. */
	/* m681 #150 REBOOT-BOOTLOOP FIX: try PSCI SYSTEM_RESET FIRST, with NO dog
	 * armed.  The WDT is already DISABLED here (ENABLE was cleared above).  v194
	 * armed a 2s AUTO_RESTART dog BEFORE the SMC; on a clean PSCI reset that dog
	 * config survives the warm reset and re-fires during the ~9s preloader+LK of
	 * the NEXT boot (bootprof pl_t~4.4s + lk_t~4.5s >> 2s) -> the reboot BOOTLOOP
	 * (bootreason=wdt_by_pass_pwk is that RE-FIRE, not the initial reset).  With
	 * no dog armed a PSCI reset is clean and the next boot proceeds.  Only if the
	 * ATF DECLINES (SMC returns) do we arm the short dog as a last-resort reset. */
	pr_emerg("[FORGE_M681] #150 wdt_arch_reset: PSCI SYSTEM_RESET SMC 0x84000009 (no dog armed, WDT MODE=0x%x)\n",
		 __raw_readl(MTK_WDT_MODE));

	spin_unlock(&rgu_reg_operation_spinlock);

	/* PRIMARY: clean PSCI reset, dogless. Returns only if the ATF declines. */
	forge_psci_system_reset();

	/* FALLBACK (PSCI declined): now arm the short 2s AUTO_RESTART dog to force a
	 * reset. This path CAN bootloop (short dog persists) but only reaches here if
	 * PSCI is broken -- the lesser evil vs never resetting. */
	pr_emerg("[FORGE_M681] #150 PSCI SYSTEM_RESET returned (ATF declined) -> arming 2s dog fallback\n");
	spin_lock(&rgu_reg_operation_spinlock);
	{
		u32 m = __raw_readl(MTK_WDT_MODE);
		m &= ~(MTK_WDT_MODE_DUAL_MODE | MTK_WDT_MODE_IRQ);
		m |= MTK_WDT_MODE_KEY | MTK_WDT_MODE_ENABLE |
		     MTK_WDT_MODE_EXTEN | MTK_WDT_MODE_AUTO_RESTART;
		mt_reg_sync_writel(((2u * (1u << 6)) << 5) | MTK_WDT_LENGTH_KEY,
				   MTK_WDT_LENGTH);
		mt_reg_sync_writel(m, MTK_WDT_MODE);
		mt_reg_sync_writel(MTK_WDT_RESTART_KEY, MTK_WDT_RESTART);
	}
	spin_unlock(&rgu_reg_operation_spinlock);

	while (1) {
		wdt_dump_reg();
		pr_err("wdt_arch_reset error\n");
	}

}

int mtk_rgu_dram_reserved(int enable)
{
	unsigned int tmp;

	if (1 == enable) {
		/* enable ddr reserved mode */
		tmp = __raw_readl(MTK_WDT_MODE);
		tmp |= (MTK_WDT_MODE_DDR_RESERVE | MTK_WDT_MODE_KEY);
		mt_reg_sync_writel(tmp, MTK_WDT_MODE);
	} else if (0 == enable) {
		/* disable ddr reserved mode, set reset mode,
		   disable watchdog output reset signal */
		tmp = __raw_readl(MTK_WDT_MODE);
		tmp &= (~MTK_WDT_MODE_DDR_RESERVE);
		tmp |= MTK_WDT_MODE_KEY;
		mt_reg_sync_writel(tmp, MTK_WDT_MODE);
	}

	pr_debug("mtk_rgu_dram_reserved:MTK_WDT_MODE(0x%x)\n", __raw_readl(MTK_WDT_MODE));
	return 0;
}

int mtk_wdt_swsysret_config(int bit, int set_value)
{
	unsigned int wdt_sys_val;

	/* Rung C: no RGU under qemu -M virt (virt boot #10: NULL deref at
	 * mtk_wdt_swsysret_config+0x38 from init/ccci).  Same NULL-guard
	 * family as mtk_wdt_request_en_set. */
	if (!toprgu_base)
		return -ENODEV;

	spin_lock(&rgu_reg_operation_spinlock);
	wdt_sys_val = __raw_readl(MTK_WDT_SWSYSRST);
	pr_debug("fwq2 before set wdt_sys_val =%x\n", wdt_sys_val);
	wdt_sys_val |= MTK_WDT_SWSYS_RST_KEY;
	switch (bit) {
	case MTK_WDT_SWSYS_RST_MD_RST:
		if (1 == set_value)
			wdt_sys_val |= MTK_WDT_SWSYS_RST_MD_RST;
		if (0 == set_value)
			wdt_sys_val &= ~MTK_WDT_SWSYS_RST_MD_RST;
		break;
	case MTK_WDT_SWSYS_RST_MD_LITE_RST:
		if (1 == set_value)
			wdt_sys_val |= MTK_WDT_SWSYS_RST_MD_LITE_RST;
		if (0 == set_value)
			wdt_sys_val &= ~MTK_WDT_SWSYS_RST_MD_LITE_RST;
		break;
	}
	mt_reg_sync_writel(wdt_sys_val, MTK_WDT_SWSYSRST);
	spin_unlock(&rgu_reg_operation_spinlock);

	mdelay(10);
	pr_debug("after set wdt_sys_val =%x,wdt_sys_val=%x\n", __raw_readl(MTK_WDT_SWSYSRST),
		 wdt_sys_val);
	return 0;
}

int mtk_wdt_request_en_set(int mark_bit, WD_REQ_CTL en)
{
	int res = 0;
	unsigned int tmp, ext_req_con;
	struct device_node *np_rgu;

	if (!toprgu_base) {
		np_rgu = of_find_compatible_node(NULL, NULL, rgu_of_match[0].compatible);
		toprgu_base = of_iomap(np_rgu, 0);
		if (!toprgu_base)
			pr_err("RGU iomap failed\n");
		pr_debug("RGU base: 0x%p  RGU irq: %d\n", toprgu_base, wdt_irq_id);
	}
	/* Rung C: no RGU under qemu -M virt (virt boot #8: NULL+0x30 deref via
	 * mrdump_key_init -> debug_key_eint_config).  Never deref a failed iomap. */
	if (!toprgu_base)
		return -ENODEV;

	spin_lock(&rgu_reg_operation_spinlock);
	tmp = __raw_readl(MTK_WDT_REQ_MODE);
	tmp |= MTK_WDT_REQ_MODE_KEY;

	if (MTK_WDT_REQ_MODE_SPM_SCPSYS == mark_bit) {
		if (WD_REQ_EN == en)
			tmp |= (MTK_WDT_REQ_MODE_SPM_SCPSYS);
		if (WD_REQ_DIS == en)
			tmp &= ~(MTK_WDT_REQ_MODE_SPM_SCPSYS);
	} else if (MTK_WDT_REQ_MODE_SPM_THERMAL == mark_bit) {
		if (WD_REQ_EN == en)
			tmp |= (MTK_WDT_REQ_MODE_SPM_THERMAL);
		if (WD_REQ_DIS == en)
			tmp &= ~(MTK_WDT_REQ_MODE_SPM_THERMAL);
	} else if (MTK_WDT_REQ_MODE_EINT == mark_bit) {
		if (WD_REQ_EN == en) {
			if (ext_debugkey_io != -1) {
				ext_req_con = (ext_debugkey_io << 4) | 0x01;
				mt_reg_sync_writel(ext_req_con, MTK_WDT_EXT_REQ_CON);
				tmp |= (MTK_WDT_REQ_MODE_EINT);
			} else {
				tmp &= ~(MTK_WDT_REQ_MODE_EINT);
				res = -1;
			}
		}
		if (WD_REQ_DIS == en)
			tmp &= ~(MTK_WDT_REQ_MODE_EINT);
	} else if (MTK_WDT_REQ_MODE_SYSRST == mark_bit) {
		/*
		   if (WD_REQ_EN == en) {
		   DRV_WriteReg32(MTK_WDT_SYSDBG_DEG_EN1, MTK_WDT_SYSDBG_DEG_EN1_KEY);
		   DRV_WriteReg32(MTK_WDT_SYSDBG_DEG_EN2, MTK_WDT_SYSDBG_DEG_EN2_KEY);
		   tmp |= (MTK_WDT_REQ_MODE_SYSRST);
		   }
		   if (WD_REQ_DIS == en)
		   tmp &= ~(MTK_WDT_REQ_MODE_SYSRST);
		 */
	} else if (MTK_WDT_REQ_MODE_THERMAL == mark_bit) {
		if (WD_REQ_EN == en)
			tmp |= (MTK_WDT_REQ_MODE_THERMAL);
		if (WD_REQ_DIS == en)
			tmp &= ~(MTK_WDT_REQ_MODE_THERMAL);
	} else
		res = -1;

	mt_reg_sync_writel(tmp, MTK_WDT_REQ_MODE);
	spin_unlock(&rgu_reg_operation_spinlock);
	return res;
}

int mtk_wdt_request_mode_set(int mark_bit, WD_REQ_MODE mode)
{
	int res = 0;
	unsigned int tmp;
	struct device_node *np_rgu;

	if (!toprgu_base) {
		np_rgu = of_find_compatible_node(NULL, NULL, rgu_of_match[0].compatible);
		toprgu_base = of_iomap(np_rgu, 0);
		if (!toprgu_base)
			pr_err("RGU iomap failed\n");
		pr_debug("RGU base: 0x%p  RGU irq: %d\n", toprgu_base, wdt_irq_id);
	}
	/* Rung C: same NULL-guard as mtk_wdt_request_en_set (no RGU on virt). */
	if (!toprgu_base)
		return -ENODEV;

	spin_lock(&rgu_reg_operation_spinlock);
	tmp = __raw_readl(MTK_WDT_REQ_IRQ_EN);
	tmp |= MTK_WDT_REQ_IRQ_KEY;

	if (MTK_WDT_REQ_MODE_SPM_SCPSYS == mark_bit) {
		if (WD_REQ_IRQ_MODE == mode)
			tmp |= (MTK_WDT_REQ_IRQ_SPM_SCPSYS_EN);
		if (WD_REQ_RST_MODE == mode)
			tmp &= ~(MTK_WDT_REQ_IRQ_SPM_SCPSYS_EN);
	} else if (MTK_WDT_REQ_MODE_SPM_THERMAL == mark_bit) {
		if (WD_REQ_IRQ_MODE == mode)
			tmp |= (MTK_WDT_REQ_IRQ_SPM_THERMAL_EN);
		if (WD_REQ_RST_MODE == mode)
			tmp &= ~(MTK_WDT_REQ_IRQ_SPM_THERMAL_EN);
	} else if (MTK_WDT_REQ_MODE_EINT == mark_bit) {
		if (WD_REQ_IRQ_MODE == mode)
			tmp |= (MTK_WDT_REQ_IRQ_EINT_EN);
		if (WD_REQ_RST_MODE == mode)
			tmp &= ~(MTK_WDT_REQ_IRQ_EINT_EN);
	} else if (MTK_WDT_REQ_MODE_SYSRST == mark_bit) {
		/*
		   if (WD_REQ_IRQ_MODE == mode)
		   tmp |= (MTK_WDT_REQ_IRQ_SYSRST_EN);
		   if (WD_REQ_RST_MODE == mode)
		   tmp &= ~(MTK_WDT_REQ_IRQ_SYSRST_EN);
		 */
	} else if (MTK_WDT_REQ_MODE_THERMAL == mark_bit) {
		if (WD_REQ_IRQ_MODE == mode)
			tmp |= (MTK_WDT_REQ_IRQ_THERMAL_EN);
		if (WD_REQ_RST_MODE == mode)
			tmp &= ~(MTK_WDT_REQ_IRQ_THERMAL_EN);
	} else
		res = -1;
	mt_reg_sync_writel(tmp, MTK_WDT_REQ_IRQ_EN);
	spin_unlock(&rgu_reg_operation_spinlock);
	return res;
}

/*this API is for C2K only
* flag: 1 is to clear;0 is to set
* shift: which bit need to do set or clear
*/
void mtk_wdt_set_c2k_sysrst(unsigned int flag, unsigned int shift)
{
#ifdef CONFIG_OF
	struct device_node *np_rgu;
#endif
	unsigned int ret;
#ifdef CONFIG_OF
	np_rgu = of_find_compatible_node(NULL, NULL, rgu_of_match[0].compatible);

	if (!toprgu_base) {
		toprgu_base = of_iomap(np_rgu, 0);
		if (!toprgu_base)
			pr_err("mtk_wdt_set_c2k_sysrst RGU iomap failed\n");
		pr_debug("mtk_wdt_set_c2k_sysrst RGU base: 0x%p  RGU irq: %d\n", toprgu_base,
			 wdt_irq_id);
	}
	/* Rung C: same NULL-guard as mtk_wdt_request_en_set (no RGU on virt). */
	if (!toprgu_base)
		return;
#endif
	spin_lock(&rgu_reg_operation_spinlock);
	if (1 == flag) {
		ret = __raw_readl(MTK_WDT_SWSYSRST);
		ret &= (~(1 << shift));
		mt_reg_sync_writel((ret | MTK_WDT_SWSYS_RST_KEY), MTK_WDT_SWSYSRST);
	} else {		/* means set x bit */
		ret = __raw_readl(MTK_WDT_SWSYSRST);
		ret |= ((1 << shift));
		mt_reg_sync_writel((ret | MTK_WDT_SWSYS_RST_KEY), MTK_WDT_SWSYSRST);
	}
	spin_unlock(&rgu_reg_operation_spinlock);
}

#else
/* ------------------------------------------------------------------------------------------------- */
/* Dummy functions */
/* ------------------------------------------------------------------------------------------------- */
void mtk_wdt_set_time_out_value(unsigned int value)
{
}

static void mtk_wdt_set_reset_length(unsigned int value)
{
}

void mtk_wdt_mode_config(bool dual_mode_en, bool irq, bool ext_en, bool ext_pol, bool wdt_en)
{
}

int mtk_wdt_enable(enum wk_wdt_en en)
{
	return 0;
}

void mtk_wdt_restart(enum wd_restart_type type)
{
}

static void mtk_wdt_sw_trigger(void)
{
}

static unsigned char mtk_wdt_check_status(void)
{
	return 0;
}

void wdt_arch_reset(char mode)
{
}

int mtk_wdt_confirm_hwreboot(void)
{
	return 0;
}

void mtk_wd_suspend(void)
{
}

void mtk_wd_resume(void)
{
}

void mtk_wd_suspend_sodi(void)
{
}

void mtk_wd_resume_sodi(void)
{
}

void wdt_dump_reg(void)
{
}

int mtk_wdt_swsysret_config(int bit, int set_value)
{
	return 0;
}

int mtk_wdt_request_mode_set(int mark_bit, WD_REQ_MODE mode)
{
	return 0;
}

int mtk_wdt_request_en_set(int mark_bit, WD_REQ_CTL en)
{
	return 0;
}

void mtk_wdt_set_c2k_sysrst(unsigned int flag)
{
}

int mtk_rgu_dram_reserved(int enable)
{
	return 0;
}

#endif				/* #ifndef __USING_DUMMY_WDT_DRV__ */

#ifndef CONFIG_FIQ_GLUE
static void wdt_report_info(void)
{
	/* extern struct task_struct *wk_tsk; */
	struct task_struct *task;

	task = &init_task;
	pr_debug("Qwdt: -- watchdog time out\n");

	for_each_process(task) {
		if (task->state == 0) {
			pr_debug("PID: %d, name: %s\n backtrace:\n", task->pid, task->comm);
			show_stack(task, NULL);
			pr_debug("\n");
		}
	}

	pr_debug("backtrace of current task:\n");
	show_stack(NULL, NULL);
	pr_debug("Qwdt: -- watchdog time out\n");
}
#endif


#ifdef CONFIG_FIQ_GLUE
static void wdt_fiq(void *arg, void *regs, void *svc_sp)
{
	unsigned int wdt_mode_val;
	struct wd_api *wd_api = NULL;
#ifdef CONFIG_HUAWEI_BFM
	qcom_set_boot_fail_flag(KERNEL_AP_WDT);
#endif
	get_wd_api(&wd_api);
	wdt_mode_val = __raw_readl(MTK_WDT_STATUS);
	mt_reg_sync_writel(wdt_mode_val, MTK_WDT_NONRST_REG);
#ifdef	CONFIG_MTK_WD_KICKER
	aee_wdt_printf("\n kick=0x%08x,check=0x%08x,STA=%x\n", wd_api->wd_get_kick_bit(),
		       wd_api->wd_get_check_bit(), wdt_mode_val);
	aee_wdt_dump_reg();
#endif

	aee_wdt_fiq_info(arg, regs, svc_sp);
#if 0
	asm volatile("mov %0, %1\n\t"
		  "mov fp, %2\n\t"
		 : "=r" (sp)
		 : "r" (svc_sp), "r" (preg[11])
		 );
	*((unsigned int *)(0x00000000)); /* trigger exception */
#endif
}
#else				/* CONFIG_FIQ_GLUE */
static irqreturn_t mtk_wdt_isr(int irq, void *dev_id)
{
	pr_err("fwq mtk_wdt_isr\n");
#ifndef __USING_DUMMY_WDT_DRV__	/* FPGA will set this flag */
	/* mt65xx_irq_mask(AP_RGU_WDT_IRQ_ID); */
	rgu_wdt_intr_has_trigger = 1;
	wdt_report_info();
	BUG();

#endif
	return IRQ_HANDLED;
}
#endif				/* CONFIG_FIQ_GLUE */

/*
 * Device interface
 */
static int mtk_wdt_probe(struct platform_device *dev)
{
	int ret = 0;
	unsigned int interval_val;
	struct device_node *node;
	u32 ints[2] = { 0, 0 };

	/* m681 v46/v47: probe-entry marker.  v47 put "wdt" back in the
	 * platform.c denylist so this probe normally does NOT run — 0xE5
	 * firing in a post-reset marker means the deny regressed (driver
	 * name aliasing / casefold miss) and mtk_wdt_probe actually reached
	 * entry; if 0xE5+aux (below) is present but later 0xE6/0xE7 are
	 * NOT, probe wedged inside of_iomap/irq_of_parse_and_map/
	 * request_irq — Hypothesis A confirmed. */
	{ extern void forge_m681_mark(unsigned char); forge_m681_mark(0xE5); }
	pr_err("******** MTK WDT driver probe!! ********\n");
#ifdef CONFIG_OF
	if (!toprgu_base) {
		toprgu_base = of_iomap(dev->dev.of_node, 0);
		if (!toprgu_base) {
			pr_err("RGU iomap failed\n");
			return -ENODEV;
		}
	}
	/* m681 v46/v47: prove of_iomap succeeded — aux = toprgu_base low32.
	 * Combined with the entry mark above, distinguishes "probe ran but
	 * iomap failed" (return -ENODEV early, no 0xE5+aux) from "iomap OK
	 * and probe wedged downstream on irq_of_parse_and_map/request_irq"
	 * (0xE5+aux present, no 0xE6/0xE7). */
	{ extern void forge_m681_mark_aux(unsigned char, unsigned int);
	  forge_m681_mark_aux(0xE5, (unsigned int)(unsigned long)toprgu_base); }
	if (!wdt_irq_id) {
		wdt_irq_id = irq_of_parse_and_map(dev->dev.of_node, 0);
		if (!wdt_irq_id) {
			pr_err("RGU get IRQ ID failed\n");
			return -ENODEV;
		}
	}
	pr_debug("RGU base: 0x%p  RGU irq: %d\n", toprgu_base, wdt_irq_id);

#endif

	node = of_find_compatible_node(NULL, NULL, "mediatek, MRDUMP_EXT_RST-eint");
	if (node) {
		of_property_read_u32_array(node, "debounce", ints, ARRAY_SIZE(ints));
		ext_debugkey_io = ints[0];
	}
	pr_err("mtk_wdt_probe: ext_debugkey_io=%d\n", ext_debugkey_io);

#ifndef __USING_DUMMY_WDT_DRV__	/* FPGA will set this flag */

#ifndef CONFIG_FIQ_GLUE
	pr_debug("******** MTK WDT register irq ********\n");
	ret =
	    request_irq(AP_RGU_WDT_IRQ_ID, (irq_handler_t) mtk_wdt_isr, IRQF_TRIGGER_FALLING,
			"mtk_watchdog", NULL);
#else
	pr_debug("******** MTK WDT register fiq ********\n");
	ret = request_fiq(AP_RGU_WDT_IRQ_ID, wdt_fiq, IRQF_TRIGGER_FALLING, NULL);
#endif

	if (ret != 0) {
		pr_err("mtk_wdt_probe : failed to request irq (%d)\n", ret);
		return ret;
	}
	pr_debug("mtk_wdt_probe : Success to request irq\n");

	/* Set timeout vale and restart counter */
	g_last_time_time_out_value = 30;
	mtk_wdt_set_time_out_value(g_last_time_time_out_value);

	mtk_wdt_restart(WD_TYPE_NORMAL);

	/**
	 * Set the reset length: we will set a special magic key.
	 * For Power off and power on reset, the INTERVAL default value is 0x7FF.
	 * We set Interval[1:0] to different value to distinguish different stage.
	 * Enter pre-loader, we will set it to 0x0
	 * Enter u-boot, we will set it to 0x1
	 * Enter kernel, we will set it to 0x2
	 * And the default value is 0x3 which means reset from a power off and power on reset
	 */
#define POWER_OFF_ON_MAGIC	(0x3)
#define PRE_LOADER_MAGIC	(0x0)
#define U_BOOT_MAGIC		(0x1)
#define KERNEL_MAGIC		(0x2)
#define MAGIC_NUM_MASK		(0x3)


#ifdef CONFIG_MTK_WD_KICKER	/* Initialize to dual mode */
	pr_debug("mtk_wdt_probe : Initialize to dual mode\n");
	/* m681 v45 (defense-in-depth, retained in v47): override dual+IRQ →
	 * single-mode HW-reset.  v47 keeps "wdt" in the platform.c denylist so
	 * mtk_wdt_probe normally does NOT run; this mode_config survives only as
	 * a defensive measure in case the denylist substring match ever misses
	 * the mtk-wdt driver (alias rename etc).  If probe ever DID run, leaving
	 * the v43 stock dual+IRQ here would cause the same AXI-IRQ-never-
	 * delivered no-return failure v46 hit.  l681 M17-18 proven: toprgu WDT
	 * in DUAL mode cuts a progressing boot (stage-1 LENGTH=30s IRQ expires,
	 * independent stage-2 INTERVAL begins, RESTART_KEY only reloads stage-1).
	 * dual_mode=FALSE + irq=FALSE gives a single LENGTH counter: the per-
	 * initcall forge_m681_wdt_kick RESTART_KEY reliably pets it on a
	 * progressing boot, a wedged initcall (no kick for 30s) trips a HW reset
	 * independent of CPU state (AXI bus-hang cannot take IRQ).  The armed
	 * state is also snapshotted at 0xE6/0xE7 below for marker verification. */
	mtk_wdt_mode_config(FALSE, FALSE, TRUE, FALSE, TRUE);
#else				/* Initialize to disable wdt */
	pr_debug("mtk_wdt_probe : Initialize to disable wdt\n");
	mtk_wdt_mode_config(FALSE, FALSE, TRUE, FALSE, FALSE);
	g_wdt_enable = 0;
#endif


	/* Update interval register value and check reboot flag */
	interval_val = __raw_readl(MTK_WDT_INTERVAL);
	interval_val &= ~(MAGIC_NUM_MASK);
	interval_val |= (KERNEL_MAGIC);
	/* Write back INTERVAL REG */
	mt_reg_sync_writel(interval_val, MTK_WDT_INTERVAL);

	/* m681 v45/v47: snapshot the armed WDT state into the forge SRAM marker so
	 * a post-reset readback from recovery proves the mode/length we wrote.
	 * v47 normally denies this probe, so 0xE6/0xE7 firing in a capture means
	 * the v47 self-arm in forge_m681_wdt_arm() was OVERRIDDEN by mtk_wdt_probe
	 * actually running (deny regression) — the aux bytes then tell us which
	 * of self-arm vs probe-arm is the live state. */
	{
		extern void forge_m681_mark_aux(unsigned char, unsigned int);
		forge_m681_mark_aux(0xE6, __raw_readl(MTK_WDT_MODE));
		forge_m681_mark_aux(0xE7, __raw_readl(MTK_WDT_LENGTH));
		pr_emerg("[FORGE_M681] WDT armed single-mode hw-reset: MODE=0x%x LENGTH=0x%x STATUS=0x%x\n",
			 __raw_readl(MTK_WDT_MODE), __raw_readl(MTK_WDT_LENGTH),
			 __raw_readl(MTK_WDT_STATUS));
	}

	/* m681 bring-up: stub bypass to prevent hang at request_en/mode_set
	 * (l681 M3: mtk_wdt_request_mode_set(EINT, IRQ_MODE) touches gated EINT
	 * registers → AXI bus-hang. WDT is already armed above; the EINT debug-key
	 * wiring is not needed for adb/logcat). */
	return 0;

	/* Reset External debug key */
	mtk_wdt_request_en_set(MTK_WDT_REQ_MODE_SYSRST, WD_REQ_DIS);
	mtk_wdt_request_en_set(MTK_WDT_REQ_MODE_EINT, WD_REQ_DIS);
	mtk_wdt_request_mode_set(MTK_WDT_REQ_MODE_SYSRST, WD_REQ_IRQ_MODE);
	mtk_wdt_request_mode_set(MTK_WDT_REQ_MODE_EINT, WD_REQ_IRQ_MODE);
#endif
	udelay(100);
	pr_debug("mtk_wdt_probe : done WDT_MODE(%x),MTK_WDT_NONRST_REG(%x)\n",
		 __raw_readl(MTK_WDT_MODE), __raw_readl(MTK_WDT_NONRST_REG));
	pr_debug("mtk_wdt_probe : done MTK_WDT_REQ_MODE(%x)\n", __raw_readl(MTK_WDT_REQ_MODE));
	pr_debug("mtk_wdt_probe : done MTK_WDT_REQ_IRQ_EN(%x)\n", __raw_readl(MTK_WDT_REQ_IRQ_EN));

	return ret;
}

static int mtk_wdt_remove(struct platform_device *dev)
{
	pr_debug("******** MTK wdt driver remove!! ********\n");

#ifndef __USING_DUMMY_WDT_DRV__	/* FPGA will set this flag */
	free_irq(AP_RGU_WDT_IRQ_ID, NULL);
#endif
	return 0;
}

static void mtk_wdt_shutdown(struct platform_device *dev)
{
	pr_debug("******** MTK WDT driver shutdown!! ********\n");

	/* mtk_wdt_ModeSelection(KAL_FALSE, KAL_FALSE, KAL_FALSE); */
	/* kick external wdt */
	/* mtk_wdt_mode_config(TRUE, FALSE, FALSE, FALSE, FALSE); */

	mtk_wdt_restart(WD_TYPE_NORMAL);
	pr_debug("******** MTK WDT driver shutdown done ********\n");
}

void mtk_wd_suspend(void)
{
	/* mtk_wdt_ModeSelection(KAL_FALSE, KAL_FALSE, KAL_FALSE); */
	/* en debug, dis irq, dis ext, low pol, dis wdt */
	mtk_wdt_mode_config(TRUE, TRUE, TRUE, FALSE, FALSE);

	mtk_wdt_restart(WD_TYPE_NORMAL);

	/*aee_sram_printk("[WDT] suspend\n"); */
	pr_debug("[WDT] suspend\n");
}

void mtk_wd_resume(void)
{

	if (g_wdt_enable == 1) {
		mtk_wdt_set_time_out_value(g_last_time_time_out_value);
		mtk_wdt_mode_config(TRUE, TRUE, TRUE, FALSE, TRUE);
		mtk_wdt_restart(WD_TYPE_NORMAL);
	}

	/*aee_sram_printk("[WDT] resume(%d)\n", g_wdt_enable); */
	pr_debug("[WDT] resume(%d)\n", g_wdt_enable);
}

void mtk_wd_suspend_sodi(void)
{
	/* mtk_wdt_ModeSelection(KAL_FALSE, KAL_FALSE, KAL_FALSE); */
	/* en debug, dis irq, dis ext, low pol, dis wdt */
	mtk_wdt_mode_config(TRUE, TRUE, TRUE, FALSE, FALSE);
	mtk_wdt_restart(WD_TYPE_NORMAL);
}

void mtk_wd_resume_sodi(void)
{
	if (g_wdt_enable == 1) {
		mtk_wdt_set_time_out_value(g_last_time_time_out_value);
		mtk_wdt_mode_config(TRUE, TRUE, TRUE, FALSE, TRUE);
		mtk_wdt_restart(WD_TYPE_NORMAL);
	}
}

static struct platform_driver mtk_wdt_driver = {

	.driver = {
		   .name = "mtk-wdt",
#ifdef CONFIG_OF
		   .of_match_table = rgu_of_match,
#endif
		   },
	.probe = mtk_wdt_probe,
	.remove = mtk_wdt_remove,
	.shutdown = mtk_wdt_shutdown,
/* .suspend	= mtk_wdt_suspend, */
/* .resume	= mtk_wdt_resume, */
};

#ifndef CONFIG_OF
struct platform_device mtk_device_wdt = {
	.name = "mtk-wdt",
	.id = 0,
	.dev = {
		}
};
#endif

#ifdef CONFIG_KICK_SPM_WDT
static void spm_wdt_init(void)
{
	unsigned int tmp;
	/* set scpsys reset mode , not trigger irq */
	/* #ifndef CONFIG_ARM64 */
	/*6795 Macro */
	tmp = __raw_readl(MTK_WDT_REQ_MODE);
	tmp |= MTK_WDT_REQ_MODE_KEY;
	tmp |= (MTK_WDT_REQ_MODE_SPM_SCPSYS);
	mt_reg_sync_writel(tmp, MTK_WDT_REQ_MODE);

	tmp = __raw_readl(MTK_WDT_REQ_IRQ_EN);
	tmp |= MTK_WDT_REQ_IRQ_KEY;
	tmp &= ~(MTK_WDT_REQ_IRQ_SPM_SCPSYS_EN);
	mt_reg_sync_writel(tmp, MTK_WDT_REQ_IRQ_EN);
	/* #endif */

	pr_debug("mtk_wdt_init [MTK_WDT] not use RGU WDT use_SPM_WDT!! ********\n");
	/* pr_alert("WDT REQ_MODE=0x%x,  WDT REQ_EN=0x%x\n",
	   __raw_readl(MTK_WDT_REQ_MODE), __raw_readl(MTK_WDT_REQ_IRQ_EN)); */

	tmp = __raw_readl(MTK_WDT_MODE);
	tmp |= MTK_WDT_MODE_KEY;
	/* disable wdt */
	tmp &= (~(MTK_WDT_MODE_IRQ | MTK_WDT_MODE_ENABLE | MTK_WDT_MODE_DUAL_MODE));

	/* Bit 4: WDT_Auto_restart, this is a reserved bit, we use it as bypass powerkey flag. */
	/* Because HW reboot always need reboot to kernel, we set it always. */
	tmp |= MTK_WDT_MODE_AUTO_RESTART;
	/* BIt2  ext signal */
	tmp |= MTK_WDT_MODE_EXTEN;
	mt_reg_sync_writel(tmp, MTK_WDT_MODE);

}
#endif


/*
 * init and exit function
 */
static int __init mtk_wdt_init(void)
{

	int ret;

#ifndef CONFIG_OF
	ret = platform_device_register(&mtk_device_wdt);
	if (ret) {
		pr_err("****[mtk_wdt_driver] Unable to device register(%d)\n", ret);
		return ret;
	}
#endif
	ret = platform_driver_register(&mtk_wdt_driver);
	if (ret) {
		pr_err("****[mtk_wdt_driver] Unable to register driver (%d)\n", ret);
		return ret;
	}
	pr_alert("mtk_wdt_init ok\n");
	return 0;
}

static void __exit mtk_wdt_exit(void)
{
}

/*this function is for those user who need WDT APIs before WDT driver's probe*/
static int __init mtk_wdt_get_base_addr(void)
{
#ifdef CONFIG_OF
	struct device_node *np_rgu;

	np_rgu = of_find_compatible_node(NULL, NULL, rgu_of_match[0].compatible);

	if (!toprgu_base) {
		toprgu_base = of_iomap(np_rgu, 0);
		if (!toprgu_base)
			pr_err("RGU iomap failed\n");

		pr_debug("RGU base: 0x%p\n", toprgu_base);
	}
#endif
	return 0;
}
core_initcall(mtk_wdt_get_base_addr);
postcore_initcall(mtk_wdt_init);
module_exit(mtk_wdt_exit);

MODULE_AUTHOR("MTK");
MODULE_DESCRIPTION("Watchdog Device Driver");
MODULE_LICENSE("GPL");
