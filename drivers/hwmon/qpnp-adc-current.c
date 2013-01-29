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

#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/spmi.h>
#include <linux/of_irq.h>
#include <linux/wakelock.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/hwmon-sysfs.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/platform_device.h>

/* QPNP IADC register definition */
#define QPNP_IADC_REVISION1				0x0
#define QPNP_IADC_REVISION2				0x1
#define QPNP_IADC_REVISION3				0x2
#define QPNP_IADC_REVISION4				0x3
#define QPNP_IADC_PERPH_TYPE				0x4
#define QPNP_IADC_PERH_SUBTYPE				0x5

#define QPNP_IADC_SUPPORTED_REVISION2			1

#define QPNP_STATUS1					0x8
#define QPNP_STATUS1_OP_MODE				4
#define QPNP_STATUS1_MULTI_MEAS_EN			BIT(3)
#define QPNP_STATUS1_MEAS_INTERVAL_EN_STS		BIT(2)
#define QPNP_STATUS1_REQ_STS				BIT(1)
#define QPNP_STATUS1_EOC				BIT(0)
#define QPNP_STATUS2					0x9
#define QPNP_STATUS2_CONV_SEQ_STATE_SHIFT		4
#define QPNP_STATUS2_FIFO_NOT_EMPTY_FLAG		BIT(1)
#define QPNP_STATUS2_CONV_SEQ_TIMEOUT_STS		BIT(0)
#define QPNP_CONV_TIMEOUT_ERR				2

#define QPNP_IADC_MODE_CTL				0x40
#define QPNP_OP_MODE_SHIFT				4
#define QPNP_USE_BMS_DATA				BIT(4)
#define QPNP_VADC_SYNCH_EN				BIT(2)
#define QPNP_OFFSET_RMV_EN				BIT(1)
#define QPNP_ADC_TRIM_EN				BIT(0)
#define QPNP_IADC_EN_CTL1				0x46
#define QPNP_IADC_ADC_EN				BIT(7)
#define QPNP_ADC_CH_SEL_CTL				0x48
#define QPNP_ADC_DIG_PARAM				0x50
#define QPNP_ADC_CLK_SEL_MASK				0x3
#define QPNP_ADC_DEC_RATIO_SEL_MASK			0xc
#define QPNP_ADC_DIG_DEC_RATIO_SEL_SHIFT		2

#define QPNP_HW_SETTLE_DELAY				0x51
#define QPNP_CONV_REQ					0x52
#define QPNP_CONV_REQ_SET				BIT(7)
#define QPNP_CONV_SEQ_CTL				0x54
#define QPNP_CONV_SEQ_HOLDOFF_SHIFT			4
#define QPNP_CONV_SEQ_TRIG_CTL				0x55
#define QPNP_FAST_AVG_CTL				0x5a

#define QPNP_M0_LOW_THR_LSB				0x5c
#define QPNP_M0_LOW_THR_MSB				0x5d
#define QPNP_M0_HIGH_THR_LSB				0x5e
#define QPNP_M0_HIGH_THR_MSB				0x5f
#define QPNP_M1_LOW_THR_LSB				0x69
#define QPNP_M1_LOW_THR_MSB				0x6a
#define QPNP_M1_HIGH_THR_LSB				0x6b
#define QPNP_M1_HIGH_THR_MSB				0x6c

#define QPNP_DATA0					0x60
#define QPNP_DATA1					0x61
#define QPNP_CONV_TIMEOUT_ERR				2

#define QPNP_IADC_SEC_ACCESS				0xD0
#define QPNP_IADC_SEC_ACCESS_DATA			0xA5
#define QPNP_IADC_MSB_OFFSET				0xF2
#define QPNP_IADC_LSB_OFFSET				0xF3
#define QPNP_IADC_NOMINAL_RSENSE			0xF4
#define QPNP_IADC_ATE_GAIN_CALIB_OFFSET			0xF5

#define QPNP_IADC_ADC_CH_SEL_CTL			0x48
#define QPNP_IADC_ADC_CHX_SEL_SHIFT			3

