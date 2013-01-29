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
 *
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include <mach/subsystem_restart.h>

#include "msm_vidc_common.h"
#include "vidc_hfi_api.h"
#include "msm_smem.h"
#include "msm_vidc_debug.h"

#define HW_RESPONSE_TIMEOUT (5 * 60 * 1000)

#define IS_ALREADY_IN_STATE(__p, __d) ({\
	int __rc = (__p >= __d);\
	__rc; \
})

#define V4L2_EVENT_SEQ_CHANGED_SUFFICIENT \
		V4L2_EVENT_MSM_VIDC_PORT_SETTINGS_CHANGED_SUFFICIENT
#define V4L2_EVENT_SEQ_CHANGED_INSUFFICIENT \
		V4L2_EVENT_MSM_VIDC_PORT_SETTINGS_CHANGED_INSUFFICIENT

#define NUM_MBS_PER_SEC(__height, __width, __fps) ({\
	(__height >> 4) * (__width >> 4) * __fps; \
})

#define VIDC_BUS_LOAD(__height, __width, __fps, __br) ({\
	__height * __width * __fps; \
})

#define GET_NUM_MBS(__h, __w) ({\
	u32 __mbs = (__h >> 4) * (__w >> 4);\
	__mbs;\
})

#define TIME_DIFF_THRESHOLD 200

/*Load is in Macroblocks (MBs) per sec. This value is calculated
 * based on one 4k video instance @ 24 fps plus one 1080p video
 * instance @ 30fps. 1 MB = 16 X 16 pixels*/
#define MAX_LOAD 1074240

static int msm_comm_get_load(struct msm_vidc_core *core,
	enum session_type type)
{
	struct msm_vidc_inst *inst = NULL;
	int num_mbs_per_sec = 0;
	unsigned long flags;
	if (!core) {
		dprintk(VIDC_ERR, "Invalid args: %p\n", core);
		return -EINVAL;
	}
	list_for_each_entry(inst, &core->instances, list) {
		spin_lock_irqsave(&inst->lock, flags);
		if (inst->session_type == type &&
			inst->state >= MSM_VIDC_OPEN_DONE &&
			inst->state < MSM_VIDC_STOP_DONE) {
			num_mbs_per_sec += NUM_MBS_PER_SEC(inst->prop.height,
					inst->prop.width, inst->prop.fps);
		}
		spin_unlock_irqrestore(&inst->lock, flags);
	}
	return num_mbs_per_sec;
}

static int msm_comm_scale_bus(struct msm_vidc_core *core,
	enum session_type type, enum mem_type mtype)
{
	int load;
	int rc = 0;
	struct hfi_device *hdev;

	if (!core || type >= MSM_VIDC_MAX_DEVICES) {
		dprintk(VIDC_ERR, "Invalid args: %p, %d\n", core, type);
		return -EINVAL;
	}

	hdev = core->device;
	if (!hdev) {
		dprintk(VIDC_ERR, "Invalid device handle %p\n", hdev);
		return -EINVAL;
	}

	load = msm_comm_get_load(core, type);

	rc = hdev->scale_bus(hdev->hfi_device_data, load, type, mtype);
	if (rc)
		dprintk(VIDC_ERR, "Failed to scale bus: %d\n", rc);

	return rc;
}

static void msm_comm_unvote_buses(struct msm_vidc_core *core,
	enum mem_type mtype)
{
	int i;
	struct hfi_device *hdev;

	if (!core || !core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return;
	}
	hdev = core->device;

	for (i = 0; i < MSM_VIDC_MAX_DEVICES; i++) {
		if ((mtype & DDR_MEM) &&
			hdev->scale_bus(hdev->hfi_device_data, 0, i, DDR_MEM)) {
			dprintk(VIDC_WARN,
				"Failed to unvote for DDR accesses\n");
		}
		if ((mtype & OCMEM_MEM) &&
			hdev->scale_bus(hdev->hfi_device_data, 0, i,
					OCMEM_MEM)) {
			dprintk(VIDC_WARN,
				"Failed to unvote for OCMEM accesses\n");
		}
	}
}

struct msm_vidc_core *get_vidc_core(int core_id)
{
	struct msm_vidc_core *core;
	int found = 0;
	unsigned long flags;
	if (core_id > MSM_VIDC_CORES_MAX) {
		dprintk(VIDC_ERR, "Core id = %d is greater than max = %d\n",
			core_id, MSM_VIDC_CORES_MAX);
		return NULL;
	}
	spin_lock_irqsave(&vidc_driver->lock, flags);
	list_for_each_entry(core, &vidc_driver->cores, list) {
		if (core && core->id == core_id)
			found = 1;
			break;
	}
	spin_unlock_irqrestore(&vidc_driver->lock, flags);
	if (found)
		return core;
	return NULL;
}

const struct msm_vidc_format *msm_comm_get_pixel_fmt_index(
	const struct msm_vidc_format fmt[], int size, int index, int fmt_type)
{
	int i, k = 0;
	if (!fmt || index < 0) {
		dprintk(VIDC_ERR, "Invalid inputs, fmt = %p, index = %d\n",
						fmt, index);
		return NULL;
	}
	for (i = 0; i < size; i++) {
		if (fmt[i].type != fmt_type)
			continue;
		if (k == index)
			break;
		k++;
	}
	if (i == size) {
		dprintk(VIDC_INFO, "Format not found\n");
		return NULL;
	}
	return &fmt[i];
}
const struct msm_vidc_format *msm_comm_get_pixel_fmt_fourcc(
	const struct msm_vidc_format fmt[], int size, int fourcc, int fmt_type)
{
	int i;
	if (!fmt) {
		dprintk(VIDC_ERR, "Invalid inputs, fmt = %p\n", fmt);
		return NULL;
	}
	for (i = 0; i < size; i++) {
		if (fmt[i].fourcc == fourcc)
				break;
	}
	if (i == size) {
		dprintk(VIDC_INFO, "Format not found\n");
		return NULL;
	}
	return &fmt[i];
}

struct buf_queue *msm_comm_get_vb2q(
		struct msm_vidc_inst *inst,	enum v4l2_buf_type type)
{
	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return &inst->bufq[CAPTURE_PORT];
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return &inst->bufq[OUTPUT_PORT];
	return NULL;
}

static void handle_sys_init_done(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_core *core;
	struct vidc_hal_sys_init_done *sys_init_msg;
	int index = SYS_MSG_INDEX(cmd);
	if (!response) {
		dprintk(VIDC_ERR,
			"Failed to get valid response for sys init\n");
		return;
	}
	core = get_vidc_core(response->device_id);
	if (!core) {
		dprintk(VIDC_ERR, "Wrong device_id received\n");
		return;
	}
	dprintk(VIDC_DBG, "index = %d\n", index);
	dprintk(VIDC_DBG, "ptr = %p\n", &(core->completions[index]));
	complete(&(core->completions[index]));
	sys_init_msg = response->data;
	if (!sys_init_msg) {
		dprintk(VIDC_ERR, "sys_init_done message not proper\n");
		return;
	}
}

static void handle_session_release_buf_done(enum command_response cmd,
	void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	struct internal_buf *buf;
	struct list_head *ptr, *next;
	struct hal_buffer_info *buffer;
	u32 address, buf_found = false;

	if (!response || !response->data) {
		dprintk(VIDC_ERR, "Invalid release_buf_done response\n");
		return;
	}

	inst = (struct msm_vidc_inst *)response->session_id;
	buffer = (struct hal_buffer_info *) response->data;
	address = (u32) buffer->buffer_addr;

	list_for_each_safe(ptr, next, &inst->internalbufs) {
		buf = list_entry(ptr, struct internal_buf, list);
		if (address == buf->handle->device_addr) {
			dprintk(VIDC_DBG, "releasing scratch: 0x%x",
					(u32) buf->handle->device_addr);
					buf_found = true;
		}
	}

	list_for_each_safe(ptr, next, &inst->persistbufs) {
		buf = list_entry(ptr, struct internal_buf, list);
		if (address == (u32) buf->handle->device_addr) {
			dprintk(VIDC_DBG, "releasing persist: 0x%x",
					(u32) buf->handle->device_addr);
			buf_found = true;
		}
	}

	if (!buf_found)
		dprintk(VIDC_ERR, "invalid buffer received from firmware");
	complete(&inst->completions[SESSION_MSG_INDEX(cmd)]);
}

static void handle_sys_release_res_done(
	enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_core *core;
	if (!response) {
		dprintk(VIDC_ERR,
			"Failed to get valid response for sys init\n");
		return;
	}
	core = get_vidc_core(response->device_id);
	if (!core) {
		dprintk(VIDC_ERR, "Wrong device_id received\n");
		return;
	}
	complete(&core->completions[SYS_MSG_INDEX(cmd)]);
}

static void change_inst_state(struct msm_vidc_inst *inst,
	enum instance_state state)
{
	unsigned long flags;
	spin_lock_irqsave(&inst->lock, flags);
	dprintk(VIDC_DBG, "Moved inst: %p from state: %d to state: %d\n",
		   inst, inst->state, state);
	inst->state = state;
	spin_unlock_irqrestore(&inst->lock, flags);
}

static int signal_session_msg_receipt(enum command_response cmd,
		struct msm_vidc_inst *inst)
{
	if (!inst) {
		dprintk(VIDC_ERR, "Invalid(%p) instance id\n", inst);
		return -EINVAL;
	}
	complete(&inst->completions[SESSION_MSG_INDEX(cmd)]);
	return 0;
}

