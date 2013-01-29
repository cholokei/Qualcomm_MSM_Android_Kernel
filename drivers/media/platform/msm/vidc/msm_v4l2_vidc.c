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

#include <linux/module.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/ioctl.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <mach/iommu.h>
#include <mach/iommu_domains.h>
#include <media/msm_vidc.h>
#include "msm_vidc_internal.h"
#include "msm_vidc_debug.h"
#include "vidc_hfi_api.h"
#include "msm_smem.h"
#include "venus_hfi.h"

#define BASE_DEVICE_NUMBER 32

struct msm_vidc_drv *vidc_driver;

struct buffer_info {
	struct list_head list;
	int type;
	int num_planes;
	int fd[VIDEO_MAX_PLANES];
	int buff_off[VIDEO_MAX_PLANES];
	int size[VIDEO_MAX_PLANES];
	u32 uvaddr[VIDEO_MAX_PLANES];
	u32 device_addr[VIDEO_MAX_PLANES];
	struct msm_smem *handle[VIDEO_MAX_PLANES];
};

struct msm_v4l2_vid_inst {
	struct msm_vidc_inst *vidc_inst;
	void *mem_client;
	struct list_head registered_bufs;
};

static inline struct msm_vidc_inst *get_vidc_inst(struct file *filp, void *fh)
{
	return container_of(filp->private_data,
					struct msm_vidc_inst, event_handler);
}

static inline struct msm_v4l2_vid_inst *get_v4l2_inst(struct file *filp,
			void *fh)
{
	struct msm_vidc_inst *vidc_inst;
	vidc_inst = container_of(filp->private_data,
			struct msm_vidc_inst, event_handler);
	return (struct msm_v4l2_vid_inst *)vidc_inst->priv;
}

struct buffer_info *get_registered_buf(struct list_head *list,
				int fd, u32 buff_off, u32 size, int *plane)
{
	struct buffer_info *temp;
	struct buffer_info *ret = NULL;
	int i;
	if (!list || fd < 0 || !plane) {
		dprintk(VIDC_ERR, "Invalid input\n");
		goto err_invalid_input;
	}
	*plane = 0;
	if (!list_empty(list)) {
		list_for_each_entry(temp, list, list) {
			for (i = 0; (i < temp->num_planes)
				&& (i < VIDEO_MAX_PLANES); i++) {
				if (temp && temp->fd[i] == fd &&
						(CONTAINS(temp->buff_off[i],
						temp->size[i], buff_off)
						 || CONTAINS(buff_off,
						 size, temp->buff_off[i])
						 || OVERLAPS(buff_off, size,
						 temp->buff_off[i],
						 temp->size[i]))) {
					dprintk(VIDC_DBG,
							"This memory region is already mapped\n");
					ret = temp;
					*plane = i;
					break;
				}
			}
			if (ret)
				break;
		}
	}
err_invalid_input:
	return ret;
}

struct buffer_info *get_same_fd_buffer(struct list_head *list,
		int fd, int *plane)
{
	struct buffer_info *temp;
	struct buffer_info *ret = NULL;
	int i;
	if (!list || fd < 0 || !plane) {
		dprintk(VIDC_ERR, "Invalid input\n");
		goto err_invalid_input;
	}
	*plane = 0;
	if (!list_empty(list)) {
		list_for_each_entry(temp, list, list) {
			for (i = 0; (i < temp->num_planes)
				&& (i < VIDEO_MAX_PLANES); i++) {
				if (temp && temp->fd[i] == fd)  {
					dprintk(VIDC_INFO,
					"Found same fd buffer\n");
					ret = temp;
					*plane = i;
					break;
				}
			}
			if (ret)
				break;
		}
	}
err_invalid_input:
	return ret;
}
static u32 device_to_uvaddr(struct list_head *list, u32 device_addr)
{
	struct buffer_info *temp;
	u32 uvaddr = 0;
	int i;
	if (!list || !device_addr) {
		dprintk(VIDC_ERR, "Invalid input\n");
		goto err_invalid_input;
	}
	if (!list_empty(list)) {
		list_for_each_entry(temp, list, list) {
			for (i = 0; (i < temp->num_planes)
				&& (i < VIDEO_MAX_PLANES); i++) {
				if (temp && temp->device_addr[i]
						== device_addr)  {
					dprintk(VIDC_INFO,
					"Found same fd buffer\n");
					uvaddr = temp->uvaddr[i];
					break;
				}
			}
			if (uvaddr)
				break;
		}
	}
err_invalid_input:
	return uvaddr;
}

