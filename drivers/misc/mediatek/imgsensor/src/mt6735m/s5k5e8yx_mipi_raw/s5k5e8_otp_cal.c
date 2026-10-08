























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










struct m5c_cam_cal_info {
        u32 u4Offset;
        u32 u4Length;
        u8 __user *pu1Params;
};
#define M5C_CAM_CALIOC_G_READ _IOWR('i', 5, struct m5c_cam_cal_info)
#ifdef CONFIG_COMPAT
struct m5c_cam_cal_info32 {
        u32 u4Offset;
        u32 u4Length;
        compat_uptr_t pu1Params;
};
#define M5C_COMPAT_CAM_CALIOC_G_READ _IOWR('i', 5, struct m5c_cam_cal_info32)
#endif
#include "s5k5e8_otp_cal.h"

#define PFX "S5K5E8_OTP"
#define OTP_LOG(fmt, args...) pr_info("[%s] " fmt, PFX, ##args)
#define OTP_ERR(fmt, args...) pr_err("[%s] " fmt, PFX, ##args)

/* OTP control registers of the S5K5E8YX */
#define S5K5E8_OTP_CTRL		0x0A00
#define S5K5E8_OTP_PAGE		0x0A02
#define S5K5E8_OTP_PAGE_CAL	0x04
#define S5K5E8_OTP_GRP_FLAG0	0x0A04
#define S5K5E8_OTP_GRP_FLAG1	0x0A05
#define S5K5E8_OTP_GRP_FLAG2	0x0A06
#define S5K5E8_OTP_GRP0_BASE	0x0A07
#define S5K5E8_OTP_GRP1_BASE	0x0A17
#define S5K5E8_OTP_GRP2_BASE	0x0A27
#define S5K5E8_OTP_MODULE_ID	S5K5E8_OTP_GRP0_BASE	/* first byte of grp 0 */

#define S5K5E8_CAL_BYTES	14
#define S5K5E8_CAL_OFFSET	5
#define S5K5E8_OTP_BUF_SIZE	64

struct s5k5e8_otp_dev {
        const char *name;	/* /dev node name; must match the HAL blob */
        u8 data[S5K5E8_OTP_BUF_SIZE];
        bool valid;
        dev_t devno;
        struct cdev *cdev;
        struct class *cls;
        u32 opened;
};

static struct s5k5e8_otp_dev s5k5e8_otp[S5K5E8_MODULE_NUM] = {
        [S5K5E8_MODULE_ST]       = { .name = "S5K5E8_ST_OTP" },
        [S5K5E8_MODULE_QH]       = { .name = "S5K5E8_QH_OTP" },
        [S5K5E8_MODULE_HOLITECH] = { .name = "S5K5E8_HOLITECH_OTP" },
        [S5K5E8_MODULE_SUNWIN]   = { .name = "S5K5E8_SUNWIN_OTP" },
};

static DEFINE_SPINLOCK(s5k5e8_otp_lock);

/*
 * ------------------------------------------------------------------- OTP read
 */

static void s5k5e8_otp_page_open(void)
{
        s5k5e8_otp_write_reg(S5K5E8_OTP_CTRL, 0x04);
        s5k5e8_otp_write_reg(S5K5E8_OTP_PAGE, S5K5E8_OTP_PAGE_CAL);
        s5k5e8_otp_write_reg(S5K5E8_OTP_CTRL, 0x01);
        udelay(1000);
}

static void s5k5e8_otp_page_close(void)
{
        s5k5e8_otp_write_reg(S5K5E8_OTP_CTRL, 0x04);
        s5k5e8_otp_write_reg(S5K5E8_OTP_CTRL, 0x00);
}

/*
 * Group selection, exactly as stock: group 2 wins over group 1 over group 0.
 */
static unsigned int s5k5e8_otp_group_base(void)
{
        unsigned int f0, f1, f2;

        f0 = s5k5e8_otp_read_reg(S5K5E8_OTP_GRP_FLAG0);
        f1 = s5k5e8_otp_read_reg(S5K5E8_OTP_GRP_FLAG1);
        f2 = s5k5e8_otp_read_reg(S5K5E8_OTP_GRP_FLAG2);
        OTP_LOG("group flags 0x0A04=%d 0x0A05=%d 0x0A06=%d\n", f0, f1, f2);

        if (f2 == 1)
                return S5K5E8_OTP_GRP2_BASE;
        if (f1 == 1)
                return S5K5E8_OTP_GRP1_BASE;
        return S5K5E8_OTP_GRP0_BASE;
}

