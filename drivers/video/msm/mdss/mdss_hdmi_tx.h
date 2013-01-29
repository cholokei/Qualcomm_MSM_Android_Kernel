/* Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
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

#ifndef __MDSS_HDMI_TX_H__
#define __MDSS_HDMI_TX_H__

#include <linux/switch.h>
#include "mdss_hdmi_util.h"

enum hdmi_tx_io_type {
	HDMI_TX_CORE_IO,
	HDMI_TX_PHY_IO,
	HDMI_TX_QFPROM_IO,
	HDMI_TX_MAX_IO
};

enum hdmi_tx_power_module_type {
	HDMI_TX_HPD_PM,
	HDMI_TX_CORE_PM,
	HDMI_TX_CEC_PM,
	HDMI_TX_MAX_PM
};

struct hdmi_tx_platform_data {
	/* Data filled from device tree nodes */
	struct dss_io_data io[HDMI_TX_MAX_IO];
	struct dss_module_power power_data[HDMI_TX_MAX_PM];
};

struct hdmi_tx_ctrl {
	struct platform_device *pdev;
	struct hdmi_tx_platform_data pdata;
	struct mdss_panel_data panel_data;

	int audio_sample_rate;

	struct mutex mutex;
	struct kobject *kobj;
	struct switch_dev sdev;
	struct switch_dev audio_sdev;
	struct workqueue_struct *workq;

	uint32_t video_resolution;

	u32 panel_power_on;
	u32 panel_suspend;

	u32 hpd_state;
	u32 hpd_off_pending;
	u32 hpd_feature_on;
	u32 hpd_initialized;
	struct completion hpd_done;
	struct work_struct hpd_int_work;

	struct work_struct power_off_work;

	bool hdcp_feature_on;
	u32 present_hdcp;

	u8 spd_vendor_name[8];
	u8 spd_product_description[16];

	struct hdmi_tx_ddc_ctrl ddc_ctrl;

	void *feature_data[HDMI_TX_FEAT_MAX];
};

#endif /* __MDSS_HDMI_TX_H__ */
