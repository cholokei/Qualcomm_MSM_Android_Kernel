/* Copyright (c) 2013, The Linux Foundation. All rights reserved.
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
#include <linux/io.h>
#include <media/v4l2-subdev.h>

#include "msm.h"
#include "msm_isp_util.h"
#include "msm_isp_axi_util.h"
#include "msm_isp_stats_util.h"
#include "msm_camera_io_util.h"

#define MAX_ISP_V4l2_EVENTS 100

void msm_isp_gettimeofday(struct timeval *tv)
{
	struct timespec ts;

	ktime_get_ts(&ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec/1000;
}

int msm_isp_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
	struct v4l2_event_subscription *sub)
{
	struct vfe_device *vfe_dev = v4l2_get_subdevdata(sd);
	int rc = 0;
	rc = v4l2_event_subscribe(fh, sub, MAX_ISP_V4l2_EVENTS, NULL);
	if (rc == 0) {
		if (sub->type == V4L2_EVENT_ALL) {
			int i;

			vfe_dev->axi_data.event_mask = 0;
			for (i = 0; i < ISP_EVENT_MAX; i++)
				vfe_dev->axi_data.event_mask |= (1 << i);
		} else {
			int event_idx = sub->type - ISP_EVENT_BASE;

			vfe_dev->axi_data.event_mask |= (1 << event_idx);
		}
	}
	return rc;
}

int msm_isp_unsubscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
	struct v4l2_event_subscription *sub)
{
	struct vfe_device *vfe_dev = v4l2_get_subdevdata(sd);
	int rc = 0;

	rc = v4l2_event_unsubscribe(fh, sub);
	if (sub->type == V4L2_EVENT_ALL) {
		vfe_dev->axi_data.event_mask = 0;
	} else {
		int event_idx = sub->type - ISP_EVENT_BASE;

		vfe_dev->axi_data.event_mask &= ~(1 << event_idx);
	}
	return rc;
}

int msm_isp_cfg_pix(struct vfe_device *vfe_dev,
	struct msm_vfe_pix_cfg *pix_cfg)
{
	int rc = 0;
	/*TD Validate config info
	 * should check if all streams are off */

	vfe_dev->axi_data.src_info[VFE_PIX_0].input_mux = pix_cfg->input_mux;

	vfe_dev->hw_info->vfe_ops.core_ops.cfg_camif(vfe_dev, pix_cfg);
	return rc;
}

int msm_isp_cfg_input(struct vfe_device *vfe_dev, void *arg)
{
	int rc = 0;
	struct msm_vfe_input_cfg *input_cfg = arg;

	switch (input_cfg->input_src) {
	case VFE_PIX_0:
		msm_isp_cfg_pix(vfe_dev, &input_cfg->d.pix_cfg);
		break;
	case VFE_RAW_0:
	case VFE_RAW_1:
	case VFE_RAW_2:
	case VFE_SRC_MAX:
		break;
	}
	return rc;
}

long msm_isp_ioctl(struct v4l2_subdev *sd,
	unsigned int cmd, void *arg)
{
	struct vfe_device *vfe_dev = v4l2_get_subdevdata(sd);

	mutex_lock(&vfe_dev->mutex);
	ISP_DBG("%s cmd: %d\n", __func__, cmd);

	switch (cmd) {
	case VIDIOC_MSM_VFE_REG_CFG: {
		msm_isp_proc_cmd(vfe_dev, arg);
		break;
	}
	case VIDIOC_MSM_ISP_REQUEST_BUF:
	case VIDIOC_MSM_ISP_ENQUEUE_BUF:
	case VIDIOC_MSM_ISP_RELEASE_BUF: {
		msm_isp_proc_buf_cmd(vfe_dev->buf_mgr, cmd, arg);
		break;
	}
	case VIDIOC_MSM_ISP_REQUEST_STREAM:
		msm_isp_request_axi_stream(vfe_dev, arg);
		break;
	case VIDIOC_MSM_ISP_RELEASE_STREAM:
		msm_isp_release_axi_stream(vfe_dev, arg);
		break;
	case VIDIOC_MSM_ISP_CFG_STREAM:
		msm_isp_cfg_axi_stream(vfe_dev, arg);
		break;
	case VIDIOC_MSM_ISP_INPUT_CFG:
		msm_isp_cfg_input(vfe_dev, arg);
		break;
	case VIDIOC_MSM_ISP_SET_SRC_STATE:
		msm_isp_set_src_state(vfe_dev, arg);
		break;
	case VIDIOC_MSM_ISP_REQUEST_STATS_STREAM:
		msm_isp_request_stats_stream(vfe_dev, arg);
		break;
	case VIDIOC_MSM_ISP_RELEASE_STATS_STREAM:
		msm_isp_release_stats_stream(vfe_dev, arg);
		break;
	case VIDIOC_MSM_ISP_CFG_STATS_STREAM:
		msm_isp_cfg_stats_stream(vfe_dev, arg);
		break;
	case VIDIOC_MSM_ISP_CFG_STATS_COMP_POLICY:
		msm_isp_cfg_stats_comp_policy(vfe_dev, arg);
		break;
	}

	mutex_unlock(&vfe_dev->mutex);
	return 0;
}

