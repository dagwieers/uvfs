/*
 *   linux/fs/uvfs/symlink.c
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
/* symlink -- Handles symlink operations. */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "symlink.h"
#include "driver.h"
#include "protocol.h"

int uvfs_readlink(struct dentry* entry,
                  char* out,
                  int length)
{
    char *buff;
    int error = 0;
    uvfs_readlink_req_s* request;
    uvfs_readlink_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* inode = entry->d_inode;
    dprintk("<1>Entering readlink\n");
    if ((buff = kmalloc(UVFS_MAX_PATHLEN + 1, GFP_KERNEL)) == NULL)
    {
        dprintk("<1>Couldn't allocate symlink buffer.\n");
        return -ENOMEM;
    }
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        kfree(buff);
        return -ENOMEM;
    }
    request = &trans->u.request.readlink;
    request->type = UVFS_READLINK;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)inode->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->ino = inode->i_ino;
    memcpy(&request->fh,inode->u.generic_ip,sizeof(vfs_fhandle_s));
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto err;
    }
    reply = &trans->u.reply.readlink;
    if (reply->error < 0)
    {
        error = reply->error;
        dprintk("<1>uvfs_readlink error=%d\n", error);
        dprintk("link %s %x\n",
            entry->d_name.name, (int)entry->d_inode);
        goto err;
    }
    memcpy(buff, reply->buff, reply->len);
    buff[reply->len] = 0;
    error = vfs_readlink(entry, out, length, buff);
    entry->d_inode->i_size = reply->len;
    kfree(buff);
    kfree(trans);
    return error;
err:
    kfree(buff);
    kfree(trans);
    dprintk("<1>Exited readlink error=%d\n", error);
    return error;
}


int uvfs_follow_link(struct dentry* entry,
                     struct nameidata* nd)
{
    int error = 0;
    char *buff;
    uvfs_readlink_req_s* request;
    uvfs_readlink_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* inode = entry->d_inode;
    dprintk("<1>Entered follow_link\n");
    if ((buff = kmalloc(UVFS_MAX_PATHLEN + 1, GFP_KERNEL)) == NULL)
    {
        dprintk("<1>Couldn't allocate symlink buffer.\n");
        return -ENOMEM;
    }
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        kfree(buff);
        return -ENOMEM;
    }
    request = &trans->u.request.readlink;
    request->type = UVFS_READLINK;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)inode->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->ino = inode->i_ino;
    memcpy(&request->fh,inode->u.generic_ip,sizeof(vfs_fhandle_s));
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto err;
    }
    reply = &trans->u.reply.readlink;
    if (reply->error < 0)
    {
        error = reply->error;
        dprintk("<1>uvfs_follow_link error=%d\n", error);
        dprintk("link %s %x\n",
            entry->d_name.name, (int)entry->d_inode);
        goto err;
    }
    memcpy(buff, reply->buff, reply->len);
    buff[reply->len] = 0;
    dprintk("<1>Exited follow_link error=%d\n", error);
    error = vfs_follow_link(nd, buff);
    entry->d_inode->i_size = reply->len;
    kfree(buff);
    kfree(trans);
    return error;
err:
    dprintk("<1>Exited follow_link error=%d\n", error);
    kfree(buff);
    kfree(trans);
    return error;
}

