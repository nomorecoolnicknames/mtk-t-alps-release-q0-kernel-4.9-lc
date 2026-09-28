// SPDX-License-Identifier: GPL-2.0
/*
 * Meizu M5s M1612 Yassy ILI9881C panel, opt-in 4.9 port.
 * Derived from the ZTE/Meizu 3.18 driver, source commit
 * 921dd70f529cb0d0a8dd367dda0445911c9bb57c.
 * Tables match Flyme 6.3.1.0G stock kernel byte-for-byte:
 * init @0xd78f88 (192 entries), suspend @0xd78e20 (5 entries), stride 72.
 * Keep selection board-specific; this is not an M5c panel.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <mach/gpio_const.h>
#include <mt-plat/mt_gpio.h>
#include "lcm_drv.h"

#if !defined(CONFIG_MACH_MT6753_M5S) || !defined(CONFIG_MTK_GPIO)
#error "M5s Yassy requires the M5s board and MTK GPIO driver"
#endif
#if defined(CONFIG_MTK_LCM_DEVICE_TREE_SUPPORT)
#error "M5s Yassy uses the compiled LCM driver, not the DT command-table path"
#endif

#define FRAME_WIDTH 720
#define FRAME_HEIGHT 1280
#define REGFLAG_DELAY 0xfc
#define REGFLAG_END_OF_TABLE 0xfd
/* Stock lcmbiasenable pinctrl states drive GPIO119, without I2C bias writes. */
#define GPIO_LCM_PWR_EN (119 | 0x80000000)

static struct LCM_UTIL_FUNCS lcm_util;
#define SET_RESET_PIN(v) lcm_util.set_reset_pin(v)
#define MDELAY(ms) lcm_util.mdelay(ms)

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[64];
};

