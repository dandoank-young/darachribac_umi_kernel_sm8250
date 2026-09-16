// SPDX-License-Identifier: GPL-2.0
/*
 * Anxiety I/O Scheduler
 *
 * An adaptive I/O scheduler that reduces queue depth during
 * idle periods and increases it during bursts, minimizing
 * latency spikes while maintaining throughput.
 *
 * Based on the original anxiety scheduler by Stendro
 * Adapted for 4.19 blk-mq by UMI Kernel Team
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/delay.h>

#define ANXIETY_NAME "anxiety"
#define ANXIETY_DEFAULT_QUEUE_DEPTH 32
#define ANXIETY_MIN_QUEUE_DEPTH 4
#define ANXIETY_MAX_QUEUE_DEPTH 128
#define ANXIETY_ADAPT_INTERVAL msecs_to_jiffies(100)
#define ANXIETY_IDLE_THRESHOLD 8
#define ANXIETY_BURST_THRESHOLD 64

struct anxiety_data {
	struct request_queue *q;
	int queue_depth;
	int current_depth;
	unsigned long last_access;
	struct timer_list adapt_timer;
	struct work_struct adapt_work;
};

static int anxiety_queue_depth = ANXIETY_DEFAULT_QUEUE_DEPTH;
module_param(queue_depth, int, 0644);
MODULE_PARM_DESC(queue_depth, "Initial Anxiety I/O queue depth (default=32)");

static inline bool is_idle(struct anxiety_data *ad)
{
	return jiffies - ad->last_access > msecs_to_jiffies(50);
}

static void anxiety_adapt_queue(struct anxiety_data *ad)
{
	unsigned long now = jiffies;
	int delta;

	if (is_idle(ad)) {
		/* Reduce queue depth during idle */
		delta = ad->current_depth / 4;
		ad->current_depth = max(ad->current_depth - delta,
					ANXIETY_MIN_QUEUE_DEPTH);
	} else {
		/* Increase queue depth during activity */
		delta = (ad->current_depth + 3) / 4;
		ad->current_depth = min(ad->current_depth + delta,
					ANXIETY_MAX_QUEUE_DEPTH);
	}

	if (ad->current_depth != ad->queue_depth) {
		ad->queue_depth = ad->current_depth;
		blk_queue_readahead(ad->q, ad->queue_depth);
	}

	ad->last_access = now;
	mod_timer(&ad->adapt_timer,
		  now + ANXIETY_ADAPT_INTERVAL);
}

static void anxiety_adapt_fn(struct timer_list *t)
{
	struct anxiety_data *ad = from_timer(ad, t, adapt_timer);
	anxiety_adapt_queue(ad);
}

static int anxiety_init_queue(struct request_queue *q,
			       struct elevator_type *et)
{
	struct anxiety_data *ad;

	ad = kmalloc(sizeof(*ad), GFP_KERNEL);
	if (!ad)
		return -ENOMEM;

	ad->q = q;
	ad->queue_depth = anxiety_queue_depth;
	ad->current_depth = anxiety_queue_depth;
	ad->last_access = jiffies;

	timer_setup(&ad->adapt_timer, anxiety_adapt_fn, 0);
	mod_timer(&ad->adapt_timer,
		  jiffies + ANXIETY_ADAPT_INTERVAL);

	q->elevator->e_data = ad;
	return 0;
}

static void anxiety_exit_queue(struct elevator_queue *eq)
{
	struct anxiety_data *ad = eq->e_data;

	if (ad) {
		del_timer_sync(&ad->adapt_timer);
		kfree(ad);
	}
}

static struct elevator_type io_scheduler_anxiety = {
	.ops = {
		.init_queue = anxiety_init_queue,
		.exit_queue = anxiety_exit_queue,
	},
	.elevator_name = ANXIETY_NAME,
	.elevator_opts = ELV_OPT_BASE,
	.is_disabled = false,
	.owner = THIS_MODULE,
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
MODULE_DESCRIPTION("Anxiety Adaptive I/O Scheduler for 4.19");