static int msm_isp_send_hw_cmd(struct vfe_device *vfe_dev,
	struct msm_vfe_reg_cfg_cmd *reg_cfg_cmd, uint32_t *cfg_data)
{
	switch (reg_cfg_cmd->cmd_type) {
	case VFE_WRITE: {
		if (resource_size(vfe_dev->vfe_mem) <
				(reg_cfg_cmd->reg_offset + reg_cfg_cmd->len)) {
			pr_err("%s: Invalid length\n", __func__);
			return -EINVAL;
		}
		msm_camera_io_memcpy(vfe_dev->vfe_base +
			reg_cfg_cmd->reg_offset,
		cfg_data + reg_cfg_cmd->cmd_data/4, reg_cfg_cmd->len);
		break;
	}
	case VFE_WRITE_MB: {
		uint32_t *data_ptr = cfg_data + reg_cfg_cmd->cmd_data/4;
		msm_camera_io_w_mb(*data_ptr, vfe_dev->vfe_base +
			reg_cfg_cmd->reg_offset);
		break;
	}
	case VFE_WRITE_MASK: {
		uint32_t temp;
		temp = msm_camera_io_r(vfe_dev->vfe_base +
			reg_cfg_cmd->reg_offset);
		temp |= reg_cfg_cmd->cmd_data;
		msm_camera_io_w(temp, vfe_dev->vfe_base +
			reg_cfg_cmd->reg_offset);
		break;
	}
	case VFE_CLEAR_MASK: {
		uint32_t temp;
		temp = msm_camera_io_r(vfe_dev->vfe_base +
			reg_cfg_cmd->reg_offset);
		temp &= ~reg_cfg_cmd->cmd_data;
		msm_camera_io_w(temp, vfe_dev->vfe_base +
			reg_cfg_cmd->reg_offset);
		break;
	}
	case VFE_WRITE_AUTO_INCREMENT: {
		int i;
		uint32_t *data_ptr = cfg_data + reg_cfg_cmd->cmd_data/4;
		for (i = 0; i < reg_cfg_cmd->len/4; i++)
			msm_camera_io_w(*data_ptr++,
				vfe_dev->vfe_base + reg_cfg_cmd->reg_offset);
		break;
	}
	case VFE_READ: {
		int i;
		uint32_t *data_ptr = cfg_data + reg_cfg_cmd->cmd_data/4;
		for (i = 0; i < reg_cfg_cmd->len/4; i++)
			*data_ptr++ = msm_camera_io_r(
				vfe_dev->vfe_base + reg_cfg_cmd->reg_offset++);
		break;
	}
	}
	return 0;
}

int msm_isp_proc_cmd(struct vfe_device *vfe_dev, void *arg)
{
	int rc = 0, i;
	struct msm_vfe_cfg_cmd2 *proc_cmd = arg;
	struct msm_vfe_reg_cfg_cmd *reg_cfg_cmd;
	uint32_t *cfg_data;

	reg_cfg_cmd = kzalloc(sizeof(struct msm_vfe_reg_cfg_cmd)*
		proc_cmd->num_cfg, GFP_KERNEL);
	if (!reg_cfg_cmd) {
		pr_err("%s: reg_cfg alloc failed\n", __func__);
		rc = -ENOMEM;
		goto reg_cfg_failed;
	}

	cfg_data = kzalloc(proc_cmd->cmd_len, GFP_KERNEL);
	if (!cfg_data) {
		pr_err("%s: cfg_data alloc failed\n", __func__);
		rc = -ENOMEM;
		goto cfg_data_failed;
	}

	if (copy_from_user(reg_cfg_cmd,
		(void __user *)(proc_cmd->cfg_cmd),
		sizeof(struct msm_vfe_reg_cfg_cmd) * proc_cmd->num_cfg)) {
		rc = -EFAULT;
		goto copy_cmd_failed;
	}

	if (copy_from_user(cfg_data,
			(void __user *)(proc_cmd->cfg_data),
			proc_cmd->cmd_len)) {
		rc = -EFAULT;
		goto copy_cmd_failed;
	}

	for (i = 0; i < proc_cmd->num_cfg; i++)
		msm_isp_send_hw_cmd(vfe_dev, &reg_cfg_cmd[i], cfg_data);

	if (copy_to_user(proc_cmd->cfg_data,
			cfg_data, proc_cmd->cmd_len)) {
		rc = -EFAULT;
		goto copy_cmd_failed;
	}

copy_cmd_failed:
	kfree(cfg_data);
cfg_data_failed:
	kfree(reg_cfg_cmd);
reg_cfg_failed:
	return rc;
}

