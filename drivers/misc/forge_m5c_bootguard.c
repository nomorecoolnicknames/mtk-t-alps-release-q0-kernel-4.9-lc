
































#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/reboot.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define BG_DISK		"mmcblk0"
#define BG_PART_NAME	"para"
#define BG_PART_NO	6
#define BG_PART_START	67584ULL
#define BG_PART_SECTS	1024ULL
#define BG_BCB_BYTES	2048	/* sizeof(struct bootloader_message) */

static int forge_bootguard_deadline = 900;
core_param(forge_bootguard_deadline, forge_bootguard_deadline, int, 0644);

static DEFINE_MUTEX(bg_lock);
static bool bg_armed;		/* message written by this boot, not disarmed */
static bool bg_disarmed;	/* userspace said the boot is good */
static unsigned long bg_deadline;
static bool bg_thread_alive;	/* bg_thread runs; at most one */

/* The para partition of the M5c, or 0. */
static dev_t bg_find_para(void)
{
        struct disk_part_iter piter;
        struct hd_struct *part;
        struct gendisk *disk;
        dev_t devt = 0, whole;
        int partno;

        whole = blk_lookup_devt(BG_DISK, 0);
        if (!whole)
                return 0;
        disk = get_gendisk(whole, &partno);
        if (!disk)
                return 0;
        disk_part_iter_init(&piter, disk, 0);
        while ((part = disk_part_iter_next(&piter))) {
                const char *name = part->info ? (const char *)part->info->volname : "";

                if (part->partno == BG_PART_NO && !strcmp(name, BG_PART_NAME) &&
                    part->start_sect == BG_PART_START &&
                    part->nr_sects == BG_PART_SECTS) {
                        devt = part_devt(part);
                        break;
                }
        }
        disk_part_iter_exit(&piter);
        put_disk(disk);
        return devt;
}

/*
 * cmd != NULL: write the message; cmd == NULL: zero the 2 KiB message.
 * state != NULL: only read, *state = 1 if it says boot-recovery.
 */
static int bg_bcb_rw(const char *cmd, int *state)
{
        fmode_t mode = state ? FMODE_READ : FMODE_READ | FMODE_WRITE;
        struct block_device *bdev;
        unsigned int bs, off;
        dev_t devt;
        int err = 0;

        devt = bg_find_para();
        if (!devt)
                return -ENODEV;
        bdev = blkdev_get_by_dev(devt, mode, NULL);
        if (IS_ERR(bdev))
                return PTR_ERR(bdev);
        if (state)
                invalidate_bdev(bdev);	/* userspace may have written it */
        /* __bread() needs the device's own block size (see forge_m681_marker.c) */
        bs = block_size(bdev);
        for (off = 0; off < BG_BCB_BYTES; off += bs) {
                struct buffer_head *bh = __bread(bdev, off / bs, bs);
                unsigned int len = min_t(unsigned int, bs, BG_BCB_BYTES - off);

                if (!bh) {
                        err = -EIO;
                        break;
                }
                if (state) {
                        if (off == 0)
                                *state = !strncmp(bh->b_data, "boot-recovery", 13);
                        brelse(bh);
                        break;
                }
                memset(bh->b_data, 0, len);
                if (cmd && off == 0) {
                        strcpy(bh->b_data, cmd);			/* command[32] */
                        strcpy(bh->b_data + 32, "forge-bootguard");	/* status[32] */
                        strcpy(bh->b_data + 64, "recovery\n");		/* recovery[768] */
                }
                mark_buffer_dirty(bh);
                err = sync_dirty_buffer(bh);
                brelse(bh);
                if (err)
                        break;
        }
        if (!err && !state)
                err = blkdev_issue_flush(bdev, GFP_KERNEL, NULL);
        blkdev_put(bdev, mode);
        return err;
}

static int bg_thread_exit(void)
{
        mutex_lock(&bg_lock);
        bg_thread_alive = false;
        mutex_unlock(&bg_lock);
        return 0;
}

static int bg_thread(void *unused)
{
        bool logged = false;
        int err;

        /* Arm as soon as para exists. */
        while (!kthread_should_stop()) {
                mutex_lock(&bg_lock);
                if (bg_disarmed) {
                        mutex_unlock(&bg_lock);
                        return bg_thread_exit();
                }
                err = bg_bcb_rw("boot-recovery", NULL);
                if (!err)
                        bg_armed = true;
                mutex_unlock(&bg_lock);
                if (!err) {
                        pr_emerg("forge_bootguard: BCB boot-recovery armed in %s p%d, deadline %d s\n",
                                 BG_DISK, BG_PART_NO, forge_bootguard_deadline);
                        break;
                }
                if (!logged && time_after(jiffies, INITIAL_JIFFIES + 30 * HZ)) {
                        pr_emerg("forge_bootguard: no %s p%d \"%s\" yet (%d), still waiting\n",
                                 BG_DISK, BG_PART_NO, BG_PART_NAME, err);
                        logged = true;
                }
                msleep(100);
        }

        /* Wait for userspace to disarm, or restart into TWRP. */
        while (!kthread_should_stop()) {
                bool fire;

                mutex_lock(&bg_lock);
                if (!bg_armed) {
                        mutex_unlock(&bg_lock);
                        return bg_thread_exit();
                }
                fire = forge_bootguard_deadline > 0 &&
                       time_after_eq(jiffies, bg_deadline);
                mutex_unlock(&bg_lock);
                if (fire) {
                        pr_emerg("forge_bootguard: not disarmed within %d s, restarting into TWRP\n",
                                 forge_bootguard_deadline);
                        emergency_sync();
                        kernel_restart("recovery");
                }
                msleep(1000);
        }
        return bg_thread_exit();
}

