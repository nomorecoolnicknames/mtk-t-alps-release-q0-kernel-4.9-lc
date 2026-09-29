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

#include <kpd.h>
#include <mt-plat/aee.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/mutex.h>
#include <mt-plat/upmu_common.h>
#include <mt-plat/mtk_boot_common.h>

/* m681 4.9: MT6351 TOPSTATUS, 0 = pressed; see forge_pmic_keys_fn() */
#define FORGE_TOPSTATUS		0x0220
#define FORGE_PWRKEY_DEB	BIT(1)
#define FORGE_HOMEKEY_DEB	BIT(2)
#define FORGE_PMIC_KEYS_POLL_MS	20

extern s32 pwrap_read(u32 adr, u32 *rdata);
#ifdef CONFIG_MTK_TC1_FM_AT_SUSPEND
#include <mt_soc_afe_control.h>
#endif

#define KPD_SAY		"kpd: "
#ifndef KPD_DEBUG
#define kpd_print(fmt, arg...)	do {} while (0)
#define kpd_info(fmt, arg...)	do {} while (0)
#endif

#ifdef CONFIG_KPD_PWRKEY_USE_EINT
static u8 kpd_pwrkey_state = !KPD_PWRKEY_POLARITY;
#endif

static int kpd_show_hw_keycode = 1;
#ifndef EVB_PLATFORM
static int kpd_enable_lprst = 1;
#endif
static u16 kpd_keymap_state[KPD_NUM_MEMS] = {
	0xffff, 0xffff, 0xffff, 0xffff, 0x00ff
};

static bool kpd_sb_enable;
extern void kpd_pwrkey_pmic_handler(unsigned long pressed);
extern unsigned int get_boot_mode(void);

#ifdef CONFIG_MTK_SMARTBOOK_SUPPORT
static void sb_kpd_release_keys(struct input_dev *dev)
{
	int code;

	for (code = 0; code <= KEY_MAX; code++) {
		if (test_bit(code, dev->keybit)) {
			kpd_print("report release event for sb plug in! keycode:%d\n", code);
			input_report_key(dev, code, 0);
			input_sync(dev);
		}
	}
}

void sb_kpd_enable(void)
{
	kpd_sb_enable = true;
	kpd_print("sb_kpd_enable performed!\n");
	mt_reg_sync_writew(0x0, KP_EN);
	sb_kpd_release_keys(kpd_input_dev);
}

void sb_kpd_disable(void)
{
	kpd_sb_enable = false;
	kpd_print("sb_kpd_disable performed!\n");
	mt_reg_sync_writew(0x1, KP_EN);
}
#else
void sb_kpd_enable(void)
{
	kpd_print("sb_kpd_enable empty function for HAL!\n");
}

void sb_kpd_disable(void)
{
	kpd_print("sb_kpd_disable empty function for HAL!\n");
}
#endif

#ifndef EVB_PLATFORM
static void enable_kpd(int enable)
{
	if (enable == 1) {
		mt_reg_sync_writew((u16) (enable), KP_EN);
		kpd_print("KEYPAD is enabled\n");
	} else if (enable == 0) {
		mt_reg_sync_writew((u16) (enable), KP_EN);
		kpd_print("KEYPAD is disabled\n");
	}
}
#endif

