/*
 *   file.c -- file operations
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

#include "uvfs.h"


ssize_t uvfs_file_read(struct file* file,
                       char* buf,
                       size_t count,
                       loff_t* offset)
{
    struct dentry * dentry = file->f_dentry;
    struct inode * inode = dentry->d_inode;
    int ret;

    dprintk("<1>uvfs_file_read(%s/%s)\n",
            dentry->d_parent->d_name.name, dentry->d_name.name);

    ret = uvfs_revalidate_inode(inode);
    if (!ret)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
        return do_sync_read(file, buf, count, offset);
#else
        return generic_file_read(file, buf, count, offset);
#endif
    return ret;
}


ssize_t uvfs_file_write(struct file* file,
                        const char* buf,
                        size_t count,
                        loff_t* offset)
{
    struct dentry * dentry = file->f_dentry;
    struct inode * inode = dentry->d_inode;
    int ret;

    dprintk("<1>uvfs_file_write(%s/%s)\n",
            dentry->d_parent->d_name.name, dentry->d_name.name);

    ret = uvfs_revalidate_inode(inode);
    if (!ret)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
        return do_sync_write(file, buf, count, offset);
#else
        return generic_file_write(file, buf, count, offset);
#endif
    return ret;
}


int uvfs_file_mmap(struct file* file, struct vm_area_struct* vma)
{
    struct dentry * dentry = file->f_dentry;
    struct inode * inode = dentry->d_inode;
    int ret;

    dprintk("<1>uvfs_file_mmap(%s/%s)\n",
            dentry->d_parent->d_name.name, dentry->d_name.name);

    ret = uvfs_revalidate_inode(inode);
    if (!ret)
        ret = generic_file_mmap(file, vma);
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
        dprintk("<1>uvfs_write: out of memory\n");
        return -ENOMEM;
    }
    request = &trans->u.request.file_write;
    request->type = UVFS_WRITE;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_file_write_req_s, buff) + count;
    request->uid = current_fsuid();
    request->gid = current_fsgid();
    request->fh = UVFS_I(inode)->fh;
    request->count = count;
    request->offset = offset;
    memcpy(request->buff, buff, count);
    uvfs_make_request(trans);

    reply = &trans->u.reply.file_write;
    error = reply->error;

    kfree(trans);
    dprintk("<1>Exited uvfs_write\n");
    return error;
}


/* Write out a page from an mmaped file. */

int uvfs_writepage(struct page* pg, struct writeback_control *wbc)
/* NOTE: need to add handling of writeback_control *wbc */
{
    int err;
    struct inode* inode = pg->mapping->host;
    unsigned count;
    unsigned int end_index;
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
            SetPageUptodate(pg);
            unlock_page(pg);
            return 0;
        }
    }
    offset = (pg->index << PAGE_CACHE_SHIFT);
    buff = kmap(pg);
    err = uvfs_write(inode, buff, offset, count);
    kunmap(pg);
    if(err)
        SetPageError(pg);
    else
        SetPageUptodate(pg);
    unlock_page(pg);
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

    dprintk("<1>Entering uvfs_readpage\n");
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.file_read;
    request->type = UVFS_READ;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->uid = current_fsuid();
    request->gid = current_fsgid();
    request->fh = UVFS_I(inode)->fh;
    request->count = PAGE_CACHE_SIZE;
    request->offset = pg->index << PAGE_CACHE_SHIFT;
    uvfs_make_request(trans);

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
    kunmap(pg);
    flush_dcache_page(pg);
    SetPageUptodate(pg);
    unlock_page(pg);
    kfree(trans);
    dprintk("<1>Exited uvfs_readpage OK\n");
    return 0;
err:
    SetPageError(pg);
    flush_dcache_page(pg);
    unlock_page(pg);
    kfree(trans);
    dprintk("<1>Exited readpage error=%d\n", error);
    return error;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)

int uvfs_write_begin(struct file *file, struct address_space *mapping,
                     loff_t pos, unsigned len, unsigned flags,
                     struct page **pagep, void **fsdata)
{
    struct page *page;
    pgoff_t index;
    unsigned from;

    index = pos >> PAGE_CACHE_SHIFT;
    from = pos & (PAGE_CACHE_SIZE - 1);

    page = grab_cache_page_write_begin(mapping, index, flags);
    if (!page)
        return -ENOMEM;

