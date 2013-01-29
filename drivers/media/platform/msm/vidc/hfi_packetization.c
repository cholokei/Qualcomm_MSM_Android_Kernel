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
#include "hfi_packetization.h"
#include "msm_vidc_debug.h"
#include <linux/errno.h>
#include <mach/ocmem.h>

int create_pkt_cmd_sys_init(struct hfi_cmd_sys_init_packet *pkt,
			   u32 arch_type)
{
	int rc = 0;
	if (!pkt)
		return -EINVAL;

	pkt->packet_type = HFI_CMD_SYS_INIT;
	pkt->size = sizeof(struct hfi_cmd_sys_init_packet);
	pkt->arch_type = arch_type;
	return rc;
}

int create_pkt_cmd_sys_pc_prep(struct hfi_cmd_sys_pc_prep_packet *pkt)
{
	int rc = 0;
	if (!pkt)
		return -EINVAL;

	pkt->packet_type = HFI_CMD_SYS_PC_PREP;
	pkt->size = sizeof(struct hfi_cmd_sys_pc_prep_packet);
	return rc;
}

int create_pkt_set_cmd_sys_resource(
		struct hfi_cmd_sys_set_resource_packet *pkt,
		struct vidc_resource_hdr *resource_hdr,
		void *resource_value)
{
	int rc = 0;
	if (!pkt || !resource_hdr || !resource_value)
		return -EINVAL;

	pkt->packet_type = HFI_CMD_SYS_SET_RESOURCE;
	pkt->size = sizeof(struct hfi_cmd_sys_set_resource_packet);
	pkt->resource_handle = resource_hdr->resource_handle;

	switch (resource_hdr->resource_id) {
	case VIDC_RESOURCE_OCMEM:
	{
		struct hfi_resource_ocmem *hfioc_mem =
			(struct hfi_resource_ocmem *)
			&pkt->rg_resource_data[0];
		struct ocmem_buf *ocmem =
			(struct ocmem_buf *) resource_value;

		pkt->resource_type = HFI_RESOURCE_OCMEM;
		pkt->size += sizeof(struct hfi_resource_ocmem);
		hfioc_mem->size = (u32) ocmem->len;
		hfioc_mem->mem = (u8 *) ocmem->addr;
		break;
	}
	default:
		dprintk(VIDC_ERR, "Invalid resource_id %d",
					resource_hdr->resource_id);
		rc = -EINVAL;
	}

	return rc;
}

int create_pkt_cmd_sys_release_resource(
		struct hfi_cmd_sys_release_resource_packet *pkt,
		struct vidc_resource_hdr *resource_hdr)
{
	int rc = 0;
	if (!pkt)
		return -EINVAL;

	pkt->size = sizeof(struct hfi_cmd_sys_release_resource_packet);
	pkt->packet_type = HFI_CMD_SYS_RELEASE_RESOURCE;
	pkt->resource_type = resource_hdr->resource_id;
	pkt->resource_handle = resource_hdr->resource_handle;

	return rc;
}

int create_pkt_cmd_sys_ping(struct hfi_cmd_sys_ping_packet *pkt)
{
	int rc = 0;
	if (!pkt)
		return -EINVAL;

	pkt->size = sizeof(struct hfi_cmd_sys_ping_packet);
	pkt->packet_type = HFI_CMD_SYS_PING;

	return rc;
}

inline int create_pkt_cmd_sys_session_init(
		struct hfi_cmd_sys_session_init_packet *pkt,
		u32 session_id, u32 session_domain, u32 session_codec)
{
	int rc = 0;
	if (!pkt)
		return -EINVAL;

	pkt->size = sizeof(struct hfi_cmd_sys_session_init_packet);
	pkt->packet_type = HFI_CMD_SYS_SESSION_INIT;
	pkt->session_id = session_id;
	pkt->session_domain = session_domain;
	pkt->session_codec = session_codec;

	return rc;
}

int create_pkt_cmd_session_cmd(struct vidc_hal_session_cmd_pkt *pkt,
			int pkt_type, u32 session_id)
{
	int rc = 0;
	if (!pkt)
		return -EINVAL;

	pkt->size = sizeof(struct vidc_hal_session_cmd_pkt);
	pkt->packet_type = pkt_type;
	pkt->session_id = session_id;

	return rc;
}

static u32 get_hfi_buffer(int hal_buffer)
{
	u32 buffer;
	switch (hal_buffer) {
	case HAL_BUFFER_INPUT:
		buffer = HFI_BUFFER_INPUT;
		break;
	case HAL_BUFFER_OUTPUT:
		buffer = HFI_BUFFER_OUTPUT;
		break;
	case HAL_BUFFER_OUTPUT2:
		buffer = HFI_BUFFER_OUTPUT;
		break;
	case HAL_BUFFER_EXTRADATA_INPUT:
		buffer = HFI_BUFFER_EXTRADATA_INPUT;
		break;
	case HAL_BUFFER_EXTRADATA_OUTPUT:
		buffer = HFI_BUFFER_EXTRADATA_OUTPUT;
		break;
	case HAL_BUFFER_EXTRADATA_OUTPUT2:
		buffer = HFI_BUFFER_EXTRADATA_OUTPUT2;
		break;
	case HAL_BUFFER_INTERNAL_SCRATCH:
		buffer = HFI_BUFFER_INTERNAL_SCRATCH;
		break;
	case HAL_BUFFER_INTERNAL_PERSIST:
		buffer = HFI_BUFFER_INTERNAL_PERSIST;
		break;
	default:
		dprintk(VIDC_ERR, "Invalid buffer :0x%x\n",
				hal_buffer);
		buffer = 0;
		break;
	}
	return buffer;
}