#define QPNP_IADC_ADC_DIG_PARAM				0x50
#define QPNP_IADC_CLK_SEL_SHIFT				1
#define QPNP_IADC_DEC_RATIO_SEL				3

#define QPNP_IADC_CONV_REQUEST				0x52
#define QPNP_IADC_CONV_REQ				BIT(7)

#define QPNP_IADC_DATA0					0x60
#define QPNP_IADC_DATA1					0x61

#define QPNP_ADC_CONV_TIME_MIN				8000
#define QPNP_ADC_CONV_TIME_MAX				8200

#define QPNP_ADC_GAIN_NV				17857
#define QPNP_OFFSET_CALIBRATION_SHORT_CADC_LEADS_IDEAL	0
#define QPNP_IADC_INTERNAL_RSENSE_N_OHMS_FACTOR		10000000
#define QPNP_IADC_NANO_VOLTS_FACTOR			1000000000
#define QPNP_IADC_CALIB_SECONDS				300000
#define QPNP_IADC_RSENSE_LSB_N_OHMS_PER_BIT		15625
#define QPNP_IADC_DIE_TEMP_CALIB_OFFSET			5000

#define QPNP_RAW_CODE_16_BIT_MSB_MASK			0xff00
#define QPNP_RAW_CODE_16_BIT_LSB_MASK			0xff
#define QPNP_BIT_SHIFT_8				8
#define QPNP_RSENSE_MSB_SIGN_CHECK			0x80
#define QPNP_ADC_COMPLETION_TIMEOUT			HZ

struct qpnp_iadc_drv {
	struct qpnp_adc_drv			*adc;
	int32_t					rsense;
	struct device				*iadc_hwmon;
	bool					iadc_init_calib;
	bool					iadc_initialized;
	int64_t					die_temp_calib_offset;
	struct delayed_work			iadc_work;
	struct sensor_device_attribute		sens_attr[0];
};

struct qpnp_iadc_drv	*qpnp_iadc;

static int32_t qpnp_iadc_read_reg(uint32_t reg, u8 *data)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	int rc;

	rc = spmi_ext_register_readl(iadc->adc->spmi->ctrl, iadc->adc->slave,
		(iadc->adc->offset + reg), data, 1);
	if (rc < 0) {
		pr_err("qpnp iadc read reg %d failed with %d\n", reg, rc);
		return rc;
	}

	return 0;
}

static int32_t qpnp_iadc_write_reg(uint32_t reg, u8 data)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	int rc;
	u8 *buf;

	buf = &data;
	rc = spmi_ext_register_writel(iadc->adc->spmi->ctrl, iadc->adc->slave,
		(iadc->adc->offset + reg), buf, 1);
	if (rc < 0) {
		pr_err("qpnp iadc write reg %d failed with %d\n", reg, rc);
		return rc;
	}

	return 0;
}

static void trigger_iadc_completion(struct work_struct *work)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;

	if (!iadc || !iadc->iadc_initialized)
		return;

	complete(&iadc->adc->adc_rslt_completion);

	return;
}
DECLARE_WORK(trigger_iadc_completion_work, trigger_iadc_completion);

static irqreturn_t qpnp_iadc_isr(int irq, void *dev_id)
{
	schedule_work(&trigger_iadc_completion_work);

	return IRQ_HANDLED;
}

static int32_t qpnp_iadc_enable(bool state)
{
	int rc = 0;
	u8 data = 0;

	data = QPNP_IADC_ADC_EN;
	if (state) {
		rc = qpnp_iadc_write_reg(QPNP_IADC_EN_CTL1,
					data);
		if (rc < 0) {
			pr_err("IADC enable failed\n");
			return rc;
		}
	} else {
		rc = qpnp_iadc_write_reg(QPNP_IADC_EN_CTL1,
					(~data & QPNP_IADC_ADC_EN));
		if (rc < 0) {
			pr_err("IADC disable failed\n");
			return rc;
		}
	}

	return 0;
}

