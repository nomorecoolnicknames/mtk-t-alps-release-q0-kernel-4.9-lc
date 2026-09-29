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
*
* You should have received a copy of the GNU General Public License
* along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

/*******************************************************************************
 *
 * Filename:
 * ---------
 *   AudDrv_Gpio.c
 *
 * Project:
 * --------
 *   MT6797  Audio Driver GPIO
 *
 * Description:
 * ------------
 *   Audio register
 *
 * Author:
 * -------
 * George
 *
 *------------------------------------------------------------------------------
 *
 *
 *******************************************************************************/


/*****************************************************************************
 *                     C O M P I L E R   F L A G S
 *****************************************************************************/


/*****************************************************************************
 *                E X T E R N A L   R E F E R E N C E S
 *****************************************************************************/
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
/* m681 raw EXTAMP pin driving (spec-timed AW8736 pulses) */
#include <mt-plat/mtk_gpio.h>
#include "mtk-auddrv-gpio.h"


#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/delay.h>

#if 1
struct pinctrl *pinctrlaud;

#define MT6755_PIN 1

enum audio_system_gpio_type {
	GPIO_AUD_CLK_MOSI_OFF,
	GPIO_AUD_CLK_MOSI_ON,
	GPIO_AUD_DAT_MISO_OFF,
	GPIO_AUD_DAT_MISO_ON,
	GPIO_AUD_DAT_MOSI_OFF,
	GPIO_AUD_DAT_MOSI_ON,
	GPIO_AUD_DAT_MISO2_OFF,
	GPIO_AUD_DAT_MISO2_ON,
	GPIO_AUD_DAT_MOSI2_OFF,
	GPIO_AUD_DAT_MOSI2_ON,
	GPIO_VOW_DAT_MISO_OFF,
	GPIO_VOW_DAT_MISO_ON,
	GPIO_VOW_CLK_MISO_OFF,
	GPIO_VOW_CLK_MISO_ON,
	GPIO_SMARTPA_RESET,
	GPIO_SMARTPA_ON,
	GPIO_TDM_MODE0,
	GPIO_TDM_MODE1,
#if MT6755_PIN
	/* GPIO_DEFAULT, */
	GPIO_PMIC_MODE0,
	GPIO_PMIC_MODE1,
	GPIO_I2S_MODE0,
	GPIO_I2S_MODE1,
	GPIO_EXTAMP_HIGH,
	GPIO_EXTAMP_LOW,
	GPIO_EXTAMP2_HIGH,
	GPIO_EXTAMP2_LOW,
	GPIO_RCVSPK_HIGH,
	GPIO_RCVSPK_LOW,
#endif
	GPIO_HPDEPOP_HIGH,
	GPIO_HPDEPOP_LOW,
	GPIO_AUD_CLK_MOSI_HIGH,
	GPIO_AUD_CLK_MOSI_LOW,
	GPIO_NUM
};

struct audio_gpio_attr {
	const char *name;
	bool gpio_prepare;
	struct pinctrl_state *gpioctrl;
};

static struct audio_gpio_attr aud_gpios[GPIO_NUM] = {
	[GPIO_AUD_CLK_MOSI_OFF] = {"aud_clk_mosi_off", false, NULL},
	[GPIO_AUD_CLK_MOSI_ON] = {"aud_clk_mosi_on", false, NULL},
	[GPIO_AUD_DAT_MISO_OFF] = {"aud_dat_miso_off", false, NULL},
	[GPIO_AUD_DAT_MISO_ON] = {"aud_dat_miso_on", false, NULL},
	[GPIO_AUD_DAT_MOSI_OFF] = {"aud_dat_mosi_off", false, NULL},
	[GPIO_AUD_DAT_MOSI_ON] = {"aud_dat_mosi_on", false, NULL},
	[GPIO_AUD_DAT_MISO2_OFF] = {"aud_dat_miso2_off", false, NULL},
	[GPIO_AUD_DAT_MISO2_ON] = {"aud_dat_miso2_on", false, NULL},
	[GPIO_AUD_DAT_MOSI2_OFF] = {"aud_dat_mosi2_off", false, NULL},
	[GPIO_AUD_DAT_MOSI2_ON] = {"aud_dat_mosi2_on", false, NULL},
	[GPIO_VOW_DAT_MISO_OFF] = {"vow_dat_miso_off", false, NULL},
	[GPIO_VOW_DAT_MISO_ON] = {"vow_dat_miso_on", false, NULL},
	[GPIO_VOW_CLK_MISO_OFF] = {"vow_clk_miso_off", false, NULL},
	[GPIO_VOW_CLK_MISO_ON] = {"vow_clk_miso_on", false, NULL},

