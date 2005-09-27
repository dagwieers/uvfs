/*
 *   linux/fs/uvfs/file.c
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
/* file.c -- File operations */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include "dir.h"
#include "file.h"
#include "protocol.h"
#include "driver.h"
#include "dentry.h"

extern void displayFhandle();
extern struct super_block* Sb;


int uvfs_file_read(struct file* file,
                   char* buf,
                   size_t count,
                   loff_t* offset)
{
    struct dentry * dentry = file->f_dentry;
    int ret;

    dprintk("<1>uvfs_file_read(%s/%s)\n",
            dentry->d_parent->d_name.name, dentry->d_name.name);

    ret = uvfs_revalidate(dentry);
    if (!ret)
         return generic_file_read(file, buf, count, offset);
    return ret;
}


int uvfs_file_write(struct file* file,
                    const char* buf,
                    size_t count,
                    loff_t* offset)
{
    struct dentry * dentry = file->f_dentry;
    int ret;

    dprintk("<1>uvfs_file_write(%s/%s)\n",
            dentry->d_parent->d_name.name, dentry->d_name.name);

    ret = uvfs_revalidate(dentry);
    if (!ret)
         return generic_file_write(file, buf, count, offset);
    return ret;
}              


/* Called by page cache aware write functions. */

static int uvfs_write(struct inode* inode,
                      const char* buff, 
                      unsigned offset, 
                      unsigned count)
{
    int error = 0;
    uvfs_file_write_req_s* request;
    uvfs_file_write_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entering uvfs_write offset=%d  count=%d\n", offset, count);
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.file_write;
    request->type = UVFS_WRITE;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_file_write_req_s, buff) + count;
    request->fsid = (unsigned long)inode->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->ino = inode->i_ino;
    memcpy(&request->fh,inode->u.generic_ip,sizeof(vfs_fhandle_s));
    request->count = count;
    request->offset = offset;
    memcpy(request->buff, buff, count);
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.file_write;
    if (reply->error == 0)
    {
        inode->i_mtime = inode->i_ctime = inode->i_atime = CURRENT_TIME;
    }
    error = reply->error;
out:
    kfree(trans);
    dprintk("<1>Exited uvfs_write\n");
    return error;
}


/* Write out a page from an mmaped file. */

int uvfs_writepage(struct page* pg)
{
    int err;
    struct inode* inode = pg->mapping->host;
    unsigned count;
    unsigned long end_index;
    unsigned offset;
    char* buff;
    dprintk("<1>Entering uvfs_writepage\n");
    end_index = inode->i_size >> PAGE_CACHE_SHIFT;
    if (pg->index < end_index)
    {
        count = PAGE_CACHE_SIZE;
    }
    else
    {
        count = inode->i_size & (PAGE_CACHE_SIZE - 1);
        if (pg->index > end_index || count == 0)
        {
            UnlockPage(pg);
            return 0;
        }
    }
    offset = (pg->index << PAGE_CACHE_SHIFT);
    buff = kmap(pg);
    err = uvfs_write(inode, buff, offset, count);
    kunmap(pg);
    UnlockPage(pg);
    dprintk("<1>Exited uvfs_writepage err=%d\n", err);
    return err;
}


/* Read a page of an mmaped file.  Any data in the page beyond EOF should
   be NULLed. */

int uvfs_readpage(struct file* filp, struct page* pg)
{
    int error = 0;
    struct inode* inode = pg->mapping->host;
    char* buff;
    uvfs_file_read_req_s* request;
    uvfs_file_read_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entering readpage\n");
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.file_read;
    request->type = UVFS_READ;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)inode->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->ino = inode->i_ino;
    memcpy(&request->fh,inode->u.generic_ip,sizeof(vfs_fhandle_s));
    request->count = PAGE_CACHE_SIZE;
    request->offset = pg->index << PAGE_CACHE_SHIFT;
    if (!uvfs_make_request(trans))
    {
        error = -ERESTARTSYS;
        goto err;
    }
    reply = &trans->u.reply.file_read;
    /* Q? should we fill in the page if there was an error? */
    if (reply->error < 0)
    {
        error = reply->error;
        goto err;
    }
    buff = kmap(pg);
    memcpy(buff, reply->buff, reply->bytes_read);
    if (reply->bytes_read < PAGE_CACHE_SIZE)
    {
        memset(buff + reply->bytes_read, 0,
               PAGE_CACHE_SIZE - reply->bytes_read);
    }
    SetPageUptodate(pg);
    kunmap(pg);
    UnlockPage(pg);
    kfree(trans);
    dprintk("<1>Exited readpage OK\n");
    return 0;
