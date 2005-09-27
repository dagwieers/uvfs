/*
 *   linux/fs/uvfs/dir.c
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
/* dir.c -- Implements directory operations. */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>
#include "dir.h"
#include "file.h"
#include "protocol.h"
#include "driver.h"
#include "operations.h"
#include "super.h"
#include "dentry.h"

extern void displayFhandle();

/* This sets up the in-kernel inode attributes.  Called from any function
   that does the equivalent of iget. */

void uvfs_set_inode(struct inode* inode,
                    uvfs_attr_s* a,
                    struct dentry * dentry,
                    int caller)
{
    dprintk("<1>uvfs_set_inode: mode 0x%x   0x%x %ld\n",
           a->i_mode, (unsigned)inode, inode->i_ino);

    if (caller == UVFS_LOOKUP)
    {
        if (S_ISREG(inode->i_mode) && inode->i_mtime != a->i_mtime)
        {
            dprintk("<1>uvfs_set_inode(%s/%s) old_mtime=%d, new_mtime=%d\n",
                    dentry->d_parent->d_name.name, dentry->d_name.name,
                    (int)inode->i_mtime, (int)a->i_mtime);
            invalidate_inode_pages(inode);
        }
    }

    inode->i_mode = a->i_mode;
    inode->i_nlink = a->i_nlink;
    inode->i_uid = a->i_uid;
    inode->i_gid = a->i_gid;
    if(caller != UVFS_LOOKUP)
       inode->i_size = a->i_size;
    inode->i_atime = a->i_atime;
    inode->i_mtime = a->i_mtime;
    inode->i_ctime = a->i_ctime;
    inode->i_blksize = a->i_blksize;
    inode->i_blocks = a->i_blocks;
    inode->i_rdev = a->devno;
    dprintk("<1>uvfs_set_inode: inode 0x%x %ld\n",(unsigned)inode,inode->i_ino);
    dprintk("<1>uvfs_set_inode: rdev  0x%x devno 0x%x\n",
          (unsigned)inode->i_rdev, (unsigned)a->devno);
    if (S_ISREG(inode->i_mode))
    {
        inode->i_op = &Uvfs_file_inode_operations;
        inode->i_fop = &Uvfs_file_file_operations;
        inode->i_data.a_ops = &Uvfs_file_aops;
        if(inode->u.generic_ip == NULL)
           inode->u.generic_ip =
               (void*)kmalloc(sizeof(uvfs_inode_local_s), GFP_KERNEL);
        ((uvfs_inode_local_s *)inode->u.generic_ip)->flags = 0;
        ((uvfs_inode_local_s *)inode->u.generic_ip)->dentry = dentry;
        if (inode->i_nlink != 1)
        {
            dprintk("<1>: uvfs_set_inode %ld has link count %d\n", inode->i_ino, 
                   inode->i_nlink);
        }
        /* inode->i_nlink = 1; */
        inode->i_generation = CURRENT_TIME;
    }
    else if (S_ISDIR(inode->i_mode))
    {
        inode->i_op = &Uvfs_dir_inode_operations;
        inode->i_fop = &Uvfs_dir_file_operations;
        if(inode->u.generic_ip == NULL)
           inode->u.generic_ip =
               (void*)kmalloc(sizeof(uvfs_inode_local_s), GFP_KERNEL);
        ((uvfs_inode_local_s *)inode->u.generic_ip)->flags = 0;
        ((uvfs_inode_local_s *)inode->u.generic_ip)->dentry = dentry;
        /* inode->i_nlink = 2; */
        inode->i_size = BLOCKSIZE;
        inode->i_generation = CURRENT_TIME;
    }
    else if (S_ISLNK(inode->i_mode))
    {
        inode->i_op = &Uvfs_symlink_inode_operations;
        if(inode->u.generic_ip == NULL)
           inode->u.generic_ip =
               (void*)kmalloc(sizeof(uvfs_inode_local_s), GFP_KERNEL);
        ((uvfs_inode_local_s *)inode->u.generic_ip)->flags = 0;
        ((uvfs_inode_local_s *)inode->u.generic_ip)->dentry = dentry;
        ((uvfs_inode_local_s *)inode->u.generic_ip)->i_size = inode->i_size;
        /* inode->i_nlink = 1; */
        inode->i_mode = (S_IFLNK | 0777);
        inode->i_generation = CURRENT_TIME;
    }
    else
    {
        printk("<1>pmfs uvfs_set_inode: init_special_inode ERROR unexpected i_mode 0x%x ino %ld dev %d\n", (unsigned)inode->i_mode, inode->i_ino, a->devno);
        printk("<1>pmfs uvfs_set_inode: generic_ip 0x%x 0x%x\n", (unsigned)inode, (unsigned)inode->u.generic_ip);
        init_special_inode(inode, inode->i_mode, a->devno);
    }
    dprintk("<1>uvfs_set_inode: generic_ip 0x%x 0x%x\n", (unsigned)inode, (unsigned)inode->u.generic_ip);
}


