/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
// SPDX-License-Identifier: GPL-2.0+

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/seq_file.h>
#include <linux/pm_wakeup.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/custom_mode.h>
#include <linux/delay.h>
#include <linux/nonvolatile_common.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/mfd/oem_charger.h>
#include <linux/pmic-voter.h>
#include "smb5-reg.h"
#include "smb5-lib.h"
#include "oem_charger_local.h"

extern int oem_smblib_reg_read(u16 cmd, u8 *val);
extern int oem_smblib_reg_mask_write(u16 cmd, u8 mask, u8 val);

#define CHG_DRV_DBGLOG(x, y...)	   					\
	if (chg_drv_debug){ 							\
		printk(KERN_ERR "[oem_chg] " x, ## y);		\
	}
#define CHG_DRV_INFOLOG(x, y...)			printk(KERN_INFO "[oem_chg] " x, ## y)
#define CHG_DRV_WARNLOG(x, y...)			printk(KERN_WARNING "[oem_chg] " x, ## y)
#define CHG_DRV_ERRLOG(x, y...)				printk(KERN_ERR "[oem_chg] " x, ## y)
#define CHG_DRV_RECLOG(x, y...)				printk("REC@CHG@36[oem_chg] " x, ## y)

#define CHG_DRV_SOURCE_AC					(BIT(0) << OEM_CHG_SOURCE_AC)
#define CHG_DRV_SOURCE_USB					(BIT(0) << OEM_CHG_SOURCE_USB)
#define CHG_DRV_SOURCE_HOLDER				(BIT(0) << OEM_CHG_SOURCE_HOLDER)
#define CHG_DRV_SOURCE_APSD					(BIT(0) << OEM_CHG_SOURCE_APSD)

#define CHG_DRV_SOURCE_USB_PORT				(CHG_DRV_SOURCE_AC | CHG_DRV_SOURCE_USB | CHG_DRV_SOURCE_APSD)
#define CHG_DRV_SOURCE(x)					(BIT(0) << x)
#define CHG_DRV_SOURCE_IS_USB_PORT(x)		((CHG_DRV_SOURCE_USB_PORT) & CHG_DRV_SOURCE(x))
#define CHG_DRV_SOURCE_MASK					(CHG_DRV_SOURCE_USB_PORT | CHG_DRV_SOURCE_HOLDER)

#define CHG_DRV_IS_USB_PORT_CHARGE(chip)	((chip->connect_state  == CHG_DRV_CONNECT_STATE_USB) ? true : false)
#define CHG_DRV_IS_HOLDER_CHARGE(chip)		(((chip->connect_state == CHG_DRV_CONNECT_STATE_HOLDER) || \
											  (chip->connect_state == CHG_DRV_CONNECT_STATE_BOTH)) ? true : false)

#define CHG_DRV_TYPE_LOG(chip)				(CHG_DRV_IS_HOLDER_CHARGE(chip) ? "HOLDER" : "USB")

#define CHG_DRV_GET_SMALL(a ,b)				(a > b ? b : a)
#define CHG_DRV_GET_LARGE(a ,b)				(a > b ? a : b)

#define CHG_MA_TO_UA(mA)					(mA * 1000)
#define CHG_MIN_TO_MSEC(min)				(min * 60 * 1000)

#define APNV_CHG_ADJ_VMAX_I					(41041)
#define APNV_MC_CHARGE_FLAG					(49144)

#define CHG_DRV_CHARGE_MODE_INVALID			(-1)
#define CHG_DRV_CHARGE_MODE_NOT_CHARGE		(0)
#define CHG_DRV_CHARGE_MODE_MANUAL			(1)
#define CHG_DRV_CHARGE_MODE_NUM				(2)

#define CHG_DRV_CURRENT_OFF					(0)
#define CHG_DRV_CURRENT_500					(500)
#define CHG_DRV_CURRENT_1500				(1500)

#define CHG_DRV_SET_TIMER_20MS				(20)
#define CHG_DRV_SET_TIMER_500MS				(500)
#define CHG_DRV_SET_TIMER_750MS				(750)
#define CHG_DRV_SET_TIMER_1S				(  1 * 1000)
#define CHG_DRV_SET_TIMER_10S				( 10 * 1000)
#define CHG_DRV_SET_TIMER_60S				( 60 * 1000)
#define CHG_DRV_SET_TIMER_600S				( 600 * 1000)

#define CHG_DRV_ILIMIT_NOT_LIMITED			(0)						/* ilimit not limited	  */
#define CHG_DRV_ILIMIT_LIMITED_2000			2000					/* ilimit limited 2000mA  */
#define CHG_DRV_ILIMIT_LIMITED_1500			1500					/* ilimit limited 1500mA  */
#define CHG_DRV_ILIMIT_LIMITED_900			900						/* ilimit limited 900mA   */
#define CHG_DRV_ILIMIT_LIMITED_500			500						/* ilimit limited 500mA   */
#define CHG_DRV_ILIMIT_LIMITED_300			300						/* ilimit limited 300mA   */
#define CHG_DRV_ILIMIT_LIMITED_150			150						/* ilimit limited 150mA   */
#define CHG_DRV_ILIMIT_LIMITED_100			100						/* ilimit limited 100mA   */
#define CHG_DRV_ILIMIT_LIMITED_MIN			300						/* ilimit limited minimum */
#define CHG_DRV_ILIMIT_DISABLE				(-1)					/* charge disable		  */

#define CHG_DRV_QUEUE_LOCK()				do { \
												unsigned long flags; \
												spin_lock_irqsave(&oem_chg_drv_queue_lock, flags);

#define CHG_DRV_QUEUE_UNLOCK()					spin_unlock_irqrestore(&oem_chg_drv_queue_lock, flags); \
											} while (0);

#define CHG_DRV_EVT_QUEUE_MAX				(30)
#define CHG_DRV_BATT_VOLT_INDEX				(5)

/* charge err status bit */
#define CHG_DRV_PARAM_NO_ERROR				0x00000000
#define CHG_DRV_PARAM_BATT_TEMP_HOT			0x00000001
#define CHG_DRV_PARAM_BATT_TEMP_COLD		0x00000002
#define CHG_DRV_PARAM_BATT_VOLTAGE_OV		0x00000004
#define CHG_DRV_PARAM_ADAPTER_VOLTAGE_OV	0x00000008
#define CHG_DRV_PARAM_ADAPTER_VOLTAGE_UV	0x00000010
#define CHG_DRV_PARAM_OVP_DETECT_ERROR		0x00000020
#define CHG_DRV_PARAM_BATT_TEMP_WARM		0x00000040
#define CHG_DRV_PARAM_BATT_TEMP_COOL		0x00000080
#define CHG_DRV_PARAM_CHG_DISABLE			0x00000100
#define CHG_DRV_PARAM_UNSPEC_FAILURE		0x00000200
#define CHG_DRV_PARAM_USB_PORT_FAILURE		0x00000400
#define CHG_DRV_PARAM_USB_HOT_ERROR			0x00000800
#define CHG_DRV_PARAM_HOLDER_FAILURE		0x00001000
#define CHG_DRV_PARAM_WEAK_ADAPTER			0x00002000
#define CHG_DRV_PARAM_9V_UV_ERROR			0x00004000
#define CHG_DRV_PARAM_QC_UV_ERROR			CHG_DRV_PARAM_9V_UV_ERROR
#define CHG_DRV_PARAM_RECOVERY_ERROR		0x00008000
#define CHG_DRV_PARAM_CHG_TYPE_UNKNOWN		0x00010000
#define CHG_DRV_PARAM_FGIC_INITIALIZING		0x00020000
#define CHG_DRV_PARAM_WDOG_TIMEOUT			0x00040000
#define CHG_DRV_PARAM_CHGIC_ERROR			0x00080000
#define CHG_DRV_PARAM_AICL_ERROR			0x00100000
#define CHG_DRV_PARAM_BATT_ID_ERROR			0x00200000
#define CHG_DRV_PARAM_PDO_PROFILE_ERROR		0x00400000
#define CHG_DRV_PARAM_7V_UV_ERROR			0x00800000
#define CHG_DRV_PARAM_UNSPEC_CABLE			0x01000000		

#define CHG_DRV_PD5V_SUPPLY_UV_DEFAULT		(4000000)
#define CHG_DRV_PD5V_SUPPLY_VOLT_DEFAULT	(5000000)
#define CHG_DRV_PD5V_SUPPLY_OV_DEFAULT		(6000000)
#define CHG_DRV_PD9V_SUPPLY_UV_DEFAULT		(8000000)
#define CHG_DRV_PD9V_SUPPLY_VOLT_DEFAULT	(9000000)
#define CHG_DRV_PD9V_SUPPLY_OV_DEFAULT		(10000000)

#define CHG_DRV_BATT_VOLT_UV_DEFAULT		(3400000)
#define CHG_DRV_BATT_VOLT_OV_DEFAULT		(4450000)
#define CHG_DRV_BATT_SOC_FULL_DEFAULT		(100)
#define CHG_DRV_BATT_SOC_RECHG_DEFAULT		(98)
#define CHG_DRV_BATT_SOC_FULL_LONGEVITY_DEFAULT		(87)
#define CHG_DRV_BATT_SOC_RECHG_LONGEVITY_DEFAULT	(85)
#define CHG_DRV_BATT_TEMP_HOT_DEFAULT		(450)
#define CHG_DRV_BATT_TEMP_COLD_DEFAULT		(0)
#define CHG_DRV_BATT_TEMP_CANCEL_DEFAULT	(10)
#define CHG_DRV_NEGO_DETECT_DEFAULT			(5)
#define CHG_DRV_NEGO_MAX_DEFAULT			(30)
#define CHG_DRV_IBATT_DEFAULT				(500)
#define CHG_DRV_IBATT_WARM_DEFAULT			(1200)
#define CHG_DRV_IBATT_COOL_DEFAULT			(800)
#define CHG_DRV_CHARGE_VOLT_DEFAULT			(4340000)

typedef enum {
	CHG_DRV_STATE_IDLE = 0,					/* idle						*/
	CHG_DRV_STATE_CHARGING,					/* charging/re-charging		*/
	CHG_DRV_STATE_FULL,						/* full charging			*/
	CHG_DRV_STATE_ERROR,					/* charge error occurred	*/
	CHG_DRV_STATE_NUM,

	CHG_DRV_STATE_NOCHANGE,
} chg_drv_state_type;

typedef enum {
	CHG_DRV_EVENT_MONITOR_PARAM = 0,		/* period monitor		*/
	CHG_DRV_EVENT_CHARGE_INFO_NOTICE,		/* charge info notice	*/
	CHG_DRV_EVENT_CHARGE_CTRL,				/* charge control		*/
	CHG_DRV_EVENT_CHARGE_TIMER,				/* timer expire			*/
	CHG_DRV_EVENT_PARAM_UPDATE,				/* parameter update		*/
	CHG_DRV_EVENT_NUM
} chg_drv_event_type;

typedef enum {
	CHG_DRV_CONNECT_STATE_NONE = 0,
	CHG_DRV_CONNECT_STATE_USB,
	CHG_DRV_CONNECT_STATE_HOLDER,
	CHG_DRV_CONNECT_STATE_BOTH,
	CHG_DRV_CONNECT_STATE_NUM,

	CHG_DRV_CONNECT_STATE_NOCHANGE,
} chg_drv_connect_state_type;

typedef enum {
	CHG_DRV_CONNECT_EVENT_USB_IN = 0,
	CHG_DRV_CONNECT_EVENT_USB_OUT,
	CHG_DRV_CONNECT_EVENT_HOLDER_IN,
	CHG_DRV_CONNECT_EVENT_HOLDER_OUT,
	CHG_DRV_CONNECT_EVENT_NUM,
} chg_drv_connect_event_type;

typedef enum {
	CHG_DRV_MONITOR_NORMAL = 0,
	CHG_DRV_MONITOR_ADAPTER_VOLTAGE,
	CHG_DRV_MONITOR_AICL,
	CHG_DRV_MONITOR_NUM,
} chg_drv_monitor_type;

typedef enum {
	CHG_DRV_NOTICE_NON = 0,					/* 	*/
	CHG_DRV_NOTICE_OVP_NOTIFY_ERROR,		/* ovp notify error			*/
	CHG_DRV_NOTICE_USBHOT_NOTIFY_ERROR,		/* usb hot notify error		*/
	CHG_DRV_NOTICE_PDO_ERROR,				/* pdo profile notify error	*/
	CHG_DRV_NOTICE_CHG_ENABLE,				/* charge enable			*/
	CHG_DRV_NOTICE_CHANGE_CURRENT,			/* change of the current	*/
	CHG_DRV_NOTICE_NUM
} chg_drv_notice_type;

typedef enum {
	CHG_DRV_TIMER_NON = 0,					/* events other than a timer 	*/
	CHG_DRV_TIMER_FULL_1,					/* full charge detection timer1	*/
	CHG_DRV_TIMER_FULL_2,					/* full charge detection timer2	*/
	CHG_DRV_TIMER_PROTECTION,				/* protection timer				*/
	CHG_DRV_TIMER_VOLTAGE_CHECK,			/* voltage check timer			*/
	CHG_DRV_TIMER_NEGOTIATION,				/* negotiation timer			*/
	CHG_DRV_TIMER_NUM
} chg_drv_timer_type;

typedef enum {
	CHG_DRV_CONTROL_NONE = 0,
	CHG_DRV_CONTROL_USB_CHARGE,
	CHG_DRV_CONTROL_HOLDER_CHARGE,
	CHG_DRV_CONTROL_PRECHARGE,
	CHG_DRV_CONTROL_CHARGE_START,
	CHG_DRV_CONTROL_POWERPATH,
	CHG_DRV_CONTROL_RECHARGE,
	CHG_DRV_CONTROL_STOP,
	CHG_DRV_CONTROL_NUM,
} chg_drv_ctrl_type;

typedef enum {
	CHG_DRV_DEVICE_TYPE_USB = 0,
	CHG_DRV_DEVICE_TYPE_HOLDER,
	CHG_DRV_DEVICE_TYPE_NUM,
} chg_drv_device_type;

typedef enum {
	CHG_DRV_CHARGE_TYPE_UNKNOWN = 0,
	CHG_DRV_CHARGE_TYPE_5V,
	CHG_DRV_CHARGE_TYPE_9V,
	CHG_DRV_CHARGE_TYPE_NUM,
} chg_drv_charge_type;

typedef enum {
	CHG_DRV_STANDARD_TYPE_UNKNOWN = 0,
	CHG_DRV_STANDARD_TYPE_PD,
	CHG_DRV_STANDARD_TYPE_NUM,
} chg_drv_standard_type;

typedef enum {
	CHG_DRV_RESULT_SUCCESS = 0,
	CHG_DRV_RESULT_FAILURE,
	CHG_DRV_RESULT_RETRY,
	CHG_DRV_RESULT_OVER,
	CHG_DRV_RESULT_VOLTAGE_ERROR,
	CHG_DRV_RESULT_CONTINUE,
	CHG_DRV_RESULT_RECONFIRMATION,
} chg_drv_result_type;

typedef enum {
	CHG_DRV_AICL_TYPE_DISABLE = 0,
	CHG_DRV_AICL_TYPE_ENABLE,
	CHG_DRV_AICL_TYPE_RERUN,
} chg_drv_aicl_type;

typedef enum {
	CHG_DRV_CHARGE_TIMER_STATE_NONE = 0,
	CHG_DRV_CHARGE_TIMER_STATE_FULL1,
	CHG_DRV_CHARGE_TIMER_STATE_FULL2,
} chg_drv_timer_state;

typedef enum {
	CHG_DRV_SOFT_ICL_STEP_01 = 0,
	CHG_DRV_SOFT_ICL_STEP_02,
	CHG_DRV_SOFT_ICL_STEP_03,
	CHG_DRV_SOFT_ICL_STEP_04,
	CHG_DRV_SOFT_ICL_STEP_05,
	CHG_DRV_SOFT_ICL_STEP_06,
	CHG_DRV_SOFT_ICL_STEP_07,
	CHG_DRV_SOFT_ICL_STEP_08,
	CHG_DRV_SOFT_ICL_STEP_NUM,
} chg_drv_soft_icl_step_level;

typedef struct {
	int usb_in_volt;
	int batt_volt;
	int batt_temp;
	int soc;
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	int dc_in_volt;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	int fgic_initial_sts;

	int temp_usb_error;
	u8	chg_sts;
	u8	misc_sts;
	u8	typec_sm_sts;		
} chg_drv_monitor_param;

typedef struct {
	chg_drv_connect_state_type next_state;
	chg_drv_ctrl_type charge_ctrl;
} chg_drv_connect_ctrl;

typedef struct {
	chg_drv_monitor_type type;
} chg_drv_monitor_info;

typedef struct {
	chg_drv_connect_event_type event;
	int chg_source;
	unsigned int max_current;
} chg_drv_connect_info;

typedef struct {
	chg_drv_device_type device;
	int max_current;
} chg_drv_change_info;

typedef struct {
	chg_drv_notice_type type;
	unsigned int ovp_detect;				/* type:OVP_DETECT parameter		*/
	unsigned int usb_hot_detect;			/* type:USB_HOT_DETECT parameter	*/
	unsigned int pdo_error_detect;			/* type:PDO_ERROR_DETECT parameter	*/
	int ilimit;								/* type:CHG_ENABLE parameter		*/
	int charge_mode;						/* type:CHANGE_MODE parameter		*/
	chg_drv_change_info change_info;		/* type:CHANGE_CURRENT parameter	*/
} chg_drv_notice_info;

typedef struct {
	chg_drv_timer_type type;
	chg_drv_device_type device;
} chg_drv_timer_info;

typedef union {
	chg_drv_monitor_info monitor_info;
	chg_drv_connect_info connect_info;
	chg_drv_notice_info notice_info;
	chg_drv_timer_info timer_info;
} chg_drv_evt_data;

typedef struct {
	struct list_head head;				/* queue head	*/
	chg_drv_event_type event;			/* event id		*/
	chg_drv_evt_data data;				/* event data	*/
} chg_drv_evt_queue;

typedef struct {
	int set_status;
	int set_health;
	chg_drv_ctrl_type charge_ctrl;
} chg_drv_err_ctrl;

typedef struct {
	int over_voltage;
	int under_voltage;
} chg_drv_voltage_threshold;

typedef struct {
	struct delayed_work nego_timer_work;
	struct delayed_work check_timer_work;
	int charge_type;
	unsigned int max_current;
	int check_count[CHG_DRV_CHARGE_TYPE_NUM];
	int check_total;
	int nego_count[CHG_DRV_CHARGE_TYPE_NUM];
	int nego_total;
	int watch_count;
	int save_voltage_error;
	int soft_icl_step_up_count;
	int soft_icl_step_count;
	int soft_icl_step_error;
	int adapter_9v_ilimit_value;
	int soft_icl_ilimit_value;
	int weak_adapter_ilimit;
	bool voltage_check;
} chg_drv_device_info;

typedef struct {
	bool safety_timer_expire;
	bool batt_temp_warm_restrict;
	bool batt_temp_cool_restrict;
	bool weak_adapter;
	bool adapter_9v_under_voltage;
	bool recovery_err;
	bool chg_type_unknown;
	bool unspec_cable_err;				
} chg_drv_err_info;

typedef struct {
	bool ovp_detect;
	bool usb_port_failure;
	bool pdo_profile_error;
} chg_drv_safety_err_info;

typedef struct {
	unsigned int	state;
	unsigned int	chg_source;
	unsigned int	chg_current;
	unsigned int	current_err;
} chg_drv_info;

typedef struct {
	int volt_data[CHG_DRV_BATT_VOLT_INDEX];
	int index;
	bool data_full;
} chg_drv_batt_volt_info;

typedef struct {
	int usb5v_soft_icl_low_voltage;
	int usb5v_soft_icl_high_voltage;

	int usb5v_under_voltage;
	int usb5v_over_voltage;
	int usb9v_under_voltage_1st;
	int usb9v_under_voltage_2nd;
	int usb9v_over_voltage;

	int holder5v_under_voltage;
	int holder5v_over_voltage;
	int holder9v_under_voltage_1st;
	int holder9v_under_voltage_2nd;
	int holder9v_over_voltage;

	int pd5v_low_voltage_threshold;
	int pd5v_high_voltage_threshold;
	int pd9v_low_voltage_threshold;
	int pd9v_high_voltage_threshold;

	int batt_over_voltage;
	int batt_unspec_voltage;
	int batt_low_voltage_onchg;
	int batt_over_voltage_offchg;

	int chg_full_soc_lv1;
	int chg_full_soc_lv2;
	int chg_rechg_soc_lv1;
	int chg_rechg_soc_lv2;

	int chg_full_soc_longevity_lv1;
	int chg_full_soc_longevity_lv2;
	int chg_rechg_soc_longevity_lv1;
	int chg_rechg_soc_longevity_lv2;

	int chg_voltage;
	int chg_voltage_cycle_lv1;
	int chg_voltage_cycle_lv2;
	int chg_safety_timer_min;
	int chg_full1_timer_min;
	int chg_full2_timer_min;

	int batt_temp_hot_limit;
	int batt_temp_warm_limit;
	int batt_temp_cool_limit;
	int batt_temp_cold_limit;
	int batt_temp_start_limit;
	int batt_temp_limit_cancel;

	int chg_nego_usb_detect_count;
	int chg_nego_usb_detect_max;
	int chg_nego_holder_detect_count;
	int chg_nego_holder_detect_max;
	int chg_adapter_voltage_detect_count;
	int chg_adapter_voltage_detect_max;
	int wake_lock_timer_sec;

	int usb5v_ibat_current_max;
	int usb9v_ibat_current_max;
	int holder5v_ibat_current_max;
	int holder9v_ibat_current_max;
	int common_ibat_current_low_batt;
	int common_ibat_current_temp_warm_restrict;
	int common_ibat_current_temp_cool_restrict;

	int chg_consecutive_count;
	int chg_termcurrent_ma;
	int chg_cycle_termcurrent_ma;		

	int chg_icl_sts_num;
	int chg_icl_step_current_start_idx;
	int chg_icl_sts_thresholds_min[CHG_DRV_SOFT_ICL_STEP_NUM];
	int chg_icl_sts_thresholds_max[CHG_DRV_SOFT_ICL_STEP_NUM];
	int chg_icl_step_current_deltas[CHG_DRV_SOFT_ICL_STEP_NUM];

	int *chg_icl_thermal_mitigation;
} chg_drv_dt;

struct chg_drv_chip {
	struct task_struct			*chg_thread;
	wait_queue_head_t			chg_wq;			/* charger thread waitqueue head	*/

	struct device				*dev;

	chg_drv_dt					dt_param;

	unsigned int				connect_state;
	int							max_ilimit_value;
	int							charge_type;
	chg_drv_standard_type 		standard_type;
	int							apsd_type;			
	int							heat_charge_mode;
	int							consecutive_count;
	bool						battery_low_flag;

	int							batt_temp_hot_limit;
	int							batt_temp_warm_limit;
	int							batt_temp_cold_limit;
	int							batt_temp_cool_limit;

	chg_drv_timer_state			full_timer_state;

	int							recovery_count;
	bool						recovery_invalid;

	chg_drv_device_info			dev_usb;
	chg_drv_device_info			dev_holder;

	struct delayed_work			charge_monitor_work;
	struct delayed_work			full_timer_work1;
	struct delayed_work			full_timer_work2;
	struct delayed_work			safety_timer_work;
	struct delayed_work			adapter_volt_monitor_work;
	struct delayed_work			watchdog_work;
	struct delayed_work			aicl_monitor_work;

	struct power_supply_desc	oem_chg_psy_desc;
	struct power_supply			*oem_chg_psy;
	struct power_supply_desc	oem_mains_psy_desc;
	struct power_supply			*oem_mains_psy;
	struct power_supply_desc	oem_usb_psy_desc;
	struct power_supply			*oem_usb_psy;
	struct power_supply_desc	oem_holder_psy_desc;
	struct power_supply			*oem_holder_psy;

	struct power_supply			*bms_psy;
	struct power_supply			*batt_psy;
	struct power_supply			*usb_psy;
	struct power_supply			*dc_psy;
	struct power_supply			*oem_battery_psy;

	struct votable				*fv_votable;
	struct votable				*fcc_votable;
	struct votable				*chg_disable_votable;
	struct votable				*usb_icl_votable;
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	struct votable				*dc_icl_votable;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	int							charge_mode;
	int							vmax_adj;
	chg_drv_ctrl_type 			charge_ctrl;
	chg_drv_err_info			err_info;
	chg_drv_safety_err_info		safety_err_info;
	chg_drv_info				charger_info;
	chg_drv_batt_volt_info		batt_volt_data;

	int							chg_enable;

	struct workqueue_struct		*chg_drv_wq;
	bool						aicl_restart_first_flag;

	bool						longevity_charge_flag;
	chg_cycle_level				cycle_chg_lvl;			

	int							thermal_level;
	int							thermal_level_max;
};

#if defined(FJFEAT_AIRMONKEY)
static int chg_drv_debug = 1;
#else
static int chg_drv_debug = 0;
#endif
module_param_named(dbglog_charger, chg_drv_debug, int, S_IRUGO|S_IWUSR);
extern int charging_mode;

static struct chg_drv_chip *oem_chg_drv_chip = NULL;
static unsigned long charger_src = 0;
static unsigned long charger_apsd_src = 0;
static struct wakeup_source *chg_drv_timer_wake_lock;
static struct wakeup_source *chg_drv_charging_wake_lock;

static DEFINE_SPINLOCK(oem_chg_drv_info_lock);
static DEFINE_SPINLOCK(oem_chg_drv_queue_lock);

static LIST_HEAD(oem_chg_drv_evt_list_free);
static LIST_HEAD(oem_chg_drv_evt_list_exec);
static chg_drv_evt_queue oem_charge_evt_queue[CHG_DRV_EVT_QUEUE_MAX];