static int32_t qpnp_iadc_status_debug(void)
{
	int rc = 0;
	u8 mode = 0, status1 = 0, chan = 0, dig = 0, en = 0;

	rc = qpnp_iadc_read_reg(QPNP_IADC_MODE_CTL, &mode);
	if (rc < 0) {
		pr_err("mode ctl register read failed with %d\n", rc);
		return rc;
	}

	rc = qpnp_iadc_read_reg(QPNP_ADC_DIG_PARAM, &dig);
	if (rc < 0) {
		pr_err("digital param read failed with %d\n", rc);
		return rc;
	}

	rc = qpnp_iadc_read_reg(QPNP_IADC_ADC_CH_SEL_CTL, &chan);
	if (rc < 0) {
		pr_err("channel read failed with %d\n", rc);
		return rc;
	}

	rc = qpnp_iadc_read_reg(QPNP_STATUS1, &status1);
	if (rc < 0) {
		pr_err("status1 read failed with %d\n", rc);
		return rc;
	}

	rc = qpnp_iadc_read_reg(QPNP_IADC_EN_CTL1, &en);
	if (rc < 0) {
		pr_err("en read failed with %d\n", rc);
		return rc;
	}

	pr_err("EOC not set with status:%x, dig:%x, ch:%x, mode:%x, en:%x\n",
			status1, dig, chan, mode, en);

	rc = qpnp_iadc_enable(false);
	if (rc < 0) {
		pr_err("IADC disable failed with %d\n", rc);
		return rc;
	}

	return 0;
}

static int32_t qpnp_iadc_read_conversion_result(uint16_t *data)
{
	uint8_t rslt_lsb, rslt_msb;
	uint16_t rslt;
	int32_t rc;

	rc = qpnp_iadc_read_reg(QPNP_IADC_DATA0, &rslt_lsb);
	if (rc < 0) {
		pr_err("qpnp adc result read failed with %d\n", rc);
		return rc;
	}

	rc = qpnp_iadc_read_reg(QPNP_IADC_DATA1, &rslt_msb);
	if (rc < 0) {
		pr_err("qpnp adc result read failed with %d\n", rc);
		return rc;
	}

	rslt = (rslt_msb << 8) | rslt_lsb;
	*data = rslt;

	rc = qpnp_iadc_enable(false);
	if (rc)
		return rc;

	return 0;
}

static int32_t qpnp_iadc_configure(enum qpnp_iadc_channels channel,
						uint16_t *raw_code)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	u8 qpnp_iadc_mode_reg = 0, qpnp_iadc_ch_sel_reg = 0;
	u8 qpnp_iadc_conv_req = 0, qpnp_iadc_dig_param_reg = 0;
	int32_t rc = 0;

	qpnp_iadc_ch_sel_reg = channel;

	qpnp_iadc_dig_param_reg |= iadc->adc->amux_prop->decimation <<
					QPNP_IADC_DEC_RATIO_SEL;
	qpnp_iadc_mode_reg |= QPNP_ADC_TRIM_EN;
	qpnp_iadc_conv_req = QPNP_IADC_CONV_REQ;

	rc = qpnp_iadc_write_reg(QPNP_IADC_MODE_CTL, qpnp_iadc_mode_reg);
	if (rc) {
		pr_err("qpnp adc read adc failed with %d\n", rc);
		return rc;
	}

	rc = qpnp_iadc_write_reg(QPNP_IADC_ADC_CH_SEL_CTL,
						qpnp_iadc_ch_sel_reg);
	if (rc) {
		pr_err("qpnp adc read adc failed with %d\n", rc);
		return rc;
	}

	rc = qpnp_iadc_write_reg(QPNP_ADC_DIG_PARAM,
						qpnp_iadc_dig_param_reg);
	if (rc) {
		pr_err("qpnp adc read adc failed with %d\n", rc);
		return rc;
	}

	rc = qpnp_iadc_write_reg(QPNP_HW_SETTLE_DELAY,
				iadc->adc->amux_prop->hw_settle_time);
	if (rc < 0) {
		pr_err("qpnp adc configure error for hw settling time setup\n");
		return rc;
	}

	rc = qpnp_iadc_write_reg(QPNP_FAST_AVG_CTL,
					iadc->adc->amux_prop->fast_avg_setup);
	if (rc < 0) {
		pr_err("qpnp adc fast averaging configure error\n");
		return rc;
	}

	rc = qpnp_iadc_enable(true);
	if (rc)
		return rc;

	rc = qpnp_iadc_write_reg(QPNP_CONV_REQ, qpnp_iadc_conv_req);
	if (rc) {
		pr_err("qpnp adc read adc failed with %d\n", rc);
		return rc;
	}

	rc = wait_for_completion_timeout(&iadc->adc->adc_rslt_completion,
				QPNP_ADC_COMPLETION_TIMEOUT);
	if (!rc) {
		u8 status1 = 0;
		rc = qpnp_iadc_read_reg(QPNP_STATUS1, &status1);
		if (rc < 0)
			return rc;
		status1 &= (QPNP_STATUS1_REQ_STS | QPNP_STATUS1_EOC);
		if (status1 == QPNP_STATUS1_EOC)
			pr_debug("End of conversion status set\n");
		else {
			rc = qpnp_iadc_status_debug();
			if (rc < 0) {
				pr_err("status1 read failed with %d\n", rc);
				return rc;
			}
			return -EINVAL;
		}
	}

	rc = qpnp_iadc_read_conversion_result(raw_code);
	if (rc) {
		pr_err("qpnp adc read adc failed with %d\n", rc);
		return rc;
	}

	return 0;
}

