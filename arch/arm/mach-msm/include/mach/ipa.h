/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
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

#ifndef _IPA_H_
#define _IPA_H_

#include <linux/msm_ipa.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <mach/sps.h>

/**
 * enum ipa_nat_en_type - NAT setting type in IPA end-point
 */
enum ipa_nat_en_type {
	IPA_BYPASS_NAT,
	IPA_SRC_NAT,
	IPA_DST_NAT,
};

/**
 * enum ipa_mode_type - mode setting type in IPA end-point
 * @BASIC: basic mode
 * @ENABLE_FRAMING_HDLC: not currently supported
 * @ENABLE_DEFRAMING_HDLC: not currently supported
 */
enum ipa_mode_type {
	IPA_BASIC,
	IPA_ENABLE_FRAMING_HDLC,
	IPA_ENABLE_DEFRAMING_HDLC,
	IPA_DMA,
};

/**
 *  enum ipa_aggr_en_type - aggregation setting type in IPA
 *  end-point
 */
enum ipa_aggr_en_type {
	IPA_BYPASS_AGGR,
	IPA_ENABLE_AGGR,
	IPA_ENABLE_DEAGGR,
};

/**
 *  enum ipa_aggr_type - type of aggregation in IPA end-point
 */
enum ipa_aggr_type {
	IPA_MBIM_16,
	IPA_MBIM_32,
	IPA_TLP,
};

/**
 * enum ipa_aggr_mode - global aggregation mode
 */
enum ipa_aggr_mode {
	IPA_MBIM,
	IPA_QCNCM,
};

/**
 * enum ipa_dp_evt_type - type of event client callback is
 * invoked for on data path
 * @IPA_RECEIVE: data is struct sk_buff
 * @IPA_WRITE_DONE: data is struct sk_buff
 */
enum ipa_dp_evt_type {
	IPA_RECEIVE,
	IPA_WRITE_DONE,
};

/**
 * struct ipa_ep_cfg_nat - NAT configuration in IPA end-point
 * @nat_en:	This defines the default NAT mode for the pipe: in case of
 *		filter miss - the default NAT mode defines the NATing operation
 *		on the packet. Valid for Input Pipes only (IPA consumer)
 */
struct ipa_ep_cfg_nat {
	enum ipa_nat_en_type nat_en;
};

/**
 * struct ipa_ep_cfg_hdr - header configuration in IPA end-point
 * @hdr_len:	Header length in bytes to be added/removed. Assuming header len
 *		is constant per endpoint. Valid for both Input and Output Pipes
 * @hdr_ofst_metadata_valid:	0: Metadata_Ofst  value is invalid, i.e., no
 *				metadata within header.
 *				1: Metadata_Ofst  value is valid, i.e., metadata
 *				within header is in offset Metadata_Ofst Valid
 *				for Input Pipes only (IPA Consumer) (for output
 *				pipes, metadata already set within the header)
 * @hdr_ofst_metadata:	Offset within header in which metadata resides
 *			Size of metadata - 4bytes
 *			Example -  Stream ID/SSID/mux ID.
 *			Valid for  Input Pipes only (IPA Consumer) (for output
 *			pipes, metadata already set within the header)
 * @hdr_additional_const_len:	Defines the constant length that should be added
 *				to the payload length in order for IPA to update
 *				correctly the length field within the header
 *				(valid only in case Hdr_Ofst_Pkt_Size_Valid=1)
 *				Valid for Output Pipes (IPA Producer)
 * @hdr_ofst_pkt_size_valid:	0: Hdr_Ofst_Pkt_Size  value is invalid, i.e., no
 *				length field within the inserted header
 *				1: Hdr_Ofst_Pkt_Size  value is valid, i.e., a
 *				packet length field resides within the header
 *				Valid for Output Pipes (IPA Producer)
 * @hdr_ofst_pkt_size:	Offset within header in which packet size reside. Upon
 *			Header Insertion, IPA will update this field within the
 *			header with the packet length . Assumption is that
 *			header length field size is constant and is 2Bytes
 *			Valid for Output Pipes (IPA Producer)
 * @hdr_a5_mux:	Determines whether A5 Mux header should be added to the packet.
 *		This bit is valid only when Hdr_En=01(Header Insertion)
 *		SW should set this bit for IPA-to-A5 pipes.
 *		0: Do not insert A5 Mux Header
 *		1: Insert A5 Mux Header
 *		Valid for Output Pipes (IPA Producer)
 */
