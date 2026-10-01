#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/time.h>
#include "kd_flashlight.h"
#include <asm/io.h>
#include <asm/uaccess.h>
#include "kd_camera_typedef.h"
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/leds.h>



/******************************************************************************
 * Debug configuration
******************************************************************************/
/* availible parameter */
/* ANDROID_LOG_ASSERT */
/* ANDROID_LOG_ERROR */
/* ANDROID_LOG_WARNING */
/* ANDROID_LOG_INFO */
/* ANDROID_LOG_DEBUG */
/* ANDROID_LOG_VERBOSE */

#define TAG_NAME "[leds_strobe.c]"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
#define PK_DBG_FUNC(fmt, arg...)    pr_debug(TAG_NAME "%s: " fmt, __func__ , ##arg)

/*#define DEBUG_LEDS_STROBE*/
#ifdef DEBUG_LEDS_STROBE
#define PK_DBG PK_DBG_FUNC
#else
#define PK_DBG(a, ...)
#endif

/*
 * The flash path has no other trace in dmesg: every PK_DBG above compiles to
 * nothing unless DEBUG_LEDS_STROBE is defined, and the flashlight core's logI()
 * is disabled the same way. Keep the enable/disable path on pr_info so a torch
 * press is always visible in dmesg.
 */
#define PK_LOG(fmt, arg...)         pr_info(TAG_NAME "%s: " fmt, __func__ , ##arg)
#define PK_ERR(fmt, arg...)         pr_err(TAG_NAME "%s: " fmt, __func__ , ##arg)

/******************************************************************************
 * local variables
******************************************************************************/

static DEFINE_SPINLOCK(g_strobeSMPLock);	/* cotta-- SMP proection */


static u32 strobe_Res;
static u32 strobe_Timeus;
static BOOL g_strobe_On;

static int g_duty = -1;
static int g_timeOutTimeMs;

static DEFINE_MUTEX(g_strobeSem);


#define STROBE_DEVICE_ID 0xC6


static struct work_struct workTimeOut;

/*
 * The chip on i2c 1-0063 is an SY7806 class dual channel flash driver, not an
 * LM3642: the stock kernel keeps the "leds-LM3642" i2c driver name and the
 * LM3642_{read,write}_reg helpers, but its live code path (SY7806_set_torch_mode
 * / SY7806_set_flash_mode / SY7806_close in the same file) programs the SY7806
 * register map below and drives an HWEN pin, while the LM3642 register sequence
 * (regs 0x09/0x0A) is dead code with no callers at all.
 *
 * Registers (SY7806 / AW3644 compatible):
 */
#define SY7806_REG_ENABLE	0x01
#define SY7806_REG_FLASH_LED1	0x03
#define SY7806_REG_FLASH_LED2	0x04
#define SY7806_REG_TORCH_LED1	0x05
#define SY7806_REG_TORCH_LED2	0x06
#define SY7806_REG_TIMING	0x08
#define SY7806_REG_FLAG1	0x0B
#define SY7806_REG_DEV_ID	0x0C

/* Enable register: bits [3:2] mode, bits [1:0] per-LED enable */
#define SY7806_EN_OFF		0x00
#define SY7806_EN_TORCH_BOTH	0x0B
#define SY7806_EN_FLASH_BOTH	0x0F

/* Timing register value used by stock (maximum flash timeout) */
#define SY7806_TIMING_DEFAULT	0x0F

/*
 * Brightness tables lifted verbatim from the stock kernel image
 * (/home/valakas/m5c/kernel-reverse/vmlinux.elf, .kernel @ 0xffffffc000b961a8).
 * The HAL duty index selects the entry; duty <= 3 means torch, above that the
 * flash table is used. Stock keeps LED1 and LED2 tables identical.
 */
static const u8 sy7806_torch_duty[] = {
	0x23, 0x31, 0x6A, 0x7F
};

static const u8 sy7806_flash_duty[] = {
	0x03, 0x08, 0x0C, 0x10, 0x19, 0x21, 0x2A, 0x32, 0x3B,
	0x43, 0x4C, 0x54, 0x5D, 0x65, 0x6E, 0x77, 0x7F
};

#define SY7806_TORCH_DUTY_MAX	((int)ARRAY_SIZE(sy7806_torch_duty) - 1)
#define SY7806_FLASH_DUTY_MAX	((int)ARRAY_SIZE(sy7806_flash_duty) - 1)

static int g_bLtVersion;

/*****************************************************************************
Functions
*****************************************************************************/
static void work_timeOutFunc(struct work_struct *data);

