/*
* Copyright (C) 2016 MediaTek Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See http://www.gnu.org/licenses/gpl-2.0.html for more details.
*/
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/atomic.h>

#include "kd_camera_hw.h"

#include "kd_imgsensor.h"
#include "kd_imgsensor_define.h"
#include "kd_camera_feature.h"

/******************************************************************************
 * Debug configuration
******************************************************************************/
#define PFX "[kd_camera_hw]"
#define PK_DBG_NONE(fmt, arg...)    do {} while (0)
#define PK_DBG_FUNC(fmt, args...)    pr_debug(PFX  fmt, ##args)

#define DEBUG_CAMERA_HW_K
#ifdef DEBUG_CAMERA_HW_K
#define PK_DBG PK_DBG_FUNC
#define PK_ERR(fmt, arg...) pr_err(fmt, ##arg)
#define PK_XLOG_INFO(fmt, args...)  pr_debug(PFX  fmt, ##args)

#else
#define PK_DBG(a, ...)
#define PK_ERR(a, ...)
#define PK_XLOG_INFO(fmt, args...)
#endif


#if 1// !defined(CONFIG_MTK_LEGACY)

/* GPIO Pin control*/
struct platform_device *cam_plt_dev = NULL;
struct pinctrl *camctrl = NULL;
struct pinctrl_state *cam0_pnd_h = NULL;
struct pinctrl_state *cam0_pnd_l = NULL;
struct pinctrl_state *cam0_rst_h = NULL;
struct pinctrl_state *cam0_rst_l = NULL;
struct pinctrl_state *cam1_pnd_h = NULL;
struct pinctrl_state *cam1_pnd_l = NULL;
struct pinctrl_state *cam1_rst_h = NULL;
struct pinctrl_state *cam1_rst_l = NULL;
struct pinctrl_state *cam_ldo0_h = NULL;
struct pinctrl_state *cam_ldo0_l = NULL;

int mtkcam_gpio_init(struct platform_device *pdev)
{
        int ret = 0;

        camctrl = devm_pinctrl_get(&pdev->dev);
        if (IS_ERR(camctrl)) {
                dev_err(&pdev->dev, "Cannot find camera pinctrl!");
                ret = PTR_ERR(camctrl);
        }
        /*Cam0 Power/Rst Ping initialization */
        cam0_pnd_h = pinctrl_lookup_state(camctrl, "cam0_pnd1");
        if (IS_ERR(cam0_pnd_h)) {
                ret = PTR_ERR(cam0_pnd_h);
                pr_debug("%s : pinctrl err, cam0_pnd_h\n", __func__);
        }

        cam0_pnd_l = pinctrl_lookup_state(camctrl, "cam0_pnd0");
        if (IS_ERR(cam0_pnd_l)) {
                ret = PTR_ERR(cam0_pnd_l);
                pr_debug("%s : pinctrl err, cam0_pnd_l\n", __func__);
        }


        cam0_rst_h = pinctrl_lookup_state(camctrl, "cam0_rst1");
        if (IS_ERR(cam0_rst_h)) {
                ret = PTR_ERR(cam0_rst_h);
                pr_debug("%s : pinctrl err, cam0_rst_h\n", __func__);
        }

        cam0_rst_l = pinctrl_lookup_state(camctrl, "cam0_rst0");
        if (IS_ERR(cam0_rst_l)) {
                ret = PTR_ERR(cam0_rst_l);
                pr_debug("%s : pinctrl err, cam0_rst_l\n", __func__);
        }

        /*Cam1 Power/Rst Ping initialization */
        cam1_pnd_h = pinctrl_lookup_state(camctrl, "cam1_pnd1");
        if (IS_ERR(cam1_pnd_h)) {
                ret = PTR_ERR(cam1_pnd_h);
                pr_debug("%s : pinctrl err, cam1_pnd_h\n", __func__);
        }

        cam1_pnd_l = pinctrl_lookup_state(camctrl, "cam1_pnd0");
        if (IS_ERR(cam1_pnd_l)) {
                ret = PTR_ERR(cam1_pnd_l);
                pr_debug("%s : pinctrl err, cam1_pnd_l\n", __func__);
        }


        cam1_rst_h = pinctrl_lookup_state(camctrl, "cam1_rst1");
        if (IS_ERR(cam1_rst_h)) {
                ret = PTR_ERR(cam1_rst_h);
                pr_debug("%s : pinctrl err, cam1_rst_h\n", __func__);
        }


        cam1_rst_l = pinctrl_lookup_state(camctrl, "cam1_rst0");
        if (IS_ERR(cam1_rst_l)) {
                ret = PTR_ERR(cam1_rst_l);
                pr_debug("%s : pinctrl err, cam1_rst_l\n", __func__);
        }
        /*externel LDO enable */
        cam_ldo0_h = pinctrl_lookup_state(camctrl, "cam_ldo0_1");
        if (IS_ERR(cam_ldo0_h)) {
                ret = PTR_ERR(cam_ldo0_h);
                pr_debug("%s : pinctrl err, cam_ldo0_h\n", __func__);
        }


        cam_ldo0_l = pinctrl_lookup_state(camctrl, "cam_ldo0_0");
        if (IS_ERR(cam_ldo0_l)) {
                ret = PTR_ERR(cam_ldo0_l);
                pr_debug("%s : pinctrl err, cam_ldo0_l\n", __func__);
        }
        return ret;
}
EXPORT_SYMBOL(mtkcam_gpio_init);

int mtkcam_gpio_set(int PinIdx, int PwrType, int Val)
{
        /* forge (m5c): rewritten to (a) name the pinctrl state it drives in
         * dmesg - the previous bring-up round could not tell from the logs
         * which pins actually moved - and (b) refuse to feed an ERR pointer
         * from a failed pinctrl_lookup_state() into pinctrl_select_state().
         * Selection logic is unchanged: pin set 0 = cam0_* (main), everything
         * else = cam1_* (front), matching both the stock DTB and our DTS
         * pinctrl-names.
         */
        int ret = 0;
        struct pinctrl_state *st = NULL;
        const char *st_name = "?";

        switch (PwrType) {
        case CAMRST:
                if (PinIdx == 0) {
                        st = Val ? cam0_rst_h : cam0_rst_l;
                        st_name = Val ? "cam0_rst1" : "cam0_rst0";
                } else {
                        st = Val ? cam1_rst_h : cam1_rst_l;
                        st_name = Val ? "cam1_rst1" : "cam1_rst0";
                }
                break;
        case CAMPDN:
                if (PinIdx == 0) {
                        st = Val ? cam0_pnd_h : cam0_pnd_l;
                        st_name = Val ? "cam0_pnd1" : "cam0_pnd0";
                } else {
                        st = Val ? cam1_pnd_h : cam1_pnd_l;
                        st_name = Val ? "cam1_pnd1" : "cam1_pnd0";
                }
                break;
        case CAMLDO:
                st = Val ? cam_ldo0_h : cam_ldo0_l;
                st_name = Val ? "cam_ldo0_1" : "cam_ldo0_0";
                break;
        default:
                pr_err("[kd_camera_hw] mtkcam_gpio_set: PwrType(%d) is invalid\n",
                       PwrType);
                return 0;
        };

        if (IS_ERR_OR_NULL(camctrl) || IS_ERR_OR_NULL(st)) {
                pr_err("[kd_camera_hw] pinctrl %s unavailable (PinIdx=%d PwrType=%d Val=%d)\n",
                       st_name, PinIdx, PwrType, Val);
                return 0;
        }

        ret = pinctrl_select_state(camctrl, st);
        pr_info("[kd_camera_hw] pinctrl %s ret=%d\n", st_name, ret);

        return ret;
}