/* refresh dentry-inode-fhandle. */
    
int uvfs_refresh(struct inode* dir, struct dentry* entry)
{
    int retval = 0;
    uvfs_lookup_req_s* request;
    uvfs_lookup_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entered uvfs_refresh: name=%s inode=0x%x\n", entry->d_name.name,
            (int)entry->d_inode);
    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }

retry:
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.lookup;
    request->type = UVFS_LOOKUP;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_lookup_req_s, name) + entry->d_name.len;
    memcpy(request->name, entry->d_name.name, entry->d_name.len);     
    request->namelen = entry->d_name.len;
    request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    dprintk("uvfs_refresh: 0x%x i_ino 0x%x\n", (unsigned)dir, (unsigned)dir->i_ino);
    dprintk("uvfs_refresh: 0x%x generic_ip 0x%x\n", (unsigned)dir, (unsigned)dir->u.generic_ip);
    request->d_ino = dir->i_ino;
    memcpy(&request->fh,dir->u.generic_ip,sizeof(vfs_fhandle_s));
    // displayFhandle("refresh",&request->fh.handle);
    if (!uvfs_make_request(trans))
    {
        retval = -ERESTARTSYS;
        goto out;
    }
    
    reply = &trans->u.reply.lookup;
    retval = reply->error;

    /* see if we are experiencing a delete */
    if(retval == -EDESTADDRREQ)
    {
        dprintk("<1>uvfs_refresh: set ECOMM and retry\n");
        retval = -ECOMM;
    }
    /* may need to step back one more dentry */
    if(retval == -ECOMM)
    {
        /* do lookup of parent on grand-parent to get fhandle */
        if((retval = uvfs_refresh(entry->d_parent->d_parent->d_inode,entry->d_parent)))
        {
            d_drop(entry);
            goto out;
        }
        /* free this we are going to do another request */
        kfree(trans);

        /* retry the create now */
        goto retry;
    }

    /* check for error and refresh inode fhandle etc */
    if (!retval)
    {
        dprintk("<1>Exit uvfs_refresh: name=%s old_ino=%ld new_ino=%d\n",
              entry->d_name.name, entry->d_inode->i_ino, reply->ino);
        /* return old invalide inum to pool */
        uvfs_reuse_inum(entry->d_inode->i_ino);
        entry->d_inode->i_ino = reply->ino;
        memcpy(entry->d_inode->u.generic_ip,&reply->fh,sizeof(vfs_fhandle_s));
    }

out:
    dprintk("<1>Exit uvfs_refresh: name=%s err=%d\n",entry->d_name.name,retval);
    kfree(trans);
    return retval;
}


/* Create a new regular file. */

int uvfs_create(struct inode* dir,
                struct dentry* entry,
                int mode)
{
    int ino;
    int retval = 0;
    uvfs_create_req_s* request;
    uvfs_create_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* inode = NULL;

    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }
    if (dir->i_nlink == 0)
    {
        return -EPERM;
    }
    dprintk("<1>Entered uvfs_create: name=%s pid=%d\n", entry->d_name.name,
            current->pid);
