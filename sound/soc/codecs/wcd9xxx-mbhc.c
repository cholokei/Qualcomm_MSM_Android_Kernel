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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/ratelimit.h>
#include <linux/debugfs.h>
#include <linux/mfd/wcd9xxx/core.h>
#include <linux/mfd/wcd9xxx/wcd9xxx_registers.h>
#include <linux/mfd/wcd9xxx/wcd9320_registers.h>
#include <linux/mfd/wcd9xxx/pdata.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include "wcd9320.h"
#include "wcd9xxx-mbhc.h"
#include "wcd9xxx-resmgr.h"

#define WCD9XXX_JACK_MASK (SND_JACK_HEADSET | SND_JACK_OC_HPHL | \
			   SND_JACK_OC_HPHR | SND_JACK_LINEOUT | \
			   SND_JACK_UNSUPPORTED)
#define WCD9XXX_JACK_BUTTON_MASK (SND_JACK_BTN_0 | SND_JACK_BTN_1 | \
				  SND_JACK_BTN_2 | SND_JACK_BTN_3 | \
				  SND_JACK_BTN_4 | SND_JACK_BTN_5 | \
				  SND_JACK_BTN_6 | SND_JACK_BTN_7)

#define NUM_DCE_PLUG_DETECT 3
#define NUM_ATTEMPTS_INSERT_DETECT 25
#define NUM_ATTEMPTS_TO_REPORT 5

#define FAKE_INS_LOW 10
#define FAKE_INS_HIGH 80
#define FAKE_INS_HIGH_NO_SWCH 150
#define FAKE_REMOVAL_MIN_PERIOD_MS 50
#define FAKE_INS_DELTA_SCALED_MV 300

#define BUTTON_MIN 0x8000
#define STATUS_REL_DETECTION 0x0C

#define HS_DETECT_PLUG_TIME_MS (5 * 1000)
#define HS_DETECT_PLUG_INERVAL_MS 100
#define SWCH_REL_DEBOUNCE_TIME_MS 50
#define SWCH_IRQ_DEBOUNCE_TIME_US 5000

#define GND_MIC_SWAP_THRESHOLD 2
#define OCP_ATTEMPT 1

#define FW_READ_ATTEMPTS 15
#define FW_READ_TIMEOUT 2000000

#define BUTTON_POLLING_SUPPORTED true

#define MCLK_RATE_12288KHZ 12288000
#define MCLK_RATE_9600KHZ 9600000
#define WCD9XXX_RCO_CLK_RATE MCLK_RATE_12288KHZ

#define DEFAULT_DCE_STA_WAIT 55
#define DEFAULT_DCE_WAIT 60000
#define DEFAULT_STA_WAIT 5000

#define VDDIO_MICBIAS_MV 1800

enum meas_type {
	STA = 0,
	DCE,
};

enum {
	MBHC_USE_HPHL_TRIGGER = 1,
	MBHC_USE_MB_TRIGGER = 2
};

/*
 * Flags to track of PA and DAC state.
 * PA and DAC should be tracked separately as AUXPGA loopback requires
 * only PA to be turned on without DAC being on.
 */
enum pa_dac_ack_flags {
	WCD9XXX_HPHL_PA_OFF_ACK = 0,
	WCD9XXX_HPHR_PA_OFF_ACK,
	WCD9XXX_HPHL_DAC_OFF_ACK,
	WCD9XXX_HPHR_DAC_OFF_ACK
};

static bool wcd9xxx_mbhc_polling(struct wcd9xxx_mbhc *mbhc)
{
	return mbhc->polling_active;
}

static void wcd9xxx_turn_onoff_override(struct snd_soc_codec *codec, bool on)
{
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x04, on << 2);
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_pause_hs_polling(struct wcd9xxx_mbhc *mbhc)
{
	struct snd_soc_codec *codec = mbhc->codec;

	pr_debug("%s: enter\n", __func__);
	if (!mbhc->polling_active) {
		pr_debug("polling not active, nothing to pause\n");
		return;
	}

	/* Soft reset MBHC block */
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8, 0x8);
	pr_debug("%s: leave\n", __func__);
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_start_hs_polling(struct wcd9xxx_mbhc *mbhc)
{
	struct snd_soc_codec *codec = mbhc->codec;
	int mbhc_state = mbhc->mbhc_state;

	pr_debug("%s: enter\n", __func__);
	if (!mbhc->polling_active) {
		pr_debug("Polling is not active, do not start polling\n");
		return;
	}
	snd_soc_write(codec, WCD9XXX_A_MBHC_SCALING_MUX_1, 0x84);

	if (!mbhc->no_mic_headset_override &&
	    mbhc_state == MBHC_STATE_POTENTIAL) {
		pr_debug("%s recovering MBHC state macine\n", __func__);
		mbhc->mbhc_state = MBHC_STATE_POTENTIAL_RECOVERY;
		/* set to max button press threshold */
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B2_CTL, 0x7F);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B1_CTL, 0xFF);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B4_CTL, 0x7F);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B3_CTL, 0xFF);
		/* set to max */
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B6_CTL, 0x7F);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B5_CTL, 0xFF);
	}

	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x1);
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8, 0x0);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x1);
	pr_debug("%s: leave\n", __func__);
}

/* called under codec_resource_lock acquisition */
static void __wcd9xxx_switch_micbias(struct wcd9xxx_mbhc *mbhc,
				     int vddio_switch, bool restartpolling,
				     bool checkpolling)
{
	int cfilt_k_val;
	bool override;
	struct snd_soc_codec *codec;

	codec = mbhc->codec;

	if (vddio_switch && !mbhc->mbhc_micbias_switched &&
	    (!checkpolling || mbhc->polling_active)) {
		if (restartpolling)
			wcd9xxx_pause_hs_polling(mbhc);
		override = snd_soc_read(codec, WCD9XXX_A_CDC_MBHC_B1_CTL) &
			   0x04;
		if (!override)
			wcd9xxx_turn_onoff_override(codec, true);
		/* Adjust threshold if Mic Bias voltage changes */
		if (mbhc->mbhc_data.micb_mv != VDDIO_MICBIAS_MV) {
			cfilt_k_val = wcd9xxx_resmgr_get_k_val(mbhc->resmgr,
							      VDDIO_MICBIAS_MV);
			usleep_range(10000, 10000);
			snd_soc_update_bits(codec,
					mbhc->mbhc_bias_regs.cfilt_val,
					0xFC, (cfilt_k_val << 2));
			usleep_range(10000, 10000);
			snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B1_CTL,
				      mbhc->mbhc_data.adj_v_ins_hu & 0xFF);
			snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B2_CTL,
				      (mbhc->mbhc_data.adj_v_ins_hu >> 8) &
				      0xFF);
			pr_debug("%s: Programmed MBHC thresholds to VDDIO\n",
				 __func__);
		}

		/* Enable MIC BIAS Switch to VDDIO */
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg,
				    0x80, 0x80);
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg,
				    0x10, 0x00);
		if (!override)
			wcd9xxx_turn_onoff_override(codec, false);
		if (restartpolling)
			wcd9xxx_start_hs_polling(mbhc);

		mbhc->mbhc_micbias_switched = true;
		pr_debug("%s: VDDIO switch enabled\n", __func__);
	} else if (!vddio_switch && mbhc->mbhc_micbias_switched) {
		if ((!checkpolling || mbhc->polling_active) &&
		    restartpolling)
			wcd9xxx_pause_hs_polling(mbhc);
		/* Reprogram thresholds */
		if (mbhc->mbhc_data.micb_mv != VDDIO_MICBIAS_MV) {
			cfilt_k_val =
			    wcd9xxx_resmgr_get_k_val(mbhc->resmgr,
						     mbhc->mbhc_data.micb_mv);
			snd_soc_update_bits(codec,
					mbhc->mbhc_bias_regs.cfilt_val,
					0xFC, (cfilt_k_val << 2));
			usleep_range(10000, 10000);
			snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B1_CTL,
					mbhc->mbhc_data.v_ins_hu & 0xFF);
			snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B2_CTL,
					(mbhc->mbhc_data.v_ins_hu >> 8) & 0xFF);
			pr_debug("%s: Programmed MBHC thresholds to MICBIAS\n",
					__func__);
		}

		/* Disable MIC BIAS Switch to VDDIO */
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg, 0x80,
				    0x00);
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg, 0x10,
				    0x00);

		if ((!checkpolling || mbhc->polling_active) && restartpolling)
			wcd9xxx_start_hs_polling(mbhc);

		mbhc->mbhc_micbias_switched = false;
		pr_debug("%s: VDDIO switch disabled\n", __func__);
	}
}

static void wcd9xxx_switch_micbias(struct wcd9xxx_mbhc *mbhc, int vddio_switch)
{
	return __wcd9xxx_switch_micbias(mbhc, vddio_switch, true, true);
}

static s16 wcd9xxx_get_current_v_ins(struct wcd9xxx_mbhc *mbhc, bool hu)
{
	s16 v_ins;
	if ((mbhc->mbhc_data.micb_mv != VDDIO_MICBIAS_MV) &&
	    mbhc->mbhc_micbias_switched)
		v_ins = hu ? (s16)mbhc->mbhc_data.adj_v_ins_hu :
			(s16)mbhc->mbhc_data.adj_v_ins_h;
	else
		v_ins = hu ? (s16)mbhc->mbhc_data.v_ins_hu :
			(s16)mbhc->mbhc_data.v_ins_h;
	return v_ins;
}

void *wcd9xxx_mbhc_cal_btn_det_mp(
			    const struct wcd9xxx_mbhc_btn_detect_cfg *btn_det,
			    const enum wcd9xxx_mbhc_btn_det_mem mem)
{
	void *ret = &btn_det->_v_btn_low;

	switch (mem) {
	case MBHC_BTN_DET_GAIN:
		ret += sizeof(btn_det->_n_cic);
	case MBHC_BTN_DET_N_CIC:
		ret += sizeof(btn_det->_n_ready);
	case MBHC_BTN_DET_N_READY:
		ret += sizeof(btn_det->_v_btn_high[0]) * btn_det->num_btn;
	case MBHC_BTN_DET_V_BTN_HIGH:
		ret += sizeof(btn_det->_v_btn_low[0]) * btn_det->num_btn;
	case MBHC_BTN_DET_V_BTN_LOW:
		/* do nothing */
		break;
	default:
		ret = NULL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(wcd9xxx_mbhc_cal_btn_det_mp);

static void wcd9xxx_calibrate_hs_polling(struct wcd9xxx_mbhc *mbhc)
{
	struct snd_soc_codec *codec = mbhc->codec;
	const s16 v_ins_hu = wcd9xxx_get_current_v_ins(mbhc, true);

	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B1_CTL, v_ins_hu & 0xFF);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B2_CTL,
		      (v_ins_hu >> 8) & 0xFF);

	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B3_CTL,
		      mbhc->mbhc_data.v_b1_hu & 0xFF);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B4_CTL,
		      (mbhc->mbhc_data.v_b1_hu >> 8) & 0xFF);

	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B5_CTL,
		      mbhc->mbhc_data.v_b1_h & 0xFF);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B6_CTL,
		      (mbhc->mbhc_data.v_b1_h >> 8) & 0xFF);

	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B9_CTL,
		      mbhc->mbhc_data.v_brh & 0xFF);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B10_CTL,
		      (mbhc->mbhc_data.v_brh >> 8) & 0xFF);

	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B11_CTL,
		      mbhc->mbhc_data.v_brl & 0xFF);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_VOLT_B12_CTL,
		      (mbhc->mbhc_data.v_brl >> 8) & 0xFF);
}

static void wcd9xxx_codec_switch_cfilt_mode(struct wcd9xxx_mbhc *mbhc,
					    bool fast)
{
	struct snd_soc_codec *codec = mbhc->codec;
	u8 reg_mode_val, cur_mode_val;

	if (fast)
		reg_mode_val = WCD9XXX_CFILT_FAST_MODE;
	else
		reg_mode_val = WCD9XXX_CFILT_SLOW_MODE;

	cur_mode_val =
	    snd_soc_read(codec, mbhc->mbhc_bias_regs.cfilt_ctl) & 0x40;

	if (cur_mode_val != reg_mode_val) {
		if (mbhc->polling_active)
			wcd9xxx_pause_hs_polling(mbhc);
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.cfilt_ctl, 0x40,
				    reg_mode_val);
		if (mbhc->polling_active)
			wcd9xxx_start_hs_polling(mbhc);
		pr_debug("%s: CFILT mode change (%x to %x)\n", __func__,
			cur_mode_val, reg_mode_val);
	} else {
		pr_debug("%s: CFILT Value is already %x\n",
			__func__, cur_mode_val);
	}
}

static void wcd9xxx_jack_report(struct snd_soc_jack *jack, int status, int mask)
{
	snd_soc_jack_report_no_dapm(jack, status, mask);
}

static void __hphocp_off_report(struct wcd9xxx_mbhc *mbhc, u32 jack_status,
				int irq)
{
	struct snd_soc_codec *codec;

	pr_debug("%s: clear ocp status %x\n", __func__, jack_status);
	codec = mbhc->codec;
	if (mbhc->hph_status & jack_status) {
		mbhc->hph_status &= ~jack_status;
		wcd9xxx_jack_report(&mbhc->headset_jack,
				    mbhc->hph_status, WCD9XXX_JACK_MASK);
		snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_OCP_CTL, 0x10,
				    0x00);
		snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_OCP_CTL, 0x10,
				    0x10);
		/*
		 * reset retry counter as PA is turned off signifying
		 * start of new OCP detection session
		 */
		if (WCD9XXX_IRQ_HPH_PA_OCPL_FAULT)
			mbhc->hphlocp_cnt = 0;
		else
			mbhc->hphrocp_cnt = 0;
		wcd9xxx_enable_irq(codec->control_data, irq);
	}
}

static void hphrocp_off_report(struct wcd9xxx_mbhc *mbhc, u32 jack_status)
{
	__hphocp_off_report(mbhc, SND_JACK_OC_HPHR,
			    WCD9XXX_IRQ_HPH_PA_OCPR_FAULT);
}

static void hphlocp_off_report(struct wcd9xxx_mbhc *mbhc, u32 jack_status)
{
	__hphocp_off_report(mbhc, SND_JACK_OC_HPHL,
			    WCD9XXX_IRQ_HPH_PA_OCPL_FAULT);
}

static void wcd9xxx_get_mbhc_micbias_regs(struct wcd9xxx_mbhc *mbhc,
					struct mbhc_micbias_regs *micbias_regs)
{
	unsigned int cfilt;
	struct wcd9xxx_pdata *pdata = mbhc->resmgr->pdata;

	switch (mbhc->mbhc_cfg->micbias) {
	case MBHC_MICBIAS1:
		cfilt = pdata->micbias.bias1_cfilt_sel;
		micbias_regs->mbhc_reg = WCD9XXX_A_MICB_1_MBHC;
		micbias_regs->int_rbias = WCD9XXX_A_MICB_1_INT_RBIAS;
		micbias_regs->ctl_reg = WCD9XXX_A_MICB_1_CTL;
		break;
	case MBHC_MICBIAS2:
		cfilt = pdata->micbias.bias2_cfilt_sel;
		micbias_regs->mbhc_reg = WCD9XXX_A_MICB_2_MBHC;
		micbias_regs->int_rbias = WCD9XXX_A_MICB_2_INT_RBIAS;
		micbias_regs->ctl_reg = WCD9XXX_A_MICB_2_CTL;
		break;
	case MBHC_MICBIAS3:
		cfilt = pdata->micbias.bias3_cfilt_sel;
		micbias_regs->mbhc_reg = WCD9XXX_A_MICB_3_MBHC;
		micbias_regs->int_rbias = WCD9XXX_A_MICB_3_INT_RBIAS;
		micbias_regs->ctl_reg = WCD9XXX_A_MICB_3_CTL;
		break;
	case MBHC_MICBIAS4:
		cfilt = pdata->micbias.bias4_cfilt_sel;
		micbias_regs->mbhc_reg = mbhc->resmgr->reg_addr->micb_4_mbhc;
		micbias_regs->int_rbias =
		    mbhc->resmgr->reg_addr->micb_4_int_rbias;
		micbias_regs->ctl_reg = mbhc->resmgr->reg_addr->micb_4_ctl;
		break;
	default:
		/* Should never reach here */
		pr_err("%s: Invalid MIC BIAS for MBHC\n", __func__);
		return;
	}

	micbias_regs->cfilt_sel = cfilt;

	switch (cfilt) {
	case WCD9XXX_CFILT1_SEL:
		micbias_regs->cfilt_val = WCD9XXX_A_MICB_CFILT_1_VAL;
		micbias_regs->cfilt_ctl = WCD9XXX_A_MICB_CFILT_1_CTL;
		mbhc->mbhc_data.micb_mv =
		    mbhc->resmgr->pdata->micbias.cfilt1_mv;
		break;
	case WCD9XXX_CFILT2_SEL:
		micbias_regs->cfilt_val = WCD9XXX_A_MICB_CFILT_2_VAL;
		micbias_regs->cfilt_ctl = WCD9XXX_A_MICB_CFILT_2_CTL;
		mbhc->mbhc_data.micb_mv =
		    mbhc->resmgr->pdata->micbias.cfilt2_mv;
		break;
	case WCD9XXX_CFILT3_SEL:
		micbias_regs->cfilt_val = WCD9XXX_A_MICB_CFILT_3_VAL;
		micbias_regs->cfilt_ctl = WCD9XXX_A_MICB_CFILT_3_CTL;
		mbhc->mbhc_data.micb_mv =
		    mbhc->resmgr->pdata->micbias.cfilt3_mv;
		break;
	}
}

