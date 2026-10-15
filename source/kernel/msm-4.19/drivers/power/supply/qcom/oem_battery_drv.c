/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
// SPDX-License-Identifier: GPL-2.0+

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/nonvolatile_common.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/pm_wakeup.h>
#include <linux/custom_mode.h>
#include <linux/wait.h>
#include <linux/printk.h>

#include <linux/mfd/oem_charger.h>
#include "oem_charger_local.h"

#define BATT_DRV_DBGLOG(x, y...)				\
	if (unlikely(batt_drv_debug != 0)) {		\
		printk(KERN_ERR "[oem_batt] " x, ## y);	\
	}

#define BATT_DRV_INFOLOG(x, y...)				printk(KERN_INFO "[oem_batt] " x, ## y)
#define BATT_DRV_WARNLOG(x, y...)				printk(KERN_WARNING "[oem_batt] " x, ## y)
#define BATT_DRV_ERRLOG(x, y...)				printk(KERN_ERR "[oem_batt] " x, ## y)
#define BATT_DRV_RECLOG(x, y...)				printk("REC@REC@36[oem_batt] " x, ## y)

/* monitor time */
#define BATT_DRV_MONITOR_DELAY_100MS			msecs_to_jiffies(100)
#define BATT_DRV_MONITOR_DELAY_500MS			msecs_to_jiffies(500)
#define BATT_DRV_MONITOR_DELAY_1S				msecs_to_jiffies(1000)
#define BATT_DRV_MONITOR_DELAY_10S				msecs_to_jiffies(10000)

#define BATT_DRV_TIMER_10S						(10 * HZ)
#define BATT_DRV_TIMER_30S						(30 * HZ)
#define BATT_DRV_TIMER_60S						(60 * HZ)
#define BATT_DRV_TIMER_180S						(180 * HZ)

/* thread events */
#define BATT_DRV_EVENT_SUSPEND					0
#define BATT_DRV_EVENT_MONITOR					1

/* battery monitor time */
#define BATT_DRV_MONITOR_INTERVAL				BATT_DRV_MONITOR_DELAY_10S
#define BATT_DRV_RESUME_INTERVAL				BATT_DRV_MONITOR_DELAY_500MS
#define BATT_DRV_LOW_VOLTAGE_INTERVAL			BATT_DRV_MONITOR_DELAY_1S
#define BATT_DRV_SHUTDOWN_VOLTAGE_INTERVAL		BATT_DRV_MONITOR_DELAY_100MS

#define BATT_SOC_INIT							101
#define BATT_MSOC_INIT							0xfff
#define BATT_SOC_FULL							100
#define BATT_SOC_ZERO							0
#define BATT_AGE_OFFSET							100

#define BATT_CYCLE_COUNT_BACKUP_THRESHOLD		10
#define BATT_ADJUST_TEMP_TBL_MAX				10

#define BATT_CYCLE_COUNT_HI_SOC_DEFAULT			90
#define BATT_CYCLE_COUNT_HI_TEMP_DEFAULT		400
#define BATT_CYCLE_CHARGE_DEFAULT				100

#define BATT_RECHARGE_SOC_DEFAULT				98
#define BATT_FULL_KEEP_DEFAULT					99
#define BATT_LONGEVITY_FULL_SOC_MIN_DEFAULT		85
#define BATT_LONGEVITY_FULL_SOC_MAX_DEFAULT		87
#define BATT_LONGEVITY_RECHARGE_SOC_DEFAULT		85
#define BATT_LOW_VOLTAGE_DEFAULT				3600
#define BATT_SHUTDOWN_VOLTAGE_DEFAULT			3200
#define BATT_SHUTDOWN_COUNT_DEFAULT				3

static char batt_drv_status_str[POWER_SUPPLY_STATUS_MAX][16] = {
	"UNKNOWN",
	"CHARGING",
	"DISCHARGING",
	"NOT_CHARGING",
	"FULL",
};

static char batt_drv_health_str[POWER_SUPPLY_HEALTH_MAX][32] = {
	"UNKNOWN",
	"GOOD",
	"OVERHEAT",
	"DEAD",
	"OVERVOLTAGE",
	"UNSPEC_FAILURE",
	"COLD",
	"WATCHDOG_TIMER_EXPIRE",
	"SAFETY_TIMER_EXPIRE",
	"WARM",
	"COOL",
	"HOT",
	"UNDER_SUPPLY_VOLTAGE",
	"OVER_SUPPLY_VOLTAGE",
	"HOLDER_ERROR",
	"USB_HOT",
	"USB_ERROR",
	"OTHER_ERROR"
};

static enum power_supply_property batt_drv_properties[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_INITIALIZE,
	POWER_SUPPLY_PROP_AGE,
	POWER_SUPPLY_PROP_LONGEVITY_CHG_MODE,
};

enum batt_drv_func_limit {
	BATT_DRV_FUNC_LIMIT_CHARGE = 0,			/*  0 */
	BATT_DRV_FUNC_LIMIT_BATTERY_PRESENT,	/*  1 */
	BATT_DRV_FUNC_LIMIT_LOW_BATTERY,		/*  2 */
	BATT_DRV_FUNC_LIMIT_RESERVE_01,			/*  3 */
	BATT_DRV_FUNC_LIMIT_BATTERY_TEMP,		/*  4 */
	BATT_DRV_FUNC_LIMIT_RESERVE_02,			/*  5 */
	BATT_DRV_FUNC_LIMIT_RESERVE_03,			/*  6 */
	BATT_DRV_FUNC_LIMIT_RESERVE_04,			/*  7 */
	BATT_DRV_FUNC_LIMIT_NUM,
};

enum batt_drv_keep_capacity {
	BATT_DRV_KEEP_CAPACITY_DISABLE = 0,
	BATT_DRV_KEEP_CAPACITY_STANDBY,
	BATT_DRV_KEEP_CAPACITY_ENABLE,
};

struct charging_setting {
	uint8_t		hi_capacity_cycle_ratio;
	uint8_t		hi_temp_cycle_ratio;
	uint8_t		unused1;
	uint8_t		unused2;
	uint8_t		unused3;
	uint8_t		unused4;
};

struct charging_data {
	uint32_t	charge_count;
	uint32_t	hi_capacity_count;
	uint32_t	hi_temp_count;
	uint8_t		unused1;
	uint8_t		unused2;
	uint16_t	unused3;
};

typedef struct {
	int			recharge_soc_threshold;
	int			full_keep_threshold;

	int			low_voltage_threshold;
	int			shutdown_voltage_threshold;
	int			shutdown_count;

	int			longevity_full_soc_min_threshold;
	int			longevity_full_soc_max_threshold;
	int			longevity_recharge_soc_threshold;

	int			cycle_count_hi_soc_threshold;
	int			cycle_count_hi_temp_threshold;
	int			cycle_charge_threshold_lv1;
	int			cycle_charge_threshold_lv2;

	int			temp_correction_num;
	int			temp_correction_values[BATT_ADJUST_TEMP_TBL_MAX];
	int			temp_correction_thresholds[BATT_ADJUST_TEMP_TBL_MAX];
} batt_drv_dt;

struct battery_drv_chip {
	struct power_supply_desc	battery_psy_desc;
	struct power_supply			*battery_psy;

	struct power_supply			*bms_psy;

	struct device				*dev;
	batt_drv_dt					dt_param;

	struct workqueue_struct		*batt_drv_wq;
	struct delayed_work			battery_monitor;

	struct wakeup_source		*drv_status; 

	int							status;
	int							health;
	int							present;
	int							soc;
	int							fake_soc;
	int							old_msoc;
	int							vcell;
	int							age;

	/* monitor event state */
	unsigned long				event_state;

	unsigned long				keep_capacity_flag;
	unsigned long				keep_capacity_time;

	unsigned int				shutdown_voltage_count;
	unsigned int				start_soc_value;

	/* charging data */
	struct charging_setting		chg_setting;
	struct charging_data		chg_data;
	int							total_cycle_count;
	chg_cycle_level				cycle_chg_lvl;			
	unsigned int				incremental_soc;
	unsigned int				current_soc;
	bool						longevity_charge_flag;

	bool						check_disable[BATT_DRV_FUNC_LIMIT_NUM];

	struct mutex				lock;
	wait_queue_head_t			wq;
	void*						thread;
};

const static int capacity_convert[256] =
{
/*   00   01   02   03   04   05   06   07   08   09   0A   0B   0C   0D   0E   0F                   */
	  0,   0,   0,   1,   1,   1,   2,   2,   2,   3,   3,   3,   4,   4,   4,   5,		/* 0x00-0x0F */
	  5,   6,   6,   6,   7,   7,   8,   8,   9,   9,  10,  10,  10,  11,  11,  12,		/* 0x10-0x1F */
	 12,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,  20,		/* 0x20-0x2F */
	 20,  21,  21,  22,  22,  23,  23,  23,  24,  24,  25,  25,  26,  26,  27,  27,		/* 0x30-0x3F */
	 28,  28,  29,  29,  30,  30,  31,  31,  31,  32,  32,  33,  33,  34,  34,  35,		/* 0x40-0x4F */
	 35,  35,  36,  36,  37,  37,  38,  38,  38,  39,  39,  40,  40,  40,  41,  41,		/* 0x50-0x5F */
	 42,  42,  43,  43,  43,  44,  44,  45,  45,  45,  46,  46,  46,  47,  47,  48,		/* 0x60-0x6F */
	 48,  48,  49,  49,  50,  50,  50,  51,  51,  52,  52,  52,  53,  53,  53,  54,		/* 0x70-0x7F */
	 54,  55,  55,  55,  56,  56,  56,  57,  57,  58,  58,  58,  59,  59,  59,  60,		/* 0x80-0x8F */
	 60,  61,  61,  61,  62,  62,  63,  63,  63,  64,  64,  64,  65,  65,  66,  66,		/* 0x90-0x9F */
	 66,  67,  67,  67,  68,  68,  69,  69,  69,  70,  70,  70,  71,  71,  72,  72,		/* 0xA0-0xAF */
	 72,  73,  73,  73,  74,  74,  75,  75,  75,  76,  76,  76,  77,  77,  77,  78,		/* 0xB0-0xBF */
	 78,  79,  79,  79,  80,  80,  80,  81,  81,  81,  82,  82,  83,  83,  83,  84,		/* 0xC0-0xCF */
	 84,  84,  85,  85,  85,  86,  86,  86,  87,  87,  87,  88,  88,  88,  89,  89,		/* 0xD0-0xDF */
	 89,  90,  90,  90,  91,  91,  91,  92,  92,  92,  93,  93,  93,  94,  94,  94,		/* 0xE0-0xEF */
	 95,  95,  95,  96,  96,  96,  97,  97,  97,  97,  98,  98,  98,  99,  99, 100,		/* 0xF0-0xFF */
};

const static int capacity_convert_cycle_chg[256] =
{
/*   00   01   02   03   04   05   06   07   08   09   0A   0B   0C   0D   0E   0F                   */
	  0,   0,   0,   1,   1,   1,   2,   2,   2,   3,   3,   3,   4,   4,   4,   5,		/* 0x00-0x0F */
	  5,   6,   6,   6,   7,   7,   8,   8,   9,   9,  10,  10,  10,  11,  11,  12,		/* 0x10-0x1F */
	 12,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,  20,		/* 0x20-0x2F */
	 20,  21,  21,  22,  22,  23,  23,  23,  24,  24,  25,  25,  26,  26,  27,  27,		/* 0x30-0x3F */
	 28,  28,  29,  29,  30,  30,  31,  31,  31,  32,  32,  33,  33,  34,  34,  35,		/* 0x40-0x4F */
	 35,  35,  36,  36,  37,  37,  38,  38,  38,  39,  39,  40,  40,  40,  41,  41,		/* 0x50-0x5F */
	 42,  42,  43,  43,  43,  44,  44,  45,  45,  45,  46,  46,  46,  47,  47,  48,		/* 0x60-0x6F */
	 48,  48,  49,  49,  50,  50,  50,  51,  51,  52,  52,  52,  53,  53,  53,  54,		/* 0x70-0x7F */
	 54,  55,  55,  55,  56,  56,  56,  57,  57,  58,  58,  58,  59,  59,  59,  60,		/* 0x80-0x8F */
	 60,  61,  61,  61,  62,  62,  63,  63,  63,  64,  64,  64,  65,  65,  66,  66,		/* 0x90-0x9F */
	 66,  67,  67,  67,  68,  68,  69,  69,  69,  70,  70,  70,  71,  71,  72,  72,		/* 0xA0-0xAF */
	 72,  73,  73,  73,  74,  74,  75,  75,  75,  76,  76,  76,  77,  77,  77,  78,		/* 0xB0-0xBF */
	 78,  79,  79,  79,  80,  80,  80,  81,  81,  81,  82,  82,  83,  83,  84,  84,		/* 0xC0-0xCF */
	 84,  85,  85,  86,  86,  86,  87,  87,  88,  88,  88,  89,  89,  90,  90,  90,		/* 0xD0-0xDF */
	 91,  91,  92,  92,  92,  93,  93,  94,  94,  94,  95,  95,  96,  96,  96,  97,		/* 0xE0-0xEF */
	 97,  98,  98,  98,  99,  99, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,		/* 0xF0-0xFF */
};

static void batt_drv_cycle_count_start(struct battery_drv_chip *chip);
static void batt_drv_cycle_count_update(struct battery_drv_chip *chip);
static void batt_drv_cycle_count_finish(struct battery_drv_chip *chip);

static struct battery_drv_chip *batt_chip = NULL;
static int drv_initialized = 0; /* Driver init flag (0:no initialize, 1:initialized) */

static int battery_health = POWER_SUPPLY_HEALTH_GOOD;
static int battery_status = POWER_SUPPLY_STATUS_DISCHARGING;

static unsigned char soc_mask_flag = 0;
static unsigned long soc_mask_expire = 0;
static unsigned char soc_charging_mask_flag = 0;
static unsigned long soc_charging_mask_expire = 0;

extern int charging_mode;

static int fake_status = POWER_SUPPLY_STATUS_MAX;
static int batt_drv_set_fake_status(const char *val, const struct kernel_param *kp)
{
	int result = 0;

	result = param_set_int(val, kp);
	if (result) {
		BATT_DRV_ERRLOG("[%s] error setting value %d\n",__func__, result);
		fake_status = POWER_SUPPLY_STATUS_MAX;
	}

	return result;
}
module_param_call(dbg_status, batt_drv_set_fake_status, param_get_uint, &fake_status, 0644);

static int fake_health = POWER_SUPPLY_HEALTH_MAX;
static int batt_drv_set_fake_health(const char *val, const struct kernel_param *kp)
{
	int result = 0;

	result = param_set_int(val, kp);
	if (result) {
		BATT_DRV_ERRLOG("[%s] error setting value %d\n",__func__, result);
		fake_health = POWER_SUPPLY_HEALTH_MAX;
	}

	return result;
}
module_param_call(dbg_health, batt_drv_set_fake_health, param_get_uint, &fake_health, 0644);

static int batt_drv_debug = 0;
module_param_named(dbglog_battery, batt_drv_debug, int, S_IRUGO|S_IWUSR);

static void batt_drv_function_limits_check(struct battery_drv_chip *chip)
{
	int result = 0;
	u16 val = 0;
	int i = 0;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (unlikely(chip == NULL)) {
		BATT_DRV_WARNLOG("[%s] chip pointer is NULL\n", __func__);
		if (unlikely(batt_chip == NULL)) {
			BATT_DRV_ERRLOG("[%s] batt_chip pointer is NULL\n", __func__);
			return;
		}
		chip = batt_chip;
	}

	result = get_nonvolatile((uint8_t*)&val, APNV_CHARGE_FG_FUNC_LIMITS_I, 2);
	if (unlikely(result < 0)) {
		val = 0x0000;
		BATT_DRV_ERRLOG("[%s] NV read err : %d set value = 0x%x \n", __func__, result, val);
	}

	/*
	 * Limits function classification
	 * check_disable[0] : charge
	 * check_disable[1] : battery present
	 * check_disable[2] : low battery
	 * check_disable[3] : reserve
	 * check_disable[4] : battery temperature
	 * check_disable[5] : reserve
	 * check_disable[6] : reserve
	 * check_disable[7] : reserve
	 */

	if (val != 0x0000) {
		for (i = 0;i < BATT_DRV_FUNC_LIMIT_NUM;i++) {
			if (val & 0x0001) {
				chip->check_disable[i] = true;
			} else {
				chip->check_disable[i] = false;
			}
			val = val >> 1;
			BATT_DRV_DBGLOG("[%s] check_disable[%d] = %d\n", __func__, i, chip->check_disable[i]);
		}
	}
}

static int batt_drv_get_longevity_chg_mode(struct battery_drv_chip *chip)
{
	int result = 0;
	u8 val = 0;

	result = get_nonvolatile((u8 *)&val, APNV_ITEM_LONGEVITYCHARGE, 1);
	if (unlikely(result < 0)) {
		BATT_DRV_ERRLOG("[%s] get_nonvolatile failed. result = %d\n", __func__, result);
		chip->longevity_charge_flag = false;
	} else {
		if (val == 0x01) {
			chip->longevity_charge_flag = true;
		} else {
			chip->longevity_charge_flag = false;
		}
		result = (int)chip->longevity_charge_flag;
	}

	return result;
}
static void batt_drv_get_charge_cycle(struct battery_drv_chip *chip)
{
	int result = 0;
	struct charging_setting *chg_setting = &chip->chg_setting;
	struct charging_data *chg_data = &chip->chg_data;
	union power_supply_propval pval = {0,};

	/* get the charging data from nv */
	result = get_nonvolatile((uint8_t*)chg_data, APNV_CHARGING_DATA, sizeof(struct charging_data));
	if (unlikely(result < 0)) {
		BATT_DRV_ERRLOG("[%s] get nonvolatile %d error\n", __func__, APNV_CHARGING_DATA);
	}

	result = get_nonvolatile((uint8_t*)chg_setting, APNV_CHARGING_SETTING, sizeof(struct charging_setting));
	if (unlikely(result < 0)) {
		BATT_DRV_ERRLOG("[%s] get nonvolatile %d error\n", __func__, APNV_CHARGING_SETTING);
	}

	BATT_DRV_RECLOG("[%s]charge_count: %d, hi_capacity_count: %d, hi_temp_count: %d\n", __func__,
						chg_data->charge_count, chg_data->hi_capacity_count, chg_data->hi_temp_count);
	BATT_DRV_RECLOG("[%s]hi_capacity_cycle_ratio: %d, hi_temp_cycle_ratio: %d\n", __func__,
						chg_setting->hi_capacity_cycle_ratio, chg_setting->hi_temp_cycle_ratio);

	chip->total_cycle_count =
		chg_data->charge_count +
		(chg_data->hi_capacity_count * chg_setting->hi_capacity_cycle_ratio) +
		(chg_data->hi_temp_count * chg_setting->hi_temp_cycle_ratio);

	if ((chip->cycle_chg_lvl == CHG_CYCLE_LEVEL_01) &&
		(chip->total_cycle_count >= (chip->dt_param.cycle_charge_threshold_lv2*BATT_SOC_FULL))) {
		pval.intval = (int)CHG_CYCLE_LEVEL_02;
		power_supply_set_property(chip->bms_psy, POWER_SUPPLY_PROP_CYCLE_CHG, &pval);
		chip->cycle_chg_lvl = CHG_CYCLE_LEVEL_02;
	} else if ((chip->cycle_chg_lvl == CHG_CYCLE_LEVEL_NONE) &&
		(chip->total_cycle_count >= (chip->dt_param.cycle_charge_threshold_lv1*BATT_SOC_FULL))) {
		pval.intval = (int)CHG_CYCLE_LEVEL_01;
		power_supply_set_property(chip->bms_psy, POWER_SUPPLY_PROP_CYCLE_CHG, &pval);
		chip->cycle_chg_lvl = CHG_CYCLE_LEVEL_01;
	}

	return;
}

static int batt_drv_set_status(struct battery_drv_chip *chip, int status, int battery_capacity)
{
	int result = 0;
	bool full_led_flag = false;
	unsigned long now = jiffies;
	int old_status;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (status >= POWER_SUPPLY_STATUS_MAX) {
		BATT_DRV_ERRLOG("[%s] Parameter Error! status = %d\n", __func__, status);
		result = -1;
		goto batt_exit;
	}

	mutex_lock(&chip->lock);

	old_status = chip->status;

	if (chip->status != status) {
		if (status == POWER_SUPPLY_STATUS_CHARGING) {
			batt_drv_cycle_count_start(chip);
			if (chip->keep_capacity_flag == BATT_DRV_KEEP_CAPACITY_STANDBY) {
				chip->keep_capacity_time = jiffies;
				chip->keep_capacity_flag = BATT_DRV_KEEP_CAPACITY_ENABLE;
			}
			soc_mask_flag = 0;
		} else {
			if (chip->keep_capacity_flag == BATT_DRV_KEEP_CAPACITY_ENABLE) {
				chip->keep_capacity_flag = BATT_DRV_KEEP_CAPACITY_DISABLE;
			}
		}

		if (status == POWER_SUPPLY_STATUS_DISCHARGING) {
			batt_drv_cycle_count_finish(chip);

			if (chip->soc >= BATT_SOC_FULL) {
				/* 30 sec */
				soc_mask_expire = now + BATT_DRV_TIMER_30S;
				soc_mask_flag = 1;
				BATT_DRV_DBGLOG("[%s] now:%lu, expire:%lu\n", __func__, now, soc_mask_expire);
			}

			soc_charging_mask_flag = 0;
		}

		if ((drv_initialized == 1) &&
			(status == POWER_SUPPLY_STATUS_CHARGING)) {
			if (chip->status == POWER_SUPPLY_STATUS_FULL) {
				/* Do not update state */
				goto batt_update_skip;
			} else {
				if (chip->longevity_charge_flag) {
					if ((battery_capacity >= chip->dt_param.longevity_full_soc_min_threshold) ||
						(chip->soc >= chip->dt_param.longevity_full_soc_min_threshold)){
						full_led_flag = true;
					}
				} else {
					if ((battery_capacity >= BATT_SOC_FULL) ||
						(chip->soc >= BATT_SOC_FULL)) {
						full_led_flag = true;
					}
				}
				if (full_led_flag) {
					/* 10 sec */
					soc_charging_mask_expire = now + BATT_DRV_TIMER_10S;
					soc_charging_mask_flag = 1;
					chip->status = POWER_SUPPLY_STATUS_CHARGING;
					goto batt_update_skip;
				}
			}
		}

		chip->status = status;

batt_update_skip:
		BATT_DRV_INFOLOG("[%s] old_status:%s new_status:%s set_status:%s capa:%d volt:%dmV\n",
					__func__,
					batt_drv_status_str[old_status],
					batt_drv_status_str[chip->status],
					batt_drv_status_str[status],
					chip->soc,
					chip->vcell);

		power_supply_changed(chip->battery_psy);
	}

	mutex_unlock(&chip->lock);

batt_exit:
	return result;
}

static int batt_drv_set_health(struct battery_drv_chip *chip, int health)
{
	int result = 0;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (health >= POWER_SUPPLY_HEALTH_MAX) {
		BATT_DRV_ERRLOG("[%s] Parameter Error! health = %d\n", __func__, health);
		result = -1;
		goto batt_exit;
	}

	mutex_lock(&chip->lock);

	if (chip->health != health) {
		BATT_DRV_INFOLOG("[%s] old_health:%s new_status:%s capa:%d volt:%dmV\n", __func__,
								batt_drv_health_str[chip->health],
								batt_drv_health_str[health],
								chip->soc,
								chip->vcell);

		chip->health = health;
		result = 1;

		power_supply_changed(chip->battery_psy);
	}

	mutex_unlock(&chip->lock);

batt_exit:
	return result;
}

static int batt_drv_get_soc(struct battery_drv_chip *chip,
									   int *battery_capacity)
{
	int ret;
	union power_supply_propval pval = {0,};

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_MSOC_RAW, &pval) < 0) {
		/* Failure */
		BATT_DRV_ERRLOG("[%s] Get soc FAILURE!\n", __func__);
		ret = -1;
	} else {
	if (chip->cycle_chg_lvl == CHG_CYCLE_LEVEL_02) {		
			*battery_capacity = capacity_convert_cycle_chg[pval.intval];
		} else {
			*battery_capacity = capacity_convert[pval.intval];
		}

		if (chip->old_msoc != pval.intval) {
			BATT_DRV_INFOLOG("[%s] capacity:%d old_msoc:0x%03x -> msoc:0x%03x\n",
								__func__, *battery_capacity, chip->old_msoc, pval.intval);
			chip->old_msoc = pval.intval;
		}

		ret = 0;
	}

	return ret;
}

