/*
 * cam_cal (module calibration) provider for the Meizu m5c main camera (S5K4H8).
 *
 * forge (m5c), 2026-08-17.
 *
 * The camera HAL blobs of this phone read per-module calibration through a
 * character device whose name is fixed inside libcameracustom.so
 * (S5K4H8_ST_CAM_CALDeviceName() and friends return "S5K4H8_ST_OTP" etc.).
 * Without those nodes the HAL logs
 *   CamCal: ----error: can't open CAM_CAL /dev/S5K4H8_..._OTP----
 *   CamCal: Return ERROR ERR_NO_SHADING
 * and programs a garbage lens-shading table, which shows up as large smooth
 * rainbow blotches over otherwise correct luminance.
 *
 * The data layout, the EEPROM addresses and the i2c parameters below were
 * taken byte-for-byte out of the stock Flyme kernel image
 * (s5k4h8_{ofilm,st,holitech,sunwin}_otp_cali at 0xffffffc0005c0844 /
 * 0xffffffc0005c7474 / 0xffffffc0005ce13c / 0xffffffc0005d4d9c and their
 * private eeprom_gt24c64a_read8 helpers), not reverse-engineered by hand.
 *
 * Layout of the blob handed to userspace (MTK "MTK format" cam_cal, same
 * shape as cam_cal/src/mt6735/imx135_otp):
 *   [0]        flag        = 1
 *   [1..4]     CaliVer     = 63 69 68 63
 *   [5..6]     SerialNum   <- EEPROM 0x44,0x45
 *   [7..8]     unused (left zero by the stock driver as well)
 *   [9..12]    unit AWB    <- EEPROM 0x2C..0x2F  (R/G and B/G, u16 LE each)
 *   [13..16]   golden AWB  <- EEPROM 0x32..0x35
 *   [17..18]   AfInfinite  <- EEPROM 0x42,0x43
 *   [19..20]   AfMacro     <- EEPROM 0x46,0x47
 *   [21..22]   LscSize     = 0x074C (1868)
 *   [23..1890] Lsc[1868]   <- EEPROM 0x51..0x7BC
 */

#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#ifdef CONFIG_COMPAT
#include <linux/compat.h>
#endif

#include "cam_cal.h"
#include "cam_cal_define.h"
#include "s5k4h8_otp_cal.h"

#define PFX "S5K4H8_OTP"
#define OTP_LOG(fmt, args...) pr_info("[%s] " fmt, PFX, ##args)
#define OTP_ERR(fmt, args...) pr_err("[%s] " fmt, PFX, ##args)

extern int iReadRegI2C(u8 *a_pSendData, u16 a_sizeSendData, u8 *a_pRecvData,
		       u16 a_sizeRecvData, u16 i2cId);
extern void kdSetI2CSpeed(u16 i2cSpeed);

/* GT24C64A on the camera i2c bus; stock talks to it at 300 kHz. */
#define S5K4H8_EEPROM_WRITE_ID	0xA0
#define S5K4H8_EEPROM_SPEED	300

#define S5K4H8_EEPROM_MODULE_ID_ADDR	0x0001

#define S5K4H8_LSC_EEPROM_ADDR	0x0051
#define S5K4H8_LSC_SIZE		1868		/* 0x074C */
#define S5K4H8_LSC_OFFSET	23
/* Data[0..1890] is what stock fills; keep some slack and hand out zeroes. */
#define S5K4H8_OTP_BUF_SIZE	2048

struct s5k4h8_otp_dev {
	const char *name;	/* /dev node name; must match the HAL blob */
	u8 data[S5K4H8_OTP_BUF_SIZE];
	bool valid;
	dev_t devno;
	struct cdev *cdev;
	struct class *cls;
	u32 opened;
};

static struct s5k4h8_otp_dev s5k4h8_otp[S5K4H8_MODULE_NUM] = {
	[S5K4H8_MODULE_OFILM]    = { .name = "S5K4H8_OFILM_OTP" },
	[S5K4H8_MODULE_ST]       = { .name = "S5K4H8_ST_OTP" },
	[S5K4H8_MODULE_HOLITECH] = { .name = "S5K4H8_HOLITECH_OTP" },
	[S5K4H8_MODULE_SUNWIN]   = { .name = "S5K4H8_SUNWIN_OTP" },
};

static DEFINE_SPINLOCK(s5k4h8_otp_lock);

/*
 * ---------------------------------------------------------------- EEPROM read
 */

/* Verbatim port of the stock eeprom_gt24c64a_read8(): 2-byte big-endian
 * address, one data byte, i2c write id 0xA0.
 */