static int msm_v4l2_open(struct file *filp)
{
	int rc = 0;
	struct video_device *vdev = video_devdata(filp);
	struct msm_video_device *vid_dev =
		container_of(vdev, struct msm_video_device, vdev);
	struct msm_vidc_core *core = video_drvdata(filp);
	struct msm_v4l2_vid_inst *v4l2_inst = kzalloc(sizeof(*v4l2_inst),
						GFP_KERNEL);
	if (!v4l2_inst) {
		dprintk(VIDC_ERR,
			"Failed to allocate memory for this instance\n");
		rc = -ENOMEM;
		goto fail_nomem;
	}
	v4l2_inst->mem_client = msm_smem_new_client(SMEM_ION);
	if (!v4l2_inst->mem_client) {
		dprintk(VIDC_ERR, "Failed to create memory client\n");
		rc = -ENOMEM;
		goto fail_mem_client;
	}

	v4l2_inst->vidc_inst = msm_vidc_open(core->id, vid_dev->type);
	if (!v4l2_inst->vidc_inst) {
		dprintk(VIDC_ERR,
		"Failed to create video instance, core: %d, type = %d\n",
		core->id, vid_dev->type);
		rc = -ENOMEM;
		goto fail_open;
	}
	INIT_LIST_HEAD(&v4l2_inst->registered_bufs);
	v4l2_inst->vidc_inst->priv = v4l2_inst;
	clear_bit(V4L2_FL_USES_V4L2_FH, &vdev->flags);
	filp->private_data = &(v4l2_inst->vidc_inst->event_handler);
	return rc;
fail_open:
	msm_smem_delete_client(v4l2_inst->mem_client);
fail_mem_client:
	kfree(v4l2_inst);
fail_nomem:
	return rc;
}
static int msm_v4l2_release_output_buffers(struct msm_v4l2_vid_inst *v4l2_inst)
{
	struct list_head *ptr, *next;
	struct buffer_info *bi;
	struct v4l2_buffer buffer_info;
	struct v4l2_plane plane[VIDEO_MAX_PLANES];
	int rc = 0;
	int i;
	list_for_each_safe(ptr, next, &v4l2_inst->registered_bufs) {
		bi = list_entry(ptr, struct buffer_info, list);
		if (bi->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			buffer_info.type = bi->type;
			for (i = 0; (i < bi->num_planes)
				&& (i < VIDEO_MAX_PLANES); i++) {
				plane[i].reserved[0] = bi->fd[i];
				plane[i].reserved[1] = bi->buff_off[i];
				plane[i].length = bi->size[i];
				plane[i].m.userptr = bi->device_addr[i];
				buffer_info.m.planes = plane;
				dprintk(VIDC_DBG,
					"Releasing buffer: %d, %d, %d\n",
					buffer_info.m.planes[i].reserved[0],
					buffer_info.m.planes[i].reserved[1],
					buffer_info.m.planes[i].length);
			}
			buffer_info.length = bi->num_planes;
			rc = msm_vidc_release_buf(v4l2_inst->vidc_inst,
					&buffer_info);
			if (rc)
				dprintk(VIDC_ERR,
					"Failed Release buffer: %d, %d, %d\n",
					buffer_info.m.planes[0].reserved[0],
					buffer_info.m.planes[0].reserved[1],
					buffer_info.m.planes[0].length);
			list_del(&bi->list);
			for (i = 0; i < bi->num_planes; i++) {
				if (bi->handle[i])
					msm_smem_free(v4l2_inst->mem_client,
							bi->handle[i]);
			}
			kfree(bi);
		}
	}
	return rc;
}

