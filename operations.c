/*
 *   linux/fs/uvfs/operations.c
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
/* operations.c -- All of the operations structs are defined here. */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/fs.h>
#include "operations.h"
#include "dir.h"
#include "file.h"
#include "symlink.h"
#include "dentry.h"
#include "super.h"

/* Operations on files. */

struct file_operations Uvfs_file_file_operations =
{
    llseek: uvfs_file_llseek,
#ifdef UVFS_KVEC
    kvec_read: uvfs_kvec_read,
    kvec_write: uvfs_kvec_write,
#endif
    read: uvfs_file_read,
    write: uvfs_file_write,
    mmap: uvfs_mmap,
    open: uvfs_open,
    fsync: file_fsync,
#ifdef UVFS_AIO
    aio_read: generic_file_aio_read,
    aio_write: generic_file_aio_write
#endif
};


struct address_space_operations Uvfs_file_aops =
{
    writepage: uvfs_writepage,
    readpage: uvfs_readpage,
    prepare_write: uvfs_prepare_write,
    commit_write: uvfs_commit_write,
};


struct inode_operations Uvfs_file_inode_operations =
{
    setattr: uvfs_setattr,
    getattr: uvfs_getattr,
    permission: uvfs_permission,
    revalidate: uvfs_revalidate
};


struct inode_operations Uvfs_dir_inode_operations =
{
    create: uvfs_create,
    lookup: uvfs_lookup,
    unlink: uvfs_unlink,
#ifdef UVFS_IMPL_SYMLINK
    symlink: uvfs_symlink,
#endif
    mkdir: uvfs_mkdir,
    rmdir: uvfs_rmdir,
    rename: uvfs_rename,
    setattr: uvfs_setattr,
    getattr: uvfs_getattr,
    permission: uvfs_permission,
    revalidate: uvfs_revalidate,
#ifdef UVFS_IMPL_MKNOD
    mknod: uvfs_mknod,
#endif
#ifdef UVFS_IMPL_LINK
    link: uvfs_link
#endif
};


struct file_operations Uvfs_dir_file_operations =
{
    llseek:  uvfs_dir_llseek,
    read: generic_read_dir,
    readdir: uvfs_readdir,
    open: uvfs_open
};


struct inode_operations Uvfs_symlink_inode_operations =
{
#ifdef UVFS_IMPL_SYMLINK
    readlink: uvfs_readlink,
    follow_link: uvfs_follow_link,
#endif
    setattr: uvfs_setattr,
    getattr: uvfs_getattr,
    permission: uvfs_permission,
    revalidate: uvfs_revalidate
};


struct super_operations Uvfs_super_operations =
{
    read_inode: uvfs_read_inode,
    statfs: uvfs_statfs,
    put_super: uvfs_put_super,
#ifdef UVFS_IMPL_DELETE_INODE
    delete_inode: uvfs_delete_inode
#endif /* UVFS_IMPL_DELETE_INODE */
};


struct file_system_type Uvfs_file_system_type =
{
    name: UVFS_FS_NAME,
    read_super: uvfs_read_super,
};