void kpd_slide_qwerty_init(void)
{
#if 0
	bool evdev_flag = false;
	bool power_op = false;
	struct input_handler *handler;
	struct input_handle *handle;

	handle = rcu_dereference(dev->grab);
	if (handle) {
		handler = handle->handler;
		if (strcmp(handler->name, "evdev") == 0)
			return -1;
	} else {
		list_for_each_entry_rcu(handle, &dev->h_list, d_node) {
			handler = handle->handler;
			if (strcmp(handler->name, "evdev") == 0) {
				evdev_flag = true;
				break;
			}
		}
		if (evdev_flag == false)
			return -1;
	}

	power_op = powerOn_slidePin_interface();
	if (!power_op)
		kpd_print(KPD_SAY "Qwerty slide pin interface power on fail\n");
	else
		kpd_print("Qwerty slide pin interface power on success\n");

	mt_eint_set_sens(KPD_SLIDE_EINT, KPD_SLIDE_SENSITIVE);
	mt_eint_set_hw_debounce(KPD_SLIDE_EINT, KPD_SLIDE_DEBOUNCE);
	mt_eint_registration(KPD_SLIDE_EINT, true, KPD_SLIDE_POLARITY, kpd_slide_eint_handler, false);

	power_op = powerOff_slidePin_interface();
	if (!power_op)
		kpd_print(KPD_SAY "Qwerty slide pin interface power off fail\n");
	else
		kpd_print("Qwerty slide pin interface power off success\n");
#endif
}

void kpd_get_keymap_state(u16 state[])
{
	state[0] = *(volatile u16 *)KP_MEM1;
	state[1] = *(volatile u16 *)KP_MEM2;
	state[2] = *(volatile u16 *)KP_MEM3;
	state[3] = *(volatile u16 *)KP_MEM4;
	state[4] = *(volatile u16 *)KP_MEM5;
	kpd_print(KPD_SAY "register = %x %x %x %x %x\n", state[0], state[1], state[2], state[3], state[4]);

}

static void kpd_factory_mode_handler(void)
{
	int i, j;
	bool pressed;
	u16 new_state[KPD_NUM_MEMS], change, mask;
	u16 hw_keycode, linux_keycode;

	for (i = 0; i < KPD_NUM_MEMS - 1; i++)
		kpd_keymap_state[i] = 0xffff;
	if (!kpd_dts_data.kpd_use_extend_type)
		kpd_keymap_state[KPD_NUM_MEMS - 1] = 0x00ff;
	else
		kpd_keymap_state[KPD_NUM_MEMS - 1] = 0xffff;

	kpd_get_keymap_state(new_state);

	for (i = 0; i < KPD_NUM_MEMS; i++) {
		change = new_state[i] ^ kpd_keymap_state[i];
		if (!change)
			continue;

		for (j = 0; j < 16; j++) {
			mask = 1U << j;
			if (!(change & mask))
				continue;

			hw_keycode = (i << 4) + j;
			/* bit is 1: not pressed, 0: pressed */
			pressed = !(new_state[i] & mask);
			if (kpd_show_hw_keycode) {
				kpd_print(KPD_SAY "(%s) factory_mode HW keycode = %u\n",
				       pressed ? "pressed" : "released", hw_keycode);
			}
			BUG_ON(hw_keycode >= KPD_NUM_KEYS);
			linux_keycode = kpd_dts_data.kpd_hw_init_map[hw_keycode];
			if (unlikely(linux_keycode == 0)) {
				kpd_print("Linux keycode = 0\n");
				continue;
			}
			input_report_key(kpd_input_dev, linux_keycode, pressed);
			input_sync(kpd_input_dev);
			kpd_print("factory_mode report Linux keycode = %u\n", linux_keycode);
		}
	}

	memcpy(kpd_keymap_state, new_state, sizeof(new_state));
	kpd_print("save new keymap state\n");
}

/********************************************************************/
void kpd_auto_test_for_factorymode(void)
{
	u32 ts = FORGE_PWRKEY_DEB | FORGE_HOMEKEY_DEB;

	kpd_print("Enter kpd_auto_test_for_factorymode!\n");

	mdelay(1000);

	kpd_factory_mode_handler();
	kpd_print("begin kpd_auto_test_for_factorymode!\n");
	/* m681 4.9: TOPSTATUS directly - not every line has the PMIC flag
	 * API, and an unreadable PMIC must read as "released" */
	pwrap_read(FORGE_TOPSTATUS, &ts);
	if (ts & FORGE_PWRKEY_DEB) {
		kpd_print("power key release\n");
		/*kpd_pwrkey_pmic_handler(1);*/
		/*mdelay(time);*/
		/*kpd_pwrkey_pmic_handler(0);}*/
	} else {
		kpd_print("power key press\n");
		kpd_pwrkey_pmic_handler(1);
		/*mdelay(time);*/
		/*kpd_pwrkey_pmic_handler(0);*/
	}

#ifdef KPD_PMIC_RSTKEY_MAP
	if (ts & FORGE_HOMEKEY_DEB) {
		/*kpd_print("home key release\n");*/
		/*kpd_pmic_rstkey_handler(1);*/
		/*mdelay(time);*/
		/*kpd_pmic_rstkey_handler(0);*/
	} else {
		kpd_print("home key press\n");
		kpd_pmic_rstkey_handler(1);
		/*mdelay(time);*/
		/*kpd_pmic_rstkey_handler(0);*/
	}
#endif
}