static int msm_v4l2_close(struct file *filp)
{
	int rc = 0;
	struct list_head *ptr, *next;
	struct buffer_info *bi;
	struct msm_vidc_inst *vidc_inst;
	struct msm_v4l2_vid_inst *v4l2_inst;
	int i;
	vidc_inst = get_vidc_inst(filp, NULL);
	v4l2_inst = get_v4l2_inst(filp, NULL);
	rc = msm_v4l2_release_output_buffers(v4l2_inst);
	if (rc)
		dprintk(VIDC_WARN,
			"Failed in %s for release output buffers\n", __func__);
	list_for_each_safe(ptr, next, &v4l2_inst->registered_bufs) {
		bi = list_entry(ptr, struct buffer_info, list);
		if (bi->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			list_del(&bi->list);
			for (i = 0; (i < bi->num_planes)
				&& (i < VIDEO_MAX_PLANES); i++) {
				if (bi->handle[i])
					msm_smem_free(v4l2_inst->mem_client,
							bi->handle[i]);
			}
			kfree(bi);
		}
	}
	msm_smem_delete_client(v4l2_inst->mem_client);
	rc = msm_vidc_close(vidc_inst);
	kfree(v4l2_inst);
	return rc;
}

static int msm_v4l2_querycap(struct file *filp, void *fh,
			struct v4l2_capability *cap)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(filp, fh);
	return msm_vidc_querycap((void *)vidc_inst, cap);
}

int msm_v4l2_enum_fmt(struct file *file, void *fh,
					struct v4l2_fmtdesc *f)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	return msm_vidc_enum_fmt((void *)vidc_inst, f);
}

int msm_v4l2_s_fmt(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	return msm_vidc_s_fmt((void *)vidc_inst, f);
}

int msm_v4l2_g_fmt(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	return msm_vidc_g_fmt((void *)vidc_inst, f);
}

int msm_v4l2_s_ctrl(struct file *file, void *fh,
					struct v4l2_control *a)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	return msm_vidc_s_ctrl((void *)vidc_inst, a);
}

int msm_v4l2_g_ctrl(struct file *file, void *fh,
					struct v4l2_control *a)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	return msm_vidc_g_ctrl((void *)vidc_inst, a);
}

int msm_v4l2_reqbufs(struct file *file, void *fh,
				struct v4l2_requestbuffers *b)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	struct msm_v4l2_vid_inst *v4l2_inst;
	int rc = 0;
	v4l2_inst = get_v4l2_inst(file, NULL);
	if (b->count == 0)
		rc = msm_v4l2_release_output_buffers(v4l2_inst);
	if (rc)
		dprintk(VIDC_WARN,
			"Failed in %s for release output buffers\n", __func__);
	return msm_vidc_reqbufs((void *)vidc_inst, b);
}

int msm_v4l2_prepare_buf(struct file *file, void *fh,
				struct v4l2_buffer *b)
{
	struct msm_smem *handle = NULL;
	struct buffer_info *binfo;
	struct buffer_info *temp;
	struct msm_vidc_inst *vidc_inst;
	struct msm_v4l2_vid_inst *v4l2_inst;
	int plane = 0;
	int i, rc = 0;
	int smem_flags = 0;
	int domain;
	struct hfi_device *hdev;

	vidc_inst = get_vidc_inst(file, fh);
	v4l2_inst = get_v4l2_inst(file, fh);
	if (!v4l2_inst || !vidc_inst || !vidc_inst->core
		|| !vidc_inst->core->device) {
		rc = -EINVAL;
		goto exit;
	}

	hdev = vidc_inst->core->device;

