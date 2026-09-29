/*
 * m681 4.9: read-only charger observer.
 *
 * 4.9 has no battery or charger driver (A13y: "healthd: No battery devices
 * found"), so it is unknown whether the phone charges during a run. Before
 * anything writes to the charger, look at it.
 *
 * The charger is an SM5414 on i2c1 (11008000.i2c) at 0x49 (Meizu OSC m681
 * 3.10.72 sm5414.c; 4.4 port and LANE_CHARGER_20260715.md). Stock DCT pins:
 * nSHDN GPIO4 (1 = on), CHGEN GPIO115 (0 = charge), nINT GPIO101. Nothing here
 * writes: INT1..INT3 are clear-on-read and skipped, the pins are read back and
 * never driven. From the MT6351, over the read-only pwrap where it exists:
 * RGS_CHRDET (CHR_CON0 bit 5) and the last AUXADC channel 0 (BATSNS) results.
 * The PMIC refreshes those only when someone requests a conversion, so a value
 * that never changes may be stale; RDY is printed with it.
 *
 * Logged once at late_initcall and again whenever anything changes (checked
 * every 30 s); /proc/m681_charger dumps the current state.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <mt-plat/mtk_gpio.h>

#define FORGE_SM5414_ADDR	0x49
#define FORGE_SM5414_FIRST	0x03	/* INTMASK1; 0x00-0x02 are clear-on-read */
#define FORGE_SM5414_LAST	0x0D	/* CHGCTRL5 */
#define FORGE_SM5414_NREG	(FORGE_SM5414_LAST - FORGE_SM5414_FIRST + 1)
#define FORGE_GPIO_SHDN		4
#define FORGE_GPIO_CHGEN	115
#define FORGE_GPIO_NINT		101
#define FORGE_MT6351_CHR_CON0	0x0F78	/* RGS_CHRDET bit 5 */
#define FORGE_MT6351_AUXADC0	0x0E00	/* CH0 BATSNS: [14:0] value, [15] RDY */
#define FORGE_MT6351_AUXADC23	0x0E2E	/* CH0 BATSNS requested by the AP */
#define FORGE_CHG_POLL_MS	30000

extern s32 pwrap_read(u32 adr, u32 *rdata);

struct forge_chg_state {
	int i2c_ok;
	u8 reg[FORGE_SM5414_NREG];
	int shdn_dir, shdn_out, chgen_dir, chgen_out, nint_in;
	int pwrap_ok;
	u32 chr_con0, adc0, adc23;
};

static struct i2c_client *forge_sm5414;
static struct forge_chg_state forge_chg_last;
static bool forge_chg_logged;
static struct delayed_work forge_chg_work;

static unsigned int forge_batsns_mv(u32 adc)
{
	/* MT6351 BATSNS: 15 bits, 1.8 V reference, 1/3 divider */
	return (adc & 0x7fff) * 3 * 1800 / 32768;
}

static void forge_chg_read(struct forge_chg_state *s)
{
	int i, v;

	memset(s, 0, sizeof(*s));
	if (forge_sm5414) {
		s->i2c_ok = 1;
		for (i = 0; i < FORGE_SM5414_NREG; i++) {
			v = i2c_smbus_read_byte_data(forge_sm5414,
						     FORGE_SM5414_FIRST + i);
			if (v < 0) {
				s->i2c_ok = v;
				break;
			}
			s->reg[i] = v;
		}
	}
	s->shdn_dir = mt_get_gpio_dir(FORGE_GPIO_SHDN | 0x80000000);
	s->shdn_out = mt_get_gpio_out(FORGE_GPIO_SHDN | 0x80000000);
	s->chgen_dir = mt_get_gpio_dir(FORGE_GPIO_CHGEN | 0x80000000);
	s->chgen_out = mt_get_gpio_out(FORGE_GPIO_CHGEN | 0x80000000);
	s->nint_in = mt_get_gpio_in(FORGE_GPIO_NINT | 0x80000000);
	s->pwrap_ok = !pwrap_read(FORGE_MT6351_CHR_CON0, &s->chr_con0) &&
		      !pwrap_read(FORGE_MT6351_AUXADC0, &s->adc0) &&
		      !pwrap_read(FORGE_MT6351_AUXADC23, &s->adc23);
}