static int32_t qpnp_convert_raw_offset_voltage(void)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	uint32_t num = 0;

	num = iadc->adc->calib.offset_raw - iadc->adc->calib.offset_raw;

	iadc->adc->calib.offset_uv = (num * QPNP_ADC_GAIN_NV)/
		(iadc->adc->calib.gain_raw - iadc->adc->calib.offset_raw);

	num = iadc->adc->calib.gain_raw - iadc->adc->calib.offset_raw;

	iadc->adc->calib.gain_uv = (num * QPNP_ADC_GAIN_NV)/
		(iadc->adc->calib.gain_raw - iadc->adc->calib.offset_raw);

	return 0;
}

static int32_t qpnp_iadc_calibrate_for_trim(void)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	uint8_t rslt_lsb, rslt_msb;
	int32_t rc = 0;
	uint16_t raw_data;

	rc = qpnp_iadc_configure(GAIN_CALIBRATION_17P857MV, &raw_data);
	if (rc < 0) {
		pr_err("qpnp adc result read failed with %d\n", rc);
		goto fail;
	}

	iadc->adc->calib.gain_raw = raw_data;

	rc = qpnp_iadc_configure(OFFSET_CALIBRATION_SHORT_CADC_LEADS,
								&raw_data);
	if (rc < 0) {
		pr_err("qpnp adc result read failed with %d\n", rc);
		goto fail;
	}

	iadc->adc->calib.offset_raw = raw_data;
	if (rc < 0) {
		pr_err("qpnp adc offset/gain calculation failed\n");
		goto fail;
	}

	rc = qpnp_convert_raw_offset_voltage();

	rslt_msb = (raw_data & QPNP_RAW_CODE_16_BIT_MSB_MASK) >>
							QPNP_BIT_SHIFT_8;
	rslt_lsb = raw_data & QPNP_RAW_CODE_16_BIT_LSB_MASK;

	rc = qpnp_iadc_write_reg(QPNP_IADC_SEC_ACCESS,
					QPNP_IADC_SEC_ACCESS_DATA);
	if (rc < 0) {
		pr_err("qpnp iadc configure error for sec access\n");
		goto fail;
	}

	rc = qpnp_iadc_write_reg(QPNP_IADC_MSB_OFFSET,
						rslt_msb);
	if (rc < 0) {
		pr_err("qpnp iadc configure error for MSB write\n");
		goto fail;
	}

	rc = qpnp_iadc_write_reg(QPNP_IADC_SEC_ACCESS,
					QPNP_IADC_SEC_ACCESS_DATA);
	if (rc < 0) {
		pr_err("qpnp iadc configure error for sec access\n");
		goto fail;
	}

	rc = qpnp_iadc_write_reg(QPNP_IADC_LSB_OFFSET,
						rslt_lsb);
	if (rc < 0) {
		pr_err("qpnp iadc configure error for LSB write\n");
		goto fail;
	}
