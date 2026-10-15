/*----------------------------------------------------------------------------*/
// (C) 2021 FCNT LIMITED
/*----------------------------------------------------------------------------*/
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/device.h>
#include <linux/i2c.h>
#include "synaptics_tcm_core.h"

#define X_AXIS_MAX           1440
#define X_AXIS_MIN           0
#define Y_AXIS_MAX           3120
#define Y_AXIS_MIN           0

DECLARE_COMPLETION(aoi_event_remove_complete);

static ssize_t syna_tcm_touch_event_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);
	struct syna_tcm_aoi_fpdata *fp_data = &(tcm_hcd->fp_data);
	size_t len = 0;
	struct touch_event *touch = (struct touch_event *)fp_data->event;
	unsigned long mask = fp_data->event_mask;

	for (i = 0; mask > 0 && i < FIFO_DEPTH; i++) {
		if (__test_and_clear_bit(i, &mask))
			len += scnprintf((buf + len), PAGE_SIZE - len,
					"%c %d %d %d; ",
					touch->type,
					touch->fid,
					touch->x,
					touch->y);
		touch++;
	}

	return len;
}

ssize_t syna_tcm_aoi_set_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ret;
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);
	struct syna_tcm_aoi_fpdata *fp_data = &(tcm_hcd->fp_data);
	int left, top, right, bottom;

	ret = sscanf(buf, "%d %d %d %d", &left, &top, &right, &bottom);
	if (ret != 4)
		return -EINVAL;

	if (right > X_AXIS_MAX)
		right = X_AXIS_MAX;
	if (bottom > Y_AXIS_MAX)
		bottom = Y_AXIS_MAX;

	if (left < 0 || left > X_AXIS_MAX || right < 0) {
		fp_data->aoi_notify_enabled = false;
		return -EINVAL;
	}

	if (top < 0 || top > Y_AXIS_MAX || bottom < 0) {
		fp_data->aoi_notify_enabled = false;
		return -EINVAL;
	}

	if (left >= right || top >= bottom) {
		fp_data->aoi_notify_enabled = false;
		return count;
	}

	fp_data->aoi_left = left;
	fp_data->aoi_top = top;
	fp_data->aoi_right = right;
	fp_data->aoi_bottom = bottom;

	fp_data->aoi_notify_enabled = true;
	return count;
}

static ssize_t syna_tcm_aoi_set_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct syna_tcm_hcd *tcm_hcd = dev_get_drvdata(dev);
	struct syna_tcm_aoi_fpdata *fp_data = &(tcm_hcd->fp_data);
	size_t len = 0;

	len = scnprintf(buf + len, PAGE_SIZE,
				"%d %d %d %d",
				fp_data->aoi_left,
				fp_data->aoi_top,
				fp_data->aoi_right,
				fp_data->aoi_bottom);

	return len;
}

static ssize_t syna_tcm_power_set_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int enable;

	if (kstrtoint(buf, 10, &enable))
		return -EINVAL;

	return count;
}

static DEVICE_ATTR(touch_event, 0444, syna_tcm_touch_event_show, NULL);
static DEVICE_ATTR(aoi_set, 0644, syna_tcm_aoi_set_show, syna_tcm_aoi_set_store);
static DEVICE_ATTR(power_set, 0200, NULL, syna_tcm_power_set_store);

static struct attribute *aoi_cmd_attributes[] = {
	&dev_attr_touch_event.attr,
	&dev_attr_aoi_set.attr,
	&dev_attr_power_set.attr,
	NULL,
};

static struct attribute_group aoi_cmd_attr_group = {
	.attrs = aoi_cmd_attributes,
};

static int device_init(struct syna_tcm_hcd *tcm_hcd)
{
	int retval = 0;

	tcm_hcd->aoi_class = class_create(THIS_MODULE, "fts");
	if (IS_ERR(tcm_hcd->aoi_class)) {
		LOGE(tcm_hcd->pdev->dev.parent,
				"Failed to create aoi class\n");
		return -ENODEV;
	}

	tcm_hcd->aoi_device = device_create(tcm_hcd->aoi_class,
			NULL, 0x70, tcm_hcd, "touch_aoi");
	if (IS_ERR(tcm_hcd->aoi_device)) {
		LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to create aoi device\n");
		goto err_create_aoi_device;
	}

	retval = sysfs_create_group(&tcm_hcd->aoi_device->kobj,
			&aoi_cmd_attr_group);
	if (retval) {
		LOGE(tcm_hcd->pdev->dev.parent,
					"Failed to create aoi sysfs\n");
		goto err_sysfs_create_aoi_file;
	}

	return 0;

err_sysfs_create_aoi_file:
	device_destroy(tcm_hcd->aoi_class, 0x70);
err_create_aoi_device:
	class_destroy(tcm_hcd->aoi_class);

	return -ENODEV;
}

static int device_remove(struct syna_tcm_hcd *tcm_hcd)
{
	if (!tcm_hcd)
		goto exit;

	sysfs_remove_group(&tcm_hcd->aoi_device->kobj, &aoi_cmd_attr_group);
	device_destroy(tcm_hcd->aoi_class, 0x70);
	class_destroy(tcm_hcd->aoi_class);

exit:
	complete(&aoi_event_remove_complete);

	return 0;
}

static struct syna_tcm_module_cb aoi_event_module = {
	.type = TCM_DEVICE,
	.init = device_init,
	.remove = device_remove,
	.syncbox = NULL,
#ifdef REPORT_NOTIFIER
	.asyncbox = NULL,
#endif
	.reinit = NULL,
	.suspend = NULL,
	.resume = NULL,
	.early_suspend = NULL,
};

static int __init aoi_event_module_init(void)
{
	return syna_tcm_add_module(&aoi_event_module, true);
}

static void __exit aoi_event_module_exit(void)
{
	syna_tcm_add_module(&aoi_event_module, false);

	wait_for_completion(&aoi_event_remove_complete);

	return;
}

module_init(aoi_event_module_init);
module_exit(aoi_event_module_exit);

MODULE_DESCRIPTION("Synaptics TCM AOI Module");
MODULE_LICENSE("GPL v2");
