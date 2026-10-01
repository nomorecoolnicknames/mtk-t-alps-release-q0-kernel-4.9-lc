/*
 * cam_cal (module calibration) provider for the Meizu m5c main camera.
 *
 * forge (m5c): the S5K4H8 main module keeps its calibration in a GT24C64A
 * i2c EEPROM (8-bit write id 0xA0) that sits on the same camera i2c bus as
 * the sensor and is not described in DT. Byte 0x0001 of that EEPROM is the
 * module-vendor id, which is what selects one of the four module variants
 * the stock kernel supports.
 */
#ifndef _S5K4H8_OTP_CAL_H
#define _S5K4H8_OTP_CAL_H

/* Module variants, in the order the stock kd_sensorlist declares them. */
enum {
	S5K4H8_MODULE_OFILM = 0,
	S5K4H8_MODULE_ST,
	S5K4H8_MODULE_HOLITECH,
	S5K4H8_MODULE_SUNWIN,
	S5K4H8_MODULE_NUM
};

/* EEPROM byte 0x0001 -> variant index, or S5K4H8_MODULE_NUM when unknown. */
#define S5K4H8_MODULE_ID_OFILM		0x05
#define S5K4H8_MODULE_ID_ST		0x08
#define S5K4H8_MODULE_ID_HOLITECH	0x09
#define S5K4H8_MODULE_ID_SUNWIN		0x0A

/* Reads EEPROM byte 0x0001. Returns 0xFFFF if the i2c read failed. */
unsigned int s5k4h8_otp_get_module_id(void);

/* Maps a raw module id onto a variant index. */
unsigned int s5k4h8_otp_module_variant(unsigned int module_id);

/*
 * Fills the cam_cal buffer of one variant from the EEPROM. Must be called
 * with the camera module powered and the i2c bus switched to the main
 * sensor, i.e. from get_imgsensor_id()/open().
 */
void s5k4h8_otp_cali(unsigned int variant);

#endif /* _S5K4H8_OTP_CAL_H */