	if (!v4l2_inst->mem_client) {
		dprintk(VIDC_ERR, "Failed to get memory client\n");
		rc = -ENOMEM;
		goto exit;
	}
	binfo = kzalloc(sizeof(*binfo), GFP_KERNEL);
	if (!binfo) {
		dprintk(VIDC_ERR, "Out of memory\n");
		rc = -ENOMEM;
		goto exit;
	}
	if (b->length > VIDEO_MAX_PLANES) {
		dprintk(VIDC_ERR, "Num planes exceeds max: %d, %d\n",
			b->length, VIDEO_MAX_PLANES);
		rc = -EINVAL;
		goto exit;
	}
	for (i = 0; i < b->length; ++i) {
		smem_flags = 0;
		if (EXTRADATA_IDX(b->length) &&
			(i == EXTRADATA_IDX(b->length)) &&
			!b->m.planes[i].length) {
			continue;
		}
		temp = get_registered_buf(&v4l2_inst->registered_bufs,
				b->m.planes[i].reserved[0],
				b->m.planes[i].reserved[1],
				b->m.planes[i].length, &plane);
		if (temp) {
			dprintk(VIDC_DBG,
				"This memory region has already been prepared\n");
			rc = -EINVAL;
			kfree(binfo);
			goto exit;
		}
		if ((vidc_inst->mode == VIDC_SECURE)
				&& (!EXTRADATA_IDX(b->length)
					|| (i != EXTRADATA_IDX(b->length)))) {
			smem_flags |= SMEM_SECURE;
			domain = hdev->get_domain(hdev->hfi_device_data,
						  CP_MAP);
		} else
			domain = hdev->get_domain(hdev->hfi_device_data,
						  NS_MAP);

		if (b->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			smem_flags |= SMEM_INPUT;

		temp = get_same_fd_buffer(&v4l2_inst->registered_bufs,
				b->m.planes[i].reserved[0], &plane);

		if (temp) {
			binfo->type = b->type;
			binfo->fd[i] = b->m.planes[i].reserved[0];
			binfo->buff_off[i] = b->m.planes[i].reserved[1];
			binfo->size[i] = b->m.planes[i].length;
			binfo->uvaddr[i] = b->m.planes[i].m.userptr;
			binfo->device_addr[i] =
			temp->handle[plane]->device_addr + binfo->buff_off[i];
			binfo->handle[i] = NULL;
		} else {
			handle = msm_smem_user_to_kernel(v4l2_inst->mem_client,
			b->m.planes[i].reserved[0],
			b->m.planes[i].reserved[1],
			domain,	0, smem_flags);
			if (!handle) {
				dprintk(VIDC_ERR,
					"Failed to get device buffer address\n");
				kfree(binfo);
				goto exit;
			}
			binfo->type = b->type;
			binfo->fd[i] = b->m.planes[i].reserved[0];
			binfo->buff_off[i] = b->m.planes[i].reserved[1];
			binfo->size[i] = b->m.planes[i].length;
			binfo->uvaddr[i] = b->m.planes[i].m.userptr;
			binfo->device_addr[i] =
				handle->device_addr + binfo->buff_off[i];
			binfo->handle[i] = handle;
			dprintk(VIDC_DBG, "Registering buffer: %d, %d, %d\n",
					b->m.planes[i].reserved[0],
					b->m.planes[i].reserved[1],
					b->m.planes[i].length);
		}
		b->m.planes[i].m.userptr = binfo->device_addr[i];
	}
	binfo->num_planes = b->length;
	list_add_tail(&binfo->list, &v4l2_inst->registered_bufs);
	rc = msm_vidc_prepare_buf(v4l2_inst->vidc_inst, b);
exit:
	return rc;
}

int msm_v4l2_qbuf(struct file *file, void *fh,
				struct v4l2_buffer *b)
{
	struct msm_vidc_inst *vidc_inst;
	struct msm_v4l2_vid_inst *v4l2_inst;
	struct buffer_info *binfo;
	int plane = 0;
	int rc = 0;
	int i;
	if (b->length > VIDEO_MAX_PLANES) {
		dprintk(VIDC_ERR, "num planes exceeds max: %d\n",
			b->length);
		return -EINVAL;
	}
	vidc_inst = get_vidc_inst(file, fh);
	v4l2_inst = get_v4l2_inst(file, fh);
	for (i = 0; i < b->length; ++i) {
		if (EXTRADATA_IDX(b->length) &&
			(i == EXTRADATA_IDX(b->length)) &&
			!b->m.planes[i].length) {
			b->m.planes[i].m.userptr = 0;
			continue;
		}
		binfo = get_registered_buf(&v4l2_inst->registered_bufs,
				b->m.planes[i].reserved[0],
				b->m.planes[i].reserved[1],
				b->m.planes[i].length, &plane);
		if (!binfo) {
			dprintk(VIDC_ERR,
				"This buffer is not registered: %d, %d, %d\n",
				b->m.planes[i].reserved[0],
				b->m.planes[i].reserved[1],
				b->m.planes[i].length);
			rc = -EINVAL;
			goto err_invalid_buff;
		}
		b->m.planes[i].m.userptr = binfo->device_addr[i];
		dprintk(VIDC_DBG, "Queueing device address = 0x%x\n",
				binfo->device_addr[i]);
		if (binfo->handle[i]) {
			rc = msm_smem_clean_invalidate(v4l2_inst->mem_client,
					binfo->handle[i]);
			if (rc) {
				dprintk(VIDC_ERR,
					"Failed to clean caches: %d\n", rc);
				goto err_invalid_buff;
			}
		}
	}
	rc = msm_vidc_qbuf(v4l2_inst->vidc_inst, b);
err_invalid_buff:
	return rc;
}

int msm_v4l2_dqbuf(struct file *file, void *fh,
				struct v4l2_buffer *b)
{
	int rc = 0;
	int i;
	struct msm_v4l2_vid_inst *v4l2_inst;
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	if (b->length > VIDEO_MAX_PLANES) {
		dprintk(VIDC_ERR, "num planes exceed maximum: %d\n",
			b->length);
		return -EINVAL;
	}
	v4l2_inst = get_v4l2_inst(file, fh);
	rc = msm_vidc_dqbuf((void *)vidc_inst, b);
	if (rc) {
		dprintk(VIDC_DBG,
			"Failed to dqbuf, capability: %d, rc: %d\n",
			b->type, rc);
		goto fail_dq_buf;
	}
	for (i = 0; i < b->length; i++) {
		if (EXTRADATA_IDX(b->length) &&
				(i == EXTRADATA_IDX(b->length)) &&
				!b->m.planes[i].m.userptr) {
			continue;
		}
		b->m.planes[i].m.userptr = device_to_uvaddr(
				&v4l2_inst->registered_bufs,
				b->m.planes[i].m.userptr);
		if (!b->m.planes[i].m.userptr) {
			dprintk(VIDC_ERR,
			"Failed to find user virtual address, 0x%lx, %d, %d\n",
			b->m.planes[i].m.userptr, b->type, i);
			rc = -EINVAL;
			goto fail_dq_buf;
		}
	}
fail_dq_buf:
	return rc;
}

int msm_v4l2_streamon(struct file *file, void *fh,
				enum v4l2_buf_type i)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	return msm_vidc_streamon((void *)vidc_inst, i);
}

