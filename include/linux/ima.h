/*
 * Copyright (C) 2008 IBM Corporation
 * Author: Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */

#ifndef _LINUX_IMA_H
#define _LINUX_IMA_H

#include <linux/fs.h>
struct linux_binprm;

#ifdef CONFIG_IMA

extern int ima_enabled;

extern int __ima_bprm_check(struct linux_binprm *bprm);
extern int __ima_inode_alloc(struct inode *inode);
extern void __ima_inode_free(struct inode *inode);
extern int __ima_file_check(struct file *file, int mask);
extern void __ima_file_free(struct file *file);
extern int __ima_file_mmap(struct file *file, unsigned long prot);
extern void __ima_counts_get(struct file *file);

static inline int ima_bprm_check(struct linux_binprm *bprm)
{
	if (ima_enabled)
		return __ima_bprm_check(bprm);
	return 0;
}

static inline int ima_inode_alloc(struct inode *inode)
{
	if (ima_enabled)
		return __ima_inode_alloc(inode);
	return 0;
}

static inline void ima_inode_free(struct inode *inode)
{
	if (ima_enabled)
		__ima_inode_free(inode);
	return;
}

static inline int ima_file_check(struct file *file, int mask)
{
	if (ima_enabled)
		return __ima_file_check(file, mask);
	return 0;
}

static inline void ima_file_free(struct file *file)
{
	if (ima_enabled)
		__ima_file_free(file);
	return;
}

static inline int ima_file_mmap(struct file *file, unsigned long prot)
{
	if (ima_enabled)
		return __ima_file_mmap(file, prot);
	return 0;
}

static inline void ima_counts_get(struct file *file)
{
	if (ima_enabled)
		return __ima_counts_get(file);
	return;
}

#else
static inline int ima_bprm_check(struct linux_binprm *bprm)
{
	return 0;
}

static inline int ima_inode_alloc(struct inode *inode)
{
	return 0;
}

static inline void ima_inode_free(struct inode *inode)
{
	return;
}

static inline int ima_file_check(struct file *file, int mask)
{
	return 0;
}

static inline void ima_file_free(struct file *file)
{
	return;
}

static inline int ima_file_mmap(struct file *file, unsigned long prot)
{
	return 0;
}

static inline void ima_counts_get(struct file *file)
{
	return;
}

#endif /* CONFIG_IMA_H */
#endif /* _LINUX_IMA_H */