static int get_hfi_extradata_index(enum hal_extradata_id index)
{
	int ret = 0;
	switch (index) {
	case HAL_EXTRADATA_MB_QUANTIZATION:
		ret = HFI_PROPERTY_PARAM_VDEC_MB_QUANTIZATION;
		break;
	case HAL_EXTRADATA_INTERLACE_VIDEO:
		ret = HFI_PROPERTY_PARAM_VDEC_INTERLACE_VIDEO_EXTRADATA;
		break;
	case HAL_EXTRADATA_VC1_FRAMEDISP:
		ret = HFI_PROPERTY_PARAM_VDEC_VC1_FRAMEDISP_EXTRADATA;
		break;
	case HAL_EXTRADATA_VC1_SEQDISP:
		ret = HFI_PROPERTY_PARAM_VDEC_VC1_SEQDISP_EXTRADATA;
		break;
	case HAL_EXTRADATA_TIMESTAMP:
		ret = HFI_PROPERTY_PARAM_VDEC_TIMESTAMP_EXTRADATA;
		break;
	case HAL_EXTRADATA_S3D_FRAME_PACKING:
		ret = HFI_PROPERTY_PARAM_S3D_FRAME_PACKING_EXTRADATA;
		break;
	case HAL_EXTRADATA_FRAME_RATE:
		ret = HFI_PROPERTY_PARAM_VDEC_FRAME_RATE_EXTRADATA;
		break;
	case HAL_EXTRADATA_PANSCAN_WINDOW:
		ret = HFI_PROPERTY_PARAM_VDEC_PANSCAN_WNDW_EXTRADATA;
		break;
	case HAL_EXTRADATA_RECOVERY_POINT_SEI:
		ret = HFI_PROPERTY_PARAM_VDEC_RECOVERY_POINT_SEI_EXTRADATA;
		break;
	case HAL_EXTRADATA_CLOSED_CAPTION_UD:
		ret = HFI_PROPERTY_PARAM_VDEC_CLOSED_CAPTION_EXTRADATA;
		break;
	case HAL_EXTRADATA_AFD_UD:
		ret = HFI_PROPERTY_PARAM_VDEC_AFD_EXTRADATA;
		break;
	case HAL_EXTRADATA_MULTISLICE_INFO:
		ret = HFI_PROPERTY_PARAM_VENC_MULTI_SLICE_INFO;
		break;
	case HAL_EXTRADATA_NUM_CONCEALED_MB:
		ret = HFI_PROPERTY_PARAM_VDEC_NUM_CONCEALED_MB;
		break;
	case HAL_EXTRADATA_INDEX:
		ret = HFI_PROPERTY_PARAM_EXTRA_DATA_HEADER_CONFIG;
		break;
	default:
		dprintk(VIDC_WARN, "Extradata index not found: %d\n", index);
		break;
	}
	return ret;
}

int create_pkt_cmd_session_set_buffers(
		struct hfi_cmd_session_set_buffers_packet *pkt,
		u32 session_id,
		struct vidc_buffer_addr_info *buffer_info)
{
	int rc = 0;
	int i = 0;
	if (!pkt || !session_id)
		return -EINVAL;

	pkt->packet_type = HFI_CMD_SESSION_SET_BUFFERS;
	pkt->session_id = session_id;
	pkt->buffer_size = buffer_info->buffer_size;
	pkt->min_buffer_size = buffer_info->buffer_size;
	pkt->num_buffers = buffer_info->num_buffers;

	if ((buffer_info->buffer_type == HAL_BUFFER_OUTPUT) ||
		(buffer_info->buffer_type == HAL_BUFFER_OUTPUT2)) {
		struct hfi_buffer_info *buff;
		pkt->extra_data_size = buffer_info->extradata_size;
		pkt->size = sizeof(struct hfi_cmd_session_set_buffers_packet) -
				sizeof(u32) + (buffer_info->num_buffers *
				sizeof(struct hfi_buffer_info));
		buff = (struct hfi_buffer_info *) pkt->rg_buffer_info;
		for (i = 0; i < pkt->num_buffers; i++) {
			buff->buffer_addr = buffer_info->align_device_addr;
			buff->extra_data_addr = buffer_info->extradata_addr;
		}
	} else {
		pkt->extra_data_size = 0;
		pkt->size = sizeof(struct hfi_cmd_session_set_buffers_packet) +
			((buffer_info->num_buffers - 1) * sizeof(u32));
		for (i = 0; i < pkt->num_buffers; i++)
			pkt->rg_buffer_info[i] = buffer_info->align_device_addr;
	}

	pkt->buffer_type = get_hfi_buffer(buffer_info->buffer_type);
	if (!pkt->buffer_type)
		return -EINVAL;

	return rc;
}

int create_pkt_cmd_session_release_buffers(
		struct hfi_cmd_session_release_buffer_packet *pkt,
		u32 session_id, struct vidc_buffer_addr_info *buffer_info)
{
	int rc = 0;
	int i = 0;
	if (!pkt || !session_id)
		return -EINVAL;

	pkt->packet_type = HFI_CMD_SESSION_RELEASE_BUFFERS;
	pkt->session_id = session_id;
	pkt->buffer_size = buffer_info->buffer_size;
	pkt->num_buffers = buffer_info->num_buffers;

	if ((buffer_info->buffer_type == HAL_BUFFER_OUTPUT) ||
		(buffer_info->buffer_type == HAL_BUFFER_OUTPUT2)) {
		struct hfi_buffer_info *buff;
		buff = (struct hfi_buffer_info *) pkt->rg_buffer_info;
		for (i = 0; i < pkt->num_buffers; i++) {
			buff->buffer_addr = buffer_info->align_device_addr;
			buff->extra_data_addr = buffer_info->extradata_addr;
		}
		pkt->size = sizeof(struct hfi_cmd_session_set_buffers_packet) -
				sizeof(u32) + (buffer_info->num_buffers *
				sizeof(struct hfi_buffer_info));
	} else {
		for (i = 0; i < pkt->num_buffers; i++)
			pkt->rg_buffer_info[i] = buffer_info->align_device_addr;
		pkt->extra_data_size = 0;
		pkt->size = sizeof(struct hfi_cmd_session_set_buffers_packet) +
			((buffer_info->num_buffers - 1) * sizeof(u32));
	}
	pkt->response_req = buffer_info->response_required;
	pkt->buffer_type = get_hfi_buffer(buffer_info->buffer_type);
	if (!pkt->buffer_type)
		return -EINVAL;
	return rc;
}