static struct LCM_setting_table lcm_initialization_setting1[] = {
	{0xFF,3,{0x98,0x81,0x03}},
	{0x01,1,{0x00}},
	{0x02,1,{0x00}},
	{0x03,1,{0x53}},
	{0x04,1,{0x53}},
	{0x05,1,{0x13}},
	{0x06,1,{0x04}},
	{0x07,1,{0x02}},
	{0x08,1,{0x02}},
	{0x09,1,{0x00}},
	{0x0A,1,{0x00}},
	{0x0B,1,{0x00}},
	{0x0C,1,{0x00}},
	{0x0D,1,{0x00}},
	{0x0E,1,{0x00}},
	{0x0F,1,{0x00}},
	{0x10,1,{0x00}},
	{0x11,1,{0x00}},
	{0x12,1,{0x00}},
	{0x13,1,{0x00}},
	{0x14,1,{0x00}},
	{0x15,1,{0x00}},
	{0x16,1,{0x00}},
	{0x17,1,{0x00}},
	{0x18,1,{0x00}},
	{0x19,1,{0x00}},
	{0x1A,1,{0x00}},
	{0x1B,1,{0x00}},
	{0x1C,1,{0x00}},
	{0x1D,1,{0x00}},
	{0x1E,1,{0xC0}},
	{0x1F,1,{0x80}},
	{0x20,1,{0x02}},
	{0x21,1,{0x09}},
	{0x22,1,{0x00}},
	{0x23,1,{0x00}},
	{0x24,1,{0x00}},
	{0x25,1,{0x00}},
	{0x26,1,{0x00}},
	{0x27,1,{0x00}},
	{0x28,1,{0x55}},
	{0x29,1,{0x03}},
	{0x2A,1,{0x00}},
	{0x2B,1,{0x00}},
	{0x2C,1,{0x00}},
	{0x2D,1,{0x00}},
	{0x2E,1,{0x00}},
	{0x2F,1,{0x00}},
	{0x30,1,{0x00}},
	{0x31,1,{0x00}},
	{0x32,1,{0x00}},
	{0x33,1,{0x00}},
	{0x34,1,{0x03}},
	{0x35,1,{0x00}},
	{0x36,1,{0x05}},
	{0x37,1,{0x00}},
	{0x38,1,{0x3C}},
	{0x39,1,{0x00}},
	{0x3A,1,{0x00}},
	{0x3B,1,{0x00}},
	{0x3C,1,{0x00}},
	{0x3D,1,{0x00}},
	{0x3E,1,{0x00}},
	{0x3F,1,{0x00}},
	{0x40,1,{0x00}},
	{0x41,1,{0x00}},
	{0x42,1,{0x00}},
	{0x43,1,{0x00}},
	{0x44,1,{0x00}},
	{0x50,1,{0x01}},
	{0x51,1,{0x23}},
	{0x52,1,{0x45}},
	{0x53,1,{0x67}},
	{0x54,1,{0x89}},
	{0x55,1,{0xAB}},
	{0x56,1,{0x01}},
	{0x57,1,{0x23}},
	{0x58,1,{0x45}},
	{0x59,1,{0x67}},
	{0x5A,1,{0x89}},
	{0x5B,1,{0xAB}},
	{0x5C,1,{0xCD}},
	{0x5D,1,{0xEF}},
	{0x5E,1,{0x01}},
	{0x5F,1,{0x14}},
	{0x60,1,{0x15}},
	{0x61,1,{0x0C}},
	{0x62,1,{0x0D}},
	{0x63,1,{0x0E}},
	{0x64,1,{0x0F}},
	{0x65,1,{0x10}},
	{0x66,1,{0x11}},
	{0x67,1,{0x08}},
	{0x68,1,{0x02}},
	{0x69,1,{0x0A}},
	{0x6A,1,{0x02}},
	{0x6B,1,{0x02}},
	{0x6C,1,{0x02}},
	{0x6D,1,{0x02}},
	{0x6E,1,{0x02}},
	{0x6F,1,{0x02}},
	{0x70,1,{0x02}},
	{0x71,1,{0x02}},
	{0x72,1,{0x06}},
	{0x73,1,{0x02}},
	{0x74,1,{0x02}},
	{0x75,1,{0x14}},
	{0x76,1,{0x15}},
	{0x77,1,{0x0F}},
	{0x78,1,{0x0E}},
	{0x79,1,{0x0D}},
	{0x7A,1,{0x0C}},
	{0x7B,1,{0x11}},
	{0x7C,1,{0x10}},
	{0x7D,1,{0x06}},
	{0x7E,1,{0x02}},
	{0x7F,1,{0x0A}},
	{0x80,1,{0x02}},
	{0x81,1,{0x02}},
	{0x82,1,{0x02}},
	{0x83,1,{0x02}},
	{0x84,1,{0x02}},
	{0x85,1,{0x02}},
	{0x86,1,{0x02}},
	{0x87,1,{0x02}},
	{0x88,1,{0x08}},
	{0x89,1,{0x02}},
	{0x8A,1,{0x02}},
	{0xFF,3,{0x98,0x81,0x04}},
	{0x6C,1,{0x15}},
	{0x6E,1,{0x2F}},
	{0x6F,1,{0x55}},
	{0x3A,1,{0xA4}},
	{0x8D,1,{0x1F}},
	{0x87,1,{0xBA}},
	{0x26,1,{0x76}},
	{0xB2,1,{0xD1}},
	{0x88,1,{0x0B}},
	{0xFF,3,{0x98,0x81,0x01}},
	{0x22,1,{0x09}},
	{0x31,1,{0x00}},
	{0x53,1,{0x77}},
	{0x55,1,{0x77}},
	{0x50,1,{0xA6}},
	{0x51,1,{0xA6}},
	{0x60,1,{0x14}},
	{0xA0,1,{0x00}},
	{0xA1,1,{0x0B}},
	{0xA2,1,{0x2A}},
	{0xA3,1,{0x14}},
	{0xA4,1,{0x17}},
	{0xA5,1,{0x2A}},
	{0xA6,1,{0x1E}},
	{0xA7,1,{0x20}},
	{0xA8,1,{0x8C}},
	{0xA9,1,{0x1C}},
	{0xAA,1,{0x28}},
	{0xAB,1,{0x78}},
	{0xAC,1,{0x1A}},
	{0xAD,1,{0x15}},
	{0xAE,1,{0x4C}},
	{0xAF,1,{0x20}},
	{0xB0,1,{0x27}},
	{0xB1,1,{0x52}},
	{0xB2,1,{0x65}},
	{0xB3,1,{0x3F}},
	{0xC0,1,{0x00}},
	{0xC1,1,{0x1B}},
	{0xC2,1,{0x2A}},
	{0xC3,1,{0x14}},
	{0xC4,1,{0x17}},
	{0xC5,1,{0x2A}},
	{0xC6,1,{0x1E}},
	{0xC7,1,{0x20}},
	{0xC8,1,{0x8D}},
	{0xC9,1,{0x1C}},
	{0xCA,1,{0x28}},
	{0xCB,1,{0x78}},
	{0xCC,1,{0x1B}},
	{0xCD,1,{0x15}},
	{0xCE,1,{0x4C}},
	{0xCF,1,{0x21}},
	{0xD0,1,{0x27}},
	{0xD1,1,{0x52}},
	{0xD2,1,{0x65}},
	{0xD3,1,{0x3F}},
	{0xFF,3,{0x98,0x81,0x00}},
	{0x11,1,{0x00}},
	{REGFLAG_DELAY, 120, {}},
	{0x29,1,{0x00}},
	{REGFLAG_DELAY, 20, {}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static struct LCM_setting_table lcm_deep_sleep_mode_in_setting[] = {
	{0x28, 1, {0x00}},
	{REGFLAG_DELAY, 20, {}},
	{0x10, 1, {0x00}},
	{REGFLAG_DELAY, 120, {}},
	{REGFLAG_END_OF_TABLE, 0x00, {}}
};

static void push_table(struct LCM_setting_table *table, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		switch (table[i].cmd) {
		case REGFLAG_DELAY:
			MDELAY(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			return;
		default:
			lcm_util.dsi_set_cmdq_V2(table[i].cmd, table[i].count,
						 table[i].para_list, 1);
		}
	}
}

static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(lcm_util));
}

