/*
 *   linux/fs/uvfs/protocol.h
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
/* protocol.h -- Types for user-space kernel-space communication. */

#ifndef _UVFS_PROTOCOL_H_
#define _UVFS_PROTOCOL_H_

#ifdef __KERNEL__
#include <linux/pagemap.h>
#endif 

#ifndef __KERNEL__
#define PAGE_CACHE_SIZE 4096
#define ATTR_MODE	1
#define ATTR_UID	2
#define ATTR_GID	4
#define ATTR_SIZE	8
#define ATTR_ATIME	16
#define ATTR_MTIME	32
#define ATTR_CTIME	64
#define ATTR_ATIME_SET	128
#define ATTR_MTIME_SET	256
#define ATTR_FORCE	512	/* Not a change, but a change it */
#define ATTR_ATTR_FLAG	1024
#endif /* !__KERNEL__ */

#define UVFS_MAX_NAMELEN 256
#define UVFS_MAX_PATHLEN 1024
#define UVFS_BUFFSIZE 2048

#define UVFS_REGULAR 0
#define UVFS_DIRECTORY 1
#define UVFS_SLINK 2

#define UVFS_IOCTL_SHUTDOWN 42
#define UVFS_IOCTL_STATUS 43
#define UVFS_IOCTL_USE_COUNT 44
#define UVFS_IOCTL_MOUNT 45

#define byte_t  char
#define uint4_t unsigned int
typedef uint4_t vfs_mntid_t;
typedef uint4_t vfs_sfuid_t;
typedef uint4_t vfs_aruid_t;
typedef uint4_t vfs_sbxid_t;

typedef struct vfs_fspid_s
{
    vfs_sbxid_t fs_sbxid;
    vfs_sfuid_t fs_sfuid;
}
vfs_fspid_s;

typedef struct vfs_narid_s
{
    uint4_t     na_ipaddr;              /* archive IP address */
    vfs_aruid_t na_aruid;               /* archive unique id */
}
vfs_narid_s;

typedef struct vfs_fhandle_s            /* see vfs_TypeWriteNOID() to change */
{
    uint4_t     no_mntid;     /* 00-03 */
    byte_t      no_flags[4];  /* 04-07 */
    vfs_narid_s no_narid;     /* 08-15 */
    vfs_sbxid_t no_sbxid;     /* 16-19 */
    vfs_fspid_s no_fspid;     /* 20-27 */
    vfs_mntid_t no_blank2;    /* 28-31 */       /* field reserved for hammer */
}
vfs_fhandle_s;

/* kernel-space inode structure with dentry */
typedef struct _uvfs_inode_local
{
    vfs_fhandle_s handle;

    unsigned short flags;

    struct dentry *dentry;

    unsigned i_mode;
    unsigned long i_size;

} uvfs_inode_local_s;
        

/* user-space inode structure with out dentry */
typedef struct _uvfs_inode_info
{
    vfs_fhandle_s handle;

    unsigned short flags;

} uvfs_inode_info_s;
        
#define BLOCKSIZE	4096

typedef struct _uvfs_attr_s
{
    unsigned i_mode;
    unsigned i_nlink;
    unsigned i_uid;
    unsigned i_gid;
    unsigned long i_size;
    unsigned i_atime;
    unsigned i_mtime;
    unsigned i_ctime;
    unsigned long i_blksize;
    unsigned long i_blocks;
    int devno;
} uvfs_attr_s;

/* Requests */

typedef struct _uvfs_generic_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
} uvfs_generic_req_s;

#define UVFS_WRITE 1

typedef struct _uvfs_file_write_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int ino;
    uvfs_inode_info_s fh;
    unsigned count;
    unsigned offset;
    char buff[PAGE_CACHE_SIZE];
} uvfs_file_write_req_s;

#define UVFS_READ 2

typedef struct _uvfs_file_read_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int ino;
    uvfs_inode_info_s fh;
    int count;
    int offset;
} uvfs_file_read_req_s;

#define UVFS_CREATE 3

typedef struct _uvfs_create_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int d_ino;
    uvfs_inode_info_s fh;
    int mode;
    unsigned namelen;
    char name[UVFS_MAX_NAMELEN];
} uvfs_create_req_s;

#define UVFS_LOOKUP 4

typedef struct _uvfs_lookup_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int d_ino;
    uvfs_inode_info_s fh;
    int namelen;
    char name[UVFS_MAX_NAMELEN];
	int ino;
} uvfs_lookup_req_s;

#define UVFS_UNLINK 5

typedef struct _uvfs_unlink_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int d_ino;
    uvfs_inode_info_s fh;
    int namelen;
    char name[UVFS_MAX_NAMELEN];
} uvfs_unlink_req_s;

#define UVFS_SYMLINK 6

