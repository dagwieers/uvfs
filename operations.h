/*
 *   linux/fs/uvfs/operations.h
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
/* operations.h -- All of the myriad operations structs */

#ifndef _UVFS_OPERATIONS_H_
#define _UVFS_OPERATIONS_H_

#include <linux/fs.h>

extern struct file_operations Uvfs_file_file_operations;
extern struct address_space_operations Uvfs_file_aops;
extern struct inode_operations Uvfs_file_inode_operations;
extern struct inode_operations Uvfs_dir_inode_operations;
extern struct file_operations Uvfs_dir_file_operations;
extern struct inode_operations Uvfs_symlink_inode_operations;
extern struct dentry_operations Uvfs_dentry_operations;
extern struct super_operations Uvfs_super_operations;
extern struct file_system_type Uvfs_file_system_type;
extern struct file_operations Uvfsd_file_operations;
#endif /* _OPERATIONS_H_ */










