/*
 * (C) 2021 FCNT LIMITED
*/
/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __CUSTOM_DET_DRIVER_H__
#define __CUSTOM_DET_DRIVER_H__

//==============================================================================
// include file
//==============================================================================
#include <linux/device.h>
#include <linux/power_supply.h>

#include "smb5-lib.h"
#include "smb5-reg.h"

//==============================================================================
// feature
//==============================================================================
#define FEATURE_OEM_CHARGE
#define FEATURE_CUSTOM_DET_DRIVER

#ifdef FEATURE_CUSTOM_DET_DRIVER

typedef enum {
	CUSTOM_DET_CHARGE_APSD = 0,
} CUSTOM_DET_CHARGE_TYPE;

//==============================================================================
// define & enum
//==============================================================================
#define CUSTOM_DET_RECLOG(fmt, ...)		printk("REC@REC@0 [CUSTOM DET]" fmt, ##__VA_ARGS__)
#define CUSTOM_DET_LOG(fmt, ...)		printk("[custom_det]" fmt, ##__VA_ARGS__)
#define CUSTOM_DET_LOG_D(fmt, ...)		pr_debug("[custom_det]" fmt, ##__VA_ARGS__)

//==============================================================================
// functions
//==============================================================================
void custom_det_charge_req(CUSTOM_DET_CHARGE_TYPE type, bool enable);

void custom_det_set_charge_control_limit(int set_val);
int custom_det_get_charge_control_limit_max(void);
int custom_det_get_charge_control_limit(void);

int custom_det_init(struct smb_charger *chg);

#endif /* FEATURE_CUSTOM_DET_DRIVER */

#endif /* __CUSTOM_DET_DRIVER_H__ */