int msm_v4l2_streamoff(struct file *file, void *fh,
				enum v4l2_buf_type i)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	return msm_vidc_streamoff((void *)vidc_inst, i);
}

static int msm_v4l2_subscribe_event(struct v4l2_fh *fh,
			const struct v4l2_event_subscription *sub)
{
	struct msm_vidc_inst *vidc_inst = container_of(fh,
			struct msm_vidc_inst, event_handler);
	return msm_vidc_subscribe_event((void *)vidc_inst, sub);
}

static int msm_v4l2_unsubscribe_event(struct v4l2_fh *fh,
		const struct v4l2_event_subscription *sub)
{
	struct msm_vidc_inst *vidc_inst = container_of(fh,
			struct msm_vidc_inst, event_handler);
	return msm_vidc_unsubscribe_event((void *)vidc_inst, sub);
}

static int msm_v4l2_decoder_cmd(struct file *file, void *fh,
				struct v4l2_decoder_cmd *dec)
{
	struct msm_v4l2_vid_inst *v4l2_inst;
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	int rc = 0;
	v4l2_inst = get_v4l2_inst(file, NULL);
	if (dec->cmd == V4L2_DEC_CMD_STOP)
		rc = msm_v4l2_release_output_buffers(v4l2_inst);
	if (rc)
		dprintk(VIDC_WARN,
			"Failed to release dec output buffers: %d\n", rc);
	return msm_vidc_decoder_cmd((void *)vidc_inst, dec);
}