static struct i2c_client *LM3642_i2c_client;




struct LM3642_platform_data {
	u8 torch_pin_enable;	/* 1:  TX1/TORCH pin isa hardware TORCH enable */
	u8 pam_sync_pin_enable;	/* 1:  TX2 Mode The ENVM/TX2 is a PAM Sync. on input */
	u8 thermal_comp_mode_enable;	/* 1: LEDI/NTC pin in Thermal Comparator Mode */
	u8 strobe_pin_disable;	/* 1 : STROBE Input disabled */
	u8 vout_mode_enable;	/* 1 : Voltage Out Mode enable */
};

struct LM3642_chip_data {
	struct i2c_client *client;

	/* struct led_classdev cdev_flash; */
	/* struct led_classdev cdev_torch; */
	/* struct led_classdev cdev_indicator; */

	struct LM3642_platform_data *pdata;
	struct mutex lock;

	u8 last_flag;
	u8 no_pdata;
};

static int LM3642_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret = 0;
	struct LM3642_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	ret = i2c_smbus_write_byte_data(client, reg, val);
	mutex_unlock(&chip->lock);

	if (ret < 0)
		PK_DBG("failed writing at 0x%02x\n", reg);
	return ret;
}

static int LM3642_read_reg(struct i2c_client *client, u8 reg)
{
	int val = 0;
	struct LM3642_chip_data *chip = i2c_get_clientdata(client);

	mutex_lock(&chip->lock);
	val = i2c_smbus_read_byte_data(client, reg);
	mutex_unlock(&chip->lock);


	return val;
}




static int LM3642_chip_init(struct LM3642_chip_data *chip)
{


	return 0;
}

static int LM3642_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct LM3642_chip_data *chip;
	struct LM3642_platform_data *pdata = client->dev.platform_data;

	int err = -1;

	PK_DBG("LM3642_probe start--->.\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		PK_DBG("LM3642 i2c functionality check fail.\n");
		return err;
	}

	chip = kzalloc(sizeof(struct LM3642_chip_data), GFP_KERNEL);
	chip->client = client;

	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);

	if (pdata == NULL) {	/* values are set to Zero. */
		PK_DBG("LM3642 Platform data does not exist\n");
		pdata = kzalloc(sizeof(struct LM3642_platform_data), GFP_KERNEL);
		chip->pdata = pdata;
		chip->no_pdata = 1;
	}

	chip->pdata = pdata;
	if (LM3642_chip_init(chip) < 0)
		goto err_chip_init;

	LM3642_i2c_client = client;
	PK_DBG("LM3642 Initializing is done\n");

	return 0;

err_chip_init:
	i2c_set_clientdata(client, NULL);
	kfree(chip);
	PK_DBG("LM3642 probe is failed\n");
	return -ENODEV;
}

static int LM3642_remove(struct i2c_client *client)
{
	struct LM3642_chip_data *chip = i2c_get_clientdata(client);

	if (chip->no_pdata)
		kfree(chip->pdata);
	kfree(chip);
	return 0;
}


#define LM3642_NAME "leds-LM3642"
static const struct i2c_device_id LM3642_id[] = {
	{LM3642_NAME, 0},
	{}
};

#ifdef CONFIG_OF
static const struct of_device_id LM3642_of_match[] = {
	{.compatible = "mediatek,strobe_main"},
	{},
};
#endif

static struct i2c_driver LM3642_i2c_driver = {
	.driver = {
		   .name = LM3642_NAME,
#ifdef CONFIG_OF
		   .of_match_table = LM3642_of_match,
#endif
		   },
	.probe = LM3642_probe,
	.remove = LM3642_remove,
	.id_table = LM3642_id,
};
static int __init LM3642_init(void)
{
	PK_DBG("LM3642_init\n");
	return i2c_add_driver(&LM3642_i2c_driver);
}

static void __exit LM3642_exit(void)
{
	i2c_del_driver(&LM3642_i2c_driver);
}


module_init(LM3642_init);
module_exit(LM3642_exit);

MODULE_DESCRIPTION("Flash driver for LM3642");
MODULE_AUTHOR("pw <pengwei@mediatek.com>");
MODULE_LICENSE("GPL v2");

int readReg(int reg)
{

	int val;

	val = LM3642_read_reg(LM3642_i2c_client, reg);
	return (int)val;
}

static int sy7806_write(u8 reg, u8 val)
{
	int ret;

	if (LM3642_i2c_client == NULL) {
		PK_ERR("no i2c client\n");
		return -ENODEV;
	}

	ret = LM3642_write_reg(LM3642_i2c_client, reg, val);
	if (ret < 0)
		PK_ERR("write reg 0x%02x = 0x%02x failed, ret = %d\n", reg, val, ret);
	return ret;
}