static int wait_for_sess_signal_receipt(struct msm_vidc_inst *inst,
	enum command_response cmd)
{
	int rc = 0;
	rc = wait_for_completion_interruptible_timeout(
		&inst->completions[SESSION_MSG_INDEX(cmd)],
		msecs_to_jiffies(HW_RESPONSE_TIMEOUT));
	if (!rc) {
		dprintk(VIDC_ERR, "Wait interrupted or timeout: %d\n", rc);
		rc = -EIO;
	} else {
		rc = 0;
	}
	return rc;
}

static int wait_for_state(struct msm_vidc_inst *inst,
	enum instance_state flipped_state,
	enum instance_state desired_state,
	enum command_response hal_cmd)
{
	int rc = 0;
	if (IS_ALREADY_IN_STATE(flipped_state, desired_state)) {
		dprintk(VIDC_INFO, "inst: %p is already in state: %d\n",
						inst, inst->state);
		goto err_same_state;
	}
	dprintk(VIDC_DBG, "Waiting for hal_cmd: %d\n", hal_cmd);
	rc = wait_for_sess_signal_receipt(inst, hal_cmd);
	if (!rc)
		change_inst_state(inst, desired_state);
err_same_state:
	return rc;
}

static void handle_session_init_done(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	if (response) {
		inst = (struct msm_vidc_inst *)response->session_id;
		signal_session_msg_receipt(cmd, inst);
	} else {
		dprintk(VIDC_ERR,
			"Failed to get valid response for session init\n");
	}
}

static void handle_event_change(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	struct v4l2_event dqevent;
	struct v4l2_control control = {0};
	struct msm_vidc_cb_event *event_notify;
	int rc = 0;
	if (response) {
		inst = (struct msm_vidc_inst *)response->session_id;
		dqevent.id = 0;
		event_notify = (struct msm_vidc_cb_event *) response->data;
		switch (event_notify->hal_event_type) {
		case HAL_EVENT_SEQ_CHANGED_SUFFICIENT_RESOURCES:
			dqevent.type =
				V4L2_EVENT_SEQ_CHANGED_INSUFFICIENT;
			control.id =
				V4L2_CID_MPEG_VIDC_VIDEO_CONTINUE_DATA_TRANSFER;
			rc = v4l2_g_ctrl(&inst->ctrl_handler, &control);
			if (rc)
				dprintk(VIDC_WARN,
					"Failed to get Smooth streamng flag\n");
			if (!rc && control.value == true)
				dqevent.type =
					V4L2_EVENT_SEQ_CHANGED_SUFFICIENT;
			break;
		case HAL_EVENT_SEQ_CHANGED_INSUFFICIENT_RESOURCES:
			dqevent.type =
				V4L2_EVENT_SEQ_CHANGED_INSUFFICIENT;
			break;
		default:
			break;
		}
		inst->reconfig_height = event_notify->height;
		inst->reconfig_width = event_notify->width;
		inst->in_reconfig = true;
		v4l2_event_queue_fh(&inst->event_handler, &dqevent);
		wake_up(&inst->kernel_event_queue);
		return;
	} else {
		dprintk(VIDC_ERR,
			"Failed to get valid response for event_change\n");
	}
}

static void handle_session_prop_info(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	unsigned long flags;
	int i;
	if (!response || !response->data) {
		dprintk(VIDC_ERR,
			"Failed to get valid response for prop info\n");
		return;
	}
	inst = (struct msm_vidc_inst *)response->session_id;
	spin_lock_irqsave(&inst->lock, flags);
	memcpy(&inst->buff_req, response->data,
			sizeof(struct buffer_requirements));
	spin_unlock_irqrestore(&inst->lock, flags);
	for (i = 0; i < 8; i++) {
		dprintk(VIDC_DBG,
			"buffer type: %d, count : %d, size: %d\n",
			inst->buff_req.buffer[i].buffer_type,
			inst->buff_req.buffer[i].buffer_count_actual,
			inst->buff_req.buffer[i].buffer_size);
	}
	dprintk(VIDC_PROF, "Input buffers: %d, Output buffers: %d\n",
			inst->buff_req.buffer[0].buffer_count_actual,
			inst->buff_req.buffer[1].buffer_count_actual);
	signal_session_msg_receipt(cmd, inst);
}

static void handle_load_resource_done(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	if (response)
		inst = (struct msm_vidc_inst *)response->session_id;
	else
		dprintk(VIDC_ERR,
			"Failed to get valid response for load resource\n");
}

static void handle_start_done(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	if (response) {
		inst = (struct msm_vidc_inst *)response->session_id;
		signal_session_msg_receipt(cmd, inst);
	} else {
		dprintk(VIDC_ERR,
			"Failed to get valid response for start\n");
	}
}

static void handle_stop_done(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	if (response) {
		inst = (struct msm_vidc_inst *)response->session_id;
		signal_session_msg_receipt(cmd, inst);
	} else {
		dprintk(VIDC_ERR,
			"Failed to get valid response for stop\n");
	}
}

static void handle_release_res_done(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	if (response) {
		inst = (struct msm_vidc_inst *)response->session_id;
		signal_session_msg_receipt(cmd, inst);
	} else {
		dprintk(VIDC_ERR,
			"Failed to get valid response for release resource\n");
	}
}

static void handle_session_flush(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	struct v4l2_event dqevent;
	if (response) {
		inst = (struct msm_vidc_inst *)response->session_id;
		dqevent.type = V4L2_EVENT_MSM_VIDC_FLUSH_DONE;
		dqevent.id = 0;
		v4l2_event_queue_fh(&inst->event_handler, &dqevent);
		wake_up(&inst->kernel_event_queue);
	} else {
		dprintk(VIDC_ERR, "Failed to get valid response for flush\n");
	}
}

static void handle_session_error(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst = NULL;
	struct v4l2_event dqevent;
	if (response) {
		inst = (struct msm_vidc_inst *)response->session_id;
		if (inst) {
			dprintk(VIDC_WARN,
				"Session error receivd for session %p\n", inst);
			mutex_lock(&inst->sync_lock);
			inst->state = MSM_VIDC_CORE_INVALID;
			mutex_unlock(&inst->sync_lock);
			dqevent.type = V4L2_EVENT_MSM_VIDC_SYS_ERROR;
			dqevent.id = 0;
			v4l2_event_queue_fh(&inst->event_handler, &dqevent);
			wake_up(&inst->kernel_event_queue);
		}
	} else {
		dprintk(VIDC_ERR,
			"Failed to get valid response for session error\n");
	}
}
static void handle_sys_error(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst = NULL ;
	struct msm_vidc_core *core = NULL;
	struct v4l2_event dqevent;
	unsigned long flags;
	if (response) {
		core = get_vidc_core(response->device_id);
		dprintk(VIDC_WARN, "SYS_ERROR received for core %p\n", core);
		if (core) {
			spin_lock_irqsave(&core->lock, flags);
			core->state = VIDC_CORE_INVALID;
			spin_unlock_irqrestore(&core->lock, flags);
			dqevent.type = V4L2_EVENT_MSM_VIDC_SYS_ERROR;
			dqevent.id = 0;
			list_for_each_entry(inst, &core->instances,
					list) {
				v4l2_event_queue_fh(&inst->event_handler,
						&dqevent);

				spin_lock_irqsave(&inst->lock, flags);
				inst->state = MSM_VIDC_CORE_INVALID;
				spin_unlock_irqrestore(&inst->lock, flags);

				wake_up(&inst->kernel_event_queue);
			}
		} else {
			dprintk(VIDC_ERR,
				"Got SYS_ERR but unable to identify core");
		}
	} else {
		dprintk(VIDC_ERR,
			"Failed to get valid response for sys error\n");
	}
}

static void handle_sys_watchdog_timeout(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	struct msm_vidc_core *core = NULL;
	struct v4l2_event dqevent;
	unsigned long flags;
	dprintk(VIDC_ERR, "Venus Subsystem crashed\n");
	core = get_vidc_core(response->device_id);
	if (!core) {
		dprintk(VIDC_ERR, "Wrong device_id received\n");
		return;
	}
	subsystem_crashed("venus");
	spin_lock_irqsave(&core->lock, flags);
	core->state = VIDC_CORE_INVALID;
	spin_unlock_irqrestore(&core->lock, flags);
	dqevent.type = V4L2_EVENT_MSM_VIDC_SYS_ERROR;
	dqevent.id = 0;
	list_for_each_entry(inst, &core->instances, list) {
		if (inst) {
			v4l2_event_queue_fh(&inst->event_handler, &dqevent);
			spin_lock_irqsave(&inst->lock, flags);
			inst->state = MSM_VIDC_CORE_INVALID;
			inst->session = NULL;
			spin_unlock_irqrestore(&inst->lock, flags);
		}
	}
}

static void handle_session_close(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_cmd_done *response = data;
	struct msm_vidc_inst *inst;
	struct v4l2_event dqevent;
	if (response) {
		inst = (struct msm_vidc_inst *)response->session_id;
		signal_session_msg_receipt(cmd, inst);
		dqevent.type = V4L2_EVENT_MSM_VIDC_CLOSE_DONE;
		dqevent.id = 0;
		v4l2_event_queue_fh(&inst->event_handler, &dqevent);
		inst->session = NULL;
		wake_up(&inst->kernel_event_queue);
		show_stats(inst);
	} else {
		dprintk(VIDC_ERR,
			"Failed to get valid response for session close\n");
	}
}

