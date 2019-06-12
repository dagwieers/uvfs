/*
 *   dir.c -- directory operations
 *
 *   Copyright (C) 2002      Britt Park
 *   Copyright (C) 2004-2014 Hewlett-Packard Development Company, L.P.
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

/* Create a new regular file. */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
int uvfs_create(struct inode* dir,
                struct dentry* entry,
		umode_t mode,
		bool unused)
#else
int uvfs_create(struct inode* dir,
                struct dentry* entry,
                int mode,
                struct nameidata* idata)
#endif
{
    int retval = 0;
    uvfs_create_req_s* request;
    uvfs_create_rep_s* reply;
    uvfs_transaction_s* trans;
    struct inode* inode = NULL;

    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        return -ENAMETOOLONG;
    }
    dprintk("<1>Entered uvfs_create: name=%s pid=%d\n", entry->d_name.name,
            current->pid);

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
    request->fh = UVFS_I(dir)->fh;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
    request->uid = current_fsuid().val;
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid.val : current_fsgid().val;
#else
    request->uid = current_fsuid();
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current_fsgid();
#endif
    request->mode = mode;
    dprintk("uvfs_create: name %s  mode %o\n", entry->d_name.name, mode);
    uvfs_make_request(trans);

    reply = &trans->u.reply.create;
    if (reply->error < 0)
    {
        dprintk("<1>uvfs_create: name=%s err=%d d_drop\n",
            entry->d_name.name, reply->error);
        retval = reply->error;
        d_drop(entry);

        goto out;
    }
    inode = uvfs_iget(dir->i_sb, &reply->fh, &reply->a);
    if (inode == NULL)
    {
        retval = -ENOMEM;
        goto out;
    }
    d_instantiate(entry, inode);
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


int uvfs_lookup_by_name(struct inode* dir, struct qstr* filename, uvfs_fhandle_s *fh, uvfs_attr_s *attr)
{
    int retval = 0;
    uvfs_lookup_req_s* request;
    uvfs_lookup_rep_s* reply;
    uvfs_transaction_s* trans;

    dprintk("<1>Entered uvfs_lookup_by_name: name=%s pid=%d\n", filename->name, current->pid);
    if (filename->len > UVFS_MAX_NAMELEN)
    {
        dprintk("<1>Exited uvfs_lookup_by_name - ENAMETOOLONG\n");
        return -ENAMETOOLONG;
    }

    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        dprintk("<1>Exited uvfs_lookup_by_name - ENOMEM\n");
        return -ENOMEM;
    }
    request = &trans->u.request.lookup;
    request->type = UVFS_LOOKUP;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_lookup_req_s, name) + filename->len;
    memcpy(request->name, filename->name, filename->len);
    request->namelen = filename->len;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
    request->uid = current_fsuid().val;
    request->gid = current_fsgid().val;
#else
    request->uid = current_fsuid();
    request->gid = current_fsgid();
#endif
    request->fh = UVFS_I(dir)->fh;
    uvfs_make_request(trans);

    reply = &trans->u.reply.lookup;
    *fh = reply->fh;
    *attr = reply->a;
    retval = reply->error;

    kfree(trans);
    dprintk("<1>Exited uvfs_lookup_by_name\n");
    return retval;
}


/* Lookup an entry in a directory, get its inode and fillin the dentry. */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
struct dentry* uvfs_lookup(struct inode* dir, struct dentry* entry, unsigned int unused)
#else
struct dentry* uvfs_lookup(struct inode* dir, struct dentry* entry, struct nameidata* idata)
#endif
{
    int err;
    uvfs_fhandle_s fh;
    uvfs_attr_s    attr;
    struct dentry * retval;
    struct inode * inode = 0;

    dprintk("<1>Entered uvfs_lookup\n");
    err = uvfs_lookup_by_name(dir, &entry->d_name, &fh, &attr);

    if (err < 0)
    {
        dprintk("<1>uvfs_lookup: error looking up %s/%s\n",
                entry->d_parent->d_name.name, entry->d_name.name);

        /* This is kernel goofiness.  If an entry doesn't
           exist you're supposed to make entry into a negative dentry,
           one with a NULL inode. */
        if (err == -ENOENT)
        {
            retval = NULL;

            dprintk("<1>uvfs_lookup: error looking up %s/%s d_inode %p\n",
                entry->d_parent->d_name.name, entry->d_name.name,
                entry->d_inode);
        } else
        {
            dprintk("<1>uvfs_lookup: error = %d (%d)\n", err,
                    current->pid);

            retval = ERR_PTR(err);
        }
    }
    else
    {
        dprintk("GOOD leaf %s %p\n", entry->d_name.name, entry->d_inode);

        inode = uvfs_iget(dir->i_sb, &fh, &attr);
        if (inode == NULL)
        {
            retval = ERR_PTR(-ENOMEM);
        }
    }
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
    if(inode)
        retval = d_splice_alias(inode, entry);
#else
    entry->d_op = &Uvfs_dentry_operations;
    retval = d_splice_alias(inode, entry);
#endif

    dprintk("<1>uvfs_lookup: dentry at %p, dentry->d_inode at %p\n", entry, entry->d_inode);
    dprintk("<1>Exited uvfs_lookup\n");

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

    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.unlink;
    request->type = UVFS_UNLINK;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_unlink_req_s, name) + entry->d_name.len;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
    request->uid = current_fsuid().val;
    request->gid = current_fsgid().val;