retry:
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.create;
    request->type = UVFS_CREATE;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_create_req_s, name) + entry->d_name.len;
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
    dprintk("uvfs_create: 0x%x i_ino 0x%x\n", (unsigned)dir, (unsigned)dir->i_ino);
    dprintk("uvfs_create: 0x%x generic_ip 0x%x\n", (unsigned)dir, (unsigned)dir->u.generic_ip);
    request->d_ino = dir->i_ino;
    memcpy(&request->fh,dir->u.generic_ip,sizeof(vfs_fhandle_s));
    request->uid = current->fsuid;
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
    request->mode = mode;
    dprintk("uvfs_create: name %s  mode %o\n", entry->d_name.name, mode);
    if (!uvfs_make_request(trans))
    {
        retval = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.create;
    if (reply->error < 0)
    {
        /* see if we are experiencing a delete */
        if(reply->error == -EDESTADDRREQ)
        {
             dprintk("<1>uvfs_create: set ECOMM and retry\n");
             reply->error = -ECOMM;
        }
        /* check if ECOMM or ESTALE error */
        if(reply->error == -ECOMM)
        {
            dprintk("<1>uvfs_create ECOMM\n");
            dprintk("leaf %s %x\n",
                  entry->d_name.name,
                  entry->d_inode);

#ifdef DEBUG
            /* display the antecedents of this dentry */
            SHOW_PATH(entry)
#endif /* DEBUG */

            /* do lookup of parent on grand-parent to get fhandle */
            if(uvfs_refresh(entry->d_parent->d_parent->d_inode,entry->d_parent))
            {
                dprintk("<1>uvfs_create uvfs_refresh(FAILED) ENOENT\n");
                retval = -ENOENT;
                d_drop(entry);
                goto out;
            }

            /* free this we are going to do another request */
            kfree(trans);

            /* retry the create now */
            goto retry;
        }
        else
        {
            dprintk("<1>uvfs_create: name=%s err=%d d_drop\n",
                entry->d_name.name, reply->error);
            retval = reply->error;
            d_drop(entry);
        }
        goto out;
    }
    ino = reply->ino;
    lock_super(dir->i_sb);
    inode = new_inode(dir->i_sb);
    if (inode == NULL)
    {
        unlock_super(dir->i_sb);
        retval = -ENOMEM;
        goto out;
    }
    inode->i_ino = ino;
    inode->u.generic_ip = NULL;
    insert_inode_hash(inode);
    mark_inode_dirty(inode);
    unlock_super(dir->i_sb);
    uvfs_set_inode(inode, &reply->a, entry, UVFS_CREATE);
    memcpy(inode->u.generic_ip,&reply->fh,sizeof(vfs_fhandle_s));
    d_instantiate(entry, inode);
    dir->i_mtime = dir->i_ctime = dir->i_atime = CURRENT_TIME;
out:
    if (inode != NULL)
    {
        dprintk("<1>Exited uvfs_create 0x%x %ld (%d)\n", 
                (unsigned)inode, 
                inode->i_ino,
                current->pid);
    }
    kfree(trans);
    return retval;
}


/* Create a new hardlink */

int uvfs_link(struct dentry* old_entry, 
              struct inode* dir,
              struct dentry* entry)
{
    int retval = 0;
    uvfs_link_req_s* request;
    uvfs_link_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* old_inode = old_entry->d_inode;
    
    if (S_ISDIR(old_inode->i_mode))
    {
        return -EPERM;
    }
    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }
    if (dir->i_nlink == 0)
    {
        return -EPERM;
    }
    dprintk("<1>Entered uvfs_link: name=%s pid=%d\n", entry->d_name.name,
            current->pid);
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.link;
    request->type = UVFS_LINK;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_link_req_s, name) + entry->d_name.len;
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
    request->d_ino = dir->i_ino;
    memcpy(&request->fh_d,dir->u.generic_ip,sizeof(vfs_fhandle_s));
    request->ino = old_inode->i_ino;
    memcpy(&request->fh,
           old_inode->u.generic_ip,sizeof(vfs_fhandle_s));
    if (!uvfs_make_request(trans))
    {
        retval = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.link;
    if (reply->error < 0)
    {
        retval = reply->error;
        goto out;
    }
    old_inode->i_nlink++;
    dir->i_mtime = dir->i_ctime = dir->i_atime = old_inode->i_ctime = 
        old_inode->i_atime = CURRENT_TIME;
    /* Kookie but it looks like its the filesystems responsibility */
    atomic_inc(&old_inode->i_count);
    d_instantiate(entry, old_inode);
out:
    kfree(trans);
    dprintk("<1>Exited uvfs_link 0x%x %ld (%d)\n", 
            (unsigned)old_inode, 
            old_inode->i_ino,
            current->pid);
    return retval;
}


/* Create a special file. */