static struct vb2_buffer *get_vb_from_device_addr(struct buf_queue *bufq,
		u32 dev_addr)
{
	struct vb2_buffer *vb = NULL;
	struct vb2_queue *q = NULL;
	int found = 0;
	if (!bufq) {
		dprintk(VIDC_ERR, "Invalid parameter\n");
		return NULL;
	}
	q = &bufq->vb2_bufq;
	mutex_lock(&bufq->lock);
	list_for_each_entry(vb, &q->queued_list, queued_entry) {
		if (vb->v4l2_planes[0].m.userptr == dev_addr) {
			found = 1;
			break;
		}
	}
	mutex_unlock(&bufq->lock);
	if (!found) {
		dprintk(VIDC_ERR,
			"Failed to find the buffer in queued list: 0x%x, %d\n",
			dev_addr, q->type);
		vb = NULL;
	}
	return vb;
}

static void handle_ebd(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_data_done *response = data;
	struct vb2_buffer *vb;
	struct msm_vidc_inst *inst;
	if (!response) {
		dprintk(VIDC_ERR, "Invalid response from vidc_hal\n");
		return;
	}
	vb = response->clnt_data;
	inst = (struct msm_vidc_inst *)response->session_id;
	if (vb) {
		mutex_lock(&inst->bufq[OUTPUT_PORT].lock);
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
		mutex_unlock(&inst->bufq[OUTPUT_PORT].lock);
		wake_up(&inst->kernel_event_queue);
		msm_vidc_debugfs_update(inst, MSM_VIDC_DEBUGFS_EVENT_EBD);
	}
}

static void msm_comm_update_clocks(struct msm_vidc_inst *inst,
	u64 cur_time_stamp)
{
	u32 new_time_diff = 0, cur_time_diff = 0;
	u8 updated_fps = 0;
	struct v4l2_ctrl *ctrl = NULL;
	u32 output_order = 0;

	if (inst->session_type == MSM_VIDC_ENCODER)
		goto exit;
	if (cur_time_stamp >= LLONG_MAX) {
		dprintk(VIDC_WARN,
			"Clock scaling failed : Timestamp invalid\n");
		goto exit;
	}
	ctrl = v4l2_ctrl_find(&inst->ctrl_handler,
		V4L2_CID_MPEG_VIDC_VIDEO_OUTPUT_ORDER);
	if (!ctrl) {
		dprintk(VIDC_WARN, "Unable to find output order control\n");
		dprintk(VIDC_WARN,
			"Performance might be impacted for higher fps clips\n");
		goto exit;
	}
	output_order = v4l2_ctrl_g_ctrl(ctrl);
	if (output_order == V4L2_MPEG_VIDC_VIDEO_OUTPUT_ORDER_DISPLAY) {
		new_time_diff =
			(u32)(cur_time_stamp - inst->prop.prev_time_stamp);
		inst->prop.prev_time_stamp = cur_time_stamp;
		if (!new_time_diff)
			goto exit;
		if (inst->prop.fps)
			cur_time_diff = USEC_PER_SEC / inst->prop.fps;
		cur_time_diff = cur_time_diff > new_time_diff ?
			cur_time_diff - new_time_diff :
			new_time_diff - cur_time_diff;
		if (cur_time_diff > TIME_DIFF_THRESHOLD) {
			updated_fps = (u8) (USEC_PER_SEC / new_time_diff);
			if (updated_fps && (updated_fps != inst->prop.fps)) {
				inst->prop.fps = updated_fps;
				dprintk(VIDC_DBG,
						"Updating clocks: Decoding fps = %d\n",
						inst->prop.fps);
				msm_comm_scale_clocks_and_bus(inst);
			}
		}
	}
exit:
	return;
}

static void handle_fbd(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_data_done *response = data;
	struct msm_vidc_inst *inst;
	struct vb2_buffer *vb;
	struct vidc_hal_fbd *fill_buf_done;

	if (!response) {
		dprintk(VIDC_ERR, "Invalid response from vidc_hal\n");
		return;
	}
	inst = (struct msm_vidc_inst *)response->session_id;
	fill_buf_done = (struct vidc_hal_fbd *)&response->output_done;
	vb = get_vb_from_device_addr(&inst->bufq[CAPTURE_PORT],
		(u32)fill_buf_done->packet_buffer1);
	if (vb) {
		vb->v4l2_planes[0].bytesused = fill_buf_done->filled_len1;
		vb->v4l2_planes[0].reserved[2] = fill_buf_done->start_x_coord;
		vb->v4l2_planes[0].reserved[3] = fill_buf_done->start_y_coord;
		vb->v4l2_planes[0].reserved[4] = fill_buf_done->frame_width;
		vb->v4l2_planes[0].reserved[5] = fill_buf_done->frame_height;
		if (!(fill_buf_done->flags1 &
			HAL_BUFFERFLAG_TIMESTAMPINVALID) &&
			fill_buf_done->filled_len1) {
			int64_t time_usec = fill_buf_done->timestamp_hi;
			time_usec = (time_usec << 32) |
				fill_buf_done->timestamp_lo;
			vb->v4l2_buf.timestamp =
				ns_to_timeval(time_usec * NSEC_PER_USEC);
				msm_comm_update_clocks(inst, time_usec);
		}
		vb->v4l2_buf.flags = 0;

		if (fill_buf_done->flags1 & HAL_BUFFERFLAG_EOS)
			vb->v4l2_buf.flags |= V4L2_BUF_FLAG_EOS;
		if (fill_buf_done->flags1 & HAL_BUFFERFLAG_CODECCONFIG)
			vb->v4l2_buf.flags &= ~V4L2_QCOM_BUF_FLAG_CODECCONFIG;
		if (fill_buf_done->flags1 & HAL_BUFFERFLAG_SYNCFRAME)
			vb->v4l2_buf.flags |= V4L2_BUF_FLAG_KEYFRAME;
		if (fill_buf_done->flags1 & HAL_BUFFERFLAG_EOSEQ)
			vb->v4l2_buf.flags |= V4L2_QCOM_BUF_FLAG_EOSEQ;
		switch (fill_buf_done->picture_type) {
		case HAL_PICTURE_IDR:
		case HAL_PICTURE_I:
			vb->v4l2_buf.flags |= V4L2_BUF_FLAG_KEYFRAME;
			break;
		case HAL_PICTURE_P:
			vb->v4l2_buf.flags |= V4L2_BUF_FLAG_PFRAME;
			break;
		case HAL_PICTURE_B:
			vb->v4l2_buf.flags |= V4L2_BUF_FLAG_BFRAME;
			break;
		case HAL_FRAME_NOTCODED:
		case HAL_UNUSED_PICT:
			/* Do we need to care about these? */
		case HAL_FRAME_YUV:
			break;
		default:
			break;
		}
		inst->count.fbd++;
		if (fill_buf_done->filled_len1)
			msm_vidc_debugfs_update(inst,
				MSM_VIDC_DEBUGFS_EVENT_FBD);

		dprintk(VIDC_DBG, "Filled length = %d; flags %x\n",
				vb->v4l2_planes[0].bytesused,
				vb->v4l2_buf.flags);
		mutex_lock(&inst->bufq[CAPTURE_PORT].lock);
		vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
		mutex_unlock(&inst->bufq[CAPTURE_PORT].lock);
		wake_up(&inst->kernel_event_queue);
	} else {
		/*
		 * FIXME:
		 * Special handling for EOS case: if we sent a 0 length input
		 * buf with EOS set, Venus doesn't return a valid output buffer.
		 * So pick up a random buffer that's with us, and send it to
		 * v4l2 client with EOS flag set.
		 *
		 * This would normally be OK unless client decides to send
		 * frames even after EOS.
		 *
		 * This should be fixed in upcoming versions of firmware
		 */
		if (fill_buf_done->flags1 & HAL_BUFFERFLAG_EOS
			&& fill_buf_done->filled_len1 == 0) {
			struct buf_queue *q = &inst->bufq[CAPTURE_PORT];

			if (!list_empty(&q->vb2_bufq.queued_list)) {
				vb = list_first_entry(&q->vb2_bufq.queued_list,
					struct vb2_buffer, queued_entry);
				vb->v4l2_planes[0].bytesused = 0;
				vb->v4l2_buf.flags |= V4L2_BUF_FLAG_EOS;
				mutex_lock(&q->lock);
				vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
				mutex_unlock(&q->lock);
			}

		}
	}
}

static void  handle_seq_hdr_done(enum command_response cmd, void *data)
{
	struct msm_vidc_cb_data_done *response = data;
	struct msm_vidc_inst *inst;
	struct vb2_buffer *vb;
	struct vidc_hal_fbd *fill_buf_done;
	if (!response) {
		dprintk(VIDC_ERR, "Invalid response from vidc_hal\n");
		return;
	}
	inst = (struct msm_vidc_inst *)response->session_id;
	fill_buf_done = (struct vidc_hal_fbd *)&response->output_done;
	vb = get_vb_from_device_addr(&inst->bufq[CAPTURE_PORT],
		(u32)fill_buf_done->packet_buffer1);
	if (vb)
		vb->v4l2_planes[0].bytesused = fill_buf_done->filled_len1;

	vb->v4l2_buf.flags = V4L2_QCOM_BUF_FLAG_CODECCONFIG;

	dprintk(VIDC_DBG, "Filled length = %d; flags %x\n",
				vb->v4l2_planes[0].bytesused,
				vb->v4l2_buf.flags);
	mutex_lock(&inst->bufq[CAPTURE_PORT].lock);
	vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
	mutex_unlock(&inst->bufq[CAPTURE_PORT].lock);
}

