/*
 *   linux/fs/uvfs/super.c
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
/* super.c -- Superblock functions. */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "super.h"
#include "protocol.h"
#include "driver.h"
#include "operations.h"
#include "dir.h"

struct super_block* Sb = NULL;

int uvfs_mounted = 0;	// mounted or not !

void displayFhandle(char* msg, vfs_fhandle_s* fh)
{
    printk("%s  sbxid=0x%x, inum=0x%x, mntid=0x%x, arid=0x%x\n",
            msg,
            fh->no_sbxid,
            fh->no_fspid.fs_sfuid,
            fh->no_mntid,
            fh->no_narid.na_aruid);
}

void displayInodeInfo(char* msg, uvfs_inode_info_s* ip)
{
    displayFhandle(msg, &ip->handle);
    printk("flags 0x%x\n", ip->flags);
}

/* Noop but VFS expects the function to exist. */
void uvfs_read_inode(struct inode* inode)
{
    dprintk("<1>Entering read_inode\n");
    dprintk("read_inode: 0x%x i_ino 0x%x\n", (unsigned)inode, (unsigned)inode->i_ino);
    dprintk("read_inode: 0x%x generic_ip 0x%x\n", (unsigned)inode, (unsigned)inode->u.generic_ip);
    dprintk("<1>Exiting read_inode\n");
}


int uvfs_statfs(struct super_block* sb, struct statfs* stat)
{
    int error = 0;
    uvfs_statfs_req_s* request;
    uvfs_statfs_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entering statfs\n");
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.statfs;
    request->type = UVFS_STATFS;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)sb->u.generic_sbp;
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.statfs;
    stat->f_type = reply->f_type;
    stat->f_bsize = reply->f_bsize;
    stat->f_blocks = reply->f_blocks;
    stat->f_bfree = reply->f_bfree;
    stat->f_bavail = reply->f_bavail;
    stat->f_files = reply->f_files;
    stat->f_ffree = reply->f_ffree;
    stat->f_namelen = reply->f_namelen;
    error = reply->error;
out:
    kfree(trans);
    dprintk("<1>Exited statfs\n");
    return error;
}


/* Unmount. */

void uvfs_put_super(struct super_block* sb)
{
    uvfs_put_super_req_s* request;
    uvfs_transaction_s* trans;
    dprintk("<1>Entering put_super\n");
    // check if we are currently mounted */
    if(!uvfs_mounted)
    {
        // nope so return immediately
        dprintk("<1>Exited put_super not currently mounted\n");
        return;
    }
    // check if server is running
    if(GET_USE_COUNT(THIS_MODULE) == 0)
    {
        // nope so return immediately
        dprintk("<1>Exited put_super server not running\n");
        return;
    }
    // yes tell server to unmount
    dprintk("<1>put_super new_transaction\n");
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return;
    }
    request = &trans->u.request.put_super;
    request->type = UVFS_PUT_SUPER;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)sb->u.generic_sbp;
    dprintk("<1>do uvfs_make_request\n");
    uvfs_make_request(trans);
    kfree(trans);
    MOD_DEC_USE_COUNT;
    uvfs_mounted = 0;  // server unmounted
    dprintk("<1>Exited put_super server running\n");
}


/* Called at mount time. */