/********************************************************************/
void long_press_reboot_function_setting(void)
{
#ifndef EVB_PLATFORM
	if (kpd_enable_lprst && get_boot_mode() == NORMAL_BOOT) {
		kpd_info("Normal Boot long press reboot selection\n");
#ifdef CONFIG_KPD_PMIC_LPRST_TD
		kpd_info("Enable normal mode LPRST\n");
#ifdef CONFIG_ONEKEY_REBOOT_NORMAL_MODE
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_EN, 0x01);
		pmic_set_register_value(PMIC_RG_HOMEKEY_RST_EN, 0x00);
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_TD, CONFIG_KPD_PMIC_LPRST_TD);
#endif

#ifdef CONFIG_TWOKEY_REBOOT_NORMAL_MODE
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_EN, 0x01);
		pmic_set_register_value(PMIC_RG_HOMEKEY_RST_EN, 0x01);
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_TD, CONFIG_KPD_PMIC_LPRST_TD);
#endif
#else
		kpd_info("disable normal mode LPRST\n");
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_EN, 0x00);
		pmic_set_register_value(PMIC_RG_HOMEKEY_RST_EN, 0x00);

#endif
	} else {
		kpd_info("Other Boot Mode long press reboot selection\n");
#ifdef CONFIG_KPD_PMIC_LPRST_TD
		kpd_info("Enable other mode LPRST\n");
#ifdef CONFIG_ONEKEY_REBOOT_OTHER_MODE
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_EN, 0x01);
		pmic_set_register_value(PMIC_RG_HOMEKEY_RST_EN, 0x00);
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_TD, CONFIG_KPD_PMIC_LPRST_TD);
#endif

#ifdef CONFIG_TWOKEY_REBOOT_OTHER_MODE
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_EN, 0x01);
		pmic_set_register_value(PMIC_RG_HOMEKEY_RST_EN, 0x01);
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_TD, CONFIG_KPD_PMIC_LPRST_TD);
#endif
#else
		kpd_info("disable other mode LPRST\n");
		pmic_set_register_value(PMIC_RG_PWRKEY_RST_EN, 0x00);
		pmic_set_register_value(PMIC_RG_HOMEKEY_RST_EN, 0x00);
#endif
	}
#else
	pmic_set_register_value(PMIC_RG_PWRKEY_RST_EN, 0x00);
	pmic_set_register_value(PMIC_RG_HOMEKEY_RST_EN, 0x00);
#endif
}

/********************************************************************/
void kpd_wakeup_src_setting(int enable)
{
#ifndef EVB_PLATFORM
#ifdef CONFIG_MTK_TC1_FM_AT_SUSPEND
	int is_fm_radio_playing = 0;

	/* If FM is playing, keep keypad as wakeup source */
	if (ConditionEnterSuspend() == true)
		is_fm_radio_playing = 0;
	else
		is_fm_radio_playing = 1;

	if (is_fm_radio_playing == 0) {
		if (enable == 1) {
			kpd_print("enable kpd work!\n");
			enable_kpd(1);
		} else {
			kpd_print("disable kpd work!\n");
			enable_kpd(0);
		}
	}
#else
	if (enable == 1) {
		kpd_print("enable kpd work!\n");
		enable_kpd(1);
	} else {
		kpd_print("disable kpd work!\n");
		enable_kpd(0);
	}
#endif
#endif
}

