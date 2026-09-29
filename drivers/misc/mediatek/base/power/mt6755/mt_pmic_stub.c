/*
 * MT6755 PMIC access while the full MT6351 driver is being integrated.
 * Reads use the LK-initialized WACS2 wrapper. Writes remain restricted to
 * the existing touch-rail path; blocked requests must not report success.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/moduleparam.h>
#include <linux/ratelimit.h>
#include <mach/upmu_sw.h>
#include <mach/upmu_hw.h>
#include <mt-plat/upmu_common.h>

#ifdef CONFIG_MTK_PMIC_WRAP_HAL
/*
 * m681-49-disp: READ-ONLY PMIC access. With the pwrap chip HAL built
 * (pmic_wrap/mt6755/pwrap_hal_v1.c, WACS2 on the LK-initialised wrapper),
 * reads are real: pmic_read_interface[_nolock] go through pwrap_wacs2(), and
 * pmic_get_register_value[_nolock] index the MT6351 pmu_flags_table
 * (mt_pmic_flags_mt6351.c, 3.10 stock). Writes stay blocked here (counted;
 * the first one is logged with its caller) and the HAL drops any direct
 * pwrap_write() too, so no PMIC register changes until kernel-m681-49 turns
 * writes on after a hardware check - so far only the touch rail enable
 * (forge_pmic_touch_write() below). Telemetry:
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

static bool forge_pmic_field_valid(unsigned int reg, unsigned int mask,
                                   unsigned int shift)
{
	return !(reg & ~0xfffeU) && mask && shift < 16 &&
	       mask <= (0xffffU >> shift);
}

static unsigned int forge_pmic_read(unsigned int RegNum, unsigned int *val,
				    unsigned int MASK, unsigned int SHIFT)
{
	u32 rdata = 0;
	unsigned int ret;

	if (!val)
		return -EINVAL;
	*val = 0;
	if (!forge_pmic_field_valid(RegNum, MASK, SHIFT))
		return -EINVAL;
	ret = pwrap_wacs2(0, RegNum, 0, &rdata);
	/* Keep the legacy value-only readers deterministic on transport failure. */
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
				   unsigned int mask, unsigned int shift,
				   unsigned int *result)
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
	*result = wr;
	return true;
}

/*
 * m681 4.9: PMIC write allowlist (M681_49_WHY_NOT_BOOTING.md §28.4, §29;
 * lead decisions 2026-09-29). Every write through pmic_config_interface()
 * is a read-modify-write checked against one table:
 *  - plain register: the bits it would change, old ^ new, must lie inside
 *    the entry's mask;
 *  - SET/CLR register (FORGE_PW_W1): no read-modify-write - only the field
 *    itself is written (zeros are no-ops there), and it must lie inside the
 *    mask. A read of such a register may return the base register, and
 *    writing those bits back to a CLR register would clear them;
 *  - FORGE_PW_SAME: only a write that changes nothing (rails other drivers
 *    depend on; no entries yet).
 * Anything else is refused whole (-EPERM), never applied in part. Each
 * entry belongs to an owner with its own switch, has accept/refuse
 * counters, and its first accepted write is logged with the caller. The
 * pwrap gate (pwrap_hal_v1.c) passes a table address only while this code
 * has it in flight (forge_pmic_table_write_adr), so a direct pwrap_write()
 * cannot use the table. Dump: /sys/module/mt_pmic_stub/parameters/write_table.
 */
enum { FORGE_PW_CONN, FORGE_PW_AUDIO, FORGE_PW_OWNERS };

static int forge_pw_allow[FORGE_PW_OWNERS] = {
	[FORGE_PW_CONN] = 1,	/* lead-approved; consys itself stays gated */
	[FORGE_PW_AUDIO] = 0,	/* codec phase P1 only on the lead's "go" */
};
module_param_named(allow_conn, forge_pw_allow[FORGE_PW_CONN], int, 0644);
module_param_named(allow_audio, forge_pw_allow[FORGE_PW_AUDIO], int, 0644);

#define FORGE_PW_W1	0x1
#define FORGE_PW_SAME	0x2

struct forge_pw_entry {
	u16 lo, hi;		/* register range, inclusive */
	u16 mask;		/* bits a write may change (or set, for W1) */
	u8 owner, flags;
	const char *name;
	u32 accepted, refused;
};