	[GPIO_SMARTPA_RESET] = {"aud_smartpa_reset", false, NULL},
	[GPIO_SMARTPA_ON] = {"aud_smartpa_on", false, NULL},
	[GPIO_TDM_MODE0] = {"aud_tdm_mode0", false, NULL},
	[GPIO_TDM_MODE1] = {"aud_tdm_mode1", false, NULL},

#if MT6755_PIN
	/* [GPIO_DEFAULT] = {"default", false, NULL}, */
	[GPIO_PMIC_MODE0] = {"audpmicclk-mode0", false, NULL},
	[GPIO_PMIC_MODE1] = {"audpmicclk-mode1", false, NULL},
	[GPIO_I2S_MODE0] = {"audi2s1-mode0", false, NULL},
	[GPIO_I2S_MODE1] = {"audi2s1-mode1", false, NULL},
	[GPIO_EXTAMP_HIGH] = {"extamp-pullhigh", false, NULL},
	[GPIO_EXTAMP_LOW] = {"extamp-pulllow", false, NULL},
	[GPIO_EXTAMP2_HIGH] = {"extamp2-pullhigh", false, NULL},
	[GPIO_EXTAMP2_LOW] = {"extamp2-pulllow", false, NULL},
	[GPIO_RCVSPK_HIGH] = {"rcvspk-pullhigh", false, NULL},
	[GPIO_RCVSPK_LOW] = {"rcvspk-pulllow", false, NULL},
#endif

	[GPIO_HPDEPOP_HIGH] = {"hpdepop-pullhigh", false, NULL},
	[GPIO_HPDEPOP_LOW] = {"hpdepop-pulllow", false, NULL},
	[GPIO_AUD_CLK_MOSI_HIGH] = {"aud_clk_mosi_pull_high", false, NULL},
	[GPIO_AUD_CLK_MOSI_LOW] = {"aud_clk_mosi_pull_low", false, NULL},
};
#endif

static unsigned int extbuck_fan53526_exist;

static DEFINE_MUTEX(gpio_request_mutex);

