/*
 *   linux/fs/uvfs/init.c
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
/* init.c -- Modules initialization. */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include "driver.h"
#include "operations.h"
#include "protocol.h"

static struct proc_dir_entry* uvfs_proc_file;

MODULE_LICENSE("GPL");

int uvfs_init(void)
{
    int result;
    spin_lock_init(&Uvfs_lock);
    init_waitqueue_head(&Uvfs_driver_queue);

    dprintk("<1>uvfs_init(/proc/fs/%s)\n", UVFS_FS_NAME);

    uvfs_proc_file = 
        create_proc_entry(UVFS_FS_NAME, S_IFREG | 0600, proc_root_fs);
    if (uvfs_proc_file == NULL)
    {
        printk("<1>Could not create /proc/fs/%s\n", UVFS_FS_NAME);
        return -EIO;
    }
    uvfs_proc_file->proc_fops = &Uvfsd_file_operations;
    result = register_filesystem(&Uvfs_file_system_type);
    if (result < 0)
    {
        remove_proc_entry(UVFS_FS_NAME, proc_root_fs);
        return result;
    }
    return 0;
}


void uvfs_cleanup(void)
{
    dprintk("<1>uvfs_cleanup(/proc/fs/%s)\n", UVFS_FS_NAME);
    unregister_filesystem(&Uvfs_file_system_type);
    remove_proc_entry(UVFS_FS_NAME, proc_root_fs);
}


module_init(uvfs_init);
module_exit(uvfs_cleanup);



