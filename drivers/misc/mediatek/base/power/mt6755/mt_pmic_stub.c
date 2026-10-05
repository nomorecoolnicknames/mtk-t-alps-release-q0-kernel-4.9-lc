/*
 * m681/mt6755 PMIC residual stubs.
 *
 * The real MT6353 PMIC driver (drivers/misc/mediatek/pmic/mt6353/, ported from
 * the confirmed-bootable 3.18 m6-graft) is now built (CONFIG_MTK_PMIC_NEW_ARCH +
 * CONFIG_MTK_PMIC_CHIP_MT6353) and provides the bulk of the PMIC API:
 * pmic_read_interface / pmic_config_interface(+_nolock) (pmic.c),
 * pmic_set/get_register_value(+_nolock) (upmu_common.c),
 * upmu_set/get_reg_value, pmic_lock/unlock (pmic.c) — those stubs were removed.
 *
 * What remains stubbed here, and why:
 *  - pmic_config_interface_nospinlock / pmic_set_register_value_nospinlock:
 *    not provided by the mt6353 driver; only referenced by deferred callers.
 *  - pmic_force_vcore_pwm: vcore-DVFS is deferred (spm_v2 idle/vcorefs trimmed).
 *  - upmu_is_chr_det: not exported by the mt6353 driver build; charger presence
 *    is reported via pmic_get_register_value(PMIC_RGS_CHRDET) by the real
 *    chr_type_det path. Left as a conservative "no charger" until the charger
 *    subsystem (MTK_CHARGER_INTERFACE / battery) is re-enabled.
 *  - pwrap_base / mt_pmic_wrap_eint_*: PMIC-wrap *chip HAL* (pwrap_hal.c) is
 *    still off (CONFIG_MTK_PMIC_WRAP pulls the conflicting upstream
 *    drivers/soc/mediatek pwrap). The common pwrap layer references these; the
 *    PMIC HAL talks to hardware through pwrap, so real PMIC register I/O needs
 *    the pwrap chip HAL brought up next.
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <mach/upmu_sw.h>
#include <mach/upmu_hw.h>
#include <mt-plat/upmu_common.h>

#ifdef CONFIG_MTK_PMIC_WRAP_HAL
/*
 * m681-49-disp: READ-ONLY PMIC access. With the pwrap chip HAL built
 * (pmic_wrap/mt6755/pwrap_hal_v1.c, WACS2 on the LK-initialised wrapper),
 * reads are real: pmic_read_interface[_nolock] go through pwrap_wacs2(), and
 * pmic_get_register_value[_nolock] index the MT6351 pmu_flags_table
 * (mt_pmic_flags_mt6351.c, 3.10 stock). Writes stay no-ops here (counted;
 * the first one is logged with its caller) and the HAL drops any direct
 * pwrap_write() too, so no PMIC register changes until kernel-m681-49 turns
 * writes on after a hardware check - so far only the touch rail enable
 * (forge_pmic_touch_write() below). Battery requests and own AUXADC clock
 * preparation below use separate default-off, feature gates. Telemetry:
 * /sys/module/mt_pmic_stub/parameters/{dropped_writes,last_dropped}.
 */
extern s32 pwrap_wacs2(u32 write, u32 adr, u32 wdata, u32 *rdata);
extern const PMU_FLAG_TABLE_ENTRY pmu_flags_table[];

static unsigned int forge_pmic_dropped_writes;
module_param_named(dropped_writes, forge_pmic_dropped_writes, uint, 0444);
static unsigned int forge_pmic_last_dropped;	/* reg << 16 | (val & 0xffff) */
module_param_named(last_dropped, forge_pmic_last_dropped, uint, 0444);

static void forge_pmic_drop(const char *api, unsigned int reg, unsigned int val,
			    void *caller)
{
	forge_pmic_dropped_writes++;
	forge_pmic_last_dropped = (reg << 16) | (val & 0xffff);
	if (forge_pmic_dropped_writes == 1)
		pr_warn("[FORGE_M681] PMIC write dropped (read-only PMIC on 4.9): %s reg/flag=0x%x val=0x%x from %pS\n",
			api, reg, val, caller);
}