static enum power_supply_property chg_drv_common_properties[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property chg_drv_oem_chg_properties[] = {
	POWER_SUPPLY_PROP_CHG_ENABLE,
	POWER_SUPPLY_PROP_CHG_MODE,
	POWER_SUPPLY_PROP_CHG_STATE,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
};

static chg_drv_voltage_threshold chg_drv_usb_volt_chk_table[CHG_DRV_CHARGE_TYPE_NUM] = {{ 0 }};
static chg_drv_voltage_threshold chg_drv_holder_volt_chk_table[CHG_DRV_CHARGE_TYPE_NUM] = {{ 0 }};

typedef enum {
	CHG_DRV_ERR_NON = 0,					/* error not occurred		*/
	CHG_DRV_ERR_EXPIRE_TIME_OUT,			/* safe timer expire		*/
	CHG_DRV_ERR_USB_PORT_FAILURE,			/* usb port failure			*/
	CHG_DRV_ERR_OVP_DETECT,					/* ovp detect				*/
	CHG_DRV_ERR_CHARGE_DISABLE,				/* charge disable			*/
	CHG_DRV_ERR_BATT_TEMP_HOT_ERR,			/* batt temp hot 			*/
	CHG_DRV_ERR_BATT_TEMP_COLD_ERR,			/* batt temp cold			*/
	CHG_DRV_ERR_OVER_BATT_VOLTAGE,			/* over batt voltage		*/
	CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR,	/* over supply voltage		*/
	CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR,	/* under supply voltage		*/
	CHG_DRV_ERR_USB_HOT_ERR,				/* usb hot					*/
	CHG_DRV_ERR_HEAT_CHARGE,				/* heat charge				*/
	CHG_DRV_ERR_HOLDER_FAILURE,				/* holder failure			*/
	CHG_DRV_ERR_WEAK_ADAPTER,				/* weak adapter detect		*/
	CHG_DRV_ERR_OTHER_ERR,					/* other error				*/
	CHG_DRV_ERR_FGIC_INITIALIZING,			/* FGIC initializing		*/
	CHG_DRV_ERR_UNSPEC_CABLE,				/* unspecified cable		*/
	CHG_DRV_ERR_NUM
} chg_drv_err_type;

static const chg_drv_err_ctrl err_ctrl_table[CHG_DRV_ERR_NUM] = {
/*	  set_status						set_health									charge_ctrl					*/
	{ POWER_SUPPLY_STATUS_CHARGING,		POWER_SUPPLY_HEALTH_GOOD,					CHG_DRV_CONTROL_NONE		},	/* CHG_DRV_ERR_NON						*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_UNSPEC_FAILURE,			CHG_DRV_CONTROL_POWERPATH	},	/* CHG_DRV_ERR_EXPIRE_TIME_OUT			*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_USB_HOT,				CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_USB_PORT_FAILURE			*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_OVER_SUPPLY_VOLTAGE,	CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_OVP_DETECT				*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_GOOD,					CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_CHARGE_DISABLE			*/
	{ POWER_SUPPLY_STATUS_CHARGING,		POWER_SUPPLY_HEALTH_GOOD,					CHG_DRV_CONTROL_POWERPATH	},	/* CHG_DRV_ERR_BATT_TEMP_HOT_ERR		*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_COLD,					CHG_DRV_CONTROL_POWERPATH	},	/* CHG_DRV_ERR_BATT_TEMP_COLD_ERR		*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_OVERVOLTAGE,			CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_OVER_BATT_VOLTAGE		*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_OVER_SUPPLY_VOLTAGE,	CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR	*/
	{ POWER_SUPPLY_STATUS_DISCHARGING,	POWER_SUPPLY_HEALTH_GOOD,					CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR	*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_USB_HOT,				CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_USB_HOT_ERR				*/
	{ POWER_SUPPLY_STATUS_CHARGING,		POWER_SUPPLY_HEALTH_GOOD,					CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_HEAT_CHARGE				*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_HOLDER_ERROR,			CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_HOLDER_FAILURE			*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_OTHER_ERROR,			CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_WEAK_ADAPTER				*/
	{ POWER_SUPPLY_STATUS_NOT_CHARGING,	POWER_SUPPLY_HEALTH_OTHER_ERROR,			CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_OTHER_ERR				*/
	{ POWER_SUPPLY_STATUS_CHARGING,		POWER_SUPPLY_HEALTH_GOOD,					CHG_DRV_CONTROL_POWERPATH	},	/* CHG_DRV_ERR_FGIC_INITIALIZING		*/
	{ POWER_SUPPLY_STATUS_DISCHARGING,	POWER_SUPPLY_HEALTH_GOOD,					CHG_DRV_CONTROL_STOP		},	/* CHG_DRV_ERR_UNSPEC_CABLE				*/
};

static const unsigned int ilimit_table[] = {
	300,
	500,
	700,
	900,
	1000,
	1100,
	1200,
	1400,
	1500,
	1600,
	0,
};

typedef chg_drv_state_type (*chg_drv_func)(struct chg_drv_chip *chip, chg_drv_evt_data *data);

/**
 * chg_drv_queue_put
 *
 * @param	entry_p	: entry queue pointer
 *			head_p	: list head pointer
 */
static void chg_drv_queue_put(chg_drv_evt_queue *entry_p, struct list_head *head_p)
{
	CHG_DRV_QUEUE_LOCK();
	list_add_tail(&entry_p->head, head_p);
	CHG_DRV_QUEUE_UNLOCK();
}

/**
 * chg_drv_queue_get
 *
 * @param	head_p	: list head pointer
 *
 * @retval	queue pointer
 */
static void *chg_drv_queue_get(struct list_head *head_p)
{
	void *queue_p = NULL;
	struct list_head *p, *n;

	CHG_DRV_QUEUE_LOCK();
	list_for_each_safe(p, n, head_p) {
		queue_p = list_entry(p, chg_drv_evt_queue, head);
		if (queue_p != NULL) {
			list_del(p);
			break;
		}
		queue_p = NULL;
	}
	CHG_DRV_QUEUE_UNLOCK();

	return queue_p;
}

/**
 * chg_drv_event_req
 *
 * @param	chip		: charger device info pointer
 *			event		: event type
 *			evt_data	: event data pointer
 */
static void chg_drv_event_req(struct chg_drv_chip *chip,
								chg_drv_event_type event,
								chg_drv_evt_data *evt_data)
{
	chg_drv_evt_queue *evt_queue_p = NULL;

	evt_queue_p = chg_drv_queue_get(&oem_chg_drv_evt_list_free);
	if (unlikely(evt_queue_p == NULL)) {
		CHG_DRV_ERRLOG("[%s] charger event queue empty!!\n", __func__);
		return;
	}

	evt_queue_p->event = event;
	if (evt_data == NULL) {
		memset(&evt_queue_p->data, 0, sizeof(chg_drv_evt_data));
	} else {
		memcpy(&evt_queue_p->data, evt_data, sizeof(chg_drv_evt_data));
	}

	chg_drv_queue_put(evt_queue_p, &oem_chg_drv_evt_list_exec);

	if (chip != NULL) {
		wake_up(&chip->chg_wq);
	}
}

/**
 * chg_drv_reg_read
 *
 * @param	cmd	: command id
 *			val	: read data
 *
 * @retval	Processing result
 */
static int chg_drv_reg_read(u16 cmd, u8 *val)
{
	u8 val_work = 0;
	int ret;

	if (unlikely(val == NULL)) {
		CHG_DRV_ERRLOG("[%s] Invalid parameter. cmd:[%d]\n", __func__, cmd);
		return -EINVAL;
	}

	ret = oem_smblib_reg_read(cmd, &val_work);
	if (likely(ret == 0)) {
		*val = val_work;
	} else {
		CHG_DRV_ERRLOG("PMIC register read failure. cmd:[%d]\n", cmd);
	}

	CHG_DRV_DBGLOG("[%s] cmd:[%d] val:[0x%02X] ret:[%d]\n",
												__func__, cmd, *val, ret);

	return ret;

}

/**
 * chg_drv_reg_write_byte
 *
 * @param	cmd		: command id
 *			mask	: mask data
 *			val		: write data
 *
 * @retval	Processing result
 */
static int chg_drv_reg_write_byte(u16 cmd, u8 mask, u8 val)
{
	int ret;

	ret = oem_smblib_reg_mask_write(cmd, mask, val);
	if (unlikely(ret != 0)) {
		CHG_DRV_ERRLOG("PMIC register write failure. cmd:[%d] mask:[0x%02X] val:[0x%02X]\n",
																			cmd, mask, val);
	}

	CHG_DRV_DBGLOG("[%s] cmd:[%d] mask:[0x%02X] val:[0x%02X] ret:[%d]\n",
												__func__, cmd, mask, val, ret);

	return ret;
}

/**
 * chg_drv_reg_write_bit
 *
 * @param	cmd		: command id
 *			mask	: mask bit
 *			set		: set type
 *
 * @retval	Processing result
 */
static int chg_drv_reg_write_bit(u16 cmd, u8 mask, u8 set)
{
	int ret;

	if (set == CHG_DRV_SET_HIGH) {
		ret = oem_smblib_reg_mask_write(cmd, mask, mask);
	} else {
		ret = oem_smblib_reg_mask_write(cmd, mask, 0x00);
	}
	if (unlikely(ret != 0)) {
		CHG_DRV_ERRLOG("PMIC register write failure. cmd:[%d] mask:[0x%02X] set:[%d]\n",
																		cmd, mask, set);
	}

	CHG_DRV_DBGLOG("[%s] cmd:[%d] mask:[0x%02X] set:[%d] ret:[%d]\n",
												__func__, cmd, mask, set, ret);

	return ret;
}

/**
 * chg_drv_check_current
 *
 * @param	chip			: charger device info pointer
 *			ibat_curent		: battery current limit
 *			ilimit_current	: input current limit
 */
static void chg_drv_check_current(struct chg_drv_chip *chip, unsigned int *ibat_current, unsigned int *ilimit_current)
{
	unsigned int ilimit_work;
	unsigned int ibat_work;
	int adapter_9v_ilimit_value;
	int soft_icl_ilimit_value;
	int weak_adapter_ilimit;
	int charge_type;

	/* IBAT */
	if (CHG_DRV_IS_HOLDER_CHARGE(chip)) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		ibat_work = chip->dt_param.holder5v_ibat_current_max;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		if (chip->dev_usb.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
			ibat_work = chip->dt_param.usb9v_ibat_current_max;
		} else {
			ibat_work = chip->dt_param.usb5v_ibat_current_max;
		}
	}

	if (chip->err_info.batt_temp_warm_restrict) {
		ibat_work = CHG_DRV_GET_SMALL(chip->dt_param.common_ibat_current_temp_warm_restrict, ibat_work);
	}

	if (chip->err_info.batt_temp_cool_restrict) {
		ibat_work = CHG_DRV_GET_SMALL(chip->dt_param.common_ibat_current_temp_cool_restrict, ibat_work);
	}

	if (chip->battery_low_flag) {
		ibat_work = CHG_DRV_GET_SMALL(chip->dt_param.common_ibat_current_low_batt, ibat_work);
	}

	*ibat_current = ibat_work;

	/* ILIMIT */
	if (CHG_DRV_IS_HOLDER_CHARGE(chip)) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		adapter_9v_ilimit_value = CHG_DRV_ILIMIT_NOT_LIMITED;
		soft_icl_ilimit_value = chip->dev_holder.soft_icl_ilimit_value;
		weak_adapter_ilimit = chip->dev_holder.weak_adapter_ilimit;
		ilimit_work = chip->dev_holder.max_current;
		charge_type = chip->dev_holder.charge_type;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		adapter_9v_ilimit_value = chip->dev_usb.adapter_9v_ilimit_value;
		soft_icl_ilimit_value = chip->dev_usb.soft_icl_ilimit_value;
		weak_adapter_ilimit = chip->dev_usb.weak_adapter_ilimit;
		ilimit_work = chip->dev_usb.max_current;
		charge_type = chip->dev_usb.charge_type;
	}

	if (charge_type == CHG_DRV_CHARGE_TYPE_9V) {
		if (adapter_9v_ilimit_value != CHG_DRV_ILIMIT_NOT_LIMITED) {
			ilimit_work = CHG_DRV_GET_SMALL(adapter_9v_ilimit_value, ilimit_work);
		}
	}

	if (charge_type == CHG_DRV_CHARGE_TYPE_5V) {
		if (soft_icl_ilimit_value != CHG_DRV_ILIMIT_NOT_LIMITED) {
			ilimit_work = CHG_DRV_GET_SMALL(soft_icl_ilimit_value, ilimit_work);
		}
	}

	if (charge_type == CHG_DRV_CHARGE_TYPE_UNKNOWN) {
		ilimit_work = CHG_DRV_GET_SMALL(CHG_DRV_ILIMIT_LIMITED_500, ilimit_work);
	}

	if (weak_adapter_ilimit != CHG_DRV_ILIMIT_NOT_LIMITED) {
		ilimit_work = CHG_DRV_GET_SMALL(weak_adapter_ilimit, ilimit_work);
	}

	if (chip->max_ilimit_value != CHG_DRV_ILIMIT_NOT_LIMITED) {
		if (chip->max_ilimit_value == CHG_DRV_ILIMIT_DISABLE) {
			ilimit_work = CHG_DRV_ILIMIT_LIMITED_MIN;
		} else {
			ilimit_work = CHG_DRV_GET_SMALL(chip->max_ilimit_value, ilimit_work);
		}
	}

	if ((chip->thermal_level != 0) &&
		(chip->dt_param.chg_icl_thermal_mitigation != NULL)) {
		if (chip->dt_param.chg_icl_thermal_mitigation[chip->thermal_level] == CHG_DRV_ILIMIT_DISABLE) {
			ilimit_work = CHG_DRV_ILIMIT_LIMITED_MIN;
		} else {
			ilimit_work = CHG_DRV_GET_SMALL(chip->dt_param.chg_icl_thermal_mitigation[chip->thermal_level],
											ilimit_work);
		}
	}

	*ilimit_current = ilimit_work;

	CHG_DRV_INFOLOG("[%s] %s ibat_current:[%d] ilimit_current:[%d]\n",
					__func__, CHG_DRV_TYPE_LOG(chip), *ibat_current, *ilimit_current);
}

/**
 * chg_drv_update_cycle_charge_voltage
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_update_cycle_charge_voltage(struct chg_drv_chip *chip)
{
	union power_supply_propval pval = {0,};
	int ret = 0;
	chg_cycle_level old_cycle_chg_lvl;

	old_cycle_chg_lvl = chip->cycle_chg_lvl;

	if (chip->oem_battery_psy) {
		ret = power_supply_get_property(chip->oem_battery_psy,
				POWER_SUPPLY_PROP_CYCLE_CHG, &pval);
		if (ret) {
			CHG_DRV_ERRLOG("[%s] get CYCLE_CHG error %d\n", __func__, ret);
			return ret;
		} else {
			if (old_cycle_chg_lvl == pval.intval) {
				return ret;
			} else {
				chip->cycle_chg_lvl = pval.intval;
			}
		}
	}
	switch (chip->cycle_chg_lvl) {
	case CHG_CYCLE_LEVEL_NONE:
		chip->vmax_adj = chip->dt_param.chg_voltage;
		break;

	case CHG_CYCLE_LEVEL_01:
		chip->vmax_adj = chip->dt_param.chg_voltage_cycle_lv1;
		CHG_DRV_INFOLOG("Modify max voltage as aging battery voltage(lv1) in %s!\n",__func__);
		break;

	case CHG_CYCLE_LEVEL_02:
	default:
		chip->vmax_adj = chip->dt_param.chg_voltage_cycle_lv2;
		CHG_DRV_INFOLOG("Modify max voltage as aging battery voltage(lv2) in %s!\n",__func__);
		break;
	}

	if (old_cycle_chg_lvl != chip->cycle_chg_lvl) {
		ret = vote(chip->fv_votable, OEM_CHG_VOTER, true, chip->vmax_adj);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("fv_votable Failed. ret:[%d]\n", ret);
		}
	}

	return ret;
}

/**
 * chg_drv_get_longevity_chg_mode
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_get_longevity_chg_mode(struct chg_drv_chip *chip)
{
	union power_supply_propval pval = {0,};
	int ret = 0;

	ret = power_supply_get_property(chip->oem_battery_psy, POWER_SUPPLY_PROP_LONGEVITY_CHG_MODE, &pval);
	if (unlikely(ret < 0)) {
		chip->longevity_charge_flag = false;
	} else {
		if (pval.intval) {
			chip->longevity_charge_flag = true;
		} else {
			chip->longevity_charge_flag = false;
		}
	}

	return ret;
}

/**
 * chg_drv_pmic_aicl_setting
 *
 * @param	chip	: charger device info pointer
 *			device	: preferential device
 *			type	: AICL control type
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_aicl_setting(struct chg_drv_chip *chip,
									chg_drv_device_type device,
									chg_drv_aicl_type type)
{
	int ret  = 0;

	CHG_DRV_DBGLOG("[%s] device:[%d] type:[%d] batt:[%d]\n",
								__func__, device, type, chip->battery_low_flag);

	do {
		if ((chip->battery_low_flag) &&
			(type == CHG_DRV_AICL_TYPE_ENABLE)) {
			CHG_DRV_INFOLOG("[%s] AICL is invalidated. Because the battery voltage is low.\n", __func__);
			type = CHG_DRV_AICL_TYPE_DISABLE;
		}

		switch (type) {
			case CHG_DRV_AICL_TYPE_ENABLE:
				ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_USB_USBIN_AICL_OPTIONS_CFG_REG,
												(BIT(5) | BIT(4) | BIT(3)), (BIT(5) | BIT(4) | BIT(3)));
				if (unlikely(ret != 0)) {
					CHG_DRV_ERRLOG("[%s] AICL enable Failed. dev:[%d] ret:[%d]\n", __func__, device, ret);
					break;
				}
				ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_USB_USBIN_AICL_OPTIONS_CFG_REG,
												BIT(2), CHG_DRV_SET_HIGH);
				if (unlikely(ret != 0)) {
					CHG_DRV_ERRLOG("[%s] AICL enable Failed. dev:[%d] ret:[%d]\n", __func__, device, ret);
					break;
				}
				break;

			case CHG_DRV_AICL_TYPE_DISABLE:
				ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_USB_USBIN_AICL_OPTIONS_CFG_REG,
												(BIT(5) | BIT(4) | BIT(3)), (BIT(5) | BIT(4) | BIT(3)));
				if (unlikely(ret != 0)) {
					CHG_DRV_ERRLOG("[%s] AICL disable Failed. dev:[%d] ret:[%d]\n", __func__, device, ret);
					break;
				}
				ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_USB_USBIN_AICL_OPTIONS_CFG_REG,
												BIT(2), CHG_DRV_SET_LOW);
				if (unlikely(ret != 0)) {
					CHG_DRV_ERRLOG("[%s] AICL disable Failed. dev:[%d] ret:[%d]\n", __func__, device, ret);
					break;
				}
				break;

			case CHG_DRV_AICL_TYPE_RERUN:
				ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_MISC_AICL_CMD_REG,
												BIT(1), CHG_DRV_SET_HIGH);
				if (unlikely(ret != 0)) {
					CHG_DRV_ERRLOG("[%s] AICL restart Failed. dev:[%d] ret:[%d]\n", __func__, device, ret);
					break;
				}
				break;

			default:
				break;
		}
	} while(0);

	return ret;
}

/**
 * chg_drv_pmic_slight_charge
 *
 * @param	chip	: charger device info pointer
 *			device	: device type
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_slight_charge(struct chg_drv_chip *chip, chg_drv_device_type device)
{
	int ret = 0;

	do {
		if (device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_HOLDER, CHG_DRV_AICL_TYPE_DISABLE);
			if (unlikely(ret != 0)) {
				break;
			}
			ret = vote(chip->dc_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_500));
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("dc_icl_votable Failed. ret:[%d]\n", ret);
				break;
			}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		} else {
			ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_USB, CHG_DRV_AICL_TYPE_DISABLE);
			if (unlikely(ret != 0)) {
				break;
			}

			ret = vote(chip->usb_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_500));
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("usb_icl_votable Failed. ret:[%d]\n", ret);
				break;
			}
		}
	} while (0);

	return ret;
}

/**
 * chg_drv_pmic_set_preferential_device
 *
 * @param	chip	: charger device info pointer
 *			device	: preferential device
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_set_preferential_device(struct chg_drv_chip *chip,
											   chg_drv_device_type device)
{
	int ret = 0;

	if (device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_USB_USBIN_OPTIONS_1_CFG_REG,
										BIT(4), CHG_DRV_SET_LOW);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_USB_USBIN_OPTIONS_1_CFG_REG,
										BIT(4), CHG_DRV_SET_HIGH);
	}
	if (unlikely(ret != 0)) {
		CHG_DRV_ERRLOG("SCHG_USB_USBIN_OPTIONS_1_CFG Failed. ret:[%d]\n", ret);
	}

	return ret;
}

/**
 * chg_drv_get_charge_type
 *
 * @param	chip			: charger device info pointer
 *			chg_type		: charger voltage type pointer
 *			std_type		: charger standard type pointer
 *
 * @retval	Processing result
 */
static int chg_drv_get_charge_type(struct chg_drv_chip *chip,
								  chg_drv_charge_type *chg_type,
								  chg_drv_standard_type *std_type)
{
	int ret = 0;
	union power_supply_propval pval = {0,};

	*chg_type = CHG_DRV_CHARGE_TYPE_5V;
	*std_type = CHG_DRV_STANDARD_TYPE_UNKNOWN;

	ret = power_supply_get_property(chip->usb_psy, POWER_SUPPLY_PROP_PD_ACTIVE, &pval);
	CHG_DRV_DBGLOG("[%s] PD_ACTIVE=%d\n", __func__, pval.intval);
	if (pval.intval) {
		chip->dev_usb.max_current = CHG_DRV_ILIMIT_LIMITED_1500;

		ret = power_supply_get_property(chip->usb_psy, POWER_SUPPLY_PROP_PD_VOLTAGE_MIN, &pval);
		CHG_DRV_DBGLOG("[%s] VOLTAGE_MIN=%d\n", __func__, pval.intval);
		if ((pval.intval >= chip->dt_param.pd5v_low_voltage_threshold) &&
			(pval.intval <= chip->dt_param.pd5v_high_voltage_threshold)) {
			*chg_type = CHG_DRV_CHARGE_TYPE_5V;
		} else if ((pval.intval >= chip->dt_param.pd9v_low_voltage_threshold) &&
				   (pval.intval <= chip->dt_param.pd9v_high_voltage_threshold)) {
			*chg_type = CHG_DRV_CHARGE_TYPE_9V;
		}
		*std_type = CHG_DRV_STANDARD_TYPE_PD;

		if (chip->charge_type != *chg_type) {
			CHG_DRV_INFOLOG("[%s] %s charge type %d -> %d [1:5V 3:9V] standard type %d -> %d [0:etc 1:PD]\n",
								__func__, CHG_DRV_TYPE_LOG(chip),
								chip->charge_type, *chg_type,
								chip->standard_type, *std_type);
			chip->charge_type = *chg_type;
			chip->standard_type = *std_type;
			chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
		}
	}

	return ret;
}

/**
 * chg_drv_pmic_watchdog_pet
 */
static void chg_drv_pmic_watchdog_pet(void)
{
	int ret;

	ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_MISC_BARK_BITE_WDOG_PET_REG,
									BIT(0), CHG_DRV_SET_HIGH);
	if (unlikely(ret != 0)) {
		CHG_DRV_ERRLOG("[%s] WDOG_PET Failed. ret:[%d]\n", __func__, ret);
	}
}

/**
 * chg_drv_pmic_current_setting
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_current_setting(struct chg_drv_chip *chip)
{
	unsigned int ibat_current = 0;
	unsigned int ilimit_current = 0;
	int ret = 0;

	do {
		chg_drv_check_current(chip, &ibat_current, &ilimit_current);

		ret = vote(chip->fcc_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(ibat_current));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("fcc_votable Failed. ret:[%d]\n", ret);
			break;
		}

		if (CHG_DRV_IS_HOLDER_CHARGE(chip)) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			ret = vote(chip->dc_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(ilimit_current));
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("dc_icl_votable Failed. ret:[%d]\n", ret);
				break;
			}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		} else {
			ret = vote(chip->usb_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(ilimit_current));
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("usb_icl_votable Failed. ret:[%d]\n", ret);
				break;
			}
		}
	} while(0);

	return ret;
}

/**
 * chg_drv_pmic_initial_setting
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_initial_setting(struct chg_drv_chip *chip)
{
	int ret;

	do {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		ret = vote(chip->dc_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_500));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("dc_icl_votable Failed. ret:[%d]\n", ret);
			break;
		}
		mdelay(10);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		ret = vote(chip->usb_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_500));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("usb_icl_votable Failed. ret:[%d]\n", ret);
			break;
		}
		mdelay(10);
		ret = vote(chip->fcc_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_500));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("fcc_votable Failed. ret:[%d]\n", ret);
			break;
		}
		ret = vote(chip->fv_votable, OEM_CHG_VOTER, true, chip->vmax_adj);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("fv_votable Failed. ret:[%d]\n", ret);
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_CHGR_CHGR_CFG2_REG,
										(BIT(3) | BIT(2)), 0x00);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_CHGR_CHGR_CFG2 Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_USB_CMD_ICL_OVERRIDE_REG,
										BIT(0), BIT(0));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_USB_CMD_ICL_OVERRIDE Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_USB_USBIN_ICL_OPTIONS_REG,
										(BIT(1) | BIT(0)), (BIT(1) | BIT(0)));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_USB_USBIN_ICL_OPTIONS Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_USB_USBIN_OPTIONS_1_CFG_REG,
										BIT(2), CHG_DRV_SET_LOW);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_USB_USBIN_OPTIONS_1_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_USB_USBIN_OPTIONS_1_CFG_REG,
										BIT(4), CHG_DRV_SET_LOW);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_USB_USBIN_OPTIONS_1_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_USB_USBIN_AICL_OPTIONS_CFG_REG,
										(BIT(5) | BIT(4) | BIT(3) | BIT(2)), (BIT(5) | BIT(4) | BIT(3) | BIT(2)));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_USB_USBIN_AICL_OPTIONS_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_USB_USBIN_5V_AICL_THRESHOLD_CFG_REG,
										GENMASK(2, 0), 0x00);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_USB_USBIN_5V_AICL_THRESHOLD_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_USB_USBIN_CONT_AICL_THRESHOLD_CFG_REG,
										GENMASK(5, 0), 0x00);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_USB_USBIN_CONT_AICL_THRESHOLD_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_CHGR_JEITA_EN_CFG_REG,
										0xFF, 0x00);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_CHGR_JEITA_EN_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_USB_USBIN_ADAPTER_ALLOW_CFG_REG,
										GENMASK(3, 0), 0x08);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_USB_USBIN_ADAPTER_ALLOW_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_MISC_WD_CFG_REG,
										(BIT(7) | BIT(0)), BIT(7));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_MISC_WD_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_MISC_SNARL_BARK_BITE_WD_CFG_REG,
										GENMASK(3, 2), BIT(2));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_MISC_SNARL_BARK_BITE_WD_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_CHGR_SAFETY_TIMER_ENABLE_CFG_REG,
										BIT(1) | BIT(0), BIT(1));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_CHGR_SAFETY_TIMER_ENABLE_CFG Failed. ret:[%d]\n", ret);
			break;
		}
		ret = chg_drv_reg_write_byte(CHG_DRV_CMD_SCHG_MISC_AICL_RERUN_TIME_CFG_REG,
										GENMASK(1, 0), 0x03);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_MISC_AICL_RERUN_TIME_CFG Failed. ret:[%d]\n", ret);
			break;
		}

		ret = vote(chip->chg_disable_votable, OEM_CHG_VOTER, false, 0);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("chg_disable_votable Failed. ret:[%d]\n", ret);
			break;
		}
	} while(0);

	return ret;
}

/**
 * chg_drv_pmic_5v_charge
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_5v_charge(struct chg_drv_chip *chip)
{
	int ret = 0;

	do {
		ret = chg_drv_pmic_current_setting(chip);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("[%s] Normal Charge Setting Failed. ret:[%d]\n", __func__, ret);
			break;
		}

		if (CHG_DRV_IS_HOLDER_CHARGE(chip)) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_HOLDER, CHG_DRV_AICL_TYPE_ENABLE);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		} else {
			ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_USB, CHG_DRV_AICL_TYPE_ENABLE);
		}
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("[%s] Normal Charge Setting Failed. ret:[%d]\n", __func__, ret);
			break;
		}
	} while (0);

	return ret;
}

/**
 * chg_drv_pmic_9v_charge
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_9v_charge(struct chg_drv_chip *chip)
{
	int ret = 0;

	ret = chg_drv_pmic_current_setting(chip);
	if (unlikely(ret != 0)) {
		CHG_DRV_ERRLOG("[%s] 9V Charge Setting Failed. ret:[%d]\n", __func__, ret);
	}

	return ret;
}

/**
 * chg_drv_pmic_charge_start
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_charge_start(struct chg_drv_chip *chip)
{
	int ret;

	do {
		ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_MISC_WD_CFG_REG,
										BIT(0), CHG_DRV_SET_HIGH);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_MISC_WD_CFG Failed. ret:[%d]\n", ret);
			break;
		}
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		ret = vote(chip->dc_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_500));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("dc_icl_votable Failed. ret:[%d]\n", ret);
			break;
		}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		ret = vote(chip->usb_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_500));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("usb_icl_votable Failed. ret:[%d]\n", ret);
			break;
		}
		ret = vote(chip->chg_disable_votable, OEM_CHG_VOTER, false, 0);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("chg_disable_votable Failed. ret:[%d]\n", ret);
			break;
		}
	} while(0);

	return ret;
}

/**
 * chg_drv_pmic_charge_powerpath
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_charge_powerpath(struct chg_drv_chip *chip)
{
	int ret;

	do {
		ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_MISC_WD_CFG_REG,
										BIT(0), CHG_DRV_SET_HIGH);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_MISC_WD_CFG Failed. ret:[%d]\n", ret);
			break;
		}
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		ret = vote(chip->dc_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_500));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("dc_icl_votable Failed. ret:[%d]\n", ret);
			break;
		}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		ret = vote(chip->usb_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_500));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("usb_icl_votable Failed. ret:[%d]\n", ret);
			break;
		}
		ret = vote(chip->chg_disable_votable, OEM_CHG_VOTER, true, 0);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("chg_disable_votable Failed. ret:[%d]\n", ret);
			break;
		}
	} while (0);

	return ret;
}

/**
 * chg_drv_pmic_charge_stop
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_pmic_charge_stop(struct chg_drv_chip *chip)
{
	int ret;

	do {
		ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_MISC_WD_CFG_REG,
										BIT(0), CHG_DRV_SET_LOW);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_MISC_WD_CFG Failed. ret:[%d]\n", ret);
			break;
		}
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		ret = vote(chip->dc_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_OFF));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("dc_icl_votable Failed. ret:[%d]\n", ret);
			break;
		}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		ret = vote(chip->usb_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_OFF));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("usb_icl_votable Failed. ret:[%d]\n", ret);
			break;
		}
		ret = vote(chip->chg_disable_votable, OEM_CHG_VOTER, true, 0);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("chg_disable_votable Failed. ret:[%d]\n", ret);
			break;
		}
	} while (0);

	return ret;
}

/**
 * chg_drv_start_monitor
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_start_monitor(struct chg_drv_chip *chip)
{
	unsigned long set_timer;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	set_timer = msecs_to_jiffies(CHG_DRV_SET_TIMER_10S);
	queue_delayed_work(chip->chg_drv_wq, &chip->charge_monitor_work, round_jiffies_relative(set_timer));
}

/**
 * chg_drv_stop_monitor
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_stop_monitor(struct chg_drv_chip *chip)
{
	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	cancel_delayed_work(&chip->charge_monitor_work);
}

/**
 * chg_drv_monitor_work
 *
 * @param	work	:
 */
static void chg_drv_monitor_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, charge_monitor_work);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.monitor_info.type = CHG_DRV_MONITOR_NORMAL;
	chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);
}