int uvfs_mknod(struct inode* dir, struct dentry* entry, int mode, int rdev)
{
    int ino;
    int retval = 0;
    struct inode* inode = NULL;
    uvfs_mknod_req_s* request;
    uvfs_mknod_rep_s* reply;
    uvfs_transaction_s* trans;

    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }
    if (dir->i_nlink == 0)
    {
        return -EPERM;
    }
    dprintk("<1>Entered uvfs_mknod: name=%s pid=%d\n", entry->d_name.name, 
            current->pid);
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.mknod;
    request->type = UVFS_MKNOD;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_mknod_req_s, name) + entry->d_name.len;
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
    request->d_ino = dir->i_ino;
    memcpy(&request->fh,dir->u.generic_ip,sizeof(vfs_fhandle_s));
    request->uid = current->fsuid;
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
    request->mode = mode;
    request->devno = rdev;
    if (!uvfs_make_request(trans))
    {
        retval = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.mknod;
    if (reply->error < 0)
    {
        retval = reply->error;
        d_drop(entry);
        goto out;
    }
    ino = reply->ino;
    lock_super(dir->i_sb);
    inode = new_inode(dir->i_sb);
    if (inode == NULL)
    {
        unlock_super(dir->i_sb);
        retval = -ENOMEM;
        goto out;
    }
    inode->i_ino = ino;
    inode->u.generic_ip = NULL;
    insert_inode_hash(inode);
    unlock_super(dir->i_sb);
    uvfs_set_inode(inode, &reply->a, entry, UVFS_MKNOD);
    memcpy(inode->u.generic_ip,&reply->fh,sizeof(vfs_fhandle_s));
    d_instantiate(entry, inode);
    dir->i_mtime = dir->i_ctime = dir->i_atime = CURRENT_TIME;
out:
    kfree(trans);
    if (inode != NULL)
    {
        dprintk("<1>Exited uvfs_mknod 0x%x %ld (%d)\n", (unsigned)inode, 
                inode->i_ino, current->pid);
    }
    return retval;
}


/* Lookup an entry in a directory and get its inode. */
    
struct dentry* uvfs_lookup(struct inode* dir, struct dentry* entry)
{
    int ino;
    void* retval = NULL;
    uvfs_lookup_req_s* request;
    uvfs_lookup_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* inode = NULL;
    dprintk("<1>Entered uvfs_lookup: name=%s pid=%d\n", entry->d_name.name,
            current->pid);
    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return ERR_PTR(-ENAMETOOLONG);
    }
retry:
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return ERR_PTR(-ENOMEM);
    }
    request = &trans->u.request.lookup;
    request->type = UVFS_LOOKUP;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_lookup_req_s, name) + entry->d_name.len;
    memcpy(request->name, entry->d_name.name, entry->d_name.len);     
    request->namelen = entry->d_name.len;
    request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    dprintk("uvfs_lookup: 0x%x i_ino 0x%x\n", (unsigned)dir, (unsigned)dir->i_ino);
    dprintk("uvfs_lookup: 0x%x generic_ip 0x%x\n", (unsigned)dir, (unsigned)dir->u.generic_ip);
    request->d_ino = dir->i_ino;
    memcpy(&request->fh,dir->u.generic_ip,sizeof(vfs_fhandle_s));
    // displayFhandle("lookup",&request->fh.handle);
    if (!uvfs_make_request(trans))
    {
        retval = ERR_PTR(-ERESTARTSYS);
        goto out;
    }
    
    reply = &trans->u.reply.lookup;

    if (reply->error < 0)
    {
        dprintk("<1>uvfs_lookup: error looking up %s/%s inode %d\n",
		entry->d_parent->d_name.name, entry->d_name.name, reply->ino);

        /* This is kernel goofiness.  If an entry doesn't
           exist you're supposed to make entry into a negative dentry, 
           one with a NULL inode. */
        if (reply->error == -ENOENT)
        {
            dprintk("<1>uvfs_lookup: ENOENT (%d)\n", reply->error);
            /* d_add(entry, NULL); */
            retval = NULL;

            dprintk("BAD leaf %s %x\n", entry->d_name.name, entry->d_inode);

#ifdef DEBUG
            /* display the antecedents of this dentry */
            SHOW_PATH(entry)
#endif /* DEBUG */
        }
        else
        {
            dprintk("<1>uvfs_lookup: error = %d (%d)\n", reply->error,
                    current->pid);

            /* see if we are experiencing a delete */
            if(reply->error == -EDESTADDRREQ)
            {
                 dprintk("<1>uvfs_lookup: set ECOMM and retry\n");
                 reply->error = -ECOMM;
            }

            /* check if ECOMM or ESTALE error */
            if(reply->error == -ECOMM)
            {
                dprintk("<1>uvfs_lookup ECOMM\n");
                dprintk("leaf %s %x\n",
                  entry->d_name.name,
                  entry->d_inode);

#ifdef DEBUG
                /* display the antecedents of this dentry */
                SHOW_PATH(entry)
#endif /* DEBUG */

                /* do lookup of parent on grand-parent to get fhandle */
                if(uvfs_refresh(entry->d_parent->d_parent->d_inode,entry->d_parent))
                {
                    dprintk("<1>uvfs_lookup: ENOENT grand-parent (%d)\n",
                       reply->error);
                    retval = ERR_PTR(-ENOENT);
                    /* d_drop(entry); */
                    goto out;
                }

                /* free this we are going to do another request */
                kfree(trans);

                /* retry the create now */
                goto retry;
            }
            else
            {
                retval = ERR_PTR(reply->error);
            }
        }
        goto out;
    }