static unsigned int forge_pmic_read(unsigned int RegNum, unsigned int *val,
				    unsigned int MASK, unsigned int SHIFT)
{
	u32 rdata = 0;
	unsigned int ret = pwrap_wacs2(0, RegNum, 0, &rdata);

	if (!val)
		return ret;
	/* on a failed read report 0, as the no-op stub did before */
	*val = ret ? 0 : ((rdata & (MASK << SHIFT)) >> SHIFT);
	return ret;
}

static const PMU_FLAG_TABLE_ENTRY *forge_pmic_flag(PMU_FLAGS_LIST_ENUM flagname)
{
	const PMU_FLAG_TABLE_ENTRY *f;

	if ((unsigned int)flagname >= (unsigned int)PMU_COMMAND_MAX)
		return NULL;
	f = &pmu_flags_table[flagname];
	if (f->flagname != flagname) {
		pr_warn_once("[FORGE_M681] pmu_flags_table[%d] holds flag %d - read refused\n",
			     (int)flagname, (int)f->flagname);
		return NULL;
	}
	return f;
}

unsigned int pmic_read_interface(unsigned int RegNum, unsigned int *val,
				 unsigned int MASK, unsigned int SHIFT)
{
	return forge_pmic_read(RegNum, val, MASK, SHIFT);
}
EXPORT_SYMBOL(pmic_read_interface);

unsigned int pmic_read_interface_nolock(unsigned int RegNum, unsigned int *val,
					unsigned int MASK, unsigned int SHIFT)
{
	/* pwrap_wacs2() serialises on its own spinlock */
	return forge_pmic_read(RegNum, val, MASK, SHIFT);
}
EXPORT_SYMBOL(pmic_read_interface_nolock);

unsigned short pmic_get_register_value(PMU_FLAGS_LIST_ENUM flagname)
{
	const PMU_FLAG_TABLE_ENTRY *f = forge_pmic_flag(flagname);
	unsigned int val = 0;

	if (f)
		forge_pmic_read(f->offset, &val, f->mask, f->shift);
	return val;
}
EXPORT_SYMBOL(pmic_get_register_value);

unsigned short pmic_get_register_value_nolock(PMU_FLAGS_LIST_ENUM flagname)
{
	return pmic_get_register_value(flagname);
}
EXPORT_SYMBOL(pmic_get_register_value_nolock);

/*
 * m681 4.9 A13v: the one PMIC write let through is the touch rail. GT9XX
 * never answered at 0x5d on A13v (I2C_ACKERR on every probe): VLDO28 stays
 * as the LK left it, off. The shipping 4.4 powers the touch with exactly
 * these writes (GT9XX_MZ tpd_custom_gt9xx.h: pmic_config_interface(0x0AA2 /
 * 0x0AA4, 1, 0x1, 1) = MT6351 RG_VLDO28_EN_0/_1), and the default VLDO28
 * voltage is what it ran on. Only bit 1 of those two registers, as a
 * read-modify-write; the rail status (DA_QI_VLDO28_EN, CON0 bit 15) is
 * logged whenever it or the register changes. touch_rail=0 drops them again.
 */
#define FORGE_VLDO28_CON0	0x0A9C
static int forge_pmic_touch_rail = 1;
module_param_named(touch_rail, forge_pmic_touch_rail, int, 0644);

