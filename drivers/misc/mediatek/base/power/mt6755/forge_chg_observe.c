/* M681 MT6351 battery telemetry; ADC gate defaults off.
 * Own AUXADC clock preparation has a separate default-off gate. No charger,
 * GPIO, RTC or voltage control. Capacity is an
 * uncalibrated OCV ID0 estimate, not a fuel-gauge measurement.
 */
#include <linux/m3note_board.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/ktime.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/ratelimit.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>

#define FORGE_MT6351_CHR_CON0	0x0F78	/* cable presence, not charging */
#define FORGE_CHG_TICK_MS	10000	/* CHRDET */
#define FORGE_CHG_FULL_TICKS	3	/* battery: every 30 s */
#define FORGE_BAT_FAST_TICKS	6	/* before registration: every tick, 1 min */

/* battery monitor; MT6351 fields from stock upmu_hw.h */
#define FORGE_MT6351_TOP_CKPDN_CON0	0x023A	/* b9 RG_AUXADC_SMPS_CK_PDN */
#define FORGE_MT6351_TOP_CKPDN_CON1	0x0240	/* b10 RG_AUXADC_CK_PDN */
#define FORGE_MT6351_VBIF28_CON0	0x0A86	/* b15 DA_QI_VBIF28_EN */
#define FORGE_MT6351_AUXADC_ADC3	0x0E06	/* CH3 BATON: [11:0], [15] RDY */
#define FORGE_MT6351_AUXADC_ADC25	0x0E32	/* CH1 ISENSE by AP: [14:0], [15] RDY */
#define FORGE_MT6351_AUXADC_STA0	0x0E90	/* [11:0] channel busy */
#define FORGE_MT6351_AUXADC_RQST0_SET	0x0E98	/* W1, bit n requests channel n */
#define FORGE_MT6351_AUXADC_CON0	0x0EA2	/* b15 AUXADC_CK_AON */
#define FORGE_AUXADC_CH_ISENSE	1
#define FORGE_AUXADC_CH_BATON	3
#define FORGE_AUXADC_RDY	BIT(15)
#define FORGE_AUXADC_POLLS	16	/* further reads 1.3-1.5 ms apart, as stock */
#define FORGE_BAT_MIN_MV	2500	/* a VBAT outside this is not a sample */
#define FORGE_BAT_MAX_MV	4600
#define FORGE_BAT_EMPTY_MV	3400	/* stock SHUTDOWN_SYSTEM_VOLTAGE */
#define FORGE_BAT_EMPTY_SAMPLES	3
/* BATON accepted only by proof 'A' with the same raw value this many
 * samples in a row (5 min): the temperature is stale */
#define FORGE_BAT_A_MAX		10
#define FORGE_NTC_PULLUP_OHM	24000
#define FORGE_NTC_PULLUP_MV	2800
/* VOLTAGE_NOW and TEMP older than three missed samples read as no data */
#define FORGE_BAT_STALE_MS	(3 * FORGE_CHG_FULL_TICKS * FORGE_CHG_TICK_MS)
/* no valid sample this long and no charger: STATUS UNKNOWN  */
#define FORGE_BAT_LOST_MS	(10 * 60 * 1000)

extern s32 pwrap_read(u32 adr, u32 *rdata);
extern int forge_pmic_battery_adc_prepare(void);
extern unsigned int pmic_config_interface(unsigned int RegNum, unsigned int val,
					  unsigned int MASK, unsigned int SHIFT);

static struct delayed_work forge_chg_work;
static unsigned int forge_chg_ticks;
static int forge_chg_chrdet = -1;

static bool forge_bat_enable = true;
module_param_named(battery, forge_bat_enable, bool, 0444);

/* stock Batt_Temperature_Table, BAT_NTC_10 (wt6755_66_sz_l
 * cust_battery_meter_table_multi_profile.h:90-111) */