/**
 * chg_drv_start_full_timer1
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_start_full_timer1(struct chg_drv_chip *chip)
{
	unsigned long set_timer;

	if (chip->full_timer_state == CHG_DRV_CHARGE_TIMER_STATE_NONE) {
		CHG_DRV_DBGLOG("[%s] in\n", __func__);
		chip->full_timer_state = CHG_DRV_CHARGE_TIMER_STATE_FULL1;
		set_timer = msecs_to_jiffies(CHG_MIN_TO_MSEC(chip->dt_param.chg_full1_timer_min));
		queue_delayed_work(chip->chg_drv_wq, &chip->full_timer_work1, round_jiffies_relative(set_timer));
	}
}

/**
 * chg_drv_start_full_timer2
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_start_full_timer2(struct chg_drv_chip *chip)
{
	unsigned long set_timer;

	if (chip->full_timer_state == CHG_DRV_CHARGE_TIMER_STATE_FULL1) {
		CHG_DRV_DBGLOG("[%s] in\n", __func__);
		chip->full_timer_state = CHG_DRV_CHARGE_TIMER_STATE_FULL2;
		set_timer = msecs_to_jiffies(CHG_MIN_TO_MSEC(chip->dt_param.chg_full2_timer_min));
		queue_delayed_work(chip->chg_drv_wq, &chip->full_timer_work2, round_jiffies_relative(set_timer));
	}
}

/**
 * chg_drv_stop_full_timer
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_stop_full_timer(struct chg_drv_chip *chip)
{
	CHG_DRV_DBGLOG("[%s] in timer state:[%d]\n", __func__, chip->full_timer_state);

	if (chip->full_timer_state != CHG_DRV_CHARGE_TIMER_STATE_NONE) {
		cancel_delayed_work(&chip->full_timer_work1);
		cancel_delayed_work(&chip->full_timer_work2);
		chip->full_timer_state = CHG_DRV_CHARGE_TIMER_STATE_NONE;
	}
}

/**
 * chg_drv_full_timer_work1
 *
 * @param	work	:
 */
static void chg_drv_full_timer_work1(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, full_timer_work1);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.timer_info.type = CHG_DRV_TIMER_FULL_1;
	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_TIMER, &evt_data);
}

/**
 * chg_drv_full_timer_work2
 *
 * @param	work	:
 */
static void chg_drv_full_timer_work2(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, full_timer_work2);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.timer_info.type = CHG_DRV_TIMER_FULL_2;
	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_TIMER, &evt_data);
}

/**
 * chg_drv_start_safety_timer
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_start_safety_timer(struct chg_drv_chip *chip)
{
	unsigned long set_timer;

	if (chip->dt_param.chg_safety_timer_min <= 0) {
		return;
	}

	if (!delayed_work_pending(&chip->safety_timer_work)) {
		set_timer = msecs_to_jiffies(CHG_MIN_TO_MSEC(chip->dt_param.chg_safety_timer_min));
		queue_delayed_work(chip->chg_drv_wq, &chip->safety_timer_work, round_jiffies_relative(set_timer));
		CHG_DRV_INFOLOG("[%s] set safety_timer = %dmin\n", __func__, chip->dt_param.chg_safety_timer_min);
	}
}

/**
 * chg_drv_stop_safety_timer
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_stop_safety_timer(struct chg_drv_chip *chip)
{
	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	cancel_delayed_work(&chip->safety_timer_work);
}

/**
 * chg_drv_safety_timer_work
 *
 * @param	work	:
 */
static void chg_drv_safety_timer_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, safety_timer_work);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.timer_info.type = CHG_DRV_TIMER_PROTECTION;
	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_TIMER, &evt_data);
}

/**
 * chg_drv_start_aicl_restart_monitor
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_start_aicl_restart_monitor(struct chg_drv_chip *chip)
{
	unsigned long set_timer;

	if (!delayed_work_pending(&chip->aicl_monitor_work)) {
		if (chip->aicl_restart_first_flag == true) {
			set_timer = msecs_to_jiffies(CHG_DRV_SET_TIMER_60S);
			queue_delayed_work(chip->chg_drv_wq, &chip->aicl_monitor_work, round_jiffies_relative(set_timer));
			CHG_DRV_DBGLOG("[%s] set aicl_restart = %dmsec\n", __func__, CHG_DRV_SET_TIMER_60S);
			chip->aicl_restart_first_flag = false;
		} else {
			set_timer = msecs_to_jiffies(CHG_DRV_SET_TIMER_600S);
			queue_delayed_work(chip->chg_drv_wq, &chip->aicl_monitor_work, round_jiffies_relative(set_timer));
			CHG_DRV_DBGLOG("[%s] set aicl_restart = %dmsec\n", __func__, CHG_DRV_SET_TIMER_600S);
		}
	}
}

/**
 * chg_drv_stop_aicl_restart_monitor
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_stop_aicl_restart_monitor(struct chg_drv_chip *chip)
{
	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	cancel_delayed_work(&chip->aicl_monitor_work);
	chip->aicl_restart_first_flag = true;
}

/**
 * chg_drv_aicl_monitor_work
 *
 * @param	work	:
 */
static void chg_drv_aicl_monitor_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, aicl_monitor_work);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.monitor_info.type = CHG_DRV_MONITOR_AICL;
	chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);
}

/**
 * chg_drv_start_adapter_volt_monitor
 *
 * @param	chip		: charger device info pointer
 *			set_period	: set period
 */
static void chg_drv_start_adapter_volt_monitor(struct chg_drv_chip *chip, int set_period)
{
	unsigned long set_timer;

	if (!delayed_work_pending(&chip->adapter_volt_monitor_work)) {
		CHG_DRV_DBGLOG("[%s] in\n", __func__);
		set_timer = msecs_to_jiffies(set_period);
		queue_delayed_work(chip->chg_drv_wq, &chip->adapter_volt_monitor_work, set_timer);
	}
}

/**
 * chg_drv_stop_adapter_volt_monitor
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_stop_adapter_volt_monitor(struct chg_drv_chip *chip)
{
	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	cancel_delayed_work(&chip->adapter_volt_monitor_work);
}

/**
 * chg_drv_adapter_volt_monitor_work
 *
 * @param	work	:
 */
static void chg_drv_adapter_volt_monitor_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, adapter_volt_monitor_work);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.monitor_info.type = CHG_DRV_MONITOR_ADAPTER_VOLTAGE;
	chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);
}

/**
 * chg_drv_start_negotiation_timer
 *
 * @param	chip	: charger device info pointer
 *			device	: charge device
 */
static void chg_drv_start_negotiation_timer(struct chg_drv_chip *chip,
										   chg_drv_device_type device)
{
	chg_drv_device_info *dev_info;
	unsigned long set_timer;

	if (device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		dev_info = &chip->dev_holder;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		dev_info = &chip->dev_usb;
	}

	if (!delayed_work_pending(&dev_info->nego_timer_work)) {
		CHG_DRV_DBGLOG("[%s] device:[%d].\n", __func__, device);
		set_timer = msecs_to_jiffies(CHG_DRV_SET_TIMER_1S);
		queue_delayed_work(chip->chg_drv_wq, &dev_info->nego_timer_work, set_timer);
	}
}

/**
 * chg_drv_stop_negotiation_timer
 *
 * @param	chip	: charger device info pointer
 *			device	: charge device
 */
static void chg_drv_stop_negotiation_timer(struct chg_drv_chip *chip,
										  chg_drv_device_type device)
{
	CHG_DRV_DBGLOG("[%s] device:[%d].\n", __func__, device);

	if (device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		cancel_delayed_work(&chip->dev_holder.nego_timer_work);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		cancel_delayed_work(&chip->dev_usb.nego_timer_work);
	}
}

/**
 * chg_drv_usb_nego_timer_work
 *
 * @param	work	 :
 */
static void chg_drv_usb_nego_timer_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, dev_usb.nego_timer_work);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.timer_info.type = CHG_DRV_TIMER_NEGOTIATION;
	evt_data.timer_info.device = CHG_DRV_DEVICE_TYPE_USB;
	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_TIMER, &evt_data);
}

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
/**
 * chg_drv_holder_nego_timer_work
 *
 * @param	work	:
 */
static void chg_drv_holder_nego_timer_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, dev_holder.nego_timer_work);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.timer_info.type = CHG_DRV_TIMER_NEGOTIATION;
	evt_data.timer_info.device = CHG_DRV_DEVICE_TYPE_HOLDER;
	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_TIMER, &evt_data);
}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

/**
 * chg_drv_start_voltage_check_timer
 *
 * @param	chip	: charger device info pointer
 *			device	: charge device
 */
static void chg_drv_start_voltage_check_timer(struct chg_drv_chip *chip,
											 chg_drv_device_type device)
{
	chg_drv_device_info *dev_info;
	unsigned long set_timer;

	if (device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		dev_info = &chip->dev_holder;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		dev_info = &chip->dev_usb;
	}

	if (!delayed_work_pending(&dev_info->check_timer_work)) {
		CHG_DRV_DBGLOG("[%s] device:[%d].\n", __func__, device);
		set_timer = msecs_to_jiffies(CHG_DRV_SET_TIMER_500MS);
		queue_delayed_work(chip->chg_drv_wq, &dev_info->check_timer_work, set_timer);
	}
}

/**
 * chg_drv_stop_voltage_check_timer
 *
 * @param	chip	: charger device info pointer
 *			device	: charge device
 */
static void chg_drv_stop_voltage_check_timer(struct chg_drv_chip *chip,
											chg_drv_device_type device)
{
	CHG_DRV_DBGLOG("[%s] device:[%d].\n", __func__, device);

	if (device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		cancel_delayed_work(&chip->dev_holder.check_timer_work);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		cancel_delayed_work(&chip->dev_usb.check_timer_work);
	}
}

/**
 * chg_drv_usb_check_timer_work
 *
 * @param	work	:
 */
static void chg_drv_usb_check_timer_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, dev_usb.check_timer_work);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.timer_info.device = CHG_DRV_DEVICE_TYPE_USB;
	evt_data.timer_info.type = CHG_DRV_TIMER_VOLTAGE_CHECK;
	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_TIMER, &evt_data);
}

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
/**
 * chg_drv_holder_check_timer_work
 *
 * @param	work	:
 */
static void chg_drv_holder_check_timer_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, dev_holder.check_timer_work);
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	CHG_DRV_DBGLOG("[%s] state=0x%x\n", __func__, chip->charger_info.state);

	evt_data.timer_info.device = CHG_DRV_DEVICE_TYPE_HOLDER;
	evt_data.timer_info.type = CHG_DRV_TIMER_VOLTAGE_CHECK;
	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_TIMER, &evt_data);
}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

/**
 * chg_drv_start_watchdog_timer
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_start_watchdog_timer(struct chg_drv_chip *chip)
{
	unsigned long set_timer;

	if (!delayed_work_pending(&chip->watchdog_work)) {
		set_timer = msecs_to_jiffies(CHG_DRV_SET_TIMER_10S);
		queue_delayed_work(chip->chg_drv_wq, &chip->watchdog_work, round_jiffies_relative(set_timer));
	}
}

/**
 * chg_drv_stop_watchdog_timer
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_stop_watchdog_timer(struct chg_drv_chip *chip)
{
	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	cancel_delayed_work(&chip->watchdog_work);
}

/**
 * chg_drv_watchdog_work
 *
 * @param	work	:
 */
static void chg_drv_watchdog_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chg_drv_chip *chip = container_of(dwork,
					struct chg_drv_chip, watchdog_work);


	CHG_DRV_DBGLOG("[%s] WatchDog Pet.\n", __func__);

	chg_drv_pmic_watchdog_pet();
	chg_drv_start_watchdog_timer(chip);
}

/**
 * chg_drv_batt_set_status
 *
 * @param	chip	: charger device info pointer
 *			status	: set status
 */
static void chg_drv_batt_set_status(struct chg_drv_chip *chip, int status)
{
	union power_supply_propval pval = {0,};

	CHG_DRV_DBGLOG("[%s] in %d\n", __func__, status);

	if (status >= POWER_SUPPLY_STATUS_MAX) {
		CHG_DRV_ERRLOG("[%s] Parameter Error! status = %d\n", __func__, status);
		return;
	}

	pval.intval = status;

	power_supply_set_property(chip->oem_battery_psy, POWER_SUPPLY_PROP_STATUS, &pval);

	return;
}

/**
 * chg_drv_batt_set_health
 *
 * @param	chip	: charger device info pointer
 *			health	: set health
 */
static void chg_drv_batt_set_health(struct chg_drv_chip *chip, int health)
{
	union power_supply_propval pval = {0,};

	CHG_DRV_DBGLOG("[%s] in %d\n", __func__, health);

	if (health >= POWER_SUPPLY_HEALTH_MAX) {
		CHG_DRV_ERRLOG("[%s] Parameter Error! health = %d\n", __func__, health);
		return;
	}

	pval.intval = health;

	power_supply_set_property(chip->oem_battery_psy, POWER_SUPPLY_PROP_HEALTH, &pval);

	return;
}

/**
 * chg_drv_get_charge_mode
 *
 * @retval	charge mode
 */
static int chg_drv_get_charge_mode(void)
{
	int charge_mode = CHG_DRV_CHARGE_MODE_MANUAL;
	u16 val;
//	u8 chg_flag = 0;
	int ret;

	do {
		ret = get_nonvolatile((u8 *)&val, APNV_CHARGE_FG_FUNC_LIMITS_I, 2);
		if (likely(ret >= 0)) {
			if ((val & 0x0001) != 0) {
				charge_mode = CHG_DRV_CHARGE_MODE_NOT_CHARGE;
			}
		} else {
			CHG_DRV_ERRLOG("[%s] NV read err result=%d\n", __func__, ret);
		}
	} while (0);

	CHG_DRV_DBGLOG("[%s] chg mode:%d\n", __func__, charge_mode);

	return charge_mode;
}

/**
 * chg_drv_common_get_property
 *
 * @param	psy	:
 *			psp	:
 *			val	:
 *
 * @retval	Processing result
 */
static int chg_drv_common_get_property(struct power_supply *psy,
									   enum power_supply_property psp,
									   union power_supply_propval *val)
{
	unsigned long src_work = 0;
	int rc = 0;

	switch (psp) {
		case POWER_SUPPLY_PROP_PRESENT:
		case POWER_SUPPLY_PROP_ONLINE:
			spin_lock(&oem_chg_drv_info_lock);
			if (charger_src & CHG_DRV_SOURCE_APSD) {
				src_work = charger_apsd_src;
			} else {
				src_work = charger_src;
			}

			switch (psy->desc->type) {
				case POWER_SUPPLY_TYPE_OEM_AC:
					val->intval = (((src_work & CHG_DRV_SOURCE_MASK) == CHG_DRV_SOURCE_AC) ? 1 : 0);
					break;

				case POWER_SUPPLY_TYPE_OEM_USB:
					val->intval = (((src_work & CHG_DRV_SOURCE_MASK) == CHG_DRV_SOURCE_USB) ? 1 : 0);
					break;

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				case POWER_SUPPLY_TYPE_OEM_HOLDER:
					val->intval = ((src_work & CHG_DRV_SOURCE_HOLDER) ? 1 : 0);
					break;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

				default:
					val->intval = 0;
					break;
			}
			spin_unlock(&oem_chg_drv_info_lock);
			break;

		default:
			rc = -EINVAL;
	}

	return rc;
}

/**
 * chg_drv_oem_chg_get_property
 *
 * @param	psy	:
 *			psp	:
 *			val	:
 *
 * @retval	Processing result
 */
static int chg_drv_oem_chg_get_property(struct power_supply *psy,
										enum power_supply_property psp,
										union power_supply_propval *val)
{
	struct chg_drv_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
		case POWER_SUPPLY_PROP_CHG_ENABLE:
			val->intval = chip->chg_enable;
			break;

		case POWER_SUPPLY_PROP_CHG_MODE:
			val->intval = chip->heat_charge_mode;
			break;

		case POWER_SUPPLY_PROP_CHG_STATE:
			if (chip->charge_type == CHG_DRV_CHARGE_TYPE_9V) {
				val->intval = 1;
			} else {
				val->intval = 0;
			}
			break;

		case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
			val->intval = chip->thermal_level;
			break;

		case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
			val->intval = chip->thermal_level_max;
			break;

		default:
			rc = -EINVAL;
	}

	return rc;
}

/**
 * chg_drv_oem_chg_set_prop_chg_enable
 *
 * @param	chip		: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_oem_chg_set_prop_chg_enable(struct chg_drv_chip *chip)
{
	chg_drv_evt_data evt_data;
	int rc = 0;

	CHG_DRV_INFOLOG("[%s] set chg_enable param to %d\n",__func__, chip->chg_enable);

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));
	evt_data.notice_info.type = CHG_DRV_NOTICE_CHG_ENABLE;

	switch (chip->chg_enable) {
		case 0:
			evt_data.notice_info.ilimit = CHG_DRV_ILIMIT_DISABLE;
			break;

		case 1:
			evt_data.notice_info.ilimit = CHG_DRV_ILIMIT_NOT_LIMITED;
			break;

		case 2:
			evt_data.notice_info.ilimit = CHG_DRV_ILIMIT_LIMITED_900;
			break;

		case 3:
			evt_data.notice_info.ilimit = CHG_DRV_ILIMIT_LIMITED_500;
			break;

		case 4:
			evt_data.notice_info.ilimit = CHG_DRV_ILIMIT_LIMITED_300;
			break;

		default:
			evt_data.notice_info.ilimit = CHG_DRV_ILIMIT_NOT_LIMITED;
			break;
	}
	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_INFO_NOTICE, &evt_data);

	return rc;
}

/**
 * chg_drv_oem_chg_set_prop_chg_mode
 *
 * @param	chip		: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_oem_chg_set_prop_chg_mode(struct chg_drv_chip *chip)
{
	chg_drv_evt_data evt_data;
	int rc = 0;

	CHG_DRV_INFOLOG("[%s] in mode:[%d]\n", __func__, chip->heat_charge_mode);

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));
	evt_data.monitor_info.type = CHG_DRV_MONITOR_NORMAL;
	chg_drv_stop_monitor(chip);
	chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);

	return rc;
}

/**
 * chg_drv_oem_chg_set_prop_chg_ctrl_limit
 *
 * @param	chip		: charger device info pointer
 *
 * @retval	Processing result
 */
static int chg_drv_oem_chg_set_prop_chg_ctrl_limit(struct chg_drv_chip *chip)
{
	CHG_DRV_INFOLOG("[%s] set chg_ctrl_limit %d\n",__func__, chip->thermal_level);
	chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);

	return 0;
}

/**
 * chg_drv_oem_chg_set_property
 *
 * @param	psy	:
 *			psp	:
 *			val	:
 *
 * @retval	Processing result
 */
static int chg_drv_oem_chg_set_property(struct power_supply *psy,
										enum power_supply_property psp,
										const union power_supply_propval *val)
{
	struct chg_drv_chip *chip = power_supply_get_drvdata(psy);
	int rc = 0;


	switch (psp) {
		case POWER_SUPPLY_PROP_CHG_ENABLE:
			chip->chg_enable = val->intval;
			rc = chg_drv_oem_chg_set_prop_chg_enable(chip);
			break;

		case POWER_SUPPLY_PROP_CHG_MODE:
			chip->heat_charge_mode = val->intval;
			rc = chg_drv_oem_chg_set_prop_chg_mode(chip);
			break;

		case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
			if (chip->thermal_level != val->intval) {
				chip->thermal_level = val->intval;
				rc = chg_drv_oem_chg_set_prop_chg_ctrl_limit(chip);
			}
			break;

		default:
			rc = -EINVAL;
	}

	return rc;
}

/**
 * chg_drv_oem_chg_is_writeable
 *
 * @param	psy	:
 *			psp	:
 *
 * @retval	Processing result
 */
static int chg_drv_oem_chg_is_writeable(struct power_supply *psy,
										enum power_supply_property psp)
{
	int rc;

	switch (psp) {
		case POWER_SUPPLY_PROP_CHG_ENABLE:
		case POWER_SUPPLY_PROP_CHG_MODE:
			rc = 1;
			break;

		default:
			rc = 0;
			break;
	}

	return rc;
}

/**
 * chg_drv_get_battery_voltage
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	battery voltage
 */
static int chg_drv_get_battery_voltage(struct chg_drv_chip *chip)
{
	union power_supply_propval pval = {0,};
	long volt_work = 0;
	int ave_vol = 0;
	int index_max = 0;
	int ret = 0;
	int i;

	ret = power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get VOLTAGE_NOW error %d\n", __func__, ret);
	}

	CHG_DRV_DBGLOG("[%s] Batt_Voltage:BMS[%d]\n", __func__, pval.intval);

	chip->batt_volt_data.volt_data[chip->batt_volt_data.index] = pval.intval;
	chip->batt_volt_data.index++;

	if (chip->batt_volt_data.index >= CHG_DRV_BATT_VOLT_INDEX) {
		chip->batt_volt_data.index = 0;
		chip->batt_volt_data.data_full = true;
	}

	if (chip->batt_volt_data.data_full) {
		index_max = CHG_DRV_BATT_VOLT_INDEX;
	} else {
		index_max = chip->batt_volt_data.index;
	}

	for (i = 0; i < index_max; i++) {
		volt_work += chip->batt_volt_data.volt_data[i];
	}
	ave_vol = (int)(volt_work / index_max);

	return ave_vol;
}

/**
 * chg_drv_get_usb_voltage
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	usb  supplyvoltage
 */
static int chg_drv_get_usb_voltage(struct chg_drv_chip *chip)
{
	union power_supply_propval pval = {0,};
	int ret = 0;
	int voltage = 0;
	u8 chgr_sts = 0;

	ret = chg_drv_reg_read(CHG_DRV_CMD_SCHG_CHGR_BATTERY_CHARGER_STATUS_1_REG, &chgr_sts);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get CHARGE_STATUS error %d\n", __func__, ret);
	}

	if ((chgr_sts & BATTERY_CHARGER_STATUS_MASK) <= TAPER_CHARGE) {
		ret = power_supply_get_property(chip->usb_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
	} else {
		ret = power_supply_get_property(chip->usb_psy, POWER_SUPPLY_PROP_VOLTAGE_USBIN, &pval);
	}
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get USBIN SupplyVoltage error %d\n", __func__, ret);
	} else {
		voltage = pval.intval;
		CHG_DRV_DBGLOG("[%s] USBIN:[%d]\n", __func__, voltage);
	}

	return voltage;
}

/**
 * chg_drv_get_parameters
 *
 * @param	chip	: charger device info pointer
 *			param	: monitor parameter data pointer
 */
static void chg_drv_get_parameters(struct chg_drv_chip *chip,
								  chg_drv_monitor_param *charger_param)
{
	union power_supply_propval pval = {0,};
	int ret = 0;
	long usbin_work = 0;
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	long dcin_work = 0;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	int i;
	u8 sts_work = 0;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	/* Supply Voltage */
	for (i = 0; i < 4; i++) {
		if (i != 0) msleep(25);
		usbin_work += chg_drv_get_usb_voltage(chip);

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		ret = power_supply_get_property(chip->dc_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("[%s] get DCIN Voltage error %d\n", __func__, ret);
		}
		dcin_work += pval.intval;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	}

	charger_param->usb_in_volt = (int)(usbin_work / 4);
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	charger_param->dc_in_volt = (int)(dcin_work / 4);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	/* Battery Voltage */
	charger_param->batt_volt = chg_drv_get_battery_voltage(chip);

	/* Battery Temp */
	ret = power_supply_get_property(chip->oem_battery_psy, POWER_SUPPLY_PROP_TEMP, &pval);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get BATT_TEMP error %d\n", __func__, ret);
	}
	charger_param->batt_temp = pval.intval;

	/* Status of Charge */
	ret = power_supply_get_property(chip->oem_battery_psy, POWER_SUPPLY_PROP_OEM_CHG_CTRL_SOC, &pval);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get BATT_CAPACITY error %d\n", __func__, ret);
	}
	charger_param->soc = pval.intval;

	/* charge status */
	ret = chg_drv_reg_read(CHG_DRV_CMD_SCHG_CHGR_BATTERY_CHARGER_STATUS_1_REG, &sts_work);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get CHARGE_STATUS error %d\n", __func__, ret);
	}
	charger_param->chg_sts = sts_work;

	/* FGIC initial status */
	ret = power_supply_get_property(chip->oem_battery_psy, POWER_SUPPLY_PROP_INITIALIZE, &pval);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get FGIC_INITIAL_STATUS error %d\n", __func__, ret);
	}
	charger_param->fgic_initial_sts = pval.intval;

	/* wdog timer status */
	ret = chg_drv_reg_read(CHG_DRV_CMD_SCHG_MISC_INT_RT_STS_REG, &sts_work);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get WDOGTIMER_STATUS error %d\n", __func__, ret);
	}
	charger_param->misc_sts = sts_work;

	ret = chg_drv_reg_read(CHG_DRV_CMD_SCHG_TYPEC_TYPE_C_STATE_MACHINE_STATUS_REG, &sts_work);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get TYPE_C_STATE_MACHINE_STATUS error %d\n", __func__, ret);
	}
	charger_param->typec_sm_sts = sts_work;
}

/**
 * chg_drv_check_supply_voltage
 *
 * @param	chip		: charger device info pointer
 *			device		: check charger device
 *			charge_type	: check charge type
 *			supply_volt	: supply voltage value
 *
 * @retval	err_type
 */
static chg_drv_err_type chg_drv_check_supply_voltage(struct chg_drv_chip *chip,
													 chg_drv_device_type device,
													 chg_drv_charge_type charge_type,
													 int supply_volt)
{
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	int uv_threshold = chg_drv_usb_volt_chk_table[charge_type].under_voltage;
	int adapter_9v_limit = 0;
	u8 int_rt_sts = 0;
	int ret = 0;

	do {
		if (charge_type == CHG_DRV_CHARGE_TYPE_UNKNOWN) {
			CHG_DRV_ERRLOG("[%s] [CHGDRVERR] charge type unknown. dev:[%d]\n", __func__, device);
			err_type = CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR;
			break;
		}

		if (device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			adapter_9v_limit = CHG_DRV_ILIMIT_NOT_LIMITED;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		} else {
			adapter_9v_limit = chip->dev_usb.adapter_9v_ilimit_value;
		}
		if (charge_type == CHG_DRV_CHARGE_TYPE_9V) {
			if (adapter_9v_limit == CHG_DRV_ILIMIT_LIMITED_500) {
				uv_threshold = chip->dt_param.usb9v_under_voltage_2nd;
			}

			if (supply_volt < uv_threshold)  {
				CHG_DRV_ERRLOG("[%s] [CHGDRVERR] Under SupplyVoltage. dev:[%d] volt:[%d] type:[%d]\n",
														__func__, device, supply_volt, charge_type);
				err_type = CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR;
				break;
			}
		}

		if (supply_volt > chg_drv_usb_volt_chk_table[charge_type].over_voltage) {
			CHG_DRV_ERRLOG("[%s] [CHGDRVERR] Over SupplyVoltage. dev:[%d] volt:[%d] type:[%d]\n",
													__func__, device, supply_volt, charge_type);
			err_type = CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR;
			break;
		}

		ret = chg_drv_reg_read(CHG_DRV_CMD_SCHG_USB_INT_RT_STS_REG, &int_rt_sts);
		if (unlikely(ret < 0)) {
			CHG_DRV_ERRLOG("[%s] get USB_INT_RT_STS error %d\n", __func__, ret);
		}
		if (int_rt_sts & BIT(3)) {
			CHG_DRV_ERRLOG("[%s] [CHGDRVERR] Over SupplyVoltage. dev:[%d] int_rt_sts:[0x%x] type:[%d]\n",
													__func__, device, int_rt_sts, charge_type);
//			err_type = CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR;
			break;
		}

		CHG_DRV_DBGLOG("[%s] dev:[%d] volt:[%d] type:[%d]\n",
								__func__, device, supply_volt, charge_type);
	} while (0);

	return err_type;
}

/**
 * chg_drv_check_parameters
 *
 * @param	chip	: charger device info pointer
 *			param	: charger parameter data pointer
 *
 * @retval	error type
 */