static bool forge_pmic_touch_write(unsigned int reg, unsigned int val,
				   unsigned int mask, unsigned int shift)
{
	static u32 last_st = ~0U;
	u32 old = 0, new, st0 = 0, st1 = 0;
	s32 rd, wr;

	if (!forge_pmic_touch_rail || (reg != 0x0AA2 && reg != 0x0AA4) ||
	    mask != 0x1 || shift != 1)
		return false;
	rd = pwrap_wacs2(0, reg, 0, &old);
	pwrap_wacs2(0, FORGE_VLDO28_CON0, 0, &st0);
	new = (old & ~(mask << shift)) | ((val & mask) << shift);
	wr = rd ? rd : pwrap_wacs2(1, reg, new, NULL);
	pwrap_wacs2(0, FORGE_VLDO28_CON0, 0, &st1);
	if (rd || wr || old != new || ((st1 >> 15) & 1) != last_st)
		pr_info("[FORGE_M681] VLDO28 touch rail: reg 0x%04x 0x%04x -> 0x%04x (rd %d wr %d), DA_QI_VLDO28_EN %u -> %u\n",
			reg, old, new, rd, wr, (st0 >> 15) & 1, (st1 >> 15) & 1);
	last_st = (st1 >> 15) & 1;
	return true;
}

/* MT6351 AUXADC_RQST0_SET: stock requests CH1/CH3 by W1, not RMW.
 * On by default: the m681 LK cuts the boot.img cmdline at ~99 bytes, so a
 * boot parameter never arrives . */
static bool forge_battery_adc = true;
module_param_named(battery_adc, forge_battery_adc, bool, 0444);
static DEFINE_MUTEX(forge_adc_owner_lock);
static struct task_struct *forge_adc_owner;
static u32 forge_adc_request;
static u32 forge_adc_request_addr;
static bool forge_adc_request_armed;
/* Separate gate: the ADC request gate does not grant clock writes; on by
 * default for the same reason. */
static bool forge_battery_adc_clocks = true;
module_param_named(battery_adc_clocks, forge_battery_adc_clocks, bool, 0444);

#define FORGE_ADC_AON_REG 0x0EA2
#define FORGE_ADC_AON_MASK BIT(15)
#define FORGE_ADC_SMPS_REG 0x023A
#define FORGE_ADC_SMPS_MASK BIT(9)

/* Called under WACS2's spinlock. An IRQ or another task cannot use the token. */
bool forge_pmic_adc_consume(u32 adr, u32 value)
{
	if (in_interrupt() || READ_ONCE(forge_adc_owner) != current ||
	    !READ_ONCE(forge_adc_request_armed) ||
	    READ_ONCE(forge_adc_request_addr) != adr ||
	    READ_ONCE(forge_adc_request) != value)
		return false;
	if (adr == 0x0E98) {
		if (value != BIT(1) && value != BIT(3))
			return false;
	} else if (adr != FORGE_ADC_AON_REG && adr != FORGE_ADC_SMPS_REG) {
		return false;
	}
	WRITE_ONCE(forge_adc_request_armed, false);
	return true;
}

static int forge_pmic_adc_write(unsigned int val, unsigned int mask,
				unsigned int shift)
{
	s32 ret;

	if (val != 1 || mask != 1 || (shift != 1 && shift != 3))
		return -EINVAL;
	if (!forge_battery_adc)
		return -EPERM;
	if (in_interrupt() || in_atomic() || irqs_disabled())
		return -EWOULDBLOCK;
	if (!mutex_trylock(&forge_adc_owner_lock))
		return -EBUSY;
	WRITE_ONCE(forge_adc_request_addr, 0x0E98);
	WRITE_ONCE(forge_adc_request, BIT(shift));
	WRITE_ONCE(forge_adc_request_armed, true);
	WRITE_ONCE(forge_adc_owner, current);
	ret = pwrap_wacs2(1, 0x0E98, BIT(shift), NULL);
	WRITE_ONCE(forge_adc_request_armed, false);
	WRITE_ONCE(forge_adc_owner, NULL);
	WRITE_ONCE(forge_adc_request, 0);
	WRITE_ONCE(forge_adc_request_addr, 0);
	mutex_unlock(&forge_adc_owner_lock);
	return ret;
}