static int batt_drv_set_soc(struct battery_drv_chip *chip,
									   int battery_capacity)
{
	int old_soc;
	unsigned long now = jiffies;
	int old_status;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (chip->cycle_chg_lvl != CHG_CYCLE_LEVEL_NONE) {		
	  chip->soc = battery_capacity;
	}

	old_soc = chip->soc;
	old_status = chip->status;

	if (chip->shutdown_voltage_count >= chip->dt_param.shutdown_count) {
		chip->soc = BATT_SOC_ZERO;
	} else if ((chip->status == POWER_SUPPLY_STATUS_CHARGING) && (soc_charging_mask_flag == 1)) {
		if (time_before(soc_charging_mask_expire, now)) {
			soc_charging_mask_flag = 0;
			chip->status = POWER_SUPPLY_STATUS_FULL;
		}
	} else if (chip->status == POWER_SUPPLY_STATUS_FULL) {
		if (chip->longevity_charge_flag) {
			if (battery_capacity < chip->dt_param.longevity_recharge_soc_threshold) {
				chip->soc = battery_capacity;
				chip->status = POWER_SUPPLY_STATUS_CHARGING;
			} else if (battery_capacity < chip->soc) {
				chip->soc--;
			} else if (battery_capacity > chip->dt_param.longevity_full_soc_max_threshold) {

			} else {
				chip->soc = chip->dt_param.longevity_full_soc_min_threshold;
			}
		} else {
			if (battery_capacity < chip->dt_param.recharge_soc_threshold) {
				chip->soc = battery_capacity;
				chip->status = POWER_SUPPLY_STATUS_CHARGING;
			} else {
				chip->soc = BATT_SOC_FULL;
			}
		}
	} else if (time_before(now, soc_mask_expire) && soc_mask_flag) {
		BATT_DRV_DBGLOG("[%s] not update soc indicator\n", __func__);
		if (time_before((now + BATT_DRV_TIMER_60S), soc_mask_expire)) {
			BATT_DRV_RECLOG("[%s] Error!!! soc_mask_expire is Unknown Value. now:%lu, expire:%lu\n", __func__, now, soc_mask_expire);
			soc_mask_flag = 0;
			soc_mask_expire = 0;
		}
	} else if (chip->status == POWER_SUPPLY_STATUS_CHARGING) {
		if (battery_capacity < chip->soc) {
			chip->soc--;
		} else {
			chip->soc = battery_capacity;
			if (chip->longevity_charge_flag) {
				if (chip->soc >= chip->dt_param.longevity_full_soc_min_threshold) {
					chip->status = POWER_SUPPLY_STATUS_FULL;
				}
			} else {
				if (chip->soc >= BATT_SOC_FULL) {
					chip->status = POWER_SUPPLY_STATUS_FULL;
				}
			}
		}
	} else {
		soc_mask_flag = 0;
		soc_mask_expire = 0;
		if (battery_capacity < chip->soc) {
			chip->soc--;
		}
		else {
			chip->soc = battery_capacity;
		}
	}
	if (chip->longevity_charge_flag) {
		if ((chip->soc >= chip->dt_param.longevity_full_soc_min_threshold) &&
			(chip->soc <= chip->dt_param.longevity_full_soc_max_threshold)) {
			chip->soc = chip->dt_param.longevity_full_soc_min_threshold;
		}
	}

	if ((old_soc != chip->soc) ||
		(old_status != chip->status)){
		BATT_DRV_INFOLOG("[%s] old_soc:%d new_soc:%d set_soc:%d old_status:%s new_status:%s volt:%dmV\n",
						   __func__, old_soc, chip->soc, battery_capacity,
						   batt_drv_status_str[old_status],
						   batt_drv_status_str[chip->status], chip->vcell);
	}

	return 0;
}