void handle_cmd_response(enum command_response cmd, void *data)
{
	dprintk(VIDC_DBG, "Command response = %d\n", cmd);
	switch (cmd) {
	case SYS_INIT_DONE:
		handle_sys_init_done(cmd, data);
		break;
	case RELEASE_RESOURCE_DONE:
		handle_sys_release_res_done(cmd, data);
		break;
	case SESSION_INIT_DONE:
		handle_session_init_done(cmd, data);
		break;
	case SESSION_PROPERTY_INFO:
		handle_session_prop_info(cmd, data);
		break;
	case SESSION_LOAD_RESOURCE_DONE:
		handle_load_resource_done(cmd, data);
		break;
	case SESSION_START_DONE:
		handle_start_done(cmd, data);
		break;
	case SESSION_ETB_DONE:
		handle_ebd(cmd, data);
		break;
	case SESSION_FTB_DONE:
		handle_fbd(cmd, data);
		break;
	case SESSION_STOP_DONE:
		handle_stop_done(cmd, data);
		break;
	case SESSION_RELEASE_RESOURCE_DONE:
		handle_release_res_done(cmd, data);
		break;
	case SESSION_END_DONE:
		handle_session_close(cmd, data);
		break;
	case VIDC_EVENT_CHANGE:
		handle_event_change(cmd, data);
		break;
	case SESSION_FLUSH_DONE:
		handle_session_flush(cmd, data);
		break;
	case SESSION_GET_SEQ_HDR_DONE:
		handle_seq_hdr_done(cmd, data);
		break;
	case SYS_WATCHDOG_TIMEOUT:
		handle_sys_watchdog_timeout(cmd, data);
		break;
	case SYS_ERROR:
		handle_sys_error(cmd, data);
		break;
	case SESSION_ERROR:
		handle_session_error(cmd, data);
		break;
	case SESSION_RELEASE_BUFFER_DONE:
		handle_session_release_buf_done(cmd, data);
		break;
	default:
		dprintk(VIDC_ERR, "response unhandled\n");
		break;
	}
}

static int msm_comm_scale_clocks(struct msm_vidc_core *core)
{
	int num_mbs_per_sec;
	int rc = 0;
	struct hfi_device *hdev;
	if (!core) {
		dprintk(VIDC_ERR, "%s Invalid args: %p\n", __func__, core);
		return -EINVAL;
	}

	hdev = core->device;
	if (!hdev) {
		dprintk(VIDC_ERR, "%s Invalid device handle: %p\n",
			__func__, hdev);
		return -EINVAL;
	}
	num_mbs_per_sec = msm_comm_get_load(core, MSM_VIDC_ENCODER);
	num_mbs_per_sec += msm_comm_get_load(core, MSM_VIDC_DECODER);

	dprintk(VIDC_INFO, "num_mbs_per_sec = %d\n", num_mbs_per_sec);
	rc = hdev->scale_clocks(hdev->hfi_device_data, num_mbs_per_sec);
	if (rc)
		dprintk(VIDC_ERR, "Failed to set clock rate: %d\n", rc);
	return rc;
}

void msm_comm_scale_clocks_and_bus(struct msm_vidc_inst *inst)
{
	struct msm_vidc_core *core;
	struct hfi_device *hdev;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s Invalid params\n", __func__);
		return;
	}
	core = inst->core;
	hdev = core->device;

	if (msm_comm_scale_clocks(core)) {
		dprintk(VIDC_WARN,
				"Failed to scale clocks. Performance might be impacted\n");
	}
	if (msm_comm_scale_bus(core, inst->session_type, DDR_MEM)) {
		dprintk(VIDC_WARN,
				"Failed to scale DDR bus. Performance might be impacted\n");
	}
	if (hdev->is_ocmem_present(hdev->hfi_device_data)) {
		if (msm_comm_scale_bus(core, inst->session_type,
					OCMEM_MEM))
			dprintk(VIDC_WARN,
					"Failed to scale OCMEM bus. Performance might be impacted\n");
	}
}

static inline unsigned long get_ocmem_requirement(u32 height, u32 width)
{
	int num_mbs = 0;
	num_mbs = GET_NUM_MBS(height, width);
	/*TODO: This should be changes once the numbers are
	 * available from firmware*/
	return 512 * 1024;
}

static int msm_comm_unset_ocmem(struct msm_vidc_core *core)
{
	int rc = 0;
	struct hfi_device *hdev;

	if (!core || !core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}
	hdev = core->device;

	if (core->state == VIDC_CORE_INVALID) {
		dprintk(VIDC_ERR,
				"Core is in bad state. Cannot unset ocmem\n");
		return -EIO;
	}

	init_completion(
		&core->completions[SYS_MSG_INDEX(RELEASE_RESOURCE_DONE)]);

	rc = hdev->unset_ocmem(hdev->hfi_device_data);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to set OCMEM on driver\n");
		goto release_ocmem_failed;
	}
	rc = wait_for_completion_timeout(
		&core->completions[SYS_MSG_INDEX(RELEASE_RESOURCE_DONE)],
		msecs_to_jiffies(HW_RESPONSE_TIMEOUT));
	if (!rc) {
		dprintk(VIDC_ERR, "Wait interrupted or timeout: %d\n", rc);
		rc = -EIO;
	}
release_ocmem_failed:
	return rc;
}

static int msm_comm_init_core_done(struct msm_vidc_inst *inst)
{
	struct msm_vidc_core *core = inst->core;
	unsigned long flags;
	int rc = 0;
	mutex_lock(&core->sync_lock);
	if (core->state >= VIDC_CORE_INIT_DONE) {
		dprintk(VIDC_INFO, "Video core: %d is already in state: %d\n",
				core->id, core->state);
		goto core_already_inited;
	}
	dprintk(VIDC_DBG, "Waiting for SYS_INIT_DONE\n");
	rc = wait_for_completion_timeout(
		&core->completions[SYS_MSG_INDEX(SYS_INIT_DONE)],
		msecs_to_jiffies(HW_RESPONSE_TIMEOUT));
	if (!rc) {
		dprintk(VIDC_ERR, "Wait interrupted or timeout: %d\n", rc);
		rc = -EIO;
		goto exit;
	} else {
		spin_lock_irqsave(&core->lock, flags);
		core->state = VIDC_CORE_INIT_DONE;
		spin_unlock_irqrestore(&core->lock, flags);
	}
	dprintk(VIDC_DBG, "SYS_INIT_DONE!!!\n");
core_already_inited:
	change_inst_state(inst, MSM_VIDC_CORE_INIT_DONE);
	rc = 0;
exit:
	mutex_unlock(&core->sync_lock);
	return rc;
}

static int msm_comm_init_core(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct msm_vidc_core *core = inst->core;
	unsigned long flags;
	struct hfi_device *hdev;

	if (!core || !core->device)
		return -EINVAL;
	hdev = core->device;

	mutex_lock(&core->sync_lock);
	if (core->state >= VIDC_CORE_INIT) {
		dprintk(VIDC_INFO, "Video core: %d is already in state: %d\n",
				core->id, core->state);
		goto core_already_inited;
	}

	rc = msm_comm_scale_bus(core, inst->session_type, DDR_MEM);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to scale DDR bus: %d\n", rc);
		goto fail_scale_bus;
	}

	rc = hdev->load_fw(hdev->hfi_device_data);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to load video firmware\n");
		goto fail_load_fw;
	}
	rc = msm_comm_scale_clocks(core);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to scale clocks: %d\n", rc);
		goto fail_core_init;
	}

	init_completion(&core->completions[SYS_MSG_INDEX(SYS_INIT_DONE)]);
	rc = hdev->core_init(hdev->hfi_device_data);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to init core, id = %d\n", core->id);
		goto fail_core_init;
	}
	spin_lock_irqsave(&core->lock, flags);
	core->state = VIDC_CORE_INIT;
	spin_unlock_irqrestore(&core->lock, flags);
core_already_inited:
	change_inst_state(inst, MSM_VIDC_CORE_INIT);
	mutex_unlock(&core->sync_lock);
	return rc;
fail_core_init:
	hdev->unload_fw(hdev->hfi_device_data);
fail_load_fw:
	msm_comm_unvote_buses(core, DDR_MEM);
fail_scale_bus:
	mutex_unlock(&core->sync_lock);
	return rc;
}

static int msm_vidc_deinit_core(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct msm_vidc_core *core;
	struct hfi_device *hdev;
	unsigned long flags;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}

	core = inst->core;
	hdev = core->device;

	mutex_lock(&core->sync_lock);
	if (core->state == VIDC_CORE_UNINIT) {
		dprintk(VIDC_INFO, "Video core: %d is already in state: %d\n",
				core->id, core->state);
		goto core_already_uninited;
	}
	msm_comm_scale_clocks_and_bus(inst);
	if (list_empty(&core->instances)) {
		if (inst->state != MSM_VIDC_CORE_INVALID)
			msm_comm_unset_ocmem(core);
		hdev->free_ocmem(hdev->hfi_device_data);
		dprintk(VIDC_DBG, "Calling vidc_hal_core_release\n");
		rc = hdev->core_release(hdev->hfi_device_data);
		if (rc) {
			dprintk(VIDC_ERR, "Failed to release core, id = %d\n",
							core->id);
			goto exit;
		}
		spin_lock_irqsave(&core->lock, flags);
		core->state = VIDC_CORE_UNINIT;
		spin_unlock_irqrestore(&core->lock, flags);
		hdev->unload_fw(hdev->hfi_device_data);
		msm_comm_unvote_buses(core, DDR_MEM|OCMEM_MEM);
	}
core_already_uninited:
	change_inst_state(inst, MSM_VIDC_CORE_UNINIT);
exit:
	mutex_unlock(&core->sync_lock);
	return rc;
}

int msm_comm_force_cleanup(struct msm_vidc_inst *inst)
{
	return msm_vidc_deinit_core(inst);
}