/* Caller holds forge_adc_owner_lock in process context. Preserve all other
 * register bits and grant exactly the value obtained from this owned RMW. */
static int forge_pmic_adc_clock_field(u32 reg, u32 mask, u32 target)
{
	u32 old, next, readback;
	s32 ret;

	if ((reg != FORGE_ADC_AON_REG || mask != FORGE_ADC_AON_MASK ||
	     target != FORGE_ADC_AON_MASK) &&
	    (reg != FORGE_ADC_SMPS_REG || mask != FORGE_ADC_SMPS_MASK || target))
		return -EINVAL;
	ret = pwrap_wacs2(0, reg, 0, &old);
	if (ret)
		return ret < 0 ? ret : -EIO;
	next = (old & ~mask) | target;
	if (next == old)
		return 0;
	WRITE_ONCE(forge_adc_request_addr, reg);
	WRITE_ONCE(forge_adc_request, next);
	WRITE_ONCE(forge_adc_request_armed, true);
	WRITE_ONCE(forge_adc_owner, current);
	ret = pwrap_wacs2(1, reg, next, NULL);
	WRITE_ONCE(forge_adc_request_armed, false);
	WRITE_ONCE(forge_adc_owner, NULL);
	WRITE_ONCE(forge_adc_request, 0);
	WRITE_ONCE(forge_adc_request_addr, 0);
	if (ret)
		return ret < 0 ? ret : -EIO;
	ret = pwrap_wacs2(0, reg, 0, &readback);
	if (ret)
		return ret < 0 ? ret : -EIO;
	/* Field failure is real; no ADC request or provider follows it. */
	if ((readback & mask) != target)
		return -EIO;
	return 0;
}

/* Own MT6351 stock PMIC_IMM_GetOneChannelValue: AON=1, SMPS_CK_PDN=0.
 * This is not a voltage/rail/charger permission or physical admission.
 * Missing AVG/VBUF/calibration/suspend ownership remains a separate hold. */
int forge_pmic_battery_adc_prepare(void)
{
	u32 aon, smps;
	s32 ret;

	if (!forge_battery_adc)
		return -EPERM;
	if (!of_machine_is_compatible("meizu,m681"))
		return -ENODEV;
	if (in_interrupt() || in_atomic() || irqs_disabled())
		return -EWOULDBLOCK;
	if (!mutex_trylock(&forge_adc_owner_lock))
		return -EBUSY;
	ret = pwrap_wacs2(0, FORGE_ADC_AON_REG, 0, &aon);
	if (!ret)
		ret = pwrap_wacs2(0, FORGE_ADC_SMPS_REG, 0, &smps);
	if (ret) {
		ret = ret < 0 ? ret : -EIO;
		goto out;
	}
	/* LK may already have configured these fields. No write is necessary. */
	if ((aon & FORGE_ADC_AON_MASK) && !(smps & FORGE_ADC_SMPS_MASK))
		goto out;
	if (!forge_battery_adc_clocks) {
		ret = -EACCES;
		goto out;
	}
	ret = forge_pmic_adc_clock_field(FORGE_ADC_AON_REG,
					FORGE_ADC_AON_MASK, FORGE_ADC_AON_MASK);
	if (!ret)
		ret = forge_pmic_adc_clock_field(FORGE_ADC_SMPS_REG,
						FORGE_ADC_SMPS_MASK, 0);
out:
	mutex_unlock(&forge_adc_owner_lock);
	return ret;
}

unsigned int pmic_config_interface(unsigned int RegNum, unsigned int val,
				   unsigned int MASK, unsigned int SHIFT)
{
	if (RegNum == 0x0E98)
		return forge_pmic_adc_write(val, MASK, SHIFT);
	if (forge_pmic_touch_write(RegNum, val, MASK, SHIFT))
		return 0;
	forge_pmic_drop(__func__, RegNum, val, __builtin_return_address(0));
	return 0;
}
EXPORT_SYMBOL(pmic_config_interface);