static int msm_v4l2_encoder_cmd(struct file *file, void *fh,
				struct v4l2_encoder_cmd *enc)
{
	struct msm_v4l2_vid_inst *v4l2_inst;
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	int rc = 0;
	v4l2_inst = get_v4l2_inst(file, NULL);
	if (enc->cmd == V4L2_ENC_CMD_STOP)
		rc = msm_v4l2_release_output_buffers(v4l2_inst);
	if (rc)
		dprintk(VIDC_WARN,
			"Failed to release enc output buffers: %d\n", rc);
	return msm_vidc_encoder_cmd((void *)vidc_inst, enc);
}
static int msm_v4l2_s_parm(struct file *file, void *fh,
			struct v4l2_streamparm *a)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(file, fh);
	return msm_vidc_s_parm((void *)vidc_inst, a);
}
static int msm_v4l2_g_parm(struct file *file, void *fh,
		struct v4l2_streamparm *a)
{
	return 0;
}
static const struct v4l2_ioctl_ops msm_v4l2_ioctl_ops = {
	.vidioc_querycap = msm_v4l2_querycap,
	.vidioc_enum_fmt_vid_cap_mplane = msm_v4l2_enum_fmt,
	.vidioc_enum_fmt_vid_out_mplane = msm_v4l2_enum_fmt,
	.vidioc_s_fmt_vid_cap_mplane = msm_v4l2_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = msm_v4l2_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane = msm_v4l2_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = msm_v4l2_g_fmt,
	.vidioc_reqbufs = msm_v4l2_reqbufs,
	.vidioc_prepare_buf = msm_v4l2_prepare_buf,
	.vidioc_qbuf = msm_v4l2_qbuf,
	.vidioc_dqbuf = msm_v4l2_dqbuf,
	.vidioc_streamon = msm_v4l2_streamon,
	.vidioc_streamoff = msm_v4l2_streamoff,
	.vidioc_s_ctrl = msm_v4l2_s_ctrl,
	.vidioc_g_ctrl = msm_v4l2_g_ctrl,
	.vidioc_subscribe_event = msm_v4l2_subscribe_event,
	.vidioc_unsubscribe_event = msm_v4l2_unsubscribe_event,
	.vidioc_decoder_cmd = msm_v4l2_decoder_cmd,
	.vidioc_encoder_cmd = msm_v4l2_encoder_cmd,
	.vidioc_s_parm = msm_v4l2_s_parm,
	.vidioc_g_parm = msm_v4l2_g_parm
};

static const struct v4l2_ioctl_ops msm_v4l2_enc_ioctl_ops = {
};

static unsigned int msm_v4l2_poll(struct file *filp,
	struct poll_table_struct *pt)
{
	struct msm_vidc_inst *vidc_inst = get_vidc_inst(filp, NULL);
	return msm_vidc_poll((void *)vidc_inst, filp, pt);
}

static const struct v4l2_file_operations msm_v4l2_vidc_fops = {
	.owner = THIS_MODULE,
	.open = msm_v4l2_open,
	.release = msm_v4l2_close,
	.ioctl = video_ioctl2,
	.poll = msm_v4l2_poll,
};

void msm_vidc_release_video_device(struct video_device *pvdev)
{
}

