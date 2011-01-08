/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: t; -*- */
/*
 *
 * Work Queue
 *
 * A very dumb abbreviated alternative implementation of the work queues
 */

#include <linux/init.h> 
#include <linux/module.h>
#include <linux/stat.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/jiffies.h>
#include <linux/compiler.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/cpumask.h>
#include <linux/freezer.h>
#include <linux/sched.h>

#define MY_MOD_NAME "wq"

#ifdef DEBUG
static unsigned int debug_level = 0;
module_param(debug_level, uint, S_IRUGO|S_IWUSR);

#define DBG(level, kern_level, fmt, ...)				 \
	do {								 \
		if (level <= debug_level) {				 \
			printk(kern_level (MY_MOD_NAME "[%s:%u]: ") fmt, \
			       __func__, __LINE__,			 \
			       ## __VA_ARGS__);				 \
		}							 \
	} while (0)
#else
#define DBG(...)
#endif

/* list of work items, which consistitues a work queue */
struct work_item
{
	void (*task)(unsigned long data);
	unsigned long data;
	struct list_head list;
};

/* work queues */
struct wq {
	struct list_head wq_list;
	struct list_head work_items;
};

/* global list of work queues */
static LIST_HEAD(work_queues);

/* all synchronization is done via a single global spin lock */
DEFINE_SPINLOCK(work_queues_lock);

/* worker threads wait using this wait queue */
DECLARE_WAIT_QUEUE_HEAD(worker_waitq);

/* default wait queue */
static struct wq *default_wq;

/* array of worker threads, there will be as many threads as there are CPUs */
static struct task_struct **worker_threads;

struct wq *wq_create(void)
{
	struct wq *new_wq = kcalloc(1, sizeof(*new_wq), GFP_KERNEL);
	if (unlikely(new_wq == NULL))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&new_wq->work_items);

	spin_lock(&work_queues_lock);
	list_add(&new_wq->wq_list, &work_queues);
	spin_unlock(&work_queues_lock);

	return new_wq;
}
EXPORT_SYMBOL(wq_create);

void wq_destroy(struct wq *wq)
{
	spin_lock(&work_queues_lock);
	while (!list_empty(&wq->work_items)) {
		/* the work will never be done, ignore it */
		struct work_item *wi = list_entry(&wq->work_items,
						  struct work_item, list);
		list_del(&wq->work_items);
		kfree(wi);
	}
	list_del(&wq->wq_list);
	spin_unlock(&work_queues_lock);
	kfree(wq);
}
EXPORT_SYMBOL(wq_destroy);

int wq_add_delayed_work(struct wq *wq, void (*task)(unsigned long data),
			unsigned long data, unsigned long delay)
{
	struct work_item *wi = kcalloc(1, sizeof(*wi), GFP_KERNEL);

	/* TODO: handle delay, ignoring for now */
	(void)delay;

	if (unlikely(wi == NULL))
		return -ENOMEM;

	wi->task = task;
	wi->data = data;
	spin_lock(&work_queues_lock);
	list_add(&wi->list, &wq->work_items);
	spin_unlock(&work_queues_lock);
	wake_up(&worker_waitq);
	return 0;
}
EXPORT_SYMBOL(wq_add_delayed_work);

int wq_add_work(struct wq *wq, void (*task)(unsigned long data),
		unsigned long data)
{
	if (wq == NULL)
		wq = default_wq;
	return wq_add_delayed_work(wq, task, data, 0);
}
EXPORT_SYMBOL(wq_add_work);

/*
 * look for a work item, and if one is found, return it
 */
static struct work_item *find_work(void)
{
	struct work_item *wi = NULL;
	struct wq *wq = list_first_entry(&work_queues, struct wq, wq_list);
	if (wq) {
		wi = list_first_entry(&wq->work_items, struct work_item, list);
		/* next time start from the next queue */
		list_rotate_left(&work_queues);
	}
	return wi;
}

static int worker_thread_fn(void *data)
{
	struct work_item *wi = NULL;

	(void)data;

	DBG(2, KERN_DEBUG, "New thread started: pid %d, comm %s\n",
	    current->pid, current->comm);

	/* Allow the thread to be frozen */
	set_freezable();

	for (;;) {
		DEFINE_WAIT(wait);

		/* do all work that we can get */
		while (wi) {
			/* do rest of the work w/o sleeping */
			DBG(0, KERN_DEBUG, "Doing work: %p", wi->task);
			(*wi->task)(wi->data);

			if (kthread_should_stop())
				return 0;

			try_to_freeze();

			spin_lock(&work_queues_lock);
			wi = find_work();
			spin_unlock(&work_queues_lock);
		}

		/* now there's no work, so we must wait until something
		   arrives */
		while (!wi) {
			prepare_to_wait_exclusive(&worker_waitq, &wait,
						  TASK_INTERRUPTIBLE);

			if (kthread_should_stop())
				return 0;

			if (try_to_freeze())
				continue;

			spin_lock(&work_queues_lock);
			wi = find_work();
			spin_unlock(&work_queues_lock);
			if (!wi)
				schedule();
		}
		finish_wait(&worker_waitq, &wait);
	}

	return 0;
}

static int __init wq_init(void)
{
	int err;
	int cpu;

	/* create default work queue */
	default_wq = wq_create();
	if (IS_ERR(default_wq))
		return PTR_ERR(default_wq);

	/* spawn as many threads as necessary */
	worker_threads = kcalloc(nr_cpu_ids, sizeof(*worker_threads),
				 GFP_KERNEL);
	if (worker_threads == NULL) {
		err = -ENOMEM;
		goto err;
	}

	for (cpu=0; cpu < nr_cpu_ids; cpu++) {
		worker_threads[cpu] = kthread_create(worker_thread_fn, NULL,
						     "wq_worker/%d", cpu);
		if (IS_ERR(worker_threads[cpu])) {
			err = PTR_ERR(worker_threads[cpu]);
			goto err;
		}
	}

	for (cpu=0; cpu < nr_cpu_ids; cpu++) {
		kthread_bind(worker_threads[cpu], cpu);
		wake_up_process(worker_threads[cpu]);
	}

	DBG(2, KERN_INFO, "wq loaded\n");

	return (0);

err:
	if (worker_threads) {
		/* it cost me half a day to really learn that you don't have
		 * to free tasks_struct's resulting from kthread_create */
		kfree(worker_threads);
	}
	if (default_wq)
		wq_destroy(default_wq);

	return err;
}

static void __exit wq_exit(void)
{
	int cpu;

	for (cpu=0; cpu < nr_cpu_ids; cpu++) {
		(void)kthread_stop(worker_threads[cpu]);
	}

	spin_lock(&work_queues_lock);
	while (!list_empty(&work_queues)) {
		struct wq *wq = list_entry(&work_queues, struct wq, wq_list);
		list_del(&work_queues);
		wq_destroy(wq);
	}
	spin_unlock(&work_queues_lock);
	
	DBG(2, KERN_INFO, "wq unloaded\n");
}

module_init(wq_init);
module_exit(wq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Valeriy Zamarayev <valeriy.zamarayev@gmail.com>");
MODULE_DESCRIPTION("Work queue implementation");
