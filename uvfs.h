/*
 *   uvfs.h
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

#ifndef _UVFS_UVFS_H_
#define _UVFS_UVFS_H_

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/version.h>
#include "protocol.h"

#define UVFS_FS_NAME "pmfs"
#define UVFS_LICENSE "GPL"
#define UVFS_VERSION "2.0.5"

struct uvfs_inode_info
{
    struct _uvfs_fhandle_s fh;
    struct inode vfs_inode;
};

static inline struct uvfs_inode_info *UVFS_I(struct inode *inode)
{
    return container_of(inode, struct uvfs_inode_info, vfs_inode);
}

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
    int in_use;
    int abort;
    int answered;
} uvfs_transaction_s;

#ifdef DEBUG_PRINT
#define dprintk printk
#define debugDisplayFhandle displayFhandle
#else
#define dprintk(fmt, args...)
#define debugDisplayFhandle(msg, fh)
#endif

/* uvfs/dir.c */
extern int uvfs_create(struct inode *, struct dentry *, int, struct nameidata *);
extern int uvfs_lookup_by_name(struct inode*, struct qstr*, uvfs_fhandle_s*, uvfs_attr_s*);
extern struct dentry* uvfs_lookup(struct inode *, struct dentry *, struct nameidata *);
extern int uvfs_unlink(struct inode *, struct dentry *);
extern int uvfs_symlink(struct inode *, struct dentry *, const char *);
extern int uvfs_mkdir(struct inode *, struct dentry *, int);
extern int uvfs_rmdir(struct inode *, struct dentry *);
extern int uvfs_rename(struct inode *, struct dentry *, struct inode *, struct dentry *);
extern int uvfs_readdir(struct file *, void *, filldir_t);
extern int uvfs_open(struct inode *, struct file *);
extern int uvfs_dentry_revalidate(struct dentry *, struct nameidata *);
extern int uvfs_permission(struct inode *, int, struct nameidata *);

/* uvfs/driver.c */
extern int uvfs_make_request(uvfs_transaction_s *);
extern uvfs_transaction_s* uvfs_new_transaction(void);

/* uvfs/file.c */
extern int uvfs_writepage(struct page *, struct writeback_control *);
extern int uvfs_readpage(struct file *, struct page *);
extern int uvfs_prepare_write(struct file *, struct page *, unsigned, unsigned);
extern int uvfs_commit_write(struct file *, struct page *, unsigned, unsigned);
extern int uvfs_setattr(struct dentry *, struct iattr *);
extern int uvfs_getattr(struct vfsmount *, struct dentry *, struct kstat *);
extern ssize_t uvfs_file_write(struct file *, const char *, size_t, loff_t *);
extern ssize_t uvfs_file_read(struct file *, char *, size_t, loff_t *);
extern int uvfs_file_mmap(struct file *, struct vm_area_struct *);

/* uvfs/operations.c */
extern struct file_operations Uvfs_file_file_operations;
extern struct address_space_operations Uvfs_file_aops;
extern struct inode_operations Uvfs_file_inode_operations;
extern struct inode_operations Uvfs_dir_inode_operations;
extern struct file_operations Uvfs_dir_file_operations;
extern struct inode_operations Uvfs_symlink_inode_operations;
extern struct dentry_operations Uvfs_dentry_operations;
extern struct super_operations Uvfs_super_operations;
extern struct file_system_type Uvfs_file_system_type;
extern struct export_operations Uvfs_export_operations;

/* uvfs/super.c */
extern void displayFhandle(const char *, uvfs_fhandle_s *);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
extern int uvfs_statfs(struct dentry *, struct kstatfs *);
extern int uvfs_get_sb(struct file_system_type *, int, const char *, void *, struct vfsmount *);
#else
extern int uvfs_statfs(struct super_block *, struct kstatfs *);
extern struct super_block *uvfs_get_sb(struct file_system_type *, int, const char *, void *);
#endif
extern int uvfs_read_super(struct super_block *, void *, int);
extern int uvfs_init_inodecache(void);
extern void uvfs_destroy_inodecache(void);
extern struct inode *uvfs_alloc_inode(struct super_block *);
extern void uvfs_destroy_inode(struct inode *);
extern struct inode *uvfs_iget(struct super_block *, uvfs_fhandle_s *, uvfs_attr_s *);
extern int uvfs_refresh_inode(struct inode *, uvfs_attr_s *);
extern int uvfs_revalidate_inode(struct inode *);
extern int uvfs_compare_inode(struct inode* inode, void* data);

struct dentry *uvfs_decode_fh(struct super_block *, __u32 *, int, int, int (*)(void *, struct dentry*), void *);
int uvfs_encode_fh(struct dentry *dentry, __u32 *fh, int *max_len, int connectable);
struct dentry* uvfs_get_dentry(struct super_block *sb, void *inump);
struct dentry* uvfs_get_parent(struct dentry *child);

/* uvfs/symlink.c */
extern int uvfs_readlink(struct dentry *, char *, int);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,18)
extern void * uvfs_follow_link(struct dentry *, struct nameidata *);
#else
extern int uvfs_follow_link(struct dentry *, struct nameidata *);
#endif

#endif /* _UVFS_UVFS_H_ */