static int bg_reboot_notify(struct notifier_block *nb, unsigned long action,
                            void *data)
{
        const char *cmd = data;
        int err;

        if (action != SYS_RESTART || !cmd ||
            (strcmp(cmd, "recovery") && strcmp(cmd, "bootloader")))
                return NOTIFY_DONE;
        err = bg_bcb_rw("boot-recovery", NULL);
        pr_emerg("forge_bootguard: reboot %s: BCB boot-recovery %d\n", cmd, err);
        return NOTIFY_DONE;
}

static struct notifier_block bg_reboot_nb = {
        .notifier_call = bg_reboot_notify,
};

static int bg_show(struct seq_file *m, void *v)
{
        long left = 0;
        int bcb = -1;

        mutex_lock(&bg_lock);
        if (bg_bcb_rw(NULL, &bcb))
                bcb = -1;
        if (bg_armed && time_before(jiffies, bg_deadline))
                left = (long)(bg_deadline - jiffies) / HZ;
        seq_printf(m, "armed %d bcb %s deadline %d left %ld\n", bg_armed,
                   bcb < 0 ? "?" : bcb ? "recovery" : "clear",
                   forge_bootguard_deadline, left);
        mutex_unlock(&bg_lock);
        return 0;
}

static int bg_open(struct inode *inode, struct file *file)
{
        return single_open(file, bg_show, NULL);
}

static ssize_t bg_write(struct file *file, const char __user *ubuf,
                        size_t count, loff_t *ppos)
{
        char buf[16];
        unsigned int seconds;
        bool start;
        int err;

        if (count >= sizeof(buf))
                return -EINVAL;
        if (copy_from_user(buf, ubuf, count))
                return -EFAULT;
        buf[count] = 0;
        strim(buf);

        if (!strcmp(buf, "panic")) {
                if (!bg_armed)
                        return -EPERM;
                panic("forge_bootguard: test panic requested");
        }
        err = kstrtouint(buf, 0, &seconds);
        if (err)
                return err;
        if (seconds && !capable(CAP_SYS_BOOT))
                return -EPERM;

        mutex_lock(&bg_lock);
        if (!seconds) {
                bg_disarmed = true;
                bg_armed = false;
                err = bg_bcb_rw(NULL, NULL);
                pr_info("forge_bootguard: disarmed, BCB cleared (%d)\n", err);
        } else {
                err = bg_bcb_rw("boot-recovery", NULL);
                if (!err) {
                        forge_bootguard_deadline = seconds;
                        bg_deadline = jiffies + seconds * HZ;
                        bg_armed = true;
                        bg_disarmed = false;
                }
                pr_info("forge_bootguard: re-armed for %u s (%d)\n", seconds, err);
        }
        start = seconds && !err && !bg_thread_alive;
        if (start)
                bg_thread_alive = true;
        mutex_unlock(&bg_lock);
        if (start && IS_ERR(kthread_run(bg_thread, NULL, "forge_bootguard")))
                bg_thread_exit();
        return err ? err : count;
}

static const struct file_operations bg_fops = {
        .open		= bg_open,
        .read		= seq_read,
        .write		= bg_write,
        .llseek		= seq_lseek,
        .release	= single_release,
};

static int __init forge_bootguard_init(void)
{
        struct task_struct *t;

        register_reboot_notifier(&bg_reboot_nb);
        if (!proc_create("forge_bootguard", 0666, NULL, &bg_fops))
                pr_err("forge_bootguard: cannot create /proc/forge_bootguard\n");
        if (forge_bootguard_deadline <= 0) {
                pr_emerg("forge_bootguard: disabled (deadline %d)\n",
                         forge_bootguard_deadline);
                return 0;
        }
        bg_deadline = jiffies + forge_bootguard_deadline * HZ;
        bg_thread_alive = true;
        t = kthread_run(bg_thread, NULL, "forge_bootguard");
        if (IS_ERR(t))
                bg_thread_alive = false;
        pr_emerg("forge_bootguard: started (%s)\n", IS_ERR(t) ? "FAILED" : "ok");
        return 0;
}
late_initcall(forge_bootguard_init);