/********************************************************************/
void kpd_init_keymap(u32 keymap[])
{
	int i = 0;

	if (kpd_dts_data.kpd_use_extend_type)
		kpd_keymap_state[4] = 0xffff;
	for (i = 0; i < KPD_NUM_KEYS; i++) {
		keymap[i] = kpd_dts_data.kpd_hw_init_map[i];
		/*kpd_print(KPD_SAY "keymap[%d] = %d\n", i,keymap[i]);*/
	}
}

void kpd_init_keymap_state(u16 keymap_state[])
{
	int i = 0;

	for (i = 0; i < KPD_NUM_MEMS; i++)
		keymap_state[i] = kpd_keymap_state[i];
	kpd_info("init_keymap_state done: %x %x %x %x %x!\n", keymap_state[0], keymap_state[1], keymap_state[2],
		 keymap_state[3], keymap_state[4]);
}

/********************************************************************/

void kpd_set_debounce(u16 val)
{
	mt_reg_sync_writew((u16) (val & KPD_DEBOUNCE_MASK), KP_DEBOUNCE);
}

/********************************************************************/
void kpd_pmic_rstkey_hal(unsigned long pressed)
{
	if (kpd_dts_data.kpd_sw_rstkey != 0) {
		if (!kpd_sb_enable) {
			input_report_key(kpd_input_dev, kpd_dts_data.kpd_sw_rstkey, pressed);
			input_sync(kpd_input_dev);
			if (kpd_show_hw_keycode) {
				kpd_print(KPD_SAY "(%s) HW keycode =%d using PMIC\n",
				       pressed ? "pressed" : "released", kpd_dts_data.kpd_sw_rstkey);
			}
		}
	}
}

void kpd_pmic_pwrkey_hal(unsigned long pressed)
{
#ifdef CONFIG_KPD_PWRKEY_USE_PMIC
	if (!kpd_sb_enable) {
		input_report_key(kpd_input_dev, kpd_dts_data.kpd_sw_pwrkey, pressed);
		input_sync(kpd_input_dev);
		if (kpd_show_hw_keycode) {
			kpd_print(KPD_SAY "(%s) HW keycode =%d using PMIC\n",
			       pressed ? "pressed" : "released", kpd_dts_data.kpd_sw_pwrkey);
		}
		/*ZH CHEN*/
		/*aee_powerkey_notify_press(pressed);*/
	}
#endif
}

/***********************************************************************/
void kpd_pwrkey_handler_hal(unsigned long data)
{
#ifdef CONFIG_KPD_PWRKEY_USE_EINT
	bool pressed;
	u8 old_state = kpd_pwrkey_state;

	kpd_pwrkey_state = !kpd_pwrkey_state;
	pressed = (kpd_pwrkey_state == !!KPD_PWRKEY_POLARITY);
	if (kpd_show_hw_keycode)
		kpd_print(KPD_SAY "(%s) HW keycode = using EINT\n", pressed ? "pressed" : "released");
	input_report_key(kpd_input_dev, kpd_dts_data.kpd_sw_pwrkey, pressed);
	kpd_print("report Linux keycode = %u\n", kpd_dts_data.kpd_sw_pwrkey);
	input_sync(kpd_input_dev);

	/* for detecting the return to old_state */
	mt_eint_set_polarity(KPD_PWRKEY_EINT, old_state);
	mt_eint_unmask(KPD_PWRKEY_EINT);
#endif
}

/***********************************************************************/
void mt_eint_register(void)
{
#ifdef CONFIG_KPD_PWRKEY_USE_EINT
	mt_eint_set_sens(KPD_PWRKEY_EINT, KPD_PWRKEY_SENSITIVE);
	mt_eint_set_hw_debounce(KPD_PWRKEY_EINT, KPD_PWRKEY_DEBOUNCE);
	mt_eint_registration(KPD_PWRKEY_EINT, true, KPD_PWRKEY_POLARITY, kpd_pwrkey_eint_handler, false);
#endif
}