fail:
	return rc;
}

static void qpnp_iadc_work(struct work_struct *work)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	int rc = 0;

	mutex_lock(&iadc->adc->adc_lock);

	rc = qpnp_iadc_calibrate_for_trim();
	if (rc)
		pr_err("periodic IADC calibration failed\n");

	mutex_unlock(&iadc->adc->adc_lock);

	schedule_delayed_work(&iadc->iadc_work,
			round_jiffies_relative(msecs_to_jiffies
					(QPNP_IADC_CALIB_SECONDS)));

	return;
}

static int32_t qpnp_iadc_version_check(void)
{
	uint8_t revision;
	int rc;

	rc = qpnp_iadc_read_reg(QPNP_IADC_REVISION2, &revision);
	if (rc < 0) {
		pr_err("qpnp adc result read failed with %d\n", rc);
		return rc;
	}

	if (revision < QPNP_IADC_SUPPORTED_REVISION2) {
		pr_err("IADC Version not supported\n");
		return -EINVAL;
	}

	return 0;
}

int32_t qpnp_iadc_is_ready(void)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;

	if (!iadc || !iadc->iadc_initialized)
		return -EPROBE_DEFER;
	else
		return 0;
}
EXPORT_SYMBOL(qpnp_iadc_is_ready);

int32_t qpnp_iadc_get_rsense(int32_t *rsense)
{
	uint8_t	rslt_rsense;
	int32_t	rc, sign_bit = 0;

	rc = qpnp_iadc_read_reg(QPNP_IADC_NOMINAL_RSENSE, &rslt_rsense);
	if (rc < 0) {
		pr_err("qpnp adc rsense read failed with %d\n", rc);
		return rc;
	}

	if (rslt_rsense & QPNP_RSENSE_MSB_SIGN_CHECK)
		sign_bit = 1;

	rslt_rsense &= ~QPNP_RSENSE_MSB_SIGN_CHECK;

	if (sign_bit)
		*rsense = QPNP_IADC_INTERNAL_RSENSE_N_OHMS_FACTOR -
			(rslt_rsense * QPNP_IADC_RSENSE_LSB_N_OHMS_PER_BIT);
	else
		*rsense = QPNP_IADC_INTERNAL_RSENSE_N_OHMS_FACTOR +
			(rslt_rsense * QPNP_IADC_RSENSE_LSB_N_OHMS_PER_BIT);

	return rc;
}
EXPORT_SYMBOL(qpnp_iadc_get_rsense);

int32_t qpnp_check_pmic_temp(void)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	struct qpnp_vadc_result result_pmic_therm;
	int rc;

	rc = qpnp_vadc_read(DIE_TEMP, &result_pmic_therm);
	if (rc < 0)
		return rc;

	if (((uint64_t) (result_pmic_therm.physical -
				iadc->die_temp_calib_offset))
			> QPNP_IADC_DIE_TEMP_CALIB_OFFSET) {
		mutex_lock(&iadc->adc->adc_lock);

		rc = qpnp_iadc_calibrate_for_trim();
		if (rc)
			pr_err("periodic IADC calibration failed\n");

		mutex_unlock(&iadc->adc->adc_lock);
	}

	return 0;
}

int32_t qpnp_iadc_read(enum qpnp_iadc_channels channel,
				struct qpnp_iadc_result *result)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	int32_t rc, rsense_n_ohms, sign = 0, num;
	int64_t result_current;
	uint16_t raw_data;

	if (!iadc || !iadc->iadc_initialized)
		return -EPROBE_DEFER;

	rc = qpnp_check_pmic_temp();
	if (rc) {
		pr_err("Error checking pmic therm temp\n");
		return rc;
	}

	mutex_lock(&iadc->adc->adc_lock);

	rc = qpnp_iadc_configure(channel, &raw_data);
	if (rc < 0) {
		pr_err("qpnp adc result read failed with %d\n", rc);
		goto fail;
	}

	rc = qpnp_iadc_get_rsense(&rsense_n_ohms);

	num = raw_data - iadc->adc->calib.offset_raw;
	if (num < 0) {
		sign = 1;
		num = -num;
	}

	result->result_uv = (num * QPNP_ADC_GAIN_NV)/
		(iadc->adc->calib.gain_raw - iadc->adc->calib.offset_raw);
	result_current = result->result_uv;
	result_current *= QPNP_IADC_NANO_VOLTS_FACTOR;
	do_div(result_current, rsense_n_ohms);

	if (sign) {
		result->result_uv = -result->result_uv;
		result_current = -result_current;
	}

	result->result_ua = (int32_t) result_current;