static chg_drv_err_type chg_drv_check_parameters(struct chg_drv_chip *chip,
												   chg_drv_monitor_param *param)
{
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	unsigned int current_err = CHG_DRV_PARAM_NO_ERROR;
	bool batt_temp_warm_flag = false;
	bool batt_temp_cool_flag = false;
	bool batt_low_flag = false;
	bool update_flag = false;
	int batt_low_volt_threshold;

	chg_drv_err_type usb_err = CHG_DRV_ERR_NON;
	chg_drv_err_type holder_err = CHG_DRV_ERR_NON;

	#define CHG_DRV_SET_ERR_TYPE(type, set)	if (type == CHG_DRV_ERR_NON) type = set;

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	CHG_DRV_DBGLOG("[%s] USBIN:[%d] DCIN:[%d] BATT_TEMP:[%d]\n",
				__func__, param->usb_in_volt, param->dc_in_volt, param->batt_temp);
#else
	CHG_DRV_DBGLOG("[%s] USBIN:[%d] BATT_TEMP:[%d]\n",
				__func__, param->usb_in_volt, param->batt_temp);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	/* Battery voltage check */
	if (((chip->dt_param.batt_over_voltage > 0) &&
		 (param->batt_volt >= chip->dt_param.batt_over_voltage)) ||
		((chip->charger_info.current_err & CHG_DRV_PARAM_BATT_VOLTAGE_OV) == CHG_DRV_PARAM_BATT_VOLTAGE_OV)) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] BATT_VOLTAGE_OV(%d)\n", __func__, param->batt_volt);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OVER_BATT_VOLTAGE);
		current_err |= CHG_DRV_PARAM_BATT_VOLTAGE_OV;
	}

	if (charging_mode) {
		batt_low_volt_threshold = chip->dt_param.batt_over_voltage_offchg;
	} else {
		batt_low_volt_threshold = chip->dt_param.batt_low_voltage_onchg;
	}

	if (param->batt_volt < batt_low_volt_threshold) {
		batt_low_flag = true;
	}
	if (chip->battery_low_flag != batt_low_flag) {
		chip->battery_low_flag = batt_low_flag;
		update_flag = true;
	}
	/* usb port failure check */
	if (chip->safety_err_info.usb_port_failure ||
		((chip->charger_info.current_err & CHG_DRV_PARAM_USB_PORT_FAILURE) == CHG_DRV_PARAM_USB_PORT_FAILURE)) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] CHG_DRV_PARAM_USB_PORT_FAILURE\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_USB_PORT_FAILURE);
		current_err |= CHG_DRV_PARAM_USB_PORT_FAILURE;
	}
	/* safe timer expire check */
	if (chip->err_info.safety_timer_expire) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] CHG_DRV_PARAM_UNSPEC_FAILURE\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_EXPIRE_TIME_OUT);
		current_err |= CHG_DRV_PARAM_UNSPEC_FAILURE;
	}
	/* ovp detect check */
	if (chip->safety_err_info.ovp_detect) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] CHG_DRV_PARAM_OVP_DETECT_ERROR\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OVP_DETECT);
		current_err |= CHG_DRV_PARAM_OVP_DETECT_ERROR;
	}

	/* weak adapter check */
	if (chip->err_info.weak_adapter) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] WEAK ADAPTER DETECT.\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_WEAK_ADAPTER);
		current_err |= CHG_DRV_PARAM_WEAK_ADAPTER;
	}
	/* recovery check */
	if (chip->err_info.recovery_err) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] RECOVERY IMPOSSIBLE.\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OTHER_ERR);
		current_err |= CHG_DRV_PARAM_RECOVERY_ERROR;
	}
	/* charge type check */
	if (chip->err_info.chg_type_unknown) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] CHARGE TYPE UNKNOWN.\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OTHER_ERR);
		current_err |= CHG_DRV_PARAM_CHG_TYPE_UNKNOWN;
	} else {
		if (((chip->charger_info.chg_source & CHG_DRV_SOURCE_HOLDER) != 0) && chip->dev_holder.voltage_check) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			if (chip->dev_holder.charge_type == CHG_DRV_CHARGE_TYPE_UNKNOWN) {
				CHG_DRV_ERRLOG("[%s] [CHGDRVERR] HOLDER CHARGE_TYPE_UNKNOWN\n", __func__);
				CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_HOLDER_FAILURE);
				current_err |= CHG_DRV_PARAM_HOLDER_FAILURE;
				chg_drv_start_negotiation_timer(chip, CHG_DRV_DEVICE_TYPE_HOLDER);
			}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		}
		if (((chip->charger_info.chg_source & CHG_DRV_SOURCE_USB_PORT) != 0) && chip->dev_usb.voltage_check) {
			if (chip->dev_usb.charge_type == CHG_DRV_CHARGE_TYPE_UNKNOWN) {
				CHG_DRV_ERRLOG("[%s] [CHGDRVERR] USB CHARGE_TYPE_UNKNOWN\n", __func__);
				CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OTHER_ERR);
				current_err |= CHG_DRV_PARAM_USB_PORT_FAILURE;
				chg_drv_start_negotiation_timer(chip, CHG_DRV_DEVICE_TYPE_USB);
			}
		}
	}
	/* Supply voltage check */
	if (chip->err_info.adapter_9v_under_voltage) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] 9V CHARGE UNDER VOLTAGE.\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OTHER_ERR);
		current_err |= CHG_DRV_PARAM_9V_UV_ERROR;
	}

	if (chip->charger_info.state != CHG_DRV_STATE_CHARGING) {
		/* over/under voltage check */
		if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_HOLDER) != 0) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			if (chip->dev_holder.charge_type != CHG_DRV_CHARGE_TYPE_UNKNOWN) {
				holder_err = chg_drv_check_supply_voltage(chip, CHG_DRV_DEVICE_TYPE_HOLDER,
															chip->dev_holder.charge_type, param->dc_in_volt);
			}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		}
		if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_USB_PORT) != 0) {
			if (chip->dev_usb.charge_type != CHG_DRV_CHARGE_TYPE_UNKNOWN) {
				usb_err = chg_drv_check_supply_voltage(chip, CHG_DRV_DEVICE_TYPE_USB,
															chip->dev_usb.charge_type, param->usb_in_volt);
			}
		}
		if ((holder_err == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR) || (usb_err == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR)) {
			CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR);
			current_err |= CHG_DRV_PARAM_ADAPTER_VOLTAGE_OV;
		}
		if ((holder_err == CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR) || (usb_err == CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR)) {
			CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR);
			current_err |= CHG_DRV_PARAM_ADAPTER_VOLTAGE_UV;
		}
	}

	/* charge disable check */
	if (chip->max_ilimit_value == CHG_DRV_ILIMIT_DISABLE) {
		CHG_DRV_ERRLOG("[%s] CHG_DRV_PARAM_CHG_DISABLE\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_CHARGE_DISABLE);
		current_err |= CHG_DRV_PARAM_CHG_DISABLE;
	}

	if (chip->dt_param.chg_icl_thermal_mitigation[chip->thermal_level] == CHG_DRV_ILIMIT_DISABLE) {
		CHG_DRV_ERRLOG("[%s] CHG_DRV_PARAM_CHG_DISABLE\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_CHARGE_DISABLE);
		current_err |= CHG_DRV_PARAM_CHG_DISABLE;
	}

	/* Battery temp cold check */
	if (param->batt_temp <= chip->batt_temp_cold_limit) {
		CHG_DRV_ERRLOG("[%s] BATT_TEMP_COLD(%d)\n", __func__, param->batt_temp);
		current_err |= CHG_DRV_PARAM_BATT_TEMP_COLD;
		batt_temp_cool_flag = true;
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_BATT_TEMP_COLD_ERR);
		chip->batt_temp_cold_limit = chip->dt_param.batt_temp_cold_limit + chip->dt_param.batt_temp_limit_cancel;
	} else {
		chip->batt_temp_cold_limit = chip->dt_param.batt_temp_cold_limit;
	}
	/* Battery temp hot check */
	if (param->batt_temp >= chip->batt_temp_hot_limit) {
		CHG_DRV_ERRLOG("[%s] temp_hot BATT_TEMP(%d)\n", __func__, param->batt_temp);
		current_err |= CHG_DRV_PARAM_BATT_TEMP_HOT;
		batt_temp_warm_flag = true;
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_BATT_TEMP_HOT_ERR);
		if (chip->batt_temp_hot_limit == chip->dt_param.batt_temp_start_limit) {
			chip->batt_temp_hot_limit = chip->dt_param.batt_temp_start_limit - chip->dt_param.batt_temp_limit_cancel;
		} else if (chip->batt_temp_hot_limit == chip->dt_param.batt_temp_hot_limit) {
			chip->batt_temp_hot_limit = chip->dt_param.batt_temp_hot_limit - chip->dt_param.batt_temp_limit_cancel;
		}
	} else {
		chip->batt_temp_hot_limit = chip->dt_param.batt_temp_hot_limit;
	}
	/* Battery temp warm check */
	if (param->batt_temp >= chip->batt_temp_warm_limit) {
		CHG_DRV_WARNLOG("[%s] BATT_TEMP_WARM(%d)\n", __func__, param->batt_temp);
		current_err |= CHG_DRV_PARAM_BATT_TEMP_WARM;
		batt_temp_warm_flag = true;
		chip->batt_temp_warm_limit = chip->dt_param.batt_temp_warm_limit - chip->dt_param.batt_temp_limit_cancel;
	} else {
		chip->batt_temp_warm_limit = chip->dt_param.batt_temp_warm_limit;
	}
	if (batt_temp_warm_flag != chip->err_info.batt_temp_warm_restrict) {
		chip->err_info.batt_temp_warm_restrict = batt_temp_warm_flag;
		update_flag = true;
	}
	/* Battery temp cool check */
	if (param->batt_temp <= chip->batt_temp_cool_limit) {
		CHG_DRV_WARNLOG("[%s] BATT_TEMP_COOL(%d)\n", __func__, param->batt_temp);
		current_err |= CHG_DRV_PARAM_BATT_TEMP_COOL;
		batt_temp_cool_flag = true;
		chip->batt_temp_cool_limit = chip->dt_param.batt_temp_cool_limit + chip->dt_param.batt_temp_limit_cancel;
	} else {
		chip->batt_temp_cool_limit = chip->dt_param.batt_temp_cool_limit;
	}
	if (batt_temp_cool_flag != chip->err_info.batt_temp_cool_restrict) {
		chip->err_info.batt_temp_cool_restrict = batt_temp_cool_flag;
		update_flag = true;
	}
	/* heat charge mode check */
	if (chip->heat_charge_mode == OEM_CHG_CHARGE_MODE_POWERPATH) {
		CHG_DRV_WARNLOG("[%s] HEAT_CHARGE_MODE [%d]\n",
												__func__, chip->heat_charge_mode);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_HEAT_CHARGE);
	}

	/* fgic initializing check */
	if (!param->fgic_initial_sts) {
		CHG_DRV_ERRLOG("[%s] FGIC INITIALIZING.\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_FGIC_INITIALIZING);
		current_err |= CHG_DRV_PARAM_FGIC_INITIALIZING;
	}

	/* Watch Dog Timer expire check */
	if ((param->misc_sts & BIT(1)) ||
		((chip->charger_info.current_err & CHG_DRV_PARAM_WDOG_TIMEOUT) == CHG_DRV_PARAM_WDOG_TIMEOUT)) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] WDOG TIMEOUT.\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OTHER_ERR);
		current_err |= CHG_DRV_PARAM_WDOG_TIMEOUT;
	}

	/* PDO profile error check */
	if (chip->safety_err_info.pdo_profile_error) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] PDO PROFILE ERROR.\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OTHER_ERR);
		current_err |= CHG_DRV_PARAM_PDO_PROFILE_ERROR;
	}

	if (chip->err_info.unspec_cable_err) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] UNSPEC CABLE.\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_UNSPEC_CABLE);
		current_err |= CHG_DRV_PARAM_UNSPEC_CABLE;
	}

	/* update check */
	if (update_flag) {
		chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
	}

	if (chip->dev_usb.soft_icl_step_error != CHG_DRV_PARAM_NO_ERROR) {
		CHG_DRV_ERRLOG("[%s] [CHGDRVERR] SOFT ICL WEAK ADAPTER DETECT.\n", __func__);
		CHG_DRV_SET_ERR_TYPE(err_type, CHG_DRV_ERR_OTHER_ERR);
		current_err |= chip->dev_usb.soft_icl_step_error;
	}

	if (chip->charger_info.current_err != current_err) {
		CHG_DRV_RECLOG("[%s] error sts update [0x%08x]->[0x%08x] err_type[%d]\n",
					__func__, chip->charger_info.current_err, current_err, err_type);
	}

	chip->charger_info.current_err = current_err;

	return err_type;
}

/**
 * chg_drv_check_weak_adapter
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_check_weak_adapter(struct chg_drv_chip *chip)
{
	int index = (sizeof(ilimit_table) / sizeof(unsigned int)) - 1;
	chg_drv_evt_data evt_data;
	chg_drv_device_info *dev_info;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	if (CHG_DRV_IS_HOLDER_CHARGE(chip)) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		dev_info = &chip->dev_holder;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		dev_info = &chip->dev_usb;
	}

	do {
		if (chip->charge_type != CHG_DRV_CHARGE_TYPE_5V) {
			break;
		}

		if (dev_info->weak_adapter_ilimit != CHG_DRV_ILIMIT_NOT_LIMITED) {
			for ( ; index >= 0; index--) {
				if (ilimit_table[index] == dev_info->weak_adapter_ilimit) {
					index--;
					break;
				}
			}
		}

		if (index >= 0) {
			if (dev_info->weak_adapter_ilimit != CHG_DRV_ILIMIT_NOT_LIMITED) {
				dev_info->weak_adapter_ilimit = ilimit_table[index];
			} else {
				dev_info->weak_adapter_ilimit = CHG_DRV_ILIMIT_LIMITED_900;
			}
			CHG_DRV_WARNLOG("[%s] Weak Adapter Detect. ILIMIT:[%d]\n",
										__func__, dev_info->weak_adapter_ilimit);
		} else {
			chg_drv_stop_monitor(chip);
			chip->err_info.weak_adapter = true;
			evt_data.monitor_info.type = CHG_DRV_MONITOR_NORMAL;
			chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);
			CHG_DRV_ERRLOG("[%s] [CHGDRVERR] Weak Adapter Detect. Charging STOP.\n", __func__);
		}
	} while (0);
}

/**
 * chg_drv_aicl_restart_check
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_aicl_restart_check(struct chg_drv_chip *chip)
{
	int ret = 0;
	u8 icl_sts = 0;
	u8 icl_setting = 0;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	chg_drv_reg_read(CHG_DRV_CMD_SCHG_DCDC_AICL_ICL_STATUS, &icl_sts);
	chg_drv_reg_read(CHG_DRV_CMD_SCHG_USB_USBIN_CURRENT_LIMIT_CFG_REG, &icl_setting);

	if (icl_sts != icl_setting) {
		ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_MISC_AICL_CMD_REG,
										BIT(1), CHG_DRV_SET_HIGH);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_MISC_AICL_CMD Failed. ret:[%d]\n", ret);
		}
		mdelay(1000);
		ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_MISC_AICL_CMD_REG,
										BIT(1), CHG_DRV_SET_HIGH);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("SCHG_MISC_AICL_CMD Failed. ret:[%d]\n", ret);
		}
	}
}

/**
 * chg_drv_soft_icl_usb
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	next state
 */
static bool chg_drv_soft_icl_usb(struct chg_drv_chip *chip)
{
	bool ret = false;
	int supply_volt;
	u8 icl_sts = 0;
	u8 usbin_il = 0;

	if (chip->dev_usb.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
		return ret;
	}

	if ((chip->dt_param.usb5v_soft_icl_low_voltage <= 0) ||
		(chip->dt_param.usb5v_soft_icl_high_voltage <= 0) ||
		(chip->dt_param.chg_icl_sts_num <= 0)) {
		return ret;
	}

	supply_volt = chg_drv_get_usb_voltage(chip);

	chg_drv_reg_read(CHG_DRV_CMD_SCHG_DCDC_AICL_ICL_STATUS, &icl_sts);
	chg_drv_reg_read(CHG_DRV_CMD_SCHG_USB_USBIN_CURRENT_LIMIT_CFG_REG, &usbin_il);

	do {
		if (supply_volt > chip->dt_param.usb5v_soft_icl_high_voltage) {
			chip->dev_usb.soft_icl_step_up_count++;
		} else {
			chip->dev_usb.soft_icl_step_up_count = 0;
			break;
		}

		if (chip->dev_usb.soft_icl_step_up_count >= 10) {
			chip->dev_usb.soft_icl_step_up_count = 0;
		} else {
			break;
		}

		CHG_DRV_DBGLOG("[%s] ICL_STS[0x%02X] USBIN_IL[0x%02X] threshold_min[0x%02X] threshold_max[0x%02X]\n",
							__func__, icl_sts, usbin_il,
							chip->dt_param.chg_icl_sts_thresholds_min[chip->dev_usb.soft_icl_step_count],
							chip->dt_param.chg_icl_sts_thresholds_max[chip->dev_usb.soft_icl_step_count]);

		if (icl_sts < chip->dt_param.chg_icl_sts_thresholds_min[chip->dev_usb.soft_icl_step_count] ||
			icl_sts > chip->dt_param.chg_icl_sts_thresholds_max[chip->dev_usb.soft_icl_step_count] ) {
			ret = true;
		}

		if (icl_sts != usbin_il) {
			break;
		}

		if (chip->dev_usb.soft_icl_step_count < chip->dt_param.chg_icl_sts_num) {
			chip->dev_usb.soft_icl_step_count++;
		}
	} while (0);

	if (supply_volt < chip->dt_param.usb5v_soft_icl_low_voltage) {
		if (chip->dev_usb.soft_icl_step_count > CHG_DRV_SOFT_ICL_STEP_01) {
			chip->dev_usb.soft_icl_step_count--;
		} else if(supply_volt < chip->dt_param.usb5v_under_voltage) {
			chip->dev_usb.soft_icl_step_error = CHG_DRV_PARAM_ADAPTER_VOLTAGE_UV;
			CHG_DRV_ERRLOG("[%s] [CHGDRVERR] supply_volt[%d] SOFT ICL UV.\n", __func__, supply_volt);
		}
	}

	if (chip->dev_usb.soft_icl_ilimit_value != chip->dt_param.chg_icl_step_current_deltas[chip->dev_usb.soft_icl_step_count]) {
			chip->dev_usb.soft_icl_ilimit_value = chip->dt_param.chg_icl_step_current_deltas[chip->dev_usb.soft_icl_step_count];
			ret = true;
	}

	CHG_DRV_DBGLOG("[%s] supply_volt[%d] ilimit_value[%d]\n", __func__, supply_volt, chip->dev_usb.soft_icl_ilimit_value);


	return ret;
}

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
/**
 * chg_drv_soft_icl_holder
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	next state
 */
static bool chg_drv_soft_icl_holder(struct chg_drv_chip *chip)
{
	bool ret = false;

	if (chip->dev_holder.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
		return ret;
	}

	return ret;
}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

/**
 * chg_drv_watch_usb_adapter_voltage
 *
 * @param	chip		: charger device info pointer
 *			err_type	: error type
 *			supply_volt	: supply voltage value
 *
 * @retval	result type
 */
static chg_drv_result_type chg_drv_watch_usb_adapter_voltage(struct chg_drv_chip *chip,
														   chg_drv_err_type *err_type,
														   int supply_volt)
{
	chg_drv_result_type result = CHG_DRV_RESULT_SUCCESS;
	chg_drv_charge_type check_type;
	chg_drv_charge_type chg_type_sts = CHG_DRV_CHARGE_TYPE_UNKNOWN;
	chg_drv_standard_type std_type_sts = CHG_DRV_STANDARD_TYPE_UNKNOWN;
	int ret = 0;

	do {
		if (!chip->dev_usb.voltage_check) {
			break;
		}

		ret = chg_drv_get_charge_type(chip, &chg_type_sts, &std_type_sts);
		if (ret == 0) {
			if (chg_type_sts == CHG_DRV_CHARGE_TYPE_9V) {
				check_type = CHG_DRV_CHARGE_TYPE_9V;
			} else {
				check_type = CHG_DRV_CHARGE_TYPE_5V;
			}
		} else {
			check_type = CHG_DRV_CHARGE_TYPE_5V;
		}
		if (chip->dev_usb.charge_type != check_type) {
			chip->dev_usb.watch_count = 0;
			chip->dev_usb.adapter_9v_ilimit_value = CHG_DRV_ILIMIT_NOT_LIMITED;
			chip->dev_usb.charge_type = check_type;
			break;
		}

		*err_type = chg_drv_check_supply_voltage(chip, CHG_DRV_DEVICE_TYPE_USB,
													chip->dev_usb.charge_type, supply_volt);
		if (*err_type == CHG_DRV_ERR_NON) {
			chip->dev_usb.watch_count = 0;
			break;
		}

		chip->dev_usb.watch_count++;

		if (chip->dev_usb.watch_count < 3) {
			result = CHG_DRV_RESULT_VOLTAGE_ERROR;
			break;
		}

		chip->dev_usb.watch_count = 0;

		if (chip->dev_usb.charge_type == CHG_DRV_CHARGE_TYPE_5V) {
			result = CHG_DRV_RESULT_FAILURE;
			break;
		}
		if (*err_type == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR) {
			result = CHG_DRV_RESULT_FAILURE;
			break;
		}

		if (chip->dev_usb.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
			if (chip->dev_usb.adapter_9v_ilimit_value == CHG_DRV_ILIMIT_LIMITED_500) {
				chip->err_info.adapter_9v_under_voltage = true;
				result = CHG_DRV_RESULT_FAILURE;
				break;
			}

			if (chip->dev_usb.adapter_9v_ilimit_value == CHG_DRV_ILIMIT_NOT_LIMITED) {
				chip->dev_usb.adapter_9v_ilimit_value = CHG_DRV_ILIMIT_LIMITED_900;
			} else {
				chip->dev_usb.adapter_9v_ilimit_value = CHG_DRV_ILIMIT_LIMITED_500;
			}
			result = CHG_DRV_RESULT_RETRY;
		}
	} while (0);

	return result;
}

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
/**
 * chg_drv_watch_holder_adapter_voltage
 *
 * @param	chip		: charger device info pointer
 *			err_type	: error type
 *			supply_volt	: supply voltage value
 *
 * @retval	result type
 */
static chg_drv_result_type chg_drv_watch_holder_adapter_voltage(struct chg_drv_chip *chip,
															  chg_drv_err_type *err_type,
															  int supply_volt)
{
	chg_drv_result_type result = CHG_DRV_RESULT_SUCCESS;
	chg_drv_charge_type check_type;
	bool adc_error = false;
	int uv_threshold;

	do {
		if (!chip->dev_holder.voltage_check) {
			break;
		}

		check_type = chip->dev_holder.charge_type;

		if (check_type == CHG_DRV_CHARGE_TYPE_UNKNOWN) {
			*err_type = CHG_DRV_ERR_HOLDER_FAILURE;
			result = CHG_DRV_RESULT_FAILURE;
			break;
		}

		uv_threshold = chg_drv_holder_volt_chk_table[check_type].under_voltage;
		*err_type = chg_drv_check_supply_voltage(chip, CHG_DRV_DEVICE_TYPE_HOLDER, check_type, supply_volt);
		adc_error = false;

		if (*err_type == CHG_DRV_ERR_NON) {
			chip->dev_holder.watch_count = 0;
			break;
		}

		chip->dev_holder.watch_count++;

		if (chip->dev_holder.watch_count < 3) {
			result = CHG_DRV_RESULT_VOLTAGE_ERROR;
			break;
		}

		chip->dev_holder.watch_count = 0;

		if (adc_error) {
			chip->dev_holder.charge_type = CHG_DRV_CHARGE_TYPE_UNKNOWN;
			result = CHG_DRV_RESULT_FAILURE;
			break;
		}
		if (check_type == CHG_DRV_CHARGE_TYPE_5V) {
			result = CHG_DRV_RESULT_FAILURE;
			break;
		}
		if (*err_type == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR) {
			result = CHG_DRV_RESULT_FAILURE;
			break;
		}

		result = CHG_DRV_RESULT_RETRY;

	} while (0);

	return result;
}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

/**
 * chg_drv_charge_type_judgement
 *
 * @param	chip		: charger device info pointer
 *			device		: charger device
 *			err_type	: error type
 *
 * @retval	result type
 */
static chg_drv_result_type chg_drv_charge_type_judgement(struct chg_drv_chip *chip,
														   chg_drv_device_type device,
														   chg_drv_err_type *err_type)
{
	chg_drv_device_info *dev_info;
	chg_drv_result_type result = CHG_DRV_RESULT_CONTINUE;
	int voltage = 0;

	if (device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		dev_info = &chip->dev_holder;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		dev_info = &chip->dev_usb;
	}

	do {
		if (device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			power_supply_get_property(chip->dc_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
			voltage = pval.intval;		
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		} else {
			voltage = chg_drv_get_usb_voltage(chip);	
		}

		*err_type = chg_drv_check_supply_voltage(chip, device, dev_info->charge_type, voltage);		
		if (likely(*err_type == CHG_DRV_ERR_NON)) {
			dev_info->check_count[dev_info->charge_type]++;
			if (dev_info->check_count[dev_info->charge_type] >= chip->dt_param.chg_adapter_voltage_detect_count) {
				CHG_DRV_INFOLOG("[%s] %s charge type %d [1:5V 2:9V]\n",
											__func__, CHG_DRV_TYPE_LOG(chip), dev_info->charge_type);

				memset(dev_info->check_count, 0, sizeof(dev_info->check_count));
				dev_info->check_total = 0;
				result = CHG_DRV_RESULT_SUCCESS;
				break;
			}
		} else {
			memset(dev_info->check_count, 0, sizeof(dev_info->check_count));
			dev_info->save_voltage_error = *err_type;
			result = CHG_DRV_RESULT_FAILURE;
		}

		dev_info->check_total++;

		if (dev_info->check_total >= chip->dt_param.chg_adapter_voltage_detect_max) {
			CHG_DRV_ERRLOG("[%s] [CHGDRVERR] %s SupplyVoltage Total Count Over Error\n",
													__func__, CHG_DRV_TYPE_LOG(chip));
			dev_info->check_total = 0;
			*err_type = dev_info->save_voltage_error;
			result = CHG_DRV_RESULT_OVER;
			break;
		}
	} while (0);

	return result;
}

/**
 * charge_is_battery_full
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	battey full
 */
static bool charge_is_battery_full(struct chg_drv_chip *chip)
{
	union power_supply_propval pval = {0,};
	int ibat_ma = 0;
	bool batt_full = false;
	int ret = 0;
	int termcurrent = 0;			

	if (likely(chip->bms_psy)) {
		ret = power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_CURRENT_NOW, &pval);
		if (likely(ret == 0)) {
			ibat_ma = pval.intval / 1000;

			CHG_DRV_DBGLOG("[%s] ibat_ma:[%d]\n", __func__, ibat_ma);

			if (chip->cycle_chg_lvl == CHG_CYCLE_LEVEL_NONE) {
				termcurrent = chip->dt_param.chg_termcurrent_ma;
			} else {
				termcurrent = chip->dt_param.chg_cycle_termcurrent_ma;
			}

			if ((ibat_ma * -1) > termcurrent) {
				CHG_DRV_DBGLOG("[%s] Not at EOC, battery current too high\n", __func__);
				chip->consecutive_count = 0;
			} else if (ibat_ma > 0) {
				CHG_DRV_DBGLOG("[%s] Charging but system demand increased\n", __func__);
				chip->consecutive_count = 0;
			} else {
				chip->consecutive_count++;
			}
		} else {
			CHG_DRV_DBGLOG("[%s] Get Current Failed. ret:[%d]\n", __func__, ret);
			chip->consecutive_count = 0;
		}
	} else {
		CHG_DRV_DBGLOG("[%s] No BMS supply registered return 0\n", __func__);
		chip->consecutive_count = 0;
	}

	if (chip->consecutive_count > chip->dt_param.chg_consecutive_count) {
		batt_full = true;
		chip->consecutive_count = 0;
	}

	return batt_full;
}

/**
 * chg_drv_check_apsd_result
 *
 * @param	chip	: charger device info pointer
 *
 */
static void chg_drv_check_apsd_result(struct chg_drv_chip *chip)
{
	int ret = 0;
	u8 val;
	chg_drv_device_info *dev_info;

	if (CHG_DRV_IS_HOLDER_CHARGE(chip)) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		dev_info = &chip->dev_holder;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		dev_info = &chip->dev_usb;
	}

	ret = chg_drv_reg_read(CHG_DRV_CMD_SCHG_USB_APSD_RESULT_STATUS_REG, &val);
	if (likely(ret == 0)) {
		val &= APSD_RESULT_STATUS_MASK;
		if (chip->apsd_type != val) {
			chip->apsd_type = val;
			if (val & SDP_CHARGER_BIT) {
				/* USB */
				charger_apsd_src = CHG_DRV_SOURCE_USB;
				dev_info->max_current = CHG_DRV_CURRENT_500;
			} else if ((val & CDP_CHARGER_BIT) ||
					   (val & DCP_CHARGER_BIT) ||
					   (val & QC_2P0_BIT) ||
					   (val & QC_3P0_BIT)) {
				/* AC */
				charger_apsd_src = CHG_DRV_SOURCE_AC;
				dev_info->max_current = CHG_DRV_CURRENT_1500;
			} else {
				/* Float / Other */
				charger_apsd_src = CHG_DRV_SOURCE_AC;
				dev_info->max_current = CHG_DRV_CURRENT_500;
			}
			CHG_DRV_RECLOG("[%s] APSD result:0x%x, mA=%d\n",
								__func__, val, dev_info->max_current);
			chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
		}
	}
}

/**
 * chg_drv_check_reverse_boost
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 *				1	: Executed reverse_boost fail safe
 *				0	: Do nothing
 */
static int chg_drv_check_reverse_boost(struct chg_drv_chip *chip)
{
	int result = 0;
	int ret = 0;
	union power_supply_propval pval = {0,};
	u8 sts = 0;
	int usb_icl_ua = 0;

	if (chip->charge_ctrl == CHG_DRV_CONTROL_STOP) {
		return result;
	}

	ret = power_supply_get_property(chip->usb_psy, POWER_SUPPLY_PROP_INPUT_CURRENT_NOW, &pval);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] Get Prop InputCurrentNow Failed. ret:[%d]\n", __func__, ret);
	}

	ret = chg_drv_reg_read(CHG_DRV_CMD_SCHG_DCDC_AICL_STATUS, &sts);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get AICL_STATUS error %d\n", __func__, ret);
	}

	if ((pval.intval < 20000) &&
		(sts & BIT(5))) {
		CHG_DRV_INFOLOG("[%s] [CHGDRVERR] Executed reverse_boost fail safe. Input Current Now[%d]ua, sts[0x%x]\n",
						 __func__, pval.intval, sts);

		usb_icl_ua = get_effective_result(chip->usb_icl_votable);

		ret = vote(chip->usb_icl_votable, OEM_CHG_VOTER, true, CHG_MA_TO_UA(CHG_DRV_CURRENT_OFF));
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("usb_icl_votable Failed. ret:[%d]\n", ret);
		}
		msleep(2000);
		ret = vote(chip->usb_icl_votable, OEM_CHG_VOTER, true, usb_icl_ua);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("usb_icl_votable Failed. ret:[%d]\n", ret);
		}
		result = 1;
	}
	return result;
}