static struct forge_pw_entry forge_pw_table[] = {
	/* connsys rails, bit 1 EN and bit 3 ON_CTRL (§28.4); the consys
	 * driver checks VOSEL before it sets EN */
	{ 0x0A52, 0x0A52, 0x000A, FORGE_PW_CONN, 0, "LDO_VCN18_CON0" },
	{ 0x0A0C, 0x0A0C, 0x000A, FORGE_PW_CONN, 0, "LDO_VCN28_CON0" },
	{ 0x0A98, 0x0A98, 0x000A, FORGE_PW_CONN, 0, "LDO_VCN33_CON3 (BT)" },
	{ 0x0A9A, 0x0A9A, 0x000A, FORGE_PW_CONN, 0, "LDO_VCN33_CON4 (WIFI)" },
	/* codec class A: blocks nothing but the codec uses (§29.2) */
	{ 0x0800, 0x0806, 0xFFFF, FORGE_PW_AUDIO, 0, "ZCD_CON0-3" },
	{ 0x0CF2, 0x0D30, 0xFFFF, FORGE_PW_AUDIO, 0, "AUDDEC/AUDENC/AUDNCP" },
	{ 0x2000, 0x2054, 0xFFFF, FORGE_PW_AUDIO, 0, "AFE UL/DL, NEWIF, SGEN, ADDA2" },
	{ 0x2090, 0x2098, 0xFFFF, FORGE_PW_AUDIO, 0, "AFE DCCLK, HPANC, NCP" },
	/* codec class B: shared registers, audio bits only. TOP_CKPDN_CON0
	 * b12-15 = AUDNCP, AUDIF, AUD, ZCD13M (b11 is AUXADC_26M, not audio);
	 * TOP_CLKSQ b0 = CLKSQ_EN_AUD */
	{ 0x023A, 0x023A, 0xF000, FORGE_PW_AUDIO, 0, "TOP_CKPDN_CON0" },
	{ 0x023C, 0x023C, 0xF000, FORGE_PW_AUDIO, FORGE_PW_W1, "TOP_CKPDN_CON0_SET" },
	{ 0x023E, 0x023E, 0xF000, FORGE_PW_AUDIO, FORGE_PW_W1, "TOP_CKPDN_CON0_CLR" },
	{ 0x029A, 0x029A, 0x0001, FORGE_PW_AUDIO, 0, "TOP_CLKSQ" },
	{ 0x029C, 0x029C, 0x0001, FORGE_PW_AUDIO, FORGE_PW_W1, "TOP_CLKSQ_SET" },
	{ 0x029E, 0x029E, 0x0001, FORGE_PW_AUDIO, FORGE_PW_W1, "TOP_CLKSQ_CLR" },
	/* DRV_CON2 b7:4 RG_OCTL_AUD_DAT_MISO, audio pad drive (codec init) */
	{ 0x0230, 0x0230, 0x00F0, FORGE_PW_AUDIO, 0, "DRV_CON2" },
};

/* read by the pwrap gate under its wrp_lock; written under forge_pw_lock */
u32 forge_pmic_table_write_adr;
EXPORT_SYMBOL(forge_pmic_table_write_adr);
static DEFINE_SPINLOCK(forge_pw_lock);

static struct forge_pw_entry *forge_pw_find(unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(forge_pw_table); i++)
		if (reg >= forge_pw_table[i].lo && reg <= forge_pw_table[i].hi)
			return &forge_pw_table[i];
	return NULL;
}

/* returns false if no table entry covers reg (caller drops the write) */
static bool forge_pw_write(unsigned int reg, unsigned int val,
			   unsigned int mask, unsigned int shift,
			   unsigned int *result, void *caller)
{
	struct forge_pw_entry *e = forge_pw_find(reg);
	unsigned long flags;
	u32 old = 0, new;
	s32 rd, wr = 0;
	bool ok;

	if (!e)
		return false;
	spin_lock_irqsave(&forge_pw_lock, flags);
	rd = pwrap_wacs2(0, reg, 0, &old);
	if (e->flags & FORGE_PW_W1)
		new = (val & mask) << shift;
	else
		new = (old & ~(mask << shift)) | ((val & mask) << shift);
	if (rd)
		ok = false;
	else if (!forge_pw_allow[e->owner])
		ok = false;
	else if (e->flags & FORGE_PW_SAME)
		ok = (old == new);
	else if (e->flags & FORGE_PW_W1)
		ok = !(new & ~e->mask);
	else
		ok = !((old ^ new) & ~e->mask);
	if (ok) {
		WRITE_ONCE(forge_pmic_table_write_adr, reg);
		wr = pwrap_wacs2(1, reg, new, NULL);
		WRITE_ONCE(forge_pmic_table_write_adr, 0);
	}
	if (ok && !wr)
		e->accepted++;
	else
		e->refused++;
	spin_unlock_irqrestore(&forge_pw_lock, flags);

	if (!ok) {
		pr_warn_ratelimited("[FORGE_M681] PMIC write refused: %s 0x%04x 0x%04x -> 0x%04x (allowed 0x%04x%s, owner %d %s, rd %d) from %pS\n",
				    e->name, reg, old, new, e->mask,
				    (e->flags & FORGE_PW_W1) ? " W1" : "", e->owner,
				    forge_pw_allow[e->owner] ? "on" : "off", rd, caller);
		*result = rd ? rd : -EPERM;
	} else {
		if (e->accepted == 1 || wr)
			pr_info("[FORGE_M681] PMIC write: %s 0x%04x 0x%04x -> 0x%04x (wr %d) from %pS\n",
				e->name, reg, old, new, wr, caller);
		*result = wr;
	}
	return true;
}