int cntVCAMD = 0;
int cntVCAMA = 0;
int cntVCAMIO = 0;
int cntVCAMAF = 0;
int cntVCAMD_SUB = 0;

static DEFINE_SPINLOCK(kdsensor_pw_cnt_lock);


bool _hwPowerOnCnt(KD_REGULATOR_TYPE_T powerId, int powerVolt, char *mode_name)
{

        if (_hwPowerOn(powerId, powerVolt)) {
                spin_lock(&kdsensor_pw_cnt_lock);
                if (powerId == VCAMD)
                        cntVCAMD += 1;
                else if (powerId == VCAMA)
                        cntVCAMA += 1;
                else if (powerId == VCAMIO)
                        cntVCAMIO += 1;
                else if (powerId == VCAMAF)
                        cntVCAMAF += 1;
                else if (powerId == SUB_VCAMD)
                        cntVCAMD_SUB += 1;
                spin_unlock(&kdsensor_pw_cnt_lock);
                return true;
        }
        return false;
}

bool _hwPowerDownCnt(KD_REGULATOR_TYPE_T powerId, char *mode_name)
{

        if (_hwPowerDown(powerId)) {
                spin_lock(&kdsensor_pw_cnt_lock);
                if (powerId == VCAMD)
                        cntVCAMD -= 1;
                else if (powerId == VCAMA)
                        cntVCAMA -= 1;
                else if (powerId == VCAMIO)
                        cntVCAMIO -= 1;
                else if (powerId == VCAMAF)
                        cntVCAMAF -= 1;
                else if (powerId == SUB_VCAMD)
                        cntVCAMD_SUB -= 1;
                spin_unlock(&kdsensor_pw_cnt_lock);
                return true;
        }
        return false;
}

void checkPowerBeforClose(char *mode_name)
{

        int i = 0;

        PK_DBG
            ("[checkPowerBeforClose]cntVCAMD:%d, cntVCAMA:%d,cntVCAMIO:%d, cntVCAMAF:%d, cntVCAMD_SUB:%d,\n",
             cntVCAMD, cntVCAMA, cntVCAMIO, cntVCAMAF, cntVCAMD_SUB);


        for (i = 0; i < cntVCAMD; i++)
                _hwPowerDown(VCAMD);
        for (i = 0; i < cntVCAMA; i++)
                _hwPowerDown(VCAMA);
        for (i = 0; i < cntVCAMIO; i++)
                _hwPowerDown(VCAMIO);
        for (i = 0; i < cntVCAMAF; i++)
                _hwPowerDown(VCAMAF);
        for (i = 0; i < cntVCAMD_SUB; i++)
                _hwPowerDown(SUB_VCAMD);

        cntVCAMD = 0;
        cntVCAMA = 0;
        cntVCAMIO = 0;
        cntVCAMAF = 0;
        cntVCAMD_SUB = 0;

}
EXPORT_SYMBOL(checkPowerBeforClose);



/* forge (m5c): which rails the s5k5e8 ON branch actually enabled. On 4.9
 * regulator_disable() decrements the shared rdev use_count, so blindly
 * disabling a rail this driver did not enable steals someone else's enable
 * reference (vgp1 is also claimed by the gt9xx touch driver). Calls are
 * serialized by kdCam_Mutex.
 */
/* the front module hangs off the cam1_* pinctrl states, i.e. pin set 1 */
#define S5K5E8_PIN_SET		1
#define S5K5E8_RAIL_VCAMA	0x1
#define S5K5E8_RAIL_SUBVCAMD	0x2
#define S5K5E8_RAIL_VCAMIO	0x4
#define S5K5E8_RAIL_VCAMD	0x8
static unsigned int s5k5e8_rails_on;

/*
 * forge (m5c): front-camera experiments, chosen per test kernel at build
 * time (-DFORGE_S5K5E8_PDN_DEFAULT / -DFORGE_S5K5E8_VCAMD_DEFAULT) and
 * readable in /sys/module/kernel/parameters/. The probe answers I2C_ACKERR
 * with power, reset and MCLK as in the stock kdCISModulePowerOn.
 *  forge_s5k5e8_pdn:   0 drive cam1 PDN low (since 9a241b972),
 *                      1 drive it high, -1 leave it alone (stock never
 *                      touches it in this branch).
 *  forge_s5k5e8_vcamd: 1 also enable VCAMD at 1.2V - the stock DTB points
 *                      vcamd_sub-supply at the vcamd LDO, the stock code
 *                      uses vgp1; this powers both.
 */
#ifndef FORGE_S5K5E8_PDN_DEFAULT
#define FORGE_S5K5E8_PDN_DEFAULT 0
#endif
#ifndef FORGE_S5K5E8_VCAMD_DEFAULT
#define FORGE_S5K5E8_VCAMD_DEFAULT 0
#endif
static int forge_s5k5e8_pdn = FORGE_S5K5E8_PDN_DEFAULT;
core_param(forge_s5k5e8_pdn, forge_s5k5e8_pdn, int, 0444);
static int forge_s5k5e8_vcamd = FORGE_S5K5E8_VCAMD_DEFAULT;
core_param(forge_s5k5e8_vcamd, forge_s5k5e8_vcamd, int, 0444);
extern void forge_cam_rail_report(const char *tag);

