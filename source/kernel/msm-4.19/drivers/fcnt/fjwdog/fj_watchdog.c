/*
 * fj_watchdog.c
 *
 * (C) 2021 FCNT LIMITED
 *
 */
// SPDX-License-Identifier: GPL-2.0

//==============================================================================
// include file
//==============================================================================
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/timer.h>

//==============================================================================
// define
//==============================================================================
#define DRIVER_NAME "fj_watchdog"
#define DEVICE_NAME "fj_watchdog"

#define FJ_WATCHDOG_LOG_I(fmt, args...)		printk("[fj_watchdog]" fmt "\n", ## args)
#define FJ_WATCHDOG_LOG_E(fmt, args...)		printk("[fj_watchdog]" fmt "\n", ## args)
#define FJ_WATCHDOG_LOG_D(fmt, args...)		/*printk("[fj_watchdog]" fmt "\n", ## args)*/

//==============================================================================
// static functions prototype
//==============================================================================
static int timer_value = 0;
static bool fj_watchdog_t_flag = false;
static struct timer_list fj_watchdog_t;

//==============================================================================
// timer functions
//==============================================================================
static void fj_watchdog_timer_handler(struct timer_list *t)
{
	FJ_WATCHDOG_LOG_D("%s flag:%d",__func__, fj_watchdog_t_flag);
	if (fj_watchdog_t_flag)
		panic("fj watch dog timer expired!\n");
}

static void fj_watchdog_timer_start(int value)
{
	FJ_WATCHDOG_LOG_D("%s start value:%d flag:%d",__func__, value, fj_watchdog_t_flag);
	if (value < 0) {
		FJ_WATCHDOG_LOG_E("%s invalid value %d",__func__, value);
		return;
	}

	if (fj_watchdog_t_flag) {
		fj_watchdog_t_flag = false;
		FJ_WATCHDOG_LOG_D("%s--del_timer_sync",__func__);
		del_timer_sync(&fj_watchdog_t);
	}
	if (value == 0) {
		FJ_WATCHDOG_LOG_I("Timer Stop");
		return;
	}

	FJ_WATCHDOG_LOG_I("Timer Start %ds", value);
	FJ_WATCHDOG_LOG_D("%s--mod_timer",__func__);
	mod_timer(&fj_watchdog_t, jiffies + HZ * value);

	fj_watchdog_t_flag = true;
	FJ_WATCHDOG_LOG_D("%s end flag:%d",__func__, fj_watchdog_t_flag);
}

//==============================================================================
// sysfs functions
//==============================================================================
static int fj_watchdog_timer_set(const char *val,const struct kernel_param *kp)
{
	int ret = 0;
	FJ_WATCHDOG_LOG_D("%s--param_set_int timer_value:%d",__func__, timer_value);

	ret = param_set_int(val, kp);
	if (ret) {
		FJ_WATCHDOG_LOG_E("%s param_set_int error %d",__func__, ret);
		return ret;
	}

	FJ_WATCHDOG_LOG_D("%s--fj_watchdog_timer_start timer_value:%d",__func__, timer_value);
	fj_watchdog_timer_start(timer_value);

	return ret;
}

//==============================================================================
// driver functions
//==============================================================================
static int fj_watchdog_probe(struct platform_device *pdev)
{
	FJ_WATCHDOG_LOG_D("%s--setup_timer",__func__);
	timer_setup(&fj_watchdog_t, fj_watchdog_timer_handler, 0);
	return 0;
}

static int fj_watchdog_remove(struct platform_device *pdev)
{
	FJ_WATCHDOG_LOG_D("%s",__func__);
	if (fj_watchdog_t_flag) {
		fj_watchdog_t_flag = false;
		FJ_WATCHDOG_LOG_D("%s--del_timer",__func__);
		del_timer(&fj_watchdog_t);
	}
	return 0;
}

static void fj_watchdog_dev_release(struct device *dev)
{
	FJ_WATCHDOG_LOG_D("%s",__func__);
	return;
}

static struct platform_device fj_watchdog_devices = {
	.name = DEVICE_NAME,
	.id   = -1,
	.dev = {
		.release = fj_watchdog_dev_release,
	},
};

static struct platform_driver fj_watchdog_driver = {
	.probe    = fj_watchdog_probe,
	.remove   = fj_watchdog_remove,
	.driver   = {
		.name = DRIVER_NAME,
	},
};

static int __init fj_watchdog_init(void)
{
	int ret = 0;

	FJ_WATCHDOG_LOG_D("%s--device_register",__func__);
	platform_device_register(&fj_watchdog_devices);
	FJ_WATCHDOG_LOG_D("%s--driver_register",__func__);
	ret = platform_driver_register(&fj_watchdog_driver);
	return ret;
}

static void __exit fj_watchdog_exit(void)
{
	FJ_WATCHDOG_LOG_D("%s--device_unregister",__func__);
	platform_device_unregister(&fj_watchdog_devices);
	FJ_WATCHDOG_LOG_D("%s--driver_unregister",__func__);
	platform_driver_unregister(&fj_watchdog_driver);
}

module_param_call(timer_value, fj_watchdog_timer_set, param_get_uint, &timer_value, 0660);
module_init(fj_watchdog_init);
module_exit(fj_watchdog_exit);

MODULE_AUTHOR("FUJITSU");
MODULE_DESCRIPTION("fj watchdog");
MODULE_LICENSE("GPL");