struct super_block* uvfs_read_super(struct super_block* sb,
                                    void* data,
                                    int silent)
{
    int root_ino;
    struct super_block* retval = NULL;
    struct inode* root;
    uvfs_read_super_req_s* request;
    uvfs_read_super_rep_s* reply;
    uvfs_transaction_s* trans;
    char* arg;
    size_t arglength;
    
    dprintk("<1>Entering read_super\n");
    arg = data;
    arglength = strlen(arg);
    if (arglength >= UVFS_MAX_PATHLEN)
    {
        dprintk("<1>Exited read_super > UVFS_MAX_PATHLEN\n");
        return ERR_PTR(-ENAMETOOLONG);
    }
    dprintk("<1>read_super check if already mounted\n");
    // check if we are currently mounted */
    if(uvfs_mounted)
    {
        // nope so return immediately
        dprintk("<1>Exited read_super we are already mounted\n");
        return ERR_PTR(-EBUSY);
    }
    dprintk("<1>read_super check server running\n");
    // check if server is running
    if(GET_USE_COUNT(THIS_MODULE) == 0)
    {
        // nope so return immediately
        dprintk("<1>Exited read_super server not running\n");
        return ERR_PTR(-EIO);
    }
    // yes tell server to mount
    dprintk("<1>read_super uvfs_new_transaction\n");
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        dprintk("<1>Exited read_super\n");
        return ERR_PTR(-ENOMEM);
    }
    dprintk("<1>read_super build request\n");
    request = &trans->u.request.read_super;
    request->type = UVFS_READ_SUPER;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_read_super_req_s, buff) + arglength; 
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->arglength = arglength;
    memcpy(request->buff, arg, arglength);
    dprintk("<1>read_super uvfs_make_request\n");
    if (!uvfs_make_request(trans))
    {
        retval = ERR_PTR(-ERESTARTSYS);
        goto out;
    }
    reply = &trans->u.reply.read_super;
    if (reply->error < 0)
    {
        retval = ERR_PTR(reply->error);
        goto out;
    }
    dprintk("<1>read_super get reply\n");
    Sb = sb; /* save super block ptr */
    sb->s_maxbytes = 0xFFFFFFFF;
    sb->s_blocksize = reply->s_blocksize;
    sb->s_blocksize_bits = reply->s_blocksize_bits;
    sb->s_magic = reply->s_magic;
    sb->s_op = &Uvfs_super_operations;
    sb->u.generic_sbp = (void*)reply->fsid;
    root_ino = reply->root_ino;
    root = iget(sb, root_ino);
    if (root != NULL)
    {
        dprintk("<1>read_super iget returned good root\n");
        uvfs_set_inode(root, &reply->a, (struct dentry *)NULL, UVFS_READ_SUPER);
        sb->s_root = d_alloc_root(root);
        if(sb->s_root->d_inode->u.generic_ip == NULL)
           sb->s_root->d_inode->u.generic_ip =
               (void*)kmalloc(sizeof(uvfs_inode_local_s), GFP_KERNEL);
        memcpy(sb->s_root->d_inode->u.generic_ip,
                    &reply->fh,sizeof(vfs_fhandle_s));
        dprintk("<1>root 0x%x %ld\n", (unsigned)root, root->i_ino);
        dprintk("read_super: 0x%x i_ino 0x%x\n", (unsigned)root, (unsigned)root->i_ino);
        dprintk("read_super: 0x%x generic_ip 0x%x\n", (unsigned)root, (unsigned)root->u.generic_ip);
        // displayFhandle("read_super: reply",&reply->fh.handle);
        // displayInodeInfo("read_super: info",
        //       (uvfs_inode_info_s*) sb->s_root->d_inode->u.generic_ip);
        MOD_INC_USE_COUNT;
        retval = sb;
    }
    else
    {
        dprintk("<1>iget returned null in read_super.\n");
        retval = ERR_PTR(-ENOMEM);
    }
out:
    kfree(trans);
    uvfs_mounted = 1;  // server mounted
    dprintk("<1>Exited read_super\n");
    return retval;
}


void uvfs_delete_inode(struct inode* inode)
{
#ifdef REUSE_INUM
    uvfs_delete_inode_req_s* request;
    uvfs_transaction_s* trans;
#endif
    int ino;
    unsigned long fsid;
    struct super_block* sb;

    lock_kernel();
    sb = inode->i_sb;
    lock_super(sb);
    dprintk("delete_inode: i_ino 0x%x 0x%x\n", (unsigned)inode, (unsigned)inode->i_ino);
    dprintk("delete_inode: generic_ip 0x%x 0x%x\n", (unsigned)inode, (unsigned)inode->u.generic_ip);
    ino = inode->i_ino;
    dprintk("<1>Entered uvfs_delete_inode %d (%d)\n", 
            ino, current->pid);
    fsid = (unsigned long)inode->i_sb->u.generic_sbp;
    clear_inode(inode);
    unlock_super(sb);

#ifdef REUSE_INUM
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return;
    }
    request = &trans->u.request.delete_inode;
    request->type = UVFS_DELETE_INODE;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = fsid;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->ino = ino;
    memcpy(&request->fh,inode->u.generic_ip,sizeof(vfs_fhandle_s));
    uvfs_make_request(trans);
    kfree(trans);
#endif

    if(inode->u.generic_ip)
    {
        kfree(inode->u.generic_ip);
        inode->u.generic_ip = 0;
    }
    inode->i_size = 0;
    unlock_kernel();
    dprintk("<1>uvfs_delete_inode finished.\n");
}


void uvfs_reuse_inum(int ino)
{
#ifdef REUSE_INUM
    uvfs_delete_inode_req_s* request;
    uvfs_transaction_s* trans;

    dprintk("<1>Entered uvfs_reuse_inum %d (%d)\n", ino, current->pid);
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return;
    }
    request = &trans->u.request.delete_inode;
    request->type = UVFS_DELETE_INODE;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = 0;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->ino = ino;
    uvfs_make_request(trans);
    kfree(trans);
    dprintk("<1>uvfs_reuse_inum finished.\n");
#endif
}

