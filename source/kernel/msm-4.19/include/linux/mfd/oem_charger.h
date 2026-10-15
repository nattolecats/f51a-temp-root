/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef __OEM_CHARGER_H
#define __OEM_CHARGER_H

/* mA */
#define OEM_CHG_USB_CURRENT			500
#define OEM_CHG_AC_CURRENT			1500
#define OEM_CHG_HOLDER_CURRENT		1500
#define OEM_CHG_OTHER_CURRENT		500
#define OEM_CHG_APSD_CURRENT		1500
#define OEM_CHG_OFF_CURRENT			0

#define OEM_CHG_MIN_SINK_CURRENT	500

/* notify error */
#define OEM_CHG_ERROR_NONE			0		// no error detection
#define OEM_CHG_ERROR_DETECT		1		// error detection

typedef enum {
	OEM_CHG_ERROR_OVP_NONE = 0,
	OEM_CHG_ERROR_OVP_DETECT,
	OEM_CHG_ERROR_USB_HOT_NONE,
	OEM_CHG_ERROR_USB_HOT_DETECT,
	OEM_CHG_ERROR_PDO_ERROR_NONE,
	OEM_CHG_ERROR_PDO_ERROR_DETECT,

	OEM_CHG_ERROR_UNKNOWN_ID_NONE,
	OEM_CHG_ERROR_UNKNOWN_ID_DETECT
} oem_chg_err_type;

enum {
	OEM_CHG_CHARGE_MODE_NORMAL = 0,
	OEM_CHG_CHARGE_MODE_POWERPATH,
};

/* oem_charger Prototype */
extern void oem_chg_usb_vbus_draw(unsigned int onoff);
extern void oem_chg_ac_vbus_draw(unsigned int onoff);
extern void oem_chg_holder_vbus_draw(unsigned int onoff);
extern void oem_chg_other_vbus_draw(unsigned int onoff);
extern void oem_chg_apsd_vbus_draw(unsigned int onoff);

extern void oem_chg_notify_error(oem_chg_err_type chg_err);
extern int oem_chg_pdo_available_check(u32 *pdo_data, bool *out, int num);
extern bool oem_chg_initialized(void);

#endif /* __OEM_CHARGER_H */