static const struct {
	s8 c;
	u32 ohm;
} forge_ntc[] = {
	{ -25, 87559 }, { -20, 68237 }, { -15, 53650 }, { -10, 42506 },
	{ -5, 33892 }, { 0, 27219 }, { 5, 22021 }, { 10, 17926 },
	{ 15, 14674 }, { 20, 12081 }, { 25, 10000 }, { 30, 8315 },
	{ 35, 6948 }, { 40, 5834 }, { 45, 4917 }, { 50, 4161 },
	{ 55, 3535 }, { 60, 3014 }, { 65, 2586 },
};

/* stock battery_profile_t2[0], profile ID0 (D_ATL) at 25 C, DOD % : OCV mV
 * (cust_battery_meter_table_multi_profile.h:908-984) */
static const struct forge_ocv {
	u8 dod;
	u16 mv;
} forge_ocv_id0[] = {
	{ 0, 4391 }, { 2, 4350 }, { 3, 4339 }, { 5, 4315 }, { 6, 4303 },
	{ 7, 4292 }, { 9, 4268 }, { 10, 4257 }, { 12, 4234 }, { 13, 4223 },
	{ 15, 4201 }, { 17, 4179 }, { 18, 4170 }, { 19, 4158 }, { 20, 4148 },
	{ 21, 4137 }, { 23, 4117 }, { 25, 4096 }, { 26, 4086 }, { 27, 4076 },
	{ 29, 4063 }, { 29, 4055 }, { 30, 4043 }, { 31, 4027 }, { 33, 4000 },
	{ 34, 3992 }, { 36, 3982 }, { 37, 3976 }, { 38, 3969 }, { 40, 3954 },
	{ 41, 3944 }, { 42, 3935 }, { 43, 3925 }, { 45, 3904 }, { 46, 3893 },
	{ 48, 3874 }, { 49, 3867 }, { 50, 3862 }, { 51, 3856 }, { 53, 3845 },
	{ 54, 3839 }, { 56, 3831 }, { 57, 3826 }, { 59, 3818 }, { 60, 3815 },
	{ 60, 3811 }, { 62, 3805 }, { 63, 3802 }, { 64, 3799 }, { 65, 3795 },
	{ 67, 3789 }, { 68, 3787 }, { 69, 3784 }, { 70, 3782 }, { 72, 3775 },
	{ 73, 3772 }, { 75, 3764 }, { 76, 3759 }, { 78, 3753 }, { 79, 3750 },
	{ 80, 3747 }, { 81, 3743 }, { 82, 3739 }, { 84, 3731 }, { 85, 3723 },
	{ 86, 3717 }, { 87, 3712 }, { 88, 3705 }, { 89, 3692 }, { 90, 3690 },
	{ 91, 3691 }, { 92, 3688 }, { 94, 3686 }, { 95, 3679 }, { 96, 3652 },
	{ 98, 3546 }, { 100, 3322 },
};

/* one AUXADC conversion */
struct forge_adc {
	int ret;		/* 0: the value comes from this request */
	u32 pre, raw;		/* result register before the request, and after */
	u32 sta0;		/* AUXADC_STA0 right after the request, for the log */
	int polls;
	char proof;		/* why the value counts, see forge_adc_convert() */
};

struct forge_bat_sample {
	struct forge_adc vb, bt;
	int vbat_mv, baton_mv, ntc_ohm, temp_dc, temp_ret;
	int regs_ok;
	u32 adc_con0, ckpdn0, ckpdn1, sta0, vbif28;	/* read only, for the log */
};

/* what get_property reports; written by the work only */
static DEFINE_SPINLOCK(forge_bat_lock);
static struct {
	int cap, vbat_mv, temp_dc, status, chrdet;
	bool temp_ok;
	/* CLOCK_BOOTTIME of the last valid sample: it runs through suspend,
	 * so values from before a long sleep do not pass as fresh */
	ktime_t at;
} forge_bat_pub = { .status = POWER_SUPPLY_STATUS_UNKNOWN, .chrdet = -1 };

