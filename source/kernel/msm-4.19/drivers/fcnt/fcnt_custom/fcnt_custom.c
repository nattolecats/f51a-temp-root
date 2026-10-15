/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
// SPDX-License-Identifier: GPL-2.0

//==============================================================================
// include file
//==============================================================================
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/custom_mode.h>
#include <linux/nonvolatile_common.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

//==============================================================================
// define
//==============================================================================
#define APNV_BOOT_COUNT_I    (49002)

//==============================================================================
// const data
//==============================================================================

//==============================================================================
// global valuable
//==============================================================================
int custom_boot_mode = CUSTOM_MODE_NORMAL;
EXPORT_SYMBOL(custom_boot_mode);
int charging_mode = 0;
EXPORT_SYMBOL(charging_mode);

//==============================================================================
// private valuable
//==============================================================================

//==============================================================================
// static functions prototype
//==============================================================================

//==============================================================================
// functions
//==============================================================================
static int __init androidboot_mode_enable(char *str)
{
	if (strcmp(str, "charger") == 0) {
		charging_mode = 1;
		custom_boot_mode = CUSTOM_MODE_OFF_CHARGE;
	}

	printk(KERN_INFO "custom_boot_mode = %d\n", custom_boot_mode);
	printk(KERN_INFO "chargemode: %d\n", charging_mode);
	return 0;
}
early_param("androidboot.mode", androidboot_mode_enable);

static int bootcnt_show(struct seq_file *m, void *unused)
{
	int ret = 0;
	uint8_t tmpcnt[2] = {0};
	uint16_t count = 0;

	ret = get_nonvolatile(tmpcnt, APNV_BOOT_COUNT_I, sizeof(tmpcnt));
	if (ret == sizeof(tmpcnt)) {
		count = *((uint16_t *)tmpcnt);
		seq_printf(m, "%d\n", count);
	} else {
		seq_printf(m, "%d\n", 0);
	}

	return 0;
}

static int bootcnt_open(struct inode *inode, struct file *file)
{
	return single_open(file, bootcnt_show, NULL);
}

static const struct file_operations bootcnt_fops = {
	.owner = THIS_MODULE,
	.open = bootcnt_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init bootcnt_init(void)
{
	proc_create("bootcnt", S_IRUGO, NULL, &bootcnt_fops);
	return 0;
}
module_init(bootcnt_init);

MODULE_LICENSE("GPL v2");