static int batt_drv_get_voltage(struct battery_drv_chip *chip, int *battery_voltage)
{
	int result = 0;
	union power_supply_propval pval = {0,};

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (unlikely(battery_voltage == NULL)) {
		BATT_DRV_ERRLOG("[%s] battery_voltage pointer is NULL\n", __func__);
		result = -1;
		goto batt_exit;
	}

	if (power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_VOLTAGE_NOW, &pval) < 0) {
		BATT_DRV_ERRLOG("[%s] get prop FAILURE bms_psy battery voltage\n", __func__);
		*battery_voltage = 0;
		result = -1;
		goto batt_exit;
	} else {
		*battery_voltage = pval.intval / 1000;
	}

batt_exit:
	return result;
}

static int batt_drv_get_temperature(struct battery_drv_chip *chip, int *battery_temperature)
{
	int result = 0;
	union power_supply_propval pval = {0,};
	int i;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (unlikely(battery_temperature == NULL)) {
		BATT_DRV_ERRLOG("[%s] battery_temperature pointer is NULL\n", __func__);
		result =  -1;
		goto batt_exit;
	}

	if (power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_TEMP, &pval) < 0) {
		BATT_DRV_ERRLOG("[%s] get prop FAILURE battery temperature\n", __func__);
		*battery_temperature = 0;
		result = -1;
		goto batt_exit;
	} else {
		for (i = 0; i < chip->dt_param.temp_correction_num; i++) {
			if (pval.intval < chip->dt_param.temp_correction_thresholds[i]) {
				break;
			}
		}
		*battery_temperature = pval.intval - chip->dt_param.temp_correction_values[i];
	}