static enum hal_domain get_hal_domain(int session_type)
{
	enum hal_domain domain;
	switch (session_type) {
	case MSM_VIDC_ENCODER:
		domain = HAL_VIDEO_DOMAIN_ENCODER;
		break;
	case MSM_VIDC_DECODER:
		domain = HAL_VIDEO_DOMAIN_DECODER;
		break;
	default:
		dprintk(VIDC_ERR, "Wrong domain\n");
		domain = HAL_UNUSED_DOMAIN;
		break;
	}
	return domain;
}

static enum hal_video_codec get_hal_codec_type(int fourcc)
{
	enum hal_video_codec codec;
	dprintk(VIDC_DBG, "codec is 0x%x", fourcc);
	switch (fourcc) {
	case V4L2_PIX_FMT_H264:
	case V4L2_PIX_FMT_H264_NO_SC:
		codec = HAL_VIDEO_CODEC_H264;
		break;
	case V4L2_PIX_FMT_H263:
		codec = HAL_VIDEO_CODEC_H263;
		break;
	case V4L2_PIX_FMT_MPEG1:
		codec = HAL_VIDEO_CODEC_MPEG1;
		break;
	case V4L2_PIX_FMT_MPEG2:
		codec = HAL_VIDEO_CODEC_MPEG2;
		break;
	case V4L2_PIX_FMT_MPEG4:
		codec = HAL_VIDEO_CODEC_MPEG4;
		break;
	case V4L2_PIX_FMT_VC1_ANNEX_G:
	case V4L2_PIX_FMT_VC1_ANNEX_L:
		codec = HAL_VIDEO_CODEC_VC1;
		break;
	case V4L2_PIX_FMT_VP8:
		codec = HAL_VIDEO_CODEC_VP8;
		break;
	case V4L2_PIX_FMT_DIVX_311:
		codec = HAL_VIDEO_CODEC_DIVX_311;
		break;
	case V4L2_PIX_FMT_DIVX:
		codec = HAL_VIDEO_CODEC_DIVX;
		break;
		/*HAL_VIDEO_CODEC_MVC
		  HAL_VIDEO_CODEC_SPARK
		  HAL_VIDEO_CODEC_VP6
		  HAL_VIDEO_CODEC_VP7*/
	default:
		dprintk(VIDC_ERR, "Wrong codec: %d\n", fourcc);
		codec = HAL_UNUSED_CODEC;
	}
	return codec;
}

static int msm_comm_session_init(int flipped_state,
	struct msm_vidc_inst *inst)
{
	int rc = 0;
	int fourcc = 0;
	struct hfi_device *hdev;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}
	hdev = inst->core->device;

	if (IS_ALREADY_IN_STATE(flipped_state, MSM_VIDC_OPEN)) {
		dprintk(VIDC_INFO, "inst: %p is already in state: %d\n",
						inst, inst->state);
		goto exit;
	}
	if (inst->session_type == MSM_VIDC_DECODER) {
		fourcc = inst->fmts[OUTPUT_PORT]->fourcc;
	} else if (inst->session_type == MSM_VIDC_ENCODER) {
		fourcc = inst->fmts[CAPTURE_PORT]->fourcc;
	} else {
		dprintk(VIDC_ERR, "Invalid session\n");
		return -EINVAL;
	}
	init_completion(
		&inst->completions[SESSION_MSG_INDEX(SESSION_INIT_DONE)]);
	inst->session = hdev->session_init(hdev->hfi_device_data, (u32) inst,
					get_hal_domain(inst->session_type),
					get_hal_codec_type(fourcc));
	if (!inst->session) {
		dprintk(VIDC_ERR,
			"Failed to call session init for: %d, %d, %d, %d\n",
			(int)inst->core->device, (int)inst,
			inst->session_type, fourcc);
		goto exit;
	}
	inst->ftb_count = 0;
	change_inst_state(inst, MSM_VIDC_OPEN);
exit:
	return rc;
}

static int msm_vidc_load_resources(int flipped_state,
	struct msm_vidc_inst *inst)
{
	int rc = 0;
	u32 ocmem_sz = 0;
	struct hfi_device *hdev;
	int num_mbs_per_sec = 0;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}
	num_mbs_per_sec = msm_comm_get_load(inst->core, MSM_VIDC_DECODER);
	num_mbs_per_sec += msm_comm_get_load(inst->core, MSM_VIDC_ENCODER);
	if (num_mbs_per_sec > MAX_LOAD) {
		dprintk(VIDC_ERR, "HW is overloaded, needed:%d max: %d\n",
			num_mbs_per_sec, MAX_LOAD);
		return -ENOMEM;
	}
	hdev = inst->core->device;

	if (IS_ALREADY_IN_STATE(flipped_state, MSM_VIDC_LOAD_RESOURCES)) {
		dprintk(VIDC_INFO, "inst: %p is already in state: %d\n",
						inst, inst->state);
		goto exit;
	}
	ocmem_sz = get_ocmem_requirement(inst->prop.height, inst->prop.width);
	rc = msm_comm_scale_bus(inst->core, inst->session_type, OCMEM_MEM);
	if (!rc) {
		mutex_lock(&inst->core->sync_lock);
		rc = hdev->alloc_ocmem(hdev->hfi_device_data, ocmem_sz);
		mutex_unlock(&inst->core->sync_lock);
		if (rc) {
			dprintk(VIDC_WARN,
			"Failed to allocate OCMEM. Performance will be impacted\n");
			msm_comm_unvote_buses(inst->core, OCMEM_MEM);
		}
	} else {
		dprintk(VIDC_WARN,
		"Failed to vote for OCMEM BW. Performance will be impacted\n");
	}
	rc = hdev->session_load_res((void *) inst->session);
	if (rc) {
		dprintk(VIDC_ERR,
			"Failed to send load resources\n");
		goto exit;
	}
	change_inst_state(inst, MSM_VIDC_LOAD_RESOURCES);
exit:
	return rc;
}

static int msm_vidc_start(int flipped_state, struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct hfi_device *hdev;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}

	hdev = inst->core->device;

	if (IS_ALREADY_IN_STATE(flipped_state, MSM_VIDC_START)) {
		dprintk(VIDC_INFO,
			"inst: %p is already in state: %d\n",
			inst, inst->state);
		goto exit;
	}
	init_completion(
		&inst->completions[SESSION_MSG_INDEX(SESSION_START_DONE)]);
	rc = hdev->session_start((void *) inst->session);
	if (rc) {
		dprintk(VIDC_ERR,
			"Failed to send start\n");
		goto exit;
	}
	change_inst_state(inst, MSM_VIDC_START);
exit:
	return rc;
}

static int msm_vidc_stop(int flipped_state, struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct hfi_device *hdev;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}
	hdev = inst->core->device;

	if (IS_ALREADY_IN_STATE(flipped_state, MSM_VIDC_STOP)) {
		dprintk(VIDC_INFO,
			"inst: %p is already in state: %d\n",
			inst, inst->state);
		goto exit;
	}
	dprintk(VIDC_DBG, "Send Stop to hal\n");
	init_completion(
		&inst->completions[SESSION_MSG_INDEX(SESSION_STOP_DONE)]);
	rc = hdev->session_stop((void *) inst->session);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to send stop\n");
		goto exit;
	}
	change_inst_state(inst, MSM_VIDC_STOP);
exit:
	return rc;
}

static int msm_vidc_release_res(int flipped_state, struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct hfi_device *hdev;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}
	hdev = inst->core->device;

	if (IS_ALREADY_IN_STATE(flipped_state, MSM_VIDC_RELEASE_RESOURCES)) {
		dprintk(VIDC_INFO,
			"inst: %p is already in state: %d\n",
			inst, inst->state);
		goto exit;
	}
	dprintk(VIDC_DBG,
		"Send release res to hal\n");
	init_completion(
	&inst->completions[SESSION_MSG_INDEX(SESSION_RELEASE_RESOURCE_DONE)]);
	rc = hdev->session_release_res((void *) inst->session);
	if (rc) {
		dprintk(VIDC_ERR,
			"Failed to send release resources\n");
		goto exit;
	}
	change_inst_state(inst, MSM_VIDC_RELEASE_RESOURCES);
exit:
	return rc;
}

static int msm_comm_session_close(int flipped_state,
			struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct hfi_device *hdev;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid params", __func__);
		return -EINVAL;
	}
	hdev = inst->core->device;
	if (IS_ALREADY_IN_STATE(flipped_state, MSM_VIDC_CLOSE)) {
		dprintk(VIDC_INFO,
			"inst: %p is already in state: %d\n",
						inst, inst->state);
		goto exit;
	}
	dprintk(VIDC_DBG,
		"Send session close to hal\n");
	init_completion(
		&inst->completions[SESSION_MSG_INDEX(SESSION_END_DONE)]);
	rc = hdev->session_end((void *) inst->session);
	if (rc) {
		dprintk(VIDC_ERR,
			"Failed to send close\n");
		goto exit;
	}
	change_inst_state(inst, MSM_VIDC_OPEN);
exit:
	return rc;
}

static int get_flipped_state(int present_state,
	int desired_state)
{
	int flipped_state = present_state;
	if (flipped_state < MSM_VIDC_STOP
			&& desired_state > MSM_VIDC_STOP) {
		flipped_state = MSM_VIDC_STOP + (MSM_VIDC_STOP - flipped_state);
		flipped_state &= 0xFFFE;
		flipped_state = flipped_state - 1;
	} else if (flipped_state > MSM_VIDC_STOP
			&& desired_state < MSM_VIDC_STOP) {
		flipped_state = MSM_VIDC_STOP -
			(flipped_state - MSM_VIDC_STOP + 1);
		flipped_state &= 0xFFFE;
		flipped_state = flipped_state - 1;
	}
	return flipped_state;
}