#ifdef DEBUG
    else
    {
        dprintk("GOOD leaf %s %x\n",
            entry->d_name.name, entry->d_inode);

        /* display the antecedents of this dentry */
        SHOW_PATH(entry)
    }
#endif

    ino = reply->ino;
    /* avoid inode leak */
    dprintk("<1>uvfs_lookup: dentry at %lx, dentry->d_inode at %lx\n", entry, entry->d_inode);
    if (!entry->d_inode)
    {
        inode = iget(dir->i_sb, ino);
        if (inode == NULL)
        {
            retval = ERR_PTR(-ENOMEM);
            dprintk("<1>uvfs_lookup null inode when looking up name=%s ino=%d pid(%d)\n",
               entry->d_name.name, ino, current->pid);
            goto out;
        }
        else
        {
            dprintk("<1>Exited uvfs_lookup 0x%x %ld\n", (unsigned)inode, inode->i_ino);
        }
    }
    else
    {
        inode = entry->d_inode;
    }

    uvfs_set_inode(inode, &reply->a, entry, UVFS_LOOKUP);
    memcpy(inode->u.generic_ip,&reply->fh,sizeof(vfs_fhandle_s));
    d_add(entry, inode);
out:
    kfree(trans);
    return retval;
}

/* Unlink a regular file or symlink. */

int uvfs_unlink(struct inode* dir, struct dentry* entry)
{
    int error = 0;
    uvfs_unlink_req_s* request;
    uvfs_unlink_rep_s* reply;
    uvfs_transaction_s* trans;

    dprintk("<1>Entered uvfs_unlink name=%s inode=%ld pid=%d\n", entry->d_name.name,
           entry->d_inode->i_ino, current->pid);
    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }
    if (dir->i_nlink == 0)
    {
        dprintk("<1>uvfs_unlink: deleted directory %ld %s (%d)\n",
               dir->i_ino, entry->d_name.name, current->pid);
    }
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.unlink;
    request->type = UVFS_UNLINK;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_unlink_req_s, name) + entry->d_name.len;
    request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->d_ino = dir->i_ino;
    memcpy(&request->fh,dir->u.generic_ip,sizeof(vfs_fhandle_s));
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.unlink;
    error = reply->error;
    if (error == 0)
    {
        entry->d_inode->i_ctime = dir->i_ctime;
        entry->d_inode->i_nlink--;
        dprintk("<1>uvfs_unlinked inode & = 0x%x %ld %s\n", 
                (unsigned)entry->d_inode, 
                entry->d_inode->i_ino,
                entry->d_name.name);
        dir->i_mtime = dir->i_ctime = dir->i_atime = CURRENT_TIME;
    }
out:
    kfree(trans);
    dprintk("<1>Exited uvfs_unlink. %d\n", error);
    return error;
}


/* Create a symbolic link. */

int uvfs_symlink(struct inode* dir,
                 struct dentry* entry,
                 const char* path)
{
    int error = 0;
    int pathlen;
    int ino;
    struct inode* inode;
    uvfs_symlink_req_s* request;
    uvfs_symlink_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entered uvfs_symlink name=%s pid=%d\n", entry->d_name.name,
            current->pid);
    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }
    pathlen = strlen(path);
    if (pathlen > UVFS_MAX_PATHLEN)
    {
        return -ENAMETOOLONG;
    }
    if (dir->i_nlink == 0)
    {
        dprintk("<1>Exiting: uvfs_symlink EPERM\n");
        return -EPERM;
    }