int create_pkt_cmd_session_etb_decoder(
	struct hfi_cmd_session_empty_buffer_compressed_packet *pkt,
	u32 session_id, struct vidc_frame_data *input_frame)
{
	int rc = 0;
	if (!pkt || !session_id)
		return -EINVAL;

	pkt->size =
		sizeof(struct hfi_cmd_session_empty_buffer_compressed_packet);
	pkt->packet_type = HFI_CMD_SESSION_EMPTY_BUFFER;
	pkt->session_id = session_id;
	pkt->time_stamp_hi = (int) (((u64)input_frame->timestamp) >> 32);
	pkt->time_stamp_lo = (int) input_frame->timestamp;
	pkt->flags = input_frame->flags;
	pkt->mark_target = input_frame->mark_target;
	pkt->mark_data = input_frame->mark_data;
	pkt->offset = input_frame->offset;
	pkt->alloc_len = input_frame->alloc_len;
	pkt->filled_len = input_frame->filled_len;
	pkt->input_tag = input_frame->clnt_data;
	pkt->packet_buffer = (u8 *) input_frame->device_addr;

	if (!pkt->packet_buffer)
		return -EINVAL;
	return rc;
}

int create_pkt_cmd_session_etb_encoder(
	struct hfi_cmd_session_empty_buffer_uncompressed_plane0_packet *pkt,
	u32 session_id, struct vidc_frame_data *input_frame)
{
	int rc = 0;
	if (!pkt || !session_id)
		return -EINVAL;

	pkt->size = sizeof(struct
		hfi_cmd_session_empty_buffer_uncompressed_plane0_packet);
	pkt->packet_type = HFI_CMD_SESSION_EMPTY_BUFFER;
	pkt->session_id = session_id;
	pkt->view_id = 0;
	pkt->time_stamp_hi = (u32)(((u64)input_frame->timestamp) >> 32);
	pkt->time_stamp_lo = (u32)input_frame->timestamp;
	pkt->flags = input_frame->flags;
	pkt->mark_target = input_frame->mark_target;
	pkt->mark_data = input_frame->mark_data;
	pkt->offset = input_frame->offset;
	pkt->alloc_len = input_frame->alloc_len;
	pkt->filled_len = input_frame->filled_len;
	pkt->input_tag = input_frame->clnt_data;
	pkt->packet_buffer = (u8 *) input_frame->device_addr;

	if (!pkt->packet_buffer)
		return -EINVAL;
	return rc;
}

int create_pkt_cmd_session_ftb(struct hfi_cmd_session_fill_buffer_packet *pkt,
		u32 session_id, struct vidc_frame_data *output_frame)
{
	int rc = 0;
	if (!pkt || !session_id || !output_frame)
		return -EINVAL;

	pkt->size = sizeof(struct hfi_cmd_session_fill_buffer_packet);
	pkt->packet_type = HFI_CMD_SESSION_FILL_BUFFER;
	pkt->session_id = session_id;

	if (output_frame->buffer_type == HAL_BUFFER_OUTPUT)
		pkt->stream_id = 0;
	else if (output_frame->buffer_type == HAL_BUFFER_OUTPUT2)
		pkt->stream_id = 1;

	if (!output_frame->device_addr)
		return -EINVAL;

	pkt->packet_buffer = (u8 *) output_frame->device_addr;
	pkt->extra_data_buffer = (u8 *) output_frame->extradata_addr;

	pkt->alloc_len = output_frame->alloc_len;
	pkt->filled_len = output_frame->filled_len;
	pkt->offset = output_frame->offset;
	dprintk(VIDC_DBG, "### Q OUTPUT BUFFER ###: %d, %d, %d\n",
			pkt->alloc_len, pkt->filled_len, pkt->offset);

	return rc;
}

int create_pkt_cmd_session_parse_seq_header(
		struct hfi_cmd_session_parse_sequence_header_packet *pkt,
		u32 session_id, struct vidc_seq_hdr *seq_hdr)
{
	int rc = 0;
	if (!pkt || !session_id || seq_hdr)
		return -EINVAL;

	pkt->size = sizeof(struct hfi_cmd_session_parse_sequence_header_packet);
	pkt->packet_type = HFI_CMD_SESSION_PARSE_SEQUENCE_HEADER;
	pkt->session_id = session_id;
	pkt->header_len = seq_hdr->seq_hdr_len;
	if (!seq_hdr->seq_hdr)
		return -EINVAL;
	pkt->packet_buffer = seq_hdr->seq_hdr;

	return rc;
}