/* filter state and the last sample, for the work and /proc */
static DEFINE_MUTEX(forge_bat_mutex);
static struct {
	struct forge_bat_sample last;
	int ret, temp_ret;	/* of the last sample */
	int vavg, vavg_chrdet, est, low;
	unsigned int a_run;	/* BATON samples in a row with only proof 'A' */
	u32 a_raw;		/* and their raw value */
	unsigned int ok, failed;
	bool have, reg_failed;
} forge_bat = { .ret = 1, .temp_ret = 1 };	/* 1: nothing sampled yet */
static struct power_supply *forge_bat_psy, *forge_usb_psy;

/*
 * One conversion, as stock PMIC_IMM_GetOneChannelValue() does it
 * (pmic_auxadc.c:262-294): request the channel, read AUXADC_STA0 at once
 * (kept for the log), poll RDY. The value counts only with a proof that it
 * comes from this request, a->proof:
 *  'R' RDY read 0 after the request, then 1;
 *  'V' the result differs from the one before the request;
 *  'A' only with @converting: an earlier conversion of the same sample was
 *      proven, so the AUXADC is processing requests, and a conversion that
 *      ended before the first read with an unchanged value (a steady BATON)
 *      is taken as it is.
 * Otherwise a RDY that stays up over an unchanged value may be a result from
 * before (the AUXADC clock gated, for one): -ESTALE, not a sample. The STA0
 * busy bits are not a proof: a modem request for the same channel sets
 * them too without refreshing the AP result.
 */
static int forge_adc_convert(unsigned int ch, u32 reg, bool converting,
			     struct forge_adc *a)
{
	bool dropped = false;
	int i;

	memset(a, 0, sizeof(*a));
	if (pwrap_read(reg, &a->pre))
		return a->ret = -EIO;
	/* W1 request only; refused unless mt_pmic_stub.battery_adc=1. */
	a->ret = (int)pmic_config_interface(FORGE_MT6351_AUXADC_RQST0_SET,
					    1, 1, ch);
	if (a->ret)
		return a->ret;
	if (pwrap_read(FORGE_MT6351_AUXADC_STA0, &a->sta0))
		return a->ret = -EIO;
	for (i = 0; i <= FORGE_AUXADC_POLLS; i++) {
		if (i)
			usleep_range(1300, 1500);
		if (pwrap_read(reg, &a->raw))
			return a->ret = -EIO;
		if (a->raw & FORGE_AUXADC_RDY)
			break;
		dropped = true;
	}
	a->polls = i;
	if (!(a->raw & FORGE_AUXADC_RDY))
		a->ret = -ETIMEDOUT;
	else if (dropped)
		a->proof = 'R';
	else if (a->raw != a->pre)
		a->proof = 'V';
	else if (converting)
		a->proof = 'A';
	else
		a->ret = -ESTALE;
	return a->ret;
}

/* NTC resistance and temperature in 0.1 C; -ERANGE outside the table */
static int forge_ntc_temp(int mv, int *ohm, int *dc)
{
	int i, r;

	if (mv <= 0 || mv >= FORGE_NTC_PULLUP_MV)
		return -ERANGE;
	r = FORGE_NTC_PULLUP_OHM * mv / (FORGE_NTC_PULLUP_MV - mv);
	*ohm = r;
	if (r > forge_ntc[0].ohm || r < forge_ntc[ARRAY_SIZE(forge_ntc) - 1].ohm)
		return -ERANGE;
	for (i = 1; i < ARRAY_SIZE(forge_ntc) - 1; i++)
		if (r >= forge_ntc[i].ohm)
			break;
	/* forge_ntc[i - 1].ohm >= r >= forge_ntc[i].ohm */
	*dc = forge_ntc[i - 1].c * 10 +
	      DIV_ROUND_CLOSEST((int)(forge_ntc[i - 1].ohm - r) *
				(forge_ntc[i].c - forge_ntc[i - 1].c) * 10,
				(int)(forge_ntc[i - 1].ohm - forge_ntc[i].ohm));
	return 0;
}

