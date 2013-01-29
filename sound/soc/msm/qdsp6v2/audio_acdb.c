/* Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/msm_ion.h>
#include <linux/mm.h>
#include "audio_acdb.h"


#define MAX_NETWORKS			15
#define MAX_IOCTL_DATA			(MAX_NETWORKS * 2)
#define MAX_COL_SIZE			324

#define ACDB_BLOCK_SIZE			4096
#define NUM_VOCPROC_BLOCKS		(6 * MAX_NETWORKS)
#define ACDB_TOTAL_VOICE_ALLOCATION	(ACDB_BLOCK_SIZE * NUM_VOCPROC_BLOCKS)


struct sidetone_atomic_cal {
	atomic_t	enable;
	atomic_t	gain;
};


struct acdb_data {
	struct mutex		acdb_mutex;

	/* ANC Cal */
	struct acdb_atomic_cal_block	anc_cal;

	/* AudProc Cal */
	atomic_t			asm_topology;
	atomic_t			adm_topology[MAX_AUDPROC_TYPES];
	struct acdb_atomic_cal_block	audproc_cal[MAX_AUDPROC_TYPES];
	struct acdb_atomic_cal_block	audstrm_cal[MAX_AUDPROC_TYPES];
	struct acdb_atomic_cal_block	audvol_cal[MAX_AUDPROC_TYPES];

	/* VocProc Cal */
	atomic_t			voice_rx_topology;
	atomic_t			voice_tx_topology;
	struct acdb_atomic_cal_block	vocproc_cal;
	struct acdb_atomic_cal_block	vocstrm_cal;
	struct acdb_atomic_cal_block	vocvol_cal;

	/* Voice Column data */
	struct acdb_atomic_cal_block	vocproc_col_cal[MAX_VOCPROC_TYPES];
	uint32_t			*col_data[MAX_VOCPROC_TYPES];

	/* VocProc dev cfg cal*/
	struct acdb_atomic_cal_block	vocproc_dev_cal;

	/* AFE cal */
	struct acdb_atomic_cal_block	afe_cal[MAX_AUDPROC_TYPES];

	/* Sidetone Cal */
	struct sidetone_atomic_cal	sidetone_cal;

	/* Allocation information */
	struct ion_client		*ion_client;
	struct ion_handle		*ion_handle;
	atomic_t			map_handle;
	atomic64_t			paddr;
	atomic64_t			kvaddr;
	atomic64_t			mem_len;
};

static struct acdb_data		acdb_data;
static atomic_t usage_count;

uint32_t get_voice_rx_topology(void)
{
	return atomic_read(&acdb_data.voice_rx_topology);
}

void store_voice_rx_topology(uint32_t topology)
{
	atomic_set(&acdb_data.voice_rx_topology, topology);
}

uint32_t get_voice_tx_topology(void)
{
	return atomic_read(&acdb_data.voice_tx_topology);
}

void store_voice_tx_topology(uint32_t topology)
{
	atomic_set(&acdb_data.voice_tx_topology, topology);
}

uint32_t get_adm_rx_topology(void)
{
	return atomic_read(&acdb_data.adm_topology[RX_CAL]);
}

void store_adm_rx_topology(uint32_t topology)
{
	atomic_set(&acdb_data.adm_topology[RX_CAL], topology);
}

uint32_t get_adm_tx_topology(void)
{
	return atomic_read(&acdb_data.adm_topology[TX_CAL]);
}

void store_adm_tx_topology(uint32_t topology)
{
	atomic_set(&acdb_data.adm_topology[TX_CAL], topology);
}

uint32_t get_asm_topology(void)
{
	return atomic_read(&acdb_data.asm_topology);
}

void store_asm_topology(uint32_t topology)
{
	atomic_set(&acdb_data.asm_topology, topology);
}