int create_pkt_cmd_session_get_seq_hdr(
		struct hfi_cmd_session_get_sequence_header_packet *pkt,
		u32 session_id, struct vidc_seq_hdr *seq_hdr)
{
	int rc = 0;

	if (!pkt || !session_id || !seq_hdr)
		return -EINVAL;

	pkt->size = sizeof(struct hfi_cmd_session_get_sequence_header_packet);
	pkt->packet_type = HFI_CMD_SESSION_GET_SEQUENCE_HEADER;
	pkt->session_id = session_id;
	pkt->buffer_len = seq_hdr->seq_hdr_len;
	if (!seq_hdr->seq_hdr)
		return -EINVAL;
	pkt->packet_buffer = seq_hdr->seq_hdr;

	return rc;
}

int create_pkt_cmd_session_get_buf_req(
		struct hfi_cmd_session_get_property_packet *pkt,
		u32 session_id)
{
	int rc = 0;

	if (!pkt || !session_id)
		return -EINVAL;

	pkt->size = sizeof(struct hfi_cmd_session_get_property_packet);
	pkt->packet_type = HFI_CMD_SESSION_GET_PROPERTY;
	pkt->session_id = session_id;
	pkt->num_properties = 1;
	pkt->rg_property_data[0] = HFI_PROPERTY_CONFIG_BUFFER_REQUIREMENTS;

	return rc;
}

int create_pkt_cmd_session_flush(struct hfi_cmd_session_flush_packet *pkt,
			u32 session_id, enum hal_flush flush_mode)
{
	int rc = 0;
	if (!pkt || !session_id)
		return -EINVAL;

	pkt->size = sizeof(struct hfi_cmd_session_flush_packet);
	pkt->packet_type = HFI_CMD_SESSION_FLUSH;
	pkt->session_id = session_id;
	switch (flush_mode) {
	case HAL_FLUSH_INPUT:
		pkt->flush_type = HFI_FLUSH_INPUT;
		break;
	case HAL_FLUSH_OUTPUT:
		pkt->flush_type = HFI_FLUSH_OUTPUT;
		break;
	case HAL_FLUSH_OUTPUT2:
		pkt->flush_type = HFI_FLUSH_OUTPUT2;
		break;
	case HAL_FLUSH_ALL:
		pkt->flush_type = HFI_FLUSH_ALL;
		break;
	default:
		dprintk(VIDC_ERR, "Invalid flush mode: 0x%x\n", flush_mode);
		return -EINVAL;
	}
	return rc;
}