err:
    UnlockPage(pg);
    kfree(trans);
    dprintk("<1>Exited readpage error=%d\n", error);
    return error;
}


/* uvfs this is a NOP by must be defined */
int uvfs_prepare_write(struct file* filp, struct page* pg,
                              unsigned offset, unsigned to)
{
    return 0;
}


/* Write out a portion of a page. */

int uvfs_commit_write(struct file* filp, struct page* pg,
                      unsigned offset, unsigned to)
{
    unsigned count;
    unsigned off;
    loff_t pos;
    char* buff;
    int retval;
    struct inode* inode = pg->mapping->host;

    count = to - offset;
    off = (pg->index << PAGE_CACHE_SHIFT) + offset;
    buff = kmap(pg);
    retval = uvfs_write(inode, buff + offset, off, count);
    pos = (pg->index << PAGE_CACHE_SHIFT) + to;
    if (pos > inode->i_size)
    {
        inode->i_size = pos;
        /* mark inode dirty so that echo and cat will work properly */
        mark_inode_dirty(inode); 
    }
    kunmap(pg);
    return retval;
}


/* Set attributes on a file, directory or symlink. */

int uvfs_setattr(struct dentry* entry, struct iattr* attr)
{
    int error = 0;
    struct inode* inode;
    uvfs_setattr_req_s* request;
    uvfs_setattr_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entered uvfs_setattr pid=%d\n", current->pid);
    inode = entry->d_inode;
    /* Does this check need to be made, or is it an assert? */
    if (inode == 0)
    {
        return -EINVAL;
    }
    /* Q: I'm not sure if this is needed anymore? */
    if ((error = inode_change_ok(inode, attr)) < 0)
    {
        dprintk("<1>uvfs_setattr: inode_change_ok = %d\n", error);
        return error;
    }
    /* It seems to be important that this get called before 
       we go to sleep.  In the truncate case, if the user space 
       portion is called before inode_setattr (which calls
       vmtruncate) we get an inconsistent page cache for the file.
       fsx will turn this up. 
    */
    inode_setattr(inode, attr);
    dprintk("uvfs_setattr: %s  mode %o\n", entry->d_name.name, attr->ia_mode);
retry:
    trans = uvfs_new_transaction();
    if (trans == 0)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.setattr;
    request->type = UVFS_SETATTR;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)inode->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    dprintk("uvfs_setattr: 0x%x i_ino 0x%x\n", (unsigned)inode, (unsigned)inode->i_ino);
    dprintk("uvfs_setattr: 0x%x generic_ip 0x%x\n", (unsigned)inode, (unsigned)inode->u.generic_ip);
    dprintk("uvfs_setattr: valid 0x%x mode 0x%x\n", attr->ia_valid, attr->ia_mode);
    dprintk("uvfs_setattr: usr %d grp %d\n", attr->ia_uid, attr->ia_gid);
    request->i_ino = inode->i_ino;
    memcpy(&request->fh,inode->u.generic_ip,sizeof(vfs_fhandle_s));
    request->ia_valid = attr->ia_valid;
    request->ia_mode = attr->ia_mode;
    request->ia_uid = attr->ia_uid;
    request->ia_gid = attr->ia_gid;
    request->ia_size = attr->ia_size;
    request->ia_atime = attr->ia_atime;
    request->ia_ctime = attr->ia_ctime;
    request->ia_mtime = attr->ia_mtime;
    uvfs_make_request(trans);
    if (signal_pending(current))
    {
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.setattr;
    error = reply->error;

    /* see if we are experiencing a delete */
    if(error == -EDESTADDRREQ)
    {
         dprintk("<1>uvfs_setattr: set ECOMM and retry\n");
         error = -ECOMM;
    }

    /* if we got an error we need to find out why */
    /* normally this will be an ECOMM or ESTALE error */
    if(error == -ECOMM)
    {
         dprintk("<1>uvfs_setattr ECOMM\n");
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
                dprintk("<1>uvfs_setattr uvfs_refresh(FAILED) ENOENT\n");
                error = -ENOENT;
                d_drop(entry);
                goto out;
         }

         /* free this we are going to do another request */
         kfree(trans);

         /* retry the setattr now */
         goto retry;
    }
out:
    kfree(trans);
    dprintk("<1>Exiting uvfs_setattr: error %d\n", error);
    return error;
}


/* Get attributes on a file, directory or symlink. */