struct ipa_ep_cfg_hdr {
	u32 hdr_len;
	u32 hdr_ofst_metadata_valid;
	u32 hdr_ofst_metadata;
	u32 hdr_additional_const_len;
	u32 hdr_ofst_pkt_size_valid;
	u32 hdr_ofst_pkt_size;
	u32 hdr_a5_mux;
};

/**
 * struct ipa_ep_cfg_mode - mode configuration in IPA end-point
 * @mode:	Valid for Input Pipes only (IPA Consumer)
 * @dst:	This parameter specifies the output pipe to which the packets
 *		will be routed to.
 *		This parameter is valid for Mode=DMA and not valid for
 *		Mode=Basic
 *		Valid for Input Pipes only (IPA Consumer)
 */
struct ipa_ep_cfg_mode {
	enum ipa_mode_type mode;
	enum ipa_client_type dst;
};

/**
 * struct ipa_ep_cfg_aggr - aggregation configuration in IPA end-point
 * @aggr_en:	Valid for both Input and Output Pipes
 * @aggr:	Valid for both Input and Output Pipes
 * @aggr_byte_limit:	Limit of aggregated packet size in KB (<=32KB) When set
 *			to 0, there is no size limitation on the aggregation.
 *			When both, Aggr_Byte_Limit and Aggr_Time_Limit are set
 *			to 0, there is no aggregation, every packet is sent
 *			independently according to the aggregation structure
 *			Valid for Output Pipes only (IPA Producer )
 * @aggr_time_limit:	Timer to close aggregated packet (<=32ms) When set to 0,
 *			there is no time limitation on the aggregation.  When
 *			both, Aggr_Byte_Limit and Aggr_Time_Limit are set to 0,
 *			there is no aggregation, every packet is sent
 *			independently according to the aggregation structure
 *			Valid for Output Pipes only (IPA Producer)
 */
struct ipa_ep_cfg_aggr {
	enum ipa_aggr_en_type aggr_en;
	enum ipa_aggr_type aggr;
	u32 aggr_byte_limit;
	u32 aggr_time_limit;
};

/**
 * struct ipa_ep_cfg_route - route configuration in IPA end-point
 * @rt_tbl_hdl:	Defines the default routing table index to be used in case there
 *		is no filter rule matching, valid for Input Pipes only (IPA
 *		Consumer). Clients should set this to 0 which will cause default
 *		v4 and v6 routes setup internally by IPA driver to be used for
 *		this end-point
 */
struct ipa_ep_cfg_route {
	u32 rt_tbl_hdl;
};

/**
 * struct ipa_ep_cfg - configuration of IPA end-point
 * @nat:	NAT parmeters
 * @hdr:	Header parameters
 * @mode:	Mode parameters
 * @aggr:	Aggregation parameters
 * @route:	Routing parameters
 */
struct ipa_ep_cfg {
	struct ipa_ep_cfg_nat nat;
	struct ipa_ep_cfg_hdr hdr;
	struct ipa_ep_cfg_mode mode;
	struct ipa_ep_cfg_aggr aggr;
	struct ipa_ep_cfg_route route;
};

/**
 * struct ipa_connect_params - low-level client connect input parameters. Either
 * client allocates the data and desc FIFO and specifies that in data+desc OR
 * specifies sizes and pipe_mem pref and IPA does the allocation.
 *
 * @ipa_ep_cfg:	IPA EP configuration
 * @client:	type of "client"
 * @client_bam_hdl:	 client SPS handle
 * @client_ep_idx:	 client PER EP index
 * @priv:	callback cookie
 * @notify:	callback
 *		priv - callback cookie evt - type of event data - data relevant
 *		to event.  May not be valid. See event_type enum for valid
 *		cases.
 * @desc_fifo_sz:	size of desc FIFO
 * @data_fifo_sz:	size of data FIFO
 * @pipe_mem_preferred:	if true, try to alloc the FIFOs in pipe mem, fallback
 *			to sys mem if pipe mem alloc fails
 * @desc:	desc FIFO meta-data when client has allocated it
 * @data:	data FIFO meta-data when client has allocated it
 */