/* capacity % from the OCV table, linear between rows */
static int forge_ocv_soc(int mv)
{
	int i, dod;

	if (mv >= forge_ocv_id0[0].mv)
		return 100 - forge_ocv_id0[0].dod;
	for (i = 1; i < ARRAY_SIZE(forge_ocv_id0); i++) {
		const struct forge_ocv *hi = &forge_ocv_id0[i - 1];
		const struct forge_ocv *lo = &forge_ocv_id0[i];

		if (mv < lo->mv)
			continue;
		dod = lo->dod;
		/* the stock table has a few flat or rising rows */
		if (hi->mv > lo->mv)
			dod -= DIV_ROUND_CLOSEST((mv - lo->mv) * (lo->dod - hi->dod),
						 hi->mv - lo->mv);
		return 100 - dod;
	}
	return 0;
}

/* returns 0 if m->vbat_mv is a valid VBAT sample */
static int forge_bat_measure(struct forge_bat_sample *m)
{
	int ret;

	memset(m, 0, sizeof(*m));
	m->temp_ret = -ENODATA;
	m->regs_ok = !pwrap_read(FORGE_MT6351_AUXADC_CON0, &m->adc_con0) &&
		     !pwrap_read(FORGE_MT6351_TOP_CKPDN_CON0, &m->ckpdn0) &&
		     !pwrap_read(FORGE_MT6351_TOP_CKPDN_CON1, &m->ckpdn1) &&
		     !pwrap_read(FORGE_MT6351_AUXADC_STA0, &m->sta0) &&
		     !pwrap_read(FORGE_MT6351_VBIF28_CON0, &m->vbif28);
	ret = forge_pmic_battery_adc_prepare();
	if (ret)
		return ret;
	/* VBAT needs its own proof: a stale one would freeze the capacity */
	ret = forge_adc_convert(FORGE_AUXADC_CH_ISENSE,
				FORGE_MT6351_AUXADC_ADC25, false, &m->vb);
	if (ret)
		return ret;
	/* stock: 15 bits, x3 divider, 1800 mV full scale */
	m->vbat_mv = (m->vb.raw & 0x7fff) * 3 * 1800 / 32768;
	if (m->vbat_mv < FORGE_BAT_MIN_MV || m->vbat_mv > FORGE_BAT_MAX_MV)
		return -ERANGE;
	/* the VBAT conversion just proved the AUXADC converts */
	m->temp_ret = forge_adc_convert(FORGE_AUXADC_CH_BATON,
					FORGE_MT6351_AUXADC_ADC3, true, &m->bt);
	if (!m->temp_ret) {
		/* stock: 12 bits, x2 divider, 1800 mV full scale */
		m->baton_mv = (m->bt.raw & 0xfff) * 2 * 1800 / 4096;
		m->temp_ret = forge_ntc_temp(m->baton_mv, &m->ntc_ohm,
					     &m->temp_dc);
	}
	return 0;
}

static void forge_bat_log_sample(const char *what, int ret,
				 const struct forge_bat_sample *m)
{
	pr_notice("[FORGE_M681] battery: %s (%d): VBAT %d mV (CH1 %d: 0x%04x -> 0x%04x, STA0 0x%04x, %d polls, proof %c) BATON %d mV %d ohm (CH3 %d: 0x%04x -> 0x%04x, STA0 0x%04x, %d polls, proof %c, temp %d) | pre-prepare AUXADC_CON0 0x%04x TOP_CKPDN_CON0 0x%04x CON1 0x%04x STA0 0x%04x VBIF28_CON0 0x%04x (rd %d)\n",
		  what, ret, m->vbat_mv, m->vb.ret, m->vb.pre, m->vb.raw,
		  m->vb.sta0, m->vb.polls, m->vb.proof ?: '-',
		  m->baton_mv, m->ntc_ohm, m->bt.ret, m->bt.pre, m->bt.raw,
		  m->bt.sta0, m->bt.polls, m->bt.proof ?: '-',
		  m->temp_ret, m->adc_con0, m->ckpdn0, m->ckpdn1, m->sta0,
		  m->vbif28, m->regs_ok);
}