int kdCISModulePowerOn(enum CAMERA_DUAL_CAMERA_SENSOR_ENUM SensorIdx, char *currSensorName, bool On,
                       char *mode_name)
{

        u32 pinSetIdx = 0;

#define IDX_PS_CMRST 0
#define IDX_PS_CMPDN 4
#define IDX_PS_MODE 1
#define IDX_PS_ON   2
#define IDX_PS_OFF  3

#define VOL_2800 2800000
#define VOL_1800 1800000
#define VOL_1500 1500000
#define VOL_1200 1200000
#define VOL_1000 1000000

        u32 pinSet[3][8] = {

                {CAMERA_CMRST_PIN,
                 CAMERA_CMRST_PIN_M_GPIO,	/* mode */
                 GPIO_OUT_ONE,	/* ON state */
                 GPIO_OUT_ZERO,	/* OFF state */
                 CAMERA_CMPDN_PIN,
                 CAMERA_CMPDN_PIN_M_GPIO,
                 GPIO_OUT_ONE,
                 GPIO_OUT_ZERO,
                 },
                {CAMERA_CMRST1_PIN,
                 CAMERA_CMRST1_PIN_M_GPIO,
                 GPIO_OUT_ONE,
                 GPIO_OUT_ZERO,
                 CAMERA_CMPDN1_PIN,
                 CAMERA_CMPDN1_PIN_M_GPIO,
                 GPIO_OUT_ONE,
                 GPIO_OUT_ZERO,
                 },
                {GPIO_CAMERA_INVALID,
                 GPIO_CAMERA_INVALID,	/* mode */
                 GPIO_OUT_ONE,	/* ON state */
                 GPIO_OUT_ZERO,	/* OFF state */
                 GPIO_CAMERA_INVALID,
                 GPIO_CAMERA_INVALID,
                 GPIO_OUT_ONE,
                 GPIO_OUT_ZERO,
                 }
        };



        if (DUAL_CAMERA_MAIN_SENSOR == SensorIdx)
                pinSetIdx = 0;
         else if (DUAL_CAMERA_SUB_SENSOR == SensorIdx)
                pinSetIdx = 1;
         else if (DUAL_CAMERA_MAIN_2_SENSOR == SensorIdx)
                pinSetIdx = 2;

        /* forge (m5c): visible entry marker - every other print in this
         * function is pr_debug and the previous bring-up round could not even
         * tell from dmesg whether this function runs. One line per call.
         */
        pr_info("[kd_camera_hw] power %s pinSetIdx=%d sensor=%s\n",
                On ? "ON" : "OFF", pinSetIdx,
                currSensorName ? currSensorName : "(null)");

        if (On) {
                if(currSensorName &&
                        (0 == strcmp(currSensorName,"imx135_mipi_raw") ||
                        0 == strcmp(currSensorName,SENSOR_DRVNAME_IMX135_MIPI_RAW_3MP)||
                        0 == strcmp(currSensorName,SENSOR_DRVNAME_IMX135_MIPI_RAW_5MP)||
                        0 == strcmp(currSensorName,SENSOR_DRVNAME_IMX135_MIPI_RAW_8MP))
                        &&(pinSetIdx != 0))
                        PK_DBG("[PowerON]135 not main\n");
                //else if(currSensorName && (0 == strcmp(currSensorName,SENSOR_DRVNAME_OV8856_MIPI_RAW))&&(pinSetIdx != 0))
                        //PK_DBG("[PowerON]OV8856 not main\n");
                else
                        ISP_MCLK1_EN(1);

                PK_DBG("[PowerON]pinSetIdx:%d, currSensorName: %s\n", pinSetIdx, currSensorName);

                if ((currSensorName && (0 == strcmp(currSensorName, "imx135_mipi_raw"))) ||
                        (currSensorName && (0 == strcmp(currSensorName, SENSOR_DRVNAME_IMX135_MIPI_RAW_3MP))) ||
                        (currSensorName && (0 == strcmp(currSensorName, SENSOR_DRVNAME_IMX135_MIPI_RAW_5MP))) ||
                        (currSensorName && (0 == strcmp(currSensorName, SENSOR_DRVNAME_IMX135_MIPI_RAW_8MP))) ||
                    (currSensorName && (0 == strcmp(currSensorName, "imx220mipiraw")))) {
                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }


                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }

                        if (TRUE != _hwPowerOnCnt(VCAMAF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);


                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A), power id = %d\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1000, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D), power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);


                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(2);



                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);

                                if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                        mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                        pinSet[pinSetIdx][IDX_PS_CMPDN +
                                                                          IDX_PS_ON]);
                                }
                        }

                } else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_OV5648_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 1);

                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);


                        /* VCAM_IO */
                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR]Fail to enable digital power(VCAM_IO),power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* VCAM_A */
                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A),power id = %d\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1500, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D),power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(5);

                        /* AF_VCC */
                        if (TRUE != _hwPowerOnCnt(VCAMAF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF),power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);

                        mdelay(2);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);

                        mdelay(20);
//+add by ontim
                }
                else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_OV8856_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 1);

                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);


                        /* VCAM_IO */
                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR]Fail to enable digital power(VCAM_IO),power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* VCAM_A */
                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A),power id = %d\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1500, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D),power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(5);

                        /* AF_VCC */
                        if (TRUE != _hwPowerOnCnt(VCAMAF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF),power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);

                        mdelay(2);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);

                        mdelay(20);
//+add by ontim
                    }

        else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_HI545_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 1);

                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);


                        /* VCAM_IO */
                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR]Fail to enable digital power(VCAM_IO),power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* VCAM_A */
                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A),power id = %d\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1500, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D),power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(5);

                        /* AF_VCC */
                        if (TRUE != _hwPowerOnCnt(VCAMAF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF),power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);

                        mdelay(2);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);

                        mdelay(20);
//-add by ontim
                }
                else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_HI843_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 1);

                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);


                        /* VCAM_IO */
                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR]Fail to enable digital power(VCAM_IO),power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* VCAM_A */
                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A),power id = %d\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1200, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D),power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(5);

                        /* AF_VCC */
                        if (TRUE != _hwPowerOnCnt(VCAMAF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF),power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);
#if 1
                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);
                        printk(KERN_ERR " camera wwwwwwwwwwww  line:%d\n",__LINE__);


                        mdelay(2);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);
                        printk(KERN_ERR " camera wwwwwwwwwwww  line:%d\n",__LINE__);

#else
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                                mdelay(5);
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);
                        }
                        mdelay(2);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                                mdelay(5);
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);
                        }

#endif
                        mdelay(20);
//-add by ontim
                }
                else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_GC2355_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 1);
                        /* First Power Pin High and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);

                        mdelay(50);

                        /* VCAM_A */
                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A),power id = %d\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);

                        /* VCAM_IO */
                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable IO power (VCAM_IO),power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);

                        if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1500, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D),power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);

                        /* AF_VCC */
                        if (TRUE != _hwPowerOnCnt(VCAMAF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF),power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }


                        mdelay(50);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                                mdelay(5);
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);
                        }
                        mdelay(5);
                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);
                                mdelay(5);
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }

                        mdelay(5);
//+add by ontim
                }
                else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_GC2365_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 1);
                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);

                        mdelay(50);

                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable IO power (VCAM_IO),power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);

                        if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1200, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D),power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        mdelay(10);
                        /* VCAM_A */
                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A),power id = %d\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }

                         mdelay(10);

                        /* AF_VCC */
                        if (TRUE != _hwPowerOnCnt(VCAMAF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF),power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }


                        mdelay(50);

                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);
                                mdelay(5);
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }

                        mdelay(5);
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                                mdelay(5);
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);
                        }
                        mdelay(5);

//+add by ontim
                    }

                else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_HI259_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 1);
                        /* First Power Pin low and Reset Pin Low */

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN])
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST])
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);

                        mdelay(50);
                        /* VCAM_A */
                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A),power id = %d\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);

                        /* VCAM_IO */
                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable IO power (VCAM_IO),power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);