#else
    request->uid = current_fsuid();
    request->gid = current_fsgid();
#endif
    request->fh = UVFS_I(dir)->fh;
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    uvfs_make_request(trans);

    reply = &trans->u.reply.unlink;
    error = reply->error;
    if (error == 0)
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
	drop_nlink(entry->d_inode);
#else
        entry->d_inode->i_nlink--;
#endif
        dprintk("<1>uvfs_unlinked inode & = 0x%x %ld %s\n",
                (unsigned)entry->d_inode,
                entry->d_inode->i_ino,
                entry->d_name.name);
    }

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
    struct inode* inode;
    uvfs_symlink_req_s* request;
    uvfs_symlink_rep_s* reply;
    uvfs_transaction_s* trans;

    dprintk("<1>Entered uvfs_symlink name=%s pid=%d\n", entry->d_name.name,
            current->pid);

    if (entry->d_name.len > UVFS_MAX_NAMELEN)
    {
        dprintk("<1>Exited uvfs_symlin - ENAMETOOLONG");
        return -ENAMETOOLONG;
    }
    pathlen = strlen(path);
    if (pathlen > UVFS_MAX_PATHLEN)
    {
        dprintk("<1>Exited uvfs_symlin - ENAMETOOLONG");
        return -ENAMETOOLONG;
    }

    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        dprintk("<1>Exited uvfs_symlin - ENOMEM");
        return -ENOMEM;
    }
    request = &trans->u.request.symlink;
    request->type = UVFS_SYMLINK;
    request->serial = trans->serial;
    request->size = sizeof(*request);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
    request->uid = current_fsuid().val;
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid.val : current_fsgid().val;
#else
    request->uid = current_fsuid();
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current_fsgid();
#endif
    request->fh = UVFS_I(dir)->fh;
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    memcpy(request->sympath, path, pathlen);
    request->symlen = pathlen;
    uvfs_make_request(trans);

    reply = &trans->u.reply.symlink;
    if (reply->error < 0)
    {
        dprintk("<1>uvfs_symlink: name=%s pid=%d err=%d d_drop\n",
           entry->d_name.name, current->pid, reply->error);
        d_drop(entry);
        error = reply->error;

        goto out;
    }
    inode = uvfs_iget(dir->i_sb, &reply->fh, &reply->a);
    if (inode == NULL)
    {
        error = -ENOMEM;
        goto out;
    }
    d_instantiate(entry, inode);
    dprintk("<1>Exiting uvfs_symlink 0x%x %ld\n", (unsigned)inode, inode->i_ino);

out:
    kfree(trans);
    dprintk("<1>Exited uvfs_symlink. %d\n", error);
    return error;
}


/* Make a directory. */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
int uvfs_mkdir(struct inode* dir,
               struct dentry* entry,
               umode_t mode)
#else
int uvfs_mkdir(struct inode* dir,
               struct dentry* entry,
               int mode)
