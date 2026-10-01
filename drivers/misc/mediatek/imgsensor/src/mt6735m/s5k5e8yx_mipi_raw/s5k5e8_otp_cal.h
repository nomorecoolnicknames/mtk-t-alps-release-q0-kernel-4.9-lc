/*
 * cam_cal (module calibration) provider for the Meizu m5c sub camera.
 *
 * forge (m5c): unlike the main module, the S5K5E8YX front module has no
 * EEPROM. Its calibration lives in the sensor's own OTP, page 4, and it
 * carries AWB only - no lens shading. Register 0x0A07 of that page is the
 * module-vendor id, which selects one of the four variants stock supports.
 */
#ifndef _S5K5E8_OTP_CAL_H
#define _S5K5E8_OTP_CAL_H

/* Module variants, in the order the stock kd_sensorlist declares them. */
enum {
	S5K5E8_MODULE_ST = 0,
	S5K5E8_MODULE_QH,
	S5K5E8_MODULE_HOLITECH,
	S5K5E8_MODULE_SUNWIN,
	S5K5E8_MODULE_NUM
};

#define S5K5E8_MODULE_ID_ST		0x03
#define S5K5E8_MODULE_ID_QH		0x07
#define S5K5E8_MODULE_ID_HOLITECH	0x09
#define S5K5E8_MODULE_ID_SUNWIN		0x0A

/*
 * Provided by s5k5e8yxmipiraw_Sensor.c: raw register access on the live i2c
 * write id the sensor was found at (stock hardcodes 0x30; this phone answers
 * at 0x78 and moves itself to 0x5A during init, so the live id is used).
 */
unsigned int s5k5e8_otp_read_reg(unsigned int addr);
void s5k5e8_otp_write_reg(unsigned int addr, unsigned int val);

/* Reads the module-vendor id out of OTP page 4. 0xFFFF on failure. */
unsigned int s5k5e8_otp_get_module_id(void);

/* Maps a raw module id onto a variant index. */
unsigned int s5k5e8_otp_module_variant(unsigned int module_id);

/* Fills the cam_cal buffer of one variant. Sensor must be powered. */
void s5k5e8_otp_cali(unsigned int variant);

#endif /* _S5K5E8_OTP_CAL_H */
