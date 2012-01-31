/*
 *   super.c -- superblock and inode functions
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

#include <linux/statfs.h>
#include "uvfs.h"

void displayFhandle(const char* msg, uvfs_fhandle_s* fh)
{
    printk("%s  sbxid=0x%x, inum=0x%x, mntid=0x%x, arid=0x%x\n",
            msg,
            fh->no_fspid.fs_sbxid,
            fh->no_fspid.fs_sfuid,
            fh->no_mntid,
            fh->no_narid.na_aruid);
}

static void init_once(void *foo, kmem_cache_t *cachep, unsigned long flags)
{
    struct uvfs_inode_info *uvfsi = foo;

    if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
        SLAB_CTOR_CONSTRUCTOR)
    {
        inode_init_once(&uvfsi->vfs_inode);
    }
}

static kmem_cache_t *uvfs_inode_cachep;
int uvfs_init_inodecache(void)
{
    uvfs_inode_cachep = kmem_cache_create("uvfs_inode_cache",
                        sizeof(struct uvfs_inode_info),
                        0, SLAB_RECLAIM_ACCOUNT,
                        init_once, NULL);

    if (uvfs_inode_cachep == 0)
    {
        printk(KERN_ERR "uvfs_init_inodeache: "
               "Couldn't initialize inode slabcache\n");
        return -ENOMEM;
    }

    return 0;
}

void uvfs_destroy_inodecache(void)
{
    if (kmem_cache_destroy(uvfs_inode_cachep))
        printk(KERN_INFO "uvfs_inode_cache: not all structures were freed\n");
}


struct inode *uvfs_alloc_inode(struct super_block *sb)
{
    struct uvfs_inode_info *uvfsi;

    dprintk("<1>Entering uvfs_alloc_inode\n");
    uvfsi = (struct uvfs_inode_info *)kmem_cache_alloc(uvfs_inode_cachep, SLAB_KERNEL);
    if (!uvfsi)
        return 0;

    return &uvfsi->vfs_inode;
}

void uvfs_destroy_inode(struct inode *inode)
{
    kmem_cache_free(uvfs_inode_cachep, UVFS_I(inode));
}

/* functions passed to iget5_locked */
int uvfs_compare_inode(struct inode* inode, void* data)
{
    uvfs_fhandle_s * other = (uvfs_fhandle_s*) data;
    uvfs_fhandle_s * extra = &UVFS_I(inode)->fh;

    return (extra &&
        extra->no_narid.na_aruid == other->no_narid.na_aruid &&
        extra->no_fspid.fs_sfuid == other->no_fspid.fs_sfuid &&
        extra->no_fspid.fs_sbxid == other->no_fspid.fs_sbxid);

}

static int uvfs_init_inode(struct inode* inode, void* data)
{
    UVFS_I(inode)->fh = *((uvfs_fhandle_s*)data);

    return 0;
}

struct inode *
uvfs_iget(struct super_block *sb, uvfs_fhandle_s *fh, uvfs_attr_s *fattr)
{
    struct inode *inode = NULL;
    unsigned long hash;

    hash = (unsigned long) fh->no_fspid.fs_sfuid;
    inode = iget5_locked(sb, hash, &uvfs_compare_inode, &uvfs_init_inode, fh);
    if (inode == NULL)
    {
        printk("uvfs_iget: iget5_locked failed\n");
        return inode;
    }
    if (inode->i_state & I_NEW)
    {
        inode->i_ino = hash;

        inode->i_flags |= S_NOATIME|S_NOCMTIME;

        inode->i_mode = fattr->i_mode;
        inode->i_nlink = fattr->i_nlink;
        inode->i_uid = fattr->i_uid;
        inode->i_gid = fattr->i_gid;
        inode->i_size = fattr->i_size;
        inode->i_atime.tv_sec = fattr->i_atime.tv_sec;
        inode->i_atime.tv_nsec = fattr->i_atime.tv_nsec;
        inode->i_mtime.tv_sec = fattr->i_mtime.tv_sec;
        inode->i_mtime.tv_nsec = fattr->i_mtime.tv_nsec;
        inode->i_ctime.tv_sec = fattr->i_ctime.tv_sec;
        inode->i_ctime.tv_nsec = fattr->i_ctime.tv_nsec;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
        inode->i_blksize = fattr->i_blksize;
#endif
        inode->i_blocks = fattr->i_blocks;
        inode->i_rdev = fattr->devno;

        if (S_ISREG(inode->i_mode))
        {
            inode->i_op = &Uvfs_file_inode_operations;
            inode->i_fop = &Uvfs_file_file_operations;
            inode->i_data.a_ops = &Uvfs_file_aops;
        }
        else if (S_ISDIR(inode->i_mode))
        {
            inode->i_op = &Uvfs_dir_inode_operations;
            inode->i_fop = &Uvfs_dir_file_operations;
        }
        else if (S_ISLNK(inode->i_mode))
        {
            inode->i_op = &Uvfs_symlink_inode_operations;
        }
        else
        {
            /* we should never come through this code path */
            printk("uvfs_iget: unexpected mode 0x%x for ino %ld rdev %d\n",
                   (unsigned)inode->i_mode, inode->i_ino, inode->i_rdev);
            init_special_inode(inode, inode->i_mode, fattr->devno);
        }

        unlock_new_inode(inode);
    }
    else
    {
        uvfs_refresh_inode(inode, fattr);
    }
    return inode;
}