unsigned int s5k5e8_otp_get_module_id(void)
{
        unsigned int id;

        s5k5e8_otp_page_open();
        (void)s5k5e8_otp_group_base();
        id = s5k5e8_otp_read_reg(S5K5E8_OTP_MODULE_ID);
        s5k5e8_otp_page_close();

        OTP_LOG("module_id=%d\n", id);
        return id & 0xFF;
}

unsigned int s5k5e8_otp_module_variant(unsigned int module_id)
{
        switch (module_id) {
        case S5K5E8_MODULE_ID_ST:
                return S5K5E8_MODULE_ST;
        case S5K5E8_MODULE_ID_QH:
                return S5K5E8_MODULE_QH;
        case S5K5E8_MODULE_ID_HOLITECH:
                return S5K5E8_MODULE_HOLITECH;
        case S5K5E8_MODULE_ID_SUNWIN:
                return S5K5E8_MODULE_SUNWIN;
        default:
                return S5K5E8_MODULE_NUM;
        }
}

void s5k5e8_otp_cali(unsigned int variant)
{
        struct s5k5e8_otp_dev *dev;
        unsigned int base;
        u8 *d;
        u32 i;

        if (variant >= S5K5E8_MODULE_NUM)
                return;

        dev = &s5k5e8_otp[variant];
        if (dev->valid) {
                OTP_LOG("%s already read, skip\n", dev->name);
                return;
        }

        d = dev->data;
        memset(d, 0, S5K5E8_OTP_BUF_SIZE);

        d[0] = 0x01;
        d[1] = 0x63;
        d[2] = 0x69;
        d[3] = 0x68;
        d[4] = 0x63;

        s5k5e8_otp_page_open();
        base = s5k5e8_otp_group_base();
        OTP_LOG("%s group base 0x%04x\n", dev->name, base);
        for (i = 0; i < S5K5E8_CAL_BYTES; i++)
                d[S5K5E8_CAL_OFFSET + i] =
                        (u8)s5k5e8_otp_read_reg(base + i);
        s5k5e8_otp_page_close();

        OTP_LOG("%s R_G=%d B_G=%d Gold_R_G=%d Gold_B_G=%d\n", dev->name,
                d[13] | (d[14] << 8), d[15] | (d[16] << 8),
                d[9] | (d[10] << 8), d[11] | (d[12] << 8));

        spin_lock(&s5k5e8_otp_lock);
        dev->valid = true;
        spin_unlock(&s5k5e8_otp_lock);
}

/*
 * ---------------------------------------------------------------- char device
 */

static struct s5k5e8_otp_dev *s5k5e8_otp_from_inode(struct inode *inode)
{
        unsigned int i;

        for (i = 0; i < S5K5E8_MODULE_NUM; i++) {
                if (s5k5e8_otp[i].cdev &&
                    MAJOR(s5k5e8_otp[i].devno) == imajor(inode))
                        return &s5k5e8_otp[i];
        }
        return NULL;
}

static int s5k5e8_otp_open(struct inode *inode, struct file *file)
{
        struct s5k5e8_otp_dev *dev = s5k5e8_otp_from_inode(inode);

        if (!dev)
                return -ENODEV;

        /* see the S5K4H8 side: an all-zero blob is worse than no node at all */
        if (!dev->valid) {
                OTP_ERR("%s has no calibration (this is not the fitted module),"
                        " refusing open\n", dev->name);
                return -ENODEV;
        }

        spin_lock(&s5k5e8_otp_lock);
        if (dev->opened) {
                spin_unlock(&s5k5e8_otp_lock);
                OTP_ERR("%s already opened\n", dev->name);
                return -EBUSY;
        }
        dev->opened = 1;
        spin_unlock(&s5k5e8_otp_lock);

        file->private_data = dev;
        return 0;
}

static int s5k5e8_otp_release(struct inode *inode, struct file *file)
{
        struct s5k5e8_otp_dev *dev = file->private_data;

        if (dev) {
                spin_lock(&s5k5e8_otp_lock);
                dev->opened = 0;
                spin_unlock(&s5k5e8_otp_lock);
        }
        return 0;
}

static long s5k5e8_otp_do_read(struct s5k5e8_otp_dev *dev,
                               struct m5c_cam_cal_info __user *arg)
{
        struct m5c_cam_cal_info info;

        if (copy_from_user(&info, arg, sizeof(info)))
                return -EFAULT;