batt_exit:
	return result;
}

static void batt_drv_get_age(struct battery_drv_chip *chip)
{
	int result = 0;
	u16 age_val = 0;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	result = get_nonvolatile((uint8_t*)&age_val, APNV_CHARGE_FG_AGE_I, 2);
	if ((result < 0) || (age_val > 100)) {

		chip->age = BATT_AGE_OFFSET;
		BATT_DRV_ERRLOG("[%s] APNV read err : %d %d set age = 0x%x \n", __func__, result, age_val, chip->age);
	} else {
		chip->age = (BATT_AGE_OFFSET - age_val);
		BATT_DRV_INFOLOG("[%s] APNV read  : %d set age = 0x%x \n", __func__, result, chip->age);
	}

	return;
}

static void batt_drv_cycle_count_start(struct battery_drv_chip *chip)
{
	int result = 0;
	int battery_capacity = 0;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (drv_initialized == 0) {
		goto batt_exit;
	}

	if (chip->start_soc_value < BATT_SOC_INIT) {
		goto batt_exit;
	}

	result = batt_drv_get_soc(chip, &battery_capacity);
	if (unlikely(result < 0)) {
		goto batt_exit;
	}

	chip->incremental_soc = 0;
	chip->current_soc = battery_capacity;
	chip->start_soc_value = battery_capacity;

batt_exit:
    BATT_DRV_DBGLOG("[chg_cycle] Charging, age=%d, charge_count=%d, hi_capacity_count=%d, hi_temp_count=%d\n",
					  chip->age, chip->chg_data.charge_count, chip->chg_data.hi_capacity_count, chip->chg_data.hi_temp_count);
	return;

}