/*
 * The capacity to publish after a valid sample, from the one published
 * before: the first sample sets it; then it moves one percent per sample
 * toward the estimate from the averaged VBAT, up only with a charger, down
 * only without one. 0 % only after FORGE_BAT_EMPTY_SAMPLES samples in a row
 * with the average below FORGE_BAT_EMPTY_MV (not a single reading: a load
 * dip of a battery that still has charge would shut Android down early),
 * else at least 1 %. Caller holds forge_bat_mutex.
 */
static int forge_bat_filter(int vbat_mv, int chrdet, int cap)
{
	/* a charger plugged or pulled steps VBAT: restart the average */
	if (!forge_bat.have || chrdet != forge_bat.vavg_chrdet) {
		forge_bat.vavg = vbat_mv;
		forge_bat.vavg_chrdet = chrdet;
	} else {
		forge_bat.vavg += (vbat_mv - forge_bat.vavg) / 4;
	}
	forge_bat.est = forge_ocv_soc(forge_bat.vavg);
	if (!forge_bat.have)
		cap = forge_bat.est;
	else if (chrdet == 1 && forge_bat.est > cap)
		cap++;
	else if (chrdet != 1 && forge_bat.est < cap)
		cap--;
	forge_bat.have = true;
	forge_bat.low = forge_bat.vavg < FORGE_BAT_EMPTY_MV ?
			forge_bat.low + 1 : 0;
	if (forge_bat.low >= FORGE_BAT_EMPTY_SAMPLES)
		return 0;
	return cap < 1 ? 1 : cap;
}

static int forge_bat_status(int chrdet)
{
	/* Cable presence alone cannot prove a charging current. */
	return chrdet == 0 ? POWER_SUPPLY_STATUS_DISCHARGING :
		POWER_SUPPLY_STATUS_UNKNOWN;
}

static int forge_bat_get(struct power_supply *psy,
			 enum power_supply_property psp,
			 union power_supply_propval *val)
{
	unsigned long flags;
	bool fresh;
	int ret = 0;

	spin_lock_irqsave(&forge_bat_lock, flags);
	fresh = ktime_before(ktime_get_boottime(),
			     ktime_add_ms(forge_bat_pub.at, FORGE_BAT_STALE_MS));
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = forge_bat_pub.status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		/* Only a fresh, validated VBAT is evidence of presence. */
		if (fresh)
			val->intval = 1;
		else
			ret = -ENODATA;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		/* never an error: healthd reads a failed capacity as 0 % */
		val->intval = forge_bat_pub.cap;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (fresh)
			val->intval = forge_bat_pub.vbat_mv * 1000;
		else
			ret = -ENODATA;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (fresh && forge_bat_pub.temp_ok)
			val->intval = forge_bat_pub.temp_dc;
		else
			ret = -ENODATA;
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	spin_unlock_irqrestore(&forge_bat_lock, flags);
	return ret;
}

static int forge_usb_get(struct power_supply *psy,
			 enum power_supply_property psp,
			 union power_supply_propval *val)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&forge_bat_lock, flags);
	if (psp != POWER_SUPPLY_PROP_ONLINE)
		ret = -EOPNOTSUPP;
	else if (forge_bat_pub.chrdet < 0)
		ret = -ENODATA;
	else
		val->intval = forge_bat_pub.chrdet;
	spin_unlock_irqrestore(&forge_bat_lock, flags);
	return ret;
}

static enum power_supply_property forge_bat_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_property forge_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static const struct power_supply_desc forge_bat_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = forge_bat_props,
	.num_properties = ARRAY_SIZE(forge_bat_props),
	.get_property = forge_bat_get,
	/* TEMP may be -ENODATA; no thermal zone on top of it */
	.no_thermal = true,
};

