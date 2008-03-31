/*
 *   symlink.c -- symlink operations
 *
 *   Copyright (C) 2002      Britt Park
 *   Copyright (C) 2004-2008 Interwoven, Inc.
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

#include "uvfs.h"

int uvfs_readlink(struct dentry *dentry, char *buffer, int buflen)
{
    int error = 0;
    uvfs_readlink_req_s* request;
    uvfs_readlink_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* inode = dentry->d_inode;
    dprintk("<1>Entering uvfs_readlink name=%s\n", dentry->d_name.name);
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.readlink;
    request->type = UVFS_READLINK;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->fh = UVFS_I(inode)->fh;
    uvfs_make_request(trans);

    reply = &trans->u.reply.readlink;
    if (reply->error < 0)
    {
        error = reply->error;
        goto out;
    }
    reply->buff[reply->len] = 0;
    error = vfs_readlink(dentry, buffer, buflen, reply->buff);
out:
    kfree(trans);
    dprintk("<1>Exited uvfs_readlink error=%d\n", error);
    return error;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
void *uvfs_follow_link(struct dentry *dentry, struct nameidata *nd)
#else
int uvfs_follow_link(struct dentry *dentry, struct nameidata *nd)
#endif
{
    int error = 0;
    uvfs_readlink_req_s* request;
    uvfs_readlink_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* inode = dentry->d_inode;
    dprintk("<1>Entering uvfs_follow_link name=%s\n", dentry->d_name.name);
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
        return ERR_PTR(-ENOMEM);
#else
        return -ENOMEM;
#endif
    }
    request = &trans->u.request.readlink;
    request->type = UVFS_READLINK;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->fh = UVFS_I(inode)->fh;
    uvfs_make_request(trans);

    reply = &trans->u.reply.readlink;
    if (reply->error < 0)
    {
        error = reply->error;
        goto out;
    }
    reply->buff[reply->len] = 0;
    error = vfs_follow_link(nd, reply->buff);
out:
    kfree(trans);
    dprintk("<1>Exited uvfs_follow_link error=%d\n", error);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
    return ERR_PTR(error);
#else
    return error;
#endif
}