int uvfs_refresh_inode(struct inode *inode, uvfs_attr_s *fattr)
{
    if (inode->i_size != fattr->i_size ||
        inode->i_mtime.tv_sec != fattr->i_mtime.tv_sec ||
        inode->i_mtime.tv_nsec != fattr->i_mtime.tv_nsec)
    {
        invalidate_inode_pages(inode->i_mapping);
    }

    inode->i_mode = fattr->i_mode;
    inode->i_nlink = fattr->i_nlink;
    inode->i_uid = fattr->i_uid;
    inode->i_gid = fattr->i_gid;
    inode->i_size = fattr->i_size;
    inode->i_atime.tv_sec = fattr->i_atime.tv_sec;
    inode->i_atime.tv_nsec = fattr->i_atime.tv_nsec;
    inode->i_mtime.tv_sec = fattr->i_mtime.tv_sec;
    inode->i_mtime.tv_nsec = fattr->i_mtime.tv_nsec;
    inode->i_ctime.tv_sec = fattr->i_ctime.tv_sec;
    inode->i_ctime.tv_nsec = fattr->i_ctime.tv_nsec;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
    inode->i_blksize = fattr->i_blksize;
#endif
    inode->i_blocks = fattr->i_blocks;
    inode->i_rdev = fattr->devno;

    return 0;
}

int uvfs_revalidate_inode(struct inode *inode)
{
    int error = 0;
    uvfs_getattr_req_s* request;
    uvfs_getattr_rep_s* reply;
    uvfs_transaction_s* trans;

    dprintk("<1>Entering uvfs_revalidate_inode\n");

    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.getattr;
    request->type = UVFS_GETATTR;
    request->serial = trans->serial;
    request->size = sizeof(*request);
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->fh = UVFS_I(inode)->fh;
    uvfs_make_request(trans);

    reply = &trans->u.reply.getattr;
    error = reply->error;

    if (!error)
    {
        uvfs_refresh_inode(inode, &reply->a);
    }

    kfree(trans);
    dprintk("<1>Exiting uvfs_revalidate_inode\n");
    return error;
}

/*
 * uvfs_encode_fh - encode filehandle data for NFS export
 * @dentry:  the dentry to encode
 * @fh:      where to store the file handle fragment
 * @max_len: maximum length to store there
 * @connectable: whether to store parent information; we ignore this field.
 *
 * This function fills the export_operations->encode_fh operation
 * for the uvfs filesystem.  It can't use the default implementation
 * because we depend on more than just the 32bit inode number.
 *
 * Returns the type code for the fh fragment (we are currently supporting
 * a single type); returning 255 ultimately translates to NFSERR_OPNOTSUPP
 */
int uvfs_encode_fh(struct dentry *dentry, __u32 *fh, int *max_len, int connectable)
{
    struct inode *inode = dentry->d_inode;

    dprintk("<1>uvfs_encode_fh: called against %s\n", dentry->d_name.name);
    debugDisplayFhandle("uvfs_encode_fh: ", &UVFS_I(inode)->fh);

    if (*max_len < 3)
        return 255;

    fh[0] = UVFS_I(inode)->fh.no_narid.na_aruid;
    fh[1] = UVFS_I(inode)->fh.no_fspid.fs_sfuid;
    fh[2] = UVFS_I(inode)->fh.no_fspid.fs_sbxid;

    *max_len = 3;
    return 1;
}