int FL_Enable(void)
{
	int duty = g_duty;
	int ret;

	if (duty < 0)
		duty = 0;

	/*
	 * HWEN must be high before any i2c access: with HWEN low the chip is in
	 * shutdown and does not answer on the bus at all.
	 */
	ret = flashlight_gpio_set(FLASHLIGHT_PIN_HWEN, FLASHLIGHT_PIN_HIGH);
	if (ret)
		PK_ERR("HWEN not raised (ret = %d), the chip will not answer\n", ret);

	sy7806_write(SY7806_REG_TIMING, SY7806_TIMING_DEFAULT);

	if (duty <= SY7806_TORCH_DUTY_MAX) {
		u8 level = sy7806_torch_duty[duty];

		sy7806_write(SY7806_REG_TORCH_LED1, level);
		sy7806_write(SY7806_REG_TORCH_LED2, level);
		sy7806_write(SY7806_REG_ENABLE, SY7806_EN_TORCH_BOTH);
		PK_LOG("torch on, duty = %d, level = 0x%02x\n", duty, level);
	} else {
		u8 level;

		if (duty > SY7806_FLASH_DUTY_MAX)
			duty = SY7806_FLASH_DUTY_MAX;
		level = sy7806_flash_duty[duty];

		sy7806_write(SY7806_REG_FLASH_LED1, level);
		sy7806_write(SY7806_REG_FLASH_LED2, level);
		sy7806_write(SY7806_REG_ENABLE, SY7806_EN_FLASH_BOTH);
		PK_LOG("flash on, duty = %d, level = 0x%02x\n", duty, level);
	}

	PK_LOG("readback enable = 0x%02x flag1 = 0x%02x devid = 0x%02x\n",
	       readReg(SY7806_REG_ENABLE), readReg(SY7806_REG_FLAG1),
	       readReg(SY7806_REG_DEV_ID));

	return 0;
}



int FL_Disable(void)
{
	/* HWEN high so the write lands, then drop the chip back to shutdown. */
	flashlight_gpio_set(FLASHLIGHT_PIN_HWEN, FLASHLIGHT_PIN_HIGH);
	sy7806_write(SY7806_REG_ENABLE, SY7806_EN_OFF);
	flashlight_gpio_set(FLASHLIGHT_PIN_HWEN, FLASHLIGHT_PIN_LOW);

	PK_LOG("off\n");
	return 0;
}

int FL_dim_duty(kal_uint32 duty)
{
	PK_LOG("duty = %d\n", (int)duty);
	g_duty = duty;
	return 0;
}




int FL_Init(void)
{
	int devId;
	int ret;

	ret = flashlight_gpio_set(FLASHLIGHT_PIN_HWEN, FLASHLIGHT_PIN_HIGH);

	sy7806_write(SY7806_REG_ENABLE, SY7806_EN_OFF);
	sy7806_write(SY7806_REG_TIMING, SY7806_TIMING_DEFAULT);

	devId = readReg(SY7806_REG_DEV_ID);
	g_bLtVersion = 0;

	PK_LOG("hwen ret = %d, devid reg 0x0c = 0x%02x, flag1 = 0x%02x\n",
	       ret, devId, readReg(SY7806_REG_FLAG1));

	flashlight_gpio_set(FLASHLIGHT_PIN_HWEN, FLASHLIGHT_PIN_LOW);

	return 0;
}


int FL_Uninit(void)
{
	FL_Disable();
	return 0;
}

/*
 * Shared entry points so the strobeId == 2 handler
 * (strobe_main_sid2_part1.c) drives the very same chip. The MTK camera HAL
 * addresses the main sensor flash as strobe id 1 or id 2 depending on the
 * blob; stock only programs the chip from the id 2 path, which is easy to miss.
 */
int strobe_flash_set_duty(int duty)
{
	return FL_dim_duty(duty);
}
EXPORT_SYMBOL(strobe_flash_set_duty);

int strobe_flash_enable(void)
{
	return FL_Enable();
}
EXPORT_SYMBOL(strobe_flash_enable);

int strobe_flash_disable(void)
{
	return FL_Disable();
}
EXPORT_SYMBOL(strobe_flash_disable);

/*****************************************************************************
User interface
*****************************************************************************/

static void work_timeOutFunc(struct work_struct *data)
{
	FL_Disable();
	PK_DBG("ledTimeOut_callback\n");
}