int uvfs_getattr(struct dentry* entry, struct iattr* attr)
{
    int error = 0;
    struct inode* inode;
    uvfs_getattr_req_s* request;
    uvfs_getattr_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entered uvfs_getattr pid=%d\n", current->pid);
    inode = entry->d_inode;
    /* Does this check need to be made, or is it an assert? */
    if (inode == 0)
    {
        return -EINVAL;
    }
retry:
    trans = uvfs_new_transaction();
    if (trans == 0)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.getattr;
    request->type = UVFS_GETATTR;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)inode->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->i_ino = inode->i_ino;
    memcpy(&request->fh,inode->u.generic_ip,sizeof(vfs_fhandle_s));
    uvfs_make_request(trans);
    if (signal_pending(current))
    {
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.getattr;
    error = reply->error;

    if(!error)
    {
        /* no error so set attributes */
        attr->ia_valid = reply->ia_valid;
        attr->ia_mode  = reply->ia_mode;
        attr->ia_uid   = reply->ia_uid;
        attr->ia_gid   = reply->ia_gid;
        attr->ia_size  = reply->ia_size;
        attr->ia_atime = reply->ia_atime;
        attr->ia_ctime = reply->ia_ctime;
        attr->ia_mtime = reply->ia_mtime;
    }

    /* see if we are experiencing a delete */
    if(error == -EDESTADDRREQ)
    {
        dprintk("<1>uvfs_getattr: set ECOMM and retry\n");
        error = -ECOMM;
    }

    /* if we got an error we need to find out why */
    /* normally this will be an ECOMM or ESTALE error */
    if(error == -ECOMM)
    {
         dprintk("<1>uvfs_getattr ECOMM\n");
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
                dprintk("<1>uvfs_getattr uvfs_refresh(FAILED) ENOENT\n");
                error = -ENOENT;
                d_drop(entry);
                goto out;
         }

         /* free this we are going to do another request */
         kfree(trans);

         /* retry the getattr now */
         goto retry;
    }

out:
    kfree(trans);
    dprintk("<1>Exiting uvfs_getattr: error %d\n", error);
    return error;
}


/* revalidate the attributes of this inode from backing store. */

int uvfs_revalidate(struct dentry* entry)
{
    int error = 0;
    struct inode* inode;
    uvfs_getattr_req_s* request;
    uvfs_getattr_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entered uvfs_revalidate pid=%d\n", current->pid);

#ifdef DEBUG
    if(entry)
       printk("name=%s inode=%0x%x\n", entry->d_name.name, entry->d_inode);
    else
       printk("name=default\n");
#endif

    inode = entry->d_inode;
    /* Does this check need to be made, or is it an assert? */
    if (inode == 0)
    {
        dprintk("<1>uvfs_revalidate: EINVAL\n");
        return -EINVAL;
    }

    trans = uvfs_new_transaction();
    if (trans == 0)
    {
        dprintk("<1>uvfs_revalidate: ENOMEM\n");
        return -ENOMEM;
    }
    request = &trans->u.request.getattr;
    request->type = UVFS_GETATTR;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)inode->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->i_mode = inode->i_mode;
    request->i_ino = inode->i_ino;
    memcpy(&request->fh,inode->u.generic_ip,sizeof(vfs_fhandle_s));
    dprintk("<1>uvfs_revalidate: uvfs_make_request i_mode 0x%x\n", inode->i_mode);
    uvfs_make_request(trans);
    if (signal_pending(current))
    {
        dprintk("<1>uvfs_revalidate: ERESTARTSYS\n");
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.getattr;

    /* update if we got no error */
    dprintk("<1>uvfs_revalidate: reply->error %d\n", reply->error);
    if(!reply->error & reply->ia_valid)
    {
         dprintk("<1>uvfs_revalidate: sync in-kernel 0x%x\n", reply->ia_valid);
         dprintk("<1>uvfs_revalidate: i_size=%d\n", reply->ia_size);
         inode->i_mode  = reply->ia_mode;
         inode->i_uid   = reply->ia_uid;
         inode->i_gid   = reply->ia_gid;
         if(Sb->s_root->d_inode != inode)
         {
             /* check if mode time on iwserver has changed */
             if(inode->i_mtime != reply->ia_mtime)
                reply->error = -ECOMM; /* force revalidate */
             if(S_ISREG(inode->i_mode))
                inode->i_size = reply->ia_size;
             if(S_ISDIR(inode->i_mode))
                inode->i_size = BLOCKSIZE;
             if(S_ISLNK(inode->i_mode))
             {
                inode->i_size =
                     ((uvfs_inode_local_s *)inode->u.generic_ip)->i_size;
                inode->i_mode = (S_IFLNK | 0777);
             }
         }
         else
         {
             inode->i_size  = reply->ia_size;
         }
         inode->i_atime = reply->ia_atime;
         inode->i_ctime = reply->ia_ctime;
         inode->i_mtime = reply->ia_mtime;
         UPDATE_ATIME(inode);
    }
    error = reply->error;

    /* see if we are experiencing a delete */
    if(error == -EDESTADDRREQ)
    {
        dprintk("<1>uvfs_revalidate: set ECOMM and retry\n");
        error = -ECOMM;
    }

    /* if we got an error we need to find out why */
    /* normally this will be an ECOMM or ESTALE error */
    if(error == -ECOMM)
    {
         dprintk("<1>uvfs_revalidate calling uvfs_dentry_revalidate: error %d\n", error);
         /* revalidate the inode */
         if(uvfs_dentry_revalidate(entry))
             error = -ERESTARTSYS; /* got a mis-match tell vfs to retry */
         else
             error = 0;   /* we are OK ! */

         dprintk("<1>uvfs_revalidate: error %d\n", error);
    }

out:
    kfree(trans);
    dprintk("<1>Exiting uvfs_revalidate: error %d\n", error);
    return error;
}


