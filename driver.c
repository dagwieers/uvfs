/*
 *   driver.c -- conduit from kernel to user space
 *
 *   Copyright (C) 2002      Britt Park
 *   Copyright (C) 2004-2012 Interwoven, Inc.
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

/* Allow these signals to interrupt a request in progress */
#define ALLOWED_SIGS   (sigmask(SIGKILL))

static int uvfsd_open(struct inode *, struct file *);
static int uvfsd_release(struct inode *, struct file *);
static ssize_t uvfsd_read(struct file *, char *, size_t, loff_t *);
static ssize_t uvfsd_write(struct file *, const char *, size_t, loff_t *);
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,35))
static int uvfsd_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
#else
static long uvfsd_unlocked_ioctl(struct file *, unsigned int, unsigned long);
#endif

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
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,35))
    .ioctl          = uvfsd_ioctl,
#else
    .unlocked_ioctl          = uvfsd_unlocked_ioctl,
#endif
};

static struct proc_dir_entry* uvfs_proc_file;

static wait_queue_head_t Uvfs_driver_queue;
static spinlock_t Uvfs_lock;

static int ShuttingDown = 0;
static int Serial_number = 0;

static int uvfs_use_count = 0;

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
    spin_lock(&Uvfs_lock);
    uvfs_use_count++;
    spin_unlock(&Uvfs_lock);
    return 0;
}


/*
 * close the driver from file system thread
 * each thread must close the connection to unload driver
 */
static int uvfsd_release(struct inode* inode, struct file* filp)
{
    spin_lock(&Uvfs_lock);
    uvfs_use_count--;
    if (uvfs_use_count == 0)
    {
        uvfs_transaction_s* trans;
        while (!list_empty(&Uvfs_requests))
        {
            trans = list_entry(Uvfs_requests.next, uvfs_transaction_s, list);
            list_del_init(&trans->list);
            trans->u.reply.generic.error = -EIO;
            trans->answered = 1;
            wake_up(&trans->fs_queue);
        }
        while (!list_empty(&Uvfs_replies))
        {
            trans = list_entry(Uvfs_replies.next, uvfs_transaction_s, list);
            list_del_init(&trans->list);
            trans->u.reply.generic.error = -EIO;
            trans->answered = 1;
            wake_up(&trans->fs_queue);
        }
        ShuttingDown = 0;
        Serial_number = 0;
    }
    spin_unlock(&Uvfs_lock);
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
        dprintk("<1>uvfsd_read EIO (%zu)(%lu)\n", count, sizeof(uvfs_request_u));
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
    trans->in_use = 1;
    spin_unlock(&Uvfs_lock);
    request = &trans->u.request.generic;
    ret = copy_to_user(buff, request, request->size);
    spin_lock(&Uvfs_lock);
    trans->in_use = 0;
    if (trans->abort)
        wake_up(&trans->fs_queue);
    spin_unlock(&Uvfs_lock);
    dprintk("<1>Exited uvfsd_read: type=%d %d (%d)\n",
    		request->type,
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
        dprintk("<1>uvfsd_write Undersized reply (%zu).\n", count);
        return -EIO;
    }
    if (copy_from_user(&reply, buff, sizeof(reply)))
    {
        dprintk("<1>copy_from_user failed in uvfsd_write.\n");
        return -EFAULT;
    }
    dprintk("<1>Entered uvfsd_write: type=%d serial=%d (%d)\n",
    		reply.type,
            reply.serial,
            current->pid);
    if (reply.size != count)
    {
        dprintk("<1>Mismatched write size in uvfsd_write %d %zu\n",
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
    trans->in_use = 1;
    spin_unlock(&Uvfs_lock);
    ret = copy_from_user(&trans->u.reply, buff, reply.size);
    spin_lock(&Uvfs_lock);
    trans->in_use = 0;
    trans->answered = 1;
    wake_up(&trans->fs_queue);
    spin_unlock(&Uvfs_lock);
    dprintk("<1>Exited uvfsd_write (%d)\n", current->pid);
    if(ret)
        return -EIO;
    else
        return reply.size;
}


/* Used to signal the user-space filesystem to shutdown, cmd = 0 */
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,35))
static int uvfsd_ioctl(struct inode* inode, struct file* filp,
                       unsigned int cmd, unsigned long arg)
#else
static long uvfsd_unlocked_ioctl(struct file* filp,
                       unsigned int cmd, unsigned long arg)
