/* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
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

#ifndef __CAMERA2_H__
#define __CAMERA2_H__

#include <media/msm_cam_sensor.h>
#include <mach/board.h>

enum msm_sensor_device_type_t {
	MSM_SENSOR_I2C_DEVICE,
	MSM_SENSOR_PLATFORM_DEVICE,
};

enum msm_bus_perf_setting {
	S_INIT,
	S_PREVIEW,
	S_VIDEO,
	S_CAPTURE,
	S_ZSL,
	S_STEREO_VIDEO,
	S_STEREO_CAPTURE,
	S_DEFAULT,
	S_LIVESHOT,
	S_DUAL,
	S_EXIT
};

enum cci_i2c_master_t {
	MASTER_0,
	MASTER_1,
};

struct msm_camera_slave_info {
	uint16_t sensor_slave_addr;
	uint16_t sensor_id_reg_addr;
	uint16_t sensor_id;
};

struct msm_cam_clk_info {
	const char *clk_name;
	long clk_rate;
	uint32_t delay;
};

struct msm_cam_clk_setting {
	struct msm_cam_clk_info *clk_info;
	uint16_t num_clk_info;
	uint8_t enable;
};

struct v4l2_subdev_info {
	enum v4l2_mbus_pixelcode code;
	enum v4l2_colorspace colorspace;
	uint16_t fmt;
	uint16_t order;
};

struct msm_camera_sensor_board_info {
	const char *sensor_name;
	struct msm_camera_slave_info *slave_info;
	struct msm_camera_csi_lane_params *csi_lane_params;
	struct camera_vreg_t *cam_vreg;
	int num_vreg;
	struct msm_camera_sensor_strobe_flash_data *strobe_flash_data;
	struct msm_camera_gpio_conf *gpio_conf;
	struct msm_actuator_info *actuator_info;
	struct msm_camera_i2c_conf *i2c_conf;
	struct msm_sensor_info_t *sensor_info;
	struct msm_sensor_init_params *sensor_init_params;
};

enum msm_camera_i2c_cmd_type {
	MSM_CAMERA_I2C_CMD_WRITE,
	MSM_CAMERA_I2C_CMD_POLL,
};

struct msm_camera_i2c_reg_conf {
	uint16_t reg_addr;
	uint16_t reg_data;
	enum msm_camera_i2c_data_type dt;
	enum msm_camera_i2c_cmd_type cmd_type;
	int16_t mask;
};

#endif
