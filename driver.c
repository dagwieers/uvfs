/*
 *   driver.c -- conduit from kernel to user space
 *
 *   Copyright (C) 2002      Britt Park
 *   Copyright (C) 2004-2007 Interwoven, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include "uvfs.h"

static int uvfsd_open(struct inode *, struct file *);
static int uvfsd_release(struct inode *, struct file *);
static ssize_t uvfsd_read(struct file *, char *, size_t, loff_t *);
static ssize_t uvfsd_write(struct file *, const char *, size_t, loff_t *);
static int uvfsd_ioctl(struct inode *, struct file *, unsigned int, unsigned long);

/*
 * file operations defined for the pmfs
 * device driver which appears in /proc/fs
 *
 */
struct file_operations Uvfsd_file_operations =
{
    .open           = uvfsd_open,
    .release        = uvfsd_release,
    .read           = uvfsd_read,
    .write          = uvfsd_write,
    .ioctl          = uvfsd_ioctl,
};

static struct proc_dir_entry* uvfs_proc_file;

static wait_queue_head_t Uvfs_driver_queue;
static spinlock_t Uvfs_lock;

static int ShuttingDown = 0;
static int Serial_number = 0;

extern int uvfs_use_count;      // module use count

LIST_HEAD(Uvfs_requests);
LIST_HEAD(Uvfs_replies);

static char* Op_names[] =
{
    "NULL",
    "write",
    "read",
    "create",
    "lookup",
    "unlink",
    "symlink",
    "mkdir",
    "rmdir",
    "rename",
    "readdir",
    "setattr",
    "getattr",
    "statfs",
    "read_super",
    "readlink",
    "shutdown",
    "LAST + 1"
};


/*
 * open the driver for access by server file system thread
 * driver can be opened muliple times by different threads
 */
static int uvfsd_open(struct inode* inode, struct file* file)
{
    dprintk("Entering uvfsd_open\n");
    uvfs_use_count++;
    dprintk("Exiting uvfsd_open\n");
    return 0;
}


/*
 * close the driver from file system thread
 * each thread must close the connection to unload driver
 */
static int uvfsd_release(struct inode* inode, struct file* filp)
{
    dprintk("Entering uvfsd_release\n");
    uvfs_use_count--;

    if(uvfs_use_count)
    {
         dprintk("Exiting uvfsd_release count %d\n",uvfs_use_count);
         return 0;
    }

    /* on last release reset shutdown and request serial number */
    ShuttingDown = 0;
    Serial_number = 0;
    dprintk("Exiting uvfsd_release ShutDown clear\n");
    return 0;
}


/* Reads are user space queries for fs request. */

static ssize_t uvfsd_read(struct file* filp,
                          char* buff,
                          size_t count,
                          loff_t* offset)
{
    int ret = 0;
    uvfs_transaction_s* trans;
    uvfs_generic_req_s* request;
    dprintk("<1>Entered uvfsd_read (%d)\n", current->pid);

    /* Check for bogus count. */
    if (count < sizeof(uvfs_request_u))
    {
        dprintk("<1>uvfsd_read EIO (%d)(%d)\n", count, sizeof(uvfs_request_u));
        return -EIO;
    }
    /* Grab the lock */
    spin_lock(&Uvfs_lock);
    /* Wait for a request */
    while (list_empty(&Uvfs_requests) || ShuttingDown)
    {
        wait_queue_t wait;
        if (ShuttingDown)
        {
            uvfs_shutdown_req_s req;
            int size;
            spin_unlock(&Uvfs_lock);
            req.type = UVFS_SHUTDOWN;
            size = req.size = sizeof(req);
            ret = copy_to_user(buff, &req, req.size);
            spin_lock(&Uvfs_lock);
            wake_up_interruptible(&Uvfs_driver_queue);
            spin_unlock(&Uvfs_lock);
            if(ret)
                return -EIO;
            else
                return size;
        }
        dprintk("<1>uvfsd_read: About to sleep for request\n");
        init_waitqueue_entry(&wait, current);
        add_wait_queue_exclusive(&Uvfs_driver_queue, &wait);
        dprintk("<1>uvfsd_read: add_wait_queue_exclusive\n");
        set_current_state(TASK_INTERRUPTIBLE);
        spin_unlock(&Uvfs_lock);
        schedule();
        spin_lock(&Uvfs_lock);
        dprintk("<1>uvfsd_read: set_current_state\n");
        set_current_state(TASK_RUNNING);
        remove_wait_queue(&Uvfs_driver_queue, &wait);
        if (signal_pending(current))
        {
            spin_unlock(&Uvfs_lock);
            dprintk("<1>Exited uvfsd_read: ERESTARTSYS\n");
            return -ERESTARTSYS;
        }
    }
    /* There is a request ready. */
    trans = list_entry(Uvfs_requests.next, uvfs_transaction_s, list);
    list_del_init(&trans->list);
    list_add_tail(&trans->list, &Uvfs_replies);
    /*
       This may be overkill but I can't prove to myself that
       there isn't a possibility of a request going unanswered.
    */
    wake_up_interruptible(&Uvfs_driver_queue);
    spin_unlock(&Uvfs_lock);
    request = &trans->u.request.generic;
    ret = copy_to_user(buff, request, request->size);
    dprintk("<1>Exited uvfsd_read: %d (%d)\n",
            request->size,
            current->pid);
    if(ret)
        return -EIO;
    else
        return request->size;
}


