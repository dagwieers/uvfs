/*
 *   linux/fs/uvfs/file.h
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
/* file.h -- File doings */

#ifndef _UVFS_FILE_H_
#define _UVFS_FILE_H_

#include <linux/fs.h>
#include <linux/mm.h>

extern int uvfs_writepage(struct page* pg);
extern int uvfs_readpage(struct file* filp, struct page* pg);
extern int uvfs_prepare_write(struct file* filp,
                              struct page* pg,
                              unsigned from,
                              unsigned to);
extern int uvfs_commit_write(struct file* filp,
                             struct page* pg,
                             unsigned from,
                             unsigned to);
extern int uvfs_setattr(struct dentry* entry, struct iattr* iattr);
extern int uvfs_getattr(struct dentry* entry, struct iattr* iattr);
extern int uvfs_permission(struct inode* inode, int mask);
extern int uvfs_revalidate(struct dentry* entry);
extern int uvfs_file_write(struct file* filp,
                           const char* buf,
                           size_t count,
                           loff_t* offset);
extern int uvfs_file_read(struct file* filp,
                          char* buf,
                          size_t count,
                          loff_t* offset);
extern int uvfs_mmap(struct file* filp, struct vm_area_struct* vma);
extern loff_t uvfs_file_llseek(struct file *file, loff_t offset, int origin);
extern int uvfs_kvec_write(struct file *file, kvec_cb_t cb, size_t count, loff_t pos);
extern int uvfs_kvec_read(struct file *file, kvec_cb_t cb, size_t count, loff_t pos);

#endif /* _UVFS_FILE_H_ */








