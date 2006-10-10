/*
 *   symlink.c -- symlink operations
 *
 *   Copyright (C) 2002      Britt Park
 *   Copyright (C) 2004-2006 Interwoven, Inc.
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

/*
 * read the symbolic link in the referenced dentry
 * and copy to the out buffer, for max length.
 * we will then do a vfs_readlink on that name
 * to find the real file or directory.
 *
 */
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
    dprintk("<1>Entering uvfs_readlink\n");
    if ((buff = kmalloc(UVFS_MAX_PATHLEN + 1, GFP_KERNEL)) == NULL)
    {
        dprintk("<1>uvfs_readlink Couldn't allocate symlink buffer.\n");
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
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->fh = UVFS_I(inode)->fh;
    uvfs_make_request(trans);

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
    dprintk("<1>Exited uvfs_readlink error=%d\n", error);
    return error;
}

/*
 * read the symbolic link in the referenced dentry
 * and copy to a temp buffer.
 * we will then do a vfs_follow_link on that name
 * to find the real file or directory.
 *
 */
int uvfs_follow_link(struct dentry* entry,
                     struct nameidata* nd)
{
    int error = 0;
    char *buff;
    uvfs_readlink_req_s* request;
    uvfs_readlink_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* inode = entry->d_inode;
    dprintk("<1>Entered uvfs_follow_link\n");
    if ((buff = kmalloc(UVFS_MAX_PATHLEN + 1, GFP_KERNEL)) == NULL)
    {
        dprintk("<1>uvfs_follow_link Couldn't allocate symlink buffer.\n");
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
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->fh = UVFS_I(inode)->fh;
    uvfs_make_request(trans);

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
    dprintk("<1>Exited uvfs_follow_link error=%d\n", error);
    error = vfs_follow_link(nd, buff);
    entry->d_inode->i_size = reply->len;
    kfree(buff);
    kfree(trans);
    return error;
err:
    dprintk("<1>Exited uvfs_follow_link error=%d\n", error);
    kfree(buff);
    kfree(trans);
    return error;
}
