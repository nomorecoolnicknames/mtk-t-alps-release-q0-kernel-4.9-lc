#ifndef BUILD_LK
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/m3note_board.h>

#ifndef CONFIG_FPGA_EARLY_PORTING
#define I2C_I2C_LCD_BIAS_CHANNEL 0
#define TPS_I2C_BUSNUM  I2C_I2C_LCD_BIAS_CHANNEL	/* for I2C channel 0 */
#define I2C_ID_NAME "tps65132"
#define TPS_ADDR 0x3E

#if defined(CONFIG_MTK_LEGACY)
static struct i2c_board_info tps65132_board_info __initdata = { I2C_BOARD_INFO(I2C_ID_NAME, TPS_ADDR) };
#endif
#if !defined(CONFIG_MTK_LEGACY)
static const struct of_device_id lcm_of_match[] = {
		{.compatible = "mediatek,i2c_lcd_bias"},
		{.compatible = "mediatek,I2C_LCD_BIAS"},
		{},
};
#endif

static struct i2c_client *tps65132_i2c_client;
static DEFINE_MUTEX(tps65132_client_lock);

static int tps65132_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int tps65132_remove(struct i2c_client *client);

static const struct i2c_device_id tps65132_id[] = {
	{I2C_ID_NAME, 0},
	{}
};

static struct i2c_driver tps65132_iic_driver = {
	.id_table = tps65132_id,
	.probe = tps65132_probe,
	.remove = tps65132_remove,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "tps65132",
#if !defined(CONFIG_MTK_LEGACY)
			.of_match_table = lcm_of_match,
#endif
		   },
};

static int tps65132_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	pr_info("tps65132_iic_probe\n");
	mutex_lock(&tps65132_client_lock);
	if (tps65132_i2c_client) {
		mutex_unlock(&tps65132_client_lock);
		return -EBUSY;
	}
	tps65132_i2c_client = client;
	mutex_unlock(&tps65132_client_lock);
	return 0;
}

static int tps65132_remove(struct i2c_client *client)
{
	mutex_lock(&tps65132_client_lock);
	if (tps65132_i2c_client == client)
		tps65132_i2c_client = NULL;
	mutex_unlock(&tps65132_client_lock);
	return 0;
}

int tps65132_write_bytes(unsigned char addr, unsigned char value)
{
	struct i2c_client *client;
	char write_data[2] = { addr, value };
	int ret;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC) &&
	    m3note_board_id() == M3NOTE_BOARD_UNKNOWN)
		return -ENODEV;

	mutex_lock(&tps65132_client_lock);
	client = tps65132_i2c_client;
	if (!client) {
		ret = -ENODEV;
		goto out_unlock;
	}
	ret = i2c_master_send(client, write_data, sizeof(write_data));
	if (ret >= 0 && ret != 2)
		ret = -EIO;

out_unlock:
	mutex_unlock(&tps65132_client_lock);
	if (ret < 0)
		pr_err("tps65132: write 0x%02x=0x%02x failed %d\n", addr, value, ret);
	return ret;
}

static int __init tps65132_iic_init(void)
{
	int ret;

	if (IS_ENABLED(CONFIG_M3NOTE_DIAGNOSTIC) &&
	    m3note_board_id() == M3NOTE_BOARD_UNKNOWN)
		return -ENODEV;

	{ extern int forge_display_gate_skip(const char *who); if (forge_display_gate_skip("tps65132")) return 0; }	/* m681-49-disp: NONRST2 self-heal gate */
	pr_info("tps65132_iic_init\n");
#if defined(CONFIG_MTK_LEGACY)
	i2c_register_board_info(TPS_I2C_BUSNUM, &tps65132_board_info, 1);
#endif
	ret = i2c_add_driver(&tps65132_iic_driver);
	pr_info("tps65132_iic_init add_driver ret=%d\n", ret);
	return 0;
}

static void __exit tps65132_iic_exit(void)
{
	i2c_del_driver(&tps65132_iic_driver);
}

module_init(tps65132_iic_init);
module_exit(tps65132_iic_exit);

MODULE_DESCRIPTION("MT6755 TPS65132 LCD bias I2C driver");
MODULE_LICENSE("GPL");
#endif /* CONFIG_FPGA_EARLY_PORTING */
#endif /* BUILD_LK */