int msm_comm_try_state(struct msm_vidc_inst *inst, int state)
{
	int rc = 0;
	int flipped_state;
	struct msm_vidc_core *core;
	if (!inst) {
		dprintk(VIDC_ERR,
				"Invalid instance pointer = %p\n", inst);
		return -EINVAL;
	}
	dprintk(VIDC_DBG,
			"Trying to move inst: %p from: 0x%x to 0x%x\n",
			inst, inst->state, state);
	core = inst->core;
	if (!core) {
		dprintk(VIDC_ERR,
				"Invalid core pointer = %p\n", inst);
		return -EINVAL;
	}
	mutex_lock(&inst->sync_lock);
	if (inst->state == MSM_VIDC_CORE_INVALID ||
			core->state == VIDC_CORE_INVALID) {
		dprintk(VIDC_ERR,
				"Core is in bad state can't change the state");
		goto exit;
	}
	flipped_state = get_flipped_state(inst->state, state);
	dprintk(VIDC_DBG,
			"flipped_state = 0x%x\n", flipped_state);
	switch (flipped_state) {
	case MSM_VIDC_CORE_UNINIT_DONE:
	case MSM_VIDC_CORE_INIT:
		rc = msm_comm_init_core(inst);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_CORE_INIT_DONE:
		rc = msm_comm_init_core_done(inst);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_OPEN:
		rc = msm_comm_session_init(flipped_state, inst);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_OPEN_DONE:
		rc = wait_for_state(inst, flipped_state, MSM_VIDC_OPEN_DONE,
			SESSION_INIT_DONE);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_LOAD_RESOURCES:
		rc = msm_vidc_load_resources(flipped_state, inst);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_LOAD_RESOURCES_DONE:
	case MSM_VIDC_START:
		rc = msm_vidc_start(flipped_state, inst);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_START_DONE:
		rc = wait_for_state(inst, flipped_state, MSM_VIDC_START_DONE,
				SESSION_START_DONE);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_STOP:
		rc = msm_vidc_stop(flipped_state, inst);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_STOP_DONE:
		rc = wait_for_state(inst, flipped_state, MSM_VIDC_STOP_DONE,
				SESSION_STOP_DONE);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
		dprintk(VIDC_DBG, "Moving to Stop Done state\n");
	case MSM_VIDC_RELEASE_RESOURCES:
		rc = msm_vidc_release_res(flipped_state, inst);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_RELEASE_RESOURCES_DONE:
		rc = wait_for_state(inst, flipped_state,
			MSM_VIDC_RELEASE_RESOURCES_DONE,
			SESSION_RELEASE_RESOURCE_DONE);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
		dprintk(VIDC_DBG,
				"Moving to release resources done state\n");
	case MSM_VIDC_CLOSE:
		rc = msm_comm_session_close(flipped_state, inst);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_CLOSE_DONE:
		rc = wait_for_state(inst, flipped_state, MSM_VIDC_CLOSE_DONE,
				SESSION_END_DONE);
		if (rc || state <= get_flipped_state(inst->state, state))
			break;
	case MSM_VIDC_CORE_UNINIT:
		dprintk(VIDC_DBG, "Sending core uninit\n");
		rc = msm_vidc_deinit_core(inst);
		if (rc || state == get_flipped_state(inst->state, state))
			break;
	default:
		dprintk(VIDC_ERR, "State not recognized\n");
		rc = -EINVAL;
		break;
	}
exit:
	mutex_unlock(&inst->sync_lock);
	if (rc)
		dprintk(VIDC_ERR,
				"Failed to move from state: %d to %d\n",
				inst->state, state);
	return rc;
}
int msm_comm_qbuf(struct vb2_buffer *vb)
{
	int rc = 0;
	struct vb2_queue *q;
	struct msm_vidc_inst *inst;
	struct vb2_buf_entry *entry;
	struct vidc_frame_data frame_data;
	struct msm_vidc_core *core;
	struct hfi_device *hdev;
	q = vb->vb2_queue;
	inst = q->drv_priv;
	if (!inst || !vb) {
		dprintk(VIDC_ERR, "Invalid input: %p, %p\n", inst, vb);
		return -EINVAL;
	}
	core = inst->core;
	if (!core) {
		dprintk(VIDC_ERR,
			"Invalid input: %p, %p, %p\n", inst, core, vb);
		return -EINVAL;
	}
	hdev = core->device;
	if (!hdev) {
		dprintk(VIDC_ERR, "Invalid input: %p", hdev);
		return -EINVAL;
	}

	if (inst->state == MSM_VIDC_CORE_INVALID ||
		core->state == VIDC_CORE_INVALID) {
		dprintk(VIDC_ERR, "Core is in bad state. Can't Queue\n");
		return -EINVAL;
	}
	if (inst->state != MSM_VIDC_START_DONE) {
			entry = kzalloc(sizeof(*entry), GFP_KERNEL);
			if (!entry) {
				dprintk(VIDC_ERR, "Out of memory\n");
				goto err_no_mem;
			}
			entry->vb = vb;
			mutex_lock(&inst->sync_lock);
			list_add_tail(&entry->list, &inst->pendingq);
			mutex_unlock(&inst->sync_lock);
	} else {
		int64_t time_usec = timeval_to_ns(&vb->v4l2_buf.timestamp);
		do_div(time_usec, NSEC_PER_USEC);
		memset(&frame_data, 0 , sizeof(struct vidc_frame_data));
		frame_data.alloc_len = vb->v4l2_planes[0].length;
		frame_data.filled_len = vb->v4l2_planes[0].bytesused;
		frame_data.device_addr = vb->v4l2_planes[0].m.userptr;
		frame_data.timestamp = time_usec;
		frame_data.flags = 0;
		frame_data.clnt_data = (u32)vb;
		if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			frame_data.buffer_type = HAL_BUFFER_INPUT;
			if (vb->v4l2_buf.flags & V4L2_BUF_FLAG_EOS) {
				frame_data.flags |= HAL_BUFFERFLAG_EOS;
				dprintk(VIDC_DBG,
					"Received EOS on output capability\n");
			}

			if (vb->v4l2_buf.flags &
					V4L2_QCOM_BUF_FLAG_CODECCONFIG) {
				frame_data.flags |= HAL_BUFFERFLAG_CODECCONFIG;
				dprintk(VIDC_DBG,
					"Received CODECCONFIG on output cap\n");
			}
			dprintk(VIDC_DBG,
				"Sending etb to hal: Alloc: %d :filled: %d\n",
				frame_data.alloc_len, frame_data.filled_len);
			rc = hdev->session_etb((void *) inst->session,
					&frame_data);
			if (!rc)
				msm_vidc_debugfs_update(inst,
					MSM_VIDC_DEBUGFS_EVENT_ETB);
			dprintk(VIDC_DBG, "Sent etb to HAL\n");
		} else if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			struct vidc_seq_hdr seq_hdr;
			int extra_idx = 0;
			frame_data.filled_len = 0;
			frame_data.offset = 0;
			frame_data.alloc_len = vb->v4l2_planes[0].length;
			frame_data.buffer_type = HAL_BUFFER_OUTPUT;
			extra_idx =
			EXTRADATA_IDX(inst->fmts[CAPTURE_PORT]->num_planes);
			if (extra_idx && (extra_idx < VIDEO_MAX_PLANES) &&
				vb->v4l2_planes[extra_idx].m.userptr)
				frame_data.extradata_addr =
					vb->v4l2_planes[extra_idx].m.userptr;
			dprintk(VIDC_DBG,
				"Sending ftb to hal: Alloc: %d :filled: %d",
				frame_data.alloc_len, frame_data.filled_len);
			dprintk(VIDC_DBG,
				" extradata_addr: %d\n",
				frame_data.extradata_addr);
			if (!inst->ftb_count &&
			   inst->session_type == MSM_VIDC_ENCODER) {
				seq_hdr.seq_hdr = (u8 *) vb->v4l2_planes[0].
					m.userptr;
				seq_hdr.seq_hdr_len = vb->v4l2_planes[0].length;
				rc = hdev->session_get_seq_hdr((void *)
						inst->session, &seq_hdr);
				if (!rc) {
					inst->vb2_seq_hdr = vb;
					dprintk(VIDC_DBG, "Seq_hdr: %p\n",
						inst->vb2_seq_hdr);
				}
			} else {
				rc = hdev->session_ftb((void *)
					inst->session, &frame_data);
			if (!rc)
				msm_vidc_debugfs_update(inst,
					MSM_VIDC_DEBUGFS_EVENT_FTB);
			}
			inst->ftb_count++;
		} else {
			dprintk(VIDC_ERR,
				"This capability is not supported: %d\n",
				q->type);
			rc = -EINVAL;
		}
	}
	if (rc)
		dprintk(VIDC_ERR, "Failed to queue buffer\n");
err_no_mem:
	return rc;
}

int msm_comm_try_get_bufreqs(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct hfi_device *hdev;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}
	hdev = inst->core->device;

	mutex_lock(&inst->sync_lock);
	if (inst->state < MSM_VIDC_OPEN_DONE || inst->state >= MSM_VIDC_CLOSE) {
		dprintk(VIDC_ERR,
			"Not in proper state to query buffer requirements\n");
		rc = -EAGAIN;
		goto exit;
	}
	init_completion(
		&inst->completions[SESSION_MSG_INDEX(SESSION_PROPERTY_INFO)]);
	rc = hdev->session_get_buf_req((void *) inst->session);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to get property\n");
		goto exit;
	}
	rc = wait_for_completion_timeout(
		&inst->completions[SESSION_MSG_INDEX(SESSION_PROPERTY_INFO)],
		msecs_to_jiffies(HW_RESPONSE_TIMEOUT));
	if (!rc) {
		dprintk(VIDC_ERR,
			"Wait interrupted or timeout: %d\n", rc);
		rc = -EIO;
		goto exit;
	}
	rc = 0;