void AudDrv_GPIO_probe(void *dev)
{
#if 1
	int ret;
	int i = 0;

	pr_warn("%s\n", __func__);

	pinctrlaud = devm_pinctrl_get(dev);
	if (IS_ERR(pinctrlaud)) {
		ret = PTR_ERR(pinctrlaud);
		pr_err("Cannot find pinctrlaud!\n");
		return;
	}

	/* update hpdepop gpio by PCB version - extbuck fan53526 use gpio111 which may be used by hpdepop */
	pr_warn("%s(), extbuck_fan53526_exist = %d\n", __func__, extbuck_fan53526_exist);
	if (extbuck_fan53526_exist) { /* is e2 */
		struct audio_gpio_attr gpio_hpdepop_high = {"hpdepop-pullhigh_e2", false, NULL};
		struct audio_gpio_attr gpio_hpdepop_low = {"hpdepop-pulllow_e2", false, NULL};

		aud_gpios[GPIO_HPDEPOP_HIGH] = gpio_hpdepop_high;
		aud_gpios[GPIO_HPDEPOP_LOW] = gpio_hpdepop_low;

		pr_warn("%s(), e2 PCB, update gpio name, high = %s, low = %s\n",
			__func__,
			aud_gpios[GPIO_HPDEPOP_HIGH].name,
			aud_gpios[GPIO_HPDEPOP_LOW].name);
	}

	for (i = 0; i < ARRAY_SIZE(aud_gpios); i++) {
		aud_gpios[i].gpioctrl = pinctrl_lookup_state(pinctrlaud, aud_gpios[i].name);
		if (IS_ERR(aud_gpios[i].gpioctrl)) {
			ret = PTR_ERR(aud_gpios[i].gpioctrl);
			pr_err("%s pinctrl_lookup_state %s fail %d\n", __func__, aud_gpios[i].name,
			       ret);
		} else {
			aud_gpios[i].gpio_prepare = true;
		}
	}
	//[yuwenjiao] add start for smartpa I2S GPIO.
	pr_err("AudDrv_GPIO_SMARTPA_Select!\n");
	AudDrv_GPIO_SMARTPA_Select(true);
	//[yuwenjiao] add end

	/* m681 (2026-07-18, audio lane): stock DCT boot-init muxed the
	 * AP<->MT6351 MTKIF audio pins GPIO149/150/151 to MODE_01
	 * (AUD_CLK_MOSI / AUD_DAT_MOSI / AUD_DAT_MISO,
	 * cust_gpio_boot.h:1261-1277); our tree has no DCT init and the
	 * integrator's pinmux-pins read suggests the dts "default" state may
	 * not be claiming them (silence root candidate: codec analog verified
	 * powered, but the digital feed never leaves the SoC). Force the mux
	 * here — idempotent if the dts state already applied — and log
	 * pre/post HW modes so ONE boot settles muxed-vs-unmuxed as FACT. */
	{
		static const unsigned int mtkif[3] = { 149, 150, 151 };
		unsigned int p, pre[3], post[3];

		for (p = 0; p < 3; p++) {
			pre[p] = mt_get_gpio_mode(mtkif[p] | 0x80000000);
			mt_set_gpio_mode(mtkif[p] | 0x80000000, GPIO_MODE_01);
			post[p] = mt_get_gpio_mode(mtkif[p] | 0x80000000);
		}
		pr_emerg("[FORGE_AUDIO] MTKIF mux 149/150/151 pre=%u/%u/%u post=%u/%u/%u (want 1/1/1)\n",
			 pre[0], pre[1], pre[2], post[0], post[1], post[2]);
	}
#endif
}

static int AudDrv_GPIO_Select(enum audio_system_gpio_type _type)
{
	int ret = 0;

	if (_type < 0 || _type >= GPIO_NUM) {
		pr_err("%s(), error, invaild gpio type %d\n", __func__, _type);
		return -EINVAL;
	}

	/* m681 (2026-07-18, audio lane): device-FACT 2.2s boot panic — dl1
	 * probe -> AudDrv_GPIO_probe -> SMARTPA_Select(GPIO_SMARTPA_ON) with
	 * the m681 wt6755 DTB, whose dl1 node only carries
	 * default/extamp-pullhigh/extamp-pulllow pinctrl names: the
	 * aud_smartpa_* lookup fails, gpio_prepare stays false, and the stock
	 * code fell through AUDIO_AEE into pinctrl_select_state(ERR_PTR) ->
	 * do_mem_abort -> PANIC_ON_OOPS reset (FLOG-decoded stack, §13.16).
	 * Same unchecked-pinctrl class as the AW3643 v225 NULL-pinctrl panic:
	 * a state absent in DTS is board truth (m681 has no smart-PA pins) —
	 * degrade to -EIO, never AEE, never deref the ERR pointer. */
	if (!aud_gpios[_type].gpio_prepare) {
		pr_err("%s(), gpio type %d (%s) not prepared - skip (no DTS state)\n",
		       __func__, _type, aud_gpios[_type].name);
		return -EIO;
	}

	if (IS_ERR_OR_NULL(pinctrlaud) ||
	    IS_ERR_OR_NULL(aud_gpios[_type].gpioctrl)) {
		pr_err("%s(), gpio type %d handle invalid - skip\n",
		       __func__, _type);
		return -EIO;
	}

	ret = pinctrl_select_state(pinctrlaud,
				   aud_gpios[_type].gpioctrl);
	if (ret)
		pr_err("%s(), error, can not set gpio type %d\n",
		       __func__, _type);

	return ret;
}