static void batt_drv_cycle_count_update(struct battery_drv_chip *chip)
{
	int result = 0;
	union power_supply_propval pval = {0,};
	int batt_temp = 0;
	struct charging_data chg_data;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (chip->current_soc == BATT_SOC_INIT) {
		goto batt_exit;
	}

	if (chip->current_soc == chip->soc) {
		goto batt_exit;
	}

	if (power_supply_get_property(chip->bms_psy, POWER_SUPPLY_PROP_TEMP, &pval) < 0) {
		/* Failure */
		BATT_DRV_ERRLOG("[%s] Get temp FAILURE!\n", __func__);
	} else {
		batt_temp = pval.intval;
	}

	if (chip->current_soc < chip->soc) {
		chip->incremental_soc += (chip->soc - chip->current_soc);
		chip->chg_data.charge_count += (chip->soc - chip->current_soc);

		if (batt_temp >= chip->dt_param.cycle_count_hi_temp_threshold) {
			chip->chg_data.hi_temp_count += (chip->soc - chip->current_soc);
		}

		if (chip->soc >= chip->dt_param.cycle_count_hi_soc_threshold) {
			chip->chg_data.hi_capacity_count += (chip->soc - chip->current_soc);
		}

		if (chip->incremental_soc >= BATT_CYCLE_COUNT_BACKUP_THRESHOLD) {
			memset(&chg_data, 0, sizeof(struct charging_data));
			chg_data.charge_count = chip->chg_data.charge_count;
			chg_data.hi_capacity_count = chip->chg_data.hi_capacity_count;
			chg_data.hi_temp_count = chip->chg_data.hi_temp_count;

			result = set_nonvolatile((uint8_t*)&chg_data, APNV_CHARGING_DATA, sizeof(struct charging_data));
			if (unlikely(result < 0)) {
				BATT_DRV_ERRLOG("[%s] set nonvolatile %d error\n", __func__, APNV_CHARGING_DATA);
			}
			chip->incremental_soc = 0;
		}
	}
	chip->current_soc = chip->soc;

batt_exit:
	return;
}

static void batt_drv_cycle_count_finish(struct battery_drv_chip *chip)
{
	int result = 0;
	int battery_capacity = 0;
	u16 count_val = 0;
	u16 age_val = 0;
	struct charging_data chg_data;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (drv_initialized == 0) {
		goto batt_exit;
	}

	/* save the charging data to nv */
	memset(&chg_data, 0, sizeof(struct charging_data));
	chg_data.charge_count = chip->chg_data.charge_count;
	chg_data.hi_capacity_count = chip->chg_data.hi_capacity_count;
	chg_data.hi_temp_count = chip->chg_data.hi_temp_count;

	result = set_nonvolatile((uint8_t*)&chg_data, APNV_CHARGING_DATA, sizeof(struct charging_data));
	if (unlikely(result < 0)) {
		BATT_DRV_ERRLOG("[%s] set nonvolatile %d error\n", __func__, APNV_CHARGING_DATA);
	}

	/* update the age */
	result = batt_drv_get_soc(chip, &battery_capacity);
	if (unlikely(result < 0)) {
		goto batt_exit;
	}

	if (battery_capacity <= chip->start_soc_value) {
		goto batt_exit;
	}

	result = get_nonvolatile((uint8_t*)&count_val, APNV_CHARGE_FG_VF_COUNT_I, 2);
	if (result == (-nv_not_active)) {
		count_val = 0;
		result = set_nonvolatile((uint8_t*)&count_val, APNV_CHARGE_FG_VF_COUNT_I, 2);
		if (unlikely(result < 0)) {
			goto batt_exit;
		}
	} else if (unlikely(result < 0)) {
		goto batt_exit;
	}

	count_val += (battery_capacity - chip->start_soc_value);

	if (count_val >= 2000) {
		count_val -= 2000;

		age_val = chip->age;

		if ((age_val > 0) && (age_val <= 100)) {
			age_val--;
		} else {
			BATT_DRV_ERRLOG("[%s] AGE Error age_val = %d\n", __func__, age_val);
			goto batt_exit;
		}
		age_val = (BATT_AGE_OFFSET - age_val);
		result = set_nonvolatile((uint8_t*)&age_val, APNV_CHARGE_FG_AGE_I, 2);
		if (unlikely(result < 0)) {
			goto batt_exit;
		}
		chip->age = (BATT_AGE_OFFSET - age_val);

		BATT_DRV_RECLOG("[%s] AGE UPDATE set value = 0x%04X \n", __func__, age_val);
	}

	result = set_nonvolatile((uint8_t*)&count_val, APNV_CHARGE_FG_VF_COUNT_I, 2);
	if (unlikely(result < 0)) {
		goto batt_exit;
	}

batt_exit:
    BATT_DRV_DBGLOG("[chg_cycle] Discharge , age=%d, charge_count=%d, hi_capacity_count=%d, hi_temp_count=%d\n",
					  chip->age, chg_data.charge_count, chg_data.hi_capacity_count, chg_data.hi_temp_count);
	chip->start_soc_value = BATT_SOC_INIT;
	chip->current_soc = BATT_SOC_INIT;
	return;
}

static int batt_drv_prop_status(struct battery_drv_chip *chip)
{
	if ((fake_status >= POWER_SUPPLY_STATUS_UNKNOWN) &&
		(fake_status < POWER_SUPPLY_STATUS_MAX)) {
		BATT_DRV_INFOLOG("[%s] in fake_status:%s\n", __func__, batt_drv_status_str[fake_status]);
		return fake_status;
	}
	BATT_DRV_DBGLOG("[%s] in status:%s\n", __func__, batt_drv_status_str[chip->status]);
	return chip->status;
}

static int batt_drv_prop_health(struct battery_drv_chip *chip)
{
	if ((fake_health >= POWER_SUPPLY_HEALTH_UNKNOWN) &&
		(fake_health < POWER_SUPPLY_HEALTH_MAX)) {
		BATT_DRV_INFOLOG("[%s] in fake_health:%s\n", __func__, batt_drv_health_str[fake_health]);
		return fake_health;
	}
	BATT_DRV_DBGLOG("[%s] in health:%s\n", __func__, batt_drv_health_str[chip->health]);
	return chip->health;
}

static int batt_drv_prop_present(struct battery_drv_chip *chip)
{
	return chip->present;
}

static int batt_drv_prop_capacity(struct battery_drv_chip *chip)
{
	int result;
	int work_capacity;

	if (chip->check_disable[BATT_DRV_FUNC_LIMIT_LOW_BATTERY] == true) {
		result = 90;
	} else if ((chip->fake_soc >= 0) && (chip->fake_soc <= 100)) {
		result = chip->fake_soc;
	} else {
		batt_drv_get_soc(chip, &work_capacity);
		batt_drv_set_soc(chip, work_capacity);
		batt_drv_cycle_count_update(chip);
		if (chip->soc > BATT_SOC_FULL) {
			result = BATT_SOC_FULL;
		} else {
			result = chip->soc;
		}
	}

	if (chip->keep_capacity_flag != BATT_DRV_KEEP_CAPACITY_DISABLE) {
		if (result == 0) {
			result = 1;
		}
	}

	return result;
}

static int batt_drv_prop_voltage_now(struct battery_drv_chip *chip)
{
	int result;
	int work_voltage;

	if (chip->check_disable[BATT_DRV_FUNC_LIMIT_LOW_BATTERY] == true) {
		result = 3900;
	} else {
		batt_drv_get_voltage(chip, &work_voltage);
		result = work_voltage;
	}

	return result;
}

