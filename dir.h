/*
 *   linux/fs/uvfs/dir.h
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
/* dir.h -- Directory operations */

#ifndef _UVFS_DIR_H_
#define _UVFS_DIR_H_

#include <linux/fs.h>

#include "protocol.h"

extern void uvfs_set_inode(struct inode* inode,
                      uvfs_attr_s* a, struct dentry *dentry, int caller);
extern int uvfs_create(struct inode* dir, struct dentry* entry, int mode);
extern int uvfs_mknod(struct inode* dir, struct dentry* entry, int mode,
                      int rdev);
extern int uvfs_link(struct dentry* old_entry, 
                     struct inode* dir,
                     struct dentry* entry);
extern struct dentry* uvfs_lookup(struct inode* dir, struct dentry* entry);
extern int uvfs_refresh(struct inode* dir, struct dentry* entry);
extern int uvfs_unlink(struct inode* dir, struct dentry* entry);
extern int uvfs_symlink(struct inode* dir, 
                        struct dentry* entry, 
                        const char* path);
extern int uvfs_mkdir(struct inode* dir, struct dentry* entry, int mode);
extern int uvfs_rmdir(struct inode* dir, struct dentry* entry);
extern int uvfs_rename(struct inode* srcdir,
                       struct dentry* srcentry,
                       struct inode* dstdir,
                       struct dentry* dstentry);
extern int uvfs_readdir(struct file* filp,
                        void* data,
                        filldir_t filldir);
extern loff_t uvfs_dir_llseek(struct file *file, loff_t offset, int origin);
extern int uvfs_open(struct inode *inode, struct file *filp);

/* show antecedents of this dentry */
#ifdef DEBUG
#define SHOW_PATH(ENT) \
    if((ENT)->d_parent) \
    { \
       dprintk("parent %s %x\n", \
           (ENT)->d_parent->d_name.name, (ENT)->d_parent->d_inode); \
       if((ENT)->d_parent->d_parent) \
       { \
          dprintk("grand parent %s %x\n", \
              (ENT)->d_parent->d_parent->d_name.name, \
              (ENT)->d_parent->d_parent->d_inode); \
          if((ENT)->d_parent->d_parent->d_parent) \
          { \
             dprintk("great grand parent %s %x\n", \
                 (ENT)->d_parent->d_parent->d_parent->d_name.name, \
                 (ENT)->d_parent->d_parent->d_parent->d_inode); \
             if((ENT)->d_parent->d_parent->d_parent->d_parent) \
             { \
                dprintk("great great grand parent %s %x\n", \
                  (ENT)->d_parent->d_parent->d_parent->d_parent->d_name.name, \
                  (ENT)->d_parent->d_parent->d_parent->d_parent->d_inode); \
             } \
          } \
       } \
    } \

#endif /* SHOW_PATH */
#endif /* !_UVFS_DIR_H_ */