exit:
	mutex_unlock(&inst->sync_lock);
	return rc;
}
int msm_comm_release_scratch_buffers(struct msm_vidc_inst *inst)
{
	struct msm_smem *handle;
	struct list_head *ptr, *next;
	struct internal_buf *buf;
	struct vidc_buffer_addr_info buffer_info;
	int rc = 0;
	unsigned long flags;
	struct msm_vidc_core *core;
	struct hfi_device *hdev;
	if (!inst) {
		dprintk(VIDC_ERR,
				"Invalid instance pointer = %p\n", inst);
		return -EINVAL;
	}
	core = inst->core;
	if (!core) {
		dprintk(VIDC_ERR,
				"Invalid core pointer = %p\n", core);
		return -EINVAL;
	}
	hdev = core->device;
	if (!hdev) {
		dprintk(VIDC_ERR, "Invalid device pointer = %p\n", hdev);
		return -EINVAL;
	}
	spin_lock_irqsave(&inst->lock, flags);
	if (!list_empty(&inst->internalbufs)) {
		list_for_each_safe(ptr, next, &inst->internalbufs) {
			buf = list_entry(ptr, struct internal_buf,
					list);
			handle = buf->handle;
			buffer_info.buffer_size = handle->size;
			buffer_info.buffer_type = HAL_BUFFER_INTERNAL_SCRATCH;
			buffer_info.num_buffers = 1;
			buffer_info.align_device_addr = handle->device_addr;
			if (inst->state != MSM_VIDC_CORE_INVALID &&
					core->state != VIDC_CORE_INVALID) {
				buffer_info.response_required = true;
				init_completion(
				   &inst->completions[SESSION_MSG_INDEX
				   (SESSION_RELEASE_BUFFER_DONE)]);
				rc = hdev->session_release_buffers(
						(void *) inst->session,
							&buffer_info);
				if (rc)
					dprintk(VIDC_WARN,
						"Rel scrtch buf fail:0x%x, %d",
						buffer_info.align_device_addr,
						buffer_info.buffer_size);
				spin_unlock_irqrestore(&inst->lock, flags);
				rc = wait_for_sess_signal_receipt(inst,
					SESSION_RELEASE_BUFFER_DONE);
				spin_lock_irqsave(&inst->lock, flags);
			}
			list_del(&buf->list);
			spin_unlock_irqrestore(&inst->lock, flags);
			msm_smem_free(inst->mem_client, buf->handle);
			kfree(buf);
			spin_lock_irqsave(&inst->lock, flags);
		}
	}
	spin_unlock_irqrestore(&inst->lock, flags);
	return rc;
}

int msm_comm_release_persist_buffers(struct msm_vidc_inst *inst)
{
	struct msm_smem *handle;
	struct list_head *ptr, *next;
	struct internal_buf *buf;
	struct vidc_buffer_addr_info buffer_info;
	int rc = 0;
	unsigned long flags;
	struct msm_vidc_core *core;
	struct hfi_device *hdev;
	if (!inst) {
		dprintk(VIDC_ERR,
				"Invalid instance pointer = %p\n", inst);
		return -EINVAL;
	}
	core = inst->core;
	if (!core) {
		dprintk(VIDC_ERR,
				"Invalid core pointer = %p\n", core);
		return -EINVAL;
	}
	hdev = core->device;
	if (!hdev) {
		dprintk(VIDC_ERR, "Invalid device pointer = %p\n", hdev);
		return -EINVAL;
	}
	spin_lock_irqsave(&inst->lock, flags);
	if (!list_empty(&inst->persistbufs)) {
		list_for_each_safe(ptr, next, &inst->persistbufs) {
			buf = list_entry(ptr, struct internal_buf,
					list);
			handle = buf->handle;
			buffer_info.buffer_size = handle->size;
			buffer_info.buffer_type = HAL_BUFFER_INTERNAL_PERSIST;
			buffer_info.num_buffers = 1;
			buffer_info.align_device_addr = handle->device_addr;
			if (inst->state != MSM_VIDC_CORE_INVALID &&
					core->state != VIDC_CORE_INVALID) {
				buffer_info.response_required = true;
				init_completion(
				   &inst->completions[SESSION_MSG_INDEX
				   (SESSION_RELEASE_BUFFER_DONE)]);
				rc = hdev->session_release_buffers(
						(void *) inst->session,
							&buffer_info);
				if (rc)
					dprintk(VIDC_WARN,
						"Rel prst buf fail:0x%x, %d",
						buffer_info.align_device_addr,
						buffer_info.buffer_size);
				spin_unlock_irqrestore(&inst->lock, flags);
				rc = wait_for_sess_signal_receipt(inst,
					SESSION_RELEASE_BUFFER_DONE);
				spin_lock_irqsave(&inst->lock, flags);
			}
			list_del(&buf->list);
			spin_unlock_irqrestore(&inst->lock, flags);
			msm_smem_free(inst->mem_client, buf->handle);
			kfree(buf);
			spin_lock_irqsave(&inst->lock, flags);
		}
	}
	spin_unlock_irqrestore(&inst->lock, flags);
	return rc;
}

int msm_comm_try_set_prop(struct msm_vidc_inst *inst,
	enum hal_property ptype, void *pdata)
{
	int rc = 0;
	struct hfi_device *hdev;
	if (!inst) {
		dprintk(VIDC_ERR, "Invalid input: %p\n", inst);
		return -EINVAL;
	}

	if (!inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}
	hdev = inst->core->device;

	mutex_lock(&inst->sync_lock);
	if (inst->state < MSM_VIDC_OPEN_DONE || inst->state >= MSM_VIDC_CLOSE) {
		dprintk(VIDC_ERR, "Not in proper state to set property\n");
		rc = -EAGAIN;
		goto exit;
	}
	rc = hdev->session_set_property((void *)inst->session,
			ptype, pdata);
	if (rc)
		dprintk(VIDC_ERR, "Failed to set hal property for framesize\n");
exit:
	mutex_unlock(&inst->sync_lock);
	return rc;
}

int msm_comm_set_scratch_buffers(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct msm_smem *handle;
	struct internal_buf *binfo;
	struct vidc_buffer_addr_info buffer_info;
	unsigned long flags;
	int domain;
	unsigned long smem_flags = 0;
	struct hal_buffer_requirements *scratch_buf;
	int i;
	struct hfi_device *hdev;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}

	hdev = inst->core->device;

	scratch_buf =
		&inst->buff_req.buffer[HAL_BUFFER_INTERNAL_SCRATCH];
	dprintk(VIDC_DBG,
		"scratch: num = %d, size = %d\n",
		scratch_buf->buffer_count_actual,
		scratch_buf->buffer_size);
	if (msm_comm_release_scratch_buffers(inst))
		dprintk(VIDC_WARN, "Failed to release scratch buffers\n");
	if (inst->mode == VIDC_SECURE) {
		domain = hdev->get_domain(hdev->hfi_device_data, CP_MAP);
		smem_flags |= SMEM_SECURE;
	} else
		domain = hdev->get_domain(hdev->hfi_device_data, NS_MAP);

	if (scratch_buf->buffer_size) {
		for (i = 0; i < scratch_buf->buffer_count_actual;
				i++) {
			handle = msm_smem_alloc(inst->mem_client,
				scratch_buf->buffer_size, 1, smem_flags,
				domain, 0, 0);
			if (!handle) {
				dprintk(VIDC_ERR,
					"Failed to allocate scratch memory\n");
				rc = -ENOMEM;
				goto err_no_mem;
			}
			binfo = kzalloc(sizeof(*binfo), GFP_KERNEL);
			if (!binfo) {
				dprintk(VIDC_ERR, "Out of memory\n");
				rc = -ENOMEM;
				goto fail_kzalloc;
			}
			binfo->handle = handle;
			buffer_info.buffer_size = scratch_buf->buffer_size;
			buffer_info.buffer_type = HAL_BUFFER_INTERNAL_SCRATCH;
			buffer_info.num_buffers = 1;
			buffer_info.align_device_addr = handle->device_addr;
			dprintk(VIDC_DBG, "Scratch buffer address: %x",
					buffer_info.align_device_addr);
			rc = hdev->session_set_buffers(
					(void *) inst->session,	&buffer_info);
			if (rc) {
				dprintk(VIDC_ERR,
					"vidc_hal_session_set_buffers failed");
				goto fail_set_buffers;
			}
			spin_lock_irqsave(&inst->lock, flags);
			list_add_tail(&binfo->list, &inst->internalbufs);
			spin_unlock_irqrestore(&inst->lock, flags);
		}
	}
	return rc;
fail_set_buffers:
	kfree(binfo);
fail_kzalloc:
	msm_smem_free(inst->mem_client, handle);
err_no_mem:
	return rc;
}