static void wcd9xxx_clr_and_turnon_hph_padac(struct wcd9xxx_mbhc *mbhc)
{
	bool pa_turned_on = false;
	struct snd_soc_codec *codec = mbhc->codec;
	u8 wg_time;

	wg_time = snd_soc_read(codec, WCD9XXX_A_RX_HPH_CNP_WG_TIME) ;
	wg_time += 1;

	if (test_and_clear_bit(WCD9XXX_HPHR_DAC_OFF_ACK,
			       &mbhc->hph_pa_dac_state)) {
		pr_debug("%s: HPHR clear flag and enable DAC\n", __func__);
		snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_R_DAC_CTL,
				    0xC0, 0xC0);
	}
	if (test_and_clear_bit(WCD9XXX_HPHL_DAC_OFF_ACK,
				&mbhc->hph_pa_dac_state)) {
		pr_debug("%s: HPHL clear flag and enable DAC\n", __func__);
		snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_L_DAC_CTL,
				    0xC0, 0xC0);
	}

	if (test_and_clear_bit(WCD9XXX_HPHR_PA_OFF_ACK,
			       &mbhc->hph_pa_dac_state)) {
		pr_debug("%s: HPHR clear flag and enable PA\n", __func__);
		snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_CNP_EN, 0x10,
				    1 << 4);
		pa_turned_on = true;
	}
	if (test_and_clear_bit(WCD9XXX_HPHL_PA_OFF_ACK,
			       &mbhc->hph_pa_dac_state)) {
		pr_debug("%s: HPHL clear flag and enable PA\n", __func__);
		snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_CNP_EN, 0x20, 1
				    << 5);
		pa_turned_on = true;
	}

	if (pa_turned_on) {
		pr_debug("%s: PA was turned off by MBHC and not by DAPM\n",
			 __func__);
		usleep_range(wg_time * 1000, wg_time * 1000);
	}
}

static int wcd9xxx_cancel_btn_work(struct wcd9xxx_mbhc *mbhc)
{
	int r;
	r = cancel_delayed_work_sync(&mbhc->mbhc_btn_dwork);
	if (r)
		/* if scheduled mbhc.mbhc_btn_dwork is canceled from here,
		 * we have to unlock from here instead btn_work */
		wcd9xxx_unlock_sleep(mbhc->resmgr->core);
	return r;
}

static bool wcd9xxx_is_hph_dac_on(struct snd_soc_codec *codec, int left)
{
	u8 hph_reg_val = 0;
	if (left)
		hph_reg_val = snd_soc_read(codec, WCD9XXX_A_RX_HPH_L_DAC_CTL);
	else
		hph_reg_val = snd_soc_read(codec, WCD9XXX_A_RX_HPH_R_DAC_CTL);

	return (hph_reg_val & 0xC0) ? true : false;
}

static bool wcd9xxx_is_hph_pa_on(struct snd_soc_codec *codec)
{
	u8 hph_reg_val = 0;
	hph_reg_val = snd_soc_read(codec, WCD9XXX_A_RX_HPH_CNP_EN);

	return (hph_reg_val & 0x30) ? true : false;
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_set_and_turnoff_hph_padac(struct wcd9xxx_mbhc *mbhc)
{
	u8 wg_time;
	struct snd_soc_codec *codec = mbhc->codec;

	wg_time = snd_soc_read(codec, WCD9XXX_A_RX_HPH_CNP_WG_TIME);
	wg_time += 1;

	/* If headphone PA is on, check if userspace receives
	 * removal event to sync-up PA's state */
	if (wcd9xxx_is_hph_pa_on(codec)) {
		pr_debug("%s PA is on, setting PA_OFF_ACK\n", __func__);
		set_bit(WCD9XXX_HPHL_PA_OFF_ACK, &mbhc->hph_pa_dac_state);
		set_bit(WCD9XXX_HPHR_PA_OFF_ACK, &mbhc->hph_pa_dac_state);
	} else {
		pr_debug("%s PA is off\n", __func__);
	}

	if (wcd9xxx_is_hph_dac_on(codec, 1))
		set_bit(WCD9XXX_HPHL_DAC_OFF_ACK, &mbhc->hph_pa_dac_state);
	if (wcd9xxx_is_hph_dac_on(codec, 0))
		set_bit(WCD9XXX_HPHR_DAC_OFF_ACK, &mbhc->hph_pa_dac_state);

	snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_CNP_EN, 0x30, 0x00);
	snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_L_DAC_CTL, 0xC0, 0x00);
	snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_R_DAC_CTL, 0xC0, 0x00);
	usleep_range(wg_time * 1000, wg_time * 1000);
}

static void wcd9xxx_insert_detect_setup(struct wcd9xxx_mbhc *mbhc, bool ins)
{
	if (!mbhc->mbhc_cfg->insert_detect)
		return;
	pr_debug("%s: Setting up %s detection\n", __func__,
		 ins ? "insert" : "removal");
	/* Disable detection to avoid glitch */
	snd_soc_update_bits(mbhc->codec, WCD9XXX_A_MBHC_INSERT_DETECT, 1, 0);
	snd_soc_write(mbhc->codec, WCD9XXX_A_MBHC_INSERT_DETECT,
		      (0x68 | (ins ? (1 << 1) : 0)));
	/* Re-enable detection */
	snd_soc_update_bits(mbhc->codec, WCD9XXX_A_MBHC_INSERT_DETECT, 1, 1);
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_report_plug(struct wcd9xxx_mbhc *mbhc, int insertion,
				enum snd_jack_types jack_type)
{
	WCD9XXX_BCL_ASSERT_LOCKED(mbhc->resmgr);

	pr_debug("%s: enter insertion %d hph_status %x\n",
		 __func__, insertion, mbhc->hph_status);
	if (!insertion) {
		/* Report removal */
		mbhc->hph_status &= ~jack_type;
		/*
		 * cancel possibly scheduled btn work and
		 * report release if we reported button press
		 */
		if (wcd9xxx_cancel_btn_work(mbhc))
			pr_debug("%s: button press is canceled\n", __func__);
		else if (mbhc->buttons_pressed) {
			pr_debug("%s: release of button press%d\n",
				 __func__, jack_type);
			wcd9xxx_jack_report(&mbhc->button_jack, 0,
					    mbhc->buttons_pressed);
			mbhc->buttons_pressed &=
				~WCD9XXX_JACK_BUTTON_MASK;
		}
		pr_debug("%s: Reporting removal %d(%x)\n", __func__,
			 jack_type, mbhc->hph_status);
		wcd9xxx_jack_report(&mbhc->headset_jack, mbhc->hph_status,
				    WCD9XXX_JACK_MASK);
		wcd9xxx_set_and_turnoff_hph_padac(mbhc);
		hphrocp_off_report(mbhc, SND_JACK_OC_HPHR);
		hphlocp_off_report(mbhc, SND_JACK_OC_HPHL);
		mbhc->current_plug = PLUG_TYPE_NONE;
		mbhc->polling_active = false;
	} else {
		if (mbhc->mbhc_cfg->detect_extn_cable) {
			/* Report removal of current jack type */
			if (mbhc->hph_status != jack_type) {
				pr_debug("%s: Reporting removal (%x)\n",
					 __func__, mbhc->hph_status);
				wcd9xxx_jack_report(&mbhc->headset_jack,
						    0, WCD9XXX_JACK_MASK);
				mbhc->hph_status = 0;
			}
		}
		/* Report insertion */
		mbhc->hph_status |= jack_type;

		if (jack_type == SND_JACK_HEADPHONE) {
			mbhc->current_plug = PLUG_TYPE_HEADPHONE;
		} else if (jack_type == SND_JACK_UNSUPPORTED) {
			mbhc->current_plug = PLUG_TYPE_GND_MIC_SWAP;
		} else if (jack_type == SND_JACK_HEADSET) {
			mbhc->polling_active = BUTTON_POLLING_SUPPORTED;
			mbhc->current_plug = PLUG_TYPE_HEADSET;
		} else if (jack_type == SND_JACK_LINEOUT) {
			mbhc->current_plug = PLUG_TYPE_HIGH_HPH;
		}
		pr_debug("%s: Reporting insertion %d(%x)\n", __func__,
			 jack_type, mbhc->hph_status);
		wcd9xxx_jack_report(&mbhc->headset_jack,
				    mbhc->hph_status, WCD9XXX_JACK_MASK);
		wcd9xxx_clr_and_turnon_hph_padac(mbhc);
	}
	/* Setup insert detect */
	wcd9xxx_insert_detect_setup(mbhc, !insertion);

	pr_debug("%s: leave hph_status %x\n", __func__, mbhc->hph_status);
}

/* should be called under interrupt context that hold suspend */
static void wcd9xxx_schedule_hs_detect_plug(struct wcd9xxx_mbhc *mbhc,
					    struct work_struct *work)
{
	pr_debug("%s: scheduling wcd9xxx_correct_swch_plug\n", __func__);
	WCD9XXX_BCL_ASSERT_LOCKED(mbhc->resmgr);
	mbhc->hs_detect_work_stop = false;
	wcd9xxx_lock_sleep(mbhc->resmgr->core);
	schedule_work(work);
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_cancel_hs_detect_plug(struct wcd9xxx_mbhc *mbhc,
					 struct work_struct *work)
{
	pr_debug("%s: Canceling correct_plug_swch\n", __func__);
	WCD9XXX_BCL_ASSERT_LOCKED(mbhc->resmgr);
	mbhc->hs_detect_work_stop = true;
	wmb();
	WCD9XXX_BCL_UNLOCK(mbhc->resmgr);
	if (cancel_work_sync(work)) {
		pr_debug("%s: correct_plug_swch is canceled\n",
			 __func__);
		wcd9xxx_unlock_sleep(mbhc->resmgr->core);
	}
	WCD9XXX_BCL_LOCK(mbhc->resmgr);
}

static s16 wcd9xxx_get_current_v_hs_max(struct wcd9xxx_mbhc *mbhc)
{
	s16 v_hs_max;
	struct wcd9xxx_mbhc_plug_type_cfg *plug_type;

	plug_type = WCD9XXX_MBHC_CAL_PLUG_TYPE_PTR(mbhc->mbhc_cfg->calibration);
	if ((mbhc->mbhc_data.micb_mv != VDDIO_MICBIAS_MV) &&
	    mbhc->mbhc_micbias_switched)
		v_hs_max = mbhc->mbhc_data.adj_v_hs_max;
	else
		v_hs_max = plug_type->v_hs_max;
	return v_hs_max;
}

static bool wcd9xxx_is_inval_ins_range(struct wcd9xxx_mbhc *mbhc,
				     s32 mic_volt, bool highhph, bool *highv)
{
	s16 v_hs_max;
	bool invalid = false;

	/* Perform this check only when the high voltage headphone
	 * needs to be considered as invalid
	 */
	v_hs_max = wcd9xxx_get_current_v_hs_max(mbhc);
	*highv = mic_volt > v_hs_max;
	if (!highhph && *highv)
		invalid = true;
	else if (mic_volt < mbhc->mbhc_data.v_inval_ins_high &&
		 (mic_volt > mbhc->mbhc_data.v_inval_ins_low))
		invalid = true;

	return invalid;
}

static short wcd9xxx_read_sta_result(struct snd_soc_codec *codec)
{
	u8 bias_msb, bias_lsb;
	short bias_value;

	bias_msb = snd_soc_read(codec, WCD9XXX_A_CDC_MBHC_B3_STATUS);
	bias_lsb = snd_soc_read(codec, WCD9XXX_A_CDC_MBHC_B2_STATUS);
	bias_value = (bias_msb << 8) | bias_lsb;
	return bias_value;
}

static short wcd9xxx_read_dce_result(struct snd_soc_codec *codec)
{
	u8 bias_msb, bias_lsb;
	short bias_value;

	bias_msb = snd_soc_read(codec, WCD9XXX_A_CDC_MBHC_B5_STATUS);
	bias_lsb = snd_soc_read(codec, WCD9XXX_A_CDC_MBHC_B4_STATUS);
	bias_value = (bias_msb << 8) | bias_lsb;
	return bias_value;
}

static void wcd9xxx_turn_onoff_rel_detection(struct snd_soc_codec *codec,
					     bool on)
{
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x02, on << 1);
}

static short __wcd9xxx_codec_sta_dce(struct wcd9xxx_mbhc *mbhc, int dce,
				     bool override_bypass, bool noreldetection)
{
	short bias_value;
	struct snd_soc_codec *codec = mbhc->codec;

	wcd9xxx_disable_irq(mbhc->resmgr->core, WCD9XXX_IRQ_MBHC_POTENTIAL);
	if (noreldetection)
		wcd9xxx_turn_onoff_rel_detection(codec, false);

	/* Turn on the override */
	if (!override_bypass)
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x4, 0x4);
	if (dce) {
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8,
				    0x8);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x4);
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8,
				    0x0);
		usleep_range(mbhc->mbhc_data.t_sta_dce,
			     mbhc->mbhc_data.t_sta_dce);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x4);
		usleep_range(mbhc->mbhc_data.t_dce, mbhc->mbhc_data.t_dce);
		bias_value = wcd9xxx_read_dce_result(codec);
	} else {
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8,
				    0x8);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x2);
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8,
				    0x0);
		usleep_range(mbhc->mbhc_data.t_sta_dce,
			     mbhc->mbhc_data.t_sta_dce);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x2);
		usleep_range(mbhc->mbhc_data.t_sta,
			     mbhc->mbhc_data.t_sta);
		bias_value = wcd9xxx_read_sta_result(codec);
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8,
				    0x8);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x0);
	}
	/* Turn off the override after measuring mic voltage */
	if (!override_bypass)
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x04,
				    0x00);

	if (noreldetection)
		wcd9xxx_turn_onoff_rel_detection(codec, true);
	wcd9xxx_enable_irq(mbhc->resmgr->core, WCD9XXX_IRQ_MBHC_POTENTIAL);

	return bias_value;
}

static short wcd9xxx_codec_sta_dce(struct wcd9xxx_mbhc *mbhc, int dce,
				   bool norel)
{
	return __wcd9xxx_codec_sta_dce(mbhc, dce, false, norel);
}

static s32 wcd9xxx_codec_sta_dce_v(struct wcd9xxx_mbhc *mbhc, s8 dce,
				   u16 bias_value)
{
	s16 value, z, mb;
	s32 mv;

	value = bias_value;
	if (dce) {
		z = (mbhc->mbhc_data.dce_z);
		mb = (mbhc->mbhc_data.dce_mb);
		mv = (value - z) * (s32)mbhc->mbhc_data.micb_mv / (mb - z);
	} else {
		z = (mbhc->mbhc_data.sta_z);
		mb = (mbhc->mbhc_data.sta_mb);
		mv = (value - z) * (s32)mbhc->mbhc_data.micb_mv / (mb - z);
	}

	return mv;
}

/* called only from interrupt which is under codec_resource_lock acquisition */
static short wcd9xxx_mbhc_setup_hs_polling(struct wcd9xxx_mbhc *mbhc)
{
	struct snd_soc_codec *codec = mbhc->codec;
	short bias_value;
	u8 cfilt_mode;

	WCD9XXX_BCL_ASSERT_LOCKED(mbhc->resmgr);

	pr_debug("%s: enter\n", __func__);
	if (!mbhc->mbhc_cfg->calibration) {
		pr_err("%s: Error, no calibration exists\n", __func__);
		return -ENODEV;
	}

	/*
	 * Request BG and clock.
	 * These will be released by wcd9xxx_cleanup_hs_polling
	 */
	wcd9xxx_resmgr_get_bandgap(mbhc->resmgr, WCD9XXX_BANDGAP_MBHC_MODE);
	wcd9xxx_resmgr_get_clk_block(mbhc->resmgr, WCD9XXX_CLK_RCO);

	snd_soc_update_bits(codec, WCD9XXX_A_CLK_BUFF_EN1, 0x05, 0x01);

	/* Make sure CFILT is in fast mode, save current mode */
	cfilt_mode = snd_soc_read(codec, mbhc->mbhc_bias_regs.cfilt_ctl);
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.cfilt_ctl, 0x70, 0x00);

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x2, 0x2);
	snd_soc_write(codec, WCD9XXX_A_MBHC_SCALING_MUX_1, 0x84);

	snd_soc_update_bits(codec, WCD9XXX_A_TX_7_MBHC_EN, 0x80, 0x80);
	snd_soc_update_bits(codec, WCD9XXX_A_TX_7_MBHC_EN, 0x1F, 0x1C);
	snd_soc_update_bits(codec, WCD9XXX_A_TX_7_MBHC_TEST_CTL, 0x40, 0x40);

	snd_soc_update_bits(codec, WCD9XXX_A_TX_7_MBHC_EN, 0x80, 0x00);
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8, 0x8);
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8, 0x00);

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x2, 0x2);
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x8, 0x8);

	wcd9xxx_calibrate_hs_polling(mbhc);

	/* don't flip override */
	bias_value = __wcd9xxx_codec_sta_dce(mbhc, 1, true, true);
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.cfilt_ctl, 0x40,
			    cfilt_mode);
	snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x13, 0x00);

	return bias_value;
}