static int msm_vidc_get_hfi(struct platform_device *pdev,
			    struct msm_vidc_core *core)
{
	struct device_node *np = pdev->dev.of_node;
	int rc = 0;
	const char *hfi_name = NULL;

	rc = of_property_read_string(np, "hfi", &hfi_name);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to read hfi from device tree\n");
		goto err_hfi_read;
	}

	if (!strcmp(hfi_name, "venus"))
		core->hfi_type = VIDC_HFI_VENUS;
	else if (!strcmp(hfi_name, "q6"))
		core->hfi_type = VIDC_HFI_Q6;

	dprintk(VIDC_INFO, "hfi_type = %d\n", core->hfi_type);

err_hfi_read:
	return rc;
}


static int msm_vidc_initialize_core(struct platform_device *pdev,
				struct msm_vidc_core *core)
{
	int i = 0;
	int rc = 0;
	if (!core)
		return -EINVAL;

	INIT_LIST_HEAD(&core->instances);
	mutex_init(&core->sync_lock);
	spin_lock_init(&core->lock);

	core->state = VIDC_CORE_UNINIT;
	for (i = SYS_MSG_INDEX(SYS_MSG_START);
		i <= SYS_MSG_INDEX(SYS_MSG_END); i++) {
		init_completion(&core->completions[i]);
	}

	rc = msm_vidc_get_hfi(pdev, core);
	if (rc)
		dprintk(VIDC_ERR,
			"Failed to read Host-Firmware Interface rc: %d\n", rc);

	return rc;
}

static int __devinit msm_vidc_probe(struct platform_device *pdev)
{
	int rc = 0;
	struct msm_vidc_core *core;
	unsigned long flags;
	core = kzalloc(sizeof(*core), GFP_KERNEL);
	if (!core || !vidc_driver) {
		dprintk(VIDC_ERR,
			"Failed to allocate memory for device core\n");
		rc = -ENOMEM;
		goto err_no_mem;
	}
	rc = msm_vidc_initialize_core(pdev, core);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to init core\n");
		goto err_v4l2_register;
	}
	rc = v4l2_device_register(&pdev->dev, &core->v4l2_dev);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to register v4l2 device\n");
		goto err_v4l2_register;
	}
	core->vdev[MSM_VIDC_DECODER].vdev.release =
		msm_vidc_release_video_device;
	core->vdev[MSM_VIDC_DECODER].vdev.fops = &msm_v4l2_vidc_fops;
	core->vdev[MSM_VIDC_DECODER].vdev.ioctl_ops = &msm_v4l2_ioctl_ops;
	core->vdev[MSM_VIDC_DECODER].type = MSM_VIDC_DECODER;
	core->vdev[MSM_VIDC_DECODER].vdev.vfl_dir = VFL_DIR_M2M;
	rc = video_register_device(&core->vdev[MSM_VIDC_DECODER].vdev,
					VFL_TYPE_GRABBER, BASE_DEVICE_NUMBER);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to register video decoder device");
		goto err_dec_register;
	}
	video_set_drvdata(&core->vdev[MSM_VIDC_DECODER].vdev, core);

	core->vdev[MSM_VIDC_ENCODER].vdev.release =
		msm_vidc_release_video_device;
	core->vdev[MSM_VIDC_ENCODER].vdev.fops = &msm_v4l2_vidc_fops;
	core->vdev[MSM_VIDC_ENCODER].vdev.ioctl_ops = &msm_v4l2_ioctl_ops;
	core->vdev[MSM_VIDC_ENCODER].type = MSM_VIDC_ENCODER;
	core->vdev[MSM_VIDC_ENCODER].vdev.vfl_dir = VFL_DIR_M2M;
	rc = video_register_device(&core->vdev[MSM_VIDC_ENCODER].vdev,
				VFL_TYPE_GRABBER, BASE_DEVICE_NUMBER + 1);
	if (rc) {
		dprintk(VIDC_ERR, "Failed to register video encoder device");
		goto err_enc_register;
	}
	video_set_drvdata(&core->vdev[MSM_VIDC_ENCODER].vdev, core);

	core->device = vidc_hfi_initialize(core->hfi_type, core->id,
					   pdev, &handle_cmd_response);
	if (!core->device) {
		dprintk(VIDC_ERR, "Failed to create HFI device\n");
		goto err_cores_exceeded;
	}

	spin_lock_irqsave(&vidc_driver->lock, flags);
	if (vidc_driver->num_cores  + 1 > MSM_VIDC_CORES_MAX) {
		spin_unlock_irqrestore(&vidc_driver->lock, flags);
		dprintk(VIDC_ERR, "Maximum cores already exist, core_no = %d\n",
				vidc_driver->num_cores);
		goto err_cores_exceeded;
	}

	core->id = vidc_driver->num_cores++;
	list_add_tail(&core->list, &vidc_driver->cores);
	spin_unlock_irqrestore(&vidc_driver->lock, flags);
	core->debugfs_root = msm_vidc_debugfs_init_core(
		core, vidc_driver->debugfs_root);
	pdev->dev.platform_data = core;
	return rc;

