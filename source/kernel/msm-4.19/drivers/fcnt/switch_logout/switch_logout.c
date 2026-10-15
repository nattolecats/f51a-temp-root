/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
// SPDX-License-Identifier: GPL-2.0

#include <linux/switch_logout.h>

#ifdef SWITCH_LOGOUT

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

static uint32_t debuglog_switch = 0x0;

extern void clk_switch_dump(void);
extern void regulator_switch_dump(void);
extern void gpiolib_switch_dump(void);

uint32_t check_and_get_table_from_NV(void)
{
	return debuglog_switch;
}

void switch_print_seq_kmsg(struct seq_file *s)
{
	int print_size = 0;
	int print_size_total = 0;
	int size = s->count;
	unsigned char *buf = s->buf;
	unsigned char *lnptr = NULL;

	for (print_size_total = 0; print_size_total < size; print_size_total += print_size) {
		lnptr = strchr(buf, '\n');

		if (lnptr) {
			print_size = lnptr - buf + 1;
			printk( KERN_CONT "%.*s", print_size, buf);
			buf += print_size;
		} else {
			printk( "%s\n", buf);
			break;
		}
	}

	return;
}

void *switch_print_seq_alloc(unsigned long size)
{
	struct seq_file *s;
	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return NULL;

	s->buf = vmalloc(size);
	if (!s->buf)
		goto err_free_buf;

	memset(s->buf, '\0', size);

	s->size = size;
	s->count = 0;
	s->private = "";

	return s;

err_free_buf:
	kfree(s);

	return NULL;
}

void switch_print_seq_free(struct seq_file *s)
{
	vfree(s->buf);
	kfree(s);
}

void switch_powercollapse_dump(void)
{
	if (debuglog_switch & ClockLogType) {
		printk("switch_powercollapse_flg_clock_start\n");
		clk_switch_dump();
	}

	if (debuglog_switch & RegulatorLogType) {
		printk("switch_powercollapse_flg_regulator_start\n");
		regulator_switch_dump();
	}

	if (debuglog_switch & GpioLogType) {
		printk("switch_powercollapse_flg_gpio_start\n");
		/* show gpio. */
		gpiolib_switch_dump();
	}
}

void switch_before_suspend_gpio_dump(void)
{
	if (debuglog_switch & BeforeSuspend_LogType) {
		printk("switch_before_suspend_gpio_dump_start\n");
		gpiolib_switch_dump();
	}
}

void switch_after_suspend_gpio_dump(void)
{
	if (debuglog_switch & AfterSuspend_LogType) {
		printk("switch_after_suspend_gpio_dump_start\n");
		gpiolib_switch_dump();
	}
}

/* debugfs */
static int switch_dump_gpio_show(struct seq_file *m, void *unused)
{
	gpiolib_switch_dump();

	return 0;
}

static int switch_dump_gpio_open(struct inode *inode, struct file *file)
{
	return single_open(file, switch_dump_gpio_show, inode->i_private);
}

static const struct file_operations switch_dump_gpio_ops = {
	.open = switch_dump_gpio_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int switch_dump_clock_show(struct seq_file *m, void *unused)
{
	clk_switch_dump();

	return 0;
}

static int switch_dump_clock_open(struct inode *inode, struct file *file)
{
	return single_open(file, switch_dump_clock_show, inode->i_private);
}

static const struct file_operations switch_dump_clock_ops = {
	.open = switch_dump_clock_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int switch_dump_vreg_show(struct seq_file *m, void *unused)
{
	regulator_switch_dump();

	return 0;
}

static int switch_dump_vreg_open(struct inode *inode, struct file *file)
{
	return single_open(file, switch_dump_vreg_show, inode->i_private);
}

static const struct file_operations switch_dump_vreg_ops = {
	.open = switch_dump_vreg_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

static int __init switch_dump_init(void)
{
	struct dentry *root;

	root = debugfs_create_dir("switch_dump", NULL);

	if (!root)
		return -ENOMEM;

	if (!debugfs_create_file("gpio", S_IRUGO, root, NULL, &switch_dump_gpio_ops))
		return -ENOMEM;

	if (!debugfs_create_file("clock", S_IRUGO, root, NULL, &switch_dump_clock_ops))
		return -ENOMEM;

	if (!debugfs_create_file("regulator", S_IRUGO, root, NULL, &switch_dump_vreg_ops))
		return -ENOMEM;

	return 0;
}
late_initcall(switch_dump_init);

static int __init boot_debuglog_switch(char *str)
{
	debuglog_switch = (uint32_t)simple_strtoul(str, NULL, 16);
	return 0;
}
early_param("debuglog_switch", boot_debuglog_switch);

#endif /* SWITCH_LOGOUT */