/**
 * chg_drv_terminate_usb
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_terminate_usb(struct chg_drv_chip *chip)
{
	chg_drv_stop_negotiation_timer(chip, CHG_DRV_DEVICE_TYPE_USB);
	chg_drv_stop_voltage_check_timer(chip, CHG_DRV_DEVICE_TYPE_USB);

	chip->dev_usb.charge_type = CHG_DRV_CHARGE_TYPE_UNKNOWN;
	chip->dev_usb.max_current = CHG_DRV_CURRENT_OFF;
	memset(chip->dev_usb.check_count, 0, sizeof(chip->dev_usb.check_count));
	chip->dev_usb.check_total = 0;
	memset(chip->dev_usb.nego_count, 0, sizeof(chip->dev_usb.nego_count));
	chip->dev_usb.nego_total = 0;
	chip->dev_usb.watch_count = 0;
	chip->dev_usb.save_voltage_error = CHG_DRV_ERR_NON;
	chip->dev_usb.soft_icl_step_up_count = 0;
	chip->dev_usb.soft_icl_step_count = chip->dt_param.chg_icl_step_current_start_idx;
	chip->dev_usb.soft_icl_step_error = CHG_DRV_PARAM_NO_ERROR;
	chip->dev_usb.adapter_9v_ilimit_value = CHG_DRV_ILIMIT_NOT_LIMITED;
	chip->dev_usb.soft_icl_ilimit_value = chip->dt_param.chg_icl_step_current_deltas[chip->dt_param.chg_icl_step_current_start_idx];
	chip->dev_usb.weak_adapter_ilimit = CHG_DRV_ILIMIT_NOT_LIMITED;
	chip->dev_usb.voltage_check = false;
}

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
/**
 * chg_drv_terminate_holder
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_terminate_holder(struct chg_drv_chip *chip)
{
	chg_drv_stop_negotiation_timer(chip, CHG_DRV_DEVICE_TYPE_HOLDER);
	chg_drv_stop_voltage_check_timer(chip, CHG_DRV_DEVICE_TYPE_HOLDER);

	chip->dev_holder.charge_type = CHG_DRV_CHARGE_TYPE_UNKNOWN;
	chip->dev_holder.max_current = CHG_DRV_CURRENT_OFF;
	memset(chip->dev_holder.check_count, 0, sizeof(chip->dev_holder.check_count));
	chip->dev_holder.check_total = 0;
	memset(chip->dev_holder.nego_count, 0, sizeof(chip->dev_holder.nego_count));
	chip->dev_holder.nego_total = 0;
	chip->dev_holder.watch_count = 0;
	chip->dev_holder.save_voltage_error = CHG_DRV_ERR_NON;
	chip->dev_holder.soft_icl_step_up_count = 0;
	chip->dev_holder.soft_icl_step_count = chip->dt_param.chg_icl_step_current_start_idx;
	chip->dev_holder.soft_icl_step_error = CHG_DRV_PARAM_NO_ERROR;
	chip->dev_holder.adapter_9v_ilimit_value = CHG_DRV_ILIMIT_NOT_LIMITED;
	chip->dev_holder.soft_icl_ilimit_value = CHG_DRV_ILIMIT_NOT_LIMITED;
	chip->dev_holder.weak_adapter_ilimit = CHG_DRV_ILIMIT_NOT_LIMITED;
	chip->dev_holder.voltage_check = false;
}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

/**
 * chg_drv_terminate
 *
 * @param	chip		: charger device info pointer
 *			shutdown	: [true]shutdown [false]other
 */
static void chg_drv_terminate(struct chg_drv_chip *chip, bool shutdown)
{
	int ret = 0;

	chg_drv_stop_monitor(chip);
	chg_drv_stop_adapter_volt_monitor(chip);
	chg_drv_stop_full_timer(chip);
	chg_drv_stop_safety_timer(chip);
	chg_drv_stop_aicl_restart_monitor(chip);
	chg_drv_stop_watchdog_timer(chip);

	chg_drv_terminate_usb(chip);
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	chg_drv_terminate_holder(chip);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	if (!shutdown) {
		if (chip->safety_err_info.usb_port_failure == true) {
			chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_USB_HOT);
			chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_NOT_CHARGING);
		} else if (chip->safety_err_info.ovp_detect == true) {
			chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_OVER_SUPPLY_VOLTAGE);
			chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_NOT_CHARGING);
		} else if (chip->safety_err_info.pdo_profile_error == true) {
			chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_OTHER_ERROR);
			chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_NOT_CHARGING);
		} else {
			chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_GOOD);
			chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_DISCHARGING);
		}
	}

	ret = chg_drv_update_cycle_charge_voltage(chip);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] update_cycle_charge_voltage failed. ret = %d\n", __func__, ret);
	}

	chg_drv_pmic_initial_setting(chip);

	chip->charger_info.chg_source  = 0;
	chip->charger_info.chg_current = 0;
	chip->charger_info.current_err = CHG_DRV_PARAM_NO_ERROR;

	chip->connect_state = CHG_DRV_CONNECT_STATE_NONE;
	chip->charge_type = CHG_DRV_CHARGE_TYPE_5V;
	chip->standard_type = CHG_DRV_STANDARD_TYPE_UNKNOWN;
	chip->apsd_type = 0;			
	chip->consecutive_count = 0;
	chip->battery_low_flag = false;

	memset(&chip->err_info, 0, sizeof(chg_drv_err_info));

	chip->batt_temp_hot_limit = chip->dt_param.batt_temp_start_limit;
	chip->batt_temp_warm_limit = chip->dt_param.batt_temp_warm_limit;
	chip->batt_temp_cold_limit = chip->dt_param.batt_temp_cold_limit;
	chip->batt_temp_cool_limit = chip->dt_param.batt_temp_cool_limit;

	chip->recovery_count = 0;
	chip->recovery_invalid = true;

	chip->charge_ctrl = CHG_DRV_CONTROL_NONE;

	memset(chip->batt_volt_data.volt_data, 0, sizeof(chip->batt_volt_data.volt_data));
	chip->batt_volt_data.index = 0;
	chip->batt_volt_data.data_full = false;
}

/**
 * chg_drv_charge_control
 *
 * @param	chip		: charger device info pointer
 *			charge_ctrl	: charger control type
 */
static int chg_drv_charge_control(struct chg_drv_chip *chip,
								 chg_drv_ctrl_type charge_ctrl)
{
	int ret = 0;

	CHG_DRV_DBGLOG("[%s] in charge_ctrl = %d\n", __func__, charge_ctrl);

	if (chip->charge_ctrl == charge_ctrl) {
		return ret;
	}

	switch (charge_ctrl) {
		case CHG_DRV_CONTROL_PRECHARGE:
			ret = chg_drv_pmic_charge_start(chip);
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("[%s] PreCharge Failed. ret:[%d]\n", __func__, ret);
			}
			chip->recovery_invalid = false;
			break;

		case CHG_DRV_CONTROL_RECHARGE:
		case CHG_DRV_CONTROL_CHARGE_START:
			ret = chg_drv_pmic_charge_start(chip);
			if (likely(ret == 0)) {
				if (chip->charge_type == CHG_DRV_CHARGE_TYPE_9V) {
					ret = chg_drv_pmic_9v_charge(chip);
				} else {
					ret = chg_drv_pmic_5v_charge(chip);
				}
			}
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("[%s] Charge Start Failed. ret:[%d]\n", __func__, ret);
			}
			chip->recovery_invalid = false;
			break;

		case CHG_DRV_CONTROL_POWERPATH:
			ret = chg_drv_pmic_charge_powerpath(chip);
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("[%s] PowerPath Failed. ret:[%d]\n", __func__, ret);
			}
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_HOLDER, CHG_DRV_AICL_TYPE_DISABLE);
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("[%s] PowerPath Failed. ret:[%d]\n", __func__, ret);
			}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_USB, CHG_DRV_AICL_TYPE_DISABLE);
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("[%s] PowerPath Failed. ret:[%d]\n", __func__, ret);
			}
			chip->recovery_invalid = true;
			break;

		case CHG_DRV_CONTROL_STOP:
			ret = chg_drv_pmic_charge_stop(chip);
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("[%s] Charge Stop Failed. ret:[%d]\n", __func__, ret);
			}
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_HOLDER, CHG_DRV_AICL_TYPE_DISABLE);
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("[%s] Stop Failed. ret:[%d]\n", __func__, ret);
			}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_USB, CHG_DRV_AICL_TYPE_DISABLE);
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("[%s] Charge Stop Failed. ret:[%d]\n", __func__, ret);
			}
			chip->recovery_invalid = true;
			break;

		default:
			ret = -EINVAL;
			break;
	}

	chip->charge_ctrl = charge_ctrl;

	return ret;
}

/**
 * chg_drv_err_control
 *
 * @param	chip		: charger device info pointer
 *			err_type	: error type
 */
static void chg_drv_err_control(struct chg_drv_chip *chip, chg_drv_err_type err_type)
{
	chg_drv_err_ctrl err_ctrl = {0};

	CHG_DRV_DBGLOG("[%s] in err_type = %d\n", __func__, err_type);

	if (err_type >= CHG_DRV_ERR_NUM) {
		return;
	}

	chg_drv_stop_safety_timer(chip);
	chg_drv_stop_aicl_restart_monitor(chip);
	chg_drv_stop_full_timer(chip);

	memcpy(&err_ctrl, &err_ctrl_table[err_type], sizeof(chg_drv_err_ctrl));

	if (charging_mode && (err_type!=CHG_DRV_ERR_FGIC_INITIALIZING)) {
		if (err_ctrl.charge_ctrl == CHG_DRV_CONTROL_POWERPATH) {
			err_ctrl.charge_ctrl = CHG_DRV_CONTROL_STOP;
		}
		if ((err_ctrl.charge_ctrl == CHG_DRV_CONTROL_STOP)		   &&
			(err_ctrl.set_status == POWER_SUPPLY_STATUS_CHARGING)) {
			err_ctrl.set_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
	}

	chg_drv_batt_set_health(chip, err_ctrl.set_health);
	chg_drv_batt_set_status(chip, err_ctrl.set_status);
	chg_drv_charge_control(chip, err_ctrl.charge_ctrl);
}

/**
 * chg_drv_nego_usb_control
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static chg_drv_result_type chg_drv_nego_usb_control(struct chg_drv_chip *chip)
{
	chg_drv_result_type result = CHG_DRV_RESULT_CONTINUE;
	chg_drv_charge_type search_type = CHG_DRV_CHARGE_TYPE_UNKNOWN;
	chg_drv_device_info *dev_info = &chip->dev_usb;
	chg_drv_charge_type chg_type_sts = CHG_DRV_CHARGE_TYPE_UNKNOWN;
	chg_drv_standard_type std_type_sts = CHG_DRV_STANDARD_TYPE_UNKNOWN;
	int ret = 0;

	do {
		ret = chg_drv_get_charge_type(chip, &chg_type_sts, &std_type_sts);
		if (ret == 0) {
			if (chg_type_sts == CHG_DRV_CHARGE_TYPE_9V) {
				dev_info->nego_count[CHG_DRV_CHARGE_TYPE_5V] = 0;
				search_type = CHG_DRV_CHARGE_TYPE_9V;
			} else {
				dev_info->nego_count[CHG_DRV_CHARGE_TYPE_9V] = 0;
				search_type = CHG_DRV_CHARGE_TYPE_5V;
			}
		} else {
			dev_info->nego_count[CHG_DRV_CHARGE_TYPE_9V] = 0;
			search_type = CHG_DRV_CHARGE_TYPE_5V;
		}
		dev_info->nego_count[search_type]++;

		if (dev_info->nego_count[search_type] >= chip->dt_param.chg_nego_usb_detect_count) {
			dev_info->nego_total = 0;
			memset(dev_info->nego_count, 0, sizeof(dev_info->nego_count));
			dev_info->charge_type = search_type;
			dev_info->voltage_check = true;
			result = CHG_DRV_RESULT_SUCCESS;
			break;
		}

		dev_info->nego_total++;
		if (dev_info->nego_total >= chip->dt_param.chg_nego_usb_detect_max) {
			dev_info->nego_total = 0;
			memset(dev_info->nego_count, 0, sizeof(dev_info->nego_count));
			dev_info->charge_type = CHG_DRV_CHARGE_TYPE_5V;
			dev_info->voltage_check = true;
			result = CHG_DRV_RESULT_SUCCESS;
			break;
		}
	} while (0);

	return result;
}

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
/**
 * chg_drv_nego_holder_control
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	Processing result
 */
static chg_drv_result_type chg_drv_nego_holder_control(struct chg_drv_chip *chip)
{
	chg_drv_result_type result = CHG_DRV_RESULT_CONTINUE;
	chg_drv_charge_type search_type = CHG_DRV_CHARGE_TYPE_UNKNOWN;
	chg_drv_device_info *dev_info = &chip->dev_holder;
	union power_supply_propval pval = {0,};

	do {
		power_supply_get_property(chip->dc_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);

		if ((pval.intval >= chip->dt_param.holder9v_under_voltage_2nd) &&
			(pval.intval <= chip->dt_param.holder9v_over_voltage)) {
			dev_info->nego_count[CHG_DRV_CHARGE_TYPE_9V]++;
			search_type = CHG_DRV_CHARGE_TYPE_9V;
		} else {
			dev_info->nego_count[CHG_DRV_CHARGE_TYPE_9V] = 0;
		}
		if ((pval.intval >= chip->dt_param.holder5v_under_voltage) &&
			(pval.intval <= chip->dt_param.holder5v_over_voltage)) {
			dev_info->nego_count[CHG_DRV_CHARGE_TYPE_5V]++;
			search_type = CHG_DRV_CHARGE_TYPE_5V;
		} else {
			dev_info->nego_count[CHG_DRV_CHARGE_TYPE_5V] = 0;
		}

		if (search_type != CHG_DRV_CHARGE_TYPE_UNKNOWN) {
			if (dev_info->nego_count[search_type] >= chip->dt_param.chg_nego_holder_detect_count) {
				dev_info->nego_total = 0;
				memset(dev_info->nego_count, 0, sizeof(dev_info->nego_count));
				dev_info->charge_type = search_type;
				dev_info->voltage_check = true;
				result = CHG_DRV_RESULT_SUCCESS;
				break;
			}
		}

		dev_info->nego_total++;
		if (dev_info->nego_total >= chip->dt_param.chg_nego_holder_detect_max) {
			dev_info->nego_total = 0;
			memset(dev_info->nego_count, 0, sizeof(dev_info->nego_count));
			dev_info->charge_type = CHG_DRV_CHARGE_TYPE_UNKNOWN;
			dev_info->voltage_check = true;
			chip->err_info.chg_type_unknown = true;
			result = CHG_DRV_RESULT_FAILURE;
			break;
		}
	} while (0);

	return result;
}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

/**
 * chg_drv_charge_enable_control
 *
 * @param	chip	: charger device info pointer
 *			ilimit	:
 */
static void chg_drv_charge_enable_control(struct chg_drv_chip *chip, int ilimit)
{
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	if (chip->max_ilimit_value != ilimit) {
		CHG_DRV_DBGLOG("[%s] in chg_en:[%d]->[%d]\n",
									__func__, chip->max_ilimit_value, ilimit);
		if (ilimit != CHG_DRV_ILIMIT_DISABLE) {
			chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
		}
		if ((chip->max_ilimit_value == CHG_DRV_ILIMIT_DISABLE) ||
			(ilimit == CHG_DRV_ILIMIT_DISABLE)) {
			chg_drv_stop_monitor(chip);
			evt_data.monitor_info.type = CHG_DRV_MONITOR_NORMAL;
			chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);
		}
		chip->max_ilimit_value = ilimit;
	}
}

/**
 * chg_drv_notify_error_control
 *
 * @param	chip	: charger device info pointer
 *			error	:
 */
static void chg_drv_notify_error_control(struct chg_drv_chip *chip, chg_drv_notice_info notice_info)
{
	chg_drv_evt_data evt_data;

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	chg_drv_stop_monitor(chip);
	if (notice_info.type == CHG_DRV_NOTICE_OVP_NOTIFY_ERROR) {
		if (notice_info.ovp_detect == OEM_CHG_ERROR_NONE){
			chip->safety_err_info.ovp_detect = false;
		} else {
			chip->safety_err_info.ovp_detect = true;
		}
	}

	if (notice_info.type == CHG_DRV_NOTICE_USBHOT_NOTIFY_ERROR) {
		if (notice_info.usb_hot_detect == OEM_CHG_ERROR_NONE){
			chip->safety_err_info.usb_port_failure = false;
		} else {
			chip->safety_err_info.usb_port_failure = true;
		}
	}

	if (notice_info.type == CHG_DRV_NOTICE_PDO_ERROR) {
		if (notice_info.pdo_error_detect == OEM_CHG_ERROR_NONE){
			chip->safety_err_info.pdo_profile_error = false;
		} else {
			chip->safety_err_info.pdo_profile_error = true;
		}
	}

	evt_data.monitor_info.type = CHG_DRV_MONITOR_NORMAL;
	chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);
}

/**
 * chg_drv_change_current_control
 *
 * @param	chip		: charger device info pointer
 *			change_info	:
 */
static void chg_drv_change_current_control(struct chg_drv_chip *chip,
										  chg_drv_change_info *change_info)
{
	chg_drv_device_info *dev_info;
	bool update_flag = true;

	if (change_info->device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		dev_info = &chip->dev_holder;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		dev_info = &chip->dev_usb;
		if (CHG_DRV_IS_HOLDER_CHARGE(chip)) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			update_flag = false;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		}
	}

	dev_info->max_current = change_info->max_current;

	if (dev_info->voltage_check && update_flag) {
		chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
	}
}

/**
 * chg_drv_recovery_control
 *
 * @param	chip	: charger device info pointer
 *			chg_sts	: charge status
 *
 * @retval	error type
 */
static chg_drv_err_type chg_drv_recovery_control(struct chg_drv_chip *chip, u8 chg_sts)
{
	int ret = 0;
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	union power_supply_propval pval = {0, };
	u8 icl_sts = 0;

	do {
		if ((chg_sts & BATTERY_CHARGER_STATUS_MASK) <= TAPER_CHARGE) {
			chip->recovery_count = 0;
			break;
		}

		if (chip->recovery_invalid) {
			CHG_DRV_DBGLOG("[%s] recovery invalid.\n", __func__);
			break;
		}

		chip->recovery_count++;

		/* recovery */
		CHG_DRV_INFOLOG("[%s] [CHGDRVERR] charging recovery. sts[%d] cnt[%d]\n",
							 __func__, chg_sts, chip->recovery_count);

		pval.intval = 1;
		ret = power_supply_set_property(chip->batt_psy,
					POWER_SUPPLY_PROP_FORCE_RECHARGE, &pval);
	} while (0);

	do {
		chg_drv_reg_read(CHG_DRV_CMD_SCHG_DCDC_AICL_ICL_STATUS, &icl_sts);
		if (icl_sts < 0x06) {
			CHG_DRV_INFOLOG("[%s] [CHGDRVERR] restart aicl. icl_sts[0x%02X]\n", __func__, icl_sts);
			ret = chg_drv_reg_write_bit(CHG_DRV_CMD_SCHG_MISC_AICL_CMD_REG,
											BIT(1), CHG_DRV_SET_HIGH);
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("SCHG_MISC_AICL_CMD Failed. ret:[%d]\n", ret);
				break;
			}
		}
	} while (0);

	return err_type;
}

/**
 * chg_drv_power_supply_update_all
 *
 * @param	chip	: charger device info pointer
 */
static void chg_drv_power_supply_update_all(struct chg_drv_chip *chip)
{
	if (likely(chip != NULL)) {
		CHG_DRV_RECLOG("[%s] update online chg_src=0x%04x\n",
						__func__, (unsigned int)chip->charger_info.chg_source);
		power_supply_changed(chip->oem_usb_psy);
		power_supply_changed(chip->oem_mains_psy);
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		power_supply_changed(chip->oem_holder_psy);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	}
}

static const chg_drv_connect_ctrl connect_ctrl_table[CHG_DRV_CONNECT_STATE_NUM][CHG_DRV_CONNECT_EVENT_NUM] = {
	/* CHG_DRV_CONNECT_STATE_NONE */ 			{
		/* CHG_DRV_CONNECT_EVENT_USB_IN		*/		{ CHG_DRV_CONNECT_STATE_USB,		CHG_DRV_CONTROL_USB_CHARGE		},
		/* CHG_DRV_CONNECT_EVENT_USB_OUT	*/		{ CHG_DRV_CONNECT_STATE_NOCHANGE,	CHG_DRV_CONTROL_NONE			},
		/* CHG_DRV_CONNECT_EVENT_HOLDER_IN	*/		{ CHG_DRV_CONNECT_STATE_HOLDER,		CHG_DRV_CONTROL_HOLDER_CHARGE	},
		/* CHG_DRV_CONNECT_EVENT_HOLDER_OUT	*/		{ CHG_DRV_CONNECT_STATE_NOCHANGE,	CHG_DRV_CONTROL_NONE			},
	/* CHG_DRV_CONNECT_STATE_USB */				} , {
		/* CHG_DRV_CONNECT_EVENT_USB_IN		*/		{ CHG_DRV_CONNECT_STATE_NOCHANGE,	CHG_DRV_CONTROL_USB_CHARGE		},
		/* CHG_DRV_CONNECT_EVENT_USB_OUT	*/		{ CHG_DRV_CONNECT_STATE_NONE,		CHG_DRV_CONTROL_STOP			},
		/* CHG_DRV_CONNECT_EVENT_HOLDER_IN	*/		{ CHG_DRV_CONNECT_STATE_BOTH,		CHG_DRV_CONTROL_HOLDER_CHARGE	},
		/* CHG_DRV_CONNECT_EVENT_HOLDER_OUT	*/		{ CHG_DRV_CONNECT_STATE_NOCHANGE,	CHG_DRV_CONTROL_NONE			},
	/* CHG_DRV_CONNECT_STATE_HOLDER */			} , {
		/* CHG_DRV_CONNECT_EVENT_USB_IN		*/		{ CHG_DRV_CONNECT_STATE_BOTH,		CHG_DRV_CONTROL_NONE			},
		/* CHG_DRV_CONNECT_EVENT_USB_OUT	*/		{ CHG_DRV_CONNECT_STATE_NOCHANGE,	CHG_DRV_CONTROL_NONE			},
		/* CHG_DRV_CONNECT_EVENT_HOLDER_IN	*/		{ CHG_DRV_CONNECT_STATE_NOCHANGE,	CHG_DRV_CONTROL_NONE			},
		/* CHG_DRV_CONNECT_EVENT_HOLDER_OUT	*/		{ CHG_DRV_CONNECT_STATE_NONE,		CHG_DRV_CONTROL_STOP			},
	/* CHG_DRV_CONNECT_STATE_BOTH */			} , {
		/* CHG_DRV_CONNECT_EVENT_USB_IN		*/		{ CHG_DRV_CONNECT_STATE_NOCHANGE,	CHG_DRV_CONTROL_NONE			},
		/* CHG_DRV_CONNECT_EVENT_USB_OUT	*/		{ CHG_DRV_CONNECT_STATE_HOLDER,		CHG_DRV_CONTROL_NONE			},
		/* CHG_DRV_CONNECT_EVENT_HOLDER_IN	*/		{ CHG_DRV_CONNECT_STATE_NOCHANGE,	CHG_DRV_CONTROL_NONE			},
		/* CHG_DRV_CONNECT_EVENT_HOLDER_OUT	*/		{ CHG_DRV_CONNECT_STATE_USB,		CHG_DRV_CONTROL_USB_CHARGE		},
												}
};

/**
 * chg_drv_connect_info_update
 *
 * @param	chip			: charger device info pointer
 *			connect_info	: connect info pointer
 *
 * @retval	charge control type
 */
static chg_drv_ctrl_type chg_drv_connect_info_update(struct chg_drv_chip *chip,
													   chg_drv_connect_info *connect_info)
{
	const chg_drv_connect_ctrl *connect_ctrl;
	unsigned long new_source = 0;
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	unsigned int current_backup;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	int ret;
	u8 val;
	union power_supply_propval pval = {0,};

	new_source = CHG_DRV_SOURCE(connect_info->chg_source);

	spin_lock(&oem_chg_drv_info_lock);
	if (connect_info->max_current != CHG_DRV_CURRENT_OFF) {
		ret = power_supply_get_property(chip->usb_psy, POWER_SUPPLY_PROP_PRESENT, &pval);
		if (unlikely(ret < 0)) {
			CHG_DRV_ERRLOG("[%s] Get Prop Present Failed. ret:[%d]\n", __func__, ret);
		} else if (pval.intval == 0) {
			connect_info->event = CHG_DRV_CONNECT_EVENT_USB_OUT;
			charger_src &= ~new_source;
			if (new_source == CHG_DRV_SOURCE_APSD) {
				charger_apsd_src = 0;
			}
		} else {
			if ((new_source & CHG_DRV_SOURCE_USB_PORT) != 0) {
				charger_src &= ~CHG_DRV_SOURCE_USB_PORT;
			}
			charger_src |= new_source;
			if (new_source == CHG_DRV_SOURCE_APSD) {
				ret =  chg_drv_reg_read(CHG_DRV_CMD_SCHG_USB_APSD_RESULT_STATUS_REG, &val);
				if (likely(ret == 0)) {
					val &= APSD_RESULT_STATUS_MASK;
					chip->apsd_type = val;			
					if (val & SDP_CHARGER_BIT) {
						/* USB */
						charger_apsd_src = CHG_DRV_SOURCE_USB;
						connect_info->max_current = CHG_DRV_CURRENT_500;
					} else if ((val & CDP_CHARGER_BIT) ||
							   (val & DCP_CHARGER_BIT) ||
							   (val & QC_2P0_BIT) ||
							   (val & QC_3P0_BIT)) {
						/* AC */
						charger_apsd_src = CHG_DRV_SOURCE_AC;
						connect_info->max_current = CHG_DRV_CURRENT_1500;
					} else {
						/* Float / Other */
						charger_apsd_src = CHG_DRV_SOURCE_AC;
						connect_info->max_current = CHG_DRV_CURRENT_500;
					}
					CHG_DRV_RECLOG("[%s] APSD result:0x%x, mA=%d\n",
										__func__, val, connect_info->max_current);
				}
			}
		}
	} else {
		charger_src &= ~new_source;
		if (new_source == CHG_DRV_SOURCE_APSD) {
				charger_apsd_src = 0;
		}
	}
	chip->charger_info.chg_source = charger_src;
	spin_unlock(&oem_chg_drv_info_lock);

	connect_ctrl = &connect_ctrl_table[chip->connect_state][connect_info->event];

	switch(connect_info->event) {
		case CHG_DRV_CONNECT_EVENT_USB_IN:
			ret = chg_drv_get_longevity_chg_mode(chip);
			if (unlikely(ret < 0)) {
				CHG_DRV_ERRLOG("[%s] get_longevity_charge_mode failed. ret = %d\n", __func__, ret);
			}
			chip->dev_usb.max_current = connect_info->max_current;
			chg_drv_start_negotiation_timer(chip, CHG_DRV_DEVICE_TYPE_USB);
			break;
		case CHG_DRV_CONNECT_EVENT_USB_OUT:
			chg_drv_terminate_usb(chip);
			chg_drv_pmic_slight_charge(chip, CHG_DRV_DEVICE_TYPE_USB);
			break;
		case CHG_DRV_CONNECT_EVENT_HOLDER_IN:
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			ret = chg_drv_get_longevity_chg_mode(chip);
			if (unlikely(ret < 0)) {
				CHG_DRV_ERRLOG("[%s] get_longevity_charge_mode failed. ret = %d\n", __func__, ret);
			}
			chip->dev_holder.max_current = connect_info->max_current;
			chg_drv_start_negotiation_timer(chip, CHG_DRV_DEVICE_TYPE_HOLDER);
			break;
		case CHG_DRV_CONNECT_EVENT_HOLDER_OUT:
			chg_drv_terminate_holder(chip);
			chg_drv_pmic_slight_charge(chip, CHG_DRV_DEVICE_TYPE_HOLDER);
			if (connect_ctrl->next_state == CHG_DRV_CONNECT_STATE_USB) {
				current_backup = chip->dev_usb.max_current;
				chg_drv_terminate_usb(chip);
				chip->dev_usb.max_current = current_backup;
				chg_drv_pmic_slight_charge(chip, CHG_DRV_DEVICE_TYPE_USB);
				chg_drv_start_negotiation_timer(chip, CHG_DRV_DEVICE_TYPE_USB);
			}
			break;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		default:
			break;
	}

	switch(connect_ctrl->next_state) {
		case CHG_DRV_CONNECT_STATE_USB:
			chip->charger_info.chg_current = chip->dev_usb.max_current;
			chip->charge_type = chip->dev_usb.charge_type;
			chg_drv_pmic_set_preferential_device(chip, CHG_DRV_DEVICE_TYPE_USB);
			break;
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		case CHG_DRV_CONNECT_STATE_HOLDER:
		case CHG_DRV_CONNECT_STATE_BOTH:
			chip->charger_info.chg_current = chip->dev_holder.max_current;
			chip->charge_type = chip->dev_holder.charge_type;
			chg_drv_pmic_set_preferential_device(chip, CHG_DRV_DEVICE_TYPE_HOLDER);
			break;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		case CHG_DRV_CONNECT_STATE_NONE:
		default:
			break;
	}

	if (connect_ctrl->next_state != CHG_DRV_CONNECT_STATE_NOCHANGE) {
		if (chip->connect_state != connect_ctrl->next_state) {
			CHG_DRV_INFOLOG("[%s] charger connect state %d -> %d\n",
							__func__, chip->connect_state, connect_ctrl->next_state);
			chip->connect_state = connect_ctrl->next_state;
		}
	}

	if (connect_ctrl->charge_ctrl != CHG_DRV_CONTROL_NONE) {
		chg_drv_power_supply_update_all(chip);
	}

	return connect_ctrl->charge_ctrl;
}