/************************************************************************/

/*
 * m681 4.9: Power and Home without a PMIC driver.
 *
 * Both are MT6351 keys (TOPSTATUS 0x0220: PWRKEY_DEB bit 1, HOMEKEY_DEB
 * bit 2, 0 = pressed; kpd-sw-pwrkey 116, kpd-sw-rstkey 102 in the DT). On
 * 4.4 the PMIC interrupt handler fed them to kpd_pwrkey_pmic_handler() and
 * kpd_pmic_rstkey_handler(). 4.9 has read-only PMIC access and no PMIC
 * interrupt handling, so A13y had no key device at all.
 *
 * Two sources feed the same handlers through forge_pmic_keys_report():
 *  - a 20 ms poll of TOPSTATUS, always available where pwrap reads work;
 *  - the PMIC interrupt, EINT 150 from the "mediatek,mt6351-pmic" node, with
 *    the four key interrupts (INT_CON0/INT_STATUS0 bits 0-3: PWRKEY,
 *    HOMEKEY, PWRKEY_R, HOMEKEY_R) enabled through INT_CON0_SET and cleared
 *    W1C in INT_STATUS0 - the only PMIC interrupt writes the lead allowed.
 *    It is set up only if no other enabled PMIC source is already pending:
 *    the line is level-high and those are not ours to clear.
 *
 * A polled key cannot wake a suspended phone. So until a key interrupt has
 * actually arrived - the proof that the EINT path works on this board - the
 * poller keeps running and holds a wakeup source that keeps the kernel out
 * of suspend (hal_kpd.pmic_keys_block_suspend=0 lifts it for tests). Once
 * proven, the poller stops, the hold is released and the EINT is a wakeup
 * source. If the poller sees a key change and no interrupt follows within
 * 500 ms, or another source holds the line, the EINT is dropped again and
 * polling resumes.
 */
#define FORGE_INT_CON0		0x02C2	/* CON1..3 follow every 6 bytes */
#define FORGE_INT_CON0_SET	0x02C4
#define FORGE_INT_STATUS0	0x02E0	/* STATUS1..3 follow every 2 bytes */
#define FORGE_KEY_INTS		0x000F

extern s32 pwrap_write(u32 adr, u32 wdata);

static int forge_pmic_keys_block_suspend = 1;
module_param_named(pmic_keys_block_suspend, forge_pmic_keys_block_suspend,
		   int, 0644);
static int forge_pmic_keys_use_eint = 1;
module_param_named(pmic_keys_eint, forge_pmic_keys_use_eint, int, 0444);
static struct delayed_work forge_pmic_keys_work;
static struct wakeup_source *forge_pmic_keys_ws;
static DEFINE_MUTEX(forge_pmic_keys_lock);
static u32 forge_pmic_keys_last = FORGE_PWRKEY_DEB | FORGE_HOMEKEY_DEB;
static int forge_pmic_keys_irq;		/* > 0: EINT requested */
static bool forge_pmic_keys_eint_seen;	/* a key interrupt has arrived */
static unsigned long forge_pmic_keys_poll_change;	/* jiffies, 0 = none */

/* Feed a TOPSTATUS sample to kpd; true if a key changed. */
static bool forge_pmic_keys_report(u32 ts, const char *src)
{
	static unsigned int logged;
	u32 now = ts & (FORGE_PWRKEY_DEB | FORGE_HOMEKEY_DEB), changed;

	mutex_lock(&forge_pmic_keys_lock);
	changed = now ^ forge_pmic_keys_last;
	forge_pmic_keys_last = now;
#ifdef CONFIG_KPD_PWRKEY_USE_PMIC
	if (changed & FORGE_PWRKEY_DEB)
		kpd_pwrkey_pmic_handler(!(now & FORGE_PWRKEY_DEB));
#endif
	if (changed & FORGE_HOMEKEY_DEB)
		kpd_pmic_rstkey_handler(!(now & FORGE_HOMEKEY_DEB));
	mutex_unlock(&forge_pmic_keys_lock);
	if (changed && logged < 64) {
		logged++;
		pr_info(KPD_SAY "m681: PMIC keys (%s) TOPSTATUS 0x%04x: power %s, home %s\n",
			src, ts, now & FORGE_PWRKEY_DEB ? "up" : "down",
			now & FORGE_HOMEKEY_DEB ? "up" : "down");
	}
	return changed != 0;
}