static void wcd9xxx_shutdown_hs_removal_detect(struct wcd9xxx_mbhc *mbhc)
{
	struct snd_soc_codec *codec = mbhc->codec;
	const struct wcd9xxx_mbhc_general_cfg *generic =
	    WCD9XXX_MBHC_CAL_GENERAL_PTR(mbhc->mbhc_cfg->calibration);

	/* Need MBHC clock */
	wcd9xxx_resmgr_get_clk_block(mbhc->resmgr, WCD9XXX_CLK_RCO);

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x2, 0x2);
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x6, 0x0);
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg, 0x80, 0x00);

	usleep_range(generic->t_shutdown_plug_rem,
		     generic->t_shutdown_plug_rem);

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0xA, 0x8);

	/* Put requested CLK back */
	wcd9xxx_resmgr_put_clk_block(mbhc->resmgr, WCD9XXX_CLK_RCO);

	snd_soc_write(codec, WCD9XXX_A_MBHC_SCALING_MUX_1, 0x00);
}

static void wcd9xxx_cleanup_hs_polling(struct wcd9xxx_mbhc *mbhc)
{
	WCD9XXX_BCL_ASSERT_LOCKED(mbhc->resmgr);

	wcd9xxx_shutdown_hs_removal_detect(mbhc);

	/* Release clock and BG requested by wcd9xxx_mbhc_setup_hs_polling */
	wcd9xxx_resmgr_put_clk_block(mbhc->resmgr, WCD9XXX_CLK_RCO);
	wcd9xxx_resmgr_put_bandgap(mbhc->resmgr, WCD9XXX_BANDGAP_MBHC_MODE);

	mbhc->polling_active = false;
	mbhc->mbhc_state = MBHC_STATE_NONE;
}

static s16 scale_v_micb_vddio(struct wcd9xxx_mbhc *mbhc, int v, bool tovddio)
{
	int r;
	int vddio_k, mb_k;
	vddio_k = wcd9xxx_resmgr_get_k_val(mbhc->resmgr, VDDIO_MICBIAS_MV);
	mb_k = wcd9xxx_resmgr_get_k_val(mbhc->resmgr, mbhc->mbhc_data.micb_mv);
	if (tovddio)
		r = v * vddio_k / mb_k;
	else
		r = v * mb_k / vddio_k;
	return r;
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_codec_hphr_gnd_switch(struct snd_soc_codec *codec, bool on)
{
	snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x01, on);
	if (on)
		usleep_range(5000, 5000);
}

static bool wcd9xxx_is_inval_ins_delta(struct snd_soc_codec *codec,
				       int mic_volt, int mic_volt_prev,
				       int threshold)
{
	return abs(mic_volt - mic_volt_prev) > threshold;
}

static void wcd9xxx_onoff_vddio_switch(struct wcd9xxx_mbhc *mbhc, bool on)
{
	if (on) {
		snd_soc_update_bits(mbhc->codec, mbhc->mbhc_bias_regs.mbhc_reg,
				    1 << 7, 1 << 7);
		snd_soc_update_bits(mbhc->codec, WCD9XXX_A_MAD_ANA_CTRL,
				    1 << 4, 0);
	} else {
		snd_soc_update_bits(mbhc->codec, WCD9XXX_A_MAD_ANA_CTRL,
				    1 << 4, 1 << 4);
		snd_soc_update_bits(mbhc->codec, mbhc->mbhc_bias_regs.mbhc_reg,
				    1 << 7, 0);
	}
	if (on)
		usleep_range(10000, 10000);
}

/* called under codec_resource_lock acquisition and mbhc override = 1 */
static enum wcd9xxx_mbhc_plug_type
wcd9xxx_codec_get_plug_type(struct wcd9xxx_mbhc *mbhc, bool highhph)
{
	int i;
	bool gndswitch, vddioswitch;
	int scaled;
	struct wcd9xxx_mbhc_plug_type_cfg *plug_type_ptr;
	struct snd_soc_codec *codec = mbhc->codec;
	const bool vddio = (mbhc->mbhc_data.micb_mv != VDDIO_MICBIAS_MV);
	int num_det = (NUM_DCE_PLUG_DETECT + vddio);
	enum wcd9xxx_mbhc_plug_type plug_type[num_det];
	s16 mb_v[num_det];
	s32 mic_mv[num_det];
	bool inval;
	bool highdelta;
	bool ahighv = false, highv;

	pr_debug("%s: enter\n", __func__);
	WCD9XXX_BCL_ASSERT_LOCKED(mbhc->resmgr);

	/* make sure override is on */
	WARN_ON(!(snd_soc_read(codec, WCD9XXX_A_CDC_MBHC_B1_CTL) & 0x04));

	/* GND and MIC swap detection requires at least 2 rounds of DCE */
	BUG_ON(num_det < 2);

	plug_type_ptr =
		WCD9XXX_MBHC_CAL_PLUG_TYPE_PTR(mbhc->mbhc_cfg->calibration);

	plug_type[0] = PLUG_TYPE_INVALID;

	/* performs DCEs for N times
	 * 1st: check if voltage is in invalid range
	 * 2nd - N-2nd: check voltage range and delta
	 * N-1st: check voltage range, delta with HPHR GND switch
	 * Nth: check voltage range with VDDIO switch if micbias V != vddio V*/
	for (i = 0; i < num_det; i++) {
		gndswitch = (i == (num_det - 1 - vddio));
		vddioswitch = (vddio && ((i == num_det - 1) ||
					(i == num_det - 2)));
		if (i == 0) {
			mb_v[i] = wcd9xxx_mbhc_setup_hs_polling(mbhc);
			mic_mv[i] = wcd9xxx_codec_sta_dce_v(mbhc, 1 , mb_v[i]);
			inval = wcd9xxx_is_inval_ins_range(mbhc, mic_mv[i],
					highhph, &highv);
			ahighv |= highv;
			scaled = mic_mv[i];
		} else {
			if (vddioswitch)
				wcd9xxx_onoff_vddio_switch(mbhc, true);
			if (gndswitch)
				wcd9xxx_codec_hphr_gnd_switch(codec, true);
			mb_v[i] = __wcd9xxx_codec_sta_dce(mbhc, 1, true, true);
			mic_mv[i] = wcd9xxx_codec_sta_dce_v(mbhc, 1 , mb_v[i]);
			if (vddioswitch)
				scaled = scale_v_micb_vddio(mbhc, mic_mv[i],
							    false);
			else
				scaled = mic_mv[i];
			/* !gndswitch & vddioswitch means the previous DCE
			 * was done with gndswitch, don't compare with DCE
			 * with gndswitch */
			highdelta = wcd9xxx_is_inval_ins_delta(codec, scaled,
					mic_mv[i - !gndswitch - vddioswitch],
					FAKE_INS_DELTA_SCALED_MV);
			inval = (wcd9xxx_is_inval_ins_range(mbhc, mic_mv[i],
						highhph, &highv) ||
					highdelta);
			ahighv |= highv;
			if (gndswitch)
				wcd9xxx_codec_hphr_gnd_switch(codec, false);
			if (vddioswitch)
				wcd9xxx_onoff_vddio_switch(mbhc, false);
			/* claim UNSUPPORTED plug insertion when
			 * good headset is detected but HPHR GND switch makes
			 * delta difference */
			if (i == (num_det - 2) && highdelta && !ahighv)
				plug_type[0] = PLUG_TYPE_GND_MIC_SWAP;
			else if (i == (num_det - 1) && inval)
				plug_type[0] = PLUG_TYPE_INVALID;
		}
		pr_debug("%s: DCE #%d, %04x, V %d, scaled V %d, GND %d, VDDIO %d, inval %d\n",
			 __func__, i + 1, mb_v[i] & 0xffff, mic_mv[i], scaled,
			 gndswitch, vddioswitch, inval);
		/* don't need to run further DCEs */
		if (ahighv && inval)
			break;
		mic_mv[i] = scaled;
	}

	for (i = 0; (plug_type[0] != PLUG_TYPE_GND_MIC_SWAP && !inval) &&
		    (i < num_det); i++) {
		/*
		 * If we are here, means none of the all
		 * measurements are fake, continue plug type detection.
		 * If all three measurements do not produce same
		 * plug type, restart insertion detection
		 */
		if (mic_mv[i] < plug_type_ptr->v_no_mic) {
			plug_type[i] = PLUG_TYPE_HEADPHONE;
			pr_debug("%s: Detect attempt %d, detected Headphone\n",
				 __func__, i);
		} else if (highhph && (mic_mv[i] > plug_type_ptr->v_hs_max)) {
			plug_type[i] = PLUG_TYPE_HIGH_HPH;
			pr_debug("%s: Detect attempt %d, detected High Headphone\n",
				 __func__, i);
		} else {
			plug_type[i] = PLUG_TYPE_HEADSET;
			pr_debug("%s: Detect attempt %d, detected Headset\n",
					__func__, i);
		}

		if (i > 0 && (plug_type[i - 1] != plug_type[i])) {
			pr_err("%s: Detect attempt %d and %d are not same",
			       __func__, i - 1, i);
			plug_type[0] = PLUG_TYPE_INVALID;
			inval = true;
			break;
		}
	}

	pr_debug("%s: Detected plug type %d\n", __func__, plug_type[0]);
	pr_debug("%s: leave\n", __func__);
	return plug_type[0];
}

static bool wcd9xxx_swch_level_remove(struct wcd9xxx_mbhc *mbhc)
{
	if (mbhc->mbhc_cfg->gpio)
		return (gpio_get_value_cansleep(mbhc->mbhc_cfg->gpio) !=
			mbhc->mbhc_cfg->gpio_level_insert);
	else if (mbhc->mbhc_cfg->insert_detect)
		return snd_soc_read(mbhc->codec,
				    WCD9XXX_A_MBHC_INSERT_DET_STATUS) &
				    (1 << 2);
	else
		WARN(1, "Invalid jack detection configuration\n");

	return true;
}

static bool is_clk_active(struct snd_soc_codec *codec)
{
	return !!(snd_soc_read(codec, WCD9XXX_A_CDC_CLK_MCLK_CTL) & 0x05);
}

static int wcd9xxx_enable_hs_detect(struct wcd9xxx_mbhc *mbhc,
				    int insertion, int trigger, bool padac_off)
{
	struct snd_soc_codec *codec = mbhc->codec;
	int central_bias_enabled = 0;
	const struct wcd9xxx_mbhc_general_cfg *generic =
	    WCD9XXX_MBHC_CAL_GENERAL_PTR(mbhc->mbhc_cfg->calibration);
	const struct wcd9xxx_mbhc_plug_detect_cfg *plug_det =
	    WCD9XXX_MBHC_CAL_PLUG_DET_PTR(mbhc->mbhc_cfg->calibration);

	pr_debug("%s: enter insertion(%d) trigger(0x%x)\n",
		 __func__, insertion, trigger);

	if (!mbhc->mbhc_cfg->calibration) {
		pr_err("Error, no wcd9xxx calibration\n");
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_INT_CTL, 0x1, 0);

	/*
	 * Make sure mic bias and Mic line schmitt trigger
	 * are turned OFF
	 */
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.ctl_reg, 0x01, 0x01);
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg, 0x90, 0x00);

	if (insertion) {
		wcd9xxx_switch_micbias(mbhc, 0);

		/* DAPM can manipulate PA/DAC bits concurrently */
		if (padac_off == true)
			wcd9xxx_set_and_turnoff_hph_padac(mbhc);

		if (trigger & MBHC_USE_HPHL_TRIGGER) {
			/* Enable HPH Schmitt Trigger */
			snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x11,
					0x11);
			snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x0C,
					plug_det->hph_current << 2);
			snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x02,
					0x02);
		}
		if (trigger & MBHC_USE_MB_TRIGGER) {
			/* enable the mic line schmitt trigger */
			snd_soc_update_bits(codec,
					mbhc->mbhc_bias_regs.mbhc_reg,
					0x60, plug_det->mic_current << 5);
			snd_soc_update_bits(codec,
					mbhc->mbhc_bias_regs.mbhc_reg,
					0x80, 0x80);
			usleep_range(plug_det->t_mic_pid, plug_det->t_mic_pid);
			snd_soc_update_bits(codec,
					mbhc->mbhc_bias_regs.ctl_reg, 0x01,
					0x00);
			snd_soc_update_bits(codec,
					mbhc->mbhc_bias_regs.mbhc_reg,
					0x10, 0x10);
		}

		/* setup for insetion detection */
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_INT_CTL, 0x2, 0);
	} else {
		pr_debug("setup for removal detection\n");
		/* Make sure the HPH schmitt trigger is OFF */
		snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x12, 0x00);

		/* enable the mic line schmitt trigger */
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.ctl_reg,
				    0x01, 0x00);
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg, 0x60,
				    plug_det->mic_current << 5);
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg,
				    0x80, 0x80);
		usleep_range(plug_det->t_mic_pid, plug_det->t_mic_pid);
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg,
				    0x10, 0x10);

		/* Setup for low power removal detection */
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_INT_CTL, 0x2,
				    0x2);
	}

	if (snd_soc_read(codec, WCD9XXX_A_CDC_MBHC_B1_CTL) & 0x4) {
		/* called by interrupt */
		if (!is_clk_active(codec)) {
			wcd9xxx_resmgr_enable_config_mode(codec, 1);
			snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL,
					0x06, 0);
			usleep_range(generic->t_shutdown_plug_rem,
					generic->t_shutdown_plug_rem);
			wcd9xxx_resmgr_enable_config_mode(codec, 0);
		} else
			snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL,
					0x06, 0);
	}

	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.int_rbias, 0x80, 0);

	/* If central bandgap disabled */
	if (!(snd_soc_read(codec, WCD9XXX_A_PIN_CTL_OE1) & 1)) {
		snd_soc_update_bits(codec, WCD9XXX_A_PIN_CTL_OE1, 0x3, 0x3);
		usleep_range(generic->t_bg_fast_settle,
			     generic->t_bg_fast_settle);
		central_bias_enabled = 1;
	}

	/* If LDO_H disabled */
	if (snd_soc_read(codec, WCD9XXX_A_PIN_CTL_OE0) & 0x80) {
		snd_soc_update_bits(codec, WCD9XXX_A_PIN_CTL_OE0, 0x10, 0);
		snd_soc_update_bits(codec, WCD9XXX_A_PIN_CTL_OE0, 0x80, 0x80);
		usleep_range(generic->t_ldoh, generic->t_ldoh);
		snd_soc_update_bits(codec, WCD9XXX_A_PIN_CTL_OE0, 0x80, 0);

		if (central_bias_enabled)
			snd_soc_update_bits(codec, WCD9XXX_A_PIN_CTL_OE1, 0x1,
					    0);
	}

	snd_soc_update_bits(codec, mbhc->resmgr->reg_addr->micb_4_mbhc, 0x3,
			    mbhc->mbhc_cfg->micbias);

	wcd9xxx_enable_irq(mbhc->resmgr->core, WCD9XXX_IRQ_MBHC_INSERTION);
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_INT_CTL, 0x1, 0x1);
	pr_debug("%s: leave\n", __func__);

	return 0;
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_find_plug_and_report(struct wcd9xxx_mbhc *mbhc,
					 enum wcd9xxx_mbhc_plug_type plug_type)
{
	pr_debug("%s: enter current_plug(%d) new_plug(%d)\n",
		 __func__, mbhc->current_plug, plug_type);

	WCD9XXX_BCL_ASSERT_LOCKED(mbhc->resmgr);