unsigned int pmic_config_interface_nolock(unsigned int RegNum, unsigned int val,
					  unsigned int MASK, unsigned int SHIFT)
{
	/* No sleeping ownership route through the nolock API. */
	if (RegNum == 0x0E98)
		return -EOPNOTSUPP;
	forge_pmic_drop(__func__, RegNum, val, __builtin_return_address(0));
	return 0;
}
EXPORT_SYMBOL(pmic_config_interface_nolock);

unsigned short pmic_set_register_value(PMU_FLAGS_LIST_ENUM flagname, unsigned int val)
{
	forge_pmic_drop(__func__, flagname, val, __builtin_return_address(0));
	return 0;
}
EXPORT_SYMBOL(pmic_set_register_value);

unsigned short pmic_set_register_value_nolock(PMU_FLAGS_LIST_ENUM flagname, unsigned int val)
{
	forge_pmic_drop(__func__, flagname, val, __builtin_return_address(0));
	return 0;
}
EXPORT_SYMBOL(pmic_set_register_value_nolock);

/* battery-throttling notifiers (pmic_throttling_dlpt.c, not built): gpufreq
 * registers its low-battery/volume callbacks; nothing ever calls them here */
void register_low_battery_notify(void (*low_battery_callback)(LOW_BATTERY_LEVEL),
				 LOW_BATTERY_PRIO prio_val)
{
}
EXPORT_SYMBOL(register_low_battery_notify);

void register_battery_percent_notify(void (*battery_percent_callback)(BATTERY_PERCENT_LEVEL),
				     BATTERY_PERCENT_PRIO prio_val)
{
}
EXPORT_SYMBOL(register_battery_percent_notify);
#endif /* CONFIG_MTK_PMIC_WRAP_HAL */

unsigned int pmic_config_interface_nospinlock(unsigned int RegNum, unsigned int val,
					      unsigned int MASK, unsigned int SHIFT)
{
#ifdef CONFIG_MTK_PMIC_WRAP_HAL
	if (RegNum == 0x0E98)
		return -EOPNOTSUPP;
	forge_pmic_drop(__func__, RegNum, val, __builtin_return_address(0));
#endif
	return 0;
}
EXPORT_SYMBOL(pmic_config_interface_nospinlock);

unsigned short pmic_set_register_value_nospinlock(PMU_FLAGS_LIST_ENUM flagname, unsigned int val)
{
#ifdef CONFIG_MTK_PMIC_WRAP_HAL
	forge_pmic_drop(__func__, flagname, val, __builtin_return_address(0));
#endif
	return 0;
}
EXPORT_SYMBOL(pmic_set_register_value_nospinlock);

int pmic_force_vcore_pwm(bool enable)
{
	return 0;
}
EXPORT_SYMBOL(pmic_force_vcore_pwm);

#ifndef CONFIG_MTK_SMART_BATTERY
/* charger presence (PMIC) — report "no charger" while the charger subsystem is
 * deferred. The real chr_type_det path reads PMIC_RGS_CHRDET directly.
 * m681 (2026-07-15): with CONFIG_MTK_SMART_BATTERY=y the real provider is
 * battery_common_fg_20.c — stub compiled out to avoid duplicate symbol. */
bool upmu_is_chr_det(void)
{
	return false;
}
EXPORT_SYMBOL(upmu_is_chr_det);
#endif /* !CONFIG_MTK_SMART_BATTERY */

/*
 * PMIC-wrap chip-HAL symbols (pwrap_base, mt_pmic_wrap_eint_status,
 * mt_pmic_wrap_eint_clr) are now provided by the real chip HAL,
 * drivers/misc/mediatek/pmic_wrap/mt6755/pwrap_hal_v1.c, which compiles
 * once the mt6755/Makefile uses CONFIG_MTK_PMIC_WRAP_HAL (the Kconfig symbol
 * that actually exists) instead of the phantom CONFIG_MTK_PMIC_WRAP. The
 * stubs that used to stand in for them here were removed to avoid multiple
 * definition link errors now that the real HAL is built.
 */