fail:
	mutex_unlock(&iadc->adc->adc_lock);

	return rc;
}
EXPORT_SYMBOL(qpnp_iadc_read);

int32_t qpnp_iadc_get_gain_and_offset(struct qpnp_iadc_calib *result)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	int rc;

	if (!iadc || !iadc->iadc_initialized)
		return -EPROBE_DEFER;

	rc = qpnp_check_pmic_temp();
	if (rc) {
		pr_err("Error checking pmic therm temp\n");
		return rc;
	}

	mutex_lock(&iadc->adc->adc_lock);
	result->gain_raw = iadc->adc->calib.gain_raw;
	result->ideal_gain_nv = QPNP_ADC_GAIN_NV;
	result->gain_uv = iadc->adc->calib.gain_uv;
	result->offset_raw = iadc->adc->calib.offset_raw;
	result->ideal_offset_uv =
				QPNP_OFFSET_CALIBRATION_SHORT_CADC_LEADS_IDEAL;
	result->offset_uv = iadc->adc->calib.offset_uv;
	mutex_unlock(&iadc->adc->adc_lock);

	return 0;
}
EXPORT_SYMBOL(qpnp_iadc_get_gain_and_offset);

static ssize_t qpnp_iadc_show(struct device *dev,
			struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct qpnp_iadc_result result;
	int rc = -1;

	rc = qpnp_iadc_read(attr->index, &result);

	if (rc)
		return 0;

	return snprintf(buf, QPNP_ADC_HWMON_NAME_LENGTH,
					"Result:%d\n", result.result_ua);
}

static struct sensor_device_attribute qpnp_adc_attr =
	SENSOR_ATTR(NULL, S_IRUGO, qpnp_iadc_show, NULL, 0);

static int32_t qpnp_iadc_init_hwmon(struct spmi_device *spmi)
{
	struct qpnp_iadc_drv *iadc = qpnp_iadc;
	struct device_node *child;
	struct device_node *node = spmi->dev.of_node;
	int rc = 0, i = 0, channel;

	for_each_child_of_node(node, child) {
		channel = iadc->adc->adc_channels[i].channel_num;
		qpnp_adc_attr.index = iadc->adc->adc_channels[i].channel_num;
		qpnp_adc_attr.dev_attr.attr.name =
						iadc->adc->adc_channels[i].name;
		memcpy(&iadc->sens_attr[i], &qpnp_adc_attr,
						sizeof(qpnp_adc_attr));
		sysfs_attr_init(&iadc->sens_attr[i].dev_attr.attr);
		rc = device_create_file(&spmi->dev,
				&iadc->sens_attr[i].dev_attr);
		if (rc) {
			dev_err(&spmi->dev,
				"device_create_file failed for dev %s\n",
				iadc->adc->adc_channels[i].name);
			goto hwmon_err_sens;
		}
		i++;
	}

	return 0;
hwmon_err_sens:
	pr_err("Init HWMON failed for qpnp_iadc with %d\n", rc);
	return rc;
}