typedef struct _uvfs_symlink_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int d_ino;
    uvfs_inode_info_s fh;
    char name[UVFS_MAX_NAMELEN];
    int namelen;
    char sympath[UVFS_MAX_PATHLEN];
    int symlen;
} uvfs_symlink_req_s;

#define UVFS_MKDIR 7

typedef struct _uvfs_mkdir_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int mode;
    int d_ino;
    uvfs_inode_info_s fh;
    int namelen;
    char name[UVFS_MAX_NAMELEN];
} uvfs_mkdir_req_s;

#define UVFS_RMDIR 8

typedef struct _uvfs_rmdir_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int d_ino;
    uvfs_inode_info_s fh;
    int namelen;
    char name[UVFS_MAX_NAMELEN];
} uvfs_rmdir_req_s;

#define UVFS_RENAME 9

typedef struct _uvfs_rename_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int old_d_ino;
    uvfs_inode_info_s fh_old;
    char old_name[UVFS_MAX_NAMELEN];
    int old_namelen;
    int new_d_ino;
    uvfs_inode_info_s fh_new;
    char new_name[UVFS_MAX_NAMELEN];
    int new_namelen;
} uvfs_rename_req_s;

#define UVFS_READDIR 10

typedef struct _uvfs_readdir_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int d_ino;
    uvfs_inode_info_s fh;
    int entry_no;
} uvfs_readdir_req_s;

#define UVFS_SETATTR 11

typedef struct _uvfs_setattr_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int i_ino;
    uvfs_inode_info_s fh;
    unsigned ia_valid;
    unsigned ia_uid;
    unsigned ia_gid;
    unsigned ia_size;
    unsigned ia_atime;
    unsigned ia_mtime;
    unsigned ia_ctime;
    unsigned ia_mode;
} uvfs_setattr_req_s;

#define UVFS_GETATTR 12

typedef struct _uvfs_getattr_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    unsigned i_mode;
    int i_ino;
    uvfs_inode_info_s fh;
} uvfs_getattr_req_s;

#define UVFS_STATFS 13

typedef struct _uvfs_statfs_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
} uvfs_statfs_req_s;

#define UVFS_PUT_SUPER 14

typedef struct _uvfs_put_super_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
} uvfs_put_super_req_s;

#define UVFS_READ_SUPER 15

typedef struct _uvfs_read_super_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int arglength;
    char buff[UVFS_MAX_PATHLEN];
} uvfs_read_super_req_s;

#define UVFS_READLINK 16

typedef struct _uvfs_readlink_req_s
{ 
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int ino;
    uvfs_inode_info_s fh;
} uvfs_readlink_req_s;

#define UVFS_DELETE_INODE 17

typedef struct _uvfs_delete_inode_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int ino;
    uvfs_inode_info_s fh;
} uvfs_delete_inode_req_s;
       
#define UVFS_SHUTDOWN 18

/* Note that there is no corresponding reply struct. */
typedef struct _uvfs_shutdown_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
} uvfs_shutdown_req_s;

#define UVFS_MKNOD 19

typedef struct _uvfs_mknod_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    unsigned uid;
    unsigned gid;
    int d_ino;
    uvfs_inode_info_s fh;
    int mode;
    int devno;
    unsigned namelen;
    char name[UVFS_MAX_NAMELEN];
} uvfs_mknod_req_s;

#define UVFS_LINK 20

typedef struct _uvfs_link_req_s
{
    int type;
    int serial;
    int size;
    unsigned long fsid;
    int d_ino;
    uvfs_inode_info_s fh_d;
    int ino;
    uvfs_inode_info_s fh;
    unsigned namelen;
    char name[UVFS_MAX_NAMELEN];
} uvfs_link_req_s;
    
typedef union _uvfs_request_u
{
    uvfs_generic_req_s generic;
    uvfs_file_write_req_s file_write;
    uvfs_file_read_req_s file_read;
    uvfs_create_req_s create;
    uvfs_lookup_req_s lookup;
    uvfs_unlink_req_s unlink;
    uvfs_symlink_req_s symlink;
    uvfs_mkdir_req_s mkdir;
    uvfs_rmdir_req_s rmdir;
    uvfs_rename_req_s rename;
    uvfs_readdir_req_s readdir;
    uvfs_setattr_req_s setattr;
    uvfs_getattr_req_s getattr;
    uvfs_statfs_req_s statfs;
    uvfs_put_super_req_s put_super;
    uvfs_read_super_req_s read_super;
    uvfs_readlink_req_s readlink;
    uvfs_delete_inode_req_s delete_inode;
    uvfs_shutdown_req_s shutdown;
    uvfs_mknod_req_s mknod;
    uvfs_link_req_s link;
} uvfs_request_u;


/* Replies */

typedef struct _uvfs_generic_rep_s
{
    int type;
    int serial;
    int size;
} uvfs_generic_rep_s;


typedef struct _uvfs_file_write_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    unsigned bytes_written;
} uvfs_file_write_rep_s;