static int set_aud_clk_mosi(bool _enable)
{
/*
 * scp also need this gpio on mt6797,
 * don't switch gpio if they exist.
 */
#ifndef CONFIG_MTK_TINYSYS_SCP_SUPPORT
	static int aud_clk_mosi_counter;

	if (_enable) {
		aud_clk_mosi_counter++;
		if (aud_clk_mosi_counter == 1)
			return AudDrv_GPIO_Select(GPIO_AUD_CLK_MOSI_ON);
	} else {
		if (aud_clk_mosi_counter > 0) {
			aud_clk_mosi_counter--;
		} else {
			aud_clk_mosi_counter = 0;
			pr_warn("%s(), counter %d <= 0\n",
				__func__, aud_clk_mosi_counter);
		}

		if (aud_clk_mosi_counter == 0)
			return AudDrv_GPIO_Select(GPIO_AUD_CLK_MOSI_OFF);
	}
#endif
	return 0;
}

static int set_aud_dat_mosi(bool _enable)
{
	if (_enable)
		return AudDrv_GPIO_Select(GPIO_AUD_DAT_MOSI_ON);
	else
		return AudDrv_GPIO_Select(GPIO_AUD_DAT_MOSI_OFF);
}

static int set_aud_dat_miso(bool _enable, Soc_Aud_Digital_Block _usage)
{
	static bool adda_enable;
	static bool vow_enable;

	switch (_usage) {
	case Soc_Aud_Digital_Block_ADDA_UL:
		adda_enable = _enable;
		break;
	case Soc_Aud_Digital_Block_ADDA_VOW:
		vow_enable = _enable;
		break;
	default:
		return -EINVAL;
	}

	if (vow_enable)
		return AudDrv_GPIO_Select(GPIO_VOW_DAT_MISO_ON);
	else if (adda_enable)
		return AudDrv_GPIO_Select(GPIO_AUD_DAT_MISO_ON);
	else
		return AudDrv_GPIO_Select(GPIO_AUD_DAT_MISO_OFF);

}

static int set_aud_dat_mosi2(bool _enable)
{
	if (_enable)
		return AudDrv_GPIO_Select(GPIO_AUD_DAT_MOSI2_ON);
	else
		return AudDrv_GPIO_Select(GPIO_AUD_DAT_MOSI2_OFF);
}

static int set_aud_dat_miso2(bool _enable)
{
	if (_enable)
		return AudDrv_GPIO_Select(GPIO_AUD_DAT_MISO2_ON);
	else
		return AudDrv_GPIO_Select(GPIO_AUD_DAT_MISO2_OFF);
}

static int set_vow_clk_miso(bool _enable)
{
	if (_enable)
		return AudDrv_GPIO_Select(GPIO_VOW_CLK_MISO_ON);
	else
		return AudDrv_GPIO_Select(GPIO_VOW_CLK_MISO_OFF);
}

int AudDrv_GPIO_Request(bool _enable, Soc_Aud_Digital_Block _usage)
{
	mutex_lock(&gpio_request_mutex);
	switch (_usage) {
	case Soc_Aud_Digital_Block_ADDA_DL:
		set_aud_clk_mosi(_enable);
		set_aud_dat_mosi(_enable);
		break;
	case Soc_Aud_Digital_Block_ADDA_UL:
		set_aud_clk_mosi(_enable);
		set_aud_dat_miso(_enable, _usage);
		break;
	case Soc_Aud_Digital_Block_ADDA_VOW:
		set_vow_clk_miso(_enable);
		set_aud_dat_miso(_enable, _usage);
		break;
	case Soc_Aud_Digital_Block_ADDA_ANC:
		set_aud_clk_mosi(_enable);
		set_aud_dat_miso2(_enable);
		set_aud_dat_mosi2(_enable);
		break;
	case Soc_Aud_Digital_Block_ADDA_ALL:
		set_aud_clk_mosi(_enable);
		set_aud_dat_mosi(_enable);
		set_aud_dat_mosi2(_enable);
		set_aud_dat_miso(_enable, Soc_Aud_Digital_Block_ADDA_UL);
		set_aud_dat_miso2(_enable);
		break;
	default:
		mutex_unlock(&gpio_request_mutex);
		return -EINVAL;
	}
	mutex_unlock(&gpio_request_mutex);
	return 0;
}

