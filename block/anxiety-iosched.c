// SPDX-License-Identifier: GPL-2.0
/*
 * Anxiety I/O Scheduler - Adaptive queue depth based on I/O patterns
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#define ANXIETY_NAME "anxiety"
#define ANXIETY_DEFAULT_DEPTH 32
#define ANXIETY_MIN_DEPTH 4
#define ANXIETY_MAX_DEPTH 128
#define ADAPT_INTERVAL msecs_to_jiffies(100)

struct anxiety_data {
	struct request_queue *q;
	unsigned int req_depth;
	unsigned int act_depth;
	unsigned long last_access;
	struct timer_list adapt_timer;
};

static int queue_depth = ANXIETY_DEFAULT_DEPTH;
module_param(queue_depth, int, 0644);
MODULE_PARM_DESC(queue_depth, "Initial queue depth (default=32)");

static void anxiety_adapt_fn(struct timer_list *t)
{
	struct anxiety_data *ad = from_timer(ad, t, adapt_timer);
	unsigned int delta;

	if (jiffies - ad->last_access > msecs_to_jiffies(50)) {
		delta = ad->act_depth / 4;
		if (delta < 1)
			delta = 1;
		ad->act_depth -= delta;
		if (ad->act_depth < ANXIETY_MIN_DEPTH)
			ad->act_depth = ANXIETY_MIN_DEPTH;
	} else {
		delta = (ad->act_depth + 3) / 4;
		if (delta < 1)
			delta = 1;
		ad->act_depth += delta;
		if (ad->act_depth > ANXIETY_MAX_DEPTH)
			ad->act_depth = ANXIETY_MAX_DEPTH;
	}

	if (ad->act_depth != ad->req_depth) {
		ad->req_depth = ad->act_depth;
		blk_set_queue_depth(ad->q, ad->req_depth);
	}

	ad->last_access = jiffies;
	mod_timer(&ad->adapt_timer, jiffies + ADAPT_INTERVAL);
}

static int anxiety_init_queue(struct request_queue *q, struct elevator_type *e)
{
	struct anxiety_data *ad;

	ad = kmalloc(sizeof(*ad), GFP_KERNEL);
	if (!ad)
		return -ENOMEM;

	ad->q = q;
	ad->req_depth = queue_depth;
	ad->act_depth = queue_depth;
	ad->last_access = jiffies;

	timer_setup(&ad->adapt_timer, anxiety_adapt_fn, 0);
	mod_timer(&ad->adapt_timer, jiffies + ADAPT_INTERVAL);

	q->elevator->elevator_data = ad;
	return 0;
}

static void anxiety_exit_queue(struct elevator_queue *eq)
{
	struct anxiety_data *ad = eq->elevator_data;

	if (ad) {
		del_timer_sync(&ad->adapt_timer);
		kfree(ad);
	}
}

static struct elevator_type io_scheduler_anxiety = {
	.ops = {
		.mq = {
			.init_sched = anxiety_init_queue,
			.exit_sched = anxiety_exit_queue,
		},
	},
	.uses_mq = true,
	.elevator_name = ANXIETY_NAME,
	.elevator_owner = THIS_MODULE,
};

static int __init anxiety_init(void)
{
	return elv_register(&io_scheduler_anxiety);
}

static void __exit anxiety_exit(void)
{
	elv_unregister(&io_scheduler_anxiety);
}

module_init(anxiety_init);
module_exit(anxiety_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("UMI Kernel Team");
MODULE_DESCRIPTION("Anxiety Adaptive I/O Scheduler");
