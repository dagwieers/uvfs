/*
 *   linux/fs/uvfs/dentry.c
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
/* dentry.c -- Implements dentry operations */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/locks.h>
#include <linux/smp_lock.h>
#include "dentry.h"
#include "driver.h"
#include "file.h"
#include "dir.h"
#include "protocol.h"

/*
 * This is called every time the dcache has a lookup hit,
 * and we should check whether we can really trust that lookup.
 *
 * NOTE! The hit can be a negative hit too, don't assume
 * we have an inode!
 *
 * RETURNS:
 *    zero : needs revalidation
 * nonzero : dosn't need revalidation
 *
 */
int uvfs_dentry_revalidate(struct dentry * dentry)
{
	struct inode *inode;

	dprintk("<1>Entered uvfs_dentry_revalidate: %s/%s inode\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	lock_kernel();
	inode = dentry->d_inode;

	/* check for null inode */
	if (!inode)
	{
		dprintk("<1>uvfs_dentry_revalidate: inode null\n");

		/* check if root inode */
		if (IS_ROOT(dentry))
		{
			dprintk("<1>Exited uvfs_dentry_revalidate IS_ROOT 1\n");
			goto out_bad;
		}

		goto out_bad;
	}

	/* check if bad inode */
	if (is_bad_inode(inode))
	{
		dprintk("<1>Exited uvfs_dentry_revalidate is_bad_inode\n");
		goto out_bad;
	}

	/* check if root inode */
	if (IS_ROOT(dentry))
	{
		dprintk("<1>uvfs_dentry_revalidate: uvfs_check_verifier IS_ROOT 2 OK\n");
		goto out_valid;
	}

 out_bad:
	dprintk("<1>uvfs_dentry_revalidate: out_bad check inode type\n");
	if (inode)
	{
		if (S_ISDIR(inode->i_mode))
		{
			dprintk("<1>uvfs_dentry_revalidate: is ISDIR purge ");
			dprintk(": %s/%s inode 0x%x i_ino %d\n",
				dentry->d_parent->d_name.name,
				dentry->d_name.name, inode, inode->i_ino);

			/* Purge readdir caches. */
			dprintk("<1>uvfs_dentry_revalidate: invalidate_inode_pages\n");
			invalidate_inode_pages(inode);

			/* If we have submounts, don't unhash ! */
			dprintk("<1>uvfs_dentry_revalidate: have_submounts\n");
			if (have_submounts(dentry))
				goto out_valid;

			dprintk(
			"<1>uvfs_dentry_revalidate: shrink_dcache_parent\n");
			shrink_dcache_parent(dentry);

			/* check that we are not root inode */
			if (inode != inode->i_sb->s_root->d_inode)
			{
				/* must be an invalid inode remove it */
				dprintk("<1>uvfs_dentry_revalidate: non-root inode remove_inode_hash\n");
				remove_inode_hash(inode);
			}
		}
		else if(S_ISREG(inode->i_mode))
		{
			dprintk("<1>uvfs_dentry_revalidate: is ISREG ");
			dprintk(": %s/%s inode 0x%x\n",
				dentry->d_parent->d_name.name,
				dentry->d_name.name, inode);

			/* Purge readdir caches. */
			dprintk("<1>uvfs_dentry_revalidate: invalidate_inode_pages\n");
			invalidate_inode_pages(inode);

			/* check that we are not root inode */
			if (inode != inode->i_sb->s_root->d_inode)
			{
				/* must be an invalid inode remove it */
				dprintk("<1>uvfs_dentry_revalidate: non-root inode remove_inode_hash\n");
				remove_inode_hash(inode);
			}
		}
		else if(S_ISLNK(inode->i_mode))
		{
			dprintk("<1>uvfs_dentry_revalidate: is ISLNK ");
			dprintk(": %s/%s inode 0x%x\n",
				dentry->d_parent->d_name.name,
				dentry->d_name.name, inode);

			/* Purge readdir caches. */
			dprintk("<1>uvfs_dentry_revalidate: invalidate_inode_pages\n");
			invalidate_inode_pages(inode);

			/* check that we are not root inode */
			if (inode != inode->i_sb->s_root->d_inode)
			{
				/* must be an invalid inode remove it */
				dprintk("<1>uvfs_dentry_revalidate: non-root inode remove_inode_hash\n");
				remove_inode_hash(inode);
			}
		}
	}

	dprintk("<1>uvfs_dentry_revalidate: d_drop(%s)\n", dentry->d_name.name);
	d_drop(dentry);

	unlock_kernel();

	dprintk("<1>Exited uvfs_dentry_revalidate: bad\n");
	return 0;

 out_valid:
	unlock_kernel();

	dprintk("<1>Exited uvfs_dentry_revalidate: valid\n");
	return 1;
}

#ifdef INODE_DENTRY_VALIDATION
#define get_dentry(ptr, type) \
        ((type *)((char *)(ptr + sizeof(list_t))))

struct dentry * uvfs_inode_to_dentry(struct inode *inode)
{
	struct list_head *head, *next, *tmp;
	struct dentry *dentry;
	struct inode *tmp_inode;

	spin_lock(&dcache_lock);
	head = &inode->i_dentry;
	next = inode->i_dentry.next;
	while (next != head)
	{
		tmp = next;
		next = tmp->next;
		tmp_inode = list_entry(tmp, struct dentry, d_inode);
		/* see if this is the dentry for my inode */
		dprintk("uvfs_inode_parent_dentry: look for dentry\n");
		if (inode == tmp_inode)
		{
			/* get inode's dentry */
			dprintk("uvfs_inode_to_dentry: got dentry\n");
			dentry = get_dentry(tmp, struct dentry);
			spin_unlock(&dcache_lock);
			return dentry;
		}
	}
	spin_unlock(&dcache_lock);
	return NULL;
}


struct dentry * uvfs_inode_parent_dentry(struct inode *inode)
{
	struct list_head *head, *next, *tmp;
	struct dentry *dentry;
	struct inode *tmp_inode;

	spin_lock(&dcache_lock);
	head = &inode->i_dentry;
	next = inode->i_dentry.next;
	while (next != head)
	{
		tmp = next;
		next = tmp->next;
		tmp_inode = list_entry(tmp, struct dentry, d_inode);
		dprintk("uvfs_inode_parent_dentry: look for parent dentry\n");
		/* see if this is the dentry for my inode */
		if (inode == tmp_inode)
		{
			/* get inode's parent dentry */
			dprintk("uvfs_inode_parent_dentry: got parent dentry\n");
			dentry = list_entry(tmp, struct dentry, d_parent);
			spin_unlock(&dcache_lock);
			return dentry;
		}
	}
	spin_unlock(&dcache_lock);
	return NULL;
}


int uvfs_inode_revalidate(struct inode * inode)
{
	struct dentry *dentry = (struct dentry *)NULL;
	struct dentry *p_dentry = (struct dentry *)NULL;

	dprintk("<1>Entered uvfs_inode_revalidate: inode\n");

	lock_kernel();

	/* check if null or bad inode */
	if (!inode || is_bad_inode(inode))
	{
		printk("<1>uvfs_inode_revalidate: null or bad inode\n");
		goto out_valid;
	}

	/* check if root inode */
	if (inode == inode->i_sb->s_root->d_inode)
	{
		dprintk("<1>uvfs_inode_revalidate: root inode\n");
		goto out_valid;
	}

	if (S_ISDIR(inode->i_mode))
	{
		dprintk("<1>uvfs_inode_revalidate: is ISDIR purge ");
		dprintk(": inode 0x%x i_ino %d\n", inode, inode->i_ino);
	}
	else if(S_ISREG(inode->i_mode))
	{
		dprintk("<1>uvfs_inode_revalidate: is ISREG ");
		dprintk(": inode 0x%x\n", inode);
	}
	else if(S_ISLNK(inode->i_mode))
	{
		dprintk("<1>uvfs_inode_revalidate: is ISLNK ");
		dprintk(": inode 0x%x\n", inode);
	}

	/* get inode's dentry and parent dentry */
	dentry = uvfs_inode_to_dentry(inode);
	p_dentry = uvfs_inode_parent_dentry(inode);
	if(dentry)
	{
		/* Purge readdir caches. */
		dprintk("<1>uvfs_inode_revalidate: invalidate_inode_pages\n");
		invalidate_inode_pages(inode);

		/* must be an invalid inode remove it */
		dprintk("<1>uvfs_inode_revalidate: remove_inode_hash\n");
		remove_inode_hash(inode);

		dprintk("<1>uvfs_inode_revalidate: d_drop(%s)\n", dentry->d_name.name);
		d_drop(dentry);
	}

	if(p_dentry)
	{
		/* Purge readdir caches. */
		dprintk("<1>uvfs_inode_revalidate: invalidate_inode_pages\n");
		invalidate_inode_pages(p_dentry->d_inode);

		/* must be an invalid inode remove it */
		dprintk("<1>uvfs_inode_revalidate: remove_inode_hash\n");
		remove_inode_hash(p_dentry->d_inode);

		/* If we have submounts, don't unhash ! */
		dprintk("<1>uvfs_inode_revalidate: have_submounts\n");
		if (have_submounts(p_dentry))
			goto out_valid;

		dprintk("<1>uvfs_inode_revalidate: shrink_dcache_parent\n");
		shrink_dcache_parent(p_dentry);

		dprintk("<1>uvfs_inode_revalidate: d_drop(%s)\n",p_dentry->d_name.name);
		d_drop(p_dentry);
	}

	unlock_kernel();

	dprintk("<1>Exited uvfs_inode_revalidate: bad\n");
	return 0;

 out_valid:
	unlock_kernel();

	dprintk("<1>Exited uvfs_inode_revalidate: valid\n");
	return 1;
}
#endif /* INODE_DENTRY_VALIDATION */