err_cores_exceeded:
	video_unregister_device(&core->vdev[MSM_VIDC_ENCODER].vdev);
err_enc_register:
	video_unregister_device(&core->vdev[MSM_VIDC_DECODER].vdev);
err_dec_register:
	v4l2_device_unregister(&core->v4l2_dev);
err_v4l2_register:
	kfree(core);
err_no_mem:
	return rc;
}

static int __devexit msm_vidc_remove(struct platform_device *pdev)
{
	int rc = 0;
	struct msm_vidc_core *core;

	if (!pdev) {
		dprintk(VIDC_ERR, "%s invalid input %p", __func__, pdev);
		return -EINVAL;
	}
	core = pdev->dev.platform_data;

	if (!core) {
		dprintk(VIDC_ERR, "%s invalid core", __func__);
		return -EINVAL;
	}

	vidc_hfi_deinitialize(core->hfi_type, core->device);
	video_unregister_device(&core->vdev[MSM_VIDC_ENCODER].vdev);
	video_unregister_device(&core->vdev[MSM_VIDC_DECODER].vdev);
	v4l2_device_unregister(&core->v4l2_dev);

	kfree(core);
	return rc;
}
static const struct of_device_id msm_vidc_dt_match[] = {
	{.compatible = "qcom,msm-vidc"},
};

MODULE_DEVICE_TABLE(of, msm_vidc_dt_match);

static struct platform_driver msm_vidc_driver = {
	.probe = msm_vidc_probe,
	.remove = msm_vidc_remove,
	.driver = {
		.name = "msm_vidc",
		.owner = THIS_MODULE,
		.of_match_table = msm_vidc_dt_match,
	},
};

static int __init msm_vidc_init(void)
{
	int rc = 0;
	vidc_driver = kzalloc(sizeof(*vidc_driver),
						GFP_KERNEL);
	if (!vidc_driver) {
		dprintk(VIDC_ERR,
			"Failed to allocate memroy for msm_vidc_drv\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&vidc_driver->cores);
	spin_lock_init(&vidc_driver->lock);
	vidc_driver->debugfs_root = debugfs_create_dir("msm_vidc", NULL);
	if (!vidc_driver->debugfs_root)
		dprintk(VIDC_ERR,
			"Failed to create debugfs for msm_vidc\n");

	rc = platform_driver_register(&msm_vidc_driver);
	if (rc) {
		dprintk(VIDC_ERR,
			"Failed to register platform driver\n");
		kfree(vidc_driver);
		vidc_driver = NULL;
	}

	return rc;
}

static void __exit msm_vidc_exit(void)
{
	platform_driver_unregister(&msm_vidc_driver);
	debugfs_remove_recursive(vidc_driver->debugfs_root);
	kfree(vidc_driver);
	vidc_driver = NULL;
}

module_init(msm_vidc_init);
module_exit(msm_vidc_exit);