void get_voice_cal_allocation(struct acdb_cal_block *cal_block)
{
	cal_block->cal_size = ACDB_TOTAL_VOICE_ALLOCATION;
	cal_block->cal_paddr =
		atomic_read(&acdb_data.vocproc_cal.cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.vocproc_cal.cal_kvaddr);
}

void get_anc_cal(struct acdb_cal_block *cal_block)
{
	pr_debug("%s\n", __func__);

	if (cal_block == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}

	cal_block->cal_size =
		atomic_read(&acdb_data.anc_cal.cal_size);
	cal_block->cal_paddr =
		atomic_read(&acdb_data.anc_cal.cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.anc_cal.cal_kvaddr);
done:
	return;
}

void store_anc_cal(struct cal_block *cal_block)
{
	pr_debug("%s,\n", __func__);

	if (cal_block->cal_offset > atomic64_read(&acdb_data.mem_len)) {
		pr_err("%s: offset %d is > mem_len %ld\n",
			__func__, cal_block->cal_offset,
			(long)atomic64_read(&acdb_data.mem_len));
		goto done;
	}

	atomic_set(&acdb_data.anc_cal.cal_size,
		cal_block->cal_size);
	atomic_set(&acdb_data.anc_cal.cal_paddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.paddr));
	atomic_set(&acdb_data.anc_cal.cal_kvaddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.kvaddr));
done:
	return;
}

void store_afe_cal(int32_t path, struct cal_block *cal_block)
{
	pr_debug("%s, path = %d\n", __func__, path);

	if (cal_block->cal_offset > atomic64_read(&acdb_data.mem_len)) {
		pr_err("%s: offset %d is > mem_len %ld\n",
			__func__, cal_block->cal_offset,
			(long)atomic64_read(&acdb_data.mem_len));
		goto done;
	}
	if ((path >= MAX_AUDPROC_TYPES) || (path < 0)) {
		pr_err("ACDB=> Bad path sent to %s, path: %d\n",
			__func__, path);
		goto done;
	}

	atomic_set(&acdb_data.afe_cal[path].cal_size,
		cal_block->cal_size);
	atomic_set(&acdb_data.afe_cal[path].cal_paddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.paddr));
	atomic_set(&acdb_data.afe_cal[path].cal_kvaddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.kvaddr));
done:
	return;
}

void get_afe_cal(int32_t path, struct acdb_cal_block *cal_block)
{
	pr_debug("%s, path = %d\n", __func__, path);

	if (cal_block == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}
	if ((path >= MAX_AUDPROC_TYPES) || (path < 0)) {
		pr_err("ACDB=> Bad path sent to %s, path: %d\n",
			__func__, path);
		goto done;
	}

	cal_block->cal_size =
		atomic_read(&acdb_data.afe_cal[path].cal_size);
	cal_block->cal_paddr =
		atomic_read(&acdb_data.afe_cal[path].cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.afe_cal[path].cal_kvaddr);
done:
	return;
}

void store_audproc_cal(int32_t path, struct cal_block *cal_block)
{
	pr_debug("%s, path = %d\n", __func__, path);

	if (cal_block->cal_offset > atomic64_read(&acdb_data.mem_len)) {
		pr_err("%s: offset %d is > mem_len %ld\n",
			__func__, cal_block->cal_offset,
			(long)atomic64_read(&acdb_data.mem_len));
		goto done;
	}
	if (path >= MAX_AUDPROC_TYPES) {
		pr_err("ACDB=> Bad path sent to %s, path: %d\n",
			__func__, path);
		goto done;
	}

	atomic_set(&acdb_data.audproc_cal[path].cal_size,
		cal_block->cal_size);
	atomic_set(&acdb_data.audproc_cal[path].cal_paddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.paddr));
	atomic_set(&acdb_data.audproc_cal[path].cal_kvaddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.kvaddr));
done:
	return;
}