#if 1
                        if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1200, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D),power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);
#endif
                        /* AF_VCC */
                        if (TRUE != _hwPowerOnCnt(VCAMAF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF),power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }


                        mdelay(50);
#if 1
                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);
                        }
                        mdelay(5);
            printk(KERN_ERR " camera wwwwwwwwwwww  line:%d\n",__LINE__);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);
                        }
#else
                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);
                                mdelay(5);
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }
                        mdelay(5);
            printk(KERN_ERR " camera wwwwwwwwwwww  line:%d\n",__LINE__);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                                mdelay(5);
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);
                        }

#endif
                        mdelay(5);
//+add by ontim
                    }
                /* forge (m5c): front S5K5E8 module. The stock Flyme kernel powers
                 * it in a dedicated branch (kdCISModulePowerOn @ 0xffffffc0005bd318
                 * in the stock vmlinux.elf): both resets low -> VCAMA 2.8V ->
                 * VGP1 1.2V (the sub sensor digital core, NOT VCAMD) -> VCAMIO
                 * 1.8V -> RST1 high -> MCLK on. CMPDN1 is never driven and
                 * VCAMD / VCAMAF are never enabled for this sensor. The generic
                 * branch below powered the wrong core rail (VCAMD @ 1.5V) and
                 * left the real one (VGP1) untouched, so the sensor NACKed on
                 * every i2c address. SUB_VCAMD resolves to vgp1 in
                 * Get_Cam_Regulator().
                 */
                    else if (currSensorName && pinSetIdx == S5K5E8_PIN_SET
                           && ((0 == strcmp(SENSOR_DRVNAME_S5K5E8_ST_MIPI_RAW, currSensorName))
                           || (0 == strcmp(SENSOR_DRVNAME_S5K5E8_QH_MIPI_RAW, currSensorName))
                           || (0 == strcmp(SENSOR_DRVNAME_S5K5E8_HOLITECH_MIPI_RAW, currSensorName))
                           || (0 == strcmp(SENSOR_DRVNAME_S5K5E8_SUNWIN_MIPI_RAW, currSensorName))
                           || (0 == strcmp(SENSOR_DRVNAME_S5K5E8YX_MIPI_RAW, currSensorName)))) {













                        /*
                         * forge (m5c): power the front module whenever ITS driver
                         * is the one being probed, whatever socket the HAL named.
                         *
                         * Stock gates this branch on pinSetIdx == 1 (vmlinux
                         * 0x5bc044: "cmp w19,#1; b.ne <generic>") because on stock
                         * the HAL's second search pass really did arrive with
                         * socket 2. Here it never does: the sub pass shows up as
                         * SET_DRIVER slot1 socket=2 drvIdx=255, i.e. an empty
                         * slot, so with that gate the front sensor is only ever
                         * probed from the MAIN pass - through the generic branch,
                         * which powers VCAMD 1.5V (the wrong rail for this module)
                         * and drives cam0's reset. Live result: the sensor ACKs on
                         * 0x78 with no bus error but reads back id 0x0, exactly
                         * what an addressable-but-held-in-reset sensor looks like.
                         *
                         * So key the branch on the driver name and always drive the
                         * front module's own pins (set 1), regardless of pinSetIdx.
                         * The main sensor's reset is pulled low first either way,
                         * so only one of the two ever answers on the shared bus.
                         */
                        pr_info("[kd_camera_hw] s5k5e8 stock power branch (sub pins, called with pinSetIdx=%d)\n",
                                pinSetIdx);

                        /* keep the (unpowered) main sensor off the shared i2c bus,
                         * exactly as stock does */
                        mtkcam_gpio_set(0, CAMRST, GPIO_OUT_ZERO);

                        /*
                         * forge (m5c): take the front module out of power-down.
                         *
                         * Stock never touches CMPDN1 here, but stock also never
                         * runs the generic branch for this sensor - here the
                         * MAIN pass does, and that branch drives CMPDN of the
                         * pin set it was called with. Leaving the line asserted
                         * keeps the sensor asleep, which is exactly what the bus
                         * reports: I2C_ACKERR on every address of its table
                         * while power, reset and MCLK are all correct.
                         */
                        if (forge_s5k5e8_pdn >= 0 && GPIO_CAMERA_INVALID !=
                            pinSet[S5K5E8_PIN_SET][IDX_PS_CMPDN])
                                mtkcam_gpio_set(S5K5E8_PIN_SET, CAMPDN,
                                                pinSet[S5K5E8_PIN_SET][IDX_PS_CMPDN +
                                                (forge_s5k5e8_pdn ? IDX_PS_ON : IDX_PS_OFF)]);
                        pr_info("[kd_camera_hw] s5k5e8: pdn mode %d, extra vcamd %d\n",
                                forge_s5k5e8_pdn, forge_s5k5e8_vcamd);

                        if (GPIO_CAMERA_INVALID != pinSet[S5K5E8_PIN_SET][IDX_PS_CMRST])
                                mtkcam_gpio_set(S5K5E8_PIN_SET, CAMRST,
                                                pinSet[S5K5E8_PIN_SET][IDX_PS_CMRST + IDX_PS_OFF]);

                        mdelay(1);

                        /* VCAM_A */
                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                pr_err("[kd_camera_hw] s5k5e8: Fail to enable analog power (VCAM_A)\n");
                                goto _kdCISModulePowerOn_exit_;
                        }
                        s5k5e8_rails_on |= S5K5E8_RAIL_VCAMA;

                        mdelay(1);

                        /* digital core from VGP1 @ 1.2V, as in stock */
                        if (TRUE != _hwPowerOnCnt(SUB_VCAMD, VOL_1200, mode_name)) {
                                pr_err("[kd_camera_hw] s5k5e8: Fail to enable digital power (SUB_VCAMD/vgp1)\n");
                                /* forge (m5c): back out, or every 5s HAL retry leaks
                                 * one VCAMA enable ref (seen live: vcama stuck at
                                 * users=1 between probes) */
                                if (TRUE == _hwPowerDownCnt(VCAMA, mode_name))
                                        s5k5e8_rails_on &= ~S5K5E8_RAIL_VCAMA;
                                goto _kdCISModulePowerOn_exit_;
                        }
                        s5k5e8_rails_on |= S5K5E8_RAIL_SUBVCAMD;

                        mdelay(1);

                        /* VCAM_IO */
                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                pr_err("[kd_camera_hw] s5k5e8: Fail to enable IO power (VCAM_IO)\n");
                                if (TRUE == _hwPowerDownCnt(SUB_VCAMD, mode_name))
                                        s5k5e8_rails_on &= ~S5K5E8_RAIL_SUBVCAMD;
                                if (TRUE == _hwPowerDownCnt(VCAMA, mode_name))
                                        s5k5e8_rails_on &= ~S5K5E8_RAIL_VCAMA;
                                goto _kdCISModulePowerOn_exit_;
                        }
                        s5k5e8_rails_on |= S5K5E8_RAIL_VCAMIO;

                        if (forge_s5k5e8_vcamd) {
                                if (TRUE == _hwPowerOnCnt(VCAMD, VOL_1200, mode_name))
                                        s5k5e8_rails_on |= S5K5E8_RAIL_VCAMD;
                                else
                                        pr_err("[kd_camera_hw] s5k5e8: Fail to enable VCAMD 1.2V (experiment)\n");
                        }

                        mdelay(2);

                        if (GPIO_CAMERA_INVALID != pinSet[S5K5E8_PIN_SET][IDX_PS_CMRST])
                                mtkcam_gpio_set(S5K5E8_PIN_SET, CAMRST,
                                                pinSet[S5K5E8_PIN_SET][IDX_PS_CMRST + IDX_PS_ON]);

                        mdelay(1);
                        /* stock turns MCLK on at this point; ISP_MCLK1_EN(1)
                         * already ran at the top of this function */
                        forge_cam_rail_report("s5k5e8 on");
                    }
                    else {

                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }
                        /*
                         * forge (m5c, kernel K): an S5K5E8 entry probed on the MAIN
                         * socket. Stock sends it here as well (pinSetIdx 0 only
                         * special-cases the S5K4H8 names), so the main module gets
                         * its normal power. But nothing drives the front module's
                         * reset before the sub pass: on kernel J the very first
                         * main-pass probe found RST1 high, the front module ACKed at
                         * 0x30 with 0x5e80 and the HAL bound camera 0 to the s5k5e8
                         * driver (FAILED_OPEN_4). Hold the front module in reset so
                         * the main pass cannot see it; its own branch raises RST1
                         * again on the sub pass. Main socket power is unchanged.
                         */
                        if (pinSetIdx == 0 && currSensorName
                            && (0 == strncmp(currSensorName, "s5k5e8", 6))
                            && GPIO_CAMERA_INVALID != pinSet[S5K5E8_PIN_SET][IDX_PS_CMRST]) {
                                pr_info("[kd_camera_hw] main pass %s: front module held in reset\n",
                                        currSensorName);
                                mtkcam_gpio_set(S5K5E8_PIN_SET, CAMRST,
                                                pinSet[S5K5E8_PIN_SET][IDX_PS_CMRST + IDX_PS_OFF]);
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerOnCnt(VCAMIO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerOnCnt(VCAMA, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A), power id = %d\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_D */
                        if (currSensorName
                            && (0 == strcmp(SENSOR_DRVNAME_S5K2P8_MIPI_RAW, currSensorName))) {
                                if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1200, mode_name)) {
                                        PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
                                        goto _kdCISModulePowerOn_exit_;
                                }
                        } else if (currSensorName
                                   && (0 ==
                                       strcmp(SENSOR_DRVNAME_IMX219_MIPI_RAW, currSensorName))) {
                                if (pinSetIdx == 0
                                    && TRUE != _hwPowerOnCnt(VCAMD, VOL_1200, mode_name)) {
                                        PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
                                        goto _kdCISModulePowerOn_exit_;
                                }
                        } else {	/* Main VCAMD max 1.5V */
                                if (TRUE != _hwPowerOnCnt(VCAMD, VOL_1500, mode_name)) {
                                        PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
                                        goto _kdCISModulePowerOn_exit_;
                                }

                        }


                        /* AF_VCC */
                        if (TRUE != _hwPowerOnCnt(VCAMAF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(5);

                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);
                        }

                        mdelay(1);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON]);
                        }
                }

        } else {		/* power OFF */

                PK_DBG("[PowerOFF]pinSetIdx:%d\n", pinSetIdx);

                if ((currSensorName && (0 == strcmp(currSensorName, "imx135_mipi_raw"))) ||
                        (currSensorName && (0 == strcmp(currSensorName, SENSOR_DRVNAME_IMX135_MIPI_RAW_3MP))) ||
                        (currSensorName && (0 == strcmp(currSensorName, SENSOR_DRVNAME_IMX135_MIPI_RAW_5MP))) ||
                        (currSensorName && (0 == strcmp(currSensorName, SENSOR_DRVNAME_IMX135_MIPI_RAW_8MP))) ||
                    (currSensorName && (0 == strcmp(currSensorName, "imx220mipiraw")))) {
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDownCnt(VCAMAF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDownCnt(VCAMIO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        if (TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDownCnt(VCAMA, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }

                } else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_OV5648_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 0);
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }


                        if (TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDownCnt(VCAMA, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDownCnt(VCAMIO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDownCnt(VCAMAF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }
//+add by ontim
                }
                else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_OV8856_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 0);
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }


                        if (TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDownCnt(VCAMA, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDownCnt(VCAMIO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDownCnt(VCAMAF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }
                    //+add by ontim
                    }
                    else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_HI545_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 0);
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }


                        if (TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDownCnt(VCAMA, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDownCnt(VCAMIO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDownCnt(VCAMAF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

//-add by ontim
                }
                 else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_HI843_MIPI_RAW, currSensorName))) {
                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 0);
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }


                        if (TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDownCnt(VCAMA, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDownCnt(VCAMIO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDownCnt(VCAMAF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

//-add by ontim
                }
                else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_GC2355_MIPI_RAW, currSensorName))) {
                        /* Set PDN Pin Low->high */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }

                        ISP_MCLK1_EN(0);

                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 0);
                        if (TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDownCnt(VCAMA, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDownCnt(VCAMIO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDownCnt(VCAMAF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }
//+add by ontim
                }
             else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_GC2365_MIPI_RAW, currSensorName))) {


                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 0);
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDownCnt(VCAMIO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        if (TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDownCnt(VCAMA, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }
                                        /* AF_VCC */
                        if (TRUE != _hwPowerDownCnt(VCAMAF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                                ISP_MCLK1_EN(0);

                                /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON]);
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }

//+add by ontim
                }

                    else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_HI259_MIPI_RAW, currSensorName))) {
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }

                        ISP_MCLK1_EN(0);

                        mtkcam_gpio_set(pinSetIdx, CAMLDO, 0);
            #if 1
                        if (TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     VCAMD);
                                goto _kdCISModulePowerOn_exit_;
                        }
            #endif
                        /* VCAM_A */
                        if (TRUE != _hwPowerDownCnt(VCAMA, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDownCnt(VCAMIO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDownCnt(VCAMAF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }
//-add by ontim
                }
                /* forge (m5c): mirror of the s5k5e8 power-on branch above - only
                 * the three rails it enabled may be released here, otherwise the
                 * regulator enable counts underflow. Stock order: MCLK off (done
                 * in the common tail below), RST low, VGP1/VCAMIO/VCAMA off.
                 */
                    else if (currSensorName && pinSetIdx == S5K5E8_PIN_SET
                           && ((0 == strcmp(SENSOR_DRVNAME_S5K5E8_ST_MIPI_RAW, currSensorName))
                           || (0 == strcmp(SENSOR_DRVNAME_S5K5E8_QH_MIPI_RAW, currSensorName))
                           || (0 == strcmp(SENSOR_DRVNAME_S5K5E8_HOLITECH_MIPI_RAW, currSensorName))
                           || (0 == strcmp(SENSOR_DRVNAME_S5K5E8_SUNWIN_MIPI_RAW, currSensorName))
                           || (0 == strcmp(SENSOR_DRVNAME_S5K5E8YX_MIPI_RAW, currSensorName)))) {













                        /* forge (m5c): mirrors the ON branch, which is keyed on the
                         * driver name rather than the socket -
                         * a MAIN-socket pass powered up via the generic branch and
                         * must be released by the generic branch. */

                        if (GPIO_CAMERA_INVALID != pinSet[S5K5E8_PIN_SET][IDX_PS_CMRST])
                                mtkcam_gpio_set(S5K5E8_PIN_SET, CAMRST,
                                                pinSet[S5K5E8_PIN_SET][IDX_PS_CMRST + IDX_PS_OFF]);

                        mdelay(1);

                        /* forge (m5c): release ONLY the rails the ON branch
                         * enabled (mask above) - a blind disable after a failed
                         * probe would steal the gt9xx touch driver's vgp1 enable
                         * reference. Try every owed rail even if one trips.
                         */
                        {
                                int pwr_fail = 0;

                                if (s5k5e8_rails_on & S5K5E8_RAIL_VCAMD) {
                                        if (TRUE == _hwPowerDownCnt(VCAMD, mode_name))
                                                s5k5e8_rails_on &= ~S5K5E8_RAIL_VCAMD;
                                        else {
                                                pr_err("[kd_camera_hw] s5k5e8: Fail to OFF VCAMD (experiment)\n");
                                                pwr_fail = 1;
                                        }
                                }
                                if (s5k5e8_rails_on & S5K5E8_RAIL_VCAMIO) {
                                        if (TRUE == _hwPowerDownCnt(VCAMIO, mode_name))
                                                s5k5e8_rails_on &= ~S5K5E8_RAIL_VCAMIO;
                                        else {
                                                pr_err("[kd_camera_hw] s5k5e8: Fail to OFF IO power (VCAM_IO)\n");
                                                pwr_fail = 1;
                                        }
                                }
                                if (s5k5e8_rails_on & S5K5E8_RAIL_SUBVCAMD) {
                                        if (TRUE == _hwPowerDownCnt(SUB_VCAMD, mode_name))
                                                s5k5e8_rails_on &= ~S5K5E8_RAIL_SUBVCAMD;
                                        else {
                                                pr_err("[kd_camera_hw] s5k5e8: Fail to OFF digital power (SUB_VCAMD/vgp1)\n");
                                                pwr_fail = 1;
                                        }
                                }
                                if (s5k5e8_rails_on & S5K5E8_RAIL_VCAMA) {
                                        if (TRUE == _hwPowerDownCnt(VCAMA, mode_name))
                                                s5k5e8_rails_on &= ~S5K5E8_RAIL_VCAMA;
                                        else {
                                                pr_err("[kd_camera_hw] s5k5e8: Fail to OFF analog power (VCAM_A)\n");
                                                pwr_fail = 1;
                                        }
                                }
                                if (pwr_fail)
                                        goto _kdCISModulePowerOn_exit_;
                        }
                }
                    else
                {
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                mtkcam_gpio_set(pinSetIdx, CAMPDN,
                                                pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF]);
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                mtkcam_gpio_set(pinSetIdx, CAMRST,
                                                pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF]);
                        }


                        if (currSensorName
                            && (0 == strcmp(SENSOR_DRVNAME_IMX219_MIPI_RAW, currSensorName))) {
                                if (pinSetIdx == 0 && TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                        PK_DBG
                                            ("[CAMERA SENSOR] main imx220 Fail to OFF core power (VCAM_D), power id = %d\n",
                                             VCAMD);
                                        goto _kdCISModulePowerOn_exit_;
                                }
                        } else {
                                if (TRUE != _hwPowerDownCnt(VCAMD, mode_name)) {
                                        PK_DBG
                                            ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                             VCAMD);
                                        goto _kdCISModulePowerOn_exit_;
                                }
                        }

                        /* VCAM_A */
                        if (TRUE != _hwPowerDownCnt(VCAMA, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     VCAMA);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDownCnt(VCAMIO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     VCAMIO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDownCnt(VCAMAF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     VCAMAF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                }
                if(((0 == strcmp(currSensorName,"imx135_mip_iraw")) ||
                        (0 == strcmp(currSensorName,SENSOR_DRVNAME_IMX135_MIPI_RAW_3MP)) ||
                        (0 == strcmp(currSensorName,SENSOR_DRVNAME_IMX135_MIPI_RAW_5MP)) ||
                        (0 == strcmp(currSensorName,SENSOR_DRVNAME_IMX135_MIPI_RAW_8MP))
                        )&&(pinSetIdx != 0))
                        PK_DBG("[PowerOFF]135 not main\n");
                else
                        ISP_MCLK1_EN(0);
        }


        return 0;


_kdCISModulePowerOn_exit_:
        return -EIO;

}
#else