/**
 * chg_drv_func_charging_monitor
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_charging_monitor(struct chg_drv_chip *chip,
														  chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	chg_drv_monitor_param param = {0};
	chg_drv_err_type usb_err_type = CHG_DRV_ERR_NON;
	chg_drv_err_type holder_err_type = CHG_DRV_ERR_NON;
	chg_drv_result_type usb_result = CHG_DRV_RESULT_SUCCESS;
	chg_drv_result_type holder_result = CHG_DRV_RESULT_SUCCESS;
	union power_supply_propval pval = {0,};
	int ret = 0;
	int ibat_ma = 0;
	bool icl_update = false;
	chg_drv_evt_data evt_data;					

	CHG_DRV_DBGLOG("[%s] in type:[%d]\n", __func__, data->monitor_info.type);

	switch (data->monitor_info.type) {
		case CHG_DRV_MONITOR_NORMAL:
			chg_drv_get_parameters(chip, &param);
			err_type = chg_drv_check_parameters(chip, &param);

			switch (err_type) {
				case CHG_DRV_ERR_NON:
					if (likely(chip->bms_psy)) {
						ret = power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_CURRENT_NOW, &pval);
					}
					if (likely(ret == 0)) {
						ibat_ma = pval.intval / 1000;
					}
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
					CHG_DRV_INFOLOG("[%s] usb:[%d]mV dc:[%d]mV sts:[0x%02x] current:[%d]mA battvolt:[%d]uV\n",
						__func__, (param.usb_in_volt/1000), (param.dc_in_volt/1000),
						param.chg_sts, ibat_ma, param.batt_volt);
#else
					CHG_DRV_INFOLOG("[%s] usb:[%d]mV sts:[0x%02x] current:[%d]mA battvolt:[%d]uV\n",
						__func__, (param.usb_in_volt/1000), param.chg_sts, ibat_ma, param.batt_volt);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
					err_type = chg_drv_recovery_control(chip, param.chg_sts);
					if (err_type != CHG_DRV_ERR_NON) {
						chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
						next_state = CHG_DRV_STATE_ERROR;
						break;
					}
					if (chip->longevity_charge_flag) {
						if (param.soc <= chip->dt_param.chg_rechg_soc_longevity_lv2) {
							chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_CHARGING);
						}
						if (param.soc >= chip->dt_param.chg_full_soc_longevity_lv2) {
							chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_FULL);
						}
						if (param.soc >= chip->dt_param.chg_full_soc_longevity_lv1) {
							CHG_DRV_INFOLOG("[%s] battery full.\n", __func__);
							chg_drv_stop_safety_timer(chip);
							chg_drv_stop_aicl_restart_monitor(chip);
							chg_drv_charge_control(chip, CHG_DRV_CONTROL_POWERPATH);
							chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_FULL);
							next_state = CHG_DRV_STATE_FULL;
						} else {
							chg_drv_charge_control(chip, CHG_DRV_CONTROL_CHARGE_START);
						}
					} else {
						if (param.soc <= chip->dt_param.chg_rechg_soc_lv2) {
							chg_drv_stop_full_timer(chip);
							chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_CHARGING);
						} else if (param.soc >= chip->dt_param.chg_full_soc_lv1) {
							chg_drv_start_full_timer1(chip);
						}
						if (param.soc >= chip->dt_param.chg_full_soc_lv2 && charge_is_battery_full(chip)) {
							CHG_DRV_INFOLOG("[%s] battery full.\n", __func__);
							chg_drv_stop_full_timer(chip);
							chg_drv_stop_safety_timer(chip);
							chg_drv_stop_aicl_restart_monitor(chip);
							chg_drv_charge_control(chip, CHG_DRV_CONTROL_POWERPATH);
							chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_FULL);
							next_state = CHG_DRV_STATE_FULL;
						} else {
							chg_drv_charge_control(chip, CHG_DRV_CONTROL_CHARGE_START);
						}
					}
					break;

				default:
					chg_drv_err_control(chip, err_type);
					next_state = CHG_DRV_STATE_ERROR;
					break;
			}

			chg_drv_start_monitor(chip);
			break;

		case CHG_DRV_MONITOR_ADAPTER_VOLTAGE:
			/* check supply voltage */
			chg_drv_get_parameters(chip, &param);

			if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_HOLDER) != 0) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				holder_result = chg_drv_watch_holder_adapter_voltage(chip, &holder_err_type, param.dc_in_volt);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			}
			if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_USB_PORT) != 0) {
				usb_result = chg_drv_watch_usb_adapter_voltage(chip, &usb_err_type, param.usb_in_volt);
			}

			if (holder_result == CHG_DRV_RESULT_FAILURE) {
				/* holder error */
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				if (holder_err_type == CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR) {
					chg_drv_check_weak_adapter(chip);
					if (chip->dev_holder.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
						holder_err_type = CHG_DRV_ERR_OTHER_ERR;
					}
				}
				chg_drv_err_control(chip, holder_err_type);
				next_state = CHG_DRV_STATE_ERROR;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			} else if (usb_result == CHG_DRV_RESULT_FAILURE) {
				/* usb error */
				if (usb_err_type == CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR) {
					chg_drv_check_weak_adapter(chip);
					if (chip->dev_usb.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
						usb_err_type = CHG_DRV_ERR_OTHER_ERR;
					}
				}
				chg_drv_err_control(chip, usb_err_type);
				next_state = CHG_DRV_STATE_ERROR;
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
			} else if ((usb_result == CHG_DRV_RESULT_VOLTAGE_ERROR) ||
					   (holder_result == CHG_DRV_RESULT_VOLTAGE_ERROR)) {
				/* voltage error */
				if ((holder_err_type == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR) ||
					(usb_err_type == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR)) {
					chg_drv_charge_control(chip, CHG_DRV_CONTROL_STOP);
				}
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_750MS);
			} else if ((usb_result == CHG_DRV_RESULT_RETRY) ||
					   (holder_result == CHG_DRV_RESULT_RETRY)) {
				chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
			} else {
				chg_drv_charge_control(chip, CHG_DRV_CONTROL_CHARGE_START);
				/* soft icl */
				if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_HOLDER) != 0) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
					icl_update |= chg_drv_soft_icl_holder(chip);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
				}
				if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_USB_PORT) != 0) {
					icl_update |= chg_drv_soft_icl_usb(chip);
				}
				if (icl_update) {
					chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
				}
				if ((param.typec_sm_sts == 0x02) ||
					(param.typec_sm_sts == 0x2C)) {
					chip->err_info.unspec_cable_err = true;			
					memset(&evt_data, 0, sizeof(chg_drv_evt_data));
					evt_data.monitor_info.type = CHG_DRV_MONITOR_NORMAL;
					chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);
				}
				if (!chg_drv_check_reverse_boost(chip)) {
					chg_drv_check_apsd_result(chip);		
				}
				/* success */
				if ((param.chg_sts & 0x06) == 0x00) {
					chg_drv_charge_control(chip, CHG_DRV_CONTROL_RECHARGE);
				}
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
			}
			break;

		case CHG_DRV_MONITOR_AICL:
			chg_drv_aicl_restart_check(chip);
			chg_drv_start_aicl_restart_monitor(chip);
			break;

		default:
			break;
	}

	return next_state;
}

/**
 * chg_drv_func_full_monitor
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_full_monitor(struct chg_drv_chip *chip,
													  chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	chg_drv_monitor_param param = {0};
	int ret = 0;
	bool icl_update = false;
	int rechg_soc = 0;
	chg_drv_err_type usb_err_type = CHG_DRV_ERR_NON;
	chg_drv_err_type holder_err_type = CHG_DRV_ERR_NON;
	chg_drv_result_type usb_result = CHG_DRV_RESULT_SUCCESS;
	chg_drv_result_type holder_result = CHG_DRV_RESULT_SUCCESS;
	chg_drv_evt_data evt_data;					

	CHG_DRV_DBGLOG("[%s] in type:[%d]\n", __func__, data->monitor_info.type);

	switch (data->monitor_info.type) {
		case CHG_DRV_MONITOR_NORMAL:
			chg_drv_get_parameters(chip, &param);
			err_type = chg_drv_check_parameters(chip, &param);

			switch (err_type) {
				case CHG_DRV_ERR_NON:
					if (chip->longevity_charge_flag) {
						rechg_soc = chip->dt_param.chg_rechg_soc_longevity_lv1;
					} else {
						rechg_soc = chip->dt_param.chg_rechg_soc_lv1;
					}
					if (param.soc <= rechg_soc) {
						CHG_DRV_RECLOG("[%s] soc changed from FULL to %d, start recharging\n",
											__func__, rechg_soc);
						ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_RECHARGE);
						if (ret == 0) {
							chg_drv_start_safety_timer(chip);
							chg_drv_start_aicl_restart_monitor(chip);
							chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
							/* keep POWER_SUPPLY_STATUS_FULL status */
							next_state = CHG_DRV_STATE_CHARGING;
						} else {
							chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
							next_state = CHG_DRV_STATE_ERROR;
						}
					}
					break;

				default:
					chg_drv_err_control(chip, err_type);
					next_state = CHG_DRV_STATE_ERROR;
					break;
			}

			chg_drv_start_monitor(chip);
			break;

		case CHG_DRV_MONITOR_ADAPTER_VOLTAGE:
			/* check supply voltage */
			chg_drv_get_parameters(chip, &param);

			if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_HOLDER) != 0) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				holder_result = chg_drv_watch_holder_adapter_voltage(chip, &holder_err_type, param.dc_in_volt);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			}
			if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_USB_PORT) != 0) {
				usb_result = chg_drv_watch_usb_adapter_voltage(chip, &usb_err_type, param.usb_in_volt);
			}

			if (holder_result == CHG_DRV_RESULT_FAILURE) {
				/* holder error */
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				if (holder_err_type == CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR) {
					chg_drv_check_weak_adapter(chip);
					if (chip->dev_holder.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
						holder_err_type = CHG_DRV_ERR_OTHER_ERR;
					}
				}
				chg_drv_err_control(chip, holder_err_type);
				next_state = CHG_DRV_STATE_ERROR;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			} else if (usb_result == CHG_DRV_RESULT_FAILURE) {
				/* usb error */
				if (usb_err_type == CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR) {
					chg_drv_check_weak_adapter(chip);
					if (chip->dev_usb.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
						usb_err_type = CHG_DRV_ERR_OTHER_ERR;
					}
				}
				chg_drv_err_control(chip, usb_err_type);
				next_state = CHG_DRV_STATE_ERROR;
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
			} else if ((usb_result == CHG_DRV_RESULT_VOLTAGE_ERROR) ||
					   (holder_result == CHG_DRV_RESULT_VOLTAGE_ERROR)) {
				/* voltage error */
				if ((holder_err_type == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR) ||
					(usb_err_type == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR)) {
					chg_drv_charge_control(chip, CHG_DRV_CONTROL_STOP);
				}
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_750MS);
			} else if ((usb_result == CHG_DRV_RESULT_RETRY) ||
					   (holder_result == CHG_DRV_RESULT_RETRY)) {
				chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
			} else {
				chg_drv_charge_control(chip, CHG_DRV_CONTROL_POWERPATH);
				/* soft icl */
				if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_HOLDER) != 0) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
					icl_update |= chg_drv_soft_icl_holder(chip);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
				}
				if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_USB_PORT) != 0) {
					icl_update |= chg_drv_soft_icl_usb(chip);
				}
				if (icl_update) {
					chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
				}
				if ((param.typec_sm_sts == 0x02) ||
					(param.typec_sm_sts == 0x2C)) {
					chip->err_info.unspec_cable_err = true;			
					memset(&evt_data, 0, sizeof(chg_drv_evt_data));
					evt_data.monitor_info.type = CHG_DRV_MONITOR_NORMAL;
					chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);
				}
				if (!chg_drv_check_reverse_boost(chip)) {
					chg_drv_check_apsd_result(chip);		
				}
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
			}
			break;

		case CHG_DRV_MONITOR_AICL:
			break;

		default:
			break;
	}

	return next_state;
}

/**
 * chg_drv_func_error_monitor
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_error_monitor(struct chg_drv_chip *chip, chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	chg_drv_monitor_param param = {0};
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	int ret = 0;
	bool icl_update = false;
	chg_drv_err_type usb_err_type = CHG_DRV_ERR_NON;
	chg_drv_err_type holder_err_type = CHG_DRV_ERR_NON;
	chg_drv_result_type usb_result = CHG_DRV_RESULT_SUCCESS;
	chg_drv_result_type holder_result = CHG_DRV_RESULT_SUCCESS;
	int full_soc = 0;
	chg_drv_evt_data evt_data;					

	CHG_DRV_DBGLOG("[%s] in type:[%d]\n", __func__, data->monitor_info.type);

	switch (data->monitor_info.type) {
		case CHG_DRV_MONITOR_NORMAL:
			chg_drv_get_parameters(chip, &param);
			err_type = chg_drv_check_parameters(chip, &param);

			switch (err_type) {
				case CHG_DRV_ERR_NON:
					if (chip->longevity_charge_flag) {
						full_soc = chip->dt_param.chg_full_soc_longevity_lv1;
					} else {
						full_soc = chip->dt_param.chg_full_soc_lv1;
					}
					if (param.soc > full_soc) {
						ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_POWERPATH);
						if (likely(ret == 0)) {
							chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_CHARGING);
							chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_GOOD);		
							next_state = CHG_DRV_STATE_FULL;
						} else {
							chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
							next_state = CHG_DRV_STATE_ERROR;
						}
					} else {
						ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_RECHARGE);
						if (likely(ret == 0)) {
							chg_drv_start_safety_timer(chip);
							chg_drv_start_aicl_restart_monitor(chip);
							chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
							chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_CHARGING);
							chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_GOOD);
							next_state = CHG_DRV_STATE_CHARGING;
						}
					}
					break;

				default:
					chg_drv_err_control(chip, err_type);
					break;
			}

			chg_drv_start_monitor(chip);
			break;

		case CHG_DRV_MONITOR_ADAPTER_VOLTAGE:
			/* check supply voltage */
			chg_drv_get_parameters(chip, &param);

			if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_HOLDER) != 0) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				holder_result = chg_drv_watch_holder_adapter_voltage(chip, &holder_err_type, param.dc_in_volt);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			}
			if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_USB_PORT) != 0) {
				usb_result = chg_drv_watch_usb_adapter_voltage(chip, &usb_err_type, param.usb_in_volt);
			}

			if (holder_result == CHG_DRV_RESULT_FAILURE) {
				/* holder error */
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				if (holder_err_type == CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR) {
					chg_drv_check_weak_adapter(chip);
					if (chip->dev_holder.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
						holder_err_type = CHG_DRV_ERR_OTHER_ERR;
					}
				}
				chg_drv_err_control(chip, holder_err_type);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			} else if (usb_result == CHG_DRV_RESULT_FAILURE) {
				/* usb error */
				if (usb_err_type == CHG_DRV_ERR_UNDER_SUPPLY_VOLTAGE_ERR) {
					chg_drv_check_weak_adapter(chip);
					if (chip->dev_usb.charge_type == CHG_DRV_CHARGE_TYPE_9V) {
						usb_err_type = CHG_DRV_ERR_OTHER_ERR;
					}
				}
				chg_drv_err_control(chip, usb_err_type);
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
			} else if ((usb_result == CHG_DRV_RESULT_VOLTAGE_ERROR) ||
					   (holder_result == CHG_DRV_RESULT_VOLTAGE_ERROR)) {
				/* voltage error */
				if ((holder_err_type == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR) ||
					(usb_err_type == CHG_DRV_ERR_OVER_SUPPLY_VOLTAGE_ERR)) {
					chg_drv_charge_control(chip, CHG_DRV_CONTROL_STOP);
				}
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_750MS);
			} else if ((usb_result == CHG_DRV_RESULT_RETRY) ||
					   (holder_result == CHG_DRV_RESULT_RETRY)) {
				chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
				chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
			} else {
				if (chip->charge_ctrl != CHG_DRV_CONTROL_STOP) {
					/* soft icl */
					if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_HOLDER) != 0) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
						icl_update |= chg_drv_soft_icl_holder(chip);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
					}
					if ((chip->charger_info.chg_source & CHG_DRV_SOURCE_USB_PORT) != 0) {
						icl_update |= chg_drv_soft_icl_usb(chip);
					}
					if (icl_update) {
						chg_drv_event_req(chip, CHG_DRV_EVENT_PARAM_UPDATE, NULL);
					}
					if (!chg_drv_check_reverse_boost(chip)) {
						chg_drv_check_apsd_result(chip);		
					}
				}
				if ((param.typec_sm_sts != 0x02) &&
					(param.typec_sm_sts != 0x2C)) {
					chip->err_info.unspec_cable_err = false;		
					memset(&evt_data, 0, sizeof(chg_drv_evt_data));
					evt_data.monitor_info.type = CHG_DRV_MONITOR_NORMAL;
					chg_drv_event_req(chip, CHG_DRV_EVENT_MONITOR_PARAM, &evt_data);
				}
			}
			chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
			break;

		case CHG_DRV_MONITOR_AICL:
			break;

		default:
			break;
	}

	return next_state;
}

/**
 * chg_drv_func_idle_info_notice
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_idle_info_notice(struct chg_drv_chip *chip, chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	switch (data->notice_info.type) {
		case CHG_DRV_NOTICE_OVP_NOTIFY_ERROR:
		case CHG_DRV_NOTICE_USBHOT_NOTIFY_ERROR:
		case CHG_DRV_NOTICE_PDO_ERROR:

			if (data->notice_info.type == CHG_DRV_NOTICE_PDO_ERROR) {
				if (data->notice_info.pdo_error_detect == OEM_CHG_ERROR_NONE){
					chip->safety_err_info.pdo_profile_error = false;
				} else {
					chip->safety_err_info.pdo_profile_error = true;
				}
			}

			if (data->notice_info.type == CHG_DRV_NOTICE_USBHOT_NOTIFY_ERROR) {
				if (data->notice_info.usb_hot_detect == OEM_CHG_ERROR_NONE) {
					chip->safety_err_info.usb_port_failure = false;
				} else {
					chip->safety_err_info.usb_port_failure = true;
				}
			}

			if (data->notice_info.type == CHG_DRV_NOTICE_OVP_NOTIFY_ERROR) {
				if (data->notice_info.ovp_detect == OEM_CHG_ERROR_NONE) {
					chip->safety_err_info.ovp_detect = false;
				} else {
					chip->safety_err_info.ovp_detect = true;
				}
			}

			if (chip->safety_err_info.usb_port_failure == true) {
				chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_USB_HOT);
				chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_NOT_CHARGING);
			} else if (chip->safety_err_info.ovp_detect == true) {
				chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_OVER_SUPPLY_VOLTAGE);
				chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_NOT_CHARGING);
			} else if (chip->safety_err_info.pdo_profile_error == true) {
				chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_OTHER_ERROR);
				chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_NOT_CHARGING);
			} else {
				chg_drv_batt_set_health(chip, POWER_SUPPLY_HEALTH_GOOD);
				chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_DISCHARGING);
			}
			break;

		case CHG_DRV_NOTICE_CHG_ENABLE:
			chip->max_ilimit_value = data->notice_info.ilimit;
			break;

		default:
			break;
	}

	return next_state;
}

/**
 * chg_drv_func_info_notice
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_info_notice(struct chg_drv_chip *chip,
													 chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;

	CHG_DRV_DBGLOG("[%s] in type = %d\n", __func__, data->notice_info.type);

	switch (data->notice_info.type) {
		case CHG_DRV_NOTICE_OVP_NOTIFY_ERROR:
		case CHG_DRV_NOTICE_USBHOT_NOTIFY_ERROR:
		case CHG_DRV_NOTICE_PDO_ERROR:
			chg_drv_notify_error_control(chip, data->notice_info);
			break;

		case CHG_DRV_NOTICE_CHG_ENABLE:
			chg_drv_charge_enable_control(chip, data->notice_info.ilimit);
			break;

		case CHG_DRV_NOTICE_CHANGE_CURRENT:
			chg_drv_change_current_control(chip, &data->notice_info.change_info);
			break;

		default:
			break;
	}

	return next_state;
}

/**
 * chg_drv_func_idle_control
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_idle_control(struct chg_drv_chip *chip,
													  chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	chg_drv_ctrl_type charge_ctrl;
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	chg_drv_monitor_param param = {0};
	int ret;
	int full_soc = 0;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	do {
		if (chip->charge_mode == CHG_DRV_CHARGE_MODE_NOT_CHARGE) {
			chg_drv_charge_control(chip, CHG_DRV_CONTROL_STOP);
			break;
		}

		charge_ctrl = chg_drv_connect_info_update(chip, &data->connect_info);

		switch (charge_ctrl) {
			case CHG_DRV_CONTROL_STOP:
				chg_drv_charge_control(chip, CHG_DRV_CONTROL_STOP);
				chg_drv_terminate(chip, false);
				break;

			case CHG_DRV_CONTROL_USB_CHARGE:
			case CHG_DRV_CONTROL_HOLDER_CHARGE:
				chg_drv_get_parameters(chip, &param);
				err_type = chg_drv_check_parameters(chip, &param);
				if (err_type == CHG_DRV_ERR_NON) {
					if (chip->longevity_charge_flag) {
						full_soc = chip->dt_param.chg_full_soc_longevity_lv1;
					} else {
						full_soc = chip->dt_param.chg_full_soc_lv1;
					}
					if (param.soc > full_soc) {
						ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_POWERPATH);
						if (likely(ret == 0)) {
							chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_CHARGING);
							next_state = CHG_DRV_STATE_FULL;
						} else {
							chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
							next_state = CHG_DRV_STATE_ERROR;
						}
					} else {
						ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_PRECHARGE);
						if (likely(ret == 0)) {
							chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_CHARGING);
							chg_drv_start_safety_timer(chip);
							chg_drv_start_aicl_restart_monitor(chip);
							next_state = CHG_DRV_STATE_CHARGING;
						} else {
							chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
							next_state = CHG_DRV_STATE_ERROR;
						}
					}
				} else {
					chg_drv_err_control(chip, err_type);
					next_state = CHG_DRV_STATE_ERROR;
				}
				chg_drv_start_monitor(chip);
				chg_drv_start_watchdog_timer(chip);
				break;

			default:
				break;
		}
	} while (0);

	return next_state;
}

/**
 * chg_drv_func_charging_control
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_charging_control(struct chg_drv_chip *chip,
														  chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	chg_drv_ctrl_type charge_ctrl;
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	chg_drv_monitor_param param = {0};
	int ret = 0;

	charge_ctrl = chg_drv_connect_info_update(chip, &data->connect_info);

	switch (charge_ctrl) {
		case CHG_DRV_CONTROL_STOP:
			chg_drv_charge_control(chip, CHG_DRV_CONTROL_STOP);
			chg_drv_terminate(chip, false);
			next_state = CHG_DRV_STATE_IDLE;
			break;

		case CHG_DRV_CONTROL_USB_CHARGE:
		case CHG_DRV_CONTROL_HOLDER_CHARGE:
			chg_drv_stop_adapter_volt_monitor(chip);
			chg_drv_stop_monitor(chip);
			chg_drv_stop_full_timer(chip);
			chg_drv_stop_safety_timer(chip);
			chg_drv_stop_aicl_restart_monitor(chip);

			chg_drv_get_parameters(chip, &param);
			err_type = chg_drv_check_parameters(chip, &param);

			if (err_type == CHG_DRV_ERR_NON) {
				ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_PRECHARGE);
				if (likely(ret == 0)) {
					chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_CHARGING);
					chg_drv_start_safety_timer(chip);
					chg_drv_start_aicl_restart_monitor(chip);
				} else {
					chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
					chg_drv_start_monitor(chip);
					next_state = CHG_DRV_STATE_ERROR;
				}
			} else {
				chg_drv_err_control(chip, err_type);
				chg_drv_start_monitor(chip);
				next_state = CHG_DRV_STATE_ERROR;
			}
			break;

		default:
			break;
	}

	return next_state;
}

/**
 * chg_drv_func_connect_control
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_connect_control(struct chg_drv_chip *chip,
														 chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	chg_drv_ctrl_type charge_ctrl;
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	chg_drv_monitor_param param = {0};
	int ret = 0;

	charge_ctrl = chg_drv_connect_info_update(chip, &data->connect_info);

	switch (charge_ctrl) {
		case CHG_DRV_CONTROL_STOP:
			chg_drv_charge_control(chip, CHG_DRV_CONTROL_STOP);
			chg_drv_terminate(chip, false);
			next_state = CHG_DRV_STATE_IDLE;
			break;

		case CHG_DRV_CONTROL_USB_CHARGE:
		case CHG_DRV_CONTROL_HOLDER_CHARGE:
			chg_drv_stop_monitor(chip);

			chg_drv_get_parameters(chip, &param);
			err_type = chg_drv_check_parameters(chip, &param);
			if (err_type == CHG_DRV_ERR_NON) {
				ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_POWERPATH);
				if (unlikely(ret != 0)) {
					chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
					next_state = CHG_DRV_STATE_ERROR;
				}
			} else {
				chg_drv_err_control(chip, err_type);
				next_state = CHG_DRV_STATE_ERROR;
			}

			chg_drv_start_monitor(chip);
			break;

		default:
			break;
	}

	return next_state;
}

/**
 * chg_drv_func_charging_timer_expire
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_charging_timer_expire(struct chg_drv_chip *chip,
															   chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;
	chg_drv_result_type result;
	union power_supply_propval pval = {0,};
	int ret = 0;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	switch (data->timer_info.type) {
		case CHG_DRV_TIMER_FULL_1:
			CHG_DRV_DBGLOG("[%s] additional charge\n", __func__);
			chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_FULL);
			chg_drv_start_full_timer2(chip);
			break;

		case CHG_DRV_TIMER_FULL_2:
			CHG_DRV_INFOLOG("[%s] battery full.\n", __func__);
			chg_drv_stop_full_timer(chip);
			chg_drv_stop_safety_timer(chip);
			chg_drv_stop_aicl_restart_monitor(chip);
			chg_drv_charge_control(chip, CHG_DRV_CONTROL_POWERPATH);
			chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_FULL);
			next_state = CHG_DRV_STATE_FULL;
			break;

		case CHG_DRV_TIMER_PROTECTION:
			ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_POWERPATH);
			if (unlikely(ret != 0)) {
				chip->err_info.safety_timer_expire = true;
				chg_drv_err_control(chip, CHG_DRV_ERR_EXPIRE_TIME_OUT);
				next_state = CHG_DRV_STATE_ERROR;
				break;
			}
			msleep(2000);
			ret = power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval);
			if (unlikely(ret != 0)) {
				chip->err_info.safety_timer_expire = true;
				chg_drv_err_control(chip, CHG_DRV_ERR_EXPIRE_TIME_OUT);
				next_state = CHG_DRV_STATE_ERROR;
				break;
			}

			if (pval.intval < chip->dt_param.batt_unspec_voltage) {
				chip->err_info.safety_timer_expire = true;
				chg_drv_err_control(chip, CHG_DRV_ERR_EXPIRE_TIME_OUT);
				next_state = CHG_DRV_STATE_ERROR;
				break;
			}

			ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_RECHARGE);
			if (unlikely(ret != 0)) {
				chip->err_info.safety_timer_expire = true;
				chg_drv_err_control(chip, CHG_DRV_ERR_EXPIRE_TIME_OUT);
				next_state = CHG_DRV_STATE_ERROR;
				break;
			}

			chg_drv_start_safety_timer(chip);
			break;

		case CHG_DRV_TIMER_VOLTAGE_CHECK:
			result = chg_drv_charge_type_judgement(chip, data->timer_info.device, &err_type);
			switch (result) {
				case CHG_DRV_RESULT_SUCCESS:
					if (CHG_DRV_IS_HOLDER_CHARGE(chip) &&
						data->timer_info.device == CHG_DRV_DEVICE_TYPE_USB) {
						break;
					}
					if (data->timer_info.device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
						chip->charge_type = chip->dev_holder.charge_type;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
					} else {
						chip->charge_type = chip->dev_usb.charge_type;
					}
					ret = chg_drv_charge_control(chip, CHG_DRV_CONTROL_RECHARGE);
					if (likely(ret == 0)) {
						chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_CHARGING);
					} else {
						chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
						next_state = CHG_DRV_STATE_ERROR;
					}
					chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
					chg_drv_start_monitor(chip);
					break;

				case CHG_DRV_RESULT_CONTINUE:
					chg_drv_start_voltage_check_timer(chip, data->timer_info.device);
					break;

				case CHG_DRV_RESULT_FAILURE:
					if (CHG_DRV_IS_HOLDER_CHARGE(chip) &&
						data->timer_info.device == CHG_DRV_DEVICE_TYPE_USB) {
						chg_drv_start_voltage_check_timer(chip, data->timer_info.device);
						break;
					}
					chg_drv_charge_control(chip, CHG_DRV_CONTROL_STOP);
					chg_drv_batt_set_status(chip, POWER_SUPPLY_STATUS_NOT_CHARGING);
					chg_drv_start_voltage_check_timer(chip, data->timer_info.device);
					break;

				case CHG_DRV_RESULT_OVER:
					chg_drv_err_control(chip, err_type);
					chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
					chg_drv_start_monitor(chip);
					next_state = CHG_DRV_STATE_ERROR;
					break;

				default:
					break;
			}
			break;

		case CHG_DRV_TIMER_NEGOTIATION:
			if (data->timer_info.device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				result = chg_drv_nego_holder_control(chip);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			} else {
				result = chg_drv_nego_usb_control(chip);
			}
			switch (result) {
				case CHG_DRV_RESULT_SUCCESS:
					chg_drv_start_voltage_check_timer(chip, data->timer_info.device);
					break;

				case CHG_DRV_RESULT_FAILURE:
					chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
					chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
					chg_drv_start_monitor(chip);
					next_state = CHG_DRV_STATE_ERROR;
					break;

				case CHG_DRV_RESULT_CONTINUE:
					chg_drv_start_negotiation_timer(chip, data->timer_info.device);
					break;

				default:
					break;
			}
			break;

		default:
			break;
	}

	return next_state;
}

/**
 * chg_drv_func_timer_expire
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_timer_expire(struct chg_drv_chip *chip,
													  chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	chg_drv_result_type result;
	chg_drv_err_type err_type = CHG_DRV_ERR_NON;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	switch (data->timer_info.type) {
		case CHG_DRV_TIMER_FULL_1:
			chg_drv_stop_full_timer(chip);
			break;
		case CHG_DRV_TIMER_FULL_2:
			chg_drv_stop_full_timer(chip);
			break;

		case CHG_DRV_TIMER_VOLTAGE_CHECK:
			result = chg_drv_charge_type_judgement(chip, data->timer_info.device, &err_type);
			switch (result) {
				case CHG_DRV_RESULT_SUCCESS:
					if (CHG_DRV_IS_HOLDER_CHARGE(chip) &&
						data->timer_info.device == CHG_DRV_DEVICE_TYPE_USB) {
						break;
					}
					if (data->timer_info.device == CHG_DRV_DEVICE_TYPE_HOLDER) {
						chip->charge_type = chip->dev_holder.charge_type;
					} else {
						chip->charge_type = chip->dev_usb.charge_type;
					}
					chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
					chg_drv_start_monitor(chip);
					break;

				case CHG_DRV_RESULT_CONTINUE:
				case CHG_DRV_RESULT_FAILURE:
					chg_drv_start_voltage_check_timer(chip, data->timer_info.device);
					break;

				case CHG_DRV_RESULT_OVER:
					chg_drv_err_control(chip, err_type);
					chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
					chg_drv_start_monitor(chip);
					next_state = CHG_DRV_STATE_ERROR;
					break;

				default:
					break;
			}
			break;

		case CHG_DRV_TIMER_NEGOTIATION:
			if (data->timer_info.device == CHG_DRV_DEVICE_TYPE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				result = chg_drv_nego_holder_control(chip);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			} else {
				result = chg_drv_nego_usb_control(chip);
			}
			switch (result) {
				case CHG_DRV_RESULT_SUCCESS:
					chg_drv_start_voltage_check_timer(chip, data->timer_info.device);
					break;

				case CHG_DRV_RESULT_FAILURE:
					chg_drv_err_control(chip, CHG_DRV_ERR_OTHER_ERR);
					chg_drv_start_adapter_volt_monitor(chip, CHG_DRV_SET_TIMER_1S);
					chg_drv_start_monitor(chip);
					next_state = CHG_DRV_STATE_ERROR;
					break;

				case CHG_DRV_RESULT_CONTINUE:
					chg_drv_start_negotiation_timer(chip, data->timer_info.device);
					break;

				default:
					break;
			}
			break;

		default:
			break;
	}

	return next_state;
}

/**
 * chg_drv_func_charging_update
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_charging_update(struct chg_drv_chip *chip,
														 chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	int ret = 0;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	do {
		if (chip->charge_ctrl == CHG_DRV_CONTROL_STOP) {
			CHG_DRV_INFOLOG("[%s] charge_ctrl=STOP, Skip current setting\n", __func__);
			break;
		}

		ret = chg_drv_pmic_current_setting(chip);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("[%s] Parameter Update Failed. ret:[%d]\n", __func__, ret);
			break;
		}

		if (chip->charge_type == CHG_DRV_CHARGE_TYPE_5V) {
			if (CHG_DRV_IS_HOLDER_CHARGE(chip)) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
				ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_HOLDER, CHG_DRV_AICL_TYPE_RERUN);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
			} else {
				ret = chg_drv_pmic_aicl_setting(chip, CHG_DRV_DEVICE_TYPE_USB, CHG_DRV_AICL_TYPE_RERUN);
			}
			if (unlikely(ret != 0)) {
				CHG_DRV_ERRLOG("[%s] Parameter Update Failed. ret:[%d]\n", __func__, ret);
				break;
			}
		}
	} while (0);

	return next_state;
}

/**
 * chg_drv_func_parameter_update
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_parameter_update(struct chg_drv_chip *chip,
														  chg_drv_evt_data *data)
{
	chg_drv_state_type next_state = CHG_DRV_STATE_NOCHANGE;
	int ret = 0;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	do {
		if (chip->charge_ctrl == CHG_DRV_CONTROL_STOP) {
			CHG_DRV_INFOLOG("[%s] charge_ctrl=STOP, Skip current setting\n", __func__);
			break;
		}

		ret = chg_drv_pmic_current_setting(chip);
		if (unlikely(ret != 0)) {
			CHG_DRV_ERRLOG("[%s] Parameter Update Failed. ret:[%d]\n", __func__, ret);
		}
	} while (0);

	return next_state;
}

/**
 * chg_drv_func_null
 *
 * @param	chip	: charger device info pointer
 *			data	: event data queue pointer
 *
 * @retval	next state
 */