void get_audproc_cal(int32_t path, struct acdb_cal_block *cal_block)
{
	pr_debug("%s, path = %d\n", __func__, path);

	if (cal_block == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}
	if (path >= MAX_AUDPROC_TYPES) {
		pr_err("ACDB=> Bad path sent to %s, path: %d\n",
			__func__, path);
		goto done;
	}

	cal_block->cal_size =
		atomic_read(&acdb_data.audproc_cal[path].cal_size);
	cal_block->cal_paddr =
		atomic_read(&acdb_data.audproc_cal[path].cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.audproc_cal[path].cal_kvaddr);
done:
	return;
}

void store_audstrm_cal(int32_t path, struct cal_block *cal_block)
{
	pr_debug("%s, path = %d\n", __func__, path);

	if (cal_block->cal_offset > atomic64_read(&acdb_data.mem_len)) {
		pr_err("%s: offset %d is > mem_len %ld\n",
			__func__, cal_block->cal_offset,
			(long)atomic64_read(&acdb_data.mem_len));
		goto done;
	}
	if (path >= MAX_AUDPROC_TYPES) {
		pr_err("ACDB=> Bad path sent to %s, path: %d\n",
			__func__, path);
		goto done;
	}

	atomic_set(&acdb_data.audstrm_cal[path].cal_size,
		cal_block->cal_size);
	atomic_set(&acdb_data.audstrm_cal[path].cal_paddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.paddr));
	atomic_set(&acdb_data.audstrm_cal[path].cal_kvaddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.kvaddr));
done:
	return;
}

void get_audstrm_cal(int32_t path, struct acdb_cal_block *cal_block)
{
	pr_debug("%s, path = %d\n", __func__, path);

	if (cal_block == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}
	if (path >= MAX_AUDPROC_TYPES) {
		pr_err("ACDB=> Bad path sent to %s, path: %d\n",
			__func__, path);
		goto done;
	}

	cal_block->cal_size =
		atomic_read(&acdb_data.audstrm_cal[path].cal_size);
	cal_block->cal_paddr =
		atomic_read(&acdb_data.audstrm_cal[path].cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.audstrm_cal[path].cal_kvaddr);
done:
	return;
}

void store_audvol_cal(int32_t path, struct cal_block *cal_block)
{
	pr_debug("%s, path = %d\n", __func__, path);

	if (cal_block->cal_offset > atomic64_read(&acdb_data.mem_len)) {
		pr_err("%s: offset %d is > mem_len %ld\n",
			__func__, cal_block->cal_offset,
			(long)atomic64_read(&acdb_data.mem_len));
		goto done;
	}
	if (path >= MAX_AUDPROC_TYPES) {
		pr_err("ACDB=> Bad path sent to %s, path: %d\n",
			__func__, path);
		goto done;
	}

	atomic_set(&acdb_data.audvol_cal[path].cal_size,
		cal_block->cal_size);
	atomic_set(&acdb_data.audvol_cal[path].cal_paddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.paddr));
	atomic_set(&acdb_data.audvol_cal[path].cal_kvaddr,
		cal_block->cal_offset + atomic64_read(&acdb_data.kvaddr));
done:
	return;
}

void get_audvol_cal(int32_t path, struct acdb_cal_block *cal_block)
{
	pr_debug("%s, path = %d\n", __func__, path);

	if (cal_block == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}
	if (path >= MAX_AUDPROC_TYPES || path < 0) {
		pr_err("ACDB=> Bad path sent to %s, path: %d\n",
			__func__, path);
		goto done;
	}

	cal_block->cal_size =
		atomic_read(&acdb_data.audvol_cal[path].cal_size);
	cal_block->cal_paddr =
		atomic_read(&acdb_data.audvol_cal[path].cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.audvol_cal[path].cal_kvaddr);
done:
	return;
}

