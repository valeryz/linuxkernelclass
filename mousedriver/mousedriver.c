/* -*- mode: c: c-basic-offset: 8; indent-tabs-mode: t; -*-
 * Mouse Driver Module
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>

#ifndef CONFIG_PROC_FS
#error Enable procfs support in kernel
#endif

#define MOD_NAME "mousedrv"

struct mouse_drv
{
	atomic_t event_count;
};

/* Ok, I understand the value of gathering all module vars in a sturct, but
 * let's not complicate things for such as simple module. They are static so
 * pose no problem for the linker */
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_info;

#ifdef DEBUG
static unsigned int debug_level = 0;
module_param(debug_level, uint, S_IRUGO|S_IWUSR);

#define DBG(level, kern_level, fmt, ...)				 \
	do {								 \
		if (level <= debug_level) {				 \
			printk(kern_level (MOD_NAME "[%s:%u]: ") fmt, \
			       __func__, __LINE__,			 \
			       ## __VA_ARGS__);				 \
		}							 \
	} while (0)
#else
#define DBG(...)
#endif

static int read_proc(char *page, char **start, off_t off,
		     int count, int *eof, void *data)
{
	unsigned long diff;
	int n;


	*eof = 1;
	return (0);
}

static int __init setup_procfs_entry(void)
{
	proc_dir = proc_mkdir(MOD_NAME, NULL);
	if (unlikely(!proc_dir)) {
		printk(KERN_ERR "unable to create /proc/%s\n", MOD_NAME);
		return (-1);
	}

	proc_info = create_proc_read_entry(MOD_NAME, S_IRUGO, proc_dir,
					   read_proc, NULL);
	if (unlikely(!proc_info)) {
		printk(KERN_ERR "cannot create proc entry\n");
		remove_proc_entry(MOD_NAME, NULL);
		return (-1);
	}

	proc_info->read_proc = read_proc;
	proc_info->data = NULL;

	return (0);
}

static irqreturn_t mouse_interrupt(int irq, void *data)
{
	struct mouse_drv *drv = data;
	atomic_inc(&drv->event_count);
	return IRQ_HANDLED;
}

static int __init mousedrv_init(void)
{
	int irq;
	int res;
	
	if (unlikely(setup_procfs_entry() == -1))
		return (-1);

	DBG(2, KERN_INFO, "mousedrv loaded\n");

        /* setup interrupt handler */
	irq = 12; 		/* interrupt for the mouse */
        res = request_irq(irq, mouse_interrupt, IRQF_SHARED, "mymouse", NULL);
	return res;
}

static void __exit cleanup_procfs_entry(void)
{
	free_irq(12, NULL);
	
	if (likely(proc_info))
		remove_proc_entry(MOD_NAME, proc_dir);
	if (likely(proc_dir))
		remove_proc_entry(MOD_NAME, NULL);
}

static void __exit mousedrv_exit(void)
{
	cleanup_procfs_entry();
	DBG(2, KERN_INFO, "mousedrv unloaded\n");
}

module_init(mousedrv_init);
module_exit(mousedrv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Valeriy Zamarayev <valeriy.zamarayev@gmail.com>");
MODULE_DESCRIPTION("Mouse driver module");