typedef struct _uvfs_file_read_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    int bytes_read;
    char buff[PAGE_CACHE_SIZE];
} uvfs_file_read_rep_s;


typedef struct _uvfs_create_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    int ino;
    uvfs_inode_info_s fh;
    uvfs_attr_s a;
} uvfs_create_rep_s;


typedef struct _uvfs_lookup_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    int ino;
    uvfs_inode_info_s fh;
    uvfs_attr_s a;
} uvfs_lookup_rep_s;


typedef struct _uvfs_unlink_rep_s
{
    int type;
    int serial;
    int size;
    int error;
} uvfs_unlink_rep_s;


typedef struct _uvfs_symlink_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    int ino;
    uvfs_inode_info_s fh;
    uvfs_attr_s a;
} uvfs_symlink_rep_s;


typedef struct _uvfs_mkdir_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    int ino;
    uvfs_inode_info_s fh;
    uvfs_attr_s a;
} uvfs_mkdir_rep_s;


typedef struct _uvfs_rmdir_rep_s
{
    int type;
    int serial;
    int size;
    int error;
} uvfs_rmdir_rep_s;


typedef struct _uvfs_rename_rep_s
{
    int type;
    int serial;
    int size;
    int error;
} uvfs_rename_rep_s;


typedef struct _uvfs_dirent_s
{
    int length;
    int ino;
    int index;
} uvfs_dirent_s;


typedef struct _uvfs_readdir_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    int count;
    char data[UVFS_BUFFSIZE];
} uvfs_readdir_rep_s;


typedef struct _uvfs_setattr_rep_s
{
    int type;
    int serial;
    int size;
    int error;
} uvfs_setattr_rep_s;


typedef struct _uvfs_getattr_rep_s
{   
    int type;
    int serial;
    int size;
    int error;
    unsigned long fsid;
    int i_ino;
    uvfs_inode_info_s fh;
    unsigned ia_valid;
    unsigned ia_uid;
    unsigned ia_gid;
    unsigned ia_size;
    unsigned ia_atime;
    unsigned ia_mtime;
    unsigned ia_ctime;
    unsigned ia_mode;
} uvfs_getattr_rep_s;


typedef struct _uvfs_statfs_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    long f_type;
    long f_bsize;
    long f_blocks;
    long f_bfree;
    long f_bavail;
    long f_files;
    long f_ffree;
    long f_namelen;
} uvfs_statfs_rep_s;


typedef struct _uvfs_put_super_rep_s
{
    int type;
    int serial;
    int size;
    int error;
} uvfs_put_super_rep_s;


typedef struct _uvfs_read_super_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    unsigned long fsid;
    unsigned long s_blocksize;
    unsigned long s_blocksize_bits;
    unsigned long s_magic;
    unsigned long root_ino;
    uvfs_inode_info_s fh;
    uvfs_attr_s a;
} uvfs_read_super_rep_s;

typedef struct _uvfs_readlink_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    int len;
    char buff[UVFS_MAX_PATHLEN];
} uvfs_readlink_rep_s;


typedef struct _uvfs_delete_inode_rep_s
{
    int type;
    int serial;
    int size;
    int error;
} uvfs_delete_inode_rep_s;


typedef struct _uvfs_shutdown_rep_s
{
    int type;
    int serial;
    int size;
    int error;
} uvfs_shutdown_rep_s;


typedef struct _uvfs_mknod_rep_s
{
    int type;
    int serial;
    int size;
    int error;
    int ino;
    uvfs_inode_info_s fh;
    uvfs_attr_s a;
} uvfs_mknod_rep_s;


typedef struct _uvfs_link_rep_s
{
    int type;
    int serial;
    int size;
    int error;
} uvfs_link_rep_s;    


typedef union _uvfs_reply_u
{
    uvfs_generic_rep_s generic;
    uvfs_file_write_rep_s file_write;
    uvfs_file_read_rep_s file_read;
    uvfs_create_rep_s create;
    uvfs_lookup_rep_s lookup;
    uvfs_unlink_rep_s unlink;
    uvfs_symlink_rep_s symlink;
    uvfs_mkdir_rep_s mkdir;
    uvfs_rmdir_rep_s rmdir;
    uvfs_rename_rep_s rename;
    uvfs_readdir_rep_s readdir;
    uvfs_setattr_rep_s setattr;
    uvfs_getattr_rep_s getattr;
    uvfs_statfs_rep_s statfs;
    uvfs_put_super_rep_s put_super;
    uvfs_read_super_rep_s read_super;
    uvfs_readlink_rep_s readlink;
    uvfs_delete_inode_rep_s delete_inode;
    uvfs_shutdown_rep_s shutdown;
    uvfs_mknod_rep_s mknod;
    uvfs_link_rep_s link;
} uvfs_reply_u;

#endif /* !_UVFS_PROTOCOL_H_ */



