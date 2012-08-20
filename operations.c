/*
 *   operations.c -- operations structs
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

#include <linux/module.h>
#include <linux/buffer_head.h>  /* for file_fsync() */
#include "uvfs.h"

struct file_operations Uvfs_file_file_operations =
{
    .read           = uvfs_file_read,
    .write          = uvfs_file_write,
    .mmap           = uvfs_file_mmap,
    .open           = generic_file_open,
    .fsync          = file_fsync,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
    .aio_read       = generic_file_aio_read,
    .aio_write      = generic_file_aio_write,
#endif
};

struct address_space_operations Uvfs_file_aops =
{
    .writepage      = uvfs_writepage,
    .readpage       = uvfs_readpage,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
    .write_begin    = uvfs_write_begin,
    .write_end      = uvfs_write_end,
#else
    .prepare_write  = uvfs_prepare_write,
    .commit_write   = uvfs_commit_write,
#endif
};

struct inode_operations Uvfs_file_inode_operations =
{
    .permission     = uvfs_permission,
    .setattr        = uvfs_setattr,
    .getattr        = uvfs_getattr,
};

struct inode_operations Uvfs_dir_inode_operations =
{
    .create         = uvfs_create,
    .lookup         = uvfs_lookup,
    .unlink         = uvfs_unlink,
    .symlink        = uvfs_symlink,
    .mkdir          = uvfs_mkdir,
    .rmdir          = uvfs_rmdir,
    .rename         = uvfs_rename,
    .permission     = uvfs_permission,
    .setattr        = uvfs_setattr,
    .getattr        = uvfs_getattr,
};

struct file_operations Uvfs_dir_file_operations =
{
    .read           = generic_read_dir,
    .readdir        = uvfs_readdir,
    .open           = generic_file_open,
};

struct inode_operations Uvfs_symlink_inode_operations =
{
    .readlink       = uvfs_readlink,
    .follow_link    = uvfs_follow_link,
    .setattr        = uvfs_setattr,
    .getattr        = uvfs_getattr,
};

struct dentry_operations Uvfs_dentry_operations =
{
    .d_revalidate   = uvfs_dentry_revalidate,
};

struct super_operations Uvfs_super_operations =
{
    .alloc_inode    = uvfs_alloc_inode,
    .destroy_inode  = uvfs_destroy_inode,
    .statfs         = uvfs_statfs,
};

struct export_operations Uvfs_export_operations =
{
    .encode_fh      = uvfs_encode_fh,
    .get_parent     = uvfs_get_parent,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
    .fh_to_dentry   = uvfs_fh_to_dentry,
    .fh_to_parent   = uvfs_fh_to_parent,
#else
    .decode_fh      = uvfs_decode_fh,
    .get_dentry     = uvfs_get_dentry,
#endif
};

struct file_system_type Uvfs_file_system_type =
{
    .owner          = THIS_MODULE,
    .name           = UVFS_MODULE_NAME,
    .get_sb         = uvfs_get_sb,
    .kill_sb        = kill_anon_super,
};