/* CHRDET only: no charger type detection (BC1.2 needs PMIC writes) */
static const struct power_supply_desc forge_usb_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = forge_usb_props,
	.num_properties = ARRAY_SIZE(forge_usb_props),
	.get_property = forge_usb_get,
	.no_thermal = true,
};

static void forge_bat_register(void)
{
	struct power_supply *psy;

	psy = power_supply_register(NULL, &forge_bat_desc, NULL);
	if (IS_ERR(psy)) {
		forge_bat.reg_failed = true;
		pr_err("[FORGE_M681] battery: power_supply 'battery' not registered: %ld\n",
		       PTR_ERR(psy));
		return;
	}
	forge_bat_psy = psy;
	psy = power_supply_register(NULL, &forge_usb_desc, NULL);
	if (IS_ERR(psy))
		pr_err("[FORGE_M681] battery: power_supply 'usb' not registered: %ld\n",
		       PTR_ERR(psy));
	else
		forge_usb_psy = psy;
	pr_notice("[FORGE_M681] battery: registered, %d mV, capacity %d %%; OCV profile ID0 (D_ATL, 25 C) assumed, battery ID (AUXADC CH13) not read\n",
		  forge_bat_pub.vbat_mv, forge_bat_pub.cap);
}

static void forge_bat_update(int chrdet)
{
	struct forge_bat_sample m;
	int status = forge_bat_status(chrdet);
	int ret = forge_bat_measure(&m);
	int cap = forge_bat_pub.cap, old_cap = cap, est = 0;
	bool changed, usb_changed, empty = false;
	unsigned long flags;
	ktime_t now;

	mutex_lock(&forge_bat_mutex);
	/*
	 * Proof 'A' says the AUXADC converts, not that CH3 does: a CH3 path
	 * that stopped (lead, review of eb1923d1a) would keep an old BATON
	 * forever and hide a charger overheating. After FORGE_BAT_A_MAX valid
	 * samples in a row with only 'A' on the same raw value, the
	 * temperature is stale until an 'R' or 'V' proof comes back. A sample
	 * without a BATON conversion (VBAT or CH3 failed) leaves the count
	 * alone: TEMP is not published from it anyway. An 'A' on a new raw
	 * value starts again at 1: someone did convert CH3.
	 */
	if (!ret && !m.bt.ret) {
		if (m.bt.proof != 'A')
			forge_bat.a_run = 0;
		else if (m.bt.raw == forge_bat.a_raw)
			forge_bat.a_run++;
		else
			forge_bat.a_run = 1;
		forge_bat.a_raw = m.bt.raw;
		if (forge_bat.a_run >= FORGE_BAT_A_MAX && !m.temp_ret)
			m.temp_ret = -ESTALE;
	}
	forge_bat.last = m;
	/* log changes of the outcome, not every sample */
	if (ret != forge_bat.ret || (!ret && m.temp_ret != forge_bat.temp_ret))
		forge_bat_log_sample(ret ? "no sample" : "sample", ret, &m);
	forge_bat.ret = ret;
	forge_bat.temp_ret = m.temp_ret;
	if (ret) {
		forge_bat.failed++;
		/* "in a row" means valid samples in a row */
		forge_bat.low = 0;
	} else {
		forge_bat.ok++;
		cap = forge_bat_filter(m.vbat_mv, chrdet, cap);
		est = forge_bat.est;
		empty = !cap && old_cap;
	}
	mutex_unlock(&forge_bat_mutex);

	/*
	 * No valid sample for FORGE_BAT_LOST_MS (the AUXADC stopped converting,
	 * or battery_adc=0) and no charger: the capacity is only the last
	 * value, and a draining battery would never reach the 0 % rule. Say so:
	 * STATUS UNKNOWN and an error in the log. The capacity is not changed,
	 * nothing is made up; cable presence does not
	 * prove charging, and its status remains UNKNOWN. CLOCK_BOOTTIME, so
	 * a sleep counts as time without a sample.
	 */
	now = ktime_get_boottime();
	if (ret && forge_bat.have && chrdet != 1 &&
	    ktime_after(now, ktime_add_ms(forge_bat_pub.at, FORGE_BAT_LOST_MS))) {
		static DEFINE_RATELIMIT_STATE(lost_rs, FORGE_BAT_LOST_MS / 1000 * HZ, 1);

		status = POWER_SUPPLY_STATUS_UNKNOWN;
		if (__ratelimit(&lost_rs))
			pr_err("[FORGE_M681] battery: no valid sample for %lld s and no charger (last %d): capacity held at %d %%, status unknown\n",
			       ktime_ms_delta(now, forge_bat_pub.at) / 1000,
			       ret, cap);
	}

	spin_lock_irqsave(&forge_bat_lock, flags);
	changed = forge_bat_pub.status != status || forge_bat_pub.cap != cap;
	usb_changed = forge_bat_pub.chrdet != chrdet;
	forge_bat_pub.status = status;
	forge_bat_pub.chrdet = chrdet;
	if (!ret) {
		forge_bat_pub.cap = cap;
		forge_bat_pub.vbat_mv = m.vbat_mv;
		forge_bat_pub.temp_ok = !m.temp_ret;
		forge_bat_pub.temp_dc = m.temp_dc;
		forge_bat_pub.at = now;
	}
	spin_unlock_irqrestore(&forge_bat_lock, flags);

	if (empty)
		pr_notice("[FORGE_M681] battery: %d samples in a row below %d mV (last %d mV): capacity 0 %%\n",
			  FORGE_BAT_EMPTY_SAMPLES, FORGE_BAT_EMPTY_MV,
			  m.vbat_mv);
	else if (!ret && forge_bat_psy && cap != old_cap)
		pr_info("[FORGE_M681] battery: capacity %d -> %d %% (%d mV, average %d, estimate %d %%, charger %d)\n",
			old_cap, cap, m.vbat_mv, forge_bat.vavg, est, chrdet);

	if (!ret && !forge_bat_psy && !forge_bat.reg_failed)
		forge_bat_register();
	else if (forge_bat_psy && changed)
		power_supply_changed(forge_bat_psy);
	if (forge_usb_psy && usb_changed)
		power_supply_changed(forge_usb_psy);
}

