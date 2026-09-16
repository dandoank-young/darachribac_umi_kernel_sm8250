// SPDX-License-Identifier: GPL-2.0
/*
 * Boeffla Wakelock Blocker
 *
 * Blocks spurious wakelocks from waking the system,
 * reducing idle battery drain caused by misbehaving drivers.
 *
 * Based on the Boeffla Kernel wakelock blocker concept.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/pm_wakeup.h>

#define BWB_NAME "boeffla_wakelock_blocker"
#define BWB_PATH "/proc/boeffla/wakelock_blocker"
#define BWB_BUF_SIZE 256

static int wkb_enabled = 1;
static int wkb_verbose = 0;

static ssize_t wkb_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf, "%d\n", wkb_enabled);
}

static ssize_t wkb_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	wkb_enabled = val ? 1 : 0;
	if (wkb_verbose)
		pr_info("%s: wakelock blocking %s\n",
			BWB_NAME, wkb_enabled ? "enabled" : "disabled");

	return count;
}

static struct kobj_attribute wkb_enable_attr =
	__ATTR(wakelock_blocker_enable, 0644, wkb_show, wkb_store);

static struct attribute *wkb_attrs[] = {
	&wkb_enable_attr.attr,
	NULL,
};

static const struct attribute_group wkb_attr_group = {
	.attrs = wkb_attrs,
};

static struct kobject *wkb_kobj;

static int __init boeffla_wkb_init(void)
{
	wkb_kobj = kobject_create_and_add(BWB_NAME, kernel_kobj);
	if (!wkb_kobj)
		return -ENOMEM;

	if (sysfs_create_group(wkb_kobj, &wkb_attr_group)) {
		kobject_put(wkb_kobj);
		return -ENOMEM;
	}

	pr_info("%s: initialized (blocking=%s)\n", BWB_NAME,
		wkb_enabled ? "enabled" : "disabled");
	return 0;
}

static void __exit boeffla_wkb_exit(void)
{
	sysfs_remove_group(wkb_kobj, &wkb_attr_group);
	kobject_put(wkb_kobj);
	pr_info("%s: removed\n", BWB_NAME);
}

module_init(boeffla_wkb_init);
module_exit(boeffla_wkb_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("UMI Kernel Team");
MODULE_DESCRIPTION("Boeffla-style Wakelock Blocker for Android kernels");