/*
#ifndef BOOL
typedef unsigned char BOOL;
#endif
*/


int cntVCAMD = 0;
int cntVCAMA = 0;
int cntVCAMIO = 0;
int cntVCAMAF = 0;
int cntVCAMD_SUB = 0;

static DEFINE_SPINLOCK(kdsensor_pw_cnt_lock);


bool _hwPowerOn(MT65XX_POWER powerId, int powerVolt, char *mode_name)
{

        if (hwPowerOn(powerId, powerVolt, mode_name)) {
                spin_lock(&kdsensor_pw_cnt_lock);
                if (powerId == CAMERA_POWER_VCAM_D)
                        cntVCAMD += 1;
                else if (powerId == CAMERA_POWER_VCAM_A)
                        cntVCAMA += 1;
                else if (powerId == CAMERA_POWER_VCAM_IO)
                        cntVCAMIO += 1;
                else if (powerId == CAMERA_POWER_VCAM_AF)
                        cntVCAMAF += 1;
                else if (powerId == SUB_CAMERA_POWER_VCAM_D)
                        cntVCAMD_SUB += 1;
                spin_unlock(&kdsensor_pw_cnt_lock);
                return true;
        }
        return false;
}

bool _hwPowerDown(MT65XX_POWER powerId, char *mode_name)
{

        if (hwPowerDown(powerId, mode_name)) {
                spin_lock(&kdsensor_pw_cnt_lock);
                if (powerId == CAMERA_POWER_VCAM_D)
                        cntVCAMD -= 1;
                else if (powerId == CAMERA_POWER_VCAM_A)
                        cntVCAMA -= 1;
                else if (powerId == CAMERA_POWER_VCAM_IO)
                        cntVCAMIO -= 1;
                else if (powerId == CAMERA_POWER_VCAM_AF)
                        cntVCAMAF -= 1;
                else if (powerId == SUB_CAMERA_POWER_VCAM_D)
                        cntVCAMD_SUB -= 1;
                spin_unlock(&kdsensor_pw_cnt_lock);
                return true;
        }
        return false;
}