/* Registers (bit n = INT_STATUSn) with an enabled source pending that is not
 * one of our four; ~0 if the PMIC cannot be read. */
static u32 forge_pmic_other_pending(void)
{
	u32 con, sta, pend = 0;
	int n;

	for (n = 0; n < 4; n++) {
		if (pwrap_read(FORGE_INT_CON0 + 6 * n, &con) ||
		    pwrap_read(FORGE_INT_STATUS0 + 2 * n, &sta))
			return ~0U;
		con &= sta;
		if (n == 0)
			con &= ~FORGE_KEY_INTS;
		if (con)
			pend |= 1U << n;
	}
	return pend;
}

static void forge_pmic_keys_drop_eint(const char *why)
{
	if (forge_pmic_keys_irq <= 0)
		return;
	disable_irq_nosync(forge_pmic_keys_irq);
	disable_irq_wake(forge_pmic_keys_irq);
	pr_notice(KPD_SAY "m681: PMIC key EINT dropped (%s), polling Power/Home again\n",
		  why);
	forge_pmic_keys_irq = -1;
	forge_pmic_keys_eint_seen = false;
	queue_delayed_work(system_freezable_wq, &forge_pmic_keys_work, 0);
}

static irqreturn_t forge_pmic_keys_isr(int irq, void *dev)
{
	u32 sta = 0, ts = 0;

	if (!pwrap_read(FORGE_INT_STATUS0, &sta) && (sta & FORGE_KEY_INTS))
		pwrap_write(FORGE_INT_STATUS0, sta & FORGE_KEY_INTS);	/* W1C */
	if (sta & FORGE_KEY_INTS) {
		__pm_wakeup_event(forge_pmic_keys_ws, 500);
		if (!forge_pmic_keys_eint_seen)
			pr_notice(KPD_SAY "m681: first PMIC key interrupt (INT_STATUS0 0x%04x) - EINT path proven\n",
				  sta);
		forge_pmic_keys_eint_seen = true;
		if (!pwrap_read(FORGE_TOPSTATUS, &ts))
			forge_pmic_keys_report(ts, "eint");
	}
	if (forge_pmic_other_pending())
		forge_pmic_keys_drop_eint("another PMIC source holds the line");
	return IRQ_HANDLED;
}

static void forge_pmic_keys_fn(struct work_struct *work)
{
	static bool unreadable_reported;
	u32 ts = 0;
	s32 ret;

	if (forge_pmic_keys_irq > 0 && forge_pmic_keys_eint_seen) {
		/* EINT proven: it delivers the keys and wakes the phone */
		if (forge_pmic_keys_ws->active)
			__pm_relax(forge_pmic_keys_ws);
		pr_notice(KPD_SAY "m681: Power/Home on PMIC EINT %d, poller stopped, suspend allowed\n",
			  forge_pmic_keys_irq);
		return;
	}
	if (forge_pmic_keys_block_suspend && !forge_pmic_keys_ws->active)
		__pm_stay_awake(forge_pmic_keys_ws);
	else if (!forge_pmic_keys_block_suspend && forge_pmic_keys_ws->active)
		__pm_relax(forge_pmic_keys_ws);

	ret = pwrap_read(FORGE_TOPSTATUS, &ts);
	if (ret) {
		/* no pwrap HAL on this line (base A13): retry slowly */
		if (!unreadable_reported)
			pr_notice(KPD_SAY "m681: PMIC keys unreadable (pwrap %d), Power/Home not reported\n",
				  ret);
		unreadable_reported = true;
		queue_delayed_work(system_freezable_wq, &forge_pmic_keys_work,
				   HZ);
		return;
	}
	if (forge_pmic_keys_report(ts, "poll") && forge_pmic_keys_irq > 0)
		forge_pmic_keys_poll_change = jiffies ? jiffies : 1;
	if (forge_pmic_keys_poll_change && forge_pmic_keys_irq > 0 &&
	    time_after(jiffies, forge_pmic_keys_poll_change +
				msecs_to_jiffies(500))) {
		forge_pmic_keys_poll_change = 0;
		if (!forge_pmic_keys_eint_seen)
			forge_pmic_keys_drop_eint("a key changed, no interrupt");
	}
	queue_delayed_work(system_freezable_wq, &forge_pmic_keys_work,
			   msecs_to_jiffies(FORGE_PMIC_KEYS_POLL_MS));
}

