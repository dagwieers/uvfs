/*
 *   linux/fs/uvfs/super.h
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
/* super.h -- Superblock operations and read and write inode. */

#ifndef _UVFS_SUPER_H_
#define _UVFS_SUPER_H_

#include <linux/fs.h>

extern void uvfs_read_inode(struct inode* inode);
extern int uvfs_statfs(struct super_block* sb, struct statfs* stat);
extern void uvfs_put_super(struct super_block* sb);
extern struct super_block* uvfs_read_super(struct super_block* sb,
                                           void* data,
                                           int silent);
extern void uvfs_delete_inode(struct inode* inode);
extern void uvfs_reuse_inum(int inum);

extern struct super_block* Sb;

#endif /* !_UVFS_SUPER_H_ */


