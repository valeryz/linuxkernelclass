/*
 * The module that beeps
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/input.h>

#ifndef CONFIG_PROC_FS
#error Enable procfs support in kernel
#endif

#define BEEP_MOD_NAME "beepmod"

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
			printk(kern_level (BEEP_MOD_NAME "[%s:%u]: ") fmt, \
			       __func__, __LINE__,			 \
			       ## __VA_ARGS__);				 \
		}							 \
	} while (0)
#else
#define DBG(...)
#endif

/* on/off beep time, in ms */
#define BEEP_ON_TIME 100
#define BEEP_OFF_TIME 500

static struct input_handler beep_handler;

static void beep_function(unsigned long count);

DEFINE_TIMER(beep_timer, &beep_function, 0, 0);

static int sound_helper(struct input_handle *handle, void *data)
{

	if (test_bit(EV_SND, dev->evbit)) {
		if (test_bit(SND_TONE, dev->sndbit)) {
			input_inject_event(handle, EV_SND, SND_TONE, *hz);
			if (*hz)
				return 0;
		}
		if (test_bit(SND_BELL, dev->sndbit))
			input_inject_event(handle, EV_SND, SND_BELL, *hz ? 1 : 0);
	}

	return 0;
}

static void beep_function(unsigned long count)
{
	unsigned long hz = (count % 2) ? 0 : 1000;

	input_handler_for_each_handle(&beep_handler, &hz, sound_helper);

	if (count > 8)
		return;

	beep_timer.expires = jiffies + HZ *
		((count % 2 == 0) ? BEEP_ON_TIME : BEEP_OFF_TIME) / 1000;

	beep_timer.data = count + 1;
	add_timer(&beep_timer);
}

static int write_proc(struct file *file, const char __user *buffer,
		      unsigned long count, void *data)
{
	unsigned long hz = 0;

	del_timer_sync(&beep_timer);
	input_handler_for_each_handle(&beep_handler, &hz, sound_helper);

	beep_timer.data = 1;
	mod_timer(&beep_timer, jiffies + (BEEP_OFF_TIME * HZ / 1000));

	return (count);		/* as if we've read up everything */
}

static int __init setup_procfs_entry(void)
{
	proc_dir = proc_mkdir(BEEP_MOD_NAME, NULL);
	if (unlikely(!proc_dir)) {
		printk(KERN_ERR "unable to create /proc/%s\n", BEEP_MOD_NAME);
		return (-1);
	}

	proc_info = create_proc_entry(BEEP_MOD_NAME, S_IRUGO, proc_dir);
	if (unlikely(!proc_info)) {
		printk(KERN_ERR "cannot create proc entry\n");
		remove_proc_entry(BEEP_MOD_NAME, NULL);
		return (-1);
	}

	proc_info->read_proc = NULL;
	proc_info->write_proc = write_proc;
	proc_info->data = NULL;

	return (0);
}

static int __init beepmod_init(void)
{
	if (unlikely(setup_procfs_entry() == -1))
		return (-1);

	DBG(2, KERN_INFO, "beepmod loaded\n");
	return (0);
}

static void __exit cleanup_procfs_entry(void)
{
	if (likely(proc_info))
		remove_proc_entry(BEEP_MOD_NAME, proc_dir);
	if (likely(proc_dir))
		remove_proc_entry(BEEP_MOD_NAME, NULL);
}

static void __exit beepmod_exit(void)
{
	cleanup_procfs_entry();
	DBG(2, KERN_INFO, "beepmod unloaded\n");
}

module_init(beepmod_init);
module_exit(beepmod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Valeriy Zamarayev <valeriy.zamarayev@gmail.com>");
MODULE_DESCRIPTION("Module that beeps 5 times on /proc write");