static chg_drv_state_type chg_drv_func_null(struct chg_drv_chip *chip, chg_drv_evt_data *data)
{
	return CHG_DRV_STATE_NOCHANGE;
}

static const chg_drv_func chg_drv_state_func_table[CHG_DRV_STATE_NUM][CHG_DRV_EVENT_NUM] = {
	/* CHG_DRV_STATE_IDLE */					{
		/* CHG_DRV_EVENT_MONITOR_PARAM		*/		chg_drv_func_null,
		/* CHG_DRV_EVENT_CHARGE_INFO_NOTICE	*/		chg_drv_func_idle_info_notice,
		/* CHG_DRV_EVENT_CHARGE_CTRL		*/		chg_drv_func_idle_control,
		/* CHG_DRV_EVENT_CHARGE_TIMER		*/		chg_drv_func_null,
		/* CHG_DRV_EVENT_PARAM_UPDATE		*/		chg_drv_func_null,
	/* CHG_DRV_STATE_CHARGING */				} , {
		/* CHG_DRV_EVENT_MONITOR_PARAM		*/		chg_drv_func_charging_monitor,
		/* CHG_DRV_EVENT_CHARGE_INFO_NOTICE	*/		chg_drv_func_info_notice,
		/* CHG_DRV_EVENT_CHARGE_CTRL		*/		chg_drv_func_charging_control,
		/* CHG_DRV_EVENT_CHARGE_TIMER		*/		chg_drv_func_charging_timer_expire,
		/* CHG_DRV_EVENT_PARAM_UPDATE		*/		chg_drv_func_charging_update,
	/* CHG_DRV_STATE_FULL */					} , {
		/* CHG_DRV_EVENT_MONITOR_PARAM		*/		chg_drv_func_full_monitor,
		/* CHG_DRV_EVENT_CHARGE_INFO_NOTICE	*/		chg_drv_func_info_notice,
		/* CHG_DRV_EVENT_CHARGE_CTRL		*/		chg_drv_func_connect_control,
		/* CHG_DRV_EVENT_CHARGE_TIMER		*/		chg_drv_func_timer_expire,
		/* CHG_DRV_EVENT_PARAM_UPDATE		*/		chg_drv_func_parameter_update,
	/* CHG_DRV_STATE_ERROR */				} , {
		/* CHG_DRV_EVENT_MONITOR_PARAM		*/		chg_drv_func_error_monitor,
		/* CHG_DRV_EVENT_CHARGE_INFO_NOTICE	*/		chg_drv_func_info_notice,
		/* CHG_DRV_EVENT_CHARGE_CTRL		*/		chg_drv_func_connect_control,
		/* CHG_DRV_EVENT_CHARGE_TIMER		*/		chg_drv_func_timer_expire,
		/* CHG_DRV_EVENT_PARAM_UPDATE		*/		chg_drv_func_parameter_update,
											}
};

/**
 * oem_chg_drv_thread - charger thread main function
 *
 * @param ptr	: charger device
 */
int oem_chg_drv_thread(void *ptr)
{
	struct chg_drv_chip *chip = ptr;
	chg_drv_state_type new_state = CHG_DRV_STATE_NOCHANGE;
	chg_drv_evt_queue *evt_queue_p = NULL;
	const chg_drv_func *functbl;
	union power_supply_propval pval = {0,};
	int ret = 0;

	chg_drv_charging_wake_lock = wakeup_source_register(chip->dev, "oem_charging");

	while (1) {
		ret = power_supply_get_property(chip->oem_battery_psy, POWER_SUPPLY_PROP_INITIALIZE, &pval);
		if (unlikely(ret < 0)) {
			CHG_DRV_ERRLOG("[%s] get FGIC_INITIAL_STATUS error %d\n", __func__, ret);
		}
		if (pval.intval) {
			break;
		} else {
			CHG_DRV_ERRLOG("[%s] oem_battery is initializing\n", __func__);
		}
		msleep(200);
	}

	ret = chg_drv_update_cycle_charge_voltage(chip);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] update_cycle_charge_voltage failed. ret = %d\n", __func__, ret);
	}
	CHG_DRV_DBGLOG("[%s] vmax_adj = 0x%x\n", __func__, chip->vmax_adj);

	do {
		wait_event(chip->chg_wq,
				  (!list_empty(&oem_chg_drv_evt_list_exec) || kthread_should_stop()));
		if (kthread_should_stop()) {
			break;
		}

		evt_queue_p = chg_drv_queue_get(&oem_chg_drv_evt_list_exec);
		if (evt_queue_p == NULL) {
			continue;
		}

		if ((chip->charger_info.state >= CHG_DRV_STATE_NUM) ||
			(evt_queue_p->event >= CHG_DRV_EVENT_NUM))	 {
			CHG_DRV_ERRLOG("[%s] thread Error:sts[%d] evt[%d] Fatal\n",
						__func__, chip->charger_info.state, evt_queue_p->event);
			emergency_restart();
		}

		if (chip->charger_info.state == CHG_DRV_STATE_IDLE) {
			__pm_stay_awake(chg_drv_charging_wake_lock);
		}

		CHG_DRV_DBGLOG("[%s] charge_state = %d event = %d\n",
									__func__, chip->charger_info.state, evt_queue_p->event);

		functbl = &chg_drv_state_func_table[chip->charger_info.state][evt_queue_p->event];
		new_state = (*functbl)(chip, &evt_queue_p->data);

		if (new_state != CHG_DRV_STATE_NOCHANGE) {
			CHG_DRV_INFOLOG("[%s] event:%d old state:%d -> new state:%d\n",
					 __func__, evt_queue_p->event, chip->charger_info.state, new_state);
			chip->charger_info.state = new_state;
		}

		chg_drv_queue_put(evt_queue_p, &oem_chg_drv_evt_list_free);

		if (chip->charger_info.state == CHG_DRV_STATE_IDLE) {
			__pm_relax(chg_drv_charging_wake_lock);
		}
	} while (1);

	wakeup_source_unregister(chg_drv_charging_wake_lock);

	return 0;
}

/**
 * oem_chg_drv_source_req
 *
 * @param	source	: charging source
 *			mA		: charge current
 */
void oem_chg_drv_source_req(int source, unsigned int mA)
{
	struct chg_drv_chip *chip = oem_chg_drv_chip;
	unsigned long source_work = 0;
	chg_drv_evt_data evt_data;
	static char chg_src_str[OEM_CHG_SOURCE_NUM][8] = {
		"HOLDER", "USB", "AC", "APSD",
	};

	if (unlikely(chip == NULL)) {
		CHG_DRV_ERRLOG("[%s] CHG-DRV uninitialized\n", __func__);
	}

	CHG_DRV_RECLOG("[%s] %s mA=%d\n", __func__, chg_src_str[source], mA);

	if (unlikely(chip != NULL)) {
		__pm_wakeup_event(chg_drv_timer_wake_lock, (chip->dt_param.wake_lock_timer_sec * HZ));
	}

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	source_work = CHG_DRV_SOURCE(source);

	if (mA != 0) {
		if (source_work & CHG_DRV_SOURCE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			evt_data.connect_info.event = CHG_DRV_CONNECT_EVENT_HOLDER_IN;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		} else {
			evt_data.connect_info.event = CHG_DRV_CONNECT_EVENT_USB_IN;
		}
	} else {
		if (source_work & CHG_DRV_SOURCE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
			evt_data.connect_info.event = CHG_DRV_CONNECT_EVENT_HOLDER_OUT;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
		} else {
			evt_data.connect_info.event = CHG_DRV_CONNECT_EVENT_USB_OUT;
		}
	}

	evt_data.connect_info.chg_source = source;
	evt_data.connect_info.max_current = mA;

	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_CTRL, &evt_data);
}
EXPORT_SYMBOL_GPL(oem_chg_drv_source_req);

/**
 * oem_chg_drv_current_req
 *
 * @param	source	: charging source
 *			mA		: charge current
 */
void oem_chg_drv_current_req(int source, unsigned int mA)
{
	struct chg_drv_chip *chip = oem_chg_drv_chip;
	unsigned long source_work = 0;
	chg_drv_evt_data evt_data;

	CHG_DRV_RECLOG("[%s] %d mA=%d\n", __func__, source, mA);

	__pm_wakeup_event(chg_drv_timer_wake_lock, (chip->dt_param.wake_lock_timer_sec * HZ));

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	source_work = CHG_DRV_SOURCE(source);

	if (source_work & CHG_DRV_SOURCE_HOLDER) {
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
		evt_data.notice_info.change_info.device = CHG_DRV_DEVICE_TYPE_HOLDER;
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
	} else {
		evt_data.notice_info.change_info.device = CHG_DRV_DEVICE_TYPE_USB;
	}

	evt_data.notice_info.type = CHG_DRV_NOTICE_CHANGE_CURRENT;
	evt_data.notice_info.change_info.max_current = mA;

	chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_INFO_NOTICE, &evt_data);
}
EXPORT_SYMBOL_GPL(oem_chg_drv_current_req);

/**
 * oem_chg_drv_notify_error
 *
 * @param	chg_err	: error type
 */
void oem_chg_drv_notify_error(oem_chg_err_type chg_err)
{
	struct chg_drv_chip *chip = oem_chg_drv_chip;
	chg_drv_evt_data evt_data;

	CHG_DRV_INFOLOG("[%s] in, err=%d\n", __func__, chg_err);

	if (unlikely(chip == NULL)) {
		CHG_DRV_ERRLOG("[%s] CHG-DRV uninitialized\n", __func__);
	}

	if (unlikely(chip != NULL)) {
		__pm_wakeup_event(chg_drv_timer_wake_lock, (chip->dt_param.wake_lock_timer_sec * HZ));
	}

	memset(&evt_data, 0, sizeof(chg_drv_evt_data));

	switch (chg_err) {
		case OEM_CHG_ERROR_OVP_NONE:
			evt_data.notice_info.type = CHG_DRV_NOTICE_OVP_NOTIFY_ERROR;
			evt_data.notice_info.ovp_detect = OEM_CHG_ERROR_NONE;
			chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_INFO_NOTICE, &evt_data);
			break;
		case OEM_CHG_ERROR_OVP_DETECT:
			evt_data.notice_info.type = CHG_DRV_NOTICE_OVP_NOTIFY_ERROR;
			evt_data.notice_info.ovp_detect = OEM_CHG_ERROR_DETECT;
			chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_INFO_NOTICE, &evt_data);
			break;
		case OEM_CHG_ERROR_USB_HOT_NONE:
			evt_data.notice_info.type = CHG_DRV_NOTICE_USBHOT_NOTIFY_ERROR;
			evt_data.notice_info.usb_hot_detect = OEM_CHG_ERROR_NONE;
			chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_INFO_NOTICE, &evt_data);
			break;
		case OEM_CHG_ERROR_USB_HOT_DETECT:
			evt_data.notice_info.type = CHG_DRV_NOTICE_USBHOT_NOTIFY_ERROR;
			evt_data.notice_info.usb_hot_detect = OEM_CHG_ERROR_DETECT;
			chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_INFO_NOTICE, &evt_data);
			break;
		case OEM_CHG_ERROR_PDO_ERROR_NONE:
			evt_data.notice_info.type = CHG_DRV_NOTICE_PDO_ERROR;
			evt_data.notice_info.pdo_error_detect = OEM_CHG_ERROR_NONE;
			chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_INFO_NOTICE, &evt_data);
			break;
		case OEM_CHG_ERROR_PDO_ERROR_DETECT:
			evt_data.notice_info.type = CHG_DRV_NOTICE_PDO_ERROR;
			evt_data.notice_info.pdo_error_detect = OEM_CHG_ERROR_DETECT;
			chg_drv_event_req(chip, CHG_DRV_EVENT_CHARGE_INFO_NOTICE, &evt_data);
			break;
		default:
			CHG_DRV_ERRLOG("[%s] Error Unknown type %d\n", __func__, chg_err);
			break;
	}
}
EXPORT_SYMBOL_GPL(oem_chg_drv_notify_error);

/**
 * oem_chg_drv_initialized
 *
 * @param	void
 *
 * @retval	initialized state
 */
bool oem_chg_drv_initialized(void)
{
	struct chg_drv_chip *chip = oem_chg_drv_chip;

	if (unlikely(chip == NULL)) {
		CHG_DRV_ERRLOG("[%s] CHG-DRV uninitialized\n", __func__);
		return false;
	}

	CHG_DRV_INFOLOG("[%s] CHG-DRV initialized\n", __func__);
	return true;
}
EXPORT_SYMBOL_GPL(oem_chg_drv_initialized);

/**
 * oem_chg_drv_pdo_available_check
 *
 * @param	pdo_data	: input pdo voltage
 *			out 		: output pdo
 *			num 		: pdo list num
 */
static bool enable_pdo_5v = true;
static bool enable_pdo_9v = true;
module_param(enable_pdo_5v, bool, S_IRUGO|S_IWUSR);
module_param(enable_pdo_9v, bool, S_IRUGO|S_IWUSR);
int oem_chg_drv_pdo_available_check(u32 *pdo_data, bool *out, int num)
{
	#define PDO_5V	BIT(0)
	#define PDO_9V	BIT(2)
	#define BIT_CHK(val, bit)	(((val) & (bit)) == (bit))
	struct chg_drv_chip *chip = oem_chg_drv_chip;
	int i;
	int voltage_uv;
	u32 valid = 0;

	if (unlikely(chip == NULL)) {
		CHG_DRV_ERRLOG("[%s] CHG-DRV uninitialized\n", __func__);
		return 0;
	}

	for (i = 0; i < num; i++) {
		voltage_uv = pdo_data[i];
		out[i] = false;

		if ((voltage_uv >= chip->dt_param.pd5v_low_voltage_threshold) &&
			(voltage_uv <= chip->dt_param.pd5v_high_voltage_threshold)) {
			CHG_DRV_DBGLOG("[%s] 5V-PDO found %d:%d(uV)\n",
							__func__, i, voltage_uv);
			if (BIT_CHK(valid, PDO_9V)) {
				CHG_DRV_DBGLOG("[%s] PDO order is abnormal!!\n", __func__);
			}
			if (enable_pdo_5v) {
				out[i] = true;
				valid |= PDO_5V;
			}
			continue;
		}
		if ((voltage_uv >= chip->dt_param.pd9v_low_voltage_threshold) &&
			(voltage_uv <= chip->dt_param.pd9v_high_voltage_threshold)) {
			CHG_DRV_DBGLOG("[%s] 9V-PDO found %d:%d(mV)\n",
							__func__, i, voltage_uv);

			if (!BIT_CHK(valid, PDO_5V)) {
				CHG_DRV_DBGLOG("[%s] 5V-PDO is not found!!\n", __func__);
			}
			if (enable_pdo_9v) {
				out[i] = true;
				valid |= PDO_9V;
			}
			continue;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(oem_chg_drv_pdo_available_check);

/**
 * chg_drv_parse_dt
 *
 * @param	chip	: charger device info pointer
 *
 * @retval	error type
 */
static int chg_drv_parse_dt(struct chg_drv_chip *chip)
{
	chg_drv_dt *dt = &chip->dt_param;
	struct device_node *node = chip->dev->of_node;
	int ret;
	int byte_len;

	if (!node) {
		CHG_DRV_ERRLOG("[%s] device tree node missing\n", __func__);
		return -EINVAL;
	}

	ret = of_property_read_u32(node,
				"fcnt,usb5v-soft-icl-low-supply-volt-uv",
				&dt->usb5v_soft_icl_low_voltage);
	if (ret < 0)
		dt->usb5v_soft_icl_low_voltage = 0;

	ret = of_property_read_u32(node,
				"fcnt,usb5v-soft-icl-high-supply-volt-uv",
				&dt->usb5v_soft_icl_high_voltage);
	if (ret < 0)
		dt->usb5v_soft_icl_high_voltage = 0;

	ret = of_property_read_u32(node,
				"fcnt,usb5v-under-supply-volt-uv",
				&dt->usb5v_under_voltage);
	if (ret < 0)
		dt->usb5v_under_voltage = CHG_DRV_PD5V_SUPPLY_UV_DEFAULT;
	chg_drv_usb_volt_chk_table[CHG_DRV_CHARGE_TYPE_5V].under_voltage = dt->usb5v_under_voltage;

	ret = of_property_read_u32(node,
				"fcnt,usb5v-over-supply-volt-uv",
				&dt->usb5v_over_voltage);
	if (ret < 0)
		dt->usb5v_over_voltage = CHG_DRV_PD5V_SUPPLY_OV_DEFAULT;
	chg_drv_usb_volt_chk_table[CHG_DRV_CHARGE_TYPE_5V].over_voltage = dt->usb5v_over_voltage;

	ret = of_property_read_u32(node,
				"fcnt,usb9v-under-supply-volt-uv,1st",
				&dt->usb9v_under_voltage_1st);
	if (ret < 0)
		dt->usb9v_under_voltage_1st = CHG_DRV_PD9V_SUPPLY_UV_DEFAULT;
	chg_drv_usb_volt_chk_table[CHG_DRV_CHARGE_TYPE_9V].under_voltage = dt->usb9v_under_voltage_1st;

	ret = of_property_read_u32(node,
				"fcnt,usb9v-under-supply-volt-uv,2nd",
				&dt->usb9v_under_voltage_2nd);
	if (ret < 0)
		dt->usb9v_under_voltage_2nd = CHG_DRV_PD9V_SUPPLY_UV_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,usb9v-over-supply-volt-uv",
				&dt->usb9v_over_voltage);
	if (ret < 0)
		dt->usb9v_over_voltage = CHG_DRV_PD9V_SUPPLY_OV_DEFAULT;
	chg_drv_usb_volt_chk_table[CHG_DRV_CHARGE_TYPE_9V].over_voltage = dt->usb9v_over_voltage;

	ret = of_property_read_u32(node,
				"fcnt,holder5v-under-supply-volt-uv",
				&dt->holder5v_under_voltage);
	if (ret < 0)
		dt->holder5v_under_voltage = CHG_DRV_PD5V_SUPPLY_UV_DEFAULT;
	chg_drv_holder_volt_chk_table[CHG_DRV_CHARGE_TYPE_5V].under_voltage = dt->holder5v_under_voltage;

	ret = of_property_read_u32(node,
				"fcnt,holder5v-over-supply-volt-uv",
				&dt->holder5v_over_voltage);
	if (ret < 0)
		dt->holder5v_over_voltage = CHG_DRV_PD5V_SUPPLY_OV_DEFAULT;
	chg_drv_holder_volt_chk_table[CHG_DRV_CHARGE_TYPE_5V].over_voltage = dt->holder5v_over_voltage;

	ret = of_property_read_u32(node,
				"fcnt,holder9v-under-supply-volt-uv,1st",
				&dt->holder9v_under_voltage_1st);
	if (ret < 0)
		dt->holder9v_under_voltage_1st = CHG_DRV_PD9V_SUPPLY_UV_DEFAULT;
	chg_drv_holder_volt_chk_table[CHG_DRV_CHARGE_TYPE_9V].under_voltage = dt->holder9v_under_voltage_1st;

	ret = of_property_read_u32(node,
				"fcnt,holder9v-under-supply-volt-uv,2nd",
				&dt->holder9v_under_voltage_2nd);
	if (ret < 0)
		dt->holder9v_under_voltage_2nd = CHG_DRV_PD9V_SUPPLY_UV_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,holder9v-over-supply-volt-uv",
				&dt->holder9v_over_voltage);
	if (ret < 0)
		dt->holder9v_over_voltage = CHG_DRV_PD9V_SUPPLY_OV_DEFAULT;
	chg_drv_holder_volt_chk_table[CHG_DRV_CHARGE_TYPE_9V].over_voltage = dt->holder9v_over_voltage;

	ret = of_property_read_u32(node,
				"fcnt,pd5v-low-voltage-threshold-uv",
				&dt->pd5v_low_voltage_threshold);
	if (ret < 0)
		dt->pd5v_low_voltage_threshold = CHG_DRV_PD5V_SUPPLY_VOLT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,pd5v-high-voltage-threshold-uv",
				&dt->pd5v_high_voltage_threshold);
	if (ret < 0)
		dt->pd5v_high_voltage_threshold = CHG_DRV_PD5V_SUPPLY_VOLT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,pd9v-low-voltage-threshold-uv",
				&dt->pd9v_low_voltage_threshold);
	if (ret < 0)
		dt->pd9v_low_voltage_threshold = CHG_DRV_PD9V_SUPPLY_VOLT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,pd9v-high-voltage-threshold-uv",
				&dt->pd9v_high_voltage_threshold);
	if (ret < 0)
		dt->pd9v_high_voltage_threshold = CHG_DRV_PD9V_SUPPLY_VOLT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,batt-over-volt-uv",
				&dt->batt_over_voltage);
	if (ret < 0)
		dt->batt_over_voltage = CHG_DRV_BATT_VOLT_OV_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,batt-unspec-volt-uv",
				&dt->batt_unspec_voltage);
	if (ret < 0)
		dt->batt_unspec_voltage = CHG_DRV_BATT_VOLT_UV_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,batt-low-volt-uv,onchg",
				&dt->batt_low_voltage_onchg);
	if (ret < 0)
		dt->batt_low_voltage_onchg = CHG_DRV_BATT_VOLT_UV_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,batt-low-volt-uv,offchg",
				&dt->batt_over_voltage_offchg);
	if (ret < 0)
		dt->batt_over_voltage_offchg = CHG_DRV_BATT_VOLT_UV_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-full-soc-lv1",
				&dt->chg_full_soc_lv1);
	if (ret < 0)
		dt->chg_full_soc_lv1 = CHG_DRV_BATT_SOC_FULL_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-full-soc-lv2",
				&dt->chg_full_soc_lv2);
	if (ret < 0)
		dt->chg_full_soc_lv2 = CHG_DRV_BATT_SOC_FULL_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-rechg-soc-lv1",
				&dt->chg_rechg_soc_lv1);
	if (ret < 0)
		dt->chg_rechg_soc_lv1 = CHG_DRV_BATT_SOC_RECHG_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-rechg-soc-lv2",
				&dt->chg_rechg_soc_lv2);
	if (ret < 0)
		dt->chg_rechg_soc_lv2 = CHG_DRV_BATT_SOC_RECHG_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-full-soc-longevity-lv1",
				&dt->chg_full_soc_longevity_lv1);
	if (ret < 0)
		dt->chg_full_soc_longevity_lv1 = CHG_DRV_BATT_SOC_FULL_LONGEVITY_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-full-soc-longevity-lv2",
				&dt->chg_full_soc_longevity_lv2);
	if (ret < 0)
		dt->chg_full_soc_longevity_lv2 = CHG_DRV_BATT_SOC_FULL_LONGEVITY_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-rechg-soc-longevity-lv1",
				&dt->chg_rechg_soc_longevity_lv1);
	if (ret < 0)
		dt->chg_rechg_soc_longevity_lv1 = CHG_DRV_BATT_SOC_RECHG_LONGEVITY_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-rechg-soc-longevity-lv2",
				&dt->chg_rechg_soc_longevity_lv2);
	if (ret < 0)
		dt->chg_rechg_soc_longevity_lv2 = CHG_DRV_BATT_SOC_RECHG_LONGEVITY_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-volt-uv",
				&dt->chg_voltage);
	if (ret < 0)
		dt->chg_voltage = CHG_DRV_CHARGE_VOLT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-volt-cycle-uv-lv1",
				&dt->chg_voltage_cycle_lv1);
	if (ret < 0)
		dt->chg_voltage_cycle_lv1 = CHG_DRV_CHARGE_VOLT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-volt-cycle-uv-lv2",
				&dt->chg_voltage_cycle_lv2);
	if (ret < 0)
		dt->chg_voltage_cycle_lv2 = CHG_DRV_CHARGE_VOLT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-safety-timer-min",
				&dt->chg_safety_timer_min);
	if (ret < 0)
		dt->chg_safety_timer_min = 0;

	ret = of_property_read_u32(node,
				"fcnt,chg-full1-timer-min",
				&dt->chg_full1_timer_min);
	if (ret < 0)
		dt->chg_full1_timer_min = 0;

	ret = of_property_read_u32(node,
				"fcnt,chg-full2-timer-min",
				&dt->chg_full2_timer_min);
	if (ret < 0)
		dt->chg_full2_timer_min = 0;

	ret = of_property_read_u32(node,
				"fcnt,batt-temp-hot-limit",
				&dt->batt_temp_hot_limit);
	if (ret < 0)
		dt->batt_temp_hot_limit = CHG_DRV_BATT_TEMP_HOT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,batt-temp-warm-limit",
				&dt->batt_temp_warm_limit);
	if (ret < 0)
		dt->batt_temp_warm_limit = CHG_DRV_BATT_TEMP_HOT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,batt-temp-cool-limit",
				&dt->batt_temp_cool_limit);
	if (ret < 0)
		dt->batt_temp_cool_limit = CHG_DRV_BATT_TEMP_COLD_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,batt-temp-cold-limit",
				&dt->batt_temp_cold_limit);
	if (ret < 0)
		dt->batt_temp_cold_limit = CHG_DRV_BATT_TEMP_COLD_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,batt-temp-start-limit",
				&dt->batt_temp_start_limit);
	if (ret < 0)
		dt->batt_temp_start_limit = CHG_DRV_BATT_TEMP_HOT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,batt-temp-limit-cancel",
				&dt->batt_temp_limit_cancel);
	if (ret < 0)
		dt->batt_temp_limit_cancel = CHG_DRV_BATT_TEMP_CANCEL_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-nego-usb-detect-count",
				&dt->chg_nego_usb_detect_count);
	if (ret < 0)
		dt->chg_nego_usb_detect_count = CHG_DRV_NEGO_DETECT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-nego-usb-detect-max",
				&dt->chg_nego_usb_detect_max);
	if (ret < 0)
		dt->chg_nego_usb_detect_max = CHG_DRV_NEGO_MAX_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-nego-holder-detect-count",
				&dt->chg_nego_holder_detect_count);
	if (ret < 0)
		dt->chg_nego_holder_detect_count = CHG_DRV_NEGO_DETECT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-nego-holder-detect-max",
				&dt->chg_nego_holder_detect_max);
	if (ret < 0)
		dt->chg_nego_holder_detect_max = CHG_DRV_NEGO_MAX_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-adapter-voltage-detect-count",
				&dt->chg_adapter_voltage_detect_count);
	if (ret < 0)
		dt->chg_adapter_voltage_detect_count = CHG_DRV_NEGO_DETECT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,chg-adapter-voltage-detect-max",
				&dt->chg_adapter_voltage_detect_max);
	if (ret < 0)
		dt->chg_adapter_voltage_detect_max = CHG_DRV_NEGO_MAX_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,wake-lock-timer-sec",
				&dt->wake_lock_timer_sec);
	if (ret < 0)
		dt->wake_lock_timer_sec = 3;

	ret = of_property_read_u32(node,
				"fcnt,usb5v-ibat-current-max-ma",
				&dt->usb5v_ibat_current_max);
	if (ret < 0)
		dt->usb5v_ibat_current_max = CHG_DRV_IBATT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,usb9v-ibat-current-max-ma",
				&dt->usb9v_ibat_current_max);
	if (ret < 0)
		dt->usb9v_ibat_current_max = CHG_DRV_IBATT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,holder5v-ibat-current-max-ma",
				&dt->holder5v_ibat_current_max);
	if (ret < 0)
		dt->holder5v_ibat_current_max = CHG_DRV_IBATT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,holder9v-ibat-current-max-ma",
				&dt->holder9v_ibat_current_max);
	if (ret < 0)
		dt->holder9v_ibat_current_max = CHG_DRV_IBATT_DEFAULT;

	ret = of_property_read_u32(node,
				"fcnt,ibat-current-low-batt-ma",
				&dt->common_ibat_current_low_batt);
	if (ret < 0) {
		dt->common_ibat_current_low_batt = CHG_DRV_IBATT_DEFAULT;
	}

	ret = of_property_read_u32(node,
				"fcnt,ibat-current-temp-warm-restrict-ma",
				&dt->common_ibat_current_temp_warm_restrict);
	if (ret < 0) {
		dt->common_ibat_current_temp_warm_restrict = CHG_DRV_IBATT_WARM_DEFAULT;
	}

	ret = of_property_read_u32(node,
				"fcnt,ibat-current-temp-cool-restrict-ma",
				&dt->common_ibat_current_temp_cool_restrict);
	if (ret < 0) {
		dt->common_ibat_current_temp_cool_restrict = CHG_DRV_IBATT_COOL_DEFAULT;
	}

	ret = of_property_read_u32(node,
				"fcnt,chg-consecutive-count",
				&dt->chg_consecutive_count);
	if (ret < 0)
		dt->chg_consecutive_count = 0;

	ret = of_property_read_u32(node,
				"fcnt,chg-term-current-ma",
				&dt->chg_termcurrent_ma);
	if (ret < 0)
		dt->chg_termcurrent_ma = 0;

	ret = of_property_read_u32(node,
				"fcnt,chg-cycle-term-current-ma",
				&dt->chg_cycle_termcurrent_ma);
	if (ret < 0)
		dt->chg_cycle_termcurrent_ma = 0;

	dt->chg_icl_sts_num = (CHG_DRV_SOFT_ICL_STEP_NUM-1);

	ret = of_property_read_u32(node,
				"fcnt,chg-icl-step-current-start-idx",
				&dt->chg_icl_step_current_start_idx);
	if (ret < 0)
		dt->chg_icl_sts_num = 0;

	if (of_property_count_u32_elems(node, "fcnt,chg-icl-sts-thresholds-min")
			!= CHG_DRV_SOFT_ICL_STEP_NUM) {
		dt->chg_icl_sts_num = 0;
	} else {
		ret = of_property_read_u32_array(node, "fcnt,chg-icl-sts-thresholds-min",
				dt->chg_icl_sts_thresholds_min, CHG_DRV_SOFT_ICL_STEP_NUM);
		if (ret < 0) {
			dt->chg_icl_sts_num = 0;
		}
	}

	if (of_property_count_u32_elems(node, "fcnt,chg-icl-sts-thresholds-max")
			!= CHG_DRV_SOFT_ICL_STEP_NUM) {
		dt->chg_icl_sts_num = 0;
	} else {
		ret = of_property_read_u32_array(node, "fcnt,chg-icl-sts-thresholds-max",
				dt->chg_icl_sts_thresholds_max, CHG_DRV_SOFT_ICL_STEP_NUM);
		if (ret < 0) {
			dt->chg_icl_sts_num = 0;
		}
	}

	if (of_property_count_u32_elems(node, "fcnt,chg-icl-step-current-deltas")
			!= CHG_DRV_SOFT_ICL_STEP_NUM) {
		dt->chg_icl_sts_num = 0;
	} else {
		ret = of_property_read_u32_array(node, "fcnt,chg-icl-step-current-deltas",
				dt->chg_icl_step_current_deltas, CHG_DRV_SOFT_ICL_STEP_NUM);
		if (ret < 0) {
			dt->chg_icl_sts_num = 0;
		}
	}

	if (of_find_property(node, "fcnt,thermal-mitigation", &byte_len)) {
		dt->chg_icl_thermal_mitigation = devm_kzalloc(chip->dev, byte_len,
			GFP_KERNEL);

		if (dt->chg_icl_thermal_mitigation == NULL) {
			return -ENOMEM;
		}

		chip->thermal_level_max = byte_len / sizeof(u32);
		ret = of_property_read_u32_array(node,
				"fcnt,thermal-mitigation",
				dt->chg_icl_thermal_mitigation,
				chip->thermal_level_max);
		if (ret < 0) {
			chip->thermal_level_max = 0;
		}
	}

	return 0;
}