void checkPowerBeforClose(char *mode_name)
{

        int i = 0;

        PK_DBG
            ("[checkPowerBeforClose]cntVCAMD:%d, cntVCAMA:%d,cntVCAMIO:%d, cntVCAMAF:%d, cntVCAMD_SUB:%d,\n",
             cntVCAMD, cntVCAMA, cntVCAMIO, cntVCAMAF, cntVCAMD_SUB);


        for (i = 0; i < cntVCAMD; i++)
                hwPowerDown(CAMERA_POWER_VCAM_D, mode_name);
        for (i = 0; i < cntVCAMA; i++)
                hwPowerDown(CAMERA_POWER_VCAM_A, mode_name);
        for (i = 0; i < cntVCAMIO; i++)
                hwPowerDown(CAMERA_POWER_VCAM_IO, mode_name);
        for (i = 0; i < cntVCAMAF; i++)
                hwPowerDown(CAMERA_POWER_VCAM_AF, mode_name);
        for (i = 0; i < cntVCAMD_SUB; i++)
                hwPowerDown(SUB_CAMERA_POWER_VCAM_D, mode_name);

        cntVCAMD = 0;
        cntVCAMA = 0;
        cntVCAMIO = 0;
        cntVCAMAF = 0;
        cntVCAMD_SUB = 0;

}