	if (plug_type == PLUG_TYPE_HEADPHONE &&
	    mbhc->current_plug == PLUG_TYPE_NONE) {
		/*
		 * Nothing was reported previously
		 * report a headphone or unsupported
		 */
		wcd9xxx_report_plug(mbhc, 1, SND_JACK_HEADPHONE);
		wcd9xxx_cleanup_hs_polling(mbhc);
	} else if (plug_type == PLUG_TYPE_GND_MIC_SWAP) {
		if (!mbhc->mbhc_cfg->detect_extn_cable) {
			if (mbhc->current_plug == PLUG_TYPE_HEADSET)
				wcd9xxx_report_plug(mbhc, 0,
							 SND_JACK_HEADSET);
			else if (mbhc->current_plug == PLUG_TYPE_HEADPHONE)
				wcd9xxx_report_plug(mbhc, 0,
							 SND_JACK_HEADPHONE);
		}
		wcd9xxx_report_plug(mbhc, 1, SND_JACK_UNSUPPORTED);
		wcd9xxx_cleanup_hs_polling(mbhc);
	} else if (plug_type == PLUG_TYPE_HEADSET) {
		/*
		 * If Headphone was reported previously, this will
		 * only report the mic line
		 */
		wcd9xxx_report_plug(mbhc, 1, SND_JACK_HEADSET);
		msleep(100);
		wcd9xxx_start_hs_polling(mbhc);
	} else if (plug_type == PLUG_TYPE_HIGH_HPH) {
		if (mbhc->mbhc_cfg->detect_extn_cable) {
			/* High impedance device found. Report as LINEOUT*/
			wcd9xxx_report_plug(mbhc, 1, SND_JACK_LINEOUT);
			wcd9xxx_cleanup_hs_polling(mbhc);
			pr_debug("%s: setup mic trigger for further detection\n",
				 __func__);
			mbhc->lpi_enabled = true;
			/*
			 * Do not enable HPHL trigger. If playback is active,
			 * it might lead to continuous false HPHL triggers
			 */
			wcd9xxx_enable_hs_detect(mbhc, 1, MBHC_USE_MB_TRIGGER,
						 false);
		} else {
			if (mbhc->current_plug == PLUG_TYPE_NONE)
				wcd9xxx_report_plug(mbhc, 1,
							 SND_JACK_HEADPHONE);
			wcd9xxx_cleanup_hs_polling(mbhc);
			pr_debug("setup mic trigger for further detection\n");
			mbhc->lpi_enabled = true;
			wcd9xxx_enable_hs_detect(mbhc, 1, MBHC_USE_MB_TRIGGER |
							  MBHC_USE_HPHL_TRIGGER,
						 false);
		}
	} else {
		WARN(1, "Unexpected current plug_type %d, plug_type %d\n",
		     mbhc->current_plug, plug_type);
	}
	pr_debug("%s: leave\n", __func__);
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_mbhc_decide_swch_plug(struct wcd9xxx_mbhc *mbhc)
{
	enum wcd9xxx_mbhc_plug_type plug_type;
	struct snd_soc_codec *codec = mbhc->codec;

	pr_debug("%s: enter\n", __func__);

	WCD9XXX_BCL_ASSERT_LOCKED(mbhc->resmgr);

	wcd9xxx_turn_onoff_override(codec, true);
	plug_type = wcd9xxx_codec_get_plug_type(mbhc, true);
	wcd9xxx_turn_onoff_override(codec, false);

	if (wcd9xxx_swch_level_remove(mbhc)) {
		pr_debug("%s: Switch level is low when determining plug\n",
			 __func__);
		return;
	}

	if (plug_type == PLUG_TYPE_INVALID ||
	    plug_type == PLUG_TYPE_GND_MIC_SWAP) {
		wcd9xxx_schedule_hs_detect_plug(mbhc,
						&mbhc->correct_plug_swch);
	} else if (plug_type == PLUG_TYPE_HEADPHONE) {
		wcd9xxx_report_plug(mbhc, 1, SND_JACK_HEADPHONE);
		wcd9xxx_schedule_hs_detect_plug(mbhc,
						&mbhc->correct_plug_swch);
	} else {
		pr_debug("%s: Valid plug found, determine plug type %d\n",
			 __func__, plug_type);
		wcd9xxx_find_plug_and_report(mbhc, plug_type);
	}
	pr_debug("%s: leave\n", __func__);
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_mbhc_detect_plug_type(struct wcd9xxx_mbhc *mbhc)
{
	struct snd_soc_codec *codec = mbhc->codec;
	const struct wcd9xxx_mbhc_plug_detect_cfg *plug_det =
		WCD9XXX_MBHC_CAL_PLUG_DET_PTR(mbhc->mbhc_cfg->calibration);

	pr_debug("%s: enter\n", __func__);
	WCD9XXX_BCL_ASSERT_LOCKED(mbhc->resmgr);

	/*
	 * Turn on the override,
	 * wcd9xxx_mbhc_setup_hs_polling requires override on
	 */
	wcd9xxx_turn_onoff_override(codec, true);
	if (plug_det->t_ins_complete > 20)
		msleep(plug_det->t_ins_complete);
	else
		usleep_range(plug_det->t_ins_complete * 1000,
			     plug_det->t_ins_complete * 1000);
	/* Turn off the override */
	wcd9xxx_turn_onoff_override(codec, false);

	if (wcd9xxx_swch_level_remove(mbhc))
		pr_debug("%s: Switch level low when determining plug\n",
			 __func__);
	else
		wcd9xxx_mbhc_decide_swch_plug(mbhc);
	pr_debug("%s: leave\n", __func__);
}

/* called only from interrupt which is under codec_resource_lock acquisition */
static void wcd9xxx_hs_insert_irq_swch(struct wcd9xxx_mbhc *mbhc,
				       bool is_removal)
{
	if (!is_removal) {
		pr_debug("%s: MIC trigger insertion interrupt\n", __func__);

		rmb();
		if (mbhc->lpi_enabled)
			msleep(100);

		rmb();
		if (!mbhc->lpi_enabled) {
			pr_debug("%s: lpi is disabled\n", __func__);
		} else if (!wcd9xxx_swch_level_remove(mbhc)) {
			pr_debug("%s: Valid insertion, detect plug type\n",
				 __func__);
			wcd9xxx_mbhc_decide_swch_plug(mbhc);
		} else {
			pr_debug("%s: Invalid insertion stop plug detection\n",
				 __func__);
		}
	} else if (mbhc->mbhc_cfg->detect_extn_cable) {
		pr_debug("%s: Removal\n", __func__);
		if (!wcd9xxx_swch_level_remove(mbhc)) {
			/*
			 * Switch indicates, something is still inserted.
			 * This could be extension cable i.e. headset is
			 * removed from extension cable.
			 */
			/* cancel detect plug */
			wcd9xxx_cancel_hs_detect_plug(mbhc,
						      &mbhc->correct_plug_swch);
			wcd9xxx_mbhc_decide_swch_plug(mbhc);
		}
	} else {
		pr_err("%s: Switch IRQ used, invalid MBHC Removal\n", __func__);
	}
}

static bool is_valid_mic_voltage(struct wcd9xxx_mbhc *mbhc, s32 mic_mv)
{
	const struct wcd9xxx_mbhc_plug_type_cfg *plug_type =
	    WCD9XXX_MBHC_CAL_PLUG_TYPE_PTR(mbhc->mbhc_cfg->calibration);
	const s16 v_hs_max = wcd9xxx_get_current_v_hs_max(mbhc);

	return (!(mic_mv > 10 && mic_mv < 80) && (mic_mv > plug_type->v_no_mic)
		&& (mic_mv < v_hs_max)) ? true : false;
}

/*
 * called under codec_resource_lock acquisition
 * returns true if mic voltage range is back to normal insertion
 * returns false either if timedout or removed
 */
static bool wcd9xxx_hs_remove_settle(struct wcd9xxx_mbhc *mbhc)
{
	int i;
	bool timedout, settled = false;
	s32 mic_mv[NUM_DCE_PLUG_DETECT];
	short mb_v[NUM_DCE_PLUG_DETECT];
	unsigned long retry = 0, timeout;

	timeout = jiffies + msecs_to_jiffies(HS_DETECT_PLUG_TIME_MS);
	while (!(timedout = time_after(jiffies, timeout))) {
		retry++;
		if (wcd9xxx_swch_level_remove(mbhc)) {
			pr_debug("%s: Switch indicates removal\n", __func__);
			break;
		}

		if (retry > 1)
			msleep(250);
		else
			msleep(50);

		if (wcd9xxx_swch_level_remove(mbhc)) {
			pr_debug("%s: Switch indicates removal\n", __func__);
			break;
		}

		for (i = 0; i < NUM_DCE_PLUG_DETECT; i++) {
			mb_v[i] = wcd9xxx_codec_sta_dce(mbhc, 1,  true);
			mic_mv[i] = wcd9xxx_codec_sta_dce_v(mbhc, 1 , mb_v[i]);
			pr_debug("%s : DCE run %lu, mic_mv = %d(%x)\n",
				 __func__, retry, mic_mv[i], mb_v[i]);
		}

		if (wcd9xxx_swch_level_remove(mbhc)) {
			pr_debug("%s: Switcn indicates removal\n", __func__);
			break;
		}

		if (mbhc->current_plug == PLUG_TYPE_NONE) {
			pr_debug("%s : headset/headphone is removed\n",
				 __func__);
			break;
		}

		for (i = 0; i < NUM_DCE_PLUG_DETECT; i++)
			if (!is_valid_mic_voltage(mbhc, mic_mv[i]))
				break;

		if (i == NUM_DCE_PLUG_DETECT) {
			pr_debug("%s: MIC voltage settled\n", __func__);
			settled = true;
			msleep(200);
			break;
		}
	}

	if (timedout)
		pr_debug("%s: Microphone did not settle in %d seconds\n",
			 __func__, HS_DETECT_PLUG_TIME_MS);
	return settled;
}

/* called only from interrupt which is under codec_resource_lock acquisition */
static void wcd9xxx_hs_remove_irq_swch(struct wcd9xxx_mbhc *mbhc)
{
	pr_debug("%s: enter\n", __func__);
	if (wcd9xxx_hs_remove_settle(mbhc))
		wcd9xxx_start_hs_polling(mbhc);
	pr_debug("%s: leave\n", __func__);
}

/* called only from interrupt which is under codec_resource_lock acquisition */
static void wcd9xxx_hs_remove_irq_noswch(struct wcd9xxx_mbhc *mbhc)
{
	short bias_value;
	bool removed = true;
	struct snd_soc_codec *codec = mbhc->codec;
	const struct wcd9xxx_mbhc_general_cfg *generic =
		WCD9XXX_MBHC_CAL_GENERAL_PTR(mbhc->mbhc_cfg->calibration);
	int min_us = FAKE_REMOVAL_MIN_PERIOD_MS * 1000;

	pr_debug("%s: enter\n", __func__);
	if (mbhc->current_plug != PLUG_TYPE_HEADSET) {
		pr_debug("%s(): Headset is not inserted, ignore removal\n",
			 __func__);
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL,
				0x08, 0x08);
		return;
	}

	usleep_range(generic->t_shutdown_plug_rem,
			generic->t_shutdown_plug_rem);

	do {
		bias_value = wcd9xxx_codec_sta_dce(mbhc, 1,  true);
		pr_debug("%s: DCE %d,%d, %d us left\n", __func__, bias_value,
			 wcd9xxx_codec_sta_dce_v(mbhc, 1, bias_value), min_us);
		if (bias_value < wcd9xxx_get_current_v_ins(mbhc, false)) {
			pr_debug("%s: checking false removal\n", __func__);
			msleep(500);
			removed = !wcd9xxx_hs_remove_settle(mbhc);
			pr_debug("%s: headset %sactually removed\n", __func__,
				 removed ? "" : "not ");
			break;
		}
		min_us -= mbhc->mbhc_data.t_dce;
	} while (min_us > 0);

	if (removed) {
		if (mbhc->mbhc_cfg->detect_extn_cable) {
			if (!wcd9xxx_swch_level_remove(mbhc)) {
				/*
				 * extension cable is still plugged in
				 * report it as LINEOUT device
				 */
				wcd9xxx_report_plug(mbhc, 1, SND_JACK_LINEOUT);
				wcd9xxx_cleanup_hs_polling(mbhc);
				wcd9xxx_enable_hs_detect(mbhc, 1,
							 MBHC_USE_MB_TRIGGER,
							 false);
			}
		} else {
			/* Cancel possibly running hs_detect_work */
			wcd9xxx_cancel_hs_detect_plug(mbhc,
						    &mbhc->correct_plug_noswch);
			/*
			 * If this removal is not false, first check the micbias
			 * switch status and switch it to LDOH if it is already
			 * switched to VDDIO.
			 */
			wcd9xxx_switch_micbias(mbhc, 0);

			wcd9xxx_report_plug(mbhc, 0, SND_JACK_HEADSET);
			wcd9xxx_cleanup_hs_polling(mbhc);
			wcd9xxx_enable_hs_detect(mbhc, 1, MBHC_USE_MB_TRIGGER |
							  MBHC_USE_HPHL_TRIGGER,
						 true);
		}
	} else {
		wcd9xxx_start_hs_polling(mbhc);
	}
	pr_debug("%s: leave\n", __func__);
}

/* called only from interrupt which is under codec_resource_lock acquisition */
static void wcd9xxx_hs_insert_irq_extn(struct wcd9xxx_mbhc *mbhc,
				       bool is_mb_trigger)
{
	/* Cancel possibly running hs_detect_work */
	wcd9xxx_cancel_hs_detect_plug(mbhc, &mbhc->correct_plug_swch);

	if (is_mb_trigger) {
		pr_debug("%s: Waiting for Headphone left trigger\n", __func__);
		wcd9xxx_enable_hs_detect(mbhc, 1, MBHC_USE_HPHL_TRIGGER, false);
	} else  {
		pr_debug("%s: HPHL trigger received, detecting plug type\n",
			 __func__);
		wcd9xxx_mbhc_detect_plug_type(mbhc);
	}
}

static irqreturn_t wcd9xxx_hs_remove_irq(int irq, void *data)
{
	bool vddio;
	struct wcd9xxx_mbhc *mbhc = data;

	pr_debug("%s: enter, removal interrupt\n", __func__);
	WCD9XXX_BCL_LOCK(mbhc->resmgr);
	vddio = (mbhc->mbhc_data.micb_mv != VDDIO_MICBIAS_MV &&
		 mbhc->mbhc_micbias_switched);
	if (vddio)
		wcd9xxx_onoff_vddio_switch(mbhc, true);

	if (mbhc->mbhc_cfg->detect_extn_cable &&
	    !wcd9xxx_swch_level_remove(mbhc))
		wcd9xxx_hs_remove_irq_noswch(mbhc);
	else
		wcd9xxx_hs_remove_irq_swch(mbhc);

	/*
	 * if driver turned off vddio switch and headset is not removed,
	 * turn on the vddio switch back, if headset is removed then vddio
	 * switch is off by time now and shouldn't be turn on again from here
	 */
	if (vddio && mbhc->current_plug == PLUG_TYPE_HEADSET)
		wcd9xxx_onoff_vddio_switch(mbhc, true);
	WCD9XXX_BCL_UNLOCK(mbhc->resmgr);

	return IRQ_HANDLED;
}

static irqreturn_t wcd9xxx_hs_insert_irq(int irq, void *data)
{
	bool is_mb_trigger, is_removal;
	struct wcd9xxx_mbhc *mbhc = data;
	struct snd_soc_codec *codec = mbhc->codec;

	pr_debug("%s: enter\n", __func__);
	WCD9XXX_BCL_LOCK(mbhc->resmgr);
	wcd9xxx_disable_irq(codec->control_data, WCD9XXX_IRQ_MBHC_INSERTION);

	is_mb_trigger = !!(snd_soc_read(codec, mbhc->mbhc_bias_regs.mbhc_reg) &
			   0x10);
	is_removal = !!(snd_soc_read(codec, WCD9XXX_A_CDC_MBHC_INT_CTL) & 0x02);
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_INT_CTL, 0x03, 0x00);

	/* Turn off both HPH and MIC line schmitt triggers */
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg, 0x90, 0x00);
	snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x13, 0x00);
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.ctl_reg, 0x01, 0x00);

	if (mbhc->mbhc_cfg->detect_extn_cable &&
	    mbhc->current_plug == PLUG_TYPE_HIGH_HPH)
		wcd9xxx_hs_insert_irq_extn(mbhc, is_mb_trigger);
	else
		wcd9xxx_hs_insert_irq_swch(mbhc, is_removal);

	WCD9XXX_BCL_UNLOCK(mbhc->resmgr);
	return IRQ_HANDLED;
}

static void wcd9xxx_btn_lpress_fn(struct work_struct *work)
{
	struct delayed_work *dwork;
	short bias_value;
	int dce_mv, sta_mv;
	struct wcd9xxx_mbhc *mbhc;

	pr_debug("%s:\n", __func__);

	dwork = to_delayed_work(work);
	mbhc = container_of(dwork, struct wcd9xxx_mbhc, mbhc_btn_dwork);

	bias_value = wcd9xxx_read_sta_result(mbhc->codec);
	sta_mv = wcd9xxx_codec_sta_dce_v(mbhc, 0, bias_value);

	bias_value = wcd9xxx_read_dce_result(mbhc->codec);
	dce_mv = wcd9xxx_codec_sta_dce_v(mbhc, 1, bias_value);
	pr_debug("%s: STA: %d, DCE: %d\n", __func__, sta_mv, dce_mv);

	pr_debug("%s: Reporting long button press event\n", __func__);
	wcd9xxx_jack_report(&mbhc->button_jack, mbhc->buttons_pressed,
			    mbhc->buttons_pressed);

	pr_debug("%s: leave\n", __func__);
	wcd9xxx_unlock_sleep(mbhc->resmgr->core);
}

static void wcd9xxx_mbhc_insert_work(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wcd9xxx_mbhc *mbhc;
	struct snd_soc_codec *codec;
	struct wcd9xxx *core;

	dwork = to_delayed_work(work);
	mbhc = container_of(dwork, struct wcd9xxx_mbhc, mbhc_insert_dwork);
	codec = mbhc->codec;
	core = mbhc->resmgr->core;

	pr_debug("%s:\n", __func__);

	/* Turn off both HPH and MIC line schmitt triggers */
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.mbhc_reg, 0x90, 0x00);
	snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x13, 0x00);
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.ctl_reg, 0x01, 0x00);
	wcd9xxx_disable_irq_sync(core, WCD9XXX_IRQ_MBHC_INSERTION);
	wcd9xxx_mbhc_detect_plug_type(mbhc);
	wcd9xxx_unlock_sleep(core);
}

static bool wcd9xxx_mbhc_fw_validate(const struct firmware *fw)
{
	u32 cfg_offset;
	struct wcd9xxx_mbhc_imped_detect_cfg *imped_cfg;
	struct wcd9xxx_mbhc_btn_detect_cfg *btn_cfg;

	if (fw->size < WCD9XXX_MBHC_CAL_MIN_SIZE)
		return false;

	/*
	 * Previous check guarantees that there is enough fw data up
	 * to num_btn
	 */
	btn_cfg = WCD9XXX_MBHC_CAL_BTN_DET_PTR(fw->data);
	cfg_offset = (u32) ((void *) btn_cfg - (void *) fw->data);
	if (fw->size < (cfg_offset + WCD9XXX_MBHC_CAL_BTN_SZ(btn_cfg)))
		return false;

	/*
	 * Previous check guarantees that there is enough fw data up
	 * to start of impedance detection configuration
	 */
	imped_cfg = WCD9XXX_MBHC_CAL_IMPED_DET_PTR(fw->data);
	cfg_offset = (u32) ((void *) imped_cfg - (void *) fw->data);

	if (fw->size < (cfg_offset + WCD9XXX_MBHC_CAL_IMPED_MIN_SZ))
		return false;

	if (fw->size < (cfg_offset + WCD9XXX_MBHC_CAL_IMPED_SZ(imped_cfg)))
		return false;

	return true;
}