static int batt_drv_prop_temp(struct battery_drv_chip *chip)
{
	int result;
	int work_temp;

	if (chip->check_disable[BATT_DRV_FUNC_LIMIT_BATTERY_TEMP] == true) {
		result = 250;
	} else if (drv_initialized == 0) {
		result = 250;
	} else {
		batt_drv_get_temperature(chip, &work_temp);
		result = work_temp;
	}

	return result;
}

static int batt_drv_prop_age(struct battery_drv_chip *chip)
{
	return chip->age;
}

static int batt_drv_prop_cycle_chg(struct battery_drv_chip *chip)
{
	union power_supply_propval pval = {0,};

	BATT_DRV_DBGLOG("[%s] in cycle_chg_level=%d \n", __func__, chip->cycle_chg_lvl);

	if (chip->cycle_chg_lvl == CHG_CYCLE_LEVEL_02) {
		goto batt_exit;
	}

	if (chip->status != POWER_SUPPLY_STATUS_DISCHARGING) {
		goto batt_exit;
	}

	if (drv_initialized == 0) {
		goto batt_exit;
	}

	chip->total_cycle_count =
		chip->chg_data.charge_count +
		(chip->chg_data.hi_capacity_count * chip->chg_setting.hi_capacity_cycle_ratio) +
		(chip->chg_data.hi_temp_count * chip->chg_setting.hi_temp_cycle_ratio);

	BATT_DRV_DBGLOG("[%s] in total_cycle_count=%d cycle_threshold_lv1=%d cycle_threshold_lv2=%d\n",
					__func__, chip->total_cycle_count, chip->dt_param.cycle_charge_threshold_lv1, chip->dt_param.cycle_charge_threshold_lv2);

	if ((chip->cycle_chg_lvl == CHG_CYCLE_LEVEL_01) &&
		(chip->total_cycle_count >= (chip->dt_param.cycle_charge_threshold_lv2*BATT_SOC_FULL))) {
		pval.intval = (int)CHG_CYCLE_LEVEL_02;
		power_supply_set_property(chip->bms_psy, POWER_SUPPLY_PROP_CYCLE_CHG, &pval);
		chip->cycle_chg_lvl = CHG_CYCLE_LEVEL_02;
	} else if ((chip->cycle_chg_lvl == CHG_CYCLE_LEVEL_NONE) &&
		(chip->total_cycle_count >= (chip->dt_param.cycle_charge_threshold_lv1*BATT_SOC_FULL))) {
		pval.intval = (int)CHG_CYCLE_LEVEL_01;
		power_supply_set_property(chip->bms_psy, POWER_SUPPLY_PROP_CYCLE_CHG, &pval);
		chip->cycle_chg_lvl = CHG_CYCLE_LEVEL_01;
	}

batt_exit:
	return chip->cycle_chg_lvl;
}

static int batt_drv_get_property(struct power_supply *psy,
									   enum power_supply_property psp,
									   union power_supply_propval *val)
{
	struct battery_drv_chip *chip = power_supply_get_drvdata(psy);

	if (unlikely(psy == NULL)) {
		BATT_DRV_ERRLOG("[%s] psy pointer is NULL\n", __func__);
		return -EINVAL;
	}

	BATT_DRV_DBGLOG("[%s] in psp:%d\n", __func__, psp);

	switch (psp) {
		case POWER_SUPPLY_PROP_INITIALIZE:
			val->intval = drv_initialized;
			break;

		case POWER_SUPPLY_PROP_STATUS:
			val->intval = batt_drv_prop_status(chip);
			break;

		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = batt_drv_prop_health(chip);
			break;

		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = batt_drv_prop_present(chip);
			break;

		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = batt_drv_prop_capacity(chip);
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val->intval = batt_drv_prop_voltage_now(chip);
			break;

		case POWER_SUPPLY_PROP_TEMP:
			val->intval = batt_drv_prop_temp(chip);
			break;

		case POWER_SUPPLY_PROP_AGE:
			val->intval = batt_drv_prop_age(chip);
			break;

		case POWER_SUPPLY_PROP_CYCLE_CHG:
			val->intval = batt_drv_prop_cycle_chg(chip);
			break;

		case POWER_SUPPLY_PROP_LONGEVITY_CHG_MODE:
			val->intval = batt_drv_get_longevity_chg_mode(chip);
			break;

		case POWER_SUPPLY_PROP_OEM_CHG_CTRL_SOC:
			batt_drv_get_soc(chip, &val->intval);
			break;

		default:
			return -EINVAL;

	}
	return 0;
}

static void batt_drv_set_prop_status(struct battery_drv_chip *chip, int status)
{

	int work_capacity;

	batt_drv_get_soc(chip, &work_capacity);
	batt_drv_set_status(chip, status, work_capacity);

	return;
}