static u16 s5k4h8_eeprom_read8(u32 addr)
{
	u8 cmd[2];
	u16 val = 0;

	cmd[0] = (u8)(addr >> 8);
	cmd[1] = (u8)(addr & 0xFF);
	iReadRegI2C(cmd, 2, (u8 *)&val, 1, S5K4H8_EEPROM_WRITE_ID);
	return val;
}

unsigned int s5k4h8_otp_get_module_id(void)
{
	u8 cmd[2];
	u16 val = 0;
	int ret;

	cmd[0] = (u8)(S5K4H8_EEPROM_MODULE_ID_ADDR >> 8);
	cmd[1] = (u8)(S5K4H8_EEPROM_MODULE_ID_ADDR & 0xFF);

	kdSetI2CSpeed(S5K4H8_EEPROM_SPEED);
	ret = iReadRegI2C(cmd, 2, (u8 *)&val, 1, S5K4H8_EEPROM_WRITE_ID);
	if (ret < 0) {
		OTP_ERR("EEPROM 0x%04x read failed (%d)\n",
			S5K4H8_EEPROM_MODULE_ID_ADDR, ret);
		return 0xFFFF;
	}
	return val & 0xFF;
}

unsigned int s5k4h8_otp_module_variant(unsigned int module_id)
{
	switch (module_id) {
	case S5K4H8_MODULE_ID_OFILM:
		return S5K4H8_MODULE_OFILM;
	case S5K4H8_MODULE_ID_ST:
		return S5K4H8_MODULE_ST;
	case S5K4H8_MODULE_ID_HOLITECH:
		return S5K4H8_MODULE_HOLITECH;
	case S5K4H8_MODULE_ID_SUNWIN:
		return S5K4H8_MODULE_SUNWIN;
	default:
		return S5K4H8_MODULE_NUM;
	}
}

void s5k4h8_otp_cali(unsigned int variant)
{
	struct s5k4h8_otp_dev *dev;
	u8 af[8];
	u8 *d;
	u32 i;
	u16 r30, r31, r36, r37;

	if (variant >= S5K4H8_MODULE_NUM)
		return;

	dev = &s5k4h8_otp[variant];
	if (dev->valid) {
		OTP_LOG("%s already read, skip\n", dev->name);
		return;
	}

	d = dev->data;
	memset(d, 0, S5K4H8_OTP_BUF_SIZE);
	kdSetI2CSpeed(S5K4H8_EEPROM_SPEED);

	/* stock header: flag + CaliVer */
	d[0] = 0x01;
	d[1] = 0x63;
	d[2] = 0x69;
	d[3] = 0x68;
	d[4] = 0x63;

	/* unit AWB R/G and B/G */
	d[9]  = (u8)s5k4h8_eeprom_read8(0x2C);
	d[10] = (u8)s5k4h8_eeprom_read8(0x2D);
	d[11] = (u8)s5k4h8_eeprom_read8(0x2E);
	d[12] = (u8)s5k4h8_eeprom_read8(0x2F);

	/* read but not stored by stock either; logged for bring-up evidence */
	r30 = s5k4h8_eeprom_read8(0x30);
	r31 = s5k4h8_eeprom_read8(0x31);

	/* golden AWB R/G and B/G */
	d[13] = (u8)s5k4h8_eeprom_read8(0x32);
	d[14] = (u8)s5k4h8_eeprom_read8(0x33);
	d[15] = (u8)s5k4h8_eeprom_read8(0x34);
	d[16] = (u8)s5k4h8_eeprom_read8(0x35);

	r36 = s5k4h8_eeprom_read8(0x36);
	r37 = s5k4h8_eeprom_read8(0x37);

	OTP_LOG("%s AWB unit R/G=%d B/G=%d golden R/G=%d B/G=%d af=%d misc=%d\n",
		dev->name,
		d[9] | (d[10] << 8), d[11] | (d[12] << 8),
		d[13] | (d[14] << 8), d[15] | (d[16] << 8),
		r30 | (r31 << 8), r36 | (r37 << 8));

	/* AF block: EEPROM 0x42..0x49 */
	for (i = 0; i < 8; i++)
		af[i] = (u8)s5k4h8_eeprom_read8(0x42 + i);

	d[5]  = af[2];		/* SerialNum  <- 0x44 */
	d[6]  = af[3];		/*            <- 0x45 */
	d[17] = af[0];		/* AfInfinite <- 0x42 */
	d[18] = af[1];		/*            <- 0x43 */
	d[19] = af[4];		/* AfMacro    <- 0x46 */
	d[20] = af[5];		/*            <- 0x47 */

	OTP_LOG("%s AF inf=%d macro=%d serial=%d\n", dev->name,
		af[0] | (af[1] << 8), af[4] | (af[5] << 8),
		af[2] | (af[3] << 8));

	/* lens shading: 1868 bytes at EEPROM 0x51 */
	d[21] = (u8)(S5K4H8_LSC_SIZE & 0xFF);
	d[22] = (u8)(S5K4H8_LSC_SIZE >> 8);
	for (i = 0; i < S5K4H8_LSC_SIZE; i++)
		d[S5K4H8_LSC_OFFSET + i] =
			(u8)s5k4h8_eeprom_read8(S5K4H8_LSC_EEPROM_ADDR + i);

	spin_lock(&s5k4h8_otp_lock);
	dev->valid = true;
	spin_unlock(&s5k4h8_otp_lock);

	OTP_LOG("%s LSC %d bytes read, first=0x%02x 0x%02x 0x%02x last=0x%02x\n",
		dev->name, S5K4H8_LSC_SIZE,
		d[S5K4H8_LSC_OFFSET], d[S5K4H8_LSC_OFFSET + 1],
		d[S5K4H8_LSC_OFFSET + 2],
		d[S5K4H8_LSC_OFFSET + S5K4H8_LSC_SIZE - 1]);
}