static u16 wcd9xxx_codec_v_sta_dce(struct wcd9xxx_mbhc *mbhc,
				   enum meas_type dce, s16 vin_mv)
{
	s16 diff, zero;
	u32 mb_mv, in;
	u16 value;

	mb_mv = mbhc->mbhc_data.micb_mv;

	if (mb_mv == 0) {
		pr_err("%s: Mic Bias voltage is set to zero\n", __func__);
		return -EINVAL;
	}

	if (dce) {
		diff = (mbhc->mbhc_data.dce_mb) - (mbhc->mbhc_data.dce_z);
		zero = (mbhc->mbhc_data.dce_z);
	} else {
		diff = (mbhc->mbhc_data.sta_mb) - (mbhc->mbhc_data.sta_z);
		zero = (mbhc->mbhc_data.sta_z);
	}
	in = (u32) diff * vin_mv;

	value = (u16) (in / mb_mv) + zero;
	return value;
}

static void wcd9xxx_mbhc_calc_thres(struct wcd9xxx_mbhc *mbhc)
{
	struct snd_soc_codec *codec;
	s16 btn_mv = 0, btn_delta_mv;
	struct wcd9xxx_mbhc_btn_detect_cfg *btn_det;
	struct wcd9xxx_mbhc_plug_type_cfg *plug_type;
	u16 *btn_high;
	int i;

	pr_debug("%s: enter\n", __func__);
	codec = mbhc->codec;
	btn_det = WCD9XXX_MBHC_CAL_BTN_DET_PTR(mbhc->mbhc_cfg->calibration);
	plug_type = WCD9XXX_MBHC_CAL_PLUG_TYPE_PTR(mbhc->mbhc_cfg->calibration);

	mbhc->mbhc_data.v_ins_hu =
	    wcd9xxx_codec_v_sta_dce(mbhc, STA, plug_type->v_hs_max);
	mbhc->mbhc_data.v_ins_h =
	    wcd9xxx_codec_v_sta_dce(mbhc, DCE, plug_type->v_hs_max);

	mbhc->mbhc_data.v_inval_ins_low = FAKE_INS_LOW;
	mbhc->mbhc_data.v_inval_ins_high = FAKE_INS_HIGH;

	if (mbhc->mbhc_data.micb_mv != VDDIO_MICBIAS_MV) {
		mbhc->mbhc_data.adj_v_hs_max =
		    scale_v_micb_vddio(mbhc, plug_type->v_hs_max, true);
		mbhc->mbhc_data.adj_v_ins_hu =
		    wcd9xxx_codec_v_sta_dce(mbhc, STA,
					    mbhc->mbhc_data.adj_v_hs_max);
		mbhc->mbhc_data.adj_v_ins_h =
		    wcd9xxx_codec_v_sta_dce(mbhc, DCE,
					    mbhc->mbhc_data.adj_v_hs_max);
		mbhc->mbhc_data.v_inval_ins_low =
		    scale_v_micb_vddio(mbhc, mbhc->mbhc_data.v_inval_ins_low,
				       false);
		mbhc->mbhc_data.v_inval_ins_high =
		    scale_v_micb_vddio(mbhc, mbhc->mbhc_data.v_inval_ins_high,
				       false);
	}

	btn_high = wcd9xxx_mbhc_cal_btn_det_mp(btn_det,
					       MBHC_BTN_DET_V_BTN_HIGH);
	for (i = 0; i < btn_det->num_btn; i++)
		btn_mv = btn_high[i] > btn_mv ? btn_high[i] : btn_mv;

	mbhc->mbhc_data.v_b1_h = wcd9xxx_codec_v_sta_dce(mbhc, DCE, btn_mv);
	btn_delta_mv = btn_mv + btn_det->v_btn_press_delta_sta;
	mbhc->mbhc_data.v_b1_hu =
	    wcd9xxx_codec_v_sta_dce(mbhc, STA, btn_delta_mv);

	btn_delta_mv = btn_mv + btn_det->v_btn_press_delta_cic;

	mbhc->mbhc_data.v_b1_huc =
	    wcd9xxx_codec_v_sta_dce(mbhc, DCE, btn_delta_mv);

	mbhc->mbhc_data.v_brh = mbhc->mbhc_data.v_b1_h;
	mbhc->mbhc_data.v_brl = BUTTON_MIN;

	mbhc->mbhc_data.v_no_mic =
	    wcd9xxx_codec_v_sta_dce(mbhc, STA, plug_type->v_no_mic);
	pr_debug("%s: leave\n", __func__);
}

static void wcd9xxx_onoff_ext_mclk(struct wcd9xxx_mbhc *mbhc, bool on)
{
	/*
	 * XXX: {codec}_mclk_enable holds WCD9XXX_BCL_LOCK,
	 * therefore wcd9xxx_onoff_ext_mclk caller SHOULDN'T hold
	 * WCD9XXX_BCL_LOCK when it calls wcd9xxx_onoff_ext_mclk()
	 */
	 mbhc->mbhc_cfg->mclk_cb_fn(mbhc->codec, on, false);
}

static void wcd9xxx_correct_swch_plug(struct work_struct *work)
{
	struct wcd9xxx_mbhc *mbhc;
	struct snd_soc_codec *codec;
	enum wcd9xxx_mbhc_plug_type plug_type = PLUG_TYPE_INVALID;
	unsigned long timeout;
	int retry = 0, pt_gnd_mic_swap_cnt = 0;
	bool correction = false;

	pr_debug("%s: enter\n", __func__);

	mbhc = container_of(work, struct wcd9xxx_mbhc, correct_plug_swch);
	codec = mbhc->codec;

	wcd9xxx_onoff_ext_mclk(mbhc, true);

	/*
	 * Keep override on during entire plug type correction work.
	 *
	 * This is okay under the assumption that any switch irqs which use
	 * MBHC block cancel and sync this work so override is off again
	 * prior to switch interrupt handler's MBHC block usage.
	 * Also while this correction work is running, we can guarantee
	 * DAPM doesn't use any MBHC block as this work only runs with
	 * headphone detection.
	 */
	wcd9xxx_turn_onoff_override(codec, true);

	timeout = jiffies + msecs_to_jiffies(HS_DETECT_PLUG_TIME_MS);
	while (!time_after(jiffies, timeout)) {
		++retry;
		rmb();
		if (mbhc->hs_detect_work_stop) {
			pr_debug("%s: stop requested\n", __func__);
			break;
		}

		msleep(HS_DETECT_PLUG_INERVAL_MS);
		if (wcd9xxx_swch_level_remove(mbhc)) {
			pr_debug("%s: Switch level is low\n", __func__);
			break;
		}

		/* can race with removal interrupt */
		WCD9XXX_BCL_LOCK(mbhc->resmgr);
		plug_type = wcd9xxx_codec_get_plug_type(mbhc, true);
		WCD9XXX_BCL_UNLOCK(mbhc->resmgr);

		pr_debug("%s: attempt(%d) current_plug(%d) new_plug(%d)\n",
			 __func__, retry, mbhc->current_plug, plug_type);
		if (plug_type == PLUG_TYPE_INVALID) {
			pr_debug("Invalid plug in attempt # %d\n", retry);
			if (!mbhc->mbhc_cfg->detect_extn_cable &&
			    retry == NUM_ATTEMPTS_TO_REPORT &&
			    mbhc->current_plug == PLUG_TYPE_NONE) {
				wcd9xxx_report_plug(mbhc, 1,
						    SND_JACK_HEADPHONE);
			}
		} else if (plug_type == PLUG_TYPE_HEADPHONE) {
			pr_debug("Good headphone detected, continue polling\n");
			if (mbhc->mbhc_cfg->detect_extn_cable) {
				if (mbhc->current_plug != plug_type)
					wcd9xxx_report_plug(mbhc, 1,
							    SND_JACK_HEADPHONE);
			} else if (mbhc->current_plug == PLUG_TYPE_NONE) {
				wcd9xxx_report_plug(mbhc, 1,
						    SND_JACK_HEADPHONE);
			}
		} else {
			if (plug_type == PLUG_TYPE_GND_MIC_SWAP) {
				pt_gnd_mic_swap_cnt++;
				if (pt_gnd_mic_swap_cnt <
				    GND_MIC_SWAP_THRESHOLD)
					continue;
				else if (pt_gnd_mic_swap_cnt >
					 GND_MIC_SWAP_THRESHOLD) {
					/*
					 * This is due to GND/MIC switch didn't
					 * work,  Report unsupported plug
					 */
				} else if (mbhc->mbhc_cfg->swap_gnd_mic) {
					/*
					 * if switch is toggled, check again,
					 * otherwise report unsupported plug
					 */
					if (mbhc->mbhc_cfg->swap_gnd_mic(codec))
						continue;
				}
			} else
				pt_gnd_mic_swap_cnt = 0;

			WCD9XXX_BCL_LOCK(mbhc->resmgr);
			/* Turn off override */
			wcd9xxx_turn_onoff_override(codec, false);
			/*
			 * The valid plug also includes PLUG_TYPE_GND_MIC_SWAP
			 */
			wcd9xxx_find_plug_and_report(mbhc, plug_type);
			WCD9XXX_BCL_UNLOCK(mbhc->resmgr);
			pr_debug("Attempt %d found correct plug %d\n", retry,
				 plug_type);
			correction = true;
			break;
		}
	}

	/* Turn off override */
	if (!correction)
		wcd9xxx_turn_onoff_override(codec, false);

	wcd9xxx_onoff_ext_mclk(mbhc, false);

	if (mbhc->mbhc_cfg->detect_extn_cable) {
		WCD9XXX_BCL_LOCK(mbhc->resmgr);
		if (mbhc->current_plug == PLUG_TYPE_HEADPHONE ||
		    mbhc->current_plug == PLUG_TYPE_GND_MIC_SWAP ||
		    mbhc->current_plug == PLUG_TYPE_INVALID ||
		    plug_type == PLUG_TYPE_INVALID) {
			/* Enable removal detection */
			wcd9xxx_cleanup_hs_polling(mbhc);
			wcd9xxx_enable_hs_detect(mbhc, 0, 0, false);
		}
		WCD9XXX_BCL_UNLOCK(mbhc->resmgr);
	}
	pr_debug("%s: leave current_plug(%d)\n", __func__, mbhc->current_plug);
	/* unlock sleep */
	wcd9xxx_unlock_sleep(mbhc->resmgr->core);
}

static void wcd9xxx_swch_irq_handler(struct wcd9xxx_mbhc *mbhc)
{
	bool insert;
	bool is_removed = false;
	struct snd_soc_codec *codec = mbhc->codec;

	pr_debug("%s: enter\n", __func__);

	mbhc->in_swch_irq_handler = true;
	/* Wait here for debounce time */
	usleep_range(SWCH_IRQ_DEBOUNCE_TIME_US, SWCH_IRQ_DEBOUNCE_TIME_US);

	WCD9XXX_BCL_LOCK(mbhc->resmgr);

	/* cancel pending button press */
	if (wcd9xxx_cancel_btn_work(mbhc))
		pr_debug("%s: button press is canceled\n", __func__);

	insert = !wcd9xxx_swch_level_remove(mbhc);
	pr_debug("%s: Current plug type %d, insert %d\n", __func__,
		 mbhc->current_plug, insert);
	if ((mbhc->current_plug == PLUG_TYPE_NONE) && insert) {
		mbhc->lpi_enabled = false;
		wmb();

		/* cancel detect plug */
		wcd9xxx_cancel_hs_detect_plug(mbhc,
					      &mbhc->correct_plug_swch);

		/* Disable Mic Bias pull down and HPH Switch to GND */
		snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.ctl_reg, 0x01,
				    0x00);
		snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x01, 0x00);
		wcd9xxx_mbhc_detect_plug_type(mbhc);
	} else if ((mbhc->current_plug != PLUG_TYPE_NONE) && !insert) {
		mbhc->lpi_enabled = false;
		wmb();

		/* cancel detect plug */
		wcd9xxx_cancel_hs_detect_plug(mbhc,
					      &mbhc->correct_plug_swch);

		if (mbhc->current_plug == PLUG_TYPE_HEADPHONE) {
			wcd9xxx_report_plug(mbhc, 0, SND_JACK_HEADPHONE);
			is_removed = true;
		} else if (mbhc->current_plug == PLUG_TYPE_GND_MIC_SWAP) {
			wcd9xxx_report_plug(mbhc, 0, SND_JACK_UNSUPPORTED);
			is_removed = true;
		} else if (mbhc->current_plug == PLUG_TYPE_HEADSET) {
			wcd9xxx_pause_hs_polling(mbhc);
			wcd9xxx_cleanup_hs_polling(mbhc);
			wcd9xxx_report_plug(mbhc, 0, SND_JACK_HEADSET);
			is_removed = true;
		} else if (mbhc->current_plug == PLUG_TYPE_HIGH_HPH) {
			wcd9xxx_report_plug(mbhc, 0, SND_JACK_LINEOUT);
			is_removed = true;
		}

		if (is_removed) {
			/* Enable Mic Bias pull down and HPH Switch to GND */
			snd_soc_update_bits(codec,
					mbhc->mbhc_bias_regs.ctl_reg, 0x01,
					0x01);
			snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x01,
					0x01);
			/* Make sure mic trigger is turned off */
			snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.ctl_reg,
					    0x01, 0x01);
			snd_soc_update_bits(codec,
					    mbhc->mbhc_bias_regs.mbhc_reg,
					    0x90, 0x00);
			/* Reset MBHC State Machine */
			snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL,
					    0x08, 0x08);
			snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL,
					    0x08, 0x00);
			/* Turn off override */
			wcd9xxx_turn_onoff_override(codec, false);
		}
	}

	mbhc->in_swch_irq_handler = false;
	WCD9XXX_BCL_UNLOCK(mbhc->resmgr);
	pr_debug("%s: leave\n", __func__);
}

static irqreturn_t wcd9xxx_mech_plug_detect_irq(int irq, void *data)
{
	int r = IRQ_HANDLED;
	struct wcd9xxx_mbhc *mbhc = data;

	pr_debug("%s: enter\n", __func__);
	if (unlikely(wcd9xxx_lock_sleep(mbhc->resmgr->core) == false)) {
		pr_warn("%s: failed to hold suspend\n", __func__);
		r = IRQ_NONE;
	} else {
		/* Call handler */
		wcd9xxx_swch_irq_handler(mbhc);
		wcd9xxx_unlock_sleep(mbhc->resmgr->core);
	}

	pr_debug("%s: leave %d\n", __func__, r);
	return r;
}

/* called under codec_resource_lock acquisition */
static void wcd9xxx_codec_drive_v_to_micbias(struct wcd9xxx_mbhc *mbhc,
					     int usec)
{
	int cfilt_k_val;
	bool set = true;

	if (mbhc->mbhc_data.micb_mv != VDDIO_MICBIAS_MV &&
	    mbhc->mbhc_micbias_switched) {
		pr_debug("%s: set mic V to micbias V\n", __func__);
		snd_soc_update_bits(mbhc->codec, WCD9XXX_A_CDC_MBHC_CLK_CTL,
				    0x2, 0x2);
		wcd9xxx_turn_onoff_override(mbhc->codec, true);
		while (1) {
			cfilt_k_val =
			    wcd9xxx_resmgr_get_k_val(mbhc->resmgr,
						set ? mbhc->mbhc_data.micb_mv :
						VDDIO_MICBIAS_MV);
			snd_soc_update_bits(mbhc->codec,
					    mbhc->mbhc_bias_regs.cfilt_val,
					    0xFC, (cfilt_k_val << 2));
			if (!set)
				break;
			usleep_range(usec, usec);
			set = false;
		}
		wcd9xxx_turn_onoff_override(mbhc->codec, false);
	}
}

static int wcd9xxx_is_fake_press(struct wcd9xxx_mbhc *mbhc)
{
	int i;
	int r = 0;
	const int dces = NUM_DCE_PLUG_DETECT;
	s16 mb_v, v_ins_hu, v_ins_h;

	v_ins_hu = wcd9xxx_get_current_v_ins(mbhc, true);
	v_ins_h = wcd9xxx_get_current_v_ins(mbhc, false);

	for (i = 0; i < dces; i++) {
		usleep_range(10000, 10000);
		if (i == 0) {
			mb_v = wcd9xxx_codec_sta_dce(mbhc, 0, true);
			pr_debug("%s: STA[0]: %d,%d\n", __func__, mb_v,
				 wcd9xxx_codec_sta_dce_v(mbhc, 0, mb_v));
			if (mb_v < (s16)mbhc->mbhc_data.v_b1_hu ||
			    mb_v > v_ins_hu) {
				r = 1;
				break;
			}
		} else {
			mb_v = wcd9xxx_codec_sta_dce(mbhc, 1, true);
			pr_debug("%s: DCE[%d]: %d,%d\n", __func__, i, mb_v,
				 wcd9xxx_codec_sta_dce_v(mbhc, 1, mb_v));
			if (mb_v < (s16)mbhc->mbhc_data.v_b1_h ||
			    mb_v > v_ins_h) {
				r = 1;
				break;
			}
		}
	}

	return r;
}