/*
 * uvfs_decode_fh - decode filehandle data for NFS export
 * @sb:  The superblock
 * @fh:  pointer to the file handle fragment
 * @fh_len: length of file handle fragment
 * @fh_type:    value 1 => fh can be passed directly to get_dentry()
 * @acceptable: function for testing acceptability of dentrys
 * @content:    context for @acceptable
 *
 * Allocate a dentry for the given filehandle
 *
 * @NOTES: acceptable() will be nfsd_acceptable() for nfs exports.
 */
struct dentry *uvfs_decode_fh(struct super_block *sb,
                              __u32 *fh,
                              int fh_len,
                              int fh_type,
                              int (*acceptable)(void *, struct dentry*),
                              void *context)
{
    uvfs_fhandle_s vfs_fh;

    dprintk("<1>uvfs_decode_fh: fh_len = %d, fh_type = %d\n", fh_len, fh_type);

    memset(&vfs_fh, 0, sizeof(vfs_fh));
    if (fh_type == 1)
    {
        vfs_fh.no_narid.na_aruid = fh[0];
        vfs_fh.no_fspid.fs_sfuid = fh[1];
        vfs_fh.no_fspid.fs_sbxid = fh[2];
    }
    else
    {
        dprintk("<1>uvfs_decode_fh() got unexpected fh_type %d\n", fh_type);
        return 0;
    }

    return sb->s_export_op->find_exported_dentry(sb, &vfs_fh, 0, acceptable, context);
}

/*
 * called with child->d_inode->i_sem down
 * (see find_exported_dentry in exportfs/expfs.c)
 */
struct dentry* uvfs_get_parent(struct dentry *child)
{
    struct dentry *parent;
    struct qstr dotdot = { name: "..", len: 2 };
    uvfs_fhandle_s fh;
    uvfs_attr_s attr;
    struct inode *inode = 0;
    int err;

    debugDisplayFhandle("uvfs_get_parent: child fh is: ",
                        &UVFS_I(child->d_inode)->fh);
    err = uvfs_lookup_by_name(child->d_inode, &dotdot, &fh, &attr);
    if (err)
    {
        return ERR_PTR(err);
    }

    inode = uvfs_iget(child->d_inode->i_sb, &fh, &attr);
    if (!inode)
        return ERR_PTR(-EACCES);

    parent = d_alloc_anon(inode);
    if (!parent)
    {
        iput(inode);
        return ERR_PTR(-ENOMEM);
    }
    parent->d_op = &Uvfs_dentry_operations;

    debugDisplayFhandle("uvfs_get_parent: parent fh is: ", &fh);
    return parent;
}

/*
 * the default version uses iget, making it unusable for us
 */
struct dentry* uvfs_get_dentry(struct super_block *sb, void *inump)
{
    uvfs_fhandle_s *fh = (uvfs_fhandle_s*) inump;
    struct inode *inode = NULL;
    unsigned long hash;
    struct dentry *result = 0;
    int error = 0;
    uvfs_getattr_req_s* request;
    uvfs_getattr_rep_s* reply;
    uvfs_transaction_s* trans;

    hash = (unsigned long) fh->no_fspid.fs_sfuid;
    inode = ilookup5(sb, hash, &uvfs_compare_inode, fh);
    if (inode == NULL)
    {
        dprintk("uvfs_get_dentry: ilookup5 failed, hash = %lu\n", hash);
        debugDisplayFhandle("uvfs_get_dentry: fh is: ", fh);

        trans = uvfs_new_transaction();
        if (trans == NULL)
        {
            return ERR_PTR(-ENOMEM);
        }
        request = &trans->u.request.getattr;
        request->type = UVFS_GETATTR;
        request->serial = trans->serial;
        request->size = sizeof(*request);
        request->uid = current->fsuid;
        request->gid = current->fsgid;
        request->fh = *fh;
        uvfs_make_request(trans);

        reply = &trans->u.reply.getattr;
        error = reply->error;

        if (!error)
        {
            inode = uvfs_iget(sb, fh, &reply->a);
            if (!inode)
            {
                dprintk("<1>uvfs_get_dentry: uvfs_iget returned a null inode!\n");
                result = ERR_PTR(-EACCES);
            }
        } else
        {
            dprintk("<1>uvfs_get_dentry: revalidate got error %d\n", error);
            if (error == -ECOMM)
            {
                displayFhandle("uvfs_get_dentry: ESTALE on fh: ", fh);
                result = ERR_PTR(-ESTALE);
            }
            else
            {
                result = ERR_PTR(error);
            }
        }

        kfree(trans);
    }

    if (inode)
    {
        result = d_alloc_anon(inode);
        if (!result)
        {
            iput(inode);
            return ERR_PTR(-ENOMEM);
        }
        result->d_op = &Uvfs_dentry_operations;
    }