void store_voice_col_data(uint32_t vocproc_type, uint32_t cal_size,
			  uint32_t *cal_block)
{
	if (cal_size > MAX_COL_SIZE) {
		pr_err("%s: col size is to big %d\n", __func__,
				cal_size);
		goto done;
	}
	if (copy_from_user(acdb_data.col_data[vocproc_type],
			(void *)((uint8_t *)cal_block + sizeof(cal_size)),
			cal_size)) {
		pr_err("%s: fail to copy col size %d\n",
			__func__, cal_size);
		goto done;
	}
	atomic_set(&acdb_data.vocproc_col_cal[vocproc_type].cal_size,
		cal_size);
done:
	return;
}

void get_voice_col_data(uint32_t vocproc_type,
			struct acdb_cal_block *cal_block)
{
	if (cal_block == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}

	cal_block->cal_size = atomic_read(&acdb_data.
				vocproc_col_cal[vocproc_type].cal_size);
	cal_block->cal_paddr = atomic_read(&acdb_data.
				vocproc_col_cal[vocproc_type].cal_paddr);
	cal_block->cal_kvaddr = atomic_read(&acdb_data.
				vocproc_col_cal[vocproc_type].cal_kvaddr);
done:
	return;
}

void store_vocproc_dev_cfg_cal(struct cal_block *cal_block)
{
	pr_debug("%s\n", __func__);


	if (cal_block->cal_offset >
				atomic64_read(&acdb_data.mem_len)) {
		pr_err("%s: offset %d is > mem_len %ld\n",
			__func__, cal_block->cal_offset,
			(long)atomic64_read(&acdb_data.mem_len));
		atomic_set(&acdb_data.vocproc_dev_cal.cal_size, 0);
		goto done;
	}

	atomic_set(&acdb_data.vocproc_dev_cal.cal_size,
		cal_block->cal_size);
	atomic_set(&acdb_data.vocproc_dev_cal.cal_paddr,
		cal_block->cal_offset +
	atomic64_read(&acdb_data.paddr));
			atomic_set(&acdb_data.vocproc_dev_cal.cal_kvaddr,
			cal_block->cal_offset +
			atomic64_read(&acdb_data.kvaddr));

done:
	return;
}

void get_vocproc_dev_cfg_cal(struct acdb_cal_block *cal_block)
{
	pr_debug("%s\n", __func__);

	cal_block->cal_size =
		atomic_read(&acdb_data.vocproc_dev_cal.cal_size);
	cal_block->cal_paddr =
		atomic_read(&acdb_data.vocproc_dev_cal.cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.vocproc_dev_cal.cal_kvaddr);
}



void store_vocproc_cal(struct cal_block *cal_block)
{
	pr_debug("%s\n", __func__);

	if (cal_block->cal_offset >
				atomic64_read(&acdb_data.mem_len)) {
		pr_err("%s: offset %d is > mem_len %ld\n",
			__func__, cal_block->cal_offset,
			(long)atomic64_read(&acdb_data.mem_len));
		atomic_set(&acdb_data.vocproc_cal.cal_size, 0);
		goto done;
	}

	atomic_set(&acdb_data.vocproc_cal.cal_size,
		cal_block->cal_size);
	atomic_set(&acdb_data.vocproc_cal.cal_paddr,
		cal_block->cal_offset +
		atomic64_read(&acdb_data.paddr));
	atomic_set(&acdb_data.vocproc_cal.cal_kvaddr,
		cal_block->cal_offset +
		atomic64_read(&acdb_data.kvaddr));

done:
	return;
}

void get_vocproc_cal(struct acdb_cal_block *cal_block)
{
	pr_debug("%s\n", __func__);

	if (cal_block == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}

	cal_block->cal_size =
		atomic_read(&acdb_data.vocproc_cal.cal_size);
	cal_block->cal_paddr =
		atomic_read(&acdb_data.vocproc_cal.cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.vocproc_cal.cal_kvaddr);
done:
	return;
}