/* called under codec_resource_lock acquisition */
static int wcd9xxx_determine_button(const struct wcd9xxx_mbhc *mbhc,
				  const s32 micmv)
{
	s16 *v_btn_low, *v_btn_high;
	struct wcd9xxx_mbhc_btn_detect_cfg *btn_det;
	int i, btn = -1;

	btn_det = WCD9XXX_MBHC_CAL_BTN_DET_PTR(mbhc->mbhc_cfg->calibration);
	v_btn_low = wcd9xxx_mbhc_cal_btn_det_mp(btn_det,
						MBHC_BTN_DET_V_BTN_LOW);
	v_btn_high = wcd9xxx_mbhc_cal_btn_det_mp(btn_det,
						 MBHC_BTN_DET_V_BTN_HIGH);

	for (i = 0; i < btn_det->num_btn; i++) {
		if ((v_btn_low[i] <= micmv) && (v_btn_high[i] >= micmv)) {
			btn = i;
			break;
		}
	}

	if (btn == -1)
		pr_debug("%s: couldn't find button number for mic mv %d\n",
			 __func__, micmv);

	return btn;
}

static int wcd9xxx_get_button_mask(const int btn)
{
	int mask = 0;
	switch (btn) {
	case 0:
		mask = SND_JACK_BTN_0;
		break;
	case 1:
		mask = SND_JACK_BTN_1;
		break;
	case 2:
		mask = SND_JACK_BTN_2;
		break;
	case 3:
		mask = SND_JACK_BTN_3;
		break;
	case 4:
		mask = SND_JACK_BTN_4;
		break;
	case 5:
		mask = SND_JACK_BTN_5;
		break;
	case 6:
		mask = SND_JACK_BTN_6;
		break;
	case 7:
		mask = SND_JACK_BTN_7;
		break;
	}
	return mask;
}

irqreturn_t wcd9xxx_dce_handler(int irq, void *data)
{
	int i, mask;
	short dce, sta;
	s32 mv, mv_s, stamv_s;
	bool vddio;
	u8 mbhc_status;
	int btn = -1, meas = 0;
	struct wcd9xxx_mbhc *mbhc = data;
	const struct wcd9xxx_mbhc_btn_detect_cfg *d =
	    WCD9XXX_MBHC_CAL_BTN_DET_PTR(mbhc->mbhc_cfg->calibration);
	short btnmeas[d->n_btn_meas + 1];
	struct snd_soc_codec *codec = mbhc->codec;
	struct wcd9xxx *core = mbhc->resmgr->core;
	int n_btn_meas = d->n_btn_meas;

	pr_debug("%s: enter\n", __func__);

	WCD9XXX_BCL_LOCK(mbhc->resmgr);
	mbhc_status = snd_soc_read(codec, WCD9XXX_A_CDC_MBHC_B1_STATUS) & 0x3E;

	if (mbhc->mbhc_state == MBHC_STATE_POTENTIAL_RECOVERY) {
		pr_debug("%s: mbhc is being recovered, skip button press\n",
			 __func__);
		goto done;
	}

	mbhc->mbhc_state = MBHC_STATE_POTENTIAL;

	if (!mbhc->polling_active) {
		pr_warn("%s: mbhc polling is not active, skip button press\n",
			__func__);
		goto done;
	}

	dce = wcd9xxx_read_dce_result(codec);
	mv = wcd9xxx_codec_sta_dce_v(mbhc, 1, dce);

	/* If switch nterrupt already kicked in, ignore button press */
	if (mbhc->in_swch_irq_handler) {
		pr_debug("%s: Swtich level changed, ignore button press\n",
			 __func__);
		btn = -1;
		goto done;
	}

	/* Measure scaled HW DCE */
	vddio = (mbhc->mbhc_data.micb_mv != VDDIO_MICBIAS_MV &&
		 mbhc->mbhc_micbias_switched);
	mv_s = vddio ? scale_v_micb_vddio(mbhc, mv, false) : mv;

	/* Measure scaled HW STA */
	sta = wcd9xxx_read_sta_result(codec);
	stamv_s = wcd9xxx_codec_sta_dce_v(mbhc, 0, sta);
	if (vddio)
		stamv_s = scale_v_micb_vddio(mbhc, stamv_s, false);
	if (mbhc_status != STATUS_REL_DETECTION) {
		if (mbhc->mbhc_last_resume &&
		    !time_after(jiffies, mbhc->mbhc_last_resume + HZ)) {
			pr_debug("%s: Button is released after resume\n",
				__func__);
			n_btn_meas = 0;
		} else {
			pr_debug("%s: Button is released without resume",
				 __func__);
			btn = wcd9xxx_determine_button(mbhc, mv_s);
			if (btn != wcd9xxx_determine_button(mbhc, stamv_s))
				btn = -1;
			goto done;
		}
	}

	pr_debug("%s: Meas HW - STA 0x%x,%d,%d\n", __func__,
		 sta & 0xFFFF, wcd9xxx_codec_sta_dce_v(mbhc, 0, sta), stamv_s);

	/* determine pressed button */
	btnmeas[meas++] = wcd9xxx_determine_button(mbhc, mv_s);
	pr_debug("%s: Meas HW - DCE 0x%x,%d,%d button %d\n", __func__,
		 dce & 0xFFFF, mv, mv_s, btnmeas[meas - 1]);
	if (n_btn_meas == 0)
		btn = btnmeas[0];
	for (; ((d->n_btn_meas) && (meas < (d->n_btn_meas + 1))); meas++) {
		dce = wcd9xxx_codec_sta_dce(mbhc, 1, false);
		mv = wcd9xxx_codec_sta_dce_v(mbhc, 1, dce);
		mv_s = vddio ? scale_v_micb_vddio(mbhc, mv, false) : mv;

		btnmeas[meas] = wcd9xxx_determine_button(mbhc, mv_s);
		pr_debug("%s: Meas %d - DCE 0x%x,%d,%d button %d\n",
			 __func__, meas, dce & 0xFFFF, mv, mv_s, btnmeas[meas]);
		/*
		 * if large enough measurements are collected,
		 * start to check if last all n_btn_con measurements were
		 * in same button low/high range
		 */
		if (meas + 1 >= d->n_btn_con) {
			for (i = 0; i < d->n_btn_con; i++)
				if ((btnmeas[meas] < 0) ||
				    (btnmeas[meas] != btnmeas[meas - i]))
					break;
			if (i == d->n_btn_con) {
				/* button pressed */
				btn = btnmeas[meas];
				break;
			} else if ((n_btn_meas - meas) < (d->n_btn_con - 1)) {
				/*
				 * if left measurements are less than n_btn_con,
				 * it's impossible to find button number
				 */
				break;
			}
		}
	}

	if (btn >= 0) {
		if (mbhc->in_swch_irq_handler) {
			pr_debug(
			"%s: Switch irq triggered, ignore button press\n",
			__func__);
			goto done;
		}
		mask = wcd9xxx_get_button_mask(btn);
		mbhc->buttons_pressed |= mask;
		wcd9xxx_lock_sleep(core);
		if (schedule_delayed_work(&mbhc->mbhc_btn_dwork,
					  msecs_to_jiffies(400)) == 0) {
			WARN(1, "Button pressed twice without release event\n");
			wcd9xxx_unlock_sleep(core);
		}
	} else {
		pr_debug("%s: bogus button press, too short press?\n",
			 __func__);
	}

 done:
	pr_debug("%s: leave\n", __func__);
	WCD9XXX_BCL_UNLOCK(mbhc->resmgr);
	return IRQ_HANDLED;
}

static irqreturn_t wcd9xxx_release_handler(int irq, void *data)
{
	int ret;
	struct wcd9xxx_mbhc *mbhc = data;

	pr_debug("%s: enter\n", __func__);
	WCD9XXX_BCL_LOCK(mbhc->resmgr);
	mbhc->mbhc_state = MBHC_STATE_RELEASE;

	wcd9xxx_codec_drive_v_to_micbias(mbhc, 10000);

	if (mbhc->buttons_pressed & WCD9XXX_JACK_BUTTON_MASK) {
		ret = wcd9xxx_cancel_btn_work(mbhc);
		if (ret == 0) {
			pr_debug("%s: Reporting long button release event\n",
				 __func__);
			wcd9xxx_jack_report(&mbhc->button_jack, 0,
					    mbhc->buttons_pressed);
		} else {
			if (wcd9xxx_is_fake_press(mbhc)) {
				pr_debug("%s: Fake button press interrupt\n",
					 __func__);
			} else {
				if (mbhc->in_swch_irq_handler) {
					pr_debug("%s: Switch irq kicked in, ignore\n",
						 __func__);
				} else {
					pr_debug("%s: Reporting btn press\n",
						 __func__);
					wcd9xxx_jack_report(&mbhc->button_jack,
							 mbhc->buttons_pressed,
							 mbhc->buttons_pressed);
					pr_debug("%s: Reporting btn release\n",
						 __func__);
					wcd9xxx_jack_report(&mbhc->button_jack,
						      0, mbhc->buttons_pressed);
				}
			}
		}

		mbhc->buttons_pressed &= ~WCD9XXX_JACK_BUTTON_MASK;
	}

	wcd9xxx_calibrate_hs_polling(mbhc);

	msleep(SWCH_REL_DEBOUNCE_TIME_MS);
	wcd9xxx_start_hs_polling(mbhc);

	pr_debug("%s: leave\n", __func__);
	WCD9XXX_BCL_UNLOCK(mbhc->resmgr);
	return IRQ_HANDLED;
}

static irqreturn_t wcd9xxx_hphl_ocp_irq(int irq, void *data)
{
	struct wcd9xxx_mbhc *mbhc = data;
	struct snd_soc_codec *codec;

	pr_info("%s: received HPHL OCP irq\n", __func__);

	if (mbhc) {
		codec = mbhc->codec;
		if (mbhc->hphlocp_cnt++ < OCP_ATTEMPT) {
			pr_info("%s: retry\n", __func__);
			snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_OCP_CTL,
					    0x10, 0x00);
			snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_OCP_CTL,
					    0x10, 0x10);
		} else {
			wcd9xxx_disable_irq(codec->control_data,
					  WCD9XXX_IRQ_HPH_PA_OCPL_FAULT);
			mbhc->hphlocp_cnt = 0;
			mbhc->hph_status |= SND_JACK_OC_HPHL;
			wcd9xxx_jack_report(&mbhc->headset_jack,
					    mbhc->hph_status,
					    WCD9XXX_JACK_MASK);
		}
	} else {
		pr_err("%s: Bad wcd9xxx private data\n", __func__);
	}

	return IRQ_HANDLED;
}

static irqreturn_t wcd9xxx_hphr_ocp_irq(int irq, void *data)
{
	struct wcd9xxx_mbhc *mbhc = data;
	struct snd_soc_codec *codec;

	pr_info("%s: received HPHR OCP irq\n", __func__);
	codec = mbhc->codec;
	if (mbhc->hphrocp_cnt++ < OCP_ATTEMPT) {
		pr_info("%s: retry\n", __func__);
		snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_OCP_CTL, 0x10,
				    0x00);
		snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_OCP_CTL, 0x10,
				    0x10);
	} else {
		wcd9xxx_disable_irq(mbhc->resmgr->core,
				    WCD9XXX_IRQ_HPH_PA_OCPR_FAULT);
		mbhc->hphrocp_cnt = 0;
		mbhc->hph_status |= SND_JACK_OC_HPHR;
		wcd9xxx_jack_report(&mbhc->headset_jack,
				    mbhc->hph_status, WCD9XXX_JACK_MASK);
	}

	return IRQ_HANDLED;
}

static int wcd9xxx_acdb_mclk_index(const int rate)
{
	if (rate == MCLK_RATE_12288KHZ)
		return 0;
	else if (rate == MCLK_RATE_9600KHZ)
		return 1;
	else {
		BUG_ON(1);
		return -EINVAL;
	}
}

static void wcd9xxx_update_mbhc_clk_rate(struct wcd9xxx_mbhc *mbhc, u32 rate)
{
	u32 dce_wait, sta_wait;
	u8 ncic, nmeas, navg;
	void *calibration;
	u8 *n_cic, *n_ready;
	struct wcd9xxx_mbhc_btn_detect_cfg *btn_det;
	u8 npoll = 4, nbounce_wait = 30;
	struct snd_soc_codec *codec = mbhc->codec;
	int idx = wcd9xxx_acdb_mclk_index(rate);
	int idxmclk = wcd9xxx_acdb_mclk_index(mbhc->mbhc_cfg->mclk_rate);

	pr_debug("%s: Updating clock rate dependents, rate = %u\n", __func__,
		 rate);
	calibration = mbhc->mbhc_cfg->calibration;

	/*
	 * First compute the DCE / STA wait times depending on tunable
	 * parameters. The value is computed in microseconds
	 */
	btn_det = WCD9XXX_MBHC_CAL_BTN_DET_PTR(calibration);
	n_ready = wcd9xxx_mbhc_cal_btn_det_mp(btn_det, MBHC_BTN_DET_N_READY);
	n_cic = wcd9xxx_mbhc_cal_btn_det_mp(btn_det, MBHC_BTN_DET_N_CIC);
	nmeas = WCD9XXX_MBHC_CAL_BTN_DET_PTR(calibration)->n_meas;
	navg = WCD9XXX_MBHC_CAL_GENERAL_PTR(calibration)->mbhc_navg;

	/* ncic stays with the same what we had during calibration */
	ncic = n_cic[idxmclk];
	dce_wait = (1000 * 512 * ncic * (nmeas + 1)) / (rate / 1000);
	sta_wait = (1000 * 128 * (navg + 1)) / (rate / 1000);
	mbhc->mbhc_data.t_dce = dce_wait;
	mbhc->mbhc_data.t_sta = sta_wait;
	mbhc->mbhc_data.t_sta_dce = ((1000 * 256) / (rate / 1000) *
				     n_ready[idx]) + 10;

	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_TIMER_B1_CTL, n_ready[idx]);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_TIMER_B6_CTL, ncic);

	if (rate == MCLK_RATE_12288KHZ) {
		npoll = 4;
		nbounce_wait = 30;
	} else if (rate == MCLK_RATE_9600KHZ) {
		npoll = 3;
		nbounce_wait = 23;
	}

	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_TIMER_B2_CTL, npoll);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_TIMER_B3_CTL, nbounce_wait);
	pr_debug("%s: leave\n", __func__);
}

static void wcd9xxx_mbhc_cal(struct wcd9xxx_mbhc *mbhc)
{
	u8 cfilt_mode;
	struct snd_soc_codec *codec = mbhc->codec;

	pr_debug("%s: enter\n", __func__);
	wcd9xxx_disable_irq(codec->control_data, WCD9XXX_IRQ_MBHC_POTENTIAL);
	wcd9xxx_turn_onoff_rel_detection(codec, false);

	/* t_dce and t_sta are updated by wcd9xxx_update_mbhc_clk_rate() */
	WARN_ON(!mbhc->mbhc_data.t_dce);
	WARN_ON(!mbhc->mbhc_data.t_sta);

	/*
	 * LDOH and CFILT are already configured during pdata handling.
	 * Only need to make sure CFILT and bandgap are in Fast mode.
	 * Need to restore defaults once calculation is done.
	 */
	cfilt_mode = snd_soc_read(codec, mbhc->mbhc_bias_regs.cfilt_ctl);
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.cfilt_ctl, 0x40, 0x00);

	/*
	 * Micbias, CFILT, LDOH, MBHC MUX mode settings
	 * to perform ADC calibration
	 */
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.ctl_reg, 0x60,
			    mbhc->mbhc_cfg->micbias << 5);
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.ctl_reg, 0x01, 0x00);
	snd_soc_update_bits(codec, WCD9XXX_A_LDO_H_MODE_1, 0x60, 0x60);
	snd_soc_write(codec, WCD9XXX_A_TX_7_MBHC_TEST_CTL, 0x78);
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x04, 0x04);

	/* DCE measurement for 0 volts */
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x0A);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x04);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x02);
	snd_soc_write(codec, WCD9XXX_A_MBHC_SCALING_MUX_1, 0x81);
	usleep_range(100, 100);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x04);
	usleep_range(mbhc->mbhc_data.t_dce, mbhc->mbhc_data.t_dce);
	mbhc->mbhc_data.dce_z = wcd9xxx_read_dce_result(codec);

	/* DCE measurment for MB voltage */
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x0A);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x02);
	snd_soc_write(codec, WCD9XXX_A_MBHC_SCALING_MUX_1, 0x82);
	usleep_range(100, 100);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x04);
	usleep_range(mbhc->mbhc_data.t_dce, mbhc->mbhc_data.t_dce);
	mbhc->mbhc_data.dce_mb = wcd9xxx_read_dce_result(codec);

	/* STA measuremnt for 0 volts */
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x0A);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x02);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_CLK_CTL, 0x02);
	snd_soc_write(codec, WCD9XXX_A_MBHC_SCALING_MUX_1, 0x81);
	usleep_range(100, 100);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x02);
	usleep_range(mbhc->mbhc_data.t_sta, mbhc->mbhc_data.t_sta);
	mbhc->mbhc_data.sta_z = wcd9xxx_read_sta_result(codec);

	/* STA Measurement for MB Voltage */
	snd_soc_write(codec, WCD9XXX_A_MBHC_SCALING_MUX_1, 0x82);
	usleep_range(100, 100);
	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_EN_CTL, 0x02);
	usleep_range(mbhc->mbhc_data.t_sta, mbhc->mbhc_data.t_sta);
	mbhc->mbhc_data.sta_mb = wcd9xxx_read_sta_result(codec);

	/* Restore default settings. */
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x04, 0x00);
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.cfilt_ctl, 0x40,
			    cfilt_mode);

	snd_soc_write(codec, WCD9XXX_A_MBHC_SCALING_MUX_1, 0x84);
	usleep_range(100, 100);

	wcd9xxx_enable_irq(codec->control_data, WCD9XXX_IRQ_MBHC_POTENTIAL);
	wcd9xxx_turn_onoff_rel_detection(codec, true);

	pr_debug("%s: leave\n", __func__);
}