    dprintk("<1>uvfs_get_dentry: returning 0x%p\n", result);
    return result;
}

/*
 * stat the file system
 *
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
int uvfs_statfs(struct dentry* dentry, struct kstatfs* stat)
#else
int uvfs_statfs(struct super_block* sb, struct kstatfs* stat)
#endif
{
    int error = 0;
    uvfs_statfs_req_s* request;
    uvfs_statfs_rep_s* reply;
    uvfs_transaction_s* trans;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
    struct inode *inode = dentry->d_inode;
#endif
    dprintk("<1>Entering uvfs_statfs\n");

    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        return -ENOMEM;
    }
    request = &trans->u.request.statfs;
    request->type = UVFS_STATFS;
    request->serial = trans->serial;
    request->size = sizeof(*request);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
    request->fh = UVFS_I(inode)->fh;
#else
    request->fh = UVFS_I(sb->s_root->d_inode)->fh;
#endif

    uvfs_make_request(trans);

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

    kfree(trans);
    dprintk("<1>Exited uvfs_statfs %d\n", error);
    return error;
}


/* format:  option1=data1,option2=data2
 * imagine future options might include timeout for iwserver,
 * cache expiration
 */
static int uvfs_parse_options(struct super_block* sb, char* options, char **iwstore)
{
    if (!strncmp(options, "store=", 6))
    {
        *iwstore = options + 6;
        return 0;
    }
    return 1;
}

/* Called at mount time. */
int uvfs_read_super(struct super_block* sb,
                                void* data,
                                int silent)
{
    int retval = 0;
    struct inode* root;
    uvfs_read_super_req_s* request;
    uvfs_read_super_rep_s* reply;
    uvfs_transaction_s* trans;
    char* arg;
    size_t arglength;

    dprintk("<1>Entering uvfs_read_super:"
           "sb = 0x%p, data = 0x%p, silent = %d\n",
           sb, data, silent);
    if (data == 0 || uvfs_parse_options(sb, data, &arg))
    {
        printk("<1>uvfs_read_super: invalid options!\n");
        return -EINVAL;
    }

    arglength = strlen(arg) + 1;
    if (arglength >= UVFS_MAX_PATHLEN)
    {
        dprintk("<1>Exited uvfs_read_super > UVFS_MAX_PATHLEN\n");
        return -ENAMETOOLONG;
    }
    dprintk("<1>uvfs_read_super uvfs_new_transaction\n");
    trans = uvfs_new_transaction();
    if (trans == NULL)
    {
        dprintk("<1>Exited uvfs_read_super\n");
        return -ENOMEM;
    }
    dprintk("<1>uvfs_read_super build request\n");
    request = &trans->u.request.read_super;
    request->type = UVFS_READ_SUPER;
    request->serial = trans->serial;
    request->size = offsetof(uvfs_read_super_req_s, buff) + arglength;
    request->uid = current->fsuid;
    request->gid = current->fsgid;
    request->arglength = arglength;
    memcpy(request->buff, arg, arglength);
    dprintk("<1>uvfs_read_super uvfs_make_request\n");
    uvfs_make_request(trans);

    reply = &trans->u.reply.read_super;
    if (reply->error < 0)
    {
        retval = reply->error;
        goto out;
    }
    dprintk("<1>uvfs_read_super get reply\n");
    sb->s_maxbytes = 0xFFFFFFFF;
    sb->s_blocksize = reply->s_blocksize;
    sb->s_blocksize_bits = reply->s_blocksize_bits;
    sb->s_magic = reply->s_magic;
    sb->s_op = &Uvfs_super_operations;
    sb->s_export_op = &Uvfs_export_operations;

    root = uvfs_iget(sb, &reply->fh, &reply->a);
    if (root == NULL)
    {
        retval= -ENOMEM;
        goto out;
    }

    sb->s_root = d_alloc_root(root);

    retval = 0;

out:
    kfree(trans);
    dprintk("<1>Exited uvfs_read_super %d\n", retval);
    return retval;
}

/*
 * super block wrapper function used by Linux 2.6.x kernels
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
int uvfs_get_sb(struct file_system_type *fs_type,
        int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
    return get_sb_nodev(fs_type, flags, data, uvfs_read_super, mnt);
}
#else
struct super_block *uvfs_get_sb(struct file_system_type *fs_type,
        int flags, const char *dev_name, void *data)
{
    return get_sb_nodev(fs_type, flags, data, uvfs_read_super);
}
#endif