static void __init forge_pmic_keys_setup_eint(void)
{
	struct device_node *np;
	u32 pend;
	int irq, ret;

	np = of_find_compatible_node(NULL, NULL, "mediatek,mt6351-pmic");
	if (!np) {
		pr_notice(KPD_SAY "m681: no mt6351-pmic node, PMIC keys polled\n");
		return;
	}
	irq = irq_of_parse_and_map(np, 0);
	of_node_put(np);
	if (irq <= 0) {
		pr_notice(KPD_SAY "m681: PMIC EINT not mapped, PMIC keys polled\n");
		return;
	}
	pend = forge_pmic_other_pending();
	if (pend) {
		pr_notice(KPD_SAY "m681: other PMIC interrupt sources pending (0x%x), PMIC keys polled\n",
			  pend);
		return;
	}
	ret = pwrap_write(FORGE_INT_STATUS0, FORGE_KEY_INTS);	/* W1C stale */
	if (!ret)
		ret = pwrap_write(FORGE_INT_CON0_SET, FORGE_KEY_INTS);
	if (ret) {
		pr_notice(KPD_SAY "m681: PMIC key interrupt setup write failed (%d), PMIC keys polled\n",
			  ret);
		return;
	}
	forge_pmic_keys_irq = irq;
	ret = request_threaded_irq(irq, NULL, forge_pmic_keys_isr, IRQF_ONESHOT,
				   "forge_pmic_keys", NULL);
	if (ret) {
		forge_pmic_keys_irq = -1;
		pr_notice(KPD_SAY "m681: PMIC EINT %d request failed (%d), PMIC keys polled\n",
			  irq, ret);
		return;
	}
	enable_irq_wake(irq);
	pr_notice(KPD_SAY "m681: PMIC key interrupts on EINT irq %d, waiting for the first press to trust it\n",
		  irq);
}

static int __init forge_pmic_keys_init(void)
{
	if (!kpd_input_dev) {
		pr_notice(KPD_SAY "m681: no kpd input device, PMIC keys not polled\n");
		return 0;
	}
	forge_pmic_keys_ws = wakeup_source_register("forge_pmic_keys");
	if (!forge_pmic_keys_ws)
		return -ENOMEM;
	INIT_DELAYED_WORK(&forge_pmic_keys_work, forge_pmic_keys_fn);
	if (forge_pmic_keys_use_eint && !pwrap_read(FORGE_TOPSTATUS, &forge_pmic_keys_last))
		forge_pmic_keys_setup_eint();
	forge_pmic_keys_last = FORGE_PWRKEY_DEB | FORGE_HOMEKEY_DEB;
	queue_delayed_work(system_freezable_wq, &forge_pmic_keys_work, 0);
	pr_notice(KPD_SAY "m681: polling PMIC Power/Home every %d ms, suspend %s\n",
		  FORGE_PMIC_KEYS_POLL_MS,
		  forge_pmic_keys_block_suspend ? "blocked until the EINT is proven" : "allowed");
	return 0;
}
late_initcall(forge_pmic_keys_init);
