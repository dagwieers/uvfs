/*
 *   linux/fs/uvfs/driver.h
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
/* driver.h -- Header for the serial device that is used for user-space
   kernel-space communications. */

#ifndef _UVFS_DRIVER_H_
#define _UVFS_DRIVER_H_

#include <linux/wait.h>
#include <linux/list.h>
#include <linux/slab.h>
#include "protocol.h"

#define dprintk(fmt, args...) 

/*
#define dprintk printk
*/

typedef struct _uvfs_transaction_s
{
    struct list_head list;
    wait_queue_head_t fs_queue;
    union 
    {
        uvfs_request_u request;
        uvfs_reply_u reply;
    } u;
    int serial;
    int answered;
} uvfs_transaction_s;


extern spinlock_t Uvfs_lock;
extern wait_queue_head_t Uvfs_driver_queue;
extern struct list_head Uvfs_requests;
extern struct list_head Uvfs_replies;

extern void safe_sleep_on(wait_queue_head_t* q, spinlock_t* s);
extern int uvfs_make_request(uvfs_transaction_s* trans);
extern uvfs_transaction_s* uvfs_new_transaction();

#endif /* !_UVFS_DRIVER_H_ */