void store_vocstrm_cal(struct cal_block *cal_block)
{
	pr_debug("%s\n", __func__);

	if (cal_block->cal_offset >
			atomic64_read(&acdb_data.mem_len)) {
		pr_err("%s: offset %d is > mem_len %ld\n",
			__func__, cal_block->cal_offset,
			(long)atomic64_read(&acdb_data.mem_len));
		atomic_set(&acdb_data.vocstrm_cal.cal_size, 0);
		goto done;
	}

	atomic_set(&acdb_data.vocstrm_cal.cal_size,
		cal_block->cal_size);
	atomic_set(&acdb_data.vocstrm_cal.cal_paddr,
		cal_block->cal_offset +
		atomic64_read(&acdb_data.paddr));
	atomic_set(&acdb_data.vocstrm_cal.cal_kvaddr,
		cal_block->cal_offset +
		atomic64_read(&acdb_data.kvaddr));

done:
	return;
}

void get_vocstrm_cal(struct acdb_cal_block *cal_block)
{
	pr_debug("%s\n", __func__);

	if (cal_block == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}

	cal_block->cal_size =
		atomic_read(&acdb_data.vocstrm_cal.cal_size);
	cal_block->cal_paddr =
		atomic_read(&acdb_data.vocstrm_cal.cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.vocstrm_cal.cal_kvaddr);
done:
	return;
}

void store_vocvol_cal(struct cal_block *cal_block)
{
	pr_debug("%s\n", __func__);

	if (cal_block->cal_offset >
			atomic64_read(&acdb_data.mem_len)) {
		pr_err("%s: offset %d is > mem_len %ld\n",
			__func__, cal_block->cal_offset,
			(long)atomic64_read(&acdb_data.mem_len));
		atomic_set(&acdb_data.vocvol_cal.cal_size, 0);
		goto done;
	}

	atomic_set(&acdb_data.vocvol_cal.cal_size,
		cal_block->cal_size);
	atomic_set(&acdb_data.vocvol_cal.cal_paddr,
		cal_block->cal_offset +
		atomic64_read(&acdb_data.paddr));
	atomic_set(&acdb_data.vocvol_cal.cal_kvaddr,
		cal_block->cal_offset +
		atomic64_read(&acdb_data.kvaddr));

done:
	return;
}

void get_vocvol_cal(struct acdb_cal_block *cal_block)
{
	pr_debug("%s\n", __func__);

	if (cal_block == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}

	cal_block->cal_size =
		atomic_read(&acdb_data.vocvol_cal.cal_size);
	cal_block->cal_paddr =
		atomic_read(&acdb_data.vocvol_cal.cal_paddr);
	cal_block->cal_kvaddr =
		atomic_read(&acdb_data.vocvol_cal.cal_kvaddr);
done:
	return;
}

void store_sidetone_cal(struct sidetone_cal *cal_data)
{
	pr_debug("%s\n", __func__);

	atomic_set(&acdb_data.sidetone_cal.enable, cal_data->enable);
	atomic_set(&acdb_data.sidetone_cal.gain, cal_data->gain);
}


void get_sidetone_cal(struct sidetone_cal *cal_data)
{
	pr_debug("%s\n", __func__);

	if (cal_data == NULL) {
		pr_err("ACDB=> NULL pointer sent to %s\n", __func__);
		goto done;
	}

	cal_data->enable = atomic_read(&acdb_data.sidetone_cal.enable);
	cal_data->gain = atomic_read(&acdb_data.sidetone_cal.gain);
done:
	return;
}

static int acdb_open(struct inode *inode, struct file *f)
{
	s32 result = 0;
	pr_debug("%s\n", __func__);

	if (atomic64_read(&acdb_data.mem_len)) {
		pr_debug("%s: ACDB opened but memory allocated, using existing allocation!\n",
			__func__);
	}

	atomic_inc(&usage_count);
	return result;
}