#endif
{
    int error = 0;
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
    request->fh = UVFS_I(dir)->fh;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
    request->uid = current_fsuid().val;
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid.val : current_fsgid().val;
#else
    request->uid = current_fsuid();
    request->gid = (dir->i_mode & S_ISGID) ? dir->i_gid : current_fsgid();
#endif
    request->mode = mode | S_IFDIR;
    if (dir->i_mode & S_ISGID)
        request->mode |= S_ISGID;
    uvfs_make_request(trans);

    reply = &trans->u.reply.mkdir;

    if (reply->error < 0)
    {
        dprintk("<1>uvfs_mkdir: error making dir %s\n",
                entry->d_name.name);
        error = reply->error;
        d_drop(entry);

        goto out;
    }
    inode = uvfs_iget(dir->i_sb, &reply->fh, &reply->a);
    if (inode == NULL)
    {
        error = -ENOMEM;
        goto out;
    }
    d_instantiate(entry, inode);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
    inc_nlink(dir);
#else
    dir->i_nlink++;
#endif
    dprintk("<1>Exited uvfs_mkdir 0x%x %ld\n", (unsigned)inode, inode->i_ino);
out:
    kfree(trans);
    dprintk("<1>Exited uvfs_mkdir\n");
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
        dprintk("<1>Exiting uvfs_rmdir - ENAMETOOLONG\n");
        return -ENAMETOOLONG;
    }

    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
    	dprintk("<1>Exiting uvfs_rmdir - ENOMEM\n");
        return -ENOMEM;
    }
    request = &trans->u.request.rmdir;
    request->type = UVFS_RMDIR;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_rmdir_req_s, name) + entry->d_name.len;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
    request->uid = current_fsuid().val;
    request->gid = current_fsgid().val;
#else
    request->uid = current_fsuid();
    request->gid = current_fsgid();
#endif
    request->fh = UVFS_I(dir)->fh;
    memcpy(request->name, entry->d_name.name, entry->d_name.len);
    request->namelen = entry->d_name.len;
    uvfs_make_request(trans);

    reply = &trans->u.reply.rmdir;
    error = reply->error;
    if (error == 0)
    {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
        set_nlink(entry->d_inode, entry->d_inode->i_nlink - 2);
	drop_nlink(dir);
#else
        entry->d_inode->i_nlink -= 2;
        dir->i_nlink--;
#endif
    }

    /* translate an EREMOTE into an ENOTEMPTY for directory not empty */
    if (error == -EREMOTE)
        error = -ENOTEMPTY;
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
        dprintk("<1>Exited uvfs_rename-ENAMETOOLONG\n");
        return -ENAMETOOLONG;
    }
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        dprintk("<1>Exited uvfs_rename-ENOMEM\n");
        return -ENOMEM;
    }
    request = &trans->u.request.rename;
    request->type = UVFS_RENAME;
    request->serial = trans->serial;
    request->size = sizeof(*request);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
    request->uid = current_fsuid().val;
    request->gid = current_fsgid().val;
#else
    request->uid = current_fsuid();
    request->gid = current_fsgid();
#endif
    request->fh_old = UVFS_I(srcdir)->fh;
    memcpy(request->old_name, srcentry->d_name.name, srcentry->d_name.len);
    request->old_namelen = srcentry->d_name.len;
    request->fh_new = UVFS_I(dstdir)->fh;
    memcpy(request->new_name, dstentry->d_name.name, dstentry->d_name.len);
    request->new_namelen = dstentry->d_name.len;
    uvfs_make_request(trans);

    reply = &trans->u.reply.rename;
    error = reply->error;
    if (error == 0)
    {
        if (dstentry->d_inode != NULL)
        {
            struct inode* dnode = dstentry->d_inode;
            if (S_ISDIR(dnode->i_mode))
            {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
                set_nlink(dnode, dnode->i_nlink - 2);
                drop_nlink(dstdir);
#else
                dnode->i_nlink -= 2;
                dstdir->i_nlink--;
#endif
            }
            else
            {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
                drop_nlink(dnode);
#else
                dnode->i_nlink--;
#endif
            }
        }
        if (S_ISDIR(srcentry->d_inode->i_mode))
        {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0)
            inc_nlink(dstdir);
	    drop_nlink(srcdir);
#else
            dstdir->i_nlink++;
            srcdir->i_nlink--;
#endif
        }
    }

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
    uvfs_transaction_s* trans = (uvfs_transaction_s *)NULL;
    struct inode* dir = filp->f_dentry->d_inode;
    dprintk("<1>Entering uvfs_readdir pid=%d\n", current->pid);
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,7,0)
        request->uid = current_fsuid().val;
        request->gid = current_fsgid().val;
#else
        request->uid = current_fsuid();
        request->gid = current_fsgid();
#endif
        request->fh = UVFS_I(dir)->fh;
        uvfs_make_request(trans);

        reply = &trans->u.reply.readdir;
        if (reply->error < 0)
        {
            int error = reply->error;
            kfree(trans);
            dprintk("<1>Exited uvfs_readdir, error:%d\n", error);
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

            ent = (uvfs_dirent_s*)(((unsigned long)ent +
                                    sizeof(*ent) +
                                    ent->length +
                                    3) & ~3);
        }
        kfree(trans);
    }
