/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/list.h>

#ifndef MSM_IOMMU_PERFMON_H
#define MSM_IOMMU_PERFMON_H


/**
 * struct iommu_access_ops - Callbacks for accessing IOMMU
 * @iommu_power_on:     Turn on clocks/power to unit
 * @iommu_power_off:    Turn off clocks/power to unit
 * @iommu_lock_acquire: Acquire any locks needed
 * @iommu_lock_release: Release locks needed
 */
struct iommu_access_ops {
	int (*iommu_power_on)(void *);
	int (*iommu_power_off)(void *);
	void (*iommu_lock_acquire)(void);
	void (*iommu_lock_release)(void);
};

/**
 * struct iommu_pmon_counter - container for a performance counter.
 * @counter_no:          counter number within the group
 * @absolute_counter_no: counter number within IOMMU PMU
 * @value:               cached counter value
 * @overflow_count:      no of times counter has overflowed
 * @enabled:             indicates whether counter is enabled or not
 * @current_event_class: current selected event class, -1 if none
 * @counter_dir:         debugfs directory for this counter
 * @cnt_group:           group this counter belongs to
 */
struct iommu_pmon_counter {
	unsigned int counter_no;
	unsigned int absolute_counter_no;
	unsigned long value;
	unsigned long overflow_count;
	unsigned int enabled;
	int current_event_class;
	struct dentry *counter_dir;
	struct iommu_pmon_cnt_group *cnt_group;
};

/**
 * struct iommu_pmon_cnt_group - container for a perf mon counter group.
 * @grp_no:       group number
 * @num_counters: number of counters in this group
 * @counters:     list of counter in this group
 * @group_dir:    debugfs directory for this group
 * @pmon:         pointer to the iommu_pmon object this group belongs to
 */
struct iommu_pmon_cnt_group {
	unsigned int grp_no;
	unsigned int num_counters;
	struct iommu_pmon_counter *counters;
	struct dentry *group_dir;
	struct iommu_pmon *pmon;
};

/**
 * struct iommu_info - container for a perf mon iommu info.
 * @iommu_name: name of the iommu from device tree
 * @base:       virtual base address for this iommu
 * @evt_irq:    irq number for event overflow interrupt
 * @iommu_dev:  pointer to iommu device
 * @ops:        iommu access operations pointer.
 */
struct iommu_info {
	const char *iommu_name;
	void *base;
	int evt_irq;
	struct device *iommu_dev;
	struct iommu_access_ops *ops;
};

/**
 * struct iommu_pmon - main container for a perf mon data.
 * @iommu_dir:            debugfs directory for this iommu
 * @iommu:                iommu_info instance
 * @iommu_list:           iommu_list head
 * @cnt_grp:              list of counter groups
 * @num_groups:           number of counter groups
 * @event_cls_supp_value: event classes supported for this PMU
 * @enabled:              Indicates whether perf. mon is enabled or not
 * @iommu_attached        Indicates whether iommu is attached or not.
 * @lock:                 mutex used to synchronize access to shared data
 */
struct iommu_pmon {
	struct dentry *iommu_dir;
	struct iommu_info iommu;
	struct list_head iommu_list;
	struct iommu_pmon_cnt_group *cnt_grp;
	unsigned int num_groups;
	unsigned int event_cls_supp_value;
	unsigned int enabled;
	unsigned int iommu_attach_count;
	struct mutex lock;
};

extern struct iommu_access_ops iommu_access_ops;

#ifdef CONFIG_MSM_IOMMU_PMON
/**
 * Allocate memory for performance monitor structure. Must
 * be called befre iommu_pm_iommu_register
 */
struct iommu_info *msm_iommu_pm_alloc(struct device *iommu_dev);

/**
 * Free memory previously allocated with iommu_pm_alloc
 */
void msm_iommu_pm_free(struct device *iommu_dev);

/**
 * Register iommu with the performance monitor module.
 */
int msm_iommu_pm_iommu_register(struct iommu_info *info);

/**
 * Unregister iommu with the performance monitor module.
 */
void msm_iommu_pm_iommu_unregister(struct device *dev);

/**
 * Called by iommu driver when attaching is complete
 * Must NOT be called with IOMMU mutexes held.
 * @param iommu_dev IOMMU device that is attached
  */
void msm_iommu_attached(struct device *dev);

/**
 * Called by iommu driver before detaching.
 * Must NOT be called with IOMMU mutexes held.
 * @param iommu_dev IOMMU device that is going to be detached
  */
void msm_iommu_detached(struct device *dev);
#else
static inline struct iommu_info *msm_iommu_pm_alloc(struct device *iommu_dev)
{
	return NULL;
}

static inline void msm_iommu_pm_free(struct device *iommu_dev)
{
	return;
}

static inline int msm_iommu_pm_iommu_register(struct iommu_info *info)
{
	return -EIO;
}

static inline void msm_iommu_pm_iommu_unregister(struct device *dev)
{
}

static inline void msm_iommu_attached(struct device *dev)
{
}

static inline void msm_iommu_detached(struct device *dev)
{
}
#endif
#endif