static void forge_chg_print(struct seq_file *m, const struct forge_chg_state *s)
{
	char regs[3 * FORGE_SM5414_NREG + 1];
	int i;

	for (i = 0; i < FORGE_SM5414_NREG; i++)
		snprintf(regs + 3 * i, 4, " %02x", s->reg[i]);
	if (m) {
		seq_printf(m, "sm5414 i2c=%d regs 0x03..0x0d:%s\n", s->i2c_ok, regs);
		seq_printf(m, "gpio SHDN(4) dir=%d out=%d CHGEN(115) dir=%d out=%d nINT(101) in=%d\n",
			   s->shdn_dir, s->shdn_out, s->chgen_dir, s->chgen_out,
			   s->nint_in);
		seq_printf(m, "pmic pwrap=%d CHR_CON0=0x%04x (chrdet %u) AUXADC0=0x%04x (%u mV rdy %u) AUXADC23=0x%04x (%u mV rdy %u)\n",
			   s->pwrap_ok, s->chr_con0, (s->chr_con0 >> 5) & 1,
			   s->adc0, forge_batsns_mv(s->adc0), s->adc0 >> 15,
			   s->adc23, forge_batsns_mv(s->adc23), s->adc23 >> 15);
		return;
	}
	pr_notice("[FORGE_M681] charger: sm5414 i2c=%d regs 0x03..0x0d:%s | SHDN %d/%d CHGEN %d/%d nINT %d | pwrap=%d chrdet %u BATSNS %u mV (rdy %u) AP %u mV (rdy %u)\n",
		  s->i2c_ok, regs, s->shdn_dir, s->shdn_out, s->chgen_dir,
		  s->chgen_out, s->nint_in, s->pwrap_ok,
		  (s->chr_con0 >> 5) & 1, forge_batsns_mv(s->adc0),
		  s->adc0 >> 15, forge_batsns_mv(s->adc23), s->adc23 >> 15);
}

static void forge_chg_fn(struct work_struct *work)
{
	struct forge_chg_state s;

	forge_chg_read(&s);
	if (!forge_chg_logged || memcmp(&s, &forge_chg_last, sizeof(s))) {
		forge_chg_print(NULL, &s);
		forge_chg_last = s;
		forge_chg_logged = true;
	}
	queue_delayed_work(system_freezable_power_efficient_wq, &forge_chg_work,
			   msecs_to_jiffies(FORGE_CHG_POLL_MS));
}

static int forge_chg_show(struct seq_file *m, void *v)
{
	struct forge_chg_state s;

	forge_chg_read(&s);
	forge_chg_print(m, &s);
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
	struct i2c_adapter *adap = NULL;
	int nr;

	/* i2c1 by its register base, whatever number the adapter got */
	for (nr = 0; nr < 8 && !adap; nr++) {
		adap = i2c_get_adapter(nr);
		if (adap && !strstr(adap->name, "11008000")) {
			i2c_put_adapter(adap);
			adap = NULL;
		}
	}
	if (adap) {
		forge_sm5414 = i2c_new_dummy(adap, FORGE_SM5414_ADDR);
		pr_notice("[FORGE_M681] charger: observing sm5414 on %s @0x%02x (%s)\n",
			  adap->name, FORGE_SM5414_ADDR,
			  forge_sm5414 ? "ok" : "busy");
	} else {
		pr_notice("[FORGE_M681] charger: no 11008000.i2c adapter, SM5414 not read\n");
	}
	if (!proc_create("m681_charger", 0444, NULL, &forge_chg_fops))
		pr_notice("[FORGE_M681] charger: /proc/m681_charger not created\n");
	INIT_DELAYED_WORK(&forge_chg_work, forge_chg_fn);
	queue_delayed_work(system_freezable_power_efficient_wq, &forge_chg_work, 0);
	return 0;
}
late_initcall(forge_chg_observe_init);