full:
    kfree(trans);
    dprintk("<1>Exited uvfs_readdir\n");
    return 0;
}

/*
 * This is called every time the dcache has a lookup hit,
 * and we should check whether we can really trust that lookup.
 *
 * NOTE! The hit can be a negative hit too, don't assume
 * we have an inode!
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
int uvfs_dentry_revalidate(struct dentry * dentry, unsigned int flags)
#else
int uvfs_dentry_revalidate(struct dentry * dentry, struct nameidata * nd)
#endif
{
    int error = 0;
    uvfs_attr_s attr;
    uvfs_fhandle_s fh;
    struct inode *inode;
    struct dentry *parent;
    dprintk("<1>Entered uvfs_dentry_revalidate\n");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,6,0)
    if (flags & LOOKUP_RCU)
	    return -ECHILD;
#endif

    parent = dget_parent(dentry);
    dprintk("<1>uvfs_dentry_revalidate - Got Parent\n");
    inode = dentry->d_inode;
    dprintk("<1>uvfs_dentry_revalidate - Got inode\n");

    /* always invalidate negative dentries */
    if (!inode)
    {
        dprintk("uvfs_dentry_revalidate: %s/%s negative dentry\n",
                parent->d_name.name, dentry->d_name.name);
        goto out_bad;
    }

    error = uvfs_lookup_by_name(parent->d_inode, &dentry->d_name, &fh, &attr);
    dprintk("<1>uvfs_dentry_revalidate - uvfs_lookup_by_name returned %d\n", error);
    if (error)
    {
        dprintk("uvfs_dentry_revalidate: %s/%s lookup error=%d\n",
                parent->d_name.name, dentry->d_name.name, error);

        /* don't invalidate if the lookup was interrupted by a signal */
        if (error == -ERESTARTSYS)
        {
            goto out;
        }
        goto out_bad;
    }

    if (!uvfs_compare_inode(inode, &fh))
    {
        dprintk("uvfs_dentry_revalidate: %s/%s fh mismatch\n",
                parent->d_name.name, dentry->d_name.name);
        goto out_bad;
    }

    dprintk("uvfs_dentry_revalidate: %s/%s still valid\n",
            parent->d_name.name, dentry->d_name.name);

out:
    dput(parent);
    dprintk("<1>Exited uvfs_dentry_revalidate - out\n");
    return 1;

out_bad:
    dprintk("<1>Exiting uvfs_dentry_revalidate - out_bad\n");
    if (inode && S_ISDIR(inode->i_mode)) {
        if (have_submounts(dentry))
            goto out;
        shrink_dcache_parent(dentry);
    }
    d_drop(dentry);
    dput(parent);
    dprintk("<1>Exited uvfs_dentry_revalidate - out_bad\n");
    return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
