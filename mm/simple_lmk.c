// SPDX-License-Identifier: GPL-2.0
/*
 * Simple Low Memory Killer
 *
 * A simplified LMK that monitors free memory and kills processes
 * when thresholds are crossed. More responsive than the standard
 * OOM killer for low-memory situations on Android devices.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/jiffies.h>
#include <asm/div64.h>

#define SIMPLE_LMK_NAME "simple_lmk"
#define SIMPLE_LMK_DELAY msecs_to_jiffies(2000)
#define SIMPLE_LMK_MIN_FREE_PAGES 4096  /* 16MB at 4K pages */

static int lmk_minfree[8] = {
	2048, 3072, 4096, 5120,
	8192, 10240, 12288, 16384
};
static int lmk_maxundo[8] = {
	-1, -1, -1, -1,
	-1, -1, -1, -1
};
static int lmk_deadline = 5;

static struct workqueue_struct *lmk_wq;
static struct work_struct lmk_work;

static inline unsigned long get_free_memory_kb(void)
{
	return zone_page_state(&contig_page_dataZoneId, NR_FREE_PAGES) *
		(PAGE_SIZE / 1024);
}

static int simple_lmk_kill_process(void)
{
	struct task_struct *p, *selected = NULL;
	long totalpoints = 0;
	int oom_score;

	/* Find the task with highest oom_score (least important) */
	rcu_read_lock();
	for_each_process(p) {
		if (p->flags & PF_KTHREAD)
			continue;
		if (task_is_stopped_or_traced(p))
			continue;
		oom_score = p->signal->oom_score;
		totalpoints += oom_score;
		if (!selected || oom_score > selected->signal->oom_score)
			selected = p;
	}
	rcu_read_unlock();

	if (!selected)
		return -ESRCH;

	pr_info("%s: killing process %s(%d) oom_score=%d\n",
		SIMPLE_LMK_NAME, selected->comm,
		task_pid_nr(selected), selected->signal->oom_score);

	get_task_struct(selected);
	rcu_read_lock();
	task_dump_lock_hold(selected);
	rcu_read_unlock();
	cancel_work_sync(&selected->pending_work);
	send_sig(SIGKILL, selected, 1);
	put_task_struct(selected);

	return 0;
}

static void simple_lmk_check(struct work_struct *work)
{
	unsigned long free_kb;
	int i;

	free_kb = get_free_memory_kb();

	for (i = 0; i < ARRAY_SIZE(lmk_minfree); i++) {
		int minfree_kb = lmk_minfree[i] * 4; /* convert pages to KB */
		if (free_kb < minfree_kb) {
			int kills = lmk_deadline;
			while (kills > 0) {
				if (simple_lmk_kill_process() != 0)
					break;
				kills--;
			}
			break;
		}
	}

	schedule_work_delayed(&lmk_work, SIMPLE_LMK_DELAY);
}

static int __init simple_lmk_init(void)
{
	lmk_wq = alloc_workqueue(SIMPLE_LMK_NAME,
				 WQ_MEM_RECLAIM | WQ_SYSFS, 1);
	if (!lmk_wq)
		return -ENOMEM;

	INIT_WORK(&lmk_work, simple_lmk_check);
	schedule_work(&lmk_work);

	pr_info("%s: initialized\n", SIMPLE_LMK_NAME);
	return 0;
}

static void __exit simple_lmk_exit(void)
{
	cancel_work_sync(&lmk_work);
	destroy_workqueue(lmk_wq);
	pr_info("%s: removed\n", SIMPLE_LMK_NAME);
}

module_init(simple_lmk_init);
module_exit(simple_lmk_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("UMI Kernel Team");
MODULE_DESCRIPTION("Simple Low Memory Killer for Android");