retry:
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.symlink;
    request->type = UVFS_SYMLINK;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->uid = current->fsuid;
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
    request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
    request->d_ino = dir->i_ino;
    memcpy(&request->fh,dir->u.generic_ip,sizeof(vfs_fhandle_s));
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    memcpy(request->sympath, path, pathlen);
    request->symlen = pathlen;
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.symlink;
    if (reply->error < 0)
    {
        /* see if we are experiencing a delete */
        if(reply->error == -EDESTADDRREQ)
        {
            dprintk("<1>uvfs_symlink: set ECOMM and retry\n");
            error = -ECOMM;
        }
        /* check if ECOMM or ESTALE error */
        if(reply->error == -ECOMM)
        {
            dprintk("<1>uvfs_symlink ECOMM\n");
            dprintk("leaf %s %x\n",
                  entry->d_name.name,
                  entry->d_inode);

#ifdef DEBUG
            /* display the antecedents of this dentry */
            SHOW_PATH(entry)
#endif /* DEBUG */

            /* do lookup of parent on grand-parent to get fhandle */
            if(uvfs_refresh(entry->d_parent->d_parent->d_inode,entry->d_parent))
            {
                dprintk("<1>uvfs_symlink uvfs_refresh(FAILED) ENOENT\n");
                error = -ENOENT;
                d_drop(entry);
                goto out;
            }

            /* free this we are going to do another request */
            kfree(trans);

            /* retry the create now */
            goto retry;
        }
        else
        {
            dprintk("<1>uvfs_symlink: name=%s pid=%d err=%d d_drop\n",
               entry->d_name.name, current->pid, reply->error);
            d_drop(entry);
            error = reply->error;
        }
        goto out;
    }
    reply->a.i_mode = (S_IFLNK | 0777);
    reply->a.i_size = pathlen; /* set symlink length */
    ino = reply->ino;
    lock_super(dir->i_sb);
    inode = new_inode(dir->i_sb);
    if (inode == NULL)
    {
        unlock_super(dir->i_sb);
        error = -ENOMEM;
        goto out;
    }
    inode->i_ino = ino;
    inode->u.generic_ip = NULL;
    insert_inode_hash(inode);
    mark_inode_dirty(inode);
    unlock_super(dir->i_sb);
    uvfs_set_inode(inode, &reply->a, entry, UVFS_SYMLINK);
    memcpy(inode->u.generic_ip,&reply->fh,sizeof(vfs_fhandle_s));
    d_instantiate(entry, inode);
    dir->i_mtime = dir->i_ctime = dir->i_atime = CURRENT_TIME;
    dprintk("<1>Exiting uvfs_symlink 0x%x %ld\n", (unsigned)inode, inode->i_ino);

out:
    kfree(trans);
    dprintk("<1>Exited uvfs_symlink. %d\n", error);
    return error;
}


/* Make a directory. */
        
int uvfs_mkdir(struct inode* dir,
               struct dentry* entry, 
               int mode)
{
    int error = 0;
    int ino;
    struct inode* inode;
    uvfs_mkdir_req_s* request;
    uvfs_mkdir_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entering uvfs_mkdir name=%s pid=%d\n", entry->d_name.name,
            current->pid);
    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }
    if (dir->i_nlink == 0)
    {
        return -EPERM;
    }
