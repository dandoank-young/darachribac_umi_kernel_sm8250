// SPDX-License-Identifier: GPL-2.0
/*
 * Simple Low Memory Killer
 *
 * A lightweight in-kernel low memory killer that monitors free memory
 * and kills processes when thresholds are crossed. More responsive
 * than the standard OOM killer for Android workloads.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/workqueue.h>
#include <linux/swap.h>
#include <linux/oom.h>
#include <linux/rcupdate.h>

#define SIMPLE_LMK_NAME "simple_lmk"
#define SIMPLE_LMK_DELAY_MS 2000
#define SIMPLE_LMK_MINFREE_PERCENT 3

static int minfree_percent = SIMPLE_LMK_MINFREE_PERCENT;
module_param(minfree_percent, int, 0644);
MODULE_PARM_DESC(minfree_percent, "Free memory threshold percentage (default=3)");

static struct workqueue_struct *lmk_wq;
static struct delayed_work lmk_work;

static unsigned long get_free_memory_kb(void)
{
	return nr_free_pages() * (PAGE_SIZE / 1024);
}

static unsigned long get_total_memory_kb(void)
{
	return totalram_pages * (PAGE_SIZE / 1024);
}

static int simple_lmk_kill_one(void)
{
	struct task_struct *p, *selected = NULL;
	long max_oom_score = -4000;
	unsigned long max_rss = 0;
	int killed = 0;

	rcu_read_lock();
	for_each_process(p) {
		long oom_score;
		unsigned long rss = 0;

		if (p->flags & PF_KTHREAD)
			continue;
		if (p->pid == 1)
			continue;
		if (p == current)
			continue;

		oom_score = p->signal->oom_score_adj;
		if (p->mm)
			rss = get_mm_rss(p->mm);

		if (oom_score > max_oom_score ||
		    (oom_score == max_oom_score && rss > max_rss)) {
			max_oom_score = oom_score;
			max_rss = rss;
			selected = p;
		}
	}
	rcu_read_unlock();

	if (!selected)
		return -ESRCH;

	pr_info("%s: killing %s(%d) oom_adj=%ld rss=%luKB\n",
		SIMPLE_LMK_NAME, selected->comm,
		selected->pid, max_oom_score, max_rss);

	send_sig(SIGKILL, selected, 1);
	killed = 1;

	return killed ? 0 : -ESRCH;
}

static void simple_lmk_work_func(struct work_struct *work)
{
	int killed;
	unsigned long free_kb, total_kb, threshold_kb;

	free_kb = get_free_memory_kb();
	total_kb = get_total_memory_kb();
	threshold_kb = total_kb * minfree_percent / 100;

	if (free_kb < threshold_kb) {
		/* Kill up to 5 processes per cycle */
		int i;
		for (i = 0; i < 5; i++) {
			killed = simple_lmk_kill_one();
			if (killed < 0)
				break;
		}
		if (killed >= 0)
			pr_info("%s: killed %d process(es), free=%luKB total=%luKB\n",
				SIMPLE_LMK_NAME, i, free_kb, total_kb);
	}

	/* Schedule next check */
	INIT_DELAYED_WORK(&lmk_work, simple_lmk_work_func);
	schedule_delayed_work(&lmk_work, msecs_to_jiffies(SIMPLE_LMK_DELAY_MS));
}

static int __init simple_lmk_init(void)
{
	lmk_wq = alloc_workqueue(SIMPLE_LMK_NAME,
				 WQ_MEM_RECLAIM | WQ_SYSFS, 1);
	if (!lmk_wq)
		return -ENOMEM;

	INIT_DELAYED_WORK(&lmk_work, simple_lmk_work_func);
	schedule_delayed_work(&lmk_work, msecs_to_jiffies(SIMPLE_LMK_DELAY_MS));

	pr_info("%s: initialized (threshold=%d%%)\n",
		SIMPLE_LMK_NAME, minfree_percent);
	return 0;
}

static void __exit simple_lmk_exit(void)
{
	cancel_delayed_work_sync(&lmk_work);
	destroy_workqueue(lmk_wq);
	pr_info("%s: removed\n", SIMPLE_LMK_NAME);
}

module_init(simple_lmk_init);
module_exit(simple_lmk_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("UMI Kernel Team");
MODULE_DESCRIPTION("Simple Low Memory Killer for Android 4.19");