int AudDrv_GPIO_SMARTPA_Select(int mode)
{
	int retval = 0;

#if 0
	switch (mode) {
	case 0:
		mt_set_gpio_mode(69 | 0x80000000, GPIO_MODE_00);
		mt_set_gpio_mode(70 | 0x80000000, GPIO_MODE_00);
		mt_set_gpio_mode(71 | 0x80000000, GPIO_MODE_00);
		mt_set_gpio_mode(72 | 0x80000000, GPIO_MODE_00);
		mt_set_gpio_mode(73 | 0x80000000, GPIO_MODE_00);
		break;
	case 1:
		mt_set_gpio_mode(69 | 0x80000000, GPIO_MODE_01);
		mt_set_gpio_mode(70 | 0x80000000, GPIO_MODE_01);
		mt_set_gpio_mode(71 | 0x80000000, GPIO_MODE_01);
		mt_set_gpio_mode(72 | 0x80000000, GPIO_MODE_01);
		mt_set_gpio_mode(73 | 0x80000000, GPIO_MODE_01);
		break;
	case 3:
		mt_set_gpio_mode(69 | 0x80000000, GPIO_MODE_03);
		mt_set_gpio_mode(70 | 0x80000000, GPIO_MODE_03);
		mt_set_gpio_mode(71 | 0x80000000, GPIO_MODE_03);
		mt_set_gpio_mode(72 | 0x80000000, GPIO_MODE_03);
		mt_set_gpio_mode(73 | 0x80000000, GPIO_MODE_03);
		break;
	default:
		pr_err("%s(), invalid mode = %d", __func__, mode);
		retval = -1;
	}
#else
	switch (mode) {
	case 0:
		AudDrv_GPIO_Select(GPIO_SMARTPA_RESET);
		break;
	case 1:
		AudDrv_GPIO_Select(GPIO_SMARTPA_ON);
		break;
	default:
		pr_err("%s(), invalid mode = %d", __func__, mode);
		retval = -1;
	}
#endif
	return retval;
}

int AudDrv_GPIO_TDM_Select(int mode)
{
	int retval = 0;

#if 0
	switch (mode) {
	case 0:
		mt_set_gpio_mode(135 | 0x80000000, GPIO_MODE_00);
		mt_set_gpio_mode(136 | 0x80000000, GPIO_MODE_00);
		mt_set_gpio_mode(138 | 0x80000000, GPIO_MODE_00);
		break;
	case 1:
		mt_set_gpio_mode(135 | 0x80000000, GPIO_MODE_01);
		mt_set_gpio_mode(136 | 0x80000000, GPIO_MODE_01);
		mt_set_gpio_mode(138 | 0x80000000, GPIO_MODE_01);
		break;
	default:
		pr_err("%s(), invalid mode = %d", __func__, mode);
		retval = -1;
}
#else
	switch (mode) {
	case 0:
		AudDrv_GPIO_Select(GPIO_TDM_MODE0);
		break;
	case 1:
		AudDrv_GPIO_Select(GPIO_TDM_MODE1);
		break;
	default:
		pr_err("%s(), invalid mode = %d", __func__, mode);
		retval = -1;
	}
#endif
	return retval;
}

int AudDrv_GPIO_PMIC_Select(int bEnable)
{
	int retval = 0;
#if MT6755_PIN
	if (bEnable == 1) {
		if (aud_gpios[GPIO_PMIC_MODE1].gpio_prepare) {
			retval =
			    pinctrl_select_state(pinctrlaud, aud_gpios[GPIO_PMIC_MODE1].gpioctrl);
			if (retval)
				pr_err("could not set aud_gpios[GPIO_PMIC_MODE1] pins\n");
		}
	} else {
		if (aud_gpios[GPIO_PMIC_MODE0].gpio_prepare) {
			retval =
			    pinctrl_select_state(pinctrlaud, aud_gpios[GPIO_PMIC_MODE0].gpioctrl);
			if (retval)
				pr_err("could not set aud_gpios[GPIO_PMIC_MODE0] pins\n");
		}

	}
#endif
	return retval;
}