retry:
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.mkdir;
    request->type = UVFS_MKDIR;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_mkdir_req_s, name) + entry->d_name.len;
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
    request->d_ino = dir->i_ino;
    memcpy(&request->fh,dir->u.generic_ip,sizeof(vfs_fhandle_s));
    request->uid = current->fsuid;
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current->fsgid;
    request->mode = mode | S_IFDIR;
    if (dir->i_mode & S_ISGID)
        request->mode |= S_ISGID;
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.mkdir;

    if (reply->error < 0)
    {
        /* see if we are experiencing a delete */
        if(reply->error == -EDESTADDRREQ)
        {
            dprintk("<1>uvfs_mkdir: set ECOMM and retry\n");
            error = -ECOMM;
        }

        /* check if ECOMM or ESTALE error */
        if(reply->error == -ECOMM)
        {
            dprintk("<1>uvfs_mkdir ECOMM\n");
            dprintk("leaf %s %x\n",
                  entry->d_name.name,
                  entry->d_inode);

#ifdef DEBUG
            /* display the antecedents of this dentry */
            SHOW_PATH(entry)
#endif /* DEBUG */

            /* do lookup of parent on grand-parent to get fhandle */
            if(uvfs_refresh(entry->d_parent->d_parent->d_inode,entry->d_parent))
            {
                dprintk("<1>uvfs_mkdir uvfs_refresh(FAILED) ENOENT\n");
                error = -ENOENT;
                d_drop(entry);
                goto out;
            }

            /* free this we are going to do another request */
            kfree(trans);

            /* retry the create now */
            goto retry;
        }
        else
        {
            dprintk("<1>uvfs_mkdir: error making dir %s inode %d\n",
		entry->d_name.name, reply->ino);
            error = reply->error;
            d_drop(entry);
        }
        goto out;
    }
    ino = reply->ino;
    lock_super(dir->i_sb);
    inode = new_inode(dir->i_sb);
    if (inode == NULL)
    {
        unlock_super(dir->i_sb);
        error = -ENOMEM;
        goto out;
    }
    inode->i_ino = ino;
    inode->u.generic_ip = NULL;
    insert_inode_hash(inode);
    mark_inode_dirty(inode);
    unlock_super(dir->i_sb);
    uvfs_set_inode(inode, &reply->a, entry, UVFS_MKDIR);
    memcpy(inode->u.generic_ip,&reply->fh,sizeof(vfs_fhandle_s));
    d_instantiate(entry, inode);
    dir->i_nlink++;
    dir->i_mtime = dir->i_ctime = dir->i_atime = CURRENT_TIME;
    dprintk("<1>Exited uvfs_mkdir 0x%x %ld\n", (unsigned)inode, inode->i_ino);
out:
    kfree(trans);
    return error;
}


/* Remove a directory.  Fails if directory is not empty. */

int uvfs_rmdir(struct inode* dir, struct dentry* entry)
{
    int error = 0;
    uvfs_rmdir_req_s* request;
    uvfs_rmdir_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entering uvfs_rmdir name=%s 0x%x %ld pid=%d\n", entry->d_name.name,
            (unsigned)entry->d_inode, entry->d_inode->i_ino,
            current->pid);
    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.rmdir;
    request->type = UVFS_RMDIR;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_rmdir_req_s, name) + entry->d_name.len;
    request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->d_ino = dir->i_ino;
    memcpy(&request->fh,dir->u.generic_ip,sizeof(vfs_fhandle_s));
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.rmdir;
    error = reply->error;
    if (error == 0)
    {
        entry->d_inode->i_nlink -= 2;
        dir->i_nlink--;
        dir->i_mtime = dir->i_ctime = dir->i_atime = CURRENT_TIME;
        entry->d_inode->i_ctime = dir->i_ctime;
    }
out:
    kfree(trans);
    dprintk("<1>Exiting uvfs_rmdir: error = %d 0x%x %ld\n", error,
            (unsigned)entry->d_inode, entry->d_inode->i_ino);
    return error;
}


/* Rename source to destination.  Fails for any number of reasons. */

int uvfs_rename(struct inode* srcdir,
                struct dentry* srcentry,
                struct inode* dstdir,
                struct dentry* dstentry)
{
    int error = 0;
    uvfs_rename_req_s* request;
    uvfs_rename_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entering uvfs_rename %s to %s pid=%d\n", srcentry->d_name.name, 
            dstentry->d_name.name, current->pid);
    if (srcentry->d_name.len > UVFS_MAX_NAMELEN ||
        dstentry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }
    if (srcdir->i_nlink == 0 || dstdir->i_nlink == 0)
    {
        return -EPERM;
    }
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.rename;
    request->type = UVFS_RENAME;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)srcdir->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->old_d_ino = srcdir->i_ino;
    memcpy(&request->fh_old,srcdir->u.generic_ip,sizeof(vfs_fhandle_s));
    memcpy(request->old_name, srcentry->d_name.name, srcentry->d_name.len);
    request->old_namelen = srcentry->d_name.len;
    request->new_d_ino = dstdir->i_ino;
    memcpy(&request->fh_new,dstdir->u.generic_ip,sizeof(vfs_fhandle_s));
    memcpy(request->new_name, dstentry->d_name.name, dstentry->d_name.len);
    request->new_namelen = dstentry->d_name.len;
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.rename;
    error = reply->error;
    if (error == 0)
    {
        if (dstentry->d_inode != NULL)
        {
            struct inode* dnode = dstentry->d_inode;
            dnode->i_ctime = dnode->i_atime = CURRENT_TIME;
            if (S_ISDIR(dnode->i_mode))
            {
                dnode->i_nlink -= 2;
                dstdir->i_nlink--;
            }
            else
            {
                dnode->i_nlink--;
            }
        }
        srcentry->d_inode->i_ctime = srcentry->d_inode->i_atime = CURRENT_TIME;
        if (S_ISDIR(srcentry->d_inode->i_mode))
        {
            dstdir->i_nlink++;
            srcdir->i_nlink--;
        }
    }