static int forge_pw_table_get(char *buffer, const struct kernel_param *kp)
{
	int i, n = 0;

	for (i = 0; i < ARRAY_SIZE(forge_pw_table); i++) {
		struct forge_pw_entry *e = &forge_pw_table[i];

		n += scnprintf(buffer + n, PAGE_SIZE - n,
			       "0x%04x-0x%04x mask 0x%04x%s owner %d (%s) accepted %u refused %u %s\n",
			       e->lo, e->hi, e->mask,
			       (e->flags & FORGE_PW_W1) ? " W1" :
			       (e->flags & FORGE_PW_SAME) ? " SAME" : "",
			       e->owner, forge_pw_allow[e->owner] ? "on" : "off",
			       e->accepted, e->refused, e->name);
	}
	return n;
}

static const struct kernel_param_ops forge_pw_table_ops = {
	.get = forge_pw_table_get,
};
module_param_cb(write_table, &forge_pw_table_ops, NULL, 0444);

static unsigned int forge_pmic_config(const char *api, unsigned int RegNum,
				      unsigned int val, unsigned int MASK,
				      unsigned int SHIFT, void *caller)
{
	unsigned int ret;

	if (!forge_pmic_field_valid(RegNum, MASK, SHIFT) || (val & ~MASK))
		return -EINVAL;
	if (forge_pmic_touch_write(RegNum, val, MASK, SHIFT, &ret))
		return ret;
	if (forge_pw_write(RegNum, val, MASK, SHIFT, &ret, caller))
		return ret;
	forge_pmic_drop(api, RegNum, val, caller);
	return -EPERM;
}

unsigned int pmic_config_interface(unsigned int RegNum, unsigned int val,
				   unsigned int MASK, unsigned int SHIFT)
{
	return forge_pmic_config(__func__, RegNum, val, MASK, SHIFT,
				 __builtin_return_address(0));
}
EXPORT_SYMBOL(pmic_config_interface);

unsigned int pmic_config_interface_nolock(unsigned int RegNum, unsigned int val,
					  unsigned int MASK, unsigned int SHIFT)
{
	return forge_pmic_config(__func__, RegNum, val, MASK, SHIFT,
				 __builtin_return_address(0));
}
EXPORT_SYMBOL(pmic_config_interface_nolock);

unsigned short pmic_set_register_value(PMU_FLAGS_LIST_ENUM flagname, unsigned int val)
{
	forge_pmic_drop(__func__, flagname, val, __builtin_return_address(0));
	return -EPERM;
}
EXPORT_SYMBOL(pmic_set_register_value);

unsigned short pmic_set_register_value_nolock(PMU_FLAGS_LIST_ENUM flagname, unsigned int val)
{
	forge_pmic_drop(__func__, flagname, val, __builtin_return_address(0));
	return -EPERM;
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
	forge_pmic_drop(__func__, RegNum, val, __builtin_return_address(0));
	return -EPERM;
#else
	return -EOPNOTSUPP;
#endif
}
EXPORT_SYMBOL(pmic_config_interface_nospinlock);

unsigned short pmic_set_register_value_nospinlock(PMU_FLAGS_LIST_ENUM flagname, unsigned int val)
{
#ifdef CONFIG_MTK_PMIC_WRAP_HAL
	forge_pmic_drop(__func__, flagname, val, __builtin_return_address(0));
	return -EPERM;
#else
	return -EOPNOTSUPP;
#endif
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