int AudDrv_GPIO_I2S_Select(int bEnable)
{
	int retval = 0;

#if MT6755_PIN
	if (bEnable == 1) {
		if (aud_gpios[GPIO_I2S_MODE1].gpio_prepare) {
			retval =
			    pinctrl_select_state(pinctrlaud, aud_gpios[GPIO_I2S_MODE1].gpioctrl);
			if (retval)
				pr_err("could not set aud_gpios[GPIO_I2S_MODE1] pins\n");
		}
	} else {
		if (aud_gpios[GPIO_I2S_MODE0].gpio_prepare) {
			retval =
			    pinctrl_select_state(pinctrlaud, aud_gpios[GPIO_I2S_MODE0].gpioctrl);
			if (retval)
				pr_err("could not set aud_gpios[GPIO_I2S_MODE0] pins\n");
		}

	}
#endif
	return retval;
}

/* Amplifier pin identity must be resolved for each board before enable. */
static int forge_audio_amp_gate;
module_param(forge_audio_amp_gate, int, 0644);
MODULE_PARM_DESC(forge_audio_amp_gate,
                 "Enable external speaker amplifier after board validation (default off)");

static int forge_amp_extamp_mode;
module_param(forge_amp_extamp_mode, int, 0644);
MODULE_PARM_DESC(forge_amp_extamp_mode,
                 "Amplifier rising-edge count override: 0=caller, 1..4=train");

/* Retained donor candidate, not an established M681 board mapping. */
static int forge_amp_extamp_pin = 100;
module_param(forge_amp_extamp_pin, int, 0444);
MODULE_PARM_DESC(forge_amp_extamp_pin,
                 "Unverified donor amplifier GPIO; board validation required");

static DEFINE_MUTEX(extamp_mutex);
/* Also retained after a failed rollback, when the output may still be high. */
static bool forge_extamp_engaged;
static bool extamp2_engaged;

int AudDrv_GPIO_EXTAMP_Select(int bEnable, int mode)
{
	unsigned long pin, flags;
	int ret = 0, off_ret, i, extamp_mode;

	mutex_lock(&extamp_mutex);
	if (bEnable && !READ_ONCE(forge_audio_amp_gate)) {
		ret = -EPERM;
		goto out;
	}
	if (!bEnable && !forge_extamp_engaged &&
	    !READ_ONCE(forge_audio_amp_gate))
		goto out;
	if (forge_amp_extamp_pin < 0 || forge_amp_extamp_pin > 202) {
		ret = -EINVAL;
		goto out;
	}
	pin = (unsigned int)forge_amp_extamp_pin | 0x80000000;
	if (!bEnable) {
		ret = mt_set_gpio_out(pin, GPIO_OUT_ZERO);
		if (!ret)
			forge_extamp_engaged = false;
		goto out;
	}

	/* Preserve the donor's four-edge default pending board validation. */
	extamp_mode = mode == 1 ? 1 : mode == 2 ? 2 : 4;
	if (forge_amp_extamp_mode >= 1 && forge_amp_extamp_mode <= 4)
		extamp_mode = forge_amp_extamp_mode;

	ret = mt_set_gpio_mode(pin, GPIO_MODE_GPIO);
	if (ret)
		goto out;
	ret = mt_set_gpio_dir(pin, GPIO_DIR_OUT);
	if (ret)
		goto rollback;
	ret = mt_set_gpio_out(pin, GPIO_OUT_ZERO);
	if (ret)
		goto rollback;
	udelay(10);

	local_irq_save(flags);
	for (i = 0; i < extamp_mode; i++) {
		if (i) {
			ret = mt_set_gpio_out(pin, GPIO_OUT_ZERO);
			if (ret)
				break;
			udelay(2);
		}
		ret = mt_set_gpio_out(pin, GPIO_OUT_ONE);
		if (ret)
			break;
		udelay(2);
	}
	local_irq_restore(flags);
	if (ret)
		goto rollback;
	forge_extamp_engaged = true;
	goto out;

rollback:
	off_ret = mt_set_gpio_out(pin, GPIO_OUT_ZERO);
	forge_extamp_engaged = !!off_ret;
	if (off_ret)
		pr_err("external amplifier rollback failed: %d\n", off_ret);
out:
	mutex_unlock(&extamp_mutex);
	return ret > 0 ? -EIO : ret;
}