struct ipa_connect_params {
	struct ipa_ep_cfg ipa_ep_cfg;
	enum ipa_client_type client;
	u32 client_bam_hdl;
	u32 client_ep_idx;
	void *priv;
	void (*notify)(void *priv, enum ipa_dp_evt_type evt,
			unsigned long data);
	u32 desc_fifo_sz;
	u32 data_fifo_sz;
	bool pipe_mem_preferred;
	struct sps_mem_buffer desc;
	struct sps_mem_buffer data;
};

/**
 *  struct ipa_sps_params - SPS related output parameters resulting from
 *  low/high level client connect
 *  @ipa_bam_hdl:	IPA SPS handle
 *  @ipa_ep_idx:	IPA PER EP index
 *  @desc:	desc FIFO meta-data
 *  @data:	data FIFO meta-data
 */
struct ipa_sps_params {
	u32 ipa_bam_hdl;
	u32 ipa_ep_idx;
	struct sps_mem_buffer desc;
	struct sps_mem_buffer data;
};

/**
 * struct ipa_tx_intf - interface tx properties
 * @num_props:	number of tx properties
 * @prop:	the tx properties array
 */
struct ipa_tx_intf {
	u32 num_props;
	struct ipa_ioc_tx_intf_prop *prop;
};

/**
 * struct ipa_rx_intf - interface rx properties
 * @num_props:	number of rx properties
 * @prop:	the rx properties array
 */
struct ipa_rx_intf {
	u32 num_props;
	struct ipa_ioc_rx_intf_prop *prop;
};

/**
 * struct ipa_sys_connect_params - information needed to setup an IPA end-point
 * in system-BAM mode
 * @ipa_ep_cfg:	IPA EP configuration
 * @client:	the type of client who "owns" the EP
 * @desc_fifo_sz:	size of desc FIFO
 * @priv:	callback cookie
 * @notify:	callback
 *		priv - callback cookie
 *		evt - type of event
 *		data - data relevant to event.  May not be valid. See event_type
 *		enum for valid cases.
 */
struct ipa_sys_connect_params {
	struct ipa_ep_cfg ipa_ep_cfg;
	enum ipa_client_type client;
	u32 desc_fifo_sz;
	void *priv;
	void (*notify)(void *priv,
			enum ipa_dp_evt_type evt,
			unsigned long data);
};

/**
 * struct ipa_msg_meta_wrapper - message meta-data wrapper
 * @meta:	the meta-data itself
 * @link:	opaque to client
 * @meta_wrapper_free:	function to free the metadata wrapper when IPA driver
 *			is done with it
 */
struct ipa_msg_meta_wrapper {
	struct ipa_msg_meta meta;
	struct list_head link;
	void (*meta_wrapper_free)(struct ipa_msg_meta_wrapper *buff);
};

/**
 * struct ipa_tx_meta - meta-data for the TX packet
 * @mbim_stream_id:	the stream ID used in NDP signature
 * @mbim_stream_id_valid:	 is above field valid?
 */
struct ipa_tx_meta {
	u8 mbim_stream_id;
	bool mbim_stream_id_valid;
};

/**
 * struct ipa_msg_wrapper - message wrapper
 * @msg:	the message buffer itself, MUST exist after call returns, will
 *		be freed by IPA driver when it is done with it
 * @link:	opaque to client
 * @msg_free:	function to free the message when IPA driver is done with it
 * @msg_wrapper_free:	function to free the message wrapper when IPA driver is
 *			done with it
 */
struct ipa_msg_wrapper {
	void *msg;
	struct list_head link;
	void (*msg_free)(void *msg);
	void (*msg_wrapper_free)(struct ipa_msg_wrapper *buff);
};

/**
 * typedef ipa_pull_fn - callback function
 * @buf - [in] the buffer to populate the message into
 * @sz - [in] the size of the buffer
 *
 * callback function registered by kernel client with IPA driver for IPA driver
 * to be able to pull messages from the kernel client asynchronously.
 *
 * Returns how many bytes were copied into the buffer, negative on failure.
 */
typedef int (*ipa_pull_fn)(void *buf, uint16_t sz);

#ifdef CONFIG_IPA

/*
 * Connect / Disconnect
 */
int ipa_connect(const struct ipa_connect_params *in, struct ipa_sps_params *sps,
		u32 *clnt_hdl);
int ipa_disconnect(u32 clnt_hdl);

/*
 * Configuration
 */
int ipa_cfg_ep(u32 clnt_hdl, const struct ipa_ep_cfg *ipa_ep_cfg);