/*
 * ---------------------------------------------------------------- char device
 */

static struct s5k4h8_otp_dev *s5k4h8_otp_from_inode(struct inode *inode)
{
	unsigned int i;

	for (i = 0; i < S5K4H8_MODULE_NUM; i++) {
		if (s5k4h8_otp[i].cdev &&
		    MAJOR(s5k4h8_otp[i].devno) == imajor(inode))
			return &s5k4h8_otp[i];
	}
	return NULL;
}

static int s5k4h8_otp_open(struct inode *inode, struct file *file)
{
	struct s5k4h8_otp_dev *dev = s5k4h8_otp_from_inode(inode);

	if (!dev)
		return -ENODEV;

	/*
	 * Refuse a node whose buffer was never filled. Serving an all-zero blob
	 * is worse than having no node at all: the HAL's layout check passes far
	 * enough to program a shading table derived from zeroes, which on
	 * hardware looked markedly worse than its own no-calibration path.
	 * Failing the open puts it back on that path.
	 */
	if (!dev->valid) {
		OTP_ERR("%s has no calibration (this is not the fitted module),"
			" refusing open\n", dev->name);
		return -ENODEV;
	}

	spin_lock(&s5k4h8_otp_lock);
	if (dev->opened) {
		spin_unlock(&s5k4h8_otp_lock);
		OTP_ERR("%s already opened\n", dev->name);
		return -EBUSY;
	}
	dev->opened = 1;
	spin_unlock(&s5k4h8_otp_lock);

	file->private_data = dev;
	return 0;
}

static int s5k4h8_otp_release(struct inode *inode, struct file *file)
{
	struct s5k4h8_otp_dev *dev = file->private_data;

	if (dev) {
		spin_lock(&s5k4h8_otp_lock);
		dev->opened = 0;
		spin_unlock(&s5k4h8_otp_lock);
	}
	return 0;
}

static long s5k4h8_otp_do_read(struct s5k4h8_otp_dev *dev,
			       struct stCAM_CAL_INFO_STRUCT __user *arg)
{
	struct stCAM_CAL_INFO_STRUCT info;
	u8 *buf;
	long ret = 0;

	if (copy_from_user(&info, arg, sizeof(info)))
		return -EFAULT;

	if (!info.u4Length || info.u4Length > S5K4H8_OTP_BUF_SIZE ||
	    info.u4Offset >= S5K4H8_OTP_BUF_SIZE ||
	    info.u4Offset + info.u4Length > S5K4H8_OTP_BUF_SIZE) {
		OTP_ERR("%s bad request offset=0x%x length=%u\n",
			dev->name, info.u4Offset, info.u4Length);
		return -EINVAL;
	}

	buf = kmalloc(info.u4Length, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy(buf, &dev->data[info.u4Offset], info.u4Length);
	if (copy_to_user(info.pu1Params, buf, info.u4Length))
		ret = -EFAULT;

	kfree(buf);
	return ret;
}

static long s5k4h8_otp_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	struct s5k4h8_otp_dev *dev = file->private_data;

	if (!dev)
		return -ENODEV;

	switch (cmd) {
	case CAM_CALIOC_G_READ:
		return s5k4h8_otp_do_read(dev,
					 (struct stCAM_CAL_INFO_STRUCT __user *)arg);
	case CAM_CALIOC_S_WRITE:
		/* the EEPROM is never written from Android */
		return 0;
	default:
		OTP_ERR("%s unknown ioctl 0x%x\n", dev->name, cmd);
		return -EPERM;
	}
}