static int batt_drv_set_property(struct power_supply *psy,
									   enum power_supply_property psp,
									   const union power_supply_propval *val)
{
	struct battery_drv_chip *chip = power_supply_get_drvdata(psy);

	if (unlikely(psy == NULL)) {
		BATT_DRV_ERRLOG("[%s] psy pointer is NULL\n", __func__);
		return -EINVAL;
	}

	BATT_DRV_DBGLOG("[%s] in psp:%d\n", __func__, psp);

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			batt_drv_set_prop_status(chip, val->intval);
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			batt_drv_set_health(chip, val->intval);
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			chip->fake_soc = val->intval;
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int batt_drv_property_is_writeable(struct power_supply *psy,
												enum power_supply_property psp)
{
	int result = 0;

	if (unlikely(psy == NULL)) {
		BATT_DRV_ERRLOG("[%s] psy pointer is NULL\n", __func__);
		return -EINVAL;
	}

	BATT_DRV_DBGLOG("[%s] in psp:%d\n", __func__, psp);

	switch (psp) {
		case POWER_SUPPLY_PROP_CAPACITY:
			result = 1;
			break;
		default:
			result = 0;
			break;
	}

	return result;
}

static void batt_drv_external_power_changed(struct power_supply *psy)
{
	if (unlikely(psy == NULL)) {
		BATT_DRV_ERRLOG("[%s] psy pointer is NULL\n", __func__);
		return;
	}
	return;
}

static void batt_drv_event_monitor(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct battery_drv_chip *chip;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (unlikely(work == NULL)) {
		BATT_DRV_ERRLOG("[%s] work pointer is NULL\n", __func__);
		return;
	}

	dwork = to_delayed_work(work);
	if (unlikely(dwork == NULL)) {
		BATT_DRV_ERRLOG("[%s] dwork pointer is NULL\n", __func__);
		return;
	}

	chip = container_of(dwork, struct battery_drv_chip, battery_monitor);
	if (unlikely(chip == NULL)) {
		BATT_DRV_ERRLOG("[%s] chip pointer is NULL\n", __func__);
		return;
	}

	set_bit(BATT_DRV_EVENT_MONITOR, &chip->event_state);
	wake_up(&chip->wq);
}

static void batt_drv_work_monitor(struct battery_drv_chip *chip)
{
	int result;
	int work_volt;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (unlikely(chip == NULL)) {
		BATT_DRV_WARNLOG("[%s] chip pointer is NULL\n", __func__);
		if (unlikely(batt_chip == NULL)) {
			BATT_DRV_ERRLOG("[%s] batt_chip pointer is NULL\n", __func__);
			return;
		}
		chip = batt_chip;
	}

	result = batt_drv_get_voltage(chip, &work_volt);
	if (unlikely(result < 0)) {
		queue_delayed_work(chip->batt_drv_wq, &chip->battery_monitor, BATT_DRV_MONITOR_INTERVAL);
		BATT_DRV_ERRLOG("[%s] Error get_voltage result = %d\n", __func__, result);
		goto batt_exit;
	}

	chip->vcell = work_volt;

	if (chip->vcell <= chip->dt_param.shutdown_voltage_threshold) {
		chip->shutdown_voltage_count++;
		if (chip->shutdown_voltage_count >= chip->dt_param.shutdown_count) {
			BATT_DRV_RECLOG("[%s] low battery detect volt:%dmV\n", __func__, chip->vcell);
		}
		queue_delayed_work(chip->batt_drv_wq, &chip->battery_monitor, BATT_DRV_SHUTDOWN_VOLTAGE_INTERVAL);
		chip->keep_capacity_flag = BATT_DRV_KEEP_CAPACITY_DISABLE;
	} else if (chip->vcell <= chip->dt_param.low_voltage_threshold) {
		chip->shutdown_voltage_count = 0;
		queue_delayed_work(chip->batt_drv_wq, &chip->battery_monitor, BATT_DRV_LOW_VOLTAGE_INTERVAL);
	} else {
		chip->shutdown_voltage_count = 0;
		queue_delayed_work(chip->batt_drv_wq, &chip->battery_monitor, BATT_DRV_MONITOR_INTERVAL);
	}

	if (chip->keep_capacity_flag == BATT_DRV_KEEP_CAPACITY_STANDBY) {
		if (time_before((chip->keep_capacity_time + BATT_DRV_TIMER_30S), jiffies)) {
			chip->keep_capacity_flag = BATT_DRV_KEEP_CAPACITY_DISABLE;
		}
	} else if (chip->keep_capacity_flag == BATT_DRV_KEEP_CAPACITY_ENABLE) {
		if (time_before((chip->keep_capacity_time + BATT_DRV_TIMER_180S), jiffies)) {
			chip->keep_capacity_flag = BATT_DRV_KEEP_CAPACITY_DISABLE;
		}
	}

	power_supply_changed(chip->battery_psy);

batt_exit:
	return;
}

static int batt_drv_thread(void * ptr)
{
	struct battery_drv_chip *chip = ptr;
	unsigned int timeout_count = 0;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	if (unlikely(chip == NULL)) {
		BATT_DRV_WARNLOG("[%s] chip pointer is NULL\n", __func__);
		if (unlikely(batt_chip == NULL)) {
			BATT_DRV_ERRLOG("[%s] batt_chip pointer is NULL\n", __func__);
			return -EINVAL;
		}
		chip = batt_chip;
	}

	while (1) {
		batt_drv_get_soc(chip, &chip->soc);
		if (chip->soc <= BATT_SOC_FULL) {

			if ((chip->status == POWER_SUPPLY_STATUS_CHARGING) &&
				(chip->soc >= chip->dt_param.full_keep_threshold)) {
				chip->soc = BATT_SOC_FULL;
				chip->status = POWER_SUPPLY_STATUS_FULL;
			}
			break;
		}
		msleep(100);
	}

	 timeout_count = 0;
	while (timeout_count <= 100) {
		if (check_nonvolatile_init()) {
			break;
		}
		msleep(100);
		timeout_count++;
	}

	chip->keep_capacity_time = jiffies;
	if (chip->status == POWER_SUPPLY_STATUS_CHARGING) {
		chip->keep_capacity_flag = BATT_DRV_KEEP_CAPACITY_ENABLE;
	} else {
		chip->keep_capacity_flag = BATT_DRV_KEEP_CAPACITY_STANDBY;
	}

	if (battery_status != POWER_SUPPLY_STATUS_DISCHARGING) {
		BATT_DRV_INFOLOG("[%s] old_status:%s new_status:%s\n",
						__func__,
						batt_drv_status_str[battery_status],
						batt_drv_status_str[chip->status]);
		chip->status = battery_status;
	}

	if (battery_health != POWER_SUPPLY_HEALTH_GOOD) {
		BATT_DRV_INFOLOG("[%s] health %d > %d\n", __func__, chip->health, battery_health);
		chip->health = battery_health;
	}
	batt_drv_get_age(chip);
	batt_drv_function_limits_check(chip);
	batt_drv_get_charge_cycle(chip);

	chip->present = 1;
	drv_initialized = 1;
	if (battery_status == POWER_SUPPLY_STATUS_CHARGING) {
		batt_drv_cycle_count_start(chip);
	}

	set_bit(BATT_DRV_EVENT_MONITOR, &chip->event_state);
	set_freezable();

	do {
		wait_event_freezable(chip->wq, (chip->event_state || kthread_should_stop()));

		if (kthread_should_stop()) {
			BATT_DRV_INFOLOG("[%s] while loop break\n", __func__);
			break;
		}

		if (test_bit(BATT_DRV_EVENT_SUSPEND, &chip->event_state)) {
			test_and_clear_bit(BATT_DRV_EVENT_MONITOR, &chip->event_state);
		}

		if (test_and_clear_bit(BATT_DRV_EVENT_MONITOR, &chip->event_state)) {
			batt_drv_work_monitor(chip);
		}

	} while (1);

	return 0;
}

static int batt_drv_parse_dt(struct battery_drv_chip *chip)
{
	batt_drv_dt *dt = &chip->dt_param;
	struct device_node *node = chip->dev->of_node;
	int ret;

	if (!node) {
		BATT_DRV_ERRLOG("[%s] device tree node missing\n", __func__);
		return -EINVAL;
	}

	ret = of_property_read_u32(node, "fcnt,batt-recharge-soc",
				&dt->recharge_soc_threshold);
	if (ret < 0)
		dt->recharge_soc_threshold = BATT_RECHARGE_SOC_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-full-keep-soc",
				&dt->full_keep_threshold);
	if (ret < 0)
		dt->full_keep_threshold = BATT_FULL_KEEP_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-low-voltage-mv",
				&dt->low_voltage_threshold);
	if (ret < 0)
		dt->low_voltage_threshold = BATT_LOW_VOLTAGE_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-shutdown-voltage-mv",
				&dt->shutdown_voltage_threshold);
	if (ret < 0)
		dt->shutdown_voltage_threshold = BATT_SHUTDOWN_VOLTAGE_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-shutdown-count",
				&dt->shutdown_count);
	if (ret < 0)
		dt->shutdown_count = BATT_SHUTDOWN_COUNT_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-longevity-full-soc-min",
				&dt->longevity_full_soc_min_threshold);
	if (ret < 0)
		dt->longevity_full_soc_min_threshold = BATT_LONGEVITY_FULL_SOC_MIN_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-longevity-full-soc-max",
				&dt->longevity_full_soc_max_threshold);
	if (ret < 0)
		dt->longevity_full_soc_max_threshold = BATT_LONGEVITY_FULL_SOC_MAX_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-longevity-recharge-soc",
				&dt->longevity_recharge_soc_threshold);
	if (ret < 0)
		dt->longevity_recharge_soc_threshold = BATT_LONGEVITY_RECHARGE_SOC_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-cycle-count-hi-soc",
				&dt->cycle_count_hi_soc_threshold);
	if (ret < 0)
		dt->cycle_count_hi_soc_threshold = BATT_CYCLE_COUNT_HI_SOC_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-cycle-count-hi-temp",
				&dt->cycle_count_hi_temp_threshold);
	if (ret < 0)
		dt->cycle_count_hi_temp_threshold = BATT_CYCLE_COUNT_HI_TEMP_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-cycle-charge-count-lv1",
				&dt->cycle_charge_threshold_lv1);
	if (ret < 0)
		dt->cycle_charge_threshold_lv1 = BATT_CYCLE_CHARGE_DEFAULT;

	ret = of_property_read_u32(node, "fcnt,batt-cycle-charge-count-lv2",
				&dt->cycle_charge_threshold_lv2);
	if (ret < 0)
		dt->cycle_charge_threshold_lv2 = BATT_CYCLE_CHARGE_DEFAULT;

	if ((of_property_count_u32_elems(node, "fcnt,batt-temp-correction-values") ==
		 of_property_count_u32_elems(node, "fcnt,batt-temp-correction-thresholds")) &&
		(of_property_count_u32_elems(node, "fcnt,batt-temp-correction-values") <=
													 BATT_ADJUST_TEMP_TBL_MAX) &&
		(of_property_count_u32_elems(node, "fcnt,batt-temp-correction-thresholds") <=
													 BATT_ADJUST_TEMP_TBL_MAX)) {
		dt->temp_correction_num = of_property_count_u32_elems(node,
										"fcnt,batt-temp-correction-values");

		ret = of_property_read_u32_array(node, "fcnt,batt-temp-correction-values",
				dt->temp_correction_values, dt->temp_correction_num);
		if (ret < 0) {
			dt->temp_correction_num = 0;
		}

		ret = of_property_read_u32_array(node, "fcnt,batt-temp-correction-thresholds",
				dt->temp_correction_thresholds, dt->temp_correction_num);
		if (ret < 0) {
			dt->temp_correction_num = 0;
		}
	} else {
		dt->temp_correction_num = 0;
	}

	return 0;
}