    *pagep = page;

    return uvfs_prepare_write(file, page, from, from+len);
}

int uvfs_write_end(struct file *file, struct address_space *mapping,
                   loff_t pos, unsigned len, unsigned copied,
                   struct page *page, void *fsdata)
{
    unsigned from = pos & (PAGE_CACHE_SIZE - 1);

    /* zero the stale part of the page if we did a short copy */
    if (copied < len) {
            void *kaddr = kmap_atomic(page, KM_USER0);
            memset(kaddr + from + copied, 0, len - copied);
            flush_dcache_page(page);
            kunmap_atomic(kaddr, KM_USER0);
    }

    uvfs_commit_write(file, page, from, from+copied);

    unlock_page(page);
    page_cache_release(page);

    return copied;
}

#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32) */

/* uvfs this is a NOP but must be defined */
int uvfs_prepare_write(struct file* filp, struct page* pg,
                              unsigned offset, unsigned to)
{
    dprintk("<1>Entering uvfs_prepare_write %d\n", current->pid);

    dprintk("<1>Leaving uvfs_prepare_write\n");
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

    dprintk("<1>Entering uvfs_commit_write inode %x\n", (unsigned int)inode);
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
    SetPageUptodate(pg);
    flush_dcache_page(pg);
    dprintk("<1>Leaving uvfs_commit_write error %d\n", retval);
    return retval;
}


/* Set attributes on a file, directory or symlink. */

int uvfs_setattr(struct dentry* entry, struct iattr* attr)
{
    int error = 0;
    struct inode* inode;
    unsigned int oldflags;
    uvfs_setattr_req_s* request;
    uvfs_setattr_rep_s* reply;
    uvfs_transaction_s* trans;
    dprintk("<1>Entered uvfs_setattr pid=%d\n", current->pid);
    inode = entry->d_inode;

    /* Q: I'm not sure if this is needed anymore?
     * A: This is where the permissions are checked.  It should
     *    probably be replaced with a call to uvfs_permission()
     */
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
    oldflags = attr->ia_valid;
    attr->ia_valid &= ~(ATTR_ATIME|ATTR_MTIME|ATTR_CTIME);
    if (inode_setattr(inode, attr))
    {
        dprintk("uvfs_setattr: inode_setattr() failed\n");
        return -EINVAL;
    }
    attr->ia_valid = oldflags;
    dprintk("uvfs_setattr: %s  mode %o\n", entry->d_name.name, attr->ia_mode);

    trans = uvfs_new_transaction();
    if (trans == 0)
    {
        dprintk("<1>uvfs_setattr: out of memory\n");
        return -ENOMEM;
    }
    request = &trans->u.request.setattr;
    request->type = UVFS_SETATTR;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->uid = current_fsuid();
    request->gid = current_fsgid();
    request->fh = UVFS_I(inode)->fh;
    request->ia_valid = attr->ia_valid;
    request->ia_mode = attr->ia_mode;
    request->ia_uid = attr->ia_uid;
    request->ia_gid = attr->ia_gid;
    request->ia_size = attr->ia_size;
    request->ia_atime.tv_sec = attr->ia_atime.tv_sec;
    request->ia_atime.tv_nsec = attr->ia_atime.tv_nsec;
    request->ia_mtime.tv_sec = attr->ia_mtime.tv_sec;
    request->ia_mtime.tv_nsec = attr->ia_mtime.tv_nsec;
    request->ia_ctime.tv_sec = attr->ia_ctime.tv_sec;
    request->ia_ctime.tv_nsec = attr->ia_ctime.tv_nsec;
    uvfs_make_request(trans);

    reply = &trans->u.reply.setattr;
    error = reply->error;

    kfree(trans);
    dprintk("<1>Exiting uvfs_setattr: error %d\n", error);
    return error;
}


/* Get attributes on a file, directory or symlink. */

int uvfs_getattr(struct vfsmount *mnt, struct dentry* entry, struct kstat* stat)
{
    struct inode *inode = entry->d_inode;
    int error;

    error = uvfs_revalidate_inode(inode);

    /*
     * nfsd ignores vfs_getattr errors for nfs v2/v3, so always return
     * attributes, even if inode revalidation failed.  It is better to
     * return slightly stale attributes than to let nfsd pass uninitialized
     * data back to the nfs client.
     */
    generic_fillattr(inode, stat);

    return error;
}
