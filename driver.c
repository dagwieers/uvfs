/*
 *   linux/fs/uvfs/driver.c
 *
 *   Copyright 2002-2004 Interwoven Inc.
 *   
 *   initial  prototype  version: 0.5 02/22/02 Britt Park Interwoven Inc.
 *   iwserver compatible version: 1.0 09/15/04 Randy Petersen Interwoven Inc.
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *   
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *   
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software 
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
/* driver.c -- Implementation of the serial device that acts as conduit from
   kernel to user space. */

static char const rcsid[] =
                "$Id: pmfs VFS driver uvfs-linux-ent3.1 2005/09/13 Exp $";

/* K6 = 6, P4 = 7 */
/* #define CONFIG_X86_L1_CACHE_SHIFT 7 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/time.h>

#include <asm/uaccess.h>

#include "driver.h"

static int uvfsd_read(struct file* filp,
                      char* buff,
                      size_t count,
                      loff_t* offset);
static int uvfsd_write(struct file* filp,
                       const char* buff,
                       size_t count,
                       loff_t* offset);
static int uvfsd_ioctl(struct inode* inode, struct file* file,
                       unsigned int cmd, unsigned long arg);
static int uvfsd_open(struct inode*, struct file*);
static int uvfsd_release(struct inode* inode, struct file* filp);


struct file_operations Uvfsd_file_operations =
{
    open: uvfsd_open,
    release: uvfsd_release,
    read: uvfsd_read,
    write: uvfsd_write,
    ioctl: uvfsd_ioctl
};


wait_queue_head_t Uvfs_driver_queue;
spinlock_t Uvfs_lock;
static int ShuttingDown = 0;
static int Serial_number = 0;

extern int uvfs_mounted;	// mounted or not !


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
    "put_super",
    "read_super",
    "readlink",
    "delete_inode",
    "shutdown",
    "mknod",
    "link",
    "LAST + 1"
};


static int uvfsd_open(struct inode* inode, struct file* file)
{
    dprintk("Entering uvfsd_open\n");
    MOD_INC_USE_COUNT;
    dprintk("Exiting uvfsd_open\n");
    return 0;
}


static int uvfsd_release(struct inode* inode, struct file* filp)
{
    dprintk("Entering uvfsd_release\n");
    MOD_DEC_USE_COUNT;
    if(GET_USE_COUNT(THIS_MODULE))
    {
         dprintk("Exiting uvfsd_release count %d\n",GET_USE_COUNT(THIS_MODULE));
         return 0;
    }

    /* on last release reset shutdown and request serial number */
    ShuttingDown = 0;
    Serial_number = 0;
    dprintk("Exiting uvfsd_release ShutDown clear\n");
    return 0;
}


/* Reads are user space queries for fs request. */

static int uvfsd_read(struct file* filp,
                      char* buff,
                      size_t count,
                      loff_t* offset)
{
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
            copy_to_user(buff, &req, req.size);
            spin_lock(&Uvfs_lock);
            wake_up_interruptible(&Uvfs_driver_queue);
            spin_unlock(&Uvfs_lock);
            return size;
        }
        dprintk("<1>uvfsd_read: About to sleep for request\n");
        init_waitqueue_entry(&wait, current);
        set_current_state(TASK_INTERRUPTIBLE);
        add_wait_queue_exclusive(&Uvfs_driver_queue, &wait);
        spin_unlock(&Uvfs_lock);
        schedule();
        spin_lock(&Uvfs_lock);
        remove_wait_queue(&Uvfs_driver_queue, &wait);
        if (signal_pending(current))
        {
            spin_unlock(&Uvfs_lock);
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
    copy_to_user(buff, request, request->size);
    dprintk("<1>Exited uvfsd_read: %d (%d)\n", 
            request->size,
            current->pid);
    return request->size;
}
        

/* Writes are replies from the user space filesystem implementation. */

static int uvfsd_write(struct file* file,
                       const char* buff,
                       size_t count,
                       loff_t* offset)
{
    uvfs_generic_rep_s reply;
    struct list_head* ptr;
    uvfs_transaction_s* trans = NULL;
    if (count < sizeof(uvfs_generic_rep_s))
    {
        dprintk("<1>Undersized reply (%d).\n", count);
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
    copy_from_user(&trans->u.reply, buff, reply.size);
    wake_up_interruptible(&trans->fs_queue);
    trans->answered = 1;
    dprintk("<1>Exited uvfsd_write (%d)\n", current->pid);
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
            if (GET_USE_COUNT(THIS_MODULE))
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
            return (uvfs_mounted);  // server mount state
        }
        case UVFS_IOCTL_USE_COUNT:
        default:
            // return number of opens active on this module
            return (GET_USE_COUNT(THIS_MODULE));
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
    set_current_state(TASK_INTERRUPTIBLE);

    add_wait_queue_exclusive(q, &wait);
    spin_unlock(s);
    schedule();
    spin_lock(s);
    remove_wait_queue(q, &wait);
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
            schedule();
        }
        dprintk("Entering uvfs_make_request 0\n");
        return 0;
    }
    spin_unlock(&Uvfs_lock);
    dprintk("Entering uvfs_make_request 1\n");
    return 1;
}


uvfs_transaction_s* uvfs_new_transaction()
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


