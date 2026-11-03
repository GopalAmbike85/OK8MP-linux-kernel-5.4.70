// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx SDFEC
 *
 * Copyright (C) 2019 Xilinx, Inc.
 *
 * Description:
 * This driver is developed for SDFEC16 (Soft Decision FEC 16nm)
 * IP. It exposes a char device which supports file operations
 * like  open(), close() and ioctl().
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/syslog.h>

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/sched/clock.h>

#include <asm/uaccess.h>
#include <asm/io.h>

//#include <linux/forlinx_debug.h>

#define LIST_LENTH 40 * 1024
#define MSG_LEN 256

DEFINE_SPINLOCK(forlinx_debug_logbuf_lock);
DECLARE_WAIT_QUEUE_HEAD(forlinx_debug_log_wait);

static int msg_count = 0;
static LIST_HEAD(forlinx_debug_list);

struct ram_message {
	char buf[MSG_LEN];
	struct list_head list;
};

static int forlinx_debug_list_is_empty(void)
{
        return forlinx_debug_list.next == &forlinx_debug_list;
}

static size_t print_time(u64 ts, char *buf)
{
	unsigned long rem_nsec;
            
	rem_nsec = do_div(ts, 1000000000);
	if (!buf)
		return snprintf(NULL, 0, "[%5lu.000000] ", (unsigned long)ts);
              
	return sprintf(buf, "[%5lu.%06lu] ",
			(unsigned long)ts, rem_nsec / 1000);
}

void forlinx_debug(const char *fmt, ...)
{
	va_list ap;
	int n;
	size_t len;
	struct ram_message *cn = kzalloc(sizeof(*cn), GFP_KERNEL);
	u64 ts_nsec = local_clock();
                                
	len = print_time(ts_nsec, cn->buf);
	va_start(ap, fmt);
	n = vscnprintf(cn->buf + len, sizeof(cn->buf) - len, fmt, ap);
	va_end(ap);
                        
	spin_lock_irq(&forlinx_debug_logbuf_lock);
	list_add_tail(&cn->list, &forlinx_debug_list);
	msg_count++;
	spin_unlock_irq(&forlinx_debug_logbuf_lock);

	if(msg_count >= LIST_LENTH ) {
		struct list_head *tmp;
		spin_lock_irq(&forlinx_debug_logbuf_lock);
		tmp = forlinx_debug_list.next;
		list_del_init(tmp);
		msg_count--;
		spin_unlock_irq(&forlinx_debug_logbuf_lock);
		cn = list_entry(tmp, struct ram_message, list);
		kfree(cn);
	}
                        
	wake_up_interruptible(&forlinx_debug_log_wait);
}
EXPORT_SYMBOL(forlinx_debug);
        
static int dump_forlinx_debug(char __user *buf, int len)
{
       	struct ram_message *cn;
	struct list_head *pos;
	int ret;
                       
       	ret = wait_event_interruptible(forlinx_debug_log_wait,
			(!forlinx_debug_list_is_empty()));
                        
	if(ret)
		return ret;
                                
	spin_lock_irq(&forlinx_debug_logbuf_lock);
	pos = forlinx_debug_list.next;
	list_del(pos);
	msg_count--;
	spin_unlock_irq(&forlinx_debug_logbuf_lock);
                                
	if(pos == &forlinx_debug_list) 
		return 0;
                                
	cn = list_entry(pos, struct ram_message, list);
	if(len > strlen(cn->buf))
		len = strlen(cn->buf);
                        
	len = len > MSG_LEN ? MSG_LEN : len;
	ret = copy_to_user(buf, cn->buf, len);
                                
	kfree(cn);
                        
	return ret ? -EFAULT : len;
}

static int kmsg_open(struct inode * inode, struct file * file)
{
	return 0;
}

static int kmsg_release(struct inode * inode, struct file * file)
{
	return 0;
}

static ssize_t kmsg_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	if ((file->f_flags & O_NONBLOCK) &&		
			forlinx_debug_list_is_empty())
		return -EAGAIN;

	return dump_forlinx_debug(buf, count);
}

static unsigned int kmsg_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &forlinx_debug_log_wait, wait);
	if (!forlinx_debug_list_is_empty())
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations proc_kmsg_operations = {
	.read		= kmsg_read,
	.poll		= kmsg_poll,
	.open		= kmsg_open,
	.release	= kmsg_release,
	.llseek		= generic_file_llseek,
};

static int __init forlinx_debug_init(void)
{
        spin_lock_init(&forlinx_debug_logbuf_lock);
	proc_create("kmsg2", S_IRUSR, NULL, &proc_kmsg_operations);
        return 0;
}

static void __exit forlinx_debug_exit(void)
{
}

module_init(forlinx_debug_init);
module_exit(forlinx_debug_exit);

MODULE_AUTHOR("Forlinx, Inc");
MODULE_DESCRIPTION("Forlinx Debug Driver");
MODULE_LICENSE("GPL");
