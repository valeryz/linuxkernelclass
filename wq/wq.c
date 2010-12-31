/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: t; -*- */
/*
 *
 * Work Queue
 *
 * A very dumb abbreviated alternative implementation of the work queues
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/compiler.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/cpumask.h>

#ifndef CONFIG_PROC_FS
#error Enable procfs support in kernel
#endif

#define MY_MOD_NAME "wq"

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_info;


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

static LIST_HEAD(workqueues);

struct work_item
{
	void (*task)(unsigned long data);
	unsigned long data;
	struct list_head *list;
};

struct wq {
	struct list_head *wq_list;
	struct list_head *work_items;
	struct mutex wq_lock;
};

static wait_queue_head_t wq_global_waitq;

static struct wq *default_wq;
static struct task_struct *worker_threads;

/*
 * TODO: worker threads shall wait on a global wait queue, no
 * wait queues for a given 'work queue'
 */

struct wq *wq_create(void)
{
	struct wq *new_wq = kcalloc(1, sizeof(*new_wq), GPF_KERNEL);
	if (unlikely(new_wq == NULL))
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(new_wq->work_items);
	mutex_init(&new_wq->wq_lock);

	/* TODO: add item to the list (syncrhonized - rcu?) */
	list_add(new_wq->wq_list, workqueues);

	init_waitqueue_head(&new_wq->waitq);
	return new_wq;
}
EXPORT_SYMBOL(create_wq);

void wq_destroy(struct wq *wq)
{
	/* TODO: synchronization */

	/* destroy all work items in the queue */
	mutex_lock(&wq->wq_lock);

	while (!list_empty(wq->work_items)) {
		/* the work will never be done, ignore it */
		struct work_item *wi = list_entry(wq->work_items,
						  work_item, list);
		list_del(wq->work_items);
		kfree(wi);
	}

	list_del(wq->wq_list);

	mutex_unlock(&wq->wq_lock);
	kfree(wq);
}
EXPORT_SYMBOL(destroy_wq);

int wq_add_delayed_work(struct wq *wq, void (*task)(unsigned long data),
			unsigned long data, unsigned long delay)
{
	struct work_item *wi = kcalloc(1, sizeof(*wi), GPF_KERNEL);

	/* TODO: handle delay, ignoring for now */
	(void)delay;
	
	if (unlikely(wi == NULL))
		return -ENOMEM;

	wi->task = task;
	wi->data = data;
	mutex_lock(&wq->wq_lock);
	list_add(wi->list, wq->work_items);
	mutex_unlock(&wq->wq_lock);
	wake_up(&wq->waitq);
	return 0;
}
EXPORT_SYMBOL(wq_add_work_timeout);

int wq_add_work(struct wq *wq, void (*task)(unsigned long data),
		unsigned long data)
{
	return wq_add_work_timeout(wq, task, data, 0);
}
EXPORT_SYMBOL(wq_add_work);

static int read_proc(char *page, char **start, off_t off,
		     int count, int *eof, void *data)
{
	unsigned long diff;
	int n;

	n = snprintf(page, count, "Work Queue module");
	*eof = 1;
	return (n);
}

static int __init setup_procfs_entry(void)
{
	proc_dir = proc_mkdir(MY_MOD_NAME, NULL);
	if (unlikely(!proc_dir)) {
		printk(KERN_ERR "unable to create /proc/%s\n", MY_MOD_NAME);
		return (-1);
	}

	proc_info = create_proc_read_entry(MY_MOD_NAME, S_IRUGO, proc_dir,
					   read_proc, NULL);
	if (unlikely(!proc_info)) {
		printk(KERN_ERR "cannot create proc entry\n");
		remove_proc_entry(MY_MOD_NAME, NULL);
		return (-1);
	}

	proc_info->read_proc = read_proc;
	proc_info->data = NULL;

	return (0);
}

static int __init wq_init(void)
{
	int err;
	int cpu;
	
	if (unlikely(setup_procfs_entry() == -1))
		return (-1);

	/* TODO: create default work queue */
	default_wq = wq_create();
	if (IS_ERR(default_wq))
		return PTR_ERR(default_wq);

	/* spawn as many threads as necessary */
	worker_threads = kcalloc(nr_cpu_ids, sizeof(*worker_threads),
				 GPF_KERNEL);
	if (worker_threads == NULL) {
		err = -ENOMEM;
		goto err;
	}

	for (cpu=0; cpu < nr_cpu_ids; cpu++) {
		worker_threads[cpu] = kthread_create(worker_thread, NULL,
						   "wq_worker/%d", cpu);
		if (IS_ERR(worker_threads[cpu])) {
			err = PTR_ERR(worker_threads[cpu]);
			goto err;
		}
	}

	for (cpu=0; cpu < nr_cpu_ids; cpu++) {
		kthread_bind(worker_thread[cpu], cpu);
		wake_up_process(worker_thread[cpu]);
	}

	DBG(2, KERN_INFO, "wq loaded\n");

	return (0);

err:
	if (worker_threads) {
		
		kthread_stop(
	}
	return err;
}

static void __exit cleanup_procfs_entry(void)
{
	if (likely(proc_info))
		remove_proc_entry(MY_MOD_NAME, proc_dir);
	if (likely(proc_dir))
		remove_proc_entry(MY_MOD_NAME, NULL);
}

static void __exit wq_exit(void)
{
	cleanup_procfs_entry();
	DBG(2, KERN_INFO, "wq unloaded\n");
}

module_init(wq_init);
module_exit(wq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Valeriy Zamarayev <valeriy.zamarayev@gmail.com>");
MODULE_DESCRIPTION("Work queue implementation");