int ipa_cfg_ep_nat(u32 clnt_hdl, const struct ipa_ep_cfg_nat *ipa_ep_cfg);

int ipa_cfg_ep_hdr(u32 clnt_hdl, const struct ipa_ep_cfg_hdr *ipa_ep_cfg);

int ipa_cfg_ep_mode(u32 clnt_hdl, const struct ipa_ep_cfg_mode *ipa_ep_cfg);

int ipa_cfg_ep_aggr(u32 clnt_hdl, const struct ipa_ep_cfg_aggr *ipa_ep_cfg);

int ipa_cfg_ep_route(u32 clnt_hdl, const struct ipa_ep_cfg_route *ipa_ep_cfg);

/*
 * Header removal / addition
 */
int ipa_add_hdr(struct ipa_ioc_add_hdr *hdrs);

int ipa_del_hdr(struct ipa_ioc_del_hdr *hdls);

int ipa_commit_hdr(void);

int ipa_reset_hdr(void);

int ipa_get_hdr(struct ipa_ioc_get_hdr *lookup);

int ipa_put_hdr(u32 hdr_hdl);

int ipa_copy_hdr(struct ipa_ioc_copy_hdr *copy);

/*
 * Routing
 */
int ipa_add_rt_rule(struct ipa_ioc_add_rt_rule *rules);

int ipa_del_rt_rule(struct ipa_ioc_del_rt_rule *hdls);

int ipa_commit_rt(enum ipa_ip_type ip);

int ipa_reset_rt(enum ipa_ip_type ip);

int ipa_get_rt_tbl(struct ipa_ioc_get_rt_tbl *lookup);

int ipa_put_rt_tbl(u32 rt_tbl_hdl);

/*
 * Filtering
 */
int ipa_add_flt_rule(struct ipa_ioc_add_flt_rule *rules);

int ipa_del_flt_rule(struct ipa_ioc_del_flt_rule *hdls);

int ipa_commit_flt(enum ipa_ip_type ip);

int ipa_reset_flt(enum ipa_ip_type ip);

/*
 * NAT
 */
int allocate_nat_device(struct ipa_ioc_nat_alloc_mem *mem);

int ipa_nat_init_cmd(struct ipa_ioc_v4_nat_init *init);

int ipa_nat_dma_cmd(struct ipa_ioc_nat_dma_cmd *dma);

int ipa_nat_del_cmd(struct ipa_ioc_v4_nat_del *del);

/*
 * Aggregation
 */
int ipa_set_aggr_mode(enum ipa_aggr_mode mode);

int ipa_set_qcncm_ndp_sig(char sig[3]);

int ipa_set_single_ndp_per_mbim(bool enable);

/*
 * rmnet bridge
 */
int rmnet_bridge_init(void);

int rmnet_bridge_disconnect(void);

int rmnet_bridge_connect(u32 producer_hdl,
			 u32 consumer_hdl,
			 int wwan_logical_channel_id);

/*
 * Data path
 */
int ipa_tx_dp(enum ipa_client_type dst, struct sk_buff *skb,
		struct ipa_tx_meta *metadata);

/*
 * System pipes
 */
int ipa_setup_sys_pipe(struct ipa_sys_connect_params *sys_in, u32 *clnt_hdl);

int ipa_teardown_sys_pipe(u32 clnt_hdl);

#else /* CONFIG_IPA */

/*
 * Connect / Disconnect
 */
static inline int ipa_connect(const struct ipa_connect_params *in,
		struct ipa_sps_params *sps,	u32 *clnt_hdl)
{
	return -EPERM;
}

static inline int ipa_disconnect(u32 clnt_hdl)
{
	return -EPERM;
}


/*
 * Configuration
 */
static inline int ipa_cfg_ep(u32 clnt_hdl,
		const struct ipa_ep_cfg *ipa_ep_cfg)
{
	return -EPERM;
}


static inline int ipa_cfg_ep_nat(u32 clnt_hdl,
		const struct ipa_ep_cfg_nat *ipa_ep_cfg)
{
	return -EPERM;
}


static inline int ipa_cfg_ep_hdr(u32 clnt_hdl,
		const struct ipa_ep_cfg_hdr *ipa_ep_cfg)
{
	return -EPERM;
}


static inline int ipa_cfg_ep_mode(u32 clnt_hdl,
		const struct ipa_ep_cfg_mode *ipa_ep_cfg)
{
	return -EPERM;
}