static void wcd9xxx_mbhc_setup(struct wcd9xxx_mbhc *mbhc)
{
	int n;
	u8 *gain;
	struct wcd9xxx_mbhc_general_cfg *generic;
	struct wcd9xxx_mbhc_btn_detect_cfg *btn_det;
	struct snd_soc_codec *codec = mbhc->codec;
	const int idx = wcd9xxx_acdb_mclk_index(mbhc->mbhc_cfg->mclk_rate);

	pr_debug("%s: enter\n", __func__);
	generic = WCD9XXX_MBHC_CAL_GENERAL_PTR(mbhc->mbhc_cfg->calibration);
	btn_det = WCD9XXX_MBHC_CAL_BTN_DET_PTR(mbhc->mbhc_cfg->calibration);

	for (n = 0; n < 8; n++) {
		snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_FIR_B1_CFG,
				    0x07, n);
		snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_FIR_B2_CFG,
			      btn_det->c[n]);
	}

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B2_CTL, 0x07,
			    btn_det->nc);

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_TIMER_B4_CTL, 0x70,
			    generic->mbhc_nsa << 4);

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_TIMER_B4_CTL, 0x0F,
			    btn_det->n_meas);

	snd_soc_write(codec, WCD9XXX_A_CDC_MBHC_TIMER_B5_CTL,
		      generic->mbhc_navg);

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x80, 0x80);

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x78,
			    btn_det->mbhc_nsc << 3);

	snd_soc_update_bits(codec, mbhc->resmgr->reg_addr->micb_4_mbhc, 0x03,
			    MBHC_MICBIAS2);

	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B1_CTL, 0x02, 0x02);

	snd_soc_update_bits(codec, WCD9XXX_A_MBHC_SCALING_MUX_2, 0xF0, 0xF0);

	gain = wcd9xxx_mbhc_cal_btn_det_mp(btn_det, MBHC_BTN_DET_GAIN);
	snd_soc_update_bits(codec, WCD9XXX_A_CDC_MBHC_B2_CTL, 0x78,
			    gain[idx] << 3);

	pr_debug("%s: leave\n", __func__);
}

static int wcd9xxx_setup_jack_detect_irq(struct wcd9xxx_mbhc *mbhc)
{
	int ret = 0;
	void *core = mbhc->resmgr->core;

	if (mbhc->mbhc_cfg->gpio) {
		ret = request_threaded_irq(mbhc->mbhc_cfg->gpio_irq, NULL,
					   wcd9xxx_mech_plug_detect_irq,
					   (IRQF_TRIGGER_RISING |
					    IRQF_TRIGGER_FALLING |
					    IRQF_DISABLED),
					   "headset detect", mbhc);
		if (ret) {
			pr_err("%s: Failed to request gpio irq %d\n", __func__,
			       mbhc->mbhc_cfg->gpio_irq);
		} else {
			ret = enable_irq_wake(mbhc->mbhc_cfg->gpio_irq);
			if (ret)
				pr_err("%s: Failed to enable wake up irq %d\n",
				       __func__, mbhc->mbhc_cfg->gpio_irq);
		}
	} else if (mbhc->mbhc_cfg->insert_detect) {
		/* Enable HPHL_10K_SW */
		snd_soc_update_bits(mbhc->codec, WCD9XXX_A_RX_HPH_OCP_CTL,
				    1 << 1, 1 << 1);
		ret = wcd9xxx_request_irq(core, WCD9XXX_IRQ_MBHC_JACK_SWITCH,
					  wcd9xxx_mech_plug_detect_irq,
					  "Jack Detect",
					  mbhc);
		if (ret)
			pr_err("%s: Failed to request insert detect irq %d\n",
			       __func__, WCD9XXX_IRQ_MBHC_JACK_SWITCH);
	}

	return ret;
}

static int wcd9xxx_init_and_calibrate(struct wcd9xxx_mbhc *mbhc)
{
	int ret = 0;
	struct snd_soc_codec *codec = mbhc->codec;

	pr_debug("%s: enter\n", __func__);

	/* Enable MCLK during calibration */
	wcd9xxx_onoff_ext_mclk(mbhc, true);
	wcd9xxx_mbhc_setup(mbhc);
	wcd9xxx_mbhc_cal(mbhc);
	wcd9xxx_mbhc_calc_thres(mbhc);
	wcd9xxx_onoff_ext_mclk(mbhc, false);
	wcd9xxx_calibrate_hs_polling(mbhc);

	/* Enable Mic Bias pull down and HPH Switch to GND */
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.ctl_reg, 0x01, 0x01);
	snd_soc_update_bits(codec, WCD9XXX_A_MBHC_HPH, 0x01, 0x01);
	INIT_WORK(&mbhc->correct_plug_swch, wcd9xxx_correct_swch_plug);

	if (!IS_ERR_VALUE(ret)) {
		snd_soc_update_bits(codec, WCD9XXX_A_RX_HPH_OCP_CTL, 0x10,
				    0x10);
		wcd9xxx_enable_irq(codec->control_data,
				   WCD9XXX_IRQ_HPH_PA_OCPL_FAULT);
		wcd9xxx_enable_irq(codec->control_data,
				   WCD9XXX_IRQ_HPH_PA_OCPR_FAULT);

		/* Initialize mechanical mbhc */
		ret = wcd9xxx_setup_jack_detect_irq(mbhc);

		if (!ret && mbhc->mbhc_cfg->gpio) {
			/* Requested with IRQF_DISABLED */
			enable_irq(mbhc->mbhc_cfg->gpio_irq);

			/* Bootup time detection */
			wcd9xxx_swch_irq_handler(mbhc);
		} else if (!ret && mbhc->mbhc_cfg->insert_detect) {
			pr_debug("%s: Setting up codec own insert detection\n",
				 __func__);
			/* Setup for insertion detection */
			wcd9xxx_insert_detect_setup(mbhc, true);
		}
	}

	pr_debug("%s: leave\n", __func__);

	return ret;
}

static void wcd9xxx_mbhc_fw_read(struct work_struct *work)
{
	struct delayed_work *dwork;
	struct wcd9xxx_mbhc *mbhc;
	struct snd_soc_codec *codec;
	const struct firmware *fw;
	int ret = -1, retry = 0;

	dwork = to_delayed_work(work);
	mbhc = container_of(dwork, struct wcd9xxx_mbhc, mbhc_firmware_dwork);
	codec = mbhc->codec;

	while (retry < FW_READ_ATTEMPTS) {
		retry++;
		pr_info("%s:Attempt %d to request MBHC firmware\n",
			__func__, retry);
		ret = request_firmware(&fw, "wcd9320/wcd9320_mbhc.bin",
				       codec->dev);

		if (ret != 0) {
			usleep_range(FW_READ_TIMEOUT, FW_READ_TIMEOUT);
		} else {
			pr_info("%s: MBHC Firmware read succesful\n", __func__);
			break;
		}
	}

	if (ret != 0) {
		pr_err("%s: Cannot load MBHC firmware use default cal\n",
		       __func__);
	} else if (wcd9xxx_mbhc_fw_validate(fw) == false) {
		pr_err("%s: Invalid MBHC cal data size use default cal\n",
		       __func__);
		release_firmware(fw);
	} else {
		mbhc->mbhc_cfg->calibration = (void *)fw->data;
		mbhc->mbhc_fw = fw;
	}

	(void) wcd9xxx_init_and_calibrate(mbhc);
}

#ifdef CONFIG_DEBUG_FS
ssize_t codec_mbhc_debug_read(struct file *file, char __user *buf,
			      size_t count, loff_t *pos)
{
	const int size = 768;
	char buffer[size];
	int n = 0;
	struct wcd9xxx_mbhc *mbhc = file->private_data;
	const struct mbhc_internal_cal_data *p = &mbhc->mbhc_data;
	const s16 v_ins_hu_cur = wcd9xxx_get_current_v_ins(mbhc, true);
	const s16 v_ins_h_cur = wcd9xxx_get_current_v_ins(mbhc, false);

	n = scnprintf(buffer, size - n, "dce_z = %x(%dmv)\n",  p->dce_z,
		      wcd9xxx_codec_sta_dce_v(mbhc, 1, p->dce_z));
	n += scnprintf(buffer + n, size - n, "dce_mb = %x(%dmv)\n",
		       p->dce_mb, wcd9xxx_codec_sta_dce_v(mbhc, 1, p->dce_mb));
	n += scnprintf(buffer + n, size - n, "sta_z = %x(%dmv)\n",
		       p->sta_z, wcd9xxx_codec_sta_dce_v(mbhc, 0, p->sta_z));
	n += scnprintf(buffer + n, size - n, "sta_mb = %x(%dmv)\n",
		       p->sta_mb, wcd9xxx_codec_sta_dce_v(mbhc, 0, p->sta_mb));
	n += scnprintf(buffer + n, size - n, "t_dce = %x\n",  p->t_dce);
	n += scnprintf(buffer + n, size - n, "t_sta = %x\n",  p->t_sta);
	n += scnprintf(buffer + n, size - n, "micb_mv = %dmv\n",
		       p->micb_mv);
	n += scnprintf(buffer + n, size - n, "v_ins_hu = %x(%dmv)%s\n",
		       p->v_ins_hu,
		       wcd9xxx_codec_sta_dce_v(mbhc, 0, p->v_ins_hu),
		       p->v_ins_hu == v_ins_hu_cur ? "*" : "");
	n += scnprintf(buffer + n, size - n, "v_ins_h = %x(%dmv)%s\n",
		       p->v_ins_h, wcd9xxx_codec_sta_dce_v(mbhc, 1, p->v_ins_h),
		       p->v_ins_h == v_ins_h_cur ? "*" : "");
	n += scnprintf(buffer + n, size - n, "adj_v_ins_hu = %x(%dmv)%s\n",
		       p->adj_v_ins_hu,
		       wcd9xxx_codec_sta_dce_v(mbhc, 0, p->adj_v_ins_hu),
		       p->adj_v_ins_hu == v_ins_hu_cur ? "*" : "");
	n += scnprintf(buffer + n, size - n, "adj_v_ins_h = %x(%dmv)%s\n",
		       p->adj_v_ins_h,
		       wcd9xxx_codec_sta_dce_v(mbhc, 1, p->adj_v_ins_h),
		       p->adj_v_ins_h == v_ins_h_cur ? "*" : "");
	n += scnprintf(buffer + n, size - n, "v_b1_hu = %x(%dmv)\n",
		       p->v_b1_hu,
		       wcd9xxx_codec_sta_dce_v(mbhc, 0, p->v_b1_hu));
	n += scnprintf(buffer + n, size - n, "v_b1_h = %x(%dmv)\n",
		       p->v_b1_h, wcd9xxx_codec_sta_dce_v(mbhc, 1, p->v_b1_h));
	n += scnprintf(buffer + n, size - n, "v_b1_huc = %x(%dmv)\n",
		       p->v_b1_huc,
		       wcd9xxx_codec_sta_dce_v(mbhc, 1, p->v_b1_huc));
	n += scnprintf(buffer + n, size - n, "v_brh = %x(%dmv)\n",
		       p->v_brh, wcd9xxx_codec_sta_dce_v(mbhc, 1, p->v_brh));
	n += scnprintf(buffer + n, size - n, "v_brl = %x(%dmv)\n",  p->v_brl,
		       wcd9xxx_codec_sta_dce_v(mbhc, 0, p->v_brl));
	n += scnprintf(buffer + n, size - n, "v_no_mic = %x(%dmv)\n",
		       p->v_no_mic,
		       wcd9xxx_codec_sta_dce_v(mbhc, 0, p->v_no_mic));
	n += scnprintf(buffer + n, size - n, "v_inval_ins_low = %d\n",
		       p->v_inval_ins_low);
	n += scnprintf(buffer + n, size - n, "v_inval_ins_high = %d\n",
		       p->v_inval_ins_high);
	n += scnprintf(buffer + n, size - n, "Insert detect insert = %d\n",
		       !wcd9xxx_swch_level_remove(mbhc));
	buffer[n] = 0;

	return simple_read_from_buffer(buf, count, pos, buffer, n);
}

static int codec_debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t codec_debug_write(struct file *filp,
				 const char __user *ubuf, size_t cnt,
				 loff_t *ppos)
{
	char lbuf[32];
	char *buf;
	int rc;
	struct wcd9xxx_mbhc *mbhc = filp->private_data;

	if (cnt > sizeof(lbuf) - 1)
		return -EINVAL;

	rc = copy_from_user(lbuf, ubuf, cnt);
	if (rc)
		return -EFAULT;

	lbuf[cnt] = '\0';
	buf = (char *)lbuf;
	mbhc->no_mic_headset_override = (*strsep(&buf, " ") == '0') ?
					     false : true;
	return rc;
}

static const struct file_operations mbhc_trrs_debug_ops = {
	.open = codec_debug_open,
	.write = codec_debug_write,
};

static const struct file_operations mbhc_debug_ops = {
	.open = codec_debug_open,
	.read = codec_mbhc_debug_read,
};

static void wcd9xxx_init_debugfs(struct wcd9xxx_mbhc *mbhc)
{
	mbhc->debugfs_poke =
	    debugfs_create_file("TRRS", S_IFREG | S_IRUGO, NULL, mbhc,
				&mbhc_trrs_debug_ops);
	mbhc->debugfs_mbhc =
	    debugfs_create_file("wcd9xxx_mbhc", S_IFREG | S_IRUGO,
				NULL, mbhc, &mbhc_debug_ops);
}

static void wcd9xxx_cleanup_debugfs(struct wcd9xxx_mbhc *mbhc)
{
	debugfs_remove(mbhc->debugfs_poke);
	debugfs_remove(mbhc->debugfs_mbhc);
}
#else
static void wcd9xxx_init_debugfs(struct wcd9xxx_mbhc *mbhc)
{
}

static void wcd9xxx_cleanup_debugfs(struct wcd9xxx_mbhc *mbhc)
{
}
#endif

int wcd9xxx_mbhc_start(struct wcd9xxx_mbhc *mbhc,
		       struct wcd9xxx_mbhc_config *mbhc_cfg)
{
	int rc = 0;
	struct snd_soc_codec *codec = mbhc->codec;

	pr_debug("%s: enter\n", __func__);

	if (!codec) {
		pr_err("%s: no codec\n", __func__);
		return -EINVAL;
	}

	if (mbhc_cfg->mclk_rate != MCLK_RATE_12288KHZ &&
	    mbhc_cfg->mclk_rate != MCLK_RATE_9600KHZ) {
		pr_err("Error: unsupported clock rate %d\n",
		       mbhc_cfg->mclk_rate);
		return -EINVAL;
	}

	/* Save mbhc config */
	mbhc->mbhc_cfg = mbhc_cfg;

	/* Get HW specific mbhc registers' address */
	wcd9xxx_get_mbhc_micbias_regs(mbhc, &mbhc->mbhc_bias_regs);

	/* Put CFILT in fast mode by default */
	snd_soc_update_bits(codec, mbhc->mbhc_bias_regs.cfilt_ctl,
			    0x40, WCD9XXX_CFILT_FAST_MODE);

	if (!mbhc->mbhc_cfg->read_fw_bin)
		rc = wcd9xxx_init_and_calibrate(mbhc);
	else
		schedule_delayed_work(&mbhc->mbhc_firmware_dwork,
				      usecs_to_jiffies(FW_READ_TIMEOUT));

	pr_debug("%s: leave %d\n", __func__, rc);
	return rc;
}
EXPORT_SYMBOL_GPL(wcd9xxx_mbhc_start);

static enum wcd9xxx_micbias_num
wcd9xxx_event_to_micbias(const enum wcd9xxx_notify_event event)
{
	enum wcd9xxx_micbias_num ret;
	switch (event) {
	case WCD9XXX_EVENT_PRE_MICBIAS_1_ON:
		ret = MBHC_MICBIAS1;
	case WCD9XXX_EVENT_PRE_MICBIAS_2_ON:
		ret = MBHC_MICBIAS2;
	case WCD9XXX_EVENT_PRE_MICBIAS_3_ON:
		ret = MBHC_MICBIAS3;
	case WCD9XXX_EVENT_PRE_MICBIAS_4_ON:
		ret = MBHC_MICBIAS4;
	default:
		ret = MBHC_MICBIAS_INVALID;
	}
	return ret;
}

static int wcd9xxx_event_to_cfilt(const enum wcd9xxx_notify_event event)
{
	int ret;
	switch (event) {
	case WCD9XXX_EVENT_PRE_CFILT_1_OFF:
	case WCD9XXX_EVENT_POST_CFILT_1_OFF:
	case WCD9XXX_EVENT_PRE_CFILT_1_ON:
	case WCD9XXX_EVENT_POST_CFILT_1_ON:
		ret = WCD9XXX_CFILT1_SEL;
		break;
	case WCD9XXX_EVENT_PRE_CFILT_2_OFF:
	case WCD9XXX_EVENT_POST_CFILT_2_OFF:
	case WCD9XXX_EVENT_PRE_CFILT_2_ON:
	case WCD9XXX_EVENT_POST_CFILT_2_ON:
		ret = WCD9XXX_CFILT2_SEL;
		break;
	case WCD9XXX_EVENT_PRE_CFILT_3_OFF:
	case WCD9XXX_EVENT_POST_CFILT_3_OFF:
	case WCD9XXX_EVENT_PRE_CFILT_3_ON:
	case WCD9XXX_EVENT_POST_CFILT_3_ON:
		ret = WCD9XXX_CFILT3_SEL;
		break;
	default:
		ret = -1;
	}
	return ret;
}