static void forge_chg_fn(struct work_struct *work)
{
	u32 con0;
	int chrdet = pwrap_read(FORGE_MT6351_CHR_CON0, &con0) ? -1 :
		     (con0 >> 5) & 1;
	bool full = !(forge_chg_ticks % FORGE_CHG_FULL_TICKS) ||
		    chrdet != forge_chg_chrdet;

	/* no battery yet: healthd looks for one only when it starts, so
	 * retry every tick for the first minute */
	if (forge_bat_enable && !forge_bat_psy && !forge_bat.reg_failed &&
	    forge_chg_ticks < FORGE_BAT_FAST_TICKS)
		full = true;
	forge_chg_ticks++;
	if (full && forge_bat_enable)
		forge_bat_update(chrdet);
	forge_chg_chrdet = chrdet;
	queue_delayed_work(system_freezable_power_efficient_wq, &forge_chg_work,
			   msecs_to_jiffies(FORGE_CHG_TICK_MS));
}

static void forge_temp_str(char *buf, size_t len, int dc)
{
	snprintf(buf, len, "%s%d.%d C", dc < 0 ? "-" : "", abs(dc) / 10,
		 abs(dc) % 10);
}

static void forge_bat_show(struct seq_file *m)
{
	const struct forge_bat_sample *l = &forge_bat.last;
	unsigned long flags;
	int cap, status, chrdet;
	ktime_t at;
	char t[16];

	if (!forge_bat_enable) {
		seq_puts(m, "battery: off (forge_chg_observe.battery=0)\n");
		return;
	}
	spin_lock_irqsave(&forge_bat_lock, flags);
	cap = forge_bat_pub.cap;
	status = forge_bat_pub.status;
	chrdet = forge_bat_pub.chrdet;
	at = forge_bat_pub.at;
	spin_unlock_irqrestore(&forge_bat_lock, flags);
	mutex_lock(&forge_bat_mutex);
	forge_temp_str(t, sizeof(t), l->temp_dc);
	seq_printf(m, "battery: %s, capacity %d %% (estimate %d %%, average %d mV, below %d mV: %d/%d), status %d, charger %d; samples ok %u failed %u, last valid %lld s ago; OCV profile ID0 (D_ATL, 25 C) assumed\n",
		   forge_bat_psy ? "registered" :
		   forge_bat.reg_failed ? "registration failed" : "not registered",
		   cap, forge_bat.est, forge_bat.vavg, FORGE_BAT_EMPTY_MV,
		   forge_bat.low, FORGE_BAT_EMPTY_SAMPLES, status, chrdet,
		   forge_bat.ok, forge_bat.failed,
		   forge_bat.have ?
		   ktime_ms_delta(ktime_get_boottime(), at) / 1000 : -1LL);
	seq_printf(m, "last sample %d: VBAT %d mV (CH1 %d: 0x%04x -> 0x%04x, STA0 0x%04x, %d polls, proof %c); BATON %d mV, %d ohm, %s (CH3 %d: 0x%04x -> 0x%04x, STA0 0x%04x, %d polls, proof %c, only 'A' %u/%u, temp %d)\n",
		   forge_bat.ret, l->vbat_mv, l->vb.ret, l->vb.pre, l->vb.raw,
		   l->vb.sta0, l->vb.polls, l->vb.proof ?: '-',
		   l->baton_mv, l->ntc_ohm, l->temp_ret ? "no temp" : t,
		   l->bt.ret, l->bt.pre, l->bt.raw, l->bt.sta0, l->bt.polls,
		   l->bt.proof ?: '-', forge_bat.a_run, FORGE_BAT_A_MAX,
		   l->temp_ret);
	seq_printf(m, "auxadc (read only): CON0 0x%04x (CK_AON %u) TOP_CKPDN_CON0 0x%04x (SMPS_CK_PDN %u) CON1 0x%04x (CK_PDN %u) STA0 0x%04x VBIF28_CON0 0x%04x (enabled %u) rd %d\n",
		   l->adc_con0, (l->adc_con0 >> 15) & 1, l->ckpdn0,
		   (l->ckpdn0 >> 9) & 1, l->ckpdn1, (l->ckpdn1 >> 10) & 1,
		   l->sta0, l->vbif28, (l->vbif28 >> 15) & 1, l->regs_ok);
	mutex_unlock(&forge_bat_mutex);
}

