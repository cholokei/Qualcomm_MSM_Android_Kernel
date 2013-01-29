/* include/linux/android_pmem.h
 *
 * Copyright (C) 2007 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _ANDROID_PMEM_H_
#define _ANDROID_PMEM_H_

#include <uapi/linux/android_pmem.h>

int get_pmem_file(unsigned int fd, unsigned long *start, unsigned long *vstart,
		  unsigned long *end, struct file **filp);
int get_pmem_fd(int fd, unsigned long *start, unsigned long *end);
int get_pmem_user_addr(struct file *file, unsigned long *start,
		       unsigned long *end);
void put_pmem_file(struct file* file);
void put_pmem_fd(int fd);
void flush_pmem_fd(int fd, unsigned long start, unsigned long len);
void flush_pmem_file(struct file *file, unsigned long start, unsigned long len);
int pmem_cache_maint(struct file *file, unsigned int cmd,
		struct pmem_addr *pmem_addr);

enum pmem_allocator_type {
	/* Zero is a default in platform PMEM structures in the board files,
	 * when the "allocator_type" structure element is not explicitly
	 * defined
	 */
	PMEM_ALLOCATORTYPE_BITMAP = 0, /* forced to be zero here */
	PMEM_ALLOCATORTYPE_SYSTEM,

	PMEM_ALLOCATORTYPE_ALLORNOTHING,
	PMEM_ALLOCATORTYPE_BUDDYBESTFIT,

	PMEM_ALLOCATORTYPE_MAX,
};

/* kernel api names for board specific data structures */
#define PMEM_KERNEL_EBI1_DATA_NAME "pmem_kernel_ebi1"
#define PMEM_KERNEL_SMI_DATA_NAME "pmem_kernel_smi"

struct android_pmem_platform_data
{
	const char* name;
	/* size of memory region */
	unsigned long size;

	enum pmem_allocator_type allocator_type;
	/* treated as a 'hidden' variable in the board files. Can be
	 * set, but default is the system init value of 0 which becomes a
	 * quantum of 4K pages.
	 */
	unsigned int quantum;

	/* set to indicate maps of this region should be cached, if a mix of
	 * cached and uncached is desired, set this and open the device with
	 * O_SYNC to get an uncached region */
	unsigned cached;
	/* The MSM7k has bits to enable a write buffer in the bus controller*/
	unsigned buffered;
	/* which memory type (i.e. SMI, EBI1) this PMEM device is backed by */
	unsigned memory_type;
	/*
	 * function to be called when the number of allocations goes from
	 * 0 -> 1
	 */
	int (*request_region)(void *);
	/*
	 * function to be called when the number of allocations goes from
	 * 1 -> 0
	 */
	int (*release_region)(void *);
	/*
	 * function to be called upon pmem registration
	 */
	void *(*setup_region)(void);
	/*
	 * indicates that this region should be mapped/unmaped as needed
	 */
	int map_on_demand;
	/*
	 * indicates this pmem may be reused via fmem
	 */
	int reusable;
};

int pmem_setup(struct android_pmem_platform_data *pdata,
	       long (*ioctl)(struct file *, unsigned int, unsigned long),
	       int (*release)(struct inode *, struct file *));

int pmem_remap(struct pmem_region *region, struct file *file,
	       unsigned operation);

#endif //_ANDROID_PPP_H_