int kdCISModulePowerOn(CAMERA_DUAL_CAMERA_SENSOR_ENUM SensorIdx, char *currSensorName, BOOL On,
                       char *mode_name)
{

        u32 pinSetIdx = 0;	/* default main sensor */

#define IDX_PS_CMRST 0
#define IDX_PS_CMPDN 4
#define IDX_PS_MODE 1
#define IDX_PS_ON   2
#define IDX_PS_OFF  3


        u32 pinSet[3][8] = {
                /* for main sensor */
                {CAMERA_CMRST_PIN,	/* The reset pin of main sensor uses GPIO10 of mt6306, please call mt6306 API to set */
                 CAMERA_CMRST_PIN_M_GPIO,	/* mode */
                 GPIO_OUT_ONE,	/* ON state */
                 GPIO_OUT_ZERO,	/* OFF state */
                 CAMERA_CMPDN_PIN,
                 CAMERA_CMPDN_PIN_M_GPIO,
                 GPIO_OUT_ONE,
                 GPIO_OUT_ZERO,
                 },
                /* for sub sensor */
                {CAMERA_CMRST1_PIN,
                 CAMERA_CMRST1_PIN_M_GPIO,
                 GPIO_OUT_ONE,
                 GPIO_OUT_ZERO,
                 CAMERA_CMPDN1_PIN,
                 CAMERA_CMPDN1_PIN_M_GPIO,
                 GPIO_OUT_ONE,
                 GPIO_OUT_ZERO,
                 },
                /* for main_2 sensor */
                {GPIO_CAMERA_INVALID,
                 GPIO_CAMERA_INVALID,	/* mode */
                 GPIO_OUT_ONE,	/* ON state */
                 GPIO_OUT_ZERO,	/* OFF state */
                 GPIO_CAMERA_INVALID,
                 GPIO_CAMERA_INVALID,
                 GPIO_OUT_ONE,
                 GPIO_OUT_ZERO,
                 }
        };



        if (DUAL_CAMERA_MAIN_SENSOR == SensorIdx)
                pinSetIdx = 0;
         else if (DUAL_CAMERA_SUB_SENSOR == SensorIdx)
                pinSetIdx = 1;
         else if (DUAL_CAMERA_MAIN_2_SENSOR == SensorIdx)
                pinSetIdx = 2;

        /* power ON */
        if (On) {
                if((0 == strcmp(currSensorName,"imx135_mipi_raw"))&&(pinSetIdx != 0))
                        PK_DBG("[PowerON]135 not main\n");
                else
            ISP_MCLK1_EN(1);

                PK_DBG("[PowerON]pinSetIdx:%d, currSensorName: %s\n", pinSetIdx, currSensorName);

                if ((currSensorName && (0 == strcmp(currSensorName, "imx135_mipi_raw"))) ||
                    (currSensorName && (0 == strcmp(currSensorName, "imx220mipiraw")))) {
                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }


                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_AF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF), power id = %d\n",
                                     CAMERA_POWER_VCAM_AF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* VCAM_A */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_A, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A), power id = %d\n",
                                     CAMERA_POWER_VCAM_A);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_D, VOL_1000, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D), power id = %d\n",
                                     CAMERA_POWER_VCAM_D);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* VCAM_IO */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_IO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_IO), power id = %d\n",
                                     CAMERA_POWER_VCAM_IO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(2);


                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }

                } else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_OV5648_MIPI_RAW, currSensorName))) {
                        mt_set_gpio_mode(GPIO_SPI_MOSI_PIN, GPIO_MODE_00);
                        mt_set_gpio_dir(GPIO_SPI_MOSI_PIN, GPIO_DIR_OUT);
                        mt_set_gpio_out(GPIO_SPI_MOSI_PIN, GPIO_OUT_ONE);
                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_IO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_IO), power id = %d\n",
                                     CAMERA_POWER_VCAM_IO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        /* VCAM_A */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_A, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A), power id = %d\n",
                                     CAMERA_POWER_VCAM_A);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(1);

                        if (TRUE != _hwPowerOn(SUB_CAMERA_POWER_VCAM_D, VOL_1500, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D), power id = %d\n",
                                     SUB_CAMERA_POWER_VCAM_D);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(5);

                        /* AF_VCC */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_AF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF), power id = %d\n",
                                     CAMERA_POWER_VCAM_AF);
                                goto _kdCISModulePowerOn_exit_;
                        }


                        mdelay(1);


                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }


                        mdelay(2);


                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }

                        }

                        mdelay(20);
                } else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_GC2355_MIPI_RAW, currSensorName))) {
                        mt_set_gpio_mode(GPIO_SPI_MOSI_PIN, GPIO_MODE_00);
                        mt_set_gpio_dir(GPIO_SPI_MOSI_PIN, GPIO_DIR_OUT);
                        mt_set_gpio_out(GPIO_SPI_MOSI_PIN, GPIO_OUT_ONE);
                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }

                        mdelay(50);

                        /* VCAM_A */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_A, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A), power id = %d\n",
                                     CAMERA_POWER_VCAM_A);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);

                        /* VCAM_IO */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_IO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_IO), power id = %d\n",
                                     CAMERA_POWER_VCAM_IO);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);

                        if (TRUE != _hwPowerOn(SUB_CAMERA_POWER_VCAM_D, VOL_1500, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_D), power id = %d\n",
                                     SUB_CAMERA_POWER_VCAM_D);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(10);

                        /* AF_VCC */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_AF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF), power id = %d\n",
                                     CAMERA_POWER_VCAM_AF);
                                goto _kdCISModulePowerOn_exit_;
                        }


                        mdelay(50);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                                mdelay(5);
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }

                        }
                        mdelay(5);
                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                                mdelay(5);
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }

                        mdelay(5);
                } else {
                        /* First Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_IO, VOL_1800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable digital power (VCAM_IO), power id = %d\n",
                                     CAMERA_POWER_VCAM_IO);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_A, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_A), power id = %d\n",
                                     CAMERA_POWER_VCAM_A);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_D */
                        if (currSensorName
                            && (0 == strcmp(SENSOR_DRVNAME_S5K2P8_MIPI_RAW, currSensorName))) {
                                if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_D, VOL_1200, mode_name)) {
                                        PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
                                        goto _kdCISModulePowerOn_exit_;
                                }
                        } else if (currSensorName
                                   && (0 ==
                                       strcmp(SENSOR_DRVNAME_IMX219_MIPI_RAW, currSensorName))) {
                                if (pinSetIdx == 0
                                    && TRUE != _hwPowerOn(CAMERA_POWER_VCAM_D, VOL_1200,
                                                          mode_name)) {
                                        PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
                                        goto _kdCISModulePowerOn_exit_;
                                }
                        } else {	/* Main VCAMD max 1.5V */
                                if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_D, VOL_1500, mode_name)) {
                                        PK_DBG("[CAMERA SENSOR] Fail to enable digital power\n");
                                        goto _kdCISModulePowerOn_exit_;
                                }

                        }


                        /* AF_VCC */
                        if (TRUE != _hwPowerOn(CAMERA_POWER_VCAM_AF, VOL_2800, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to enable analog power (VCAM_AF), power id = %d\n",
                                     CAMERA_POWER_VCAM_AF);
                                goto _kdCISModulePowerOn_exit_;
                        }

                        mdelay(5);

                        /* enable active sensor */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }

                        mdelay(1);

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_ON])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }
                }
        } else {		/* power OFF */

                PK_DBG("[PowerOFF]pinSetIdx:%d\n", pinSetIdx);
                if((0 == strcmp(currSensorName,"imx135_mipi_raw"))&&(pinSetIdx != 0))
                        PK_DBG("[PowerOFF]135 not main\n");
                else
            ISP_MCLK1_EN(0);

                if ((currSensorName && (0 == strcmp(currSensorName, "imx135_mipi_raw"))) ||
                    (currSensorName && (0 == strcmp(currSensorName, "imx220mipiraw")))) {
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }
                        /* Set Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_AF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     CAMERA_POWER_VCAM_AF);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_IO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     CAMERA_POWER_VCAM_IO);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }

                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_D, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     CAMERA_POWER_VCAM_D);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_A, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     CAMERA_POWER_VCAM_A);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }

                } else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_OV5648_MIPI_RAW, currSensorName))) {
                        mt_set_gpio_out(GPIO_SPI_MOSI_PIN, GPIO_OUT_ZERO);
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }

                        if (TRUE != _hwPowerDown(SUB_CAMERA_POWER_VCAM_D, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     SUB_CAMERA_POWER_VCAM_D);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_A, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     CAMERA_POWER_VCAM_A);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_IO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     CAMERA_POWER_VCAM_IO);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_AF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     CAMERA_POWER_VCAM_AF);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }

                } else if (currSensorName
                           && (0 == strcmp(SENSOR_DRVNAME_GC2355_MIPI_RAW, currSensorName))) {
                        mt_set_gpio_out(GPIO_SPI_MOSI_PIN, GPIO_OUT_ZERO);
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_ON])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }

                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }


                        if (TRUE != _hwPowerDown(SUB_CAMERA_POWER_VCAM_D, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                     SUB_CAMERA_POWER_VCAM_D);
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_A */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_A, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     CAMERA_POWER_VCAM_A);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_IO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     CAMERA_POWER_VCAM_IO);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_AF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     CAMERA_POWER_VCAM_AF);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }

                } else {
                        /* Set Power Pin low and Reset Pin Low */
                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMPDN]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA LENS] set gpio mode failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMPDN], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA LENS] set gpio dir failed!! (CMPDN)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMPDN],
                                     pinSet[pinSetIdx][IDX_PS_CMPDN + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA LENS] set gpio failed!! (CMPDN)\n");
                                }
                        }


                        if (GPIO_CAMERA_INVALID != pinSet[pinSetIdx][IDX_PS_CMRST]) {
                                if (mt_set_gpio_mode
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_MODE])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio mode failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_dir(pinSet[pinSetIdx][IDX_PS_CMRST], GPIO_DIR_OUT)) {
                                        PK_DBG("[CAMERA SENSOR] set gpio dir failed!! (CMRST)\n");
                                }
                                if (mt_set_gpio_out
                                    (pinSet[pinSetIdx][IDX_PS_CMRST],
                                     pinSet[pinSetIdx][IDX_PS_CMRST + IDX_PS_OFF])) {
                                        PK_DBG("[CAMERA SENSOR] set gpio failed!! (CMRST)\n");
                                }
                        }

                        if (currSensorName
                            && (0 == strcmp(SENSOR_DRVNAME_IMX219_MIPI_RAW, currSensorName))) {
                                if (pinSetIdx == 0
                                    && TRUE != _hwPowerDown(CAMERA_POWER_VCAM_D, mode_name)) {
                                        PK_DBG
                                            ("[CAMERA SENSOR] main imx220 Fail to OFF core power (VCAM_D), power id = %d\n",
                                             CAMERA_POWER_VCAM_D);
                                        goto _kdCISModulePowerOn_exit_;
                                }
                        } else {

                                if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_D, mode_name)) {
                                        PK_DBG
                                            ("[CAMERA SENSOR] Fail to OFF core power (VCAM_D), power id = %d\n",
                                             CAMERA_POWER_VCAM_D);
                                        goto _kdCISModulePowerOn_exit_;
                                }
                        }

                        /* VCAM_A */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_A, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF analog power (VCAM_A), power id= (%d)\n",
                                     CAMERA_POWER_VCAM_A);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* VCAM_IO */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_IO, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF digital power (VCAM_IO), power id = %d\n",
                                     CAMERA_POWER_VCAM_IO);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }
                        /* AF_VCC */
                        if (TRUE != _hwPowerDown(CAMERA_POWER_VCAM_AF, mode_name)) {
                                PK_DBG
                                    ("[CAMERA SENSOR] Fail to OFF AF power (VCAM_AF), power id = %d\n",
                                     CAMERA_POWER_VCAM_AF);
                                /* return -EIO; */
                                goto _kdCISModulePowerOn_exit_;
                        }

                }

        }

        return 0;

_kdCISModulePowerOn_exit_:
        return -EIO;

}

#endif
EXPORT_SYMBOL(kdCISModulePowerOn);

/* !-- */
/*  */