static int __devinit qpnp_iadc_probe(struct spmi_device *spmi)
{
	struct qpnp_iadc_drv *iadc;
	struct qpnp_adc_drv *adc_qpnp;
	struct device_node *node = spmi->dev.of_node;
	struct device_node *child;
	int rc, count_adc_channel_list = 0;

	if (!node)
		return -EINVAL;

	if (qpnp_iadc) {
		pr_err("IADC already in use\n");
		return -EBUSY;
	}

	for_each_child_of_node(node, child)
		count_adc_channel_list++;

	if (!count_adc_channel_list) {
		pr_err("No channel listing\n");
		return -EINVAL;
	}

	iadc = devm_kzalloc(&spmi->dev, sizeof(struct qpnp_iadc_drv) +
		(sizeof(struct sensor_device_attribute) *
				count_adc_channel_list), GFP_KERNEL);
	if (!iadc) {
		dev_err(&spmi->dev, "Unable to allocate memory\n");
		return -ENOMEM;
	}

	adc_qpnp = devm_kzalloc(&spmi->dev, sizeof(struct qpnp_adc_drv),
			GFP_KERNEL);
	if (!adc_qpnp) {
		dev_err(&spmi->dev, "Unable to allocate memory\n");
		rc = -ENOMEM;
		goto fail;
	}

	iadc->adc = adc_qpnp;

	rc = qpnp_adc_get_devicetree_data(spmi, iadc->adc);
	if (rc) {
		dev_err(&spmi->dev, "failed to read device tree\n");
		goto fail;
	}

	rc = of_property_read_u32(node, "qcom,rsense",
			&iadc->rsense);
	if (rc) {
		pr_err("Invalid rsens reference property\n");
		goto fail;
	}

	rc = devm_request_irq(&spmi->dev, iadc->adc->adc_irq_eoc,
				qpnp_iadc_isr,
	IRQF_TRIGGER_RISING, "qpnp_iadc_interrupt", iadc);
	if (rc) {
		dev_err(&spmi->dev, "failed to request adc irq\n");
		goto fail;
	} else
		enable_irq_wake(iadc->adc->adc_irq_eoc);

	iadc->iadc_init_calib = false;
	dev_set_drvdata(&spmi->dev, iadc);
	qpnp_iadc = iadc;

	rc = qpnp_iadc_init_hwmon(spmi);
	if (rc) {
		dev_err(&spmi->dev, "failed to initialize qpnp hwmon adc\n");
		goto fail;
	}
	iadc->iadc_hwmon = hwmon_device_register(&iadc->adc->spmi->dev);

	rc = qpnp_iadc_version_check();
	if (rc) {
		dev_err(&spmi->dev, "IADC version not supported\n");
		goto fail;
	}

	rc = qpnp_iadc_calibrate_for_trim();
	if (rc) {
		dev_err(&spmi->dev, "failed to calibrate for USR trim\n");
		goto fail;
	}
	iadc->iadc_init_calib = true;
	INIT_DELAYED_WORK(&iadc->iadc_work, qpnp_iadc_work);
	schedule_delayed_work(&iadc->iadc_work,
			round_jiffies_relative(msecs_to_jiffies
					(QPNP_IADC_CALIB_SECONDS)));
	iadc->iadc_initialized = true;

	return 0;
fail:
	qpnp_iadc = NULL;
	return rc;
}

static int __devexit qpnp_iadc_remove(struct spmi_device *spmi)
{
	struct qpnp_iadc_drv *iadc = dev_get_drvdata(&spmi->dev);
	struct device_node *node = spmi->dev.of_node;
	struct device_node *child;
	int i = 0;

	for_each_child_of_node(node, child) {
		device_remove_file(&spmi->dev,
			&iadc->sens_attr[i].dev_attr);
		i++;
	}
	dev_set_drvdata(&spmi->dev, NULL);

	return 0;
}

static const struct of_device_id qpnp_iadc_match_table[] = {
	{	.compatible = "qcom,qpnp-iadc",
	},
	{}
};

static struct spmi_driver qpnp_iadc_driver = {
	.driver		= {
		.name	= "qcom,qpnp-iadc",
		.of_match_table = qpnp_iadc_match_table,
	},
	.probe		= qpnp_iadc_probe,
	.remove		= qpnp_iadc_remove,
};

static int __init qpnp_iadc_init(void)
{
	return spmi_driver_register(&qpnp_iadc_driver);
}
module_init(qpnp_iadc_init);

static void __exit qpnp_iadc_exit(void)
{
	spmi_driver_unregister(&qpnp_iadc_driver);
}
module_exit(qpnp_iadc_exit);

MODULE_DESCRIPTION("QPNP PMIC current ADC driver");
MODULE_LICENSE("GPL v2");