static inline int ipa_cfg_ep_aggr(u32 clnt_hdl,
		const struct ipa_ep_cfg_aggr *ipa_ep_cfg)
{
	return -EPERM;
}


static inline int ipa_cfg_ep_route(u32 clnt_hdl,
		const struct ipa_ep_cfg_route *ipa_ep_cfg)
{
	return -EPERM;
}


/*
 * Header removal / addition
 */
static inline int ipa_add_hdr(struct ipa_ioc_add_hdr *hdrs)
{
	return -EPERM;
}


static inline int ipa_del_hdr(struct ipa_ioc_del_hdr *hdls)
{
	return -EPERM;
}


static inline int ipa_commit_hdr(void)
{
	return -EPERM;
}


static inline int ipa_reset_hdr(void)
{
	return -EPERM;
}


static inline int ipa_get_hdr(struct ipa_ioc_get_hdr *lookup)
{
	return -EPERM;
}


static inline int ipa_put_hdr(u32 hdr_hdl)
{
	return -EPERM;
}


static inline int ipa_copy_hdr(struct ipa_ioc_copy_hdr *copy)
{
	return -EPERM;
}


/*
 * Routing
 */
static inline int ipa_add_rt_rule(struct ipa_ioc_add_rt_rule *rules)
{
	return -EPERM;
}


static inline int ipa_del_rt_rule(struct ipa_ioc_del_rt_rule *hdls)
{
	return -EPERM;
}


static inline int ipa_commit_rt(enum ipa_ip_type ip)
{
	return -EPERM;
}


static inline int ipa_reset_rt(enum ipa_ip_type ip)
{
	return -EPERM;
}


static inline int ipa_get_rt_tbl(struct ipa_ioc_get_rt_tbl *lookup)
{
	return -EPERM;
}


static inline int ipa_put_rt_tbl(u32 rt_tbl_hdl)
{
	return -EPERM;
}


/*
 * Filtering
 */
static inline int ipa_add_flt_rule(struct ipa_ioc_add_flt_rule *rules)
{
	return -EPERM;
}


static inline int ipa_del_flt_rule(struct ipa_ioc_del_flt_rule *hdls)
{
	return -EPERM;
}


static inline int ipa_commit_flt(enum ipa_ip_type ip)
{
	return -EPERM;
}


static inline int ipa_reset_flt(enum ipa_ip_type ip)
{
	return -EPERM;
}


/*
 * NAT
 */
static inline int allocate_nat_device(struct ipa_ioc_nat_alloc_mem *mem)
{
	return -EPERM;
}


static inline int ipa_nat_init_cmd(struct ipa_ioc_v4_nat_init *init)
{
	return -EPERM;
}


static inline int ipa_nat_dma_cmd(struct ipa_ioc_nat_dma_cmd *dma)
{
	return -EPERM;
}


static inline int ipa_nat_del_cmd(struct ipa_ioc_v4_nat_del *del)
{
	return -EPERM;
}


/*
 * Aggregation
 */
static inline int ipa_set_aggr_mode(enum ipa_aggr_mode mode)
{
	return -EPERM;
}


static inline int ipa_set_qcncm_ndp_sig(char sig[3])
{
	return -EPERM;
}


static inline int ipa_set_single_ndp_per_mbim(bool enable)
{
	return -EPERM;
}


/*
 * rmnet bridge
 */
static inline int rmnet_bridge_init(void)
{
	return -EPERM;
}


static inline int rmnet_bridge_disconnect(void)
{
	return -EPERM;
}


static inline int rmnet_bridge_connect(u32 producer_hdl,
			 u32 consumer_hdl,
			 int wwan_logical_channel_id)
{
	return -EPERM;
}


/*
 * Data path
 */
static inline int ipa_tx_dp(enum ipa_client_type dst, struct sk_buff *skb,
		struct ipa_tx_meta *metadata)
{
	return -EPERM;
}


/*
 * System pipes
 */
static inline int ipa_setup_sys_pipe(struct ipa_sys_connect_params *sys_in,
		u32 *clnt_hdl)
{
	return -EPERM;
}


static inline int ipa_teardown_sys_pipe(u32 clnt_hdl)
{
	return -EPERM;
}


#endif /* CONFIG_IPA*/

#endif /* _IPA_H_ */
