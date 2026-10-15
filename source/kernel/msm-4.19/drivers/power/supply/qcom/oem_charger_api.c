/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/oem_charger.h>
#include "oem_charger_local.h"
#include <linux/nonvolatile_common.h>

/************************/
/*	external reference	*/
/************************/
/* charger */
extern void oem_chg_drv_source_req(int source, unsigned int mA);
extern void oem_chg_drv_current_req(int source, unsigned int mA);
extern void oem_chg_drv_notify_error(oem_chg_err_type chg_err);


extern int  oem_chg_drv_pdo_available_check(u32 *pdo_data, bool *out, int num);
extern bool oem_chg_drv_initialized(void);

/*********/
/*	API  */
/*********/
/* charger */
void oem_chg_usb_vbus_draw(unsigned int onoff)
{
	unsigned int mA = 0;

	if (onoff != 0) {
		mA = OEM_CHG_USB_CURRENT;
	} else {
		mA = OEM_CHG_OFF_CURRENT;
	}
	oem_chg_drv_source_req(OEM_CHG_SOURCE_USB, mA);
}
EXPORT_SYMBOL_GPL(oem_chg_usb_vbus_draw);

void oem_chg_ac_vbus_draw(unsigned int onoff)
{
	unsigned int mA = 0;

	if (onoff != 0) {
		mA = OEM_CHG_AC_CURRENT;
	} else {
		mA = OEM_CHG_OFF_CURRENT;
	}
	oem_chg_drv_source_req(OEM_CHG_SOURCE_AC, mA);
}
EXPORT_SYMBOL_GPL(oem_chg_ac_vbus_draw);

void oem_chg_holder_vbus_draw(unsigned int onoff)
{
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	unsigned int mA = 0;

	if (onoff != 0) {
		mA = OEM_CHG_HOLDER_CURRENT;
	} else {
		mA = OEM_CHG_OFF_CURRENT;
	}

	oem_chg_drv_source_req(OEM_CHG_SOURCE_HOLDER, mA);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
}
EXPORT_SYMBOL_GPL(oem_chg_holder_vbus_draw);

void oem_chg_other_vbus_draw(unsigned int onoff)
{
	unsigned int mA = 0;

	if (onoff != 0) {
		mA = OEM_CHG_OTHER_CURRENT;
	} else {
		mA = OEM_CHG_OFF_CURRENT;
	}
	oem_chg_drv_source_req(OEM_CHG_SOURCE_AC, mA);
}
EXPORT_SYMBOL_GPL(oem_chg_other_vbus_draw);

void oem_chg_apsd_vbus_draw(unsigned int onoff)
{
	unsigned int mA = 0;

	if (onoff != 0) {
		mA = OEM_CHG_APSD_CURRENT;
	} else {
		mA = OEM_CHG_OFF_CURRENT;
	}
	oem_chg_drv_source_req(OEM_CHG_SOURCE_APSD, mA);
}
EXPORT_SYMBOL_GPL(oem_chg_apsd_vbus_draw);

void oem_chg_notify_error(oem_chg_err_type chg_err)
{
	oem_chg_drv_notify_error(chg_err);
}
EXPORT_SYMBOL_GPL(oem_chg_notify_error);

int oem_chg_pdo_available_check(u32 *pdo_data, bool *out, int num)
{
	return oem_chg_drv_pdo_available_check(pdo_data, out, num);
}
EXPORT_SYMBOL_GPL(oem_chg_pdo_available_check);

bool oem_chg_initialized(void)
{
	return oem_chg_drv_initialized();
}
EXPORT_SYMBOL_GPL(oem_chg_initialized);

MODULE_AUTHOR("FUJITSU CONNECTED TECHNOLOGIES");
MODULE_DESCRIPTION("OEM Charger Driver");
MODULE_LICENSE("GPL");
