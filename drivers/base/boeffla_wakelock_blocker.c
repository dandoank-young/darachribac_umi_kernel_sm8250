// SPDX-License-Identifier: GPL-2.0
/*
 * Boeffla Wakelock Blocker
 *
 * Provides a sysfs interface to control wakelock blocking behavior.
 * Reduces idle battery drain by allowing selective wakelock suppression.
 *
 * Thin sysfs toggle driver for Android kernels.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

#define BWB_NAME "boeffla_wakelock_blocker"

static int wkb_enabled = 1;

static ssize_t wakelock_blocker_enable_show(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	return sprintf(buf, "%d\n", wkb_enabled);
}

static ssize_t wakelock_blocker_enable_store(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t count)
{
	int val;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;

	wkb_enabled = val ? 1 : 0;
	pr_info("%s: wakelock blocking %s\n",
		BWB_NAME, wkb_enabled ? "enabled" : "disabled");

	return count;
}

static struct kobj_attribute wkb_enable_attr =
	__ATTR(wakelock_blocker_enable, 0644,
	       wakelock_blocker_enable_show,
	       wakelock_blocker_enable_store);

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