static void lcm_get_params(struct LCM_PARAMS *params)
{
	memset(params, 0, sizeof(*params));
	params->type = LCM_TYPE_DSI;
	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;
	params->dbi.te_mode = LCM_DBI_TE_MODE_VSYNC_ONLY;
	params->dbi.te_edge_polarity = LCM_POLARITY_RISING;
	params->dsi.cont_clock=1;
	params->dsi.mode = SYNC_PULSE_VDO_MODE;
	params->dsi.LANE_NUM = LCM_FOUR_LANE;
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;
	params->dsi.intermediat_buffer_num = 2;
	params->dsi.PS=LCM_PACKED_PS_24BIT_RGB888;
	params->dsi.packet_size=256;
	params->dsi.vertical_sync_active = 8;
	params->dsi.vertical_backporch = 24;
	params->dsi.vertical_frontporch = 16;
	params->dsi.vertical_active_line = FRAME_HEIGHT;
	params->dsi.horizontal_sync_active = 40;
	params->dsi.horizontal_backporch = 40;
	params->dsi.horizontal_frontporch = 60;
	params->dsi.horizontal_blanking_pixel = 60;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	params->dsi.PLL_CLOCK = 207;
	params->dsi.ssc_disable = 1;
	params->dsi.HS_TRAIL = 10;
	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1;
	params->dsi.lcm_esd_check_table[0].cmd = 0x0A;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;
}

static void lcm_bias(unsigned int value)
{
	/* Reuse the existing M5s 3.18 GPIO path; 4.9 MTK_GPIO exports this API. */
	mt_set_gpio_mode(GPIO_LCM_PWR_EN, GPIO_MODE_00);
	mt_set_gpio_dir(GPIO_LCM_PWR_EN, GPIO_DIR_OUT);
	mt_set_gpio_out(GPIO_LCM_PWR_EN, value);
}

static void lcm_init(void)
{
	lcm_bias(GPIO_OUT_ONE);
	MDELAY(10);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(1);
	SET_RESET_PIN(0);
	MDELAY(10);
	SET_RESET_PIN(1);
	MDELAY(20);
	push_table(lcm_initialization_setting1,
		   ARRAY_SIZE(lcm_initialization_setting1));
}

static void lcm_suspend(void)
{
	push_table(lcm_deep_sleep_mode_in_setting,
		   ARRAY_SIZE(lcm_deep_sleep_mode_in_setting));
	lcm_bias(GPIO_OUT_ZERO);
	MDELAY(10);
	MDELAY(10);
}

static void lcm_resume(void)
{
	lcm_init();
}

struct LCM_DRIVER ili9881_CA_hd720_dsi_vdo_yassy_lcm_drv = {
	.name = "ili9881_CA_hd720_dsi_vdo_yassy",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	/* 4.9 disp_lcm_probe selects the LK name; do not reuse the donor ID probe. */
};