#ifdef CONFIG_COMPAT
static long s5k4h8_otp_ioctl_compat(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	struct s5k4h8_otp_dev *dev = file->private_data;
	struct COMPAT_stCAM_CAL_INFO_STRUCT __user *data32;
	struct stCAM_CAL_INFO_STRUCT info;
	compat_uptr_t p;

	if (!dev)
		return -ENODEV;

	switch (cmd) {
	case COMPAT_CAM_CALIOC_G_READ:
		data32 = compat_ptr(arg);
		if (get_user(info.u4Offset, &data32->u4Offset) ||
		    get_user(info.u4Length, &data32->u4Length) ||
		    get_user(p, &data32->pu1Params))
			return -EFAULT;
		info.pu1Params = compat_ptr(p);

		if (!info.u4Length || info.u4Length > S5K4H8_OTP_BUF_SIZE ||
		    info.u4Offset >= S5K4H8_OTP_BUF_SIZE ||
		    info.u4Offset + info.u4Length > S5K4H8_OTP_BUF_SIZE) {
			OTP_ERR("%s bad compat request offset=0x%x length=%u\n",
				dev->name, info.u4Offset, info.u4Length);
			return -EINVAL;
		}
		if (copy_to_user(info.pu1Params,
				 &dev->data[info.u4Offset], info.u4Length))
			return -EFAULT;
		return 0;
	case CAM_CALIOC_S_WRITE:
		return 0;
	default:
		return -ENOIOCTLCMD;
	}
}
#endif

static const struct file_operations s5k4h8_otp_fops = {
	.owner		= THIS_MODULE,
	.open		= s5k4h8_otp_open,
	.release	= s5k4h8_otp_release,
	.unlocked_ioctl	= s5k4h8_otp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= s5k4h8_otp_ioctl_compat,
#endif
};

static void s5k4h8_otp_unregister_one(struct s5k4h8_otp_dev *dev)
{
	if (dev->cls) {
		device_destroy(dev->cls, dev->devno);
		class_destroy(dev->cls);
		dev->cls = NULL;
	}
	if (dev->cdev) {
		cdev_del(dev->cdev);
		dev->cdev = NULL;
	}
	if (dev->devno) {
		unregister_chrdev_region(dev->devno, 1);
		dev->devno = 0;
	}
}

static int s5k4h8_otp_register_one(struct s5k4h8_otp_dev *dev)
{
	int ret;

	ret = alloc_chrdev_region(&dev->devno, 0, 1, dev->name);
	if (ret) {
		OTP_ERR("%s alloc_chrdev_region failed (%d)\n", dev->name, ret);
		return ret;
	}

	dev->cdev = cdev_alloc();
	if (!dev->cdev) {
		OTP_ERR("%s cdev_alloc failed\n", dev->name);
		ret = -ENOMEM;
		goto err;
	}
	cdev_init(dev->cdev, &s5k4h8_otp_fops);
	dev->cdev->owner = THIS_MODULE;

	ret = cdev_add(dev->cdev, dev->devno, 1);
	if (ret) {
		OTP_ERR("%s cdev_add failed (%d)\n", dev->name, ret);
		goto err;
	}

	/* stock uses the node name for the class as well */
	dev->cls = class_create(THIS_MODULE, dev->name);
	if (IS_ERR(dev->cls)) {
		ret = PTR_ERR(dev->cls);
		dev->cls = NULL;
		OTP_ERR("%s class_create failed (%d)\n", dev->name, ret);
		goto err;
	}
	device_create(dev->cls, NULL, dev->devno, NULL, dev->name);

	OTP_LOG("/dev/%s registered (major %d)\n", dev->name,
		MAJOR(dev->devno));
	return 0;

err:
	s5k4h8_otp_unregister_one(dev);
	return ret;
}

static int __init s5k4h8_otp_cal_init(void)
{
	unsigned int i;

	for (i = 0; i < S5K4H8_MODULE_NUM; i++)
		s5k4h8_otp_register_one(&s5k4h8_otp[i]);
	return 0;
}

static void __exit s5k4h8_otp_cal_exit(void)
{
	unsigned int i;

	for (i = 0; i < S5K4H8_MODULE_NUM; i++)
		s5k4h8_otp_unregister_one(&s5k4h8_otp[i]);
}

module_init(s5k4h8_otp_cal_init);
module_exit(s5k4h8_otp_cal_exit);

MODULE_DESCRIPTION("S5K4H8 cam_cal (EEPROM) driver for Meizu m5c");
MODULE_LICENSE("GPL");