int uvfs_permission(struct inode *inode, int mask)
#else
int uvfs_permission(struct inode *inode, int mask, struct nameidata *nd)
#endif
{
    int error = 0;
    uvfs_getattr_req_s* request;
    uvfs_getattr_rep_s* reply;
    uvfs_transaction_s* trans;
    umode_t i_mode;
    umode_t mode;

    dprintk("<1>Entered uvfs_permission: node=%p, mask=%0x, mode=%X\n", inode, mask, inode->i_mode);
    i_mode = inode->i_mode;
    mode = inode->i_mode;

    mask &= MAY_READ | MAY_WRITE | MAY_EXEC;
    if (mask & MAY_WRITE) {
        /*
         * Nobody gets write access to a read-only fs.
         */
        if (IS_RDONLY(inode) &&
            (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
        {
       	    dprintk("<1>Exited uvfs_permission - EROFS\n");
            return -EROFS;
        }

        /*
         * Nobody gets write access to an immutable file.
         */
        if (IS_IMMUTABLE(inode))
        {
            dprintk("<1>Exited uvfs_permission - EACCES\n");
            return -EACCES;
        }
    }

    dprintk("uvfs_permission: Before calling UVFS_GETATTR\n");
    if (current_fsuid() != UVFS_I(inode)->attr_uid)
    {
        trans = uvfs_new_transaction();
        if (trans == NULL)
        {
	    dprintk("<1>Exited uvfs_permission - ENOMEM\n");
            return -ENOMEM;
        }
        request = &trans->u.request.getattr;
        request->type = UVFS_GETATTR;
        request->serial = trans->serial;
        request->size = sizeof(*request);
        request->uid = current_fsuid();
        request->gid = current_fsgid();
        request->fh = UVFS_I(inode)->fh;
	dprintk("uvfs_permission: calling UVFS_GETATTR\n");
        uvfs_make_request(trans);
	dprintk("uvfs_permission: UVFS_GETATTR call returned\n");

        reply = &trans->u.reply.getattr;
        error = reply->error;
        if (error)
        {
            kfree(trans);
            dprintk("<1>Exited uvfs_permission with error: %d\n", error);
            return error;
        }
        i_mode = reply->a.i_mode;
        mode = reply->a.i_mode;
        kfree(trans);
    }
    dprintk("uvfs_permission: After calling UVFS_GETATTR\n");

    if (current_fsuid() == inode->i_uid)
        mode >>= 6;
    else if (in_group_p(inode->i_gid))
        mode >>= 3;

    /*
     * If the DACs are ok we don't need any capability check.
     */
    if (((mode & mask & (MAY_READ|MAY_WRITE|MAY_EXEC)) == mask))
    {
    	dprintk("<1>Exited uvfs_permission - DACs are OK\n");
        return 0;
    }

    /*
     * Read/write DACs are always overridable.
     * Executable DACs are overridable if at least one exec bit is set.
     */
    if (!(mask & MAY_EXEC) || (i_mode & S_IXUGO) || S_ISDIR(i_mode))
        if (capable(CAP_DAC_OVERRIDE))
        {
            dprintk("<1>Exited uvfs_permission - DACs are overridable\n");
            return 0;
        }

    /*
     * Searching includes executable on directories, else just read.
     */
    if (mask == MAY_READ || (S_ISDIR(i_mode) && !(mask & MAY_WRITE)))
        if (capable(CAP_DAC_READ_SEARCH))
        {
            dprintk("<1>Exited uvfs_permission - CAP_DAC_READ_SEARCH\n");
            return 0;
        }

    dprintk("<1>Exited uvfs_permission\n");
    return -EACCES;
}
#else
int uvfs_do_getattr(struct inode *inode)
{
    int error = 0;
    uvfs_getattr_req_s* request;
    uvfs_getattr_rep_s* reply;
    uvfs_transaction_s* trans;

    dprintk("<1>Entered uvfs_do_getattr\n");

    dprintk("uvfs_permission: Before calling UVFS_GETATTR\n");
    if (current_fsuid().val != UVFS_I(inode)->attr_uid)
    {
        trans = uvfs_new_transaction();
        if (trans == NULL)
        {
	    dprintk("<1>Exited uvfs_permission - ENOMEM\n");
            return -ENOMEM;
        }
        request = &trans->u.request.getattr;
        request->type = UVFS_GETATTR;
        request->serial = trans->serial;
        request->size = sizeof(*request);
        request->uid = current_fsuid().val;
        request->gid = current_fsgid().val;
        request->fh = UVFS_I(inode)->fh;
	dprintk("uvfs_permission: calling UVFS_GETATTR\n");
        uvfs_make_request(trans);
	dprintk("uvfs_permission: UVFS_GETATTR call returned\n");

        reply = &trans->u.reply.getattr;
        error = reply->error;
        if (error)
        {
            kfree(trans);
            dprintk("<1>Exited uvfs_permission with error: %d\n", error);
            return error;
        }
        inode->i_mode = reply->a.i_mode;
        kfree(trans);
    }
    dprintk("uvfs_permission: After calling UVFS_GETATTR\n");

    return error;
}
int uvfs_perm_getattr(struct inode *inode, int mask)
{
	dprintk("<1>Entered uvfs_perm_getattr\n");
	if (mask & MAY_NOT_BLOCK)
	{
		dprintk("<1>Exiting uvfs_perm_getattr MAY_NOT_BLOCK\n");
		return -ECHILD;
	}
	return uvfs_do_getattr(inode);
}
int uvfs_permission(struct inode *inode, int mask)
{
	int err = 0;
	dprintk("<1>Entered uvfs_permission: node=%p, mask=%0x, mode=%X\n", inode, mask, inode->i_mode);
	err = generic_permission(inode, mask);

	dprintk("uvfs_permission, after calling generic_permission, err=%d\n", err);
	/* If permission is denied, try to refresh file
	 * attributes.  This is also needed, because the root
	 * node will at first have no permissions */
	if (err == -EACCES) {
		err = uvfs_perm_getattr(inode, mask);
		dprintk("uvfs_permission, after calling uvfs_perm_getattr, err=%d\n", err);
		if (!err)
			err = generic_permission(inode, mask);
	}
	dprintk("<1>Exited uvfs_permission err=%d\n", err);
	return err;
}
#endif