enum hrtimer_restart ledTimeOutCallback(struct hrtimer *timer)
{
	schedule_work(&workTimeOut);
	return HRTIMER_NORESTART;
}

static struct hrtimer g_timeOutTimer;
void timerInit(void)
{
	INIT_WORK(&workTimeOut, work_timeOutFunc);
	g_timeOutTimeMs = 1000;
	hrtimer_init(&g_timeOutTimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	g_timeOutTimer.function = ledTimeOutCallback;
}



static int constant_flashlight_ioctl(unsigned int cmd, unsigned long arg)
{
	int i4RetValue = 0;
	int ior_shift;
	int iow_shift;
	int iowr_shift;

	ior_shift = cmd - (_IOR(FLASHLIGHT_MAGIC, 0, int));
	iow_shift = cmd - (_IOW(FLASHLIGHT_MAGIC, 0, int));
	iowr_shift = cmd - (_IOWR(FLASHLIGHT_MAGIC, 0, int));
/*	PK_DBG
	    ("LM3642 constant_flashlight_ioctl() line=%d ior_shift=%d, iow_shift=%d iowr_shift=%d arg=%d\n",
	     __LINE__, ior_shift, iow_shift, iowr_shift, (int)arg);
*/
	switch (cmd) {

	case FLASH_IOC_SET_TIME_OUT_TIME_MS:
		PK_LOG("FLASH_IOC_SET_TIME_OUT_TIME_MS: %d\n", (int)arg);
		g_timeOutTimeMs = arg;
		break;


	case FLASH_IOC_SET_DUTY:
		PK_LOG("FLASHLIGHT_DUTY: %d\n", (int)arg);
		FL_dim_duty(arg);
		break;


	case FLASH_IOC_SET_STEP:
		PK_LOG("FLASH_IOC_SET_STEP: %d\n", (int)arg);

		break;

	case FLASH_IOC_SET_ONOFF:
		PK_LOG("FLASHLIGHT_ONOFF: %d\n", (int)arg);
		if (arg == 1) {

			int s;
			int ms;

			if (g_timeOutTimeMs > 1000) {
				s = g_timeOutTimeMs / 1000;
				ms = g_timeOutTimeMs - s * 1000;
			} else {
				s = 0;
				ms = g_timeOutTimeMs;
			}

			if (g_timeOutTimeMs != 0) {
				ktime_t ktime;

				ktime = ktime_set(s, ms * 1000000);
				hrtimer_start(&g_timeOutTimer, ktime, HRTIMER_MODE_REL);
			}
			FL_Enable();
		} else {
			FL_Disable();
			hrtimer_cancel(&g_timeOutTimer);
		}
		break;
	default:
		PK_LOG("no such command 0x%x\n", cmd);
		i4RetValue = -EPERM;
		break;
	}
	return i4RetValue;
}




static int constant_flashlight_open(void *pArg)
{
	int i4RetValue = 0;

	PK_LOG("open, strobe_Res = %d\n", strobe_Res);

	if (0 == strobe_Res) {
		FL_Init();
		timerInit();
	}
	PK_DBG("constant_flashlight_open line=%d\n", __LINE__);
	spin_lock_irq(&g_strobeSMPLock);


	if (strobe_Res) {
		PK_DBG(" busy!\n");
		i4RetValue = -EBUSY;
	} else {
		strobe_Res += 1;
	}


	spin_unlock_irq(&g_strobeSMPLock);
	PK_DBG("constant_flashlight_open line=%d\n", __LINE__);

	return i4RetValue;

}


static int constant_flashlight_release(void *pArg)
{
	PK_DBG(" constant_flashlight_release\n");

	if (strobe_Res) {
		spin_lock_irq(&g_strobeSMPLock);

		strobe_Res = 0;
		strobe_Timeus = 0;

		/* LED On Status */
		g_strobe_On = FALSE;

		spin_unlock_irq(&g_strobeSMPLock);

		FL_Uninit();
	}

	PK_DBG(" Done\n");

	return 0;

}


FLASHLIGHT_FUNCTION_STRUCT constantFlashlightFunc = {
	constant_flashlight_open,
	constant_flashlight_release,
	constant_flashlight_ioctl
};


MUINT32 constantFlashlightInit(PFLASHLIGHT_FUNCTION_STRUCT *pfFunc)
{
	if (pfFunc != NULL)
		*pfFunc = &constantFlashlightFunc;
	return 0;
}



/* LED flash control for high current capture mode*/
ssize_t strobe_VDIrq(void)
{

	return 0;
}
EXPORT_SYMBOL(strobe_VDIrq);