/* Writes are replies from the user space filesystem implementation. */

static ssize_t uvfsd_write(struct file* file,
                           const char* buff,
                           size_t count,
                           loff_t* offset)
{
    int ret = 0;
    uvfs_generic_rep_s reply;
    struct list_head* ptr;
    uvfs_transaction_s* trans = NULL;
    if (count < sizeof(uvfs_generic_rep_s))
    {
        dprintk("<1>uvfsd_write Undersized reply (%d).\n", count);
        return -EIO;
    }
    if (copy_from_user(&reply, buff, sizeof(reply)))
    {
        dprintk("<1>copy_from_user failed in uvfsd_write.\n");
        return -EFAULT;
    }
    dprintk("<1>Entered uvfsd_write: serial=%d (%d)\n",
            reply.serial,
            current->pid);
    if (reply.size != count)
    {
        dprintk("<1>Mismatched write size in uvfsd_write %d %d\n",
               reply.size, count);
        return -EINVAL;
    }
    spin_lock(&Uvfs_lock);
    dprintk("<1>uvfsd_write: Looking for transaction serial=%d\n",
            reply.serial);
    for (ptr = Uvfs_replies.next; ptr != &Uvfs_replies; ptr = ptr->next)
    {
        trans = list_entry(ptr, uvfs_transaction_s, list);
        if (trans->serial == reply.serial)
        {
            dprintk("<1>uvfsd_write: found transaction\n");
            break;
        }
        trans = NULL;
    }
    if (trans == NULL)
    {
        dprintk("<1>uvfsd_write: invalid reply %d\n", reply.serial);
        spin_unlock(&Uvfs_lock);
        return -EINVAL;
    }
    /* We have a transaction */
    list_del_init(&trans->list);
    spin_unlock(&Uvfs_lock);
    ret = copy_from_user(&trans->u.reply, buff, reply.size);
    wake_up_interruptible(&trans->fs_queue);
    trans->answered = 1;
    dprintk("<1>Exited uvfsd_write (%d)\n", current->pid);
    if(ret)
        return -EIO;
    else
        return reply.size;
}


/* Used to signal the user-space filesystem to shutdown, cmd = 0 */

static int uvfsd_ioctl(struct inode* inode, struct file* filp,
                       unsigned int cmd, unsigned long arg)
{
    switch (cmd)
    {
        case UVFS_IOCTL_SHUTDOWN:
        {
            if (uvfs_use_count)
            {
                dprintk("Entering uvfsd_ioctl SHUTDOWN\n");
                spin_lock(&Uvfs_lock);
                ShuttingDown = 1;
                wake_up_interruptible(&Uvfs_driver_queue);
                spin_unlock(&Uvfs_lock);
                return 0;
            }
            return -EPERM;
        }
        case UVFS_IOCTL_STATUS:
        {
            struct list_head* ptr;
            spin_lock(&Uvfs_lock);
            printk("<1>Pending Requests:\n");
            for (ptr = Uvfs_requests.next;
                 ptr != &Uvfs_requests;
                 ptr = ptr->next)
            {
                uvfs_transaction_s* trans =
                    list_entry(ptr, uvfs_transaction_s, list);
                printk("<1>SN: %d (%s)\n",
                       trans->serial, Op_names[trans->u.request.generic.type]);
            }
            printk("<1>Pending replies:\n");
            for (ptr = Uvfs_replies.next;
                 ptr != &Uvfs_replies;
                 ptr = ptr->next)
            {
                uvfs_transaction_s* trans =
                    list_entry(ptr, uvfs_transaction_s, list);
                printk("<1>SN: %d (%s)\n",
                       trans->serial, Op_names[trans->u.request.generic.type]);
            }
            spin_unlock(&Uvfs_lock);
            break;
        }
        case UVFS_IOCTL_MOUNT:
        {
            // make sure nothing is mounted
            return !list_empty(&Uvfs_file_system_type.fs_supers);
        }
        case UVFS_IOCTL_USE_COUNT:
        default:
            // return number of opens active on this module
            return uvfs_use_count;
    }
    return 0;
}