static int deregister_memory(void)
{
	int i;

	if (atomic64_read(&acdb_data.mem_len)) {
		mutex_lock(&acdb_data.acdb_mutex);
		atomic64_set(&acdb_data.mem_len, 0);

		for (i = 0; i < MAX_VOCPROC_TYPES; i++) {
			kfree(acdb_data.col_data[i]);
			acdb_data.col_data[i] = NULL;
		}
		ion_unmap_kernel(acdb_data.ion_client, acdb_data.ion_handle);
		ion_free(acdb_data.ion_client, acdb_data.ion_handle);
		ion_client_destroy(acdb_data.ion_client);
		mutex_unlock(&acdb_data.acdb_mutex);
	}
	return 0;
}

static int register_memory(void)
{
	int			result;
	int			i;
	unsigned long		paddr;
	void                    *kvptr;
	unsigned long		kvaddr;
	unsigned long		mem_len;

	mutex_lock(&acdb_data.acdb_mutex);
	for (i = 0; i < MAX_VOCPROC_TYPES; i++) {
		acdb_data.col_data[i] = kmalloc(MAX_COL_SIZE, GFP_KERNEL);
		atomic_set(&acdb_data.vocproc_col_cal[i].cal_kvaddr,
			(uint32_t)acdb_data.col_data[i]);
	}

	acdb_data.ion_client =
		msm_ion_client_create(UINT_MAX, "audio_acdb_client");
	if (IS_ERR_OR_NULL(acdb_data.ion_client)) {
		pr_err("%s: Could not register ION client!!!\n", __func__);
		result = PTR_ERR(acdb_data.ion_client);
		goto err;
	}

	acdb_data.ion_handle = ion_import_dma_buf(acdb_data.ion_client,
		atomic_read(&acdb_data.map_handle));
	if (IS_ERR_OR_NULL(acdb_data.ion_handle)) {
		pr_err("%s: Could not import map handle!!!\n", __func__);
		result = PTR_ERR(acdb_data.ion_handle);
		goto err_ion_client;
	}

	result = ion_phys(acdb_data.ion_client, acdb_data.ion_handle,
				&paddr, (size_t *)&mem_len);
	if (result != 0) {
		pr_err("%s: Could not get phys addr!!!\n", __func__);
		goto err_ion_handle;
	}

	kvptr = ion_map_kernel(acdb_data.ion_client,
		acdb_data.ion_handle);
	if (IS_ERR_OR_NULL(kvptr)) {
		pr_err("%s: Could not get kernel virt addr!!!\n", __func__);
		result = PTR_ERR(kvptr);
		goto err_ion_handle;
	}
	kvaddr = (unsigned long)kvptr;
	atomic64_set(&acdb_data.paddr, paddr);
	atomic64_set(&acdb_data.kvaddr, kvaddr);
	atomic64_set(&acdb_data.mem_len, mem_len);
	mutex_unlock(&acdb_data.acdb_mutex);

	pr_debug("%s done! paddr = 0x%lx, kvaddr = 0x%lx, len = x%lx\n",
		 __func__,
		(long)atomic64_read(&acdb_data.paddr),
		(long)atomic64_read(&acdb_data.kvaddr),
		(long)atomic64_read(&acdb_data.mem_len));

	return result;
err_ion_handle:
	ion_free(acdb_data.ion_client, acdb_data.ion_handle);
err_ion_client:
	ion_client_destroy(acdb_data.ion_client);
err:
	atomic64_set(&acdb_data.mem_len, 0);
	mutex_unlock(&acdb_data.acdb_mutex);
	return result;
}
static long acdb_ioctl(struct file *f,
		unsigned int cmd, unsigned long arg)
{
	int32_t			result = 0;
	int32_t			size;
	int32_t			map_fd;
	uint32_t		topology;
	uint32_t		data[MAX_IOCTL_DATA];
	pr_debug("%s\n", __func__);

	switch (cmd) {
	case AUDIO_REGISTER_PMEM:
		pr_debug("AUDIO_REGISTER_PMEM\n");
		if (atomic_read(&acdb_data.mem_len)) {
			deregister_memory();
			pr_debug("Remove the existing memory\n");
		}

		if (copy_from_user(&map_fd, (void *)arg, sizeof(map_fd))) {
			pr_err("%s: fail to copy memory handle!\n", __func__);
			result = -EFAULT;
		} else {
			atomic_set(&acdb_data.map_handle, map_fd);
			result = register_memory();
		}
		goto done;

	case AUDIO_DEREGISTER_PMEM:
		pr_debug("AUDIO_DEREGISTER_PMEM\n");
		deregister_memory();
		goto done;
	case AUDIO_SET_VOICE_RX_TOPOLOGY:
		if (copy_from_user(&topology, (void *)arg,
				sizeof(topology))) {
			pr_err("%s: fail to copy topology!\n", __func__);
			result = -EFAULT;
		}
		store_voice_rx_topology(topology);
		goto done;
	case AUDIO_SET_VOICE_TX_TOPOLOGY:
		if (copy_from_user(&topology, (void *)arg,
				sizeof(topology))) {
			pr_err("%s: fail to copy topology!\n", __func__);
			result = -EFAULT;
		}
		store_voice_tx_topology(topology);
		goto done;
	case AUDIO_SET_ADM_RX_TOPOLOGY:
		if (copy_from_user(&topology, (void *)arg,
				sizeof(topology))) {
			pr_err("%s: fail to copy topology!\n", __func__);
			result = -EFAULT;
		}
		store_adm_rx_topology(topology);
		goto done;
	case AUDIO_SET_ADM_TX_TOPOLOGY:
		if (copy_from_user(&topology, (void *)arg,
				sizeof(topology))) {
			pr_err("%s: fail to copy topology!\n", __func__);
			result = -EFAULT;
		}
		store_adm_tx_topology(topology);
		goto done;
	case AUDIO_SET_ASM_TOPOLOGY:
		if (copy_from_user(&topology, (void *)arg,
				sizeof(topology))) {
			pr_err("%s: fail to copy topology!\n", __func__);
			result = -EFAULT;
		}
		store_asm_topology(topology);
		goto done;
	}

	if (copy_from_user(&size, (void *) arg, sizeof(size))) {

		result = -EFAULT;
		goto done;
	}

	if (size <= 0) {
		pr_err("%s: Invalid size sent to driver: %d\n",
			__func__, size);
		result = -EFAULT;
		goto done;
	}

	switch (cmd) {
	case AUDIO_SET_VOCPROC_COL_CAL:
		store_voice_col_data(VOCPROC_CAL, size, (uint32_t *)arg);
		goto done;
	case AUDIO_SET_VOCSTRM_COL_CAL:
		store_voice_col_data(VOCSTRM_CAL, size, (uint32_t *)arg);
		goto done;
	case AUDIO_SET_VOCVOL_COL_CAL:
		store_voice_col_data(VOCVOL_CAL, size, (uint32_t *)arg);
		goto done;
	}

	if (copy_from_user(data, (void *)(arg + sizeof(size)), size)) {

		pr_err("%s: fail to copy table size %d\n", __func__, size);
		result = -EFAULT;
		goto done;
	}

	if (data == NULL) {
		pr_err("%s: NULL pointer sent to driver!\n", __func__);
		result = -EFAULT;
		goto done;
	}

	if (size > sizeof(struct cal_block))
		pr_err("%s: More cal data for ioctl 0x%x then expected, size received: %d\n",
			__func__, cmd, size);

	switch (cmd) {
	case AUDIO_SET_AUDPROC_TX_CAL:
		store_audproc_cal(TX_CAL, (struct cal_block *)data);
		goto done;
	case AUDIO_SET_AUDPROC_RX_CAL:
		store_audproc_cal(RX_CAL, (struct cal_block *)data);
		goto done;
	case AUDIO_SET_AUDPROC_TX_STREAM_CAL:
		store_audstrm_cal(TX_CAL, (struct cal_block *)data);
		goto done;
	case AUDIO_SET_AUDPROC_RX_STREAM_CAL:
		store_audstrm_cal(RX_CAL, (struct cal_block *)data);
		goto done;
	case AUDIO_SET_AUDPROC_TX_VOL_CAL:
		store_audvol_cal(TX_CAL, (struct cal_block *)data);
		goto done;
	case AUDIO_SET_AUDPROC_RX_VOL_CAL:
		store_audvol_cal(RX_CAL, (struct cal_block *)data);
		goto done;
	case AUDIO_SET_AFE_TX_CAL:
		store_afe_cal(TX_CAL, (struct cal_block *)data);
		goto done;
	case AUDIO_SET_AFE_RX_CAL:
		store_afe_cal(RX_CAL, (struct cal_block *)data);
		goto done;
	case AUDIO_SET_VOCPROC_CAL:
		store_vocproc_cal((struct cal_block *)data);
		goto done;
	case AUDIO_SET_VOCPROC_STREAM_CAL:
		store_vocstrm_cal((struct cal_block *)data);
		goto done;
	case AUDIO_SET_VOCPROC_VOL_CAL:
		store_vocvol_cal((struct cal_block *)data);
		goto done;
	case AUDIO_SET_VOCPROC_DEV_CFG_CAL:
		store_vocproc_dev_cfg_cal((struct cal_block *)data);
		goto done;
	case AUDIO_SET_SIDETONE_CAL:
		store_sidetone_cal((struct sidetone_cal *)data);
		goto done;
	case AUDIO_SET_ANC_CAL:
		store_anc_cal((struct cal_block *)data);
		goto done;
	default:
		pr_err("ACDB=> ACDB ioctl not found!\n");
	}

done:
	return result;
}