static int batt_drv_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct battery_drv_chip *chip;
	struct power_supply_config battery_drv_cfg = {};

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		return -ENOMEM;
	}

	chip->dev = &(pdev->dev);

	/* init wait queue */
	init_waitqueue_head(&chip->wq);

	ret = batt_drv_parse_dt(chip);
	if (unlikely(ret != 0)) {
		BATT_DRV_ERRLOG("[%s] dt perse failed ret = %d\n", __func__, ret);
		devm_kfree(&pdev->dev, chip);
		return ret;
	}

	chip->bms_psy = power_supply_get_by_name(QPNP_BMS_PSY_NAME);
	if (chip->bms_psy == NULL) {
		BATT_DRV_ERRLOG("[%s] power_supply_get_by_name %s failed\n", __func__, QPNP_BMS_PSY_NAME);
		devm_kfree(&pdev->dev, chip);
		return -EPROBE_DEFER;
	}

	platform_set_drvdata(pdev, chip);

	mutex_init(&chip->lock);
	chip->drv_status = wakeup_source_register(&pdev->dev, "oem_batt_drv_status"); 

	chip->health = POWER_SUPPLY_HEALTH_GOOD;
	chip->fake_soc = -EINVAL;
	chip->event_state = 0;
	chip->soc = BATT_SOC_INIT;
	chip->old_msoc = BATT_MSOC_INIT;
	chip->vcell = 0;
	chip->start_soc_value = BATT_SOC_INIT;
	chip->current_soc = BATT_SOC_INIT;
	chip->incremental_soc = 0;
	chip->cycle_chg_lvl = CHG_CYCLE_LEVEL_NONE;		
	chip->status = POWER_SUPPLY_STATUS_DISCHARGING;
	chip->shutdown_voltage_count = 0;
	chip->keep_capacity_time = 0;
	chip->keep_capacity_flag = BATT_DRV_KEEP_CAPACITY_DISABLE;
	chip->longevity_charge_flag = false;

	if (charging_mode) {
		chip->status = POWER_SUPPLY_STATUS_CHARGING;
		battery_status = POWER_SUPPLY_STATUS_CHARGING;
	} else {
		chip->status = POWER_SUPPLY_STATUS_DISCHARGING;
		battery_status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	chip->battery_psy_desc.name 				  = OEM_BATTERY_PSY_NAME;
	chip->battery_psy_desc.type 				  = POWER_SUPPLY_TYPE_OEM_BATT;
	chip->battery_psy_desc.properties			  = batt_drv_properties;
	chip->battery_psy_desc.num_properties		  = ARRAY_SIZE(batt_drv_properties);
	chip->battery_psy_desc.get_property 		  = batt_drv_get_property;
	chip->battery_psy_desc.set_property 		  = batt_drv_set_property;
	chip->battery_psy_desc.property_is_writeable  = batt_drv_property_is_writeable;
	chip->battery_psy_desc.external_power_changed = batt_drv_external_power_changed;

	battery_drv_cfg.drv_data = chip;
	battery_drv_cfg.of_node = chip->dev->of_node;
	chip->battery_psy = devm_power_supply_register(chip->dev, &chip->battery_psy_desc,
													  &battery_drv_cfg);
	if (IS_ERR(chip->battery_psy)) {
		ret = PTR_ERR(chip->battery_psy);
		BATT_DRV_ERRLOG("[%s] power_supply_register %s failed ret = %d\n", __func__, OEM_BATTERY_PSY_NAME, ret);
		devm_kfree(&pdev->dev, chip);
		return ret;
	}

	INIT_DELAYED_WORK(&chip->battery_monitor, batt_drv_event_monitor);
	chip->batt_drv_wq = create_singlethread_workqueue(dev_name(&pdev->dev));
	if (!chip->batt_drv_wq) {
		BATT_DRV_ERRLOG("[%s] create_singlethread_workqueue failed\n", __func__);
		if (chip->battery_psy)
			power_supply_unregister(chip->battery_psy);
		devm_kfree(&pdev->dev, chip);
		return -ESRCH;
	}

	batt_chip = chip;

	/* kthread start */
	chip->thread = kthread_run(batt_drv_thread, chip, "oem_batt_drv");

	BATT_DRV_INFOLOG("[%s] probe End\n", __func__);

	return 0;
}

static int batt_drv_remove(struct platform_device *pdev)
{
	struct battery_drv_chip *chip = batt_chip;

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	batt_chip = NULL;
	cancel_delayed_work(&chip->battery_monitor);

	if (chip->batt_drv_wq)
		destroy_workqueue(chip->batt_drv_wq);

	if (chip->battery_psy)
		power_supply_unregister(chip->battery_psy);

	platform_set_drvdata(pdev, NULL);
	devm_kfree(&pdev->dev, chip);

	return 0;
}

#ifdef CONFIG_PM
static int batt_drv_suspend(struct device *dev)
{
	struct battery_drv_chip *chip = dev_get_drvdata(dev);

	BATT_DRV_DBGLOG("[%s] in\n", __func__);
	set_bit(BATT_DRV_EVENT_SUSPEND, &chip->event_state);

	cancel_delayed_work(&chip->battery_monitor);
	return 0;
}

static int batt_drv_resume(struct device *dev)
{
	struct battery_drv_chip *chip = dev_get_drvdata(dev);

	BATT_DRV_DBGLOG("[%s] in\n", __func__);

	clear_bit(BATT_DRV_EVENT_SUSPEND, &chip->event_state);
	queue_delayed_work(chip->batt_drv_wq, &chip->battery_monitor, BATT_DRV_RESUME_INTERVAL);
	wake_up(&chip->wq);

	return 0;
}

static const struct dev_pm_ops batt_drv_pm_ops = {
	.suspend = batt_drv_suspend,
	.resume = batt_drv_resume,
};

#else  /* CONFIG_PM */
#define batt_drv_suspend NULL
#define batt_drv_resume  NULL
#endif /* CONFIG_PM */

static struct of_device_id batt_drv_match_table[] = {
	{ .compatible = "fcnt,oem_battery_drv",},
	{ },
};

static struct platform_driver batt_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "oem_batt_drv",
		.of_match_table = batt_drv_match_table,
#ifdef CONFIG_PM
		.pm = &batt_drv_pm_ops,
#endif /* CONFIG_PM */
		   },
	.probe = batt_drv_probe,
	.remove = batt_drv_remove,
#ifndef CONFIG_PM
	.suspend	= batt_drv_suspend,
	.resume 	= batt_drv_resume,
#endif /* !CONFIG_PM */
};

static int __init batt_drv_init(void)
{
	return platform_driver_register(&batt_driver);
}
module_init(batt_drv_init);

static void __exit batt_drv_exit(void)
{
	platform_driver_unregister(&batt_driver);
}
module_exit(batt_drv_exit);


MODULE_AUTHOR("FUJITSU CONNECTED TECHNOLOGIES");
MODULE_DESCRIPTION("OEM Fuel Gauge Driver");
MODULE_LICENSE("GPL");