/* Safely go to sleep on a queue.  This makes the queue behave like
   a condition variable.  The spinlock must be locked on entry, and
   will be locked on exit. */

void safe_sleep_on(wait_queue_head_t* q, spinlock_t* s)
{
    wait_queue_t wait;

    dprintk("Entering safe_sleep_on\n");
    init_waitqueue_entry(&wait, current);

    dprintk("safe_sleep_on add_wait_exclusive\n");
    add_wait_queue_exclusive(q, &wait);
    set_current_state(TASK_INTERRUPTIBLE);
    spin_unlock(s);
    schedule();
    spin_lock(s);
    set_current_state(TASK_RUNNING);
    dprintk("safe_sleep_on remove_wait_queue\n");
    remove_wait_queue(q, &wait);
    if (signal_pending(current))
    {
        dprintk("<1>safe_sleep_on: ERESTARTSYS\n");
    }
    /* Others seem to do a set_current_state(TASK_RUNNING) here. However,
       it looks like wake_up should have already done this. Otherwise this
       thread would not have been scheduled. */
    dprintk("Exiting safe_sleep_on\n");
}

/* Returns true if we weren't interrupted. */

int uvfs_make_request(uvfs_transaction_s* trans)
{
    dprintk("Entering uvfs_make_request\n");
    spin_lock(&Uvfs_lock);
    list_add_tail(&trans->list, &Uvfs_requests);
    wake_up_interruptible(&Uvfs_driver_queue);
    safe_sleep_on(&trans->fs_queue, &Uvfs_lock);
    if (signal_pending(current))
    {
        spin_unlock(&Uvfs_lock);
        while (!trans->answered)
        {
            /* while there are no requests to process run other processes */
            schedule();
        }
        dprintk("Entering uvfs_make_request 0\n");
        return 0;
    }
    spin_unlock(&Uvfs_lock);
    dprintk("Entering uvfs_make_request 1\n");
    return 1;
}


/*
 * allocate a new tranaction request object and initialize it
 * this object will need to be freed after the request is completed
 *
 */
uvfs_transaction_s* uvfs_new_transaction(void)
{
    uvfs_transaction_s* trans;
    dprintk("Entering uvfs_new_transaction\n");
    trans = kmalloc(sizeof(uvfs_transaction_s), GFP_NOFS);
    if (trans == NULL)
    {
        dprintk("Exiting uvfs_new_transaction NULL\n");
        return NULL;
    }
    spin_lock(&Uvfs_lock);
    trans->serial = Serial_number++;
    init_waitqueue_head(&trans->fs_queue);
    INIT_LIST_HEAD(&trans->list);
    trans->answered = 0;
    dprintk("Issued serial = %d\n", trans->serial);
    spin_unlock(&Uvfs_lock);
    dprintk("Exiting uvfs_new_transaction\n");
    return trans;
}


/*
 * init pmfs driver, create /proc/fs/pmfs device node
 * and register the pmfs file system type.
 *
 */
static int __init uvfs_init(void)
{
    int result;
    spin_lock_init(&Uvfs_lock);
    init_waitqueue_head(&Uvfs_driver_queue);

    dprintk("<1>uvfs_init(/proc/fs/%s)\n", UVFS_FS_NAME);

    uvfs_proc_file =
        create_proc_entry(UVFS_FS_NAME, S_IFREG | 0600, proc_root_fs);
    if (uvfs_proc_file == NULL)
    {
        dprintk("<1>Could not create /proc/fs/%s\n", UVFS_FS_NAME);
        return -EIO;
    }
    uvfs_proc_file->proc_fops = &Uvfsd_file_operations;
    result = register_filesystem(&Uvfs_file_system_type);
    if (result < 0)
    {
        remove_proc_entry(UVFS_FS_NAME, proc_root_fs);
        return result;
    }
    if (uvfs_init_inodecache())
    {
        return -ENOMEM;
    }
    return 0;
}


/*
 * remove /proc/fs/pmfs device node
 * and un-register the pmfs file system type.
 *
 */
static void __exit uvfs_cleanup(void)
{
    dprintk("<1>uvfs_cleanup(/proc/fs/%s)\n", UVFS_FS_NAME);
    unregister_filesystem(&Uvfs_file_system_type);
    remove_proc_entry(UVFS_FS_NAME, proc_root_fs);
    uvfs_destroy_inodecache();
}

MODULE_LICENSE(UVFS_LICENSE);
MODULE_VERSION(UVFS_VERSION);

module_init(uvfs_init);
module_exit(uvfs_cleanup);