#endif
{
    switch (cmd)
    {
        case UVFS_IOCTL_SHUTDOWN:
        {
            dprintk("Entering uvfsd_ioctl SHUTDOWN\n");
            spin_lock(&Uvfs_lock);
            ShuttingDown = 1;
            wake_up_interruptible(&Uvfs_driver_queue);
            spin_unlock(&Uvfs_lock);
            return 0;
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
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,7,0))
            return !hlist_empty(&Uvfs_file_system_type.fs_supers);
#else
            return !list_empty(&Uvfs_file_system_type.fs_supers);
#endif
        }
        case UVFS_IOCTL_USE_COUNT:
        default:
            // return number of opens active on this module
            return uvfs_use_count;
    }
    return 0;
}


int uvfs_make_request(uvfs_transaction_s* trans)
{
    sigset_t oldset;
    unsigned long irqflags;

    /* Make sure the server is running, and add our request to the queue */
    spin_lock(&Uvfs_lock);
    if (uvfs_use_count == 0)
    {
        trans->u.reply.generic.error = -EIO;
        spin_unlock(&Uvfs_lock);
        return 0;
    }
    list_add_tail(&trans->list, &Uvfs_requests);
    wake_up_interruptible(&Uvfs_driver_queue);
    spin_unlock(&Uvfs_lock);

    /* Mask all signals except ALLOWED_SIGS while we wait */
    spin_lock_irqsave(&current->sighand->siglock, irqflags);
    oldset = current->blocked;
    siginitsetinv(&current->blocked, ALLOWED_SIGS & ~oldset.sig[0]);
    recalc_sigpending();
    spin_unlock_irqrestore(&current->sighand->siglock, irqflags);

    /* Wait while the request is processed */
    wait_event_interruptible(trans->fs_queue, trans->answered);

    /* Check to see if we were interrupted by a signal */
    spin_lock(&Uvfs_lock);
    if (signal_pending(current))
    {
        if (trans->in_use)
        {
            trans->abort = 1;
            spin_unlock(&Uvfs_lock);
            wait_event(trans->fs_queue, !trans->in_use);
            spin_lock(&Uvfs_lock);
        }
        list_del_init(&trans->list);
        trans->u.reply.generic.error = -ERESTARTSYS;
    }
    spin_unlock(&Uvfs_lock);

    /* Restore the original signal mask */
    spin_lock_irqsave(&current->sighand->siglock, irqflags);
    current->blocked = oldset;
    recalc_sigpending();
    spin_unlock_irqrestore(&current->sighand->siglock, irqflags);

    return 0;
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
    trans->in_use = 0;
    trans->abort = 0;
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

    dprintk("<1>uvfs_init(/proc/%s)\n", UVFS_PROC_NAME);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
    uvfs_proc_file = proc_create(UVFS_PROC_NAME, S_IFREG | 0600, NULL, &Uvfsd_file_operations);
    if (uvfs_proc_file == NULL)
    {
        dprintk("<1>Could not create /proc/%s\n", UVFS_PROC_NAME);
        return -EIO;
    }
#else
    uvfs_proc_file = create_proc_entry(UVFS_PROC_NAME, S_IFREG | 0600, NULL);
    if (uvfs_proc_file == NULL)
    {
        dprintk("<1>Could not create /proc/%s\n", UVFS_PROC_NAME);
        return -EIO;
    }
    uvfs_proc_file->proc_fops = &Uvfsd_file_operations;
#endif
    result = register_filesystem(&Uvfs_file_system_type);
    if (result < 0)
    {
        remove_proc_entry(UVFS_PROC_NAME, NULL);
        return result;
    }
    if (uvfs_init_inodecache())
    {
        dprintk("<1>uvfs_init exiting - -ENOMEM \n");
        return -ENOMEM;
    }
    dprintk("<1>uvfs_init exited\n");
    return 0;
}


/*
 * remove /proc/fs/pmfs device node
 * and un-register the pmfs file system type.
 *
 */
static void __exit uvfs_cleanup(void)
{
    dprintk("<1>uvfs_cleanup(/proc/%s)\n", UVFS_PROC_NAME);
    unregister_filesystem(&Uvfs_file_system_type);
    remove_proc_entry(UVFS_PROC_NAME, NULL);
    uvfs_destroy_inodecache();
}

MODULE_LICENSE(UVFS_LICENSE);
MODULE_VERSION(UVFS_VERSION);

module_init(uvfs_init);
module_exit(uvfs_cleanup);
