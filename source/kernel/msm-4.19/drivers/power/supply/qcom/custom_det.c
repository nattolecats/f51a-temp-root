/*
 * (C) 2021 FCNT LIMITED
*/
// SPDX-License-Identifier: GPL-2.0

//==============================================================================
// include file
//==============================================================================
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/printk.h>
#include <linux/moduleparam.h>

#include "custom_det.h"
#ifdef FEATURE_OEM_CHARGE
#include <linux/mfd/oem_charger.h>
#endif /* FEATURE_OEM_CHARGE */

#ifdef FEATURE_CUSTOM_DET_DRIVER

//==============================================================================
// define & enum
//==============================================================================
/*
 * OEM Charge dummy
 */
#ifndef FEATURE_OEM_CHARGE
	#define OEM_CHG_ERROR_OVP_NONE			/* dummy */(0)
	#define OEM_CHG_ERROR_OVP_DETECT		/* dummy */(1)
	#define OEM_CHG_ERROR_USB_HOT_DETECT	/* dummy */(2)
	#define OEM_CHG_ERROR_USB_HOT_NONE		/* dummy */(3)
	#define OEM_CHG_USB_CURRENT				/* dummy */(1)
	#define OEM_CHG_OFF_CURRENT				/* dummy */(0)
	#define oem_chg_usb_vbus_draw(type)		/* nop */
	#define oem_chg_ac_vbus_draw(type)		/* nop */
	#define oem_chg_holder_vbus_draw(type)	/* nop */
	#define oem_chg_other_vbus_draw(type)	/* nop */
	#define oem_chg_apsd_vbus_draw(type)	/* nop */
	#define oem_chg_notify_error(type)		/* nop */
#endif /* FEATURE_OEM_CHARGE */

//==============================================================================
// struct
//==============================================================================
/*
 * CUSTOM_DET_DRIVER Platform Data
 */
struct custom_det_platform_data {
	struct smb_charger *chg;			/* chg device */
};


//==============================================================================
// const
//==============================================================================

//==============================================================================
// static variable
//==============================================================================
static struct custom_det_platform_data custom_platform_data;

static bool otg_stop = 0;
static int usb_conn_therm = 0;			

//==============================================================================
// external reference
//==============================================================================

//==============================================================================
// functions
//==============================================================================
void custom_det_set_charge_control_limit(int set_val)
{
	if (usb_conn_therm != set_val) {
		if (set_val != 0) {
			oem_chg_notify_error(OEM_CHG_ERROR_USB_HOT_DETECT);
		} else {
			oem_chg_notify_error(OEM_CHG_ERROR_USB_HOT_NONE);
		}
		CUSTOM_DET_RECLOG("%s safety error usbhot=%d\n", __func__, set_val);
		usb_conn_therm = set_val;
	}
}

int custom_det_get_charge_control_limit_max(void)
{
	return 1;
}

int custom_det_get_charge_control_limit(void)
{
	return usb_conn_therm;
}
void custom_det_charge_req(CUSTOM_DET_CHARGE_TYPE type, bool enable)
{
	switch (type) {
	case CUSTOM_DET_CHARGE_APSD:
		if (enable) {
			oem_chg_apsd_vbus_draw(OEM_CHG_APSD_CURRENT);
		} else {
			oem_chg_apsd_vbus_draw(OEM_CHG_OFF_CURRENT);
		}
		break;
	}

	CUSTOM_DET_LOG("%s value:%d\n", __func__, enable);

	return;
}

static int _set_otg_stop(const char *buf, const struct kernel_param *kp)
{
	struct custom_det_platform_data *custom_pdata = &custom_platform_data;
	int ret;

	ret = param_set_bint(buf, kp);

	if (ret)
		return ret;

	if (otg_stop)
		if (custom_pdata->chg->vbus_vreg && custom_pdata->chg->vbus_vreg->rdev)
			smblib_vbus_regulator_disable(custom_pdata->chg->vbus_vreg->rdev);

	CUSTOM_DET_LOG("%s value:%d\n", __func__, otg_stop);

	return 0;
}

MODULE_PARM_DESC(otg_stop, "OTG Stop");
module_param_call(otg_stop, _set_otg_stop, NULL, &otg_stop, 0220);

int custom_det_init(struct smb_charger *chg)
{
	struct custom_det_platform_data *custom_pdata = &custom_platform_data;

	memset(custom_pdata, 0, sizeof(struct custom_det_platform_data));

	custom_pdata->chg = chg;

	return 0;
}
#endif /* FEATURE_CUSTOM_DET_DRIVER */

MODULE_DESCRIPTION("CUSTOM DET Driver");
MODULE_LICENSE("GPL v2");
