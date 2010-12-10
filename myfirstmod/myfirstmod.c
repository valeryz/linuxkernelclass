/*
 * My first Linux kernel module. Mostly c/p from skeleton :)
 *
 * In its /proc entry it outputs the number of ms it has been loaded
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/compiler.h>

#ifndef CONFIG_PROC_FS
#error Enable procfs support in kernel
#endif

#define MY_MOD_NAME "myfirstmod"

/* Ok, I understand the value of gathering all module vars in a sturct, but
 * let's not complicate things for such as simple module. They are static so
 * pose no problem for the linker */
static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_info;
static unsigned long load_jiffies;

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

static int read_proc(char *page, char **start, off_t off,
		     int count, int *eof, void *data)
{
	unsigned long diff = jiffies - load_jiffies;
	int n = snprintf(page, count,
			 "The moudle has been loaded for %lu jiffies\n", diff);
	*eof = 1;
	return n;
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

static int __init myfirstmod_init(void)
{
	if (unlikely(setup_procfs_entry() == -1))
		return (-1);

	load_jiffies = jiffies;

	DBG(2, KERN_INFO, "myfirstmod loaded\n");
	return (0);
}

static void __exit cleanup_procfs_entry(void)
{
	if (likely(proc_info))
		remove_proc_entry(MY_MOD_NAME, proc_dir);
	if (likely(proc_dir))
		remove_proc_entry(MY_MOD_NAME, NULL);
}

static void __exit myfirstmod_exit(void)
{
	cleanup_procfs_entry();
	DBG(2, KERN_INFO, "myfirstmod unloaded\n");
}

module_init(myfirstmod_init);
module_exit(myfirstmod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Valeriy Zamarayev <valeriy.zamarayev@gmail.com>");
MODULE_DESCRIPTION("My first kernel module (jiffies counter)");