static int acdb_mmap(struct file *file, struct vm_area_struct *vma)
{
	int result = 0;
	int size = vma->vm_end - vma->vm_start;

	pr_debug("%s\n", __func__);

	if (atomic64_read(&acdb_data.mem_len)) {
		if (size <= atomic64_read(&acdb_data.mem_len)) {
			vma->vm_page_prot = pgprot_noncached(
						vma->vm_page_prot);
			result = remap_pfn_range(vma,
				vma->vm_start,
				atomic64_read(&acdb_data.paddr) >> PAGE_SHIFT,
				size,
				vma->vm_page_prot);
		} else {
			pr_err("%s: Not enough memory!\n", __func__);
			result = -ENOMEM;
		}
	} else {
		pr_err("%s: memory is not allocated, yet!\n", __func__);
		result = -ENODEV;
	}

	return result;
}

static int acdb_release(struct inode *inode, struct file *f)
{
	s32 result = 0;

	atomic_dec(&usage_count);
	atomic_read(&usage_count);

	pr_debug("%s: ref count %d!\n", __func__,
		atomic_read(&usage_count));

	if (atomic_read(&usage_count) >= 1)
		result = -EBUSY;
	else
		result = deregister_memory();

	return result;
}

static const struct file_operations acdb_fops = {
	.owner = THIS_MODULE,
	.open = acdb_open,
	.release = acdb_release,
	.unlocked_ioctl = acdb_ioctl,
	.mmap = acdb_mmap,
};

struct miscdevice acdb_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "msm_acdb",
	.fops	= &acdb_fops,
};

static int __init acdb_init(void)
{
	memset(&acdb_data, 0, sizeof(acdb_data));
	mutex_init(&acdb_data.acdb_mutex);
	atomic_set(&usage_count, 0);

	return misc_register(&acdb_misc);
}

static void __exit acdb_exit(void)
{
}

module_init(acdb_init);
module_exit(acdb_exit);

MODULE_DESCRIPTION("SoC QDSP6v2 Audio ACDB driver");
MODULE_LICENSE("GPL v2");