        if (!info.u4Length || info.u4Length > S5K5E8_OTP_BUF_SIZE ||
            info.u4Offset >= S5K5E8_OTP_BUF_SIZE ||
            info.u4Offset + info.u4Length > S5K5E8_OTP_BUF_SIZE) {
                OTP_ERR("%s bad request offset=0x%x length=%u\n",
                        dev->name, info.u4Offset, info.u4Length);
                return -EINVAL;
        }

        if (copy_to_user(info.pu1Params, &dev->data[info.u4Offset],
                         info.u4Length))
                return -EFAULT;
        /* stock CAM_CAL_Ioctl returns u4Length; the HAL checks it */
        return info.u4Length;
}

static long s5k5e8_otp_ioctl(struct file *file, unsigned int cmd,
                             unsigned long arg)
{
        struct s5k5e8_otp_dev *dev = file->private_data;

        if (!dev)
                return -ENODEV;

        switch (cmd) {
        case M5C_CAM_CALIOC_G_READ:
                return s5k5e8_otp_do_read(dev,
                                         (struct m5c_cam_cal_info __user *)arg);
        case CAM_CALIOC_S_WRITE:
                return 0;
        default:
                OTP_ERR("%s unknown ioctl 0x%x\n", dev->name, cmd);
                return -EPERM;
        }
}

#ifdef CONFIG_COMPAT
static long s5k5e8_otp_ioctl_compat(struct file *file, unsigned int cmd,
                                    unsigned long arg)
{
        struct s5k5e8_otp_dev *dev = file->private_data;
        struct m5c_cam_cal_info32 __user *data32;
        struct m5c_cam_cal_info info;
        compat_uptr_t p;

        if (!dev)
                return -ENODEV;

        switch (cmd) {
        case M5C_COMPAT_CAM_CALIOC_G_READ:
                data32 = compat_ptr(arg);
                if (get_user(info.u4Offset, &data32->u4Offset) ||
                    get_user(info.u4Length, &data32->u4Length) ||
                    get_user(p, &data32->pu1Params))
                        return -EFAULT;
                info.pu1Params = compat_ptr(p);

                if (!info.u4Length || info.u4Length > S5K5E8_OTP_BUF_SIZE ||
                    info.u4Offset >= S5K5E8_OTP_BUF_SIZE ||
                    info.u4Offset + info.u4Length > S5K5E8_OTP_BUF_SIZE) {
                        OTP_ERR("%s bad compat request offset=0x%x length=%u\n",
                                dev->name, info.u4Offset, info.u4Length);
                        return -EINVAL;
                }
                if (copy_to_user(info.pu1Params,
                                 &dev->data[info.u4Offset], info.u4Length))
                        return -EFAULT;
                return info.u4Length;
        case CAM_CALIOC_S_WRITE:
                return 0;
        default:
                return -ENOIOCTLCMD;
        }
}
#endif

static const struct file_operations s5k5e8_otp_fops = {
        .owner		= THIS_MODULE,
        .open		= s5k5e8_otp_open,
        .release	= s5k5e8_otp_release,
        .unlocked_ioctl	= s5k5e8_otp_ioctl,
#ifdef CONFIG_COMPAT
        .compat_ioctl	= s5k5e8_otp_ioctl_compat,
#endif
};

static void s5k5e8_otp_unregister_one(struct s5k5e8_otp_dev *dev)
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

static int s5k5e8_otp_register_one(struct s5k5e8_otp_dev *dev)
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
        cdev_init(dev->cdev, &s5k5e8_otp_fops);
        dev->cdev->owner = THIS_MODULE;

        ret = cdev_add(dev->cdev, dev->devno, 1);
        if (ret) {
                OTP_ERR("%s cdev_add failed (%d)\n", dev->name, ret);
                goto err;
        }

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
        s5k5e8_otp_unregister_one(dev);
        return ret;
}

static int __init s5k5e8_otp_cal_init(void)
{
        unsigned int i;

        for (i = 0; i < S5K5E8_MODULE_NUM; i++)
                s5k5e8_otp_register_one(&s5k5e8_otp[i]);
        return 0;
}

static void __exit s5k5e8_otp_cal_exit(void)
{
        unsigned int i;

        for (i = 0; i < S5K5E8_MODULE_NUM; i++)
                s5k5e8_otp_unregister_one(&s5k5e8_otp[i]);
}

module_init(s5k5e8_otp_cal_init);
module_exit(s5k5e8_otp_cal_exit);

MODULE_DESCRIPTION("S5K5E8YX cam_cal (sensor OTP) driver for Meizu m5c");
MODULE_LICENSE("GPL");