int msm_comm_set_persist_buffers(struct msm_vidc_inst *inst)
{
	int rc = 0;
	struct msm_smem *handle;
	struct internal_buf *binfo;
	struct vidc_buffer_addr_info buffer_info;
	unsigned long flags;
	unsigned long smem_flags = 0;
	int domain;
	struct hal_buffer_requirements *persist_buf;
	int i;
	struct hfi_device *hdev;

	if (!inst || !inst->core || !inst->core->device) {
		dprintk(VIDC_ERR, "%s invalid parameters", __func__);
		return -EINVAL;
	}

	hdev = inst->core->device;

	persist_buf =
		&inst->buff_req.buffer[HAL_BUFFER_INTERNAL_PERSIST];
	dprintk(VIDC_DBG,
		"persist: num = %d, size = %d\n",
		persist_buf->buffer_count_actual,
		persist_buf->buffer_size);
	if (!list_empty(&inst->persistbufs)) {
		dprintk(VIDC_ERR,
			"Persist buffers already allocated\n");
		return rc;
	}

	if (inst->mode == VIDC_SECURE) {
		domain = hdev->get_domain(hdev->hfi_device_data, CP_MAP);
		flags |= SMEM_SECURE;
	} else
		domain = hdev->get_domain(hdev->hfi_device_data, NS_MAP);

	if (persist_buf->buffer_size) {
		for (i = 0;	i <	persist_buf->buffer_count_actual; i++) {
			handle = msm_smem_alloc(inst->mem_client,
				persist_buf->buffer_size, 1, smem_flags,
				domain, 0, 0);
			if (!handle) {
				dprintk(VIDC_ERR,
					"Failed to allocate persist memory\n");
				rc = -ENOMEM;
				goto err_no_mem;
			}
			binfo = kzalloc(sizeof(*binfo), GFP_KERNEL);
			if (!binfo) {
				dprintk(VIDC_ERR, "Out of memory\n");
				rc = -ENOMEM;
				goto fail_kzalloc;
			}
			binfo->handle = handle;
			buffer_info.buffer_size = persist_buf->buffer_size;
			buffer_info.buffer_type = HAL_BUFFER_INTERNAL_PERSIST;
			buffer_info.num_buffers = 1;
			buffer_info.align_device_addr = handle->device_addr;
			dprintk(VIDC_DBG, "Persist buffer address: %x",
					buffer_info.align_device_addr);
			rc = hdev->session_set_buffers(
				(void *) inst->session, &buffer_info);
			if (rc) {
				dprintk(VIDC_ERR,
					"vidc_hal_session_set_buffers failed");
				goto fail_set_buffers;
			}
			spin_lock_irqsave(&inst->lock, flags);
			list_add_tail(&binfo->list, &inst->persistbufs);
			spin_unlock_irqrestore(&inst->lock, flags);
		}
	}
	return rc;
fail_set_buffers:
	kfree(binfo);
fail_kzalloc:
	msm_smem_free(inst->mem_client, handle);
err_no_mem:
	return rc;
}

static void msm_comm_flush_in_invalid_state(struct msm_vidc_inst *inst)
{
	struct v4l2_event dqevent = {0};
	struct list_head *ptr, *next;
	struct vb2_buffer *vb;
	if (!list_empty(&inst->bufq[CAPTURE_PORT].
				vb2_bufq.queued_list)) {
		list_for_each_safe(ptr, next,
				&inst->bufq[CAPTURE_PORT].
				vb2_bufq.queued_list) {
			vb = container_of(ptr,
					struct vb2_buffer,
					queued_entry);
			if (vb) {
				vb->v4l2_planes[0].bytesused = 0;
				mutex_lock(&inst->bufq[CAPTURE_PORT].lock);
				vb2_buffer_done(vb,
						VB2_BUF_STATE_DONE);
				mutex_unlock(&inst->bufq[CAPTURE_PORT].lock);
			}
		}
	}
	if (!list_empty(&inst->bufq[OUTPUT_PORT].
				vb2_bufq.queued_list)) {
		list_for_each_safe(ptr, next,
				&inst->bufq[OUTPUT_PORT].
				vb2_bufq.queued_list) {
			vb = container_of(ptr,
					struct vb2_buffer,
					queued_entry);
			if (vb) {
				vb->v4l2_planes[0].bytesused = 0;
				mutex_lock(&inst->bufq[OUTPUT_PORT].lock);
				vb2_buffer_done(vb,
						VB2_BUF_STATE_DONE);
				mutex_unlock(&inst->bufq[OUTPUT_PORT].lock);
			}
		}
	}
	dqevent.type = V4L2_EVENT_MSM_VIDC_FLUSH_DONE;
	dqevent.id = 0;
	v4l2_event_queue_fh(&inst->event_handler, &dqevent);
	return;
}
int msm_comm_flush(struct msm_vidc_inst *inst, u32 flags)
{
	int rc =  0;
	bool ip_flush = false;
	bool op_flush = false;
	struct list_head *ptr, *next;
	struct vb2_buf_entry *temp;
	struct mutex *lock;
	struct msm_vidc_core *core;
	struct hfi_device *hdev;
	if (!inst) {
		dprintk(VIDC_ERR,
				"Invalid instance pointer = %p\n", inst);
		return -EINVAL;
	}
	core = inst->core;
	if (!core) {
		dprintk(VIDC_ERR,
				"Invalid core pointer = %p\n", core);
		return -EINVAL;
	}
	hdev = core->device;
	if (!hdev) {
		dprintk(VIDC_ERR, "Invalid device pointer = %p", hdev);
		return -EINVAL;
	}

	ip_flush = flags & V4L2_QCOM_CMD_FLUSH_OUTPUT;
	op_flush = flags & V4L2_QCOM_CMD_FLUSH_CAPTURE;

	if (ip_flush && !op_flush) {
		dprintk(VIDC_INFO, "Input only flush not supported\n");
		return 0;
	}
	if (inst->state == MSM_VIDC_CORE_INVALID ||
			core->state == VIDC_CORE_INVALID) {
		dprintk(VIDC_ERR,
				"Core %p and inst %p are in bad state\n",
					core, inst);
		msm_comm_flush_in_invalid_state(inst);
	}

	mutex_lock(&inst->sync_lock);
	if (inst->in_reconfig && !ip_flush && op_flush) {
		if (!list_empty(&inst->pendingq)) {
			/*Execution can never reach here since port reconfig
			 * wont happen unless pendingq is emptied out
			 * (both pendingq and flush being secured with same
			 * lock). Printing a message here incase this breaks.*/
			dprintk(VIDC_WARN,
			"FLUSH BUG: Pending q not empty! It should be empty\n");
		}
		rc = hdev->session_flush(inst->session,
				HAL_FLUSH_OUTPUT);
	} else {
		if (!list_empty(&inst->pendingq)) {
			/*If flush is called after queueing buffers but before
			 * streamon driver should flush the pending queue*/
			list_for_each_safe(ptr, next, &inst->pendingq) {
				temp =
				list_entry(ptr, struct vb2_buf_entry, list);
				if (temp->vb->v4l2_buf.type ==
					V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
					lock = &inst->bufq[CAPTURE_PORT].lock;
				else
					lock = &inst->bufq[OUTPUT_PORT].lock;
				mutex_lock(lock);
				vb2_buffer_done(temp->vb, VB2_BUF_STATE_DONE);
				mutex_unlock(lock);
				list_del(&temp->list);
				kfree(temp);
			}
		}
		rc = hdev->session_flush(inst->session,
				HAL_FLUSH_ALL);
	}
	mutex_unlock(&inst->sync_lock);
	return rc;
}


enum hal_extradata_id msm_comm_get_hal_extradata_index(
	enum v4l2_mpeg_vidc_extradata index)
{
	int ret = 0;
	switch (index) {
	case V4L2_MPEG_VIDC_EXTRADATA_NONE:
		ret = HAL_EXTRADATA_NONE;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_MB_QUANTIZATION:
		ret = HAL_EXTRADATA_MB_QUANTIZATION;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_INTERLACE_VIDEO:
		ret = HAL_EXTRADATA_INTERLACE_VIDEO;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_VC1_FRAMEDISP:
		ret = HAL_EXTRADATA_VC1_FRAMEDISP;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_VC1_SEQDISP:
		ret = HAL_EXTRADATA_VC1_SEQDISP;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_TIMESTAMP:
		ret = HAL_EXTRADATA_TIMESTAMP;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_S3D_FRAME_PACKING:
		ret = HAL_EXTRADATA_S3D_FRAME_PACKING;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_FRAME_RATE:
		ret = HAL_EXTRADATA_FRAME_RATE;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_PANSCAN_WINDOW:
		ret = HAL_EXTRADATA_PANSCAN_WINDOW;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_RECOVERY_POINT_SEI:
		ret = HAL_EXTRADATA_RECOVERY_POINT_SEI;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_CLOSED_CAPTION_UD:
		ret = HAL_EXTRADATA_CLOSED_CAPTION_UD;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_AFD_UD:
		ret = HAL_EXTRADATA_AFD_UD;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_MULTISLICE_INFO:
		ret = HAL_EXTRADATA_MULTISLICE_INFO;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_NUM_CONCEALED_MB:
		ret = HAL_EXTRADATA_NUM_CONCEALED_MB;
		break;
	case V4L2_MPEG_VIDC_EXTRADATA_METADATA_FILLER:
		ret = HAL_EXTRADATA_METADATA_FILLER;
		break;
	default:
		dprintk(VIDC_WARN, "Extradata not found: %d\n", index);
		break;
	}
	return ret;
};

int msm_vidc_trigger_ssr(struct msm_vidc_core *core,
	enum hal_ssr_trigger_type type)
{
	int rc = 0;
	struct hfi_device *hdev;
	if (!core && !core->device) {
		dprintk(VIDC_WARN, "Invalid parameters: %p\n", core);
		return -EINVAL;
	}
	hdev = core->device;
	if (core->state == VIDC_CORE_INIT_DONE)
		rc = hdev->core_trigger_ssr(hdev->hfi_device_data, type);
	return rc;
}