out:
    kfree(trans);
    dprintk("<1>Exited uvfs_rename\n");
    return error;
}


/* Read entries from a directory until the provided buffer is full.  It is
   important that the user space implementation of readdir continue 
   functioning in a reasonable matter when the contents of the directory 
   are changed mid-readdir. */

int uvfs_readdir(struct file* filp,
                 void *dirent,
                 filldir_t filldir)
{
    uvfs_readdir_req_s* request;
    uvfs_readdir_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* dir;
    dprintk("<1>Entering uvfs_readdir pid=%d\n", current->pid);
    dir = filp->f_dentry->d_inode;
    while (1)
    {
        int i;
        uvfs_dirent_s* ent;
        trans = uvfs_new_transaction();
        if (trans == NULL)
        {
            return -ENOMEM;
        }
        request = &trans->u.request.readdir;
        request->type = UVFS_READDIR;
        request->serial = trans->serial;
        request->size = sizeof(*request);
        request->entry_no = filp->f_pos;
        request->fsid = (unsigned long)dir->i_sb->u.generic_sbp;
        request->uid = current->fsuid;
        request->gid = current->fsgid;
        request->d_ino = dir->i_ino;
        // displayFhandle("readdir",&request->fh.handle);
        memcpy(&request->fh,dir->u.generic_ip,sizeof(vfs_fhandle_s));
        if (!uvfs_make_request(trans))
        {
            kfree(trans);
            return -ERESTARTSYS;
        }
        reply = &trans->u.reply.readdir;
        if (reply->error < 0)
        {
            int error = reply->error;
            kfree(trans);
            return error;
        }
        if (reply->count == 0)
        {
            /* We've reached the end of the directory. */
            break;
        }
        ent = (uvfs_dirent_s*)reply->data;
        for (i = 0; i < reply->count; i++)
        {
            if (filldir(dirent,
                        ((char*)ent + sizeof(*ent)),
                        ent->length,
                        filp->f_pos,
                        ent->ino,
                        DT_UNKNOWN))
            {
                goto full;
            }
            filp->f_pos = ent->index + 1;
            /*
            dprintk("<1>uvfs_readdir: ");
            for (int j = 0; j < ent->length; j++)
            {
                dprintk("%c", ((char*)ent + sizeof(*ent))[j]);
            }
            dprintk(" %d %d %ld (%d)\n", ent->index, ent->ino, 
                    dir->i_ino, current->pid);
            */
            ent = (uvfs_dirent_s*)(((unsigned)ent + 
                                    sizeof(*ent) + 
                                    ent->length + 
                                    3) & 0xfffffffc);
        }
        kfree(trans);
    }
full:
    kfree(trans);
    dprintk("<1>Exited uvfs_readdir\n");
    UPDATE_ATIME(dir);
    return 0;
}


loff_t uvfs_dir_llseek(struct file *file, loff_t offset, int origin)
{
    dprintk("<1>Entering uvfs_dir_llseek file=%s offset=%d origin=%d\n",
        file->f_dentry->d_name.name, (int)offset, origin);
    switch (origin)
    {
        case 1:
        if (offset == 0)
        {
            offset = file->f_pos;
            break;
        }
        case 2:
            return -EINVAL;
    }

    if (offset != file->f_pos)
    {
        dprintk("<1>uvfs_dir_llseek setting f_pos=%d\n", offset);
        file->f_pos = offset;
        file->f_reada = 0;
        file->f_version = ++event;
    }
    dprintk("<1>Exited uvfs_dir_llseek\n");
    return (offset <= 0) ? 0 : offset;
}


int uvfs_open(struct inode *inode, struct file *filp)
{
    int err = 0;

    dprintk("<1>Entering uvfs_open inum=%ld\n",inode->i_ino);
    /* Ensure that we revalidate the data cache */
    err = uvfs_permission(inode, 0);
    dprintk("<1>Exited uvfs_open err=%d\n", err);
    return err;
}