int AudDrv_GPIO_EXTAMP2_Select(int bEnable, int mode)
{
	int ret = 0, off_ret, i, extamp_mode;

	mutex_lock(&extamp_mutex);
	if (bEnable && !READ_ONCE(forge_audio_amp_gate)) {
		ret = -EPERM;
		goto out;
	}
	if (!bEnable && !extamp2_engaged &&
	    !READ_ONCE(forge_audio_amp_gate))
		goto out;
	if (!aud_gpios[GPIO_EXTAMP2_LOW].gpio_prepare ||
	    (bEnable && !aud_gpios[GPIO_EXTAMP2_HIGH].gpio_prepare) ||
	    IS_ERR_OR_NULL(pinctrlaud)) {
		ret = -ENODEV;
		goto out;
	}
	if (!bEnable) {
		ret = AudDrv_GPIO_Select(GPIO_EXTAMP2_LOW);
		if (!ret)
			extamp2_engaged = false;
		goto out;
	}

	extamp_mode = mode == 1 ? 1 : mode == 2 ? 2 : 3;
	for (i = 0; i < extamp_mode; i++) {
		ret = AudDrv_GPIO_Select(GPIO_EXTAMP2_LOW);
		if (ret)
			goto rollback;
		udelay(2);
		ret = AudDrv_GPIO_Select(GPIO_EXTAMP2_HIGH);
		if (ret)
			goto rollback;
		udelay(2);
	}
	extamp2_engaged = true;
	goto out;

rollback:
	off_ret = AudDrv_GPIO_Select(GPIO_EXTAMP2_LOW);
	extamp2_engaged = !!off_ret;
	if (off_ret)
		pr_err("secondary amplifier rollback failed: %d\n", off_ret);
out:
	mutex_unlock(&extamp_mutex);
	return ret > 0 ? -EIO : ret;
}

int AudDrv_GPIO_RCVSPK_Select(int bEnable)
{
	int retval = 0;

#if MT6755_PIN
	if (bEnable == 1) {
		if (aud_gpios[GPIO_RCVSPK_HIGH].gpio_prepare) {
			retval =
			    pinctrl_select_state(pinctrlaud, aud_gpios[GPIO_RCVSPK_HIGH].gpioctrl);
			if (retval)
				pr_err("could not set aud_gpios[GPIO_RCVSPK_HIGH] pins\n");
		}
	} else {
		if (aud_gpios[GPIO_RCVSPK_LOW].gpio_prepare) {
			retval =
			    pinctrl_select_state(pinctrlaud, aud_gpios[GPIO_RCVSPK_LOW].gpioctrl);
			if (retval)
				pr_err("could not set aud_gpios[GPIO_RCVSPK_LOW] pins\n");
		}

	}
#endif
	return retval;
}

int AudDrv_GPIO_HPDEPOP_Select(int bEnable)
{
	int retval = 0;

	if (bEnable == 1)
		AudDrv_GPIO_Select(GPIO_HPDEPOP_LOW);
	else
		AudDrv_GPIO_Select(GPIO_HPDEPOP_HIGH);

	return retval;
}

int audio_drv_gpio_aud_clk_pull(bool high)
{
	int retval = 0;

	pr_debug("%s, high = %d\n", __func__, high);

	if (high == 1)
		AudDrv_GPIO_Select(GPIO_AUD_CLK_MOSI_HIGH);
	else
		AudDrv_GPIO_Select(GPIO_AUD_CLK_MOSI_LOW);

	return retval;
}

static int __init dt_get_extbuck_info(unsigned long node, const char *uname, int depth, void *data)
{
	struct devinfo_extbuck_tag {
		u32 size;
		u32 tag;
		u32 extbuck_fan53526_exist;
	} *tags;
	unsigned int size = 0;

	if (depth != 1 || (strcmp(uname, "chosen") != 0 && strcmp(uname, "chosen@0") != 0))
		return 0;

	tags = (struct devinfo_extbuck_tag *) of_get_flat_dt_prop(node, "atag,extbuck_fan53526", &size);

	if (tags) {
		extbuck_fan53526_exist = tags->extbuck_fan53526_exist;
		pr_warn("[%s] fan53526_exist = %d\n", __func__, extbuck_fan53526_exist);
	}
	return 0;
}

static int __init audio_drv_gpio_init(void)
{
	of_scan_flat_dt(dt_get_extbuck_info, NULL);

	return 0;
}

arch_initcall(audio_drv_gpio_init);