int msm_isp_send_event(struct vfe_device *vfe_dev,
	uint32_t event_type,
	struct msm_isp_event_data *event_data)
{
	struct v4l2_event isp_event;
	memset(&isp_event, 0, sizeof(struct v4l2_event));
	isp_event.id = 0;
	isp_event.type = event_type;
	memcpy(&isp_event.u.data[0], event_data,
		sizeof(struct msm_isp_event_data));
	v4l2_event_queue(vfe_dev->subdev.sd.devnode, &isp_event);
	return 0;
}

#define CAL_WORD(width, M, N) ((width * M + N - 1) / N)

int msm_isp_cal_word_per_line(uint32_t output_format,
	uint32_t pixel_per_line)
{
	int val = -1;
	switch (output_format) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
		val = CAL_WORD(pixel_per_line, 1, 8);
		break;
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
		val = CAL_WORD(pixel_per_line, 1, 6);
		break;
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		val = CAL_WORD(pixel_per_line, 1, 5);
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		val = CAL_WORD(pixel_per_line, 1, 8);
		break;
		/*TD: Add more image format*/
	default:
		pr_err("%s: Invalid output format\n", __func__);
		break;
	}
	return val;
}

int msm_isp_get_bit_per_pixel(uint32_t output_format)
{
	switch (output_format) {
	case V4L2_PIX_FMT_SBGGR8:
	case V4L2_PIX_FMT_SGBRG8:
	case V4L2_PIX_FMT_SGRBG8:
	case V4L2_PIX_FMT_SRGGB8:
		return 8;
	case V4L2_PIX_FMT_SBGGR10:
	case V4L2_PIX_FMT_SGBRG10:
	case V4L2_PIX_FMT_SGRBG10:
	case V4L2_PIX_FMT_SRGGB10:
		return 10;
	case V4L2_PIX_FMT_SBGGR12:
	case V4L2_PIX_FMT_SGBRG12:
	case V4L2_PIX_FMT_SGRBG12:
	case V4L2_PIX_FMT_SRGGB12:
		return 12;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV21:
		return 8;
		/*TD: Add more image format*/
	default:
		pr_err("%s: Invalid output format\n", __func__);
		break;
	}
	return -EINVAL;
}

irqreturn_t msm_isp_process_irq(int irq_num, void *data)
{
	unsigned long flags;
	struct msm_vfe_tasklet_queue_cmd *queue_cmd;
	struct vfe_device *vfe_dev = (struct vfe_device *) data;
	uint32_t irq_status0, irq_status1;

	vfe_dev->hw_info->vfe_ops.irq_ops.
		read_irq_status(vfe_dev, &irq_status0, &irq_status1);
	if ((irq_status0 == 0) && (irq_status1 == 0)) {
		ISP_DBG("%s: irq_status0 & 1 are both 0!\n", __func__);
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&vfe_dev->tasklet_lock, flags);
	queue_cmd = &vfe_dev->tasklet_queue_cmd[vfe_dev->taskletq_idx];
	if (queue_cmd->cmd_used) {
		pr_err("%s: Tasklet queue overflow\n", __func__);
		list_del(&queue_cmd->list);
	} else {
		atomic_add(1, &vfe_dev->irq_cnt);
	}
	queue_cmd->vfeInterruptStatus0 = irq_status0;
	queue_cmd->vfeInterruptStatus1 = irq_status1;
	msm_isp_gettimeofday(&queue_cmd->tv);
	queue_cmd->cmd_used = 1;
	vfe_dev->taskletq_idx =
		(vfe_dev->taskletq_idx + 1) % MSM_VFE_TASKLETQ_SIZE;
	list_add_tail(&queue_cmd->list, &vfe_dev->tasklet_q);
	spin_unlock_irqrestore(&vfe_dev->tasklet_lock, flags);
	tasklet_schedule(&vfe_dev->vfe_tasklet);
	return IRQ_HANDLED;
}