/* check access permission on this inode */

int uvfs_permission(struct inode* inode, int mask)
{
    int error = 0;
    umode_t mode;
    uvfs_getattr_req_s* request;
    uvfs_getattr_rep_s* reply;
    uvfs_transaction_s* trans;
    struct dentry *dentry;

    /* Does this check need to be made, or is it an assert? */
    if (inode == 0)
    {
        dprintk("<1>uvfs_permission: EINVAL\n");
        return -EINVAL;
    }

    dentry = ((uvfs_inode_local_s *)inode->u.generic_ip)->dentry;
    dprintk("<1>Entered uvfs_permission mask=%d  ", mask);

#ifdef DEBUG
    if(dentry)
       printk("name=%s inode=%0x%x\n", dentry->d_name.name, dentry->d_inode);
    else
       printk("name=default\n");
#endif

    trans = uvfs_new_transaction();
    if (trans == 0)
    {
        dprintk("<1>uvfs_permission: ENOMEM\n");
        return -ENOMEM;
    }
    request = &trans->u.request.getattr;
    request->type = UVFS_GETATTR;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->fsid = (unsigned long)inode->i_sb->u.generic_sbp;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->i_mode = inode->i_mode;
    request->i_ino = inode->i_ino;
    memcpy(&request->fh,inode->u.generic_ip,sizeof(vfs_fhandle_s));
    dprintk("<1>uvfs_permission: uvfs_make_request i_mode 0x%x\n",inode->i_mode);
    uvfs_make_request(trans);
    if (signal_pending(current))
    {
        dprintk("<1>uvfs_permission: ERESTARTSYS\n");
        error = -ERESTARTSYS;
        goto out;
    }
    reply = &trans->u.reply.getattr;

    /* check if mode time on iwserver has changed */
    if(inode->i_mtime != reply->ia_mtime)
        reply->error = -ECOMM; /* force revalidate */

    error = reply->error;

    /* verify permissions */
    dprintk("<1>uvfs_permission: reply->error %d\n", reply->error);
    if(!error & reply->ia_valid)
    {
         dprintk("<1>uvfs_permission: sync in-kernel 0x%x\n", reply->ia_valid);
         dprintk("<1>uvfs_permission: i_size=%d\n", reply->ia_size);

         /* sync in-kernel inode while we are at it */
         inode->i_mode  = reply->ia_mode;
         inode->i_uid   = reply->ia_uid;
         inode->i_gid   = reply->ia_gid;
         if(S_ISDIR(inode->i_mode))
            inode->i_size = BLOCKSIZE;
         if(S_ISLNK(inode->i_mode))
         {
            inode->i_size =
                 ((uvfs_inode_local_s *)inode->u.generic_ip)->i_size;
            inode->i_mode = (S_IFLNK | 0777);
         }
         else
         {
            inode->i_size = reply->ia_size;
         }

         /* we need a working mode var to play with */
         mode = inode->i_mode;

         if (mask & MAY_WRITE)
         {
                /*
                 * Nobody gets write access to a read-only fs.
                 */
                if (IS_RDONLY(inode) &&
                    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
                {
                        error = -EROFS;
                        goto out;
                }

                /*
                 * Nobody gets write access to an immutable file.
                 */
                if (IS_IMMUTABLE(inode))
                {
                        error = -EACCES;
                        goto out;
                }
         }

         /* check if filesystem uid and user uid are same */
         if (current->fsuid == inode->i_uid)
                mode >>= 6;
         /* search group IDs for a match */
         else if (in_group_p(inode->i_gid))
                mode >>= 3;

         /*
          * If the DACs are ok we don't need any capability check.
          */
         if (((mode & mask & (MAY_READ|MAY_WRITE|MAY_EXEC)) == mask))
         {
                error = 0;
                goto out;
         }

         /*
          * Read/write DACs are always overridable.
          * Executable DACs are overridable if at least one exec bit is set.
          */
         if ((mask & (MAY_READ|MAY_WRITE)) || (inode->i_mode & S_IXUGO))
                if (capable(CAP_DAC_OVERRIDE))
         {
                error = 0;
                goto out;
         }

         /*
          * Searching includes executable on directories, else just read.
          */
         if(mask == MAY_READ || (S_ISDIR(inode->i_mode) && !(mask & MAY_WRITE)))
         {
                if (capable(CAP_DAC_READ_SEARCH))
                {
                      error = 0;
                      goto out;
                }
         }

         /* sorry no access granted*/
         error = -EACCES;
    }
    else
    {
         /* see if we are experiencing a delete */
         if(error == -EDESTADDRREQ)
         {
             dprintk("<1>uvfs_permission: set ECOMM and retry\n");
             error = -ECOMM;
         }

         /* check if ECOMM or ESTALE error */
         if(error == -ECOMM)
         {
             dprintk("<1>uvfs_permission calling uvfs_dentry_revalidate: error %d\n", error);
             /* revalidate the inode's dentry */
             if(uvfs_dentry_revalidate(dentry))
                 error = -ERESTARTSYS; /* got a mis-match tell vfs to retry */
             else
                    error = 0;
         }
         dprintk("<1>uvfs_permission: error %d\n", error);
    }
out:
    kfree(trans);
    dprintk("<1>Exiting uvfs_permission: error %d\n", error);
    return error;
}


/* Marks the inode as mmaped and calls generic_file_mmap */

int uvfs_mmap(struct file* filp, struct vm_area_struct* vma)
{
    struct dentry *dentry = filp->f_dentry;
    int     status;

    ((uvfs_inode_info_s *)filp->f_dentry->d_inode->u.generic_ip)->flags = 1;

    dprintk("uvfs_mmap(%s/%s)\n",
             dentry->d_parent->d_name.name, dentry->d_name.name);

    status = uvfs_revalidate(dentry);
    if (!status)
         status = generic_file_mmap(filp, vma);
    return status;
}


/*
** file lseek function - copy of generic - here to allow debug
*/
loff_t uvfs_file_llseek(struct file *file, loff_t offset, int origin)
{
    long long retval;

    dprintk("<1>Entering uvfs_file_llseek file=%s offset=%d origin=%d\n",
        file->f_dentry->d_name.name, (int)offset,origin);
    switch (origin)
    {
        case 2:
               offset += file->f_dentry->d_inode->i_size;
               break;
        case 1:
               offset += file->f_pos;
    }
    retval = -EINVAL;
    if ((offset >= 0) && (offset <= file->f_dentry->d_inode->i_sb->s_maxbytes))
    {
        if (offset != file->f_pos)
        {
            dprintk("<1>uvfs_file_llseek setting f_pos=%d\n", offset);
            file->f_pos = offset;
            file->f_reada = 0;
            file->f_version = ++event;
        }
        retval = offset;
    }
    dprintk("<1>Exited uvfs_file_llseek\n");
    return retval;
}


int uvfs_kvec_write(struct file *file, kvec_cb_t cb, size_t count, loff_t pos)
{
    struct dentry * dentry = file->f_dentry;
    int ret;

    dprintk("uvfs_kvec_write(%s/%s)\n",
             dentry->d_parent->d_name.name, dentry->d_name.name);

    ret = uvfs_revalidate(dentry);
    if (!ret)
         return generic_file_kvec_write(file, cb, count, pos);
    return ret;
}


int uvfs_kvec_read(struct file *file, kvec_cb_t cb, size_t count, loff_t pos)
{
    struct dentry * dentry = file->f_dentry;
    int ret;

    dprintk("uvfs_kvec_read(%s/%s)\n",
             dentry->d_parent->d_name.name, dentry->d_name.name);

    ret = uvfs_revalidate(dentry);
    if (!ret)
         return generic_file_kvec_read(file, cb, count, pos);
    return ret;
}