static int wcd9xxx_get_mbhc_cfilt_sel(struct wcd9xxx_mbhc *mbhc)
{
	int cfilt;
	const struct wcd9xxx_pdata *pdata = mbhc->resmgr->pdata;

	switch (mbhc->mbhc_cfg->micbias) {
	case MBHC_MICBIAS1:
		cfilt = pdata->micbias.bias1_cfilt_sel;
		break;
	case MBHC_MICBIAS2:
		cfilt = pdata->micbias.bias2_cfilt_sel;
		break;
	case MBHC_MICBIAS3:
		cfilt = pdata->micbias.bias3_cfilt_sel;
		break;
	case MBHC_MICBIAS4:
		cfilt = pdata->micbias.bias4_cfilt_sel;
		break;
	default:
		cfilt = MBHC_MICBIAS_INVALID;
		break;
	}
	return cfilt;
}

static int wcd9xxx_event_notify(struct notifier_block *self, unsigned long val,
				void *data)
{
	int ret = 0;
	struct wcd9xxx_mbhc *mbhc = ((struct wcd9xxx_resmgr *)data)->mbhc;
	struct snd_soc_codec *codec = mbhc->codec;
	enum wcd9xxx_notify_event event = (enum wcd9xxx_notify_event)val;

	pr_debug("%s: enter event %s(%d)\n", __func__,
		 wcd9xxx_get_event_string(event), event);

	switch (event) {
	/* MICBIAS usage change */
	case WCD9XXX_EVENT_PRE_MICBIAS_1_ON:
	case WCD9XXX_EVENT_PRE_MICBIAS_2_ON:
	case WCD9XXX_EVENT_PRE_MICBIAS_3_ON:
	case WCD9XXX_EVENT_PRE_MICBIAS_4_ON:
		if (mbhc->mbhc_cfg->micbias == wcd9xxx_event_to_micbias(event))
			wcd9xxx_switch_micbias(mbhc, 0);
		break;
	case WCD9XXX_EVENT_POST_MICBIAS_1_ON:
	case WCD9XXX_EVENT_POST_MICBIAS_2_ON:
	case WCD9XXX_EVENT_POST_MICBIAS_3_ON:
	case WCD9XXX_EVENT_POST_MICBIAS_4_ON:
		if (mbhc->mbhc_cfg->micbias ==
		    wcd9xxx_event_to_micbias(event) &&
		    wcd9xxx_mbhc_polling(mbhc)) {
			/* if polling is on, restart it */
			wcd9xxx_pause_hs_polling(mbhc);
			wcd9xxx_start_hs_polling(mbhc);
		}
		break;
	case WCD9XXX_EVENT_POST_MICBIAS_1_OFF:
	case WCD9XXX_EVENT_POST_MICBIAS_2_OFF:
	case WCD9XXX_EVENT_POST_MICBIAS_3_OFF:
	case WCD9XXX_EVENT_POST_MICBIAS_4_OFF:
		if (mbhc->mbhc_cfg->micbias ==
		    wcd9xxx_event_to_micbias(event) &&
		    wcd9xxx_is_hph_pa_on(codec))
			wcd9xxx_switch_micbias(mbhc, 1);
		break;
	/* PA usage change */
	case WCD9XXX_EVENT_PRE_HPHL_PA_ON:
		if (!(snd_soc_read(codec, mbhc->mbhc_bias_regs.ctl_reg) & 0x80))
			/* if micbias is enabled, switch to vddio */
			wcd9xxx_switch_micbias(mbhc, 1);
		break;
	case WCD9XXX_EVENT_PRE_HPHR_PA_ON:
		/* Not used now */
		break;
	case WCD9XXX_EVENT_POST_HPHL_PA_OFF:
		/* if HPH PAs are off, report OCP and switch back to CFILT */
		clear_bit(WCD9XXX_HPHL_PA_OFF_ACK, &mbhc->hph_pa_dac_state);
		clear_bit(WCD9XXX_HPHL_DAC_OFF_ACK, &mbhc->hph_pa_dac_state);
		if (mbhc->hph_status & SND_JACK_OC_HPHL)
			hphlocp_off_report(mbhc, SND_JACK_OC_HPHL);
		wcd9xxx_switch_micbias(mbhc, 0);
		break;
	case WCD9XXX_EVENT_POST_HPHR_PA_OFF:
		/* if HPH PAs are off, report OCP and switch back to CFILT */
		clear_bit(WCD9XXX_HPHR_PA_OFF_ACK, &mbhc->hph_pa_dac_state);
		clear_bit(WCD9XXX_HPHR_DAC_OFF_ACK, &mbhc->hph_pa_dac_state);
		if (mbhc->hph_status & SND_JACK_OC_HPHR)
			hphrocp_off_report(mbhc, SND_JACK_OC_HPHL);
		wcd9xxx_switch_micbias(mbhc, 0);
		break;
	/* Clock usage change */
	case WCD9XXX_EVENT_PRE_MCLK_ON:
		break;
	case WCD9XXX_EVENT_POST_MCLK_ON:
		/* Change to lower TxAAF frequency */
		snd_soc_update_bits(codec, WCD9XXX_A_TX_COM_BIAS, 1 << 4,
				    1 << 4);
		/* Re-calibrate clock rate dependent values */
		wcd9xxx_update_mbhc_clk_rate(mbhc, mbhc->mbhc_cfg->mclk_rate);
		/* If clock source changes, stop and restart polling */
		if (wcd9xxx_mbhc_polling(mbhc)) {
			wcd9xxx_calibrate_hs_polling(mbhc);
			wcd9xxx_start_hs_polling(mbhc);
		}
		break;
	case WCD9XXX_EVENT_PRE_MCLK_OFF:
		/* If clock source changes, stop and restart polling */
		if (wcd9xxx_mbhc_polling(mbhc))
			wcd9xxx_pause_hs_polling(mbhc);
		break;
	case WCD9XXX_EVENT_POST_MCLK_OFF:
		break;
	case WCD9XXX_EVENT_PRE_RCO_ON:
		break;
	case WCD9XXX_EVENT_POST_RCO_ON:
		/* Change to higher TxAAF frequency */
		snd_soc_update_bits(codec, WCD9XXX_A_TX_COM_BIAS, 1 << 4,
				    0 << 4);
		/* Re-calibrate clock rate dependent values */
		wcd9xxx_update_mbhc_clk_rate(mbhc, WCD9XXX_RCO_CLK_RATE);
		/* If clock source changes, stop and restart polling */
		if (wcd9xxx_mbhc_polling(mbhc)) {
			wcd9xxx_calibrate_hs_polling(mbhc);
			wcd9xxx_start_hs_polling(mbhc);
		}
		break;
	case WCD9XXX_EVENT_PRE_RCO_OFF:
		/* If clock source changes, stop and restart polling */
		if (wcd9xxx_mbhc_polling(mbhc))
			wcd9xxx_pause_hs_polling(mbhc);
		break;
	case WCD9XXX_EVENT_POST_RCO_OFF:
		break;
	/* CFILT usage change */
	case WCD9XXX_EVENT_PRE_CFILT_1_ON:
	case WCD9XXX_EVENT_PRE_CFILT_2_ON:
	case WCD9XXX_EVENT_PRE_CFILT_3_ON:
		if (wcd9xxx_get_mbhc_cfilt_sel(mbhc) ==
		    wcd9xxx_event_to_cfilt(event))
			/*
			 * Switch CFILT to slow mode if MBHC CFILT is being
			 * used.
			 */
			wcd9xxx_codec_switch_cfilt_mode(mbhc, false);
		break;
	case WCD9XXX_EVENT_POST_CFILT_1_OFF:
	case WCD9XXX_EVENT_POST_CFILT_2_OFF:
	case WCD9XXX_EVENT_POST_CFILT_3_OFF:
		if (wcd9xxx_get_mbhc_cfilt_sel(mbhc) ==
		    wcd9xxx_event_to_cfilt(event))
			/*
			 * Switch CFILT to fast mode if MBHC CFILT is not
			 * used anymore.
			 */
			wcd9xxx_codec_switch_cfilt_mode(mbhc, true);
		break;
	/* System resume */
	case WCD9XXX_EVENT_POST_RESUME:
		mbhc->mbhc_last_resume = jiffies;
		break;
	/* BG mode chage */
	case WCD9XXX_EVENT_PRE_BG_OFF:
	case WCD9XXX_EVENT_POST_BG_OFF:
	case WCD9XXX_EVENT_PRE_BG_AUDIO_ON:
	case WCD9XXX_EVENT_POST_BG_AUDIO_ON:
	case WCD9XXX_EVENT_PRE_BG_MBHC_ON:
	case WCD9XXX_EVENT_POST_BG_MBHC_ON:
		/* Not used for now */
		break;
	default:
		WARN(1, "Unknown event %d\n", event);
		ret = -EINVAL;
	}

	pr_debug("%s: leave\n", __func__);

	return 0;
}

/*
 * wcd9xxx_mbhc_init : initialize MBHC internal structures.
 *
 * NOTE: mbhc->mbhc_cfg is not YET configure so shouldn't be used
 */
int wcd9xxx_mbhc_init(struct wcd9xxx_mbhc *mbhc, struct wcd9xxx_resmgr *resmgr,
		      struct snd_soc_codec *codec)
{
	int ret;
	void *core;

	pr_debug("%s: enter\n", __func__);
	memset(&mbhc->mbhc_bias_regs, 0, sizeof(struct mbhc_micbias_regs));
	memset(&mbhc->mbhc_data, 0, sizeof(struct mbhc_internal_cal_data));

	mbhc->mbhc_data.t_sta_dce = DEFAULT_DCE_STA_WAIT;
	mbhc->mbhc_data.t_dce = DEFAULT_DCE_WAIT;
	mbhc->mbhc_data.t_sta = DEFAULT_STA_WAIT;
	mbhc->mbhc_micbias_switched = false;
	mbhc->polling_active = false;
	mbhc->mbhc_state = MBHC_STATE_NONE;
	mbhc->in_swch_irq_handler = false;
	mbhc->current_plug = PLUG_TYPE_NONE;
	mbhc->lpi_enabled = false;
	mbhc->no_mic_headset_override = false;
	mbhc->mbhc_last_resume = 0;
	mbhc->codec = codec;
	mbhc->resmgr = resmgr;
	mbhc->resmgr->mbhc = mbhc;

	if (mbhc->headset_jack.jack == NULL) {
		ret = snd_soc_jack_new(codec, "Headset Jack", WCD9XXX_JACK_MASK,
				       &mbhc->headset_jack);
		if (ret) {
			pr_err("%s: Failed to create new jack\n", __func__);
			return ret;
		}

		ret = snd_soc_jack_new(codec, "Button Jack",
				       WCD9XXX_JACK_BUTTON_MASK,
				       &mbhc->button_jack);
		if (ret) {
			pr_err("Failed to create new jack\n");
			return ret;
		}

		INIT_DELAYED_WORK(&mbhc->mbhc_firmware_dwork,
				  wcd9xxx_mbhc_fw_read);
		INIT_DELAYED_WORK(&mbhc->mbhc_btn_dwork, wcd9xxx_btn_lpress_fn);
		INIT_DELAYED_WORK(&mbhc->mbhc_insert_dwork,
				  wcd9xxx_mbhc_insert_work);
	}

	/* Register event notifier */
	mbhc->nblock.notifier_call = wcd9xxx_event_notify;
	ret = wcd9xxx_resmgr_register_notifier(mbhc->resmgr, &mbhc->nblock);
	if (ret) {
		pr_err("%s: Failed to register notifier %d\n", __func__, ret);
		return ret;
	}

	wcd9xxx_init_debugfs(mbhc);

	core = mbhc->resmgr->core;
	ret = wcd9xxx_request_irq(core, WCD9XXX_IRQ_MBHC_INSERTION,
				  wcd9xxx_hs_insert_irq,
				  "Headset insert detect", mbhc);
	if (ret) {
		pr_err("%s: Failed to request irq %d\n", __func__,
		       WCD9XXX_IRQ_MBHC_INSERTION);
		goto err_insert_irq;
	}
	wcd9xxx_disable_irq(core, WCD9XXX_IRQ_MBHC_INSERTION);

	ret = wcd9xxx_request_irq(core, WCD9XXX_IRQ_MBHC_REMOVAL,
				  wcd9xxx_hs_remove_irq,
				  "Headset remove detect", mbhc);
	if (ret) {
		pr_err("%s: Failed to request irq %d\n", __func__,
			WCD9XXX_IRQ_MBHC_REMOVAL);
		goto err_remove_irq;
	}

	ret = wcd9xxx_request_irq(core, WCD9XXX_IRQ_MBHC_POTENTIAL,
				  wcd9xxx_dce_handler, "DC Estimation detect",
				  mbhc);
	if (ret) {
		pr_err("%s: Failed to request irq %d\n", __func__,
		       WCD9XXX_IRQ_MBHC_POTENTIAL);
		goto err_potential_irq;
	}

	ret = wcd9xxx_request_irq(core, WCD9XXX_IRQ_MBHC_RELEASE,
				  wcd9xxx_release_handler,
				  "Button Release detect", mbhc);
	if (ret) {
		pr_err("%s: Failed to request irq %d\n", __func__,
			WCD9XXX_IRQ_MBHC_RELEASE);
		goto err_release_irq;
	}

	ret = wcd9xxx_request_irq(core, WCD9XXX_IRQ_HPH_PA_OCPL_FAULT,
				  wcd9xxx_hphl_ocp_irq, "HPH_L OCP detect",
				  mbhc);
	if (ret) {
		pr_err("%s: Failed to request irq %d\n", __func__,
		       WCD9XXX_IRQ_HPH_PA_OCPL_FAULT);
		goto err_hphl_ocp_irq;
	}
	wcd9xxx_disable_irq(core, WCD9XXX_IRQ_HPH_PA_OCPL_FAULT);

	ret = wcd9xxx_request_irq(core, WCD9XXX_IRQ_HPH_PA_OCPR_FAULT,
				  wcd9xxx_hphr_ocp_irq, "HPH_R OCP detect",
				  mbhc);
	if (ret) {
		pr_err("%s: Failed to request irq %d\n", __func__,
		       WCD9XXX_IRQ_HPH_PA_OCPR_FAULT);
		goto err_hphr_ocp_irq;
	}
	wcd9xxx_disable_irq(codec->control_data, WCD9XXX_IRQ_HPH_PA_OCPR_FAULT);

	pr_debug("%s: leave ret %d\n", __func__, ret);
	return ret;

err_hphr_ocp_irq:
	wcd9xxx_free_irq(core, WCD9XXX_IRQ_HPH_PA_OCPL_FAULT, mbhc);
err_hphl_ocp_irq:
	wcd9xxx_free_irq(core, WCD9XXX_IRQ_MBHC_RELEASE, mbhc);
err_release_irq:
	wcd9xxx_free_irq(core, WCD9XXX_IRQ_MBHC_POTENTIAL, mbhc);
err_potential_irq:
	wcd9xxx_free_irq(core, WCD9XXX_IRQ_MBHC_REMOVAL, mbhc);
err_remove_irq:
	wcd9xxx_free_irq(core, WCD9XXX_IRQ_MBHC_INSERTION, mbhc);
err_insert_irq:
	wcd9xxx_resmgr_unregister_notifier(mbhc->resmgr, &mbhc->nblock);

	pr_debug("%s: leave ret %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(wcd9xxx_mbhc_init);

void wcd9xxx_mbhc_deinit(struct wcd9xxx_mbhc *mbhc)
{
	void *cdata = mbhc->codec->control_data;

	wcd9xxx_free_irq(cdata, WCD9XXX_IRQ_SLIMBUS, mbhc);
	wcd9xxx_free_irq(cdata, WCD9XXX_IRQ_MBHC_RELEASE, mbhc);
	wcd9xxx_free_irq(cdata, WCD9XXX_IRQ_MBHC_POTENTIAL, mbhc);
	wcd9xxx_free_irq(cdata, WCD9XXX_IRQ_MBHC_REMOVAL, mbhc);
	wcd9xxx_free_irq(cdata, WCD9XXX_IRQ_MBHC_INSERTION, mbhc);

	wcd9xxx_free_irq(cdata, WCD9XXX_IRQ_MBHC_JACK_SWITCH, mbhc);
	wcd9xxx_free_irq(cdata, WCD9XXX_IRQ_HPH_PA_OCPL_FAULT, mbhc);
	wcd9xxx_free_irq(cdata, WCD9XXX_IRQ_HPH_PA_OCPR_FAULT, mbhc);
	wcd9xxx_free_irq(cdata, WCD9XXX_IRQ_MBHC_RELEASE, mbhc);

	if (mbhc->mbhc_fw)
		release_firmware(mbhc->mbhc_fw);

	wcd9xxx_resmgr_unregister_notifier(mbhc->resmgr, &mbhc->nblock);

	wcd9xxx_cleanup_debugfs(mbhc);
}
EXPORT_SYMBOL_GPL(wcd9xxx_mbhc_deinit);

MODULE_DESCRIPTION("wcd9xxx MBHC module");
MODULE_LICENSE("GPL v2");