/**
 * chg_drv_probe
 *
 * @param	pdev :
 *
 * @retval	Processing result
 */
static int chg_drv_probe(struct platform_device *pdev)
{
	struct chg_drv_chip *chip;
	struct power_supply_config chg_drv_cfg = {};
	int ret = 0;

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	chip = kzalloc(sizeof(struct chg_drv_chip), GFP_KERNEL);
	if (unlikely(chip == NULL)) {
		CHG_DRV_ERRLOG("[%s] No Memory Error!!\n", __func__);
		return -ENOMEM;
	}

	chip->dev = &(pdev->dev);
	chip->charge_mode = chg_drv_get_charge_mode();

	init_waitqueue_head(&chip->chg_wq);

	ret = chg_drv_parse_dt(chip);
	if (unlikely(ret != 0)) {
		CHG_DRV_ERRLOG("[%s] dt perse failed ret = %d\n", __func__, ret);
		goto oem_chg_drv_err_parse_dt;
	}

	chip->connect_state = CHG_DRV_CONNECT_STATE_NONE;
	chip->charge_type = CHG_DRV_CHARGE_TYPE_5V;
	chip->standard_type = CHG_DRV_STANDARD_TYPE_UNKNOWN;
	chip->apsd_type = 0;				
	chip->max_ilimit_value = CHG_DRV_ILIMIT_NOT_LIMITED;
	chip->heat_charge_mode = OEM_CHG_CHARGE_MODE_NORMAL;
	chip->chg_enable = 1;
	chip->consecutive_count = 0;
	chip->battery_low_flag = false;

	chip->dev_usb.charge_type = CHG_DRV_CHARGE_TYPE_UNKNOWN;
	chip->dev_usb.max_current = CHG_DRV_CURRENT_OFF;
	chip->dev_usb.watch_count = 0;
	chip->dev_usb.soft_icl_step_up_count = 0;
	chip->dev_usb.soft_icl_step_count = chip->dt_param.chg_icl_step_current_start_idx;
	chip->dev_usb.soft_icl_step_error = CHG_DRV_PARAM_NO_ERROR;
	chip->dev_usb.check_total = 0;
	chip->dev_usb.nego_total = 0;
	chip->dev_usb.adapter_9v_ilimit_value = CHG_DRV_ILIMIT_NOT_LIMITED;
	chip->dev_usb.soft_icl_ilimit_value = chip->dt_param.chg_icl_step_current_deltas[chip->dt_param.chg_icl_step_current_start_idx];
	chip->dev_usb.voltage_check = false;
	memset(chip->dev_usb.check_count, 0, sizeof(chip->dev_usb.check_count));
	memset(chip->dev_usb.nego_count, 0, sizeof(chip->dev_usb.nego_count));
	INIT_DELAYED_WORK(&chip->dev_usb.nego_timer_work, chg_drv_usb_nego_timer_work);
	INIT_DELAYED_WORK(&chip->dev_usb.check_timer_work, chg_drv_usb_check_timer_work);

	chip->dev_holder.charge_type = CHG_DRV_CHARGE_TYPE_UNKNOWN;
	chip->dev_holder.max_current = CHG_DRV_CURRENT_OFF;
	chip->dev_holder.watch_count = 0;
	chip->dev_holder.soft_icl_step_up_count = 0;
	chip->dev_holder.soft_icl_step_count = chip->dt_param.chg_icl_step_current_start_idx;
	chip->dev_holder.soft_icl_step_error = CHG_DRV_PARAM_NO_ERROR;
	chip->dev_holder.check_total = 0;
	chip->dev_holder.nego_total = 0;
	chip->dev_holder.adapter_9v_ilimit_value = CHG_DRV_ILIMIT_NOT_LIMITED;
	chip->dev_holder.soft_icl_ilimit_value = CHG_DRV_ILIMIT_NOT_LIMITED;
	chip->dev_holder.voltage_check = false;
	memset(chip->dev_holder.check_count, 0, sizeof(chip->dev_holder.check_count));
	memset(chip->dev_holder.nego_count, 0, sizeof(chip->dev_holder.nego_count));
#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	INIT_DELAYED_WORK(&chip->dev_holder.nego_timer_work, chg_drv_holder_nego_timer_work);
	INIT_DELAYED_WORK(&chip->dev_holder.check_timer_work, chg_drv_holder_check_timer_work);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	chip->charge_ctrl = CHG_DRV_CONTROL_NONE;

	memset(&chip->err_info, 0, sizeof(chg_drv_err_info));

	chip->batt_temp_hot_limit = chip->dt_param.batt_temp_start_limit;
	chip->batt_temp_warm_limit = chip->dt_param.batt_temp_warm_limit;
	chip->batt_temp_cold_limit = chip->dt_param.batt_temp_cold_limit;
	chip->batt_temp_cool_limit = chip->dt_param.batt_temp_cool_limit;

	INIT_DELAYED_WORK(&chip->charge_monitor_work, chg_drv_monitor_work);
	INIT_DELAYED_WORK(&chip->full_timer_work1, chg_drv_full_timer_work1);
	INIT_DELAYED_WORK(&chip->full_timer_work2, chg_drv_full_timer_work2);
	INIT_DELAYED_WORK(&chip->safety_timer_work, chg_drv_safety_timer_work);
	INIT_DELAYED_WORK(&chip->adapter_volt_monitor_work, chg_drv_adapter_volt_monitor_work);
	INIT_DELAYED_WORK(&chip->watchdog_work, chg_drv_watchdog_work);
	INIT_DELAYED_WORK(&chip->aicl_monitor_work, chg_drv_aicl_monitor_work);

	chip->full_timer_state = CHG_DRV_CHARGE_TIMER_STATE_NONE;

	chip->recovery_count = 0;
	chip->recovery_invalid = true;

	memset(chip->batt_volt_data.volt_data, 0, sizeof(chip->batt_volt_data.volt_data));
	chip->batt_volt_data.index = 0;
	chip->batt_volt_data.data_full = false;
	chip->longevity_charge_flag = false;
	chip->aicl_restart_first_flag = true;
	chip->thermal_level = 0;
	chip->cycle_chg_lvl = CHG_CYCLE_LEVEL_NONE;		

	chip->bms_psy = power_supply_get_by_name(QPNP_BMS_PSY_NAME);
	if (chip->bms_psy == NULL) {
		CHG_DRV_ERRLOG("[%s] power_supply_get_by_name %s failed\n", __func__, QPNP_BMS_PSY_NAME);
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_power_supply_get_by_name_bms;
	}

	chip->batt_psy = power_supply_get_by_name(QPNP_BATTERY_PSY_NAME);
	if (chip->batt_psy == NULL) {
		CHG_DRV_ERRLOG("[%s] power_supply_get_by_name %s failed\n", __func__, QPNP_BATTERY_PSY_NAME);
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_power_supply_get_by_name_batt;
	}

	chip->oem_battery_psy = power_supply_get_by_name(OEM_BATTERY_PSY_NAME);
	if (chip->oem_battery_psy == NULL) {
		CHG_DRV_ERRLOG("[%s] power_supply_get_by_name %s failed\n", __func__, OEM_BATTERY_PSY_NAME);
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_power_supply_get_by_name_oem_battery;
	}

	chip->usb_psy = power_supply_get_by_name(QPNP_USB_PSY_NAME);
	if (chip->usb_psy == NULL) {
		CHG_DRV_ERRLOG("[%s] power_supply_get_by_name %s failed\n", __func__, QPNP_USB_PSY_NAME);
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_power_supply_get_by_name_usb;
	}

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	chip->dc_psy = power_supply_get_by_name(QPNP_DC_PSY_NAME);
	if (chip->dc_psy == NULL) {
		CHG_DRV_ERRLOG("[%s] power_supply_get_by_name %s failed\n", __func__, QPNP_DC_PSY_NAME);
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_power_supply_get_by_name_dc;
	}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	chip->fcc_votable = find_votable("FCC");
	if (!chip->fcc_votable) {
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_find_votable_fcc;
	}

	chip->fv_votable = find_votable("FV");
	if (!chip->fv_votable) {
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_find_votable_fv;
	}

	chip->chg_disable_votable = find_votable("CHG_DISABLE");
	if (!chip->chg_disable_votable) {
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_find_votable_chg_disable;
	}

	chip->usb_icl_votable = find_votable("USB_ICL");
	if (!chip->usb_icl_votable) {
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_find_votable_usb_icl;
	}

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	chip->dc_icl_votable = find_votable("DC_ICL");
	if (!chip->dc_icl_votable) {
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_find_votable_dc_icl;
	}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	chip->chg_drv_wq = create_singlethread_workqueue(dev_name(&pdev->dev));
	if (!chip->chg_drv_wq) {
		CHG_DRV_ERRLOG("[%s] create_singlethread_workqueue failed\n", __func__);
		ret = -ESRCH;
		goto oem_chg_drv_err_create_singlethread_workqueue;
	}

	chip->oem_chg_psy_desc.name = OEM_CHARGER_PSY_NAME;
	chip->oem_chg_psy_desc.type = POWER_SUPPLY_TYPE_OEM_CHG;
	chip->oem_chg_psy_desc.get_property = chg_drv_oem_chg_get_property;
	chip->oem_chg_psy_desc.set_property = chg_drv_oem_chg_set_property;
	chip->oem_chg_psy_desc.properties = chg_drv_oem_chg_properties;
	chip->oem_chg_psy_desc.num_properties = ARRAY_SIZE(chg_drv_oem_chg_properties);
	chip->oem_chg_psy_desc.property_is_writeable = chg_drv_oem_chg_is_writeable;
	chg_drv_cfg.drv_data = chip;
	chg_drv_cfg.of_node = chip->dev->of_node;
	chip->oem_chg_psy = devm_power_supply_register(chip->dev, &chip->oem_chg_psy_desc,
											   &chg_drv_cfg);
	if (IS_ERR(chip->oem_usb_psy)) {
		ret = PTR_ERR(chip->oem_usb_psy);
		CHG_DRV_ERRLOG("[%s] power_supply_register %s failed ret = %d\n", __func__, OEM_CHG_USB_PSY_NAME, ret);
		goto oem_chg_drv_err_power_supply_register_oem_chg;
	}

	chip->oem_usb_psy_desc.name = OEM_CHG_USB_PSY_NAME;
	chip->oem_usb_psy_desc.type = POWER_SUPPLY_TYPE_OEM_USB;
	chip->oem_usb_psy_desc.get_property = chg_drv_common_get_property;
	chip->oem_usb_psy_desc.properties = chg_drv_common_properties;
	chip->oem_usb_psy_desc.num_properties = ARRAY_SIZE(chg_drv_common_properties);
	chg_drv_cfg.drv_data = chip;
	chg_drv_cfg.of_node = chip->dev->of_node;
	chip->oem_usb_psy = devm_power_supply_register(chip->dev, &chip->oem_usb_psy_desc,
											   &chg_drv_cfg);
	if (IS_ERR(chip->oem_usb_psy)) {
		ret = PTR_ERR(chip->oem_usb_psy);
		CHG_DRV_ERRLOG("[%s] power_supply_register %s failed ret = %d\n", __func__, OEM_CHG_USB_PSY_NAME, ret);
		goto oem_chg_drv_err_power_supply_register_usb;
	}

	chip->oem_mains_psy_desc.name = OEM_CHG_AC_PSY_NAME;
	chip->oem_mains_psy_desc.type = POWER_SUPPLY_TYPE_OEM_AC;
	chip->oem_mains_psy_desc.get_property = chg_drv_common_get_property;
	chip->oem_mains_psy_desc.properties = chg_drv_common_properties;
	chip->oem_mains_psy_desc.num_properties = ARRAY_SIZE(chg_drv_common_properties);
	chg_drv_cfg.drv_data = chip;
	chg_drv_cfg.of_node = chip->dev->of_node;
	chip->oem_mains_psy = devm_power_supply_register(chip->dev, &chip->oem_mains_psy_desc,
												 &chg_drv_cfg);
	if (IS_ERR(chip->oem_mains_psy)) {
		ret = PTR_ERR(chip->oem_mains_psy);
		CHG_DRV_ERRLOG("[%s] power_supply_register %s failed ret = %d\n", __func__, OEM_CHG_AC_PSY_NAME, ret);
		goto oem_chg_drv_err_power_supply_register_mains;
	}

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	chip->oem_holder_psy_desc.name = OEM_CHG_HOLDER_PSY_NAME;
	chip->oem_holder_psy_desc.type = POWER_SUPPLY_TYPE_OEM_HOLDER;
	chip->oem_holder_psy_desc.get_property = chg_drv_common_get_property;
	chip->oem_holder_psy_desc.properties = chg_drv_common_properties;
	chip->oem_holder_psy_desc.num_properties = ARRAY_SIZE(chg_drv_common_properties);
	chg_drv_cfg.drv_data = chip;
	chg_drv_cfg.of_node = chip->dev->of_node;
	chip->oem_holder_psy = devm_power_supply_register(chip->dev, &chip->oem_holder_psy_desc,
												  &chg_drv_cfg);
	if (IS_ERR(chip->oem_holder_psy)) {
		ret = PTR_ERR(chip->oem_holder_psy);
		CHG_DRV_ERRLOG("[%s] power_supply_register %s failed ret = %d\n", __func__, OEM_CHG_HOLDER_PSY_NAME, ret);
		goto oem_chg_drv_err_power_supply_register_holder;
	}
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	platform_set_drvdata(pdev, chip);

#ifdef CHG_DRV_CONFIG_USE_APNV_VMAX
	ret = get_nonvolatile((u8 *)&chip->vmax_adj, APNV_CHG_ADJ_VMAX_I, 1);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get_nonvolatile failed. ret = %d\n", __func__, ret);
		chip->vmax_adj = chip->dt_param.chg_voltage;
	}
#else
	chip->vmax_adj = chip->dt_param.chg_voltage;
#endif

	ret = chg_drv_get_longevity_chg_mode(chip);
	if (unlikely(ret < 0)) {
		CHG_DRV_ERRLOG("[%s] get_longevity_charge_mode failed. ret = %d\n", __func__, ret);
	}

	ret = chg_drv_pmic_initial_setting(chip);
	if (unlikely(ret != 0)) {
		CHG_DRV_ERRLOG("[%s] pmic initial setting failed ret = %d\n", __func__, ret);
		ret = -EPROBE_DEFER;
		goto oem_chg_drv_err_pmic_initial_setting;
	}

	oem_chg_drv_chip = chip;

	chg_drv_timer_wake_lock = wakeup_source_register(&pdev->dev, "oem_chg_drv_timer");

	chip->chg_thread = kthread_run(oem_chg_drv_thread, oem_chg_drv_chip, "oem_chg");

	CHG_DRV_INFOLOG("[%s] probe End\n", __func__);

	return 0;

oem_chg_drv_err_pmic_initial_setting:

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	if (chip->oem_holder_psy)
		power_supply_unregister(chip->oem_holder_psy);

oem_chg_drv_err_power_supply_register_holder:
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	if (chip->oem_mains_psy)
		power_supply_unregister(chip->oem_mains_psy);

oem_chg_drv_err_power_supply_register_mains:

	if (chip->oem_usb_psy)
		power_supply_unregister(chip->oem_usb_psy);

oem_chg_drv_err_power_supply_register_usb:

	if (chip->oem_chg_psy)
		power_supply_unregister(chip->oem_chg_psy);

oem_chg_drv_err_power_supply_register_oem_chg:

	if (chip->chg_drv_wq)
		destroy_workqueue(chip->chg_drv_wq);

oem_chg_drv_err_create_singlethread_workqueue:

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
oem_chg_drv_err_find_votable_dc_icl:
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
oem_chg_drv_err_find_votable_usb_icl:
oem_chg_drv_err_find_votable_chg_disable:
oem_chg_drv_err_find_votable_fv:
oem_chg_drv_err_find_votable_fcc:

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
oem_chg_drv_err_power_supply_get_by_name_dc:
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */
oem_chg_drv_err_power_supply_get_by_name_usb:
oem_chg_drv_err_power_supply_get_by_name_oem_battery:
oem_chg_drv_err_power_supply_get_by_name_batt:
oem_chg_drv_err_power_supply_get_by_name_bms:

oem_chg_drv_err_parse_dt:

	kfree(chip);

	return ret;
}

/**
 * chg_drv_remove
 *
 * @param	pdev :
 *
 * @retval	Processing result
 */
static int chg_drv_remove(struct platform_device *pdev)
{
	struct chg_drv_chip *chip = platform_get_drvdata(pdev);

#ifdef CHG_DRV_CONFIG_HOLDER_ENABLE
	if (chip->oem_holder_psy)
		power_supply_unregister(chip->oem_holder_psy);
#endif /* CHG_DRV_CONFIG_HOLDER_ENABLE */

	if (chip->oem_mains_psy)
		power_supply_unregister(chip->oem_mains_psy);

	if (chip->oem_usb_psy)
		power_supply_unregister(chip->oem_usb_psy);

	wakeup_source_unregister(chg_drv_timer_wake_lock);
	kthread_stop(chip->chg_thread);

	destroy_workqueue(chip->chg_drv_wq);

	oem_chg_drv_chip = NULL;
	platform_set_drvdata(pdev, NULL);
	kfree(chip);

	return 0;
}

#ifdef CONFIG_PM
/**
 * chg_drv_suspend
 *
 * @param	pdev :
 *
 * @retval	Processing result
 */
static int chg_drv_suspend(struct device *pdev)
{
	return 0;
}

/**
 * chg_drv_resume
 *
 * @param	pdev :
 *
 * @retval	Processing result
 */
static int chg_drv_resume(struct device *pdev)
{
	return 0;
}

static const struct dev_pm_ops oem_chg_drv_pm_ops = {
	.suspend = chg_drv_suspend,
	.resume = chg_drv_resume,
};

#else  /* CONFIG_PM */
#define chg_drv_suspend NULL
#define chg_drv_resume  NULL
#endif /* CONFIG_PM */

/**
 * chg_drv_shutdown
 *
 * @param	pdev :
 */
static void chg_drv_shutdown(struct platform_device *pdev)
{
	struct chg_drv_chip *chip = platform_get_drvdata(pdev);

	CHG_DRV_DBGLOG("[%s] in\n", __func__);

	chg_drv_terminate(chip, true);

	return;
}

static struct of_device_id chg_drv_match_table[] = {
	{ .compatible = "fcnt,oem_charger_drv",},
	{ },
};

static struct platform_driver oem_charger_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "oem_charge_drv",
		.of_match_table = chg_drv_match_table,
#ifdef CONFIG_PM
		.pm = &oem_chg_drv_pm_ops,
#endif /* CONFIG_PM */
		   },
	.probe		= chg_drv_probe,
	.remove		= chg_drv_remove,
	.shutdown	= chg_drv_shutdown,
#ifndef CONFIG_PM
	.suspend	= chg_drv_suspend,
	.resume		= chg_drv_resume,
#endif /* !CONFIG_PM */
};

/**
 * chg_drv_init
 *
 * @retval	Processing result
 */
static int __init chg_drv_init(void)
{
	int i;

	INIT_LIST_HEAD(&oem_chg_drv_evt_list_free);
	INIT_LIST_HEAD(&oem_chg_drv_evt_list_exec);
	for (i = 0; i < CHG_DRV_EVT_QUEUE_MAX; i++) {
		list_add_tail(&oem_charge_evt_queue[i].head, &oem_chg_drv_evt_list_free);
	}

	return platform_driver_register(&oem_charger_driver);
}
module_init(chg_drv_init);

/**
 * chg_drv_exit
 */
static void __exit chg_drv_exit(void)
{
	platform_driver_unregister(&oem_charger_driver);
}
module_exit(chg_drv_exit);

MODULE_AUTHOR("FUJITSU CONNECTED TECHNOLOGIES");
MODULE_DESCRIPTION("OEM Charger Driver");
MODULE_LICENSE("GPL");
