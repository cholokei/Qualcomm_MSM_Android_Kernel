#ifndef _UAPI_MEDIA_MSM_VIDC_H
#define _UAPI_MEDIA_MSM_VIDC_H

struct msm_vidc_interlace_payload {
	unsigned int format;
};
struct msm_vidc_framerate_payload {
	unsigned int frame_rate;
};
struct msm_vidc_ts_payload {
	unsigned int timestamp_hi;
	unsigned int timestamp_lo;
};
struct msm_vidc_concealmb_payload {
	unsigned int num_mbs;
};
struct msm_vidc_recoverysei_payload {
	unsigned int flags;
};
struct msm_vidc_panscan_window {
	unsigned int panscan_height_offset;
	unsigned int panscan_width_offset;
	unsigned int panscan_window_width;
	unsigned int panscan_window_height;
};
struct msm_vidc_panscan_window_payload {
	unsigned int num_panscan_windows;
	struct msm_vidc_panscan_window wnd[1];
};
enum msm_vidc_extradata_type {
	EXTRADATA_NONE = 0x00000000,
	EXTRADATA_MB_QUANTIZATION = 0x00000001,
	EXTRADATA_INTERLACE_VIDEO = 0x00000002,
	EXTRADATA_VC1_FRAMEDISP = 0x00000003,
	EXTRADATA_VC1_SEQDISP = 0x00000004,
	EXTRADATA_TIMESTAMP = 0x00000005,
	EXTRADATA_S3D_FRAME_PACKING = 0x00000006,
	EXTRADATA_FRAME_RATE = 0x00000007,
	EXTRADATA_PANSCAN_WINDOW = 0x00000008,
	EXTRADATA_RECOVERY_POINT_SEI = 0x00000009,
	EXTRADATA_MULTISLICE_INFO = 0x7F100000,
	EXTRADATA_NUM_CONCEALED_MB = 0x7F100001,
	EXTRADATA_INDEX = 0x7F100002,
	EXTRADATA_METADATA_FILLER = 0x7FE00002,
};
enum msm_vidc_interlace_type {
	INTERLACE_FRAME_PROGRESSIVE = 0x01,
	INTERLACE_INTERLEAVE_FRAME_TOPFIELDFIRST = 0x02,
	INTERLACE_INTERLEAVE_FRAME_BOTTOMFIELDFIRST = 0x04,
	INTERLACE_FRAME_TOPFIELDFIRST = 0x08,
	INTERLACE_FRAME_BOTTOMFIELDFIRST = 0x10,
};
enum msm_vidc_recovery_sei {
	FRAME_RECONSTRUCTION_INCORRECT = 0x0,
	FRAME_RECONSTRUCTION_CORRECT = 0x01,
	FRAME_RECONSTRUCTION_APPROXIMATELY_CORRECT = 0x02,
};

#endif /* _UAPI_MEDIA_MSM_VIDC_H */