static int forge_chg_show(struct seq_file *m, void *v)
{
	forge_bat_show(m);
	return 0;
}

static int forge_chg_open(struct inode *inode, struct file *file)
{
	return single_open(file, forge_chg_show, NULL);
}

static const struct file_operations forge_chg_fops = {
	.open = forge_chg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init forge_chg_observe_init(void)
{
	/* M681 SM5414/MT6351 ISENSE path only: the common M3 Note kernel also
	 * runs on L681 (BQ24196), whose battery this observer must not own. */
	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC) &&
	    m3note_board_id() != M3NOTE_BOARD_M681) {
		pr_notice("[FORGE_M681] battery: not an M681 board, monitor off\n");
		return 0;
	}
	if (!forge_bat_enable)
		pr_notice("[FORGE_M681] battery: monitor off (forge_chg_observe.battery=0)\n");
	if (!proc_create("m681_battery", 0444, NULL, &forge_chg_fops))
		pr_notice("[FORGE_M681] charger: /proc/m681_battery not created\n");
	INIT_DELAYED_WORK(&forge_chg_work, forge_chg_fn);
	/* Publish a valid first sample before userspace scans power supplies. */
	forge_chg_fn(&forge_chg_work.work);
	return 0;
}
late_initcall(forge_chg_observe_init);