void msm_isp_do_tasklet(unsigned long data)
{
	unsigned long flags;
	struct vfe_device *vfe_dev = (struct vfe_device *) data;
	struct msm_vfe_irq_ops *irq_ops = &vfe_dev->hw_info->vfe_ops.irq_ops;
	struct msm_vfe_tasklet_queue_cmd *queue_cmd;
	struct timeval tv;
	uint32_t irq_status0, irq_status1;
	while (atomic_read(&vfe_dev->irq_cnt)) {
		spin_lock_irqsave(&vfe_dev->tasklet_lock, flags);
		queue_cmd = list_first_entry(&vfe_dev->tasklet_q,
		struct msm_vfe_tasklet_queue_cmd, list);
		if (!queue_cmd) {
			atomic_set(&vfe_dev->irq_cnt, 0);
			spin_unlock_irqrestore(&vfe_dev->tasklet_lock, flags);
			return;
		}
		atomic_sub(1, &vfe_dev->irq_cnt);
		list_del(&queue_cmd->list);
		queue_cmd->cmd_used = 0;
		irq_status0 = queue_cmd->vfeInterruptStatus0;
		irq_status1 = queue_cmd->vfeInterruptStatus1;
		tv = queue_cmd->tv;
		spin_unlock_irqrestore(&vfe_dev->tasklet_lock, flags);
		ISP_DBG("%s: status0: 0x%x status1: 0x%x\n",
			__func__, irq_status0, irq_status1);
		irq_ops->process_reset_irq(vfe_dev,
			irq_status0, irq_status1);
		irq_ops->process_halt_irq(vfe_dev,
			irq_status0, irq_status1);
		irq_ops->process_camif_irq(vfe_dev,
			irq_status0, irq_status1, &tv);
		irq_ops->process_error_irq(vfe_dev,
			irq_status0, irq_status1);
		irq_ops->process_axi_irq(vfe_dev,
			irq_status0, irq_status1, &tv);
		irq_ops->process_stats_irq(vfe_dev,
			irq_status0, irq_status1, &tv);
		irq_ops->process_reg_update(vfe_dev, irq_status0, irq_status1);
	}
}

void msm_isp_set_src_state(struct vfe_device *vfe_dev, void *arg)
{
	struct msm_vfe_axi_src_state *src_state = arg;
	vfe_dev->axi_data.src_info[src_state->input_src].active =
	src_state->src_active;
}

int msm_isp_open_node(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct vfe_device *vfe_dev = v4l2_get_subdevdata(sd);
	long rc;
	ISP_DBG("%s\n", __func__);

	mutex_lock(&vfe_dev->mutex);
	if (vfe_dev->vfe_open_cnt == 1) {
		pr_err("VFE already open\n");
		mutex_unlock(&vfe_dev->mutex);
		return -ENODEV;
	}

	if (vfe_dev->hw_info->vfe_ops.core_ops.init_hw(vfe_dev) < 0) {
		pr_err("%s: init hardware failed\n", __func__);
		mutex_unlock(&vfe_dev->mutex);
		return -EBUSY;
	}

	rc = vfe_dev->hw_info->vfe_ops.core_ops.reset_hw(vfe_dev);
	if (rc <= 0) {
		pr_err("%s: reset timeout\n", __func__);
		mutex_unlock(&vfe_dev->mutex);
		return -EINVAL;
	}
	vfe_dev->hw_info->vfe_ops.core_ops.init_hw_reg(vfe_dev);

	vfe_dev->buf_mgr->ops->attach_ctx(vfe_dev->buf_mgr, vfe_dev->iommu_ctx);
	vfe_dev->buf_mgr->ops->buf_mgr_init(vfe_dev->buf_mgr, "msm_isp", 14);

	memset(&vfe_dev->axi_data, 0, sizeof(struct msm_vfe_axi_shared_data));
	memset(&vfe_dev->stats_data, 0,
		sizeof(struct msm_vfe_stats_shared_data));
	vfe_dev->axi_data.hw_info = vfe_dev->hw_info->axi_hw_info;

	ISP_DBG("%s: HW Version: 0x%x\n",
		__func__, msm_camera_io_r(vfe_dev->vfe_base));

	vfe_dev->vfe_open_cnt++;
	vfe_dev->taskletq_idx = 0;
	mutex_unlock(&vfe_dev->mutex);
	return 0;
}

int msm_isp_close_node(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	long rc;
	struct vfe_device *vfe_dev = v4l2_get_subdevdata(sd);
	ISP_DBG("%s\n", __func__);
	mutex_lock(&vfe_dev->mutex);
	if (vfe_dev->vfe_open_cnt == 0) {
		pr_err("%s: Invalid close\n", __func__);
		mutex_unlock(&vfe_dev->mutex);
		return -ENODEV;
	}

	rc = vfe_dev->hw_info->vfe_ops.axi_ops.halt(vfe_dev);
	if (rc <= 0)
		pr_err("%s: halt timeout\n", __func__);

	vfe_dev->buf_mgr->ops->buf_mgr_deinit(vfe_dev->buf_mgr);
	vfe_dev->buf_mgr->ops->detach_ctx(vfe_dev->buf_mgr, vfe_dev->iommu_ctx);
	vfe_dev->hw_info->vfe_ops.core_ops.release_hw(vfe_dev);

	vfe_dev->vfe_open_cnt--;
	mutex_unlock(&vfe_dev->mutex);
	return 0;
}