int create_pkt_cmd_session_set_property(
		struct hfi_cmd_session_set_property_packet *pkt,
		u32 session_id, enum hal_property ptype, void *pdata)
{
	int rc = 0;
	if (!pkt || !session_id)
		return -EINVAL;

	pkt->size = sizeof(struct hfi_cmd_session_set_property_packet);
	pkt->packet_type = HFI_CMD_SESSION_SET_PROPERTY;
	pkt->session_id = session_id;
	pkt->num_properties = 1;

	switch (ptype) {
	case HAL_CONFIG_FRAME_RATE:
	{
		u32 buffer_type;
		struct hfi_frame_rate *hfi;
		struct hal_frame_rate *prop = (struct hal_frame_rate *) pdata;

		pkt->rg_property_data[0] = HFI_PROPERTY_CONFIG_FRAME_RATE;
		hfi = (struct hfi_frame_rate *) &pkt->rg_property_data[1];
		buffer_type = get_hfi_buffer(prop->buffer_type);
		if (buffer_type)
			hfi->buffer_type = buffer_type;
		else
			return -EINVAL;

		hfi->frame_rate = prop->frame_rate;
		pkt->size += sizeof(u32) + sizeof(struct hfi_frame_rate);
		break;
	}
	case HAL_PARAM_UNCOMPRESSED_FORMAT_SELECT:
	{
		u32 buffer_type;
		struct hfi_uncompressed_format_select *hfi;
		struct hal_uncompressed_format_select *prop =
			(struct hal_uncompressed_format_select *) pdata;

		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_UNCOMPRESSED_FORMAT_SELECT;

		hfi = (struct hfi_uncompressed_format_select *)
					&pkt->rg_property_data[1];
		buffer_type = get_hfi_buffer(prop->buffer_type);
		if (buffer_type)
			hfi->buffer_type = buffer_type;
		else
			return -EINVAL;
		hfi->format = prop->format;
		pkt->size += sizeof(u32) +
			sizeof(struct hfi_uncompressed_format_select);
		break;
		}
	case HAL_PARAM_UNCOMPRESSED_PLANE_ACTUAL_CONSTRAINTS_INFO:
		break;
	case HAL_PARAM_UNCOMPRESSED_PLANE_ACTUAL_INFO:
		break;
	case HAL_PARAM_EXTRA_DATA_HEADER_CONFIG:
		break;
	case HAL_PARAM_FRAME_SIZE:
	{
		struct hfi_frame_size *hfi;
		struct hal_frame_size *prop = (struct hal_frame_size *) pdata;
		u32 buffer_type;

		pkt->rg_property_data[0] = HFI_PROPERTY_PARAM_FRAME_SIZE;
		hfi = (struct hfi_frame_size *) &pkt->rg_property_data[1];
		buffer_type = get_hfi_buffer(prop->buffer_type);
		if (buffer_type)
			hfi->buffer_type = buffer_type;
		else
			return -EINVAL;

		hfi->height = prop->height;
		hfi->width = prop->width;
		pkt->size += sizeof(u32) + sizeof(struct hfi_frame_size);
		break;
	}
	case HAL_CONFIG_REALTIME:
	{
		struct hfi_enable *hfi;
		pkt->rg_property_data[0] = HFI_PROPERTY_CONFIG_REALTIME;
		hfi = (struct hfi_enable *) &pkt->rg_property_data[1];
		hfi->enable = ((struct hfi_enable *) pdata)->enable;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_BUFFER_COUNT_ACTUAL:
	{
		struct hfi_buffer_count_actual *hfi;
		struct hal_buffer_count_actual *prop =
			(struct hal_buffer_count_actual *) pdata;
		u32 buffer_type;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_BUFFER_COUNT_ACTUAL;
		hfi = (struct hfi_buffer_count_actual *)
			&pkt->rg_property_data[1];
		hfi->buffer_count_actual = prop->buffer_count_actual;

		buffer_type = get_hfi_buffer(prop->buffer_type);
		if (buffer_type)
			hfi->buffer_type = buffer_type;
		else
			return -EINVAL;

		pkt->size += sizeof(u32) + sizeof(struct
				hfi_buffer_count_actual);

		break;
	}
	case HAL_PARAM_NAL_STREAM_FORMAT_SELECT:
	{
		struct hal_nal_stream_format_supported *prop =
			(struct hal_nal_stream_format_supported *)pdata;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_NAL_STREAM_FORMAT_SELECT;
		dprintk(VIDC_DBG, "data is :%d",
				prop->nal_stream_format_supported);

		switch (prop->nal_stream_format_supported) {
		case HAL_NAL_FORMAT_STARTCODES:
			pkt->rg_property_data[1] =
				HFI_NAL_FORMAT_STARTCODES;
			break;
		case HAL_NAL_FORMAT_ONE_NAL_PER_BUFFER:
			pkt->rg_property_data[1] =
				HFI_NAL_FORMAT_ONE_NAL_PER_BUFFER;
			break;
		case HAL_NAL_FORMAT_ONE_BYTE_LENGTH:
			pkt->rg_property_data[1] =
				HFI_NAL_FORMAT_ONE_BYTE_LENGTH;
			break;
		case HAL_NAL_FORMAT_TWO_BYTE_LENGTH:
			pkt->rg_property_data[1] =
				HFI_NAL_FORMAT_TWO_BYTE_LENGTH;
			break;
		case HAL_NAL_FORMAT_FOUR_BYTE_LENGTH:
			pkt->rg_property_data[1] =
				HFI_NAL_FORMAT_FOUR_BYTE_LENGTH;
			break;
		default:
			dprintk(VIDC_ERR, "Invalid nal format: 0x%x",
				  prop->nal_stream_format_supported);
			break;
		}
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VDEC_OUTPUT_ORDER:
	{
		int *data = (int *) pdata;
		pkt->rg_property_data[0] =
				HFI_PROPERTY_PARAM_VDEC_OUTPUT_ORDER;
		switch (*data) {
		case HAL_OUTPUT_ORDER_DECODE:
			pkt->rg_property_data[1] = HFI_OUTPUT_ORDER_DECODE;
			break;
		case HAL_OUTPUT_ORDER_DISPLAY:
			pkt->rg_property_data[1] = HFI_OUTPUT_ORDER_DISPLAY;
			break;
		default:
			dprintk(VIDC_ERR, "invalid output order: 0x%x",
						  *data);
			break;
		}
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VDEC_PICTURE_TYPE_DECODE:
	{
		struct hfi_enable_picture *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VDEC_PICTURE_TYPE_DECODE;
		hfi = (struct hfi_enable_picture *) &pkt->rg_property_data[1];
		hfi->picture_type = (u32) pdata;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VDEC_OUTPUT2_KEEP_ASPECT_RATIO:
	{
		struct hfi_enable *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VDEC_OUTPUT2_KEEP_ASPECT_RATIO;
		hfi = (struct hfi_enable *) &pkt->rg_property_data[1];
		hfi->enable = ((struct hfi_enable *) pdata)->enable;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_CONFIG_VDEC_POST_LOOP_DEBLOCKER:
	{
		struct hfi_enable *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_CONFIG_VDEC_POST_LOOP_DEBLOCKER;
		hfi = (struct hfi_enable *) &pkt->rg_property_data[1];
		hfi->enable = ((struct hfi_enable *) pdata)->enable;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VDEC_MULTI_STREAM:
	{
		struct hfi_multi_stream *hfi;
		struct hal_multi_stream *prop =
			(struct hal_multi_stream *) pdata;
		u32 buffer_type;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VDEC_MULTI_STREAM;
		hfi = (struct hfi_multi_stream *) &pkt->rg_property_data[1];

		buffer_type = get_hfi_buffer(prop->buffer_type);
		if (buffer_type)
			hfi->buffer_type = buffer_type;
		else
			return -EINVAL;
		hfi->enable = prop->enable;
		hfi->width = prop->width;
		hfi->height = prop->height;
		pkt->size += sizeof(u32) + sizeof(struct hfi_multi_stream);
		break;
	}
	case HAL_PARAM_VDEC_DISPLAY_PICTURE_BUFFER_COUNT:
	{
		struct hfi_display_picture_buffer_count *hfi;
		struct hal_display_picture_buffer_count *prop =
			(struct hal_display_picture_buffer_count *) pdata;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VDEC_DISPLAY_PICTURE_BUFFER_COUNT;
		hfi = (struct hfi_display_picture_buffer_count *)
			&pkt->rg_property_data[1];
		hfi->count = prop->count;
		hfi->enable = prop->enable;
		pkt->size += sizeof(u32) +
			sizeof(struct hfi_display_picture_buffer_count);
		break;
	}
	case HAL_PARAM_DIVX_FORMAT:
	{
		int *data = pdata;
		pkt->rg_property_data[0] = HFI_PROPERTY_PARAM_DIVX_FORMAT;
		switch (*data) {
		case HAL_DIVX_FORMAT_4:
			pkt->rg_property_data[1] = HFI_DIVX_FORMAT_4;
			break;
		case HAL_DIVX_FORMAT_5:
			pkt->rg_property_data[1] = HFI_DIVX_FORMAT_5;
			break;
		case HAL_DIVX_FORMAT_6:
			pkt->rg_property_data[1] = HFI_DIVX_FORMAT_6;
			break;
		default:
			dprintk(VIDC_ERR, "Invalid divx format: 0x%x", *data);
			break;
		}
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_CONFIG_VDEC_MB_ERROR_MAP_REPORTING:
	{
		struct hfi_enable *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_CONFIG_VDEC_MB_ERROR_MAP_REPORTING;
		hfi = (struct hfi_enable *) &pkt->rg_property_data[1];
		hfi->enable = ((struct hfi_enable *) pdata)->enable;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VDEC_CONTINUE_DATA_TRANSFER:
	{
		struct hfi_enable *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VDEC_CONTINUE_DATA_TRANSFER;
		hfi = (struct hfi_enable *) &pkt->rg_property_data[1];
		hfi->enable = ((struct hfi_enable *) pdata)->enable;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VDEC_SYNC_FRAME_DECODE:
	{
		struct hfi_enable *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VDEC_THUMBNAIL_MODE;
		hfi = (struct hfi_enable *) &pkt->rg_property_data[1];
		hfi->enable = ((struct hfi_enable *) pdata)->enable;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VENC_SYNC_FRAME_SEQUENCE_HEADER:
	{
		struct hfi_enable *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_CONFIG_VENC_SYNC_FRAME_SEQUENCE_HEADER;
		hfi = (struct hfi_enable *) &pkt->rg_property_data[1];
		hfi->enable = ((struct hfi_enable *) pdata)->enable;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_CONFIG_VENC_REQUEST_IFRAME:
		pkt->rg_property_data[0] =
			HFI_PROPERTY_CONFIG_VENC_REQUEST_SYNC_FRAME;
		pkt->size += sizeof(u32);
		break;
	case HAL_PARAM_VENC_MPEG4_SHORT_HEADER:
		break;
	case HAL_PARAM_VENC_MPEG4_AC_PREDICTION:
		break;
	case HAL_CONFIG_VENC_TARGET_BITRATE:
	{
		struct hfi_bitrate *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE;
		hfi = (struct hfi_bitrate *) &pkt->rg_property_data[1];
		hfi->bit_rate = ((struct hal_bitrate *)pdata)->bit_rate;
		hfi->layer_id = ((struct hal_bitrate *)pdata)->layer_id;
		pkt->size += sizeof(u32) + sizeof(struct hfi_bitrate);
		break;
	}
	case HAL_PARAM_PROFILE_LEVEL_CURRENT:
	{
		struct hfi_profile_level *hfi;
		struct hal_profile_level *prop =
			(struct hal_profile_level *) pdata;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_PROFILE_LEVEL_CURRENT;
		hfi = (struct hfi_profile_level *)
			&pkt->rg_property_data[1];
		hfi->level = (u32) prop->level;
		hfi->profile = prop->profile;
		if (!hfi->profile)
			hfi->profile = HFI_H264_PROFILE_HIGH;
		if (!hfi->level)
			hfi->level = 1;
		pkt->size += sizeof(u32) + sizeof(struct hfi_profile_level);
		break;
	}
	case HAL_PARAM_VENC_H264_ENTROPY_CONTROL:
	{
		struct hfi_h264_entropy_control *hfi;
		struct hal_h264_entropy_control *prop =
			(struct hal_h264_entropy_control *) pdata;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VENC_H264_ENTROPY_CONTROL;
		hfi = (struct hfi_h264_entropy_control *)
			&pkt->rg_property_data[1];
		switch (prop->entropy_mode) {
		case HAL_H264_ENTROPY_CAVLC:
			hfi->cabac_model = HFI_H264_ENTROPY_CAVLC;
			break;
		case HAL_H264_ENTROPY_CABAC:
			hfi->cabac_model = HFI_H264_ENTROPY_CABAC;
			switch (prop->cabac_model) {
			case HAL_H264_CABAC_MODEL_0:
				hfi->cabac_model = HFI_H264_CABAC_MODEL_0;
				break;
			case HAL_H264_CABAC_MODEL_1:
				hfi->cabac_model = HFI_H264_CABAC_MODEL_1;
				break;
			case HAL_H264_CABAC_MODEL_2:
				hfi->cabac_model = HFI_H264_CABAC_MODEL_2;
				break;
			default:
				dprintk(VIDC_ERR,
					"Invalid cabac model 0x%x",
					prop->entropy_mode);
				break;
			}
		break;
		default:
			dprintk(VIDC_ERR,
				"Invalid entropy selected: 0x%x",
				prop->cabac_model);
			break;
		}
		pkt->size += sizeof(u32) + sizeof(
			struct hfi_h264_entropy_control);
		break;
	}
	case HAL_PARAM_VENC_RATE_CONTROL:
	{
		u32 *rc;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VENC_RATE_CONTROL;
		rc = (u32 *)pdata;
		switch ((enum hal_rate_control) *rc) {
		case HAL_RATE_CONTROL_OFF:
			pkt->rg_property_data[1] = HFI_RATE_CONTROL_OFF;
			break;
		case HAL_RATE_CONTROL_CBR_CFR:
			pkt->rg_property_data[1] = HFI_RATE_CONTROL_CBR_CFR;
			break;
		case HAL_RATE_CONTROL_CBR_VFR:
			pkt->rg_property_data[1] = HFI_RATE_CONTROL_CBR_VFR;
			break;
		case HAL_RATE_CONTROL_VBR_CFR:
			pkt->rg_property_data[1] = HFI_RATE_CONTROL_VBR_CFR;
			break;
		case HAL_RATE_CONTROL_VBR_VFR:
			pkt->rg_property_data[1] = HFI_RATE_CONTROL_VBR_VFR;
			break;
		default:
			dprintk(VIDC_ERR, "Invalid Rate control setting: 0x%x",
						  (int) pdata);
			break;
		}
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VENC_MPEG4_TIME_RESOLUTION:
	{
		struct hfi_mpeg4_time_resolution *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VENC_MPEG4_TIME_RESOLUTION;
		hfi = (struct hfi_mpeg4_time_resolution *)
			&pkt->rg_property_data[1];
		hfi->time_increment_resolution =
			((struct hal_mpeg4_time_resolution *)pdata)->
					time_increment_resolution;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VENC_MPEG4_HEADER_EXTENSION:
	{
		struct hfi_mpeg4_header_extension *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VENC_MPEG4_HEADER_EXTENSION;
		hfi = (struct hfi_mpeg4_header_extension *)
			&pkt->rg_property_data[1];
		hfi->header_extension = (u32) pdata;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_PARAM_VENC_H264_DEBLOCK_CONTROL:
	{
		struct hfi_h264_db_control *hfi;
		struct hal_h264_db_control *prop =
			(struct hal_h264_db_control *) pdata;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VENC_H264_DEBLOCK_CONTROL;
		hfi = (struct hfi_h264_db_control *) &pkt->rg_property_data[1];
		switch (prop->mode) {
		case HAL_H264_DB_MODE_DISABLE:
			hfi->mode = HFI_H264_DB_MODE_DISABLE;
			break;
		case HAL_H264_DB_MODE_SKIP_SLICE_BOUNDARY:
			hfi->mode = HFI_H264_DB_MODE_SKIP_SLICE_BOUNDARY;
			break;
		case HAL_H264_DB_MODE_ALL_BOUNDARY:
			hfi->mode = HFI_H264_DB_MODE_ALL_BOUNDARY;
			break;
		default:
			dprintk(VIDC_ERR, "Invalid deblocking mode: 0x%x",
						  prop->mode);
			break;
		}
		hfi->slice_alpha_offset = prop->slice_alpha_offset;
		hfi->slice_beta_offset = prop->slice_beta_offset;
		pkt->size += sizeof(u32) +
			sizeof(struct hfi_h264_db_control);
		break;
	}
	case HAL_PARAM_VENC_SESSION_QP:
	{
		struct hfi_quantization *hfi;
		struct hal_quantization *hal_quant =
			(struct hal_quantization *) pdata;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VENC_SESSION_QP;
		hfi = (struct hfi_quantization *) &pkt->rg_property_data[1];
		hfi->qp_i = hal_quant->qpi;
		hfi->qp_p = hal_quant->qpp;
		hfi->qp_b = hal_quant->qpb;
		hfi->layer_id = hal_quant->layer_id;
		pkt->size += sizeof(u32) + sizeof(struct hfi_quantization);
		break;
	}
	case HAL_CONFIG_VENC_INTRA_PERIOD:
	{
		struct hfi_intra_period *hfi;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_CONFIG_VENC_INTRA_PERIOD;
		hfi = (struct hfi_intra_period *) &pkt->rg_property_data[1];
		memcpy(hfi, (struct hfi_intra_period *) pdata,
				sizeof(struct hfi_intra_period));
		pkt->size += sizeof(u32) + sizeof(struct hfi_intra_period);
		break;
	}
	case HAL_CONFIG_VENC_IDR_PERIOD:
	{
		struct hfi_idr_period *hfi;
		pkt->rg_property_data[0] = HFI_PROPERTY_CONFIG_VENC_IDR_PERIOD;
		hfi = (struct hfi_idr_period *) &pkt->rg_property_data[1];
		hfi->idr_period = ((struct hfi_idr_period *) pdata)->idr_period;
		pkt->size += sizeof(u32) * 2;
		break;
	}
	case HAL_CONFIG_VPE_OPERATIONS:
		break;
	case HAL_PARAM_VENC_INTRA_REFRESH:
	{
		struct hfi_intra_refresh *hfi;
		struct hal_intra_refresh *prop =
			(struct hal_intra_refresh *) pdata;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VENC_INTRA_REFRESH;
		hfi = (struct hfi_intra_refresh *) &pkt->rg_property_data[1];
		switch (prop->mode) {
		case HAL_INTRA_REFRESH_NONE:
			hfi->mode = HFI_INTRA_REFRESH_NONE;
			break;
		case HAL_INTRA_REFRESH_ADAPTIVE:
			hfi->mode = HFI_INTRA_REFRESH_ADAPTIVE;
			break;
		case HAL_INTRA_REFRESH_CYCLIC:
			hfi->mode = HFI_INTRA_REFRESH_CYCLIC;
			break;
		case HAL_INTRA_REFRESH_CYCLIC_ADAPTIVE:
			hfi->mode = HFI_INTRA_REFRESH_CYCLIC_ADAPTIVE;
			break;
		case HAL_INTRA_REFRESH_RANDOM:
			hfi->mode = HFI_INTRA_REFRESH_RANDOM;
			break;
		default:
			dprintk(VIDC_ERR, "Invalid intra refresh setting: 0x%x",
				prop->mode);
			break;
		}
		hfi->air_mbs = prop->air_mbs;
		hfi->air_ref = prop->air_ref;
		hfi->cir_mbs = prop->cir_mbs;
		pkt->size += sizeof(u32) + sizeof(struct hfi_intra_refresh);
		break;
	}
	case HAL_PARAM_VENC_MULTI_SLICE_CONTROL:
	{
		struct hfi_multi_slice_control *hfi;
		struct hal_multi_slice_control *prop =
			(struct hal_multi_slice_control *) pdata;
		pkt->rg_property_data[0] =
			HFI_PROPERTY_PARAM_VENC_MULTI_SLICE_CONTROL;
		hfi = (struct hfi_multi_slice_control *)
			&pkt->rg_property_data[1];
		switch (prop->multi_slice) {
		case HAL_MULTI_SLICE_OFF:
			hfi->multi_slice = HFI_MULTI_SLICE_OFF;
			break;
		case HAL_MULTI_SLICE_GOB:
			hfi->multi_slice = HFI_MULTI_SLICE_GOB;
			break;
		case HAL_MULTI_SLICE_BY_MB_COUNT:
			hfi->multi_slice = HFI_MULTI_SLICE_BY_MB_COUNT;
			break;
		case HAL_MULTI_SLICE_BY_BYTE_COUNT:
			hfi->multi_slice = HFI_MULTI_SLICE_BY_BYTE_COUNT;
			break;
		default:
			dprintk(VIDC_ERR, "Invalid slice settings: 0x%x",
				prop->multi_slice);
			break;
		}
		hfi->slice_size = prop->slice_size;
		pkt->size += sizeof(u32) + sizeof(struct
					hfi_multi_slice_control);
		break;
	}
	case HAL_PARAM_INDEX_EXTRADATA:
	{
		struct hfi_index_extradata_config *hfi;
		struct hal_extradata_enable *extra = pdata;
		int index = 0;
		pkt->rg_property_data[0] =
			get_hfi_extradata_index(extra->index);
		hfi =
			(struct hfi_index_extradata_config *)
			&pkt->rg_property_data[1];
		hfi->enable = extra->enable;
		index = get_hfi_extradata_index(extra->index);
		if (index)
			hfi->index_extra_data_id = index;
		else {
			dprintk(VIDC_WARN,
				"Failed to find extradata index: %d\n",
				index);
			rc = -EINVAL;
		}
		pkt->size += sizeof(u32) +
			sizeof(struct hfi_index_extradata_config);
		break;
	}
	case HAL_CONFIG_VPE_DEINTERLACE:
		break;
	/* FOLLOWING PROPERTIES ARE NOT IMPLEMENTED IN CORE YET */
	case HAL_CONFIG_BUFFER_REQUIREMENTS:
	case HAL_CONFIG_PRIORITY:
	case HAL_CONFIG_BATCH_INFO:
	case HAL_PARAM_METADATA_PASS_THROUGH:
	case HAL_SYS_IDLE_INDICATOR:
	case HAL_PARAM_UNCOMPRESSED_FORMAT_SUPPORTED:
	case HAL_PARAM_INTERLACE_FORMAT_SUPPORTED:
	case HAL_PARAM_CHROMA_SITE:
	case HAL_PARAM_PROPERTIES_SUPPORTED:
	case HAL_PARAM_PROFILE_LEVEL_SUPPORTED:
	case HAL_PARAM_CAPABILITY_SUPPORTED:
	case HAL_PARAM_NAL_STREAM_FORMAT_SUPPORTED:
	case HAL_PARAM_MULTI_VIEW_FORMAT:
	case HAL_PARAM_MAX_SEQUENCE_HEADER_SIZE:
	case HAL_PARAM_CODEC_SUPPORTED:
	case HAL_PARAM_VDEC_MULTI_VIEW_SELECT:
	case HAL_PARAM_VDEC_MB_QUANTIZATION:
	case HAL_PARAM_VDEC_NUM_CONCEALED_MB:
	case HAL_PARAM_VDEC_H264_ENTROPY_SWITCHING:
	case HAL_PARAM_VENC_SLICE_DELIVERY_MODE:
	case HAL_PARAM_VENC_MPEG4_DATA_PARTITIONING:
	case HAL_CONFIG_BUFFER_COUNT_ACTUAL:
	case HAL_CONFIG_VDEC_MULTI_STREAM:
	case HAL_PARAM_VENC_MULTI_SLICE_INFO:
	case HAL_CONFIG_VENC_TIMESTAMP_SCALE:
	case HAL_PARAM_VENC_LOW_LATENCY:
	default:
		dprintk(VIDC_ERR, "DEFAULT: Calling 0x%x", ptype);
		break;
	}
	return rc;
}

static int get_hfi_ssr_type(enum hal_ssr_trigger_type type)
{
	int rc = HFI_TEST_SSR_HW_WDOG_IRQ;
	switch (type) {
	case SSR_ERR_FATAL:
		rc = HFI_TEST_SSR_SW_ERR_FATAL;
		break;
	case SSR_SW_DIV_BY_ZERO:
		rc = HFI_TEST_SSR_SW_DIV_BY_ZERO;
		break;
	case SSR_HW_WDOG_IRQ:
		rc = HFI_TEST_SSR_HW_WDOG_IRQ;
		break;
	default:
		dprintk(VIDC_WARN,
			"SSR trigger type not recognized, using WDOG.\n");
	}
	return rc;
}

int create_pkt_ssr_cmd(enum hal_ssr_trigger_type type,
		struct hfi_cmd_sys_test_ssr_packet *pkt)
{
	if (!pkt) {
		dprintk(VIDC_ERR, "Invalid params, device: %p\n", pkt);
		return -EINVAL;
	}
	pkt->size = sizeof(struct hfi_cmd_sys_test_ssr_packet);
	pkt->packet_type = HFI_CMD_SYS_TEST_SSR;
	pkt->trigger_type = get_hfi_ssr_type(type);
	return 0;
}