/*
 * MT6311 external-buck presence queries. m681 has no MT6311 (it uses the PMIC's
 * internal VPROC/VGPU/VCORE bucks), and mt6311.c is deferred (it needs the 3.18
 * MTK-extended i2c_client which 4.4 lacks). The mt6353 driver ext-buck helpers
 * call these; reporting "absent / not ready" makes them fall back to the internal
 * bucks - the m681-correct behaviour.
 */
int is_mt6311_exist(void)
{
	return 0;
}
EXPORT_SYMBOL(is_mt6311_exist);

int is_mt6311_sw_ready(void)
{
	return 0;
}
EXPORT_SYMBOL(is_mt6311_sw_ready);

int get_mt6311_i2c_ch_num(void)
{
	return -1;
}
EXPORT_SYMBOL(get_mt6311_i2c_ch_num);

#ifndef CONFIG_MTK_PMIC_WRAP_HAL
/* m681-49 skeleton (2026-08-27): PMIC interface stubs while the pmic/
 * driver is deferred (Phase 2 carries pmic_wrap + the MT6351 driver).
 * SPM and the forge marker call these for PMIC/RTC access; no-ops until
 * the real pmic driver lands. REMOVE with the real driver.
 * m681-49-disp: with MTK_PMIC_WRAP_HAL the read-only versions at the top
 * of this file and the pwrap HAL replace this whole block. */
unsigned int pmic_config_interface(unsigned int RegNum, unsigned int val, unsigned int MASK, unsigned int SHIFT)
{
	return 0;
}
EXPORT_SYMBOL(pmic_config_interface);

unsigned int pmic_config_interface_nolock(unsigned int RegNum, unsigned int val, unsigned int MASK, unsigned int SHIFT)
{
	return 0;
}
EXPORT_SYMBOL(pmic_config_interface_nolock);

signed int pwrap_write(unsigned int adr, unsigned int wdata)
{
	return -ENODEV;
}
EXPORT_SYMBOL(pwrap_write);

/* m681-49 skeleton: read-side PMIC interface + pwrap base pointer stubs,
 * while pmic_wrap/mt6351 are deferred to Phase 2. REMOVE with the driver. */
unsigned int pmic_read_interface_nolock(unsigned int RegNum, unsigned int *val, unsigned int MASK, unsigned int SHIFT)
{
	if (val)
		*val = 0;
	return 0;
}
EXPORT_SYMBOL(pmic_read_interface_nolock);

void __iomem *pwrap_base;
EXPORT_SYMBOL(pwrap_base);
#endif /* !CONFIG_MTK_PMIC_WRAP_HAL */

#if !defined(CONFIG_MTK_PMIC_NEW_ARCH) && !defined(CONFIG_MTK_PMIC_WRAP_HAL)
/* m681-49-disp: leds/mt6755 (MT65XX_LED_MODE_PMIC = MT6351 ISINK channels)
 * references this. No m681 LED is in PMIC mode (DT: lcd-backlight led_mode 5
 * = CUST_BLS_PWM, i.e. DISP_PWM in the display block; the others 0 = NONE),
 * so it is not reached on this board. A real ISINK write needs the MT6351
 * PMIC driver + pwrap chip HAL (MTK_PMIC_NEW_ARCH + MTK_PMIC_WRAP_HAL), not
 * built on 4.9 yet; until then the write is dropped and reported once. */
unsigned short pmic_set_register_value(PMU_FLAGS_LIST_ENUM flagname, unsigned int val)
{
	pr_warn_once("[FORGE_M681] pmic_set_register_value(%d, 0x%x) dropped: no PMIC driver on 4.9\n",
		     (int)flagname, val);
	return 0;
}
EXPORT_SYMBOL(pmic_set_register_value);
#endif
