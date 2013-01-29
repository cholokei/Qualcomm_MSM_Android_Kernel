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
 */

#define pr_fmt(fmt)	"BMS: %s: " fmt, __func__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/power_supply.h>
#include <linux/spmi.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/qpnp/qpnp-adc.h>
#include <linux/mfd/pm8xxx/batterydata-lib.h>

/* BMS Register Offsets */
#define BMS1_REVISION1			0x0
#define BMS1_REVISION2			0x1
#define BMS1_STATUS1			0x8
#define BMS1_MODE_CTL			0X40
/* Coulomb counter clear registers */
#define BMS1_CC_DATA_CTL		0x42
#define BMS1_CC_CLEAR_CTL		0x43
/* OCV limit registers */
#define BMS1_OCV_USE_LOW_LIMIT_THR0	0x48
#define BMS1_OCV_USE_LOW_LIMIT_THR1	0x49
#define BMS1_OCV_USE_HIGH_LIMIT_THR0	0x4A
#define BMS1_OCV_USE_HIGH_LIMIT_THR1	0x4B
#define BMS1_OCV_USE_LIMIT_CTL		0x4C
/* Delay control */
#define BMS1_S1_DELAY_CTL		0x5A
/* CC interrupt threshold */
#define BMS1_CC_THR0			0x7A
#define BMS1_CC_THR1			0x7B
#define BMS1_CC_THR2			0x7C
#define BMS1_CC_THR3			0x7D
#define BMS1_CC_THR4			0x7E
/* OCV for r registers */
#define BMS1_OCV_FOR_R_DATA0		0x80
#define BMS1_OCV_FOR_R_DATA1		0x81
#define BMS1_VSENSE_FOR_R_DATA0		0x82
#define BMS1_VSENSE_FOR_R_DATA1		0x83
/* Coulomb counter data */
#define BMS1_CC_DATA0			0x8A
#define BMS1_CC_DATA1			0x8B
#define BMS1_CC_DATA2			0x8C
#define BMS1_CC_DATA3			0x8D
#define BMS1_CC_DATA4			0x8E
/* OCV for soc data */
#define BMS1_OCV_FOR_SOC_DATA0		0x90
#define BMS1_OCV_FOR_SOC_DATA1		0x91
#define BMS1_VSENSE_PON_DATA0		0x94
#define BMS1_VSENSE_PON_DATA1		0x95
#define BMS1_VSENSE_AVG_DATA0		0x98
#define BMS1_VSENSE_AVG_DATA1		0x99
#define BMS1_VBAT_AVG_DATA0		0x9E
#define BMS1_VBAT_AVG_DATA1		0x9F
/* Extra bms registers */
#define BMS1_BMS_DATA_REG_0		0xB0
#define IAVG_STORAGE_REG		0xB1
#define SOC_STORAGE_REG			0xB2
#define BMS1_BMS_DATA_REG_3		0xB3
/* IADC Channel Select */
#define IADC1_BMS_ADC_CH_SEL_CTL	0x48

/* Configuration for saving of shutdown soc/iavg */
#define IGNORE_SOC_TEMP_DECIDEG		50
#define IAVG_STEP_SIZE_MA		50
#define IAVG_START			600
#define SOC_ZERO			0xFF

#define IAVG_SAMPLES 16

#define QPNP_BMS_DEV_NAME "qcom,qpnp-bms"

struct soc_params {
	int		fcc_uah;
	int		cc_uah;
	int		rbatt_mohm;
	int		iavg_ua;
	int		uuc_uah;
	int		ocv_charge_uah;
	int		delta_time_s;
};

struct raw_soc_params {
	uint16_t	last_good_ocv_raw;
	int64_t		cc;
	int		last_good_ocv_uv;
};

struct qpnp_bms_chip {
	struct device			*dev;
	struct power_supply		bms_psy;
	struct power_supply		*batt_psy;
	struct spmi_device		*spmi;
	u16				base;
	u16				iadc_base;

	u8				revision1;
	u8				revision2;
	int				charger_status;
	bool				online;
	/* platform data */
	unsigned int			r_sense_mohm;
	unsigned int			v_cutoff_uv;
	unsigned int			max_voltage_uv;
	unsigned int			r_conn_mohm;
	int				shutdown_soc_valid_limit;
	int				adjust_soc_low_threshold;
	int				adjust_soc_high_threshold;
	int				chg_term_ua;
	enum battery_type		batt_type;
	unsigned int			fcc;
	struct single_row_lut		*fcc_temp_lut;
	struct single_row_lut		*fcc_sf_lut;
	struct pc_temp_ocv_lut		*pc_temp_ocv_lut;
	struct sf_lut			*pc_sf_lut;
	struct sf_lut			*rbatt_sf_lut;
	int				default_rbatt_mohm;

	struct delayed_work		calculate_soc_delayed_work;

	struct mutex			bms_output_lock;
	struct mutex			last_ocv_uv_mutex;
	struct mutex			soc_invalidation_mutex;

	bool				use_external_rsense;

	bool				ignore_shutdown_soc;
	int				shutdown_soc_invalid;
	int				shutdown_soc;
	int				shutdown_iavg_ma;

	int				low_soc_calc_threshold;
	int				low_soc_calculate_soc_ms;
	int				calculate_soc_ms;

	uint16_t			ocv_reading_at_100;
	int64_t				cc_reading_at_100;
	uint16_t			prev_last_good_ocv_raw;
	int				last_ocv_uv;
	int				last_cc_uah;
	unsigned long			tm_sec;
	bool				first_time_calc_soc;
	bool				first_time_calc_uuc;
	int				pon_ocv_uv;

	int				iavg_samples_ma[IAVG_SAMPLES];
	int				iavg_index;
	int				iavg_num_samples;
	struct timespec			t_soc_queried;
	int				last_soc;
	int				last_soc_est;

	int				charge_time_us;
	int				catch_up_time_us;
	struct single_row_lut		*adjusted_fcc_temp_lut;

	unsigned int			vadc_v0625;
	unsigned int			vadc_v1250;

	int				ibat_max_ua;
	int				prev_iavg_ua;
	int				prev_uuc_iavg_ma;
	int				prev_pc_unusable;
	int				ibat_at_cv_ua;
	int				soc_at_cv;
	int				prev_chg_soc;
	int				calculated_soc;
	int				prev_voltage_based_soc;
	bool				use_voltage_soc;
};

static struct of_device_id qpnp_bms_match_table[] = {
	{ .compatible = QPNP_BMS_DEV_NAME },
	{}
};

static char *qpnp_bms_supplicants[] = {
	"battery"
};

static enum power_supply_property msm_bms_power_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
};


static int qpnp_read_wrapper(struct qpnp_bms_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc;
	struct spmi_device *spmi = chip->spmi;

	rc = spmi_ext_register_readl(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("SPMI read failed rc=%d\n", rc);
		return rc;
	}
	return 0;
}

static int qpnp_write_wrapper(struct qpnp_bms_chip *chip, u8 *val,
			u16 base, int count)
{
	int rc;
	struct spmi_device *spmi = chip->spmi;

	rc = spmi_ext_register_writel(spmi->ctrl, spmi->sid, base, val, count);
	if (rc) {
		pr_err("SPMI write failed rc=%d\n", rc);
		return rc;
	}
	return 0;
}

static int qpnp_masked_write_base(struct qpnp_bms_chip *chip, u16 addr,
							u8 mask, u8 val)
{
	int rc;
	u8 reg;

	rc = qpnp_read_wrapper(chip, &reg, addr, 1);
	if (rc) {
		pr_err("read failed addr = %03X, rc = %d\n", addr, rc);
		return rc;
	}
	reg &= ~mask;
	reg |= val & mask;
	rc = qpnp_write_wrapper(chip, &reg, addr, 1);
	if (rc) {
		pr_err("write failed addr = %03X, val = %02x, mask = %02x, reg = %02x, rc = %d\n",
					addr, val, mask, reg, rc);
		return rc;
	}
	return 0;
}

static int qpnp_masked_write_iadc(struct qpnp_bms_chip *chip, u16 addr,
							u8 mask, u8 val)
{
	return qpnp_masked_write_base(chip, chip->iadc_base + addr, mask, val);
}

static int qpnp_masked_write(struct qpnp_bms_chip *chip, u16 addr,
							u8 mask, u8 val)
{
	return qpnp_masked_write_base(chip, chip->base + addr, mask, val);
}

#define HOLD_OREG_DATA		BIT(0)
static int lock_output_data(struct qpnp_bms_chip *chip)
{
	int rc;

	rc = qpnp_masked_write(chip, BMS1_CC_DATA_CTL,
				HOLD_OREG_DATA, HOLD_OREG_DATA);
	if (rc) {
		pr_err("couldnt lock bms output rc = %d\n", rc);
		return rc;
	}
	return 0;
}

static int unlock_output_data(struct qpnp_bms_chip *chip)
{
	int rc;

	rc = qpnp_masked_write(chip, BMS1_CC_DATA_CTL, HOLD_OREG_DATA, 0);
	if (rc) {
		pr_err("fail to unlock BMS_CONTROL rc = %d\n", rc);
		return rc;
	}
	return 0;
}

#define V_PER_BIT_MUL_FACTOR	97656
#define V_PER_BIT_DIV_FACTOR	1000
#define VADC_INTRINSIC_OFFSET	0x6000

static int vadc_reading_to_uv(unsigned int reading)
{
	if (reading <= VADC_INTRINSIC_OFFSET)
		return 0;

	return (reading - VADC_INTRINSIC_OFFSET)
			* V_PER_BIT_MUL_FACTOR / V_PER_BIT_DIV_FACTOR;
}

#define VADC_CALIB_UV		625000
#define VBATT_MUL_FACTOR	3

static int adjust_vbatt_reading(struct qpnp_bms_chip *chip,
						unsigned int reading_uv)
{
	s64 numerator, denominator;

	if (reading_uv == 0)
		return 0;

	/* don't adjust if not calibrated */
	if (chip->vadc_v0625 == 0 || chip->vadc_v1250 == 0) {
		pr_debug("No cal yet return %d\n",
				VBATT_MUL_FACTOR * reading_uv);
		return VBATT_MUL_FACTOR * reading_uv;
	}

	numerator = ((s64)reading_uv - chip->vadc_v0625) * VADC_CALIB_UV;
	denominator =  (s64)chip->vadc_v1250 - chip->vadc_v0625;
	if (denominator == 0)
		return reading_uv * VBATT_MUL_FACTOR;
	return (VADC_CALIB_UV + div_s64(numerator, denominator))
						* VBATT_MUL_FACTOR;
}

static inline int convert_vbatt_raw_to_uv(struct qpnp_bms_chip *chip,
					uint16_t reading)
{
	int uv;

	uv = vadc_reading_to_uv(reading);
	pr_debug("%u raw converted into %d uv\n", reading, uv);
	uv = adjust_vbatt_reading(chip, uv);
	pr_debug("adjusted into %d uv\n", uv);
	return uv;
}

#define CC_READING_RESOLUTION_N	542535
#define CC_READING_RESOLUTION_D	100000
static int cc_reading_to_uv(int16_t reading)
{
	return div_s64(reading * CC_READING_RESOLUTION_N,
					CC_READING_RESOLUTION_D);
}

#define QPNP_ADC_GAIN_NV				17857LL
static s64 cc_adjust_for_gain(s64 uv, uint16_t gain)
{
	s64 result_uv;

	pr_debug("adjusting_uv = %lld\n", uv);
	if (gain == 0) {
		pr_debug("gain is %d, not adjusting\n", gain);
		return uv;
	}
	pr_debug("adjusting by factor: %lld/%hu = %lld%%\n",
			QPNP_ADC_GAIN_NV, gain,
			div_s64(QPNP_ADC_GAIN_NV * 100LL, (s64)gain));

	result_uv = div_s64(uv * QPNP_ADC_GAIN_NV, (s64)gain);
	pr_debug("result_uv = %lld\n", result_uv);
	return result_uv;
}

static int convert_vsense_to_uv(struct qpnp_bms_chip *chip,
					int16_t reading)
{
	struct qpnp_iadc_calib calibration;

	qpnp_iadc_get_gain_and_offset(&calibration);
	return cc_adjust_for_gain(cc_reading_to_uv(reading),
			calibration.gain_raw);
}

static int read_vsense_avg(struct qpnp_bms_chip *chip, int *result_uv)
{
	int rc;
	int16_t reading;

	rc = qpnp_read_wrapper(chip, (u8 *)&reading,
			chip->base + BMS1_VSENSE_AVG_DATA0, 2);

	if (rc) {
		pr_err("fail to read VSENSE_AVG rc = %d\n", rc);
		return rc;
	}

	*result_uv = convert_vsense_to_uv(chip, reading);
	return 0;
}

static int get_battery_current(struct qpnp_bms_chip *chip, int *result_ua)
{
	int vsense_uv = 0;

	if (chip->r_sense_mohm == 0) {
		pr_err("r_sense is zero\n");
		return -EINVAL;
	}

	mutex_lock(&chip->bms_output_lock);
	lock_output_data(chip);
	read_vsense_avg(chip, &vsense_uv);
	unlock_output_data(chip);
	mutex_unlock(&chip->bms_output_lock);

	pr_debug("vsense_uv=%duV\n", vsense_uv);
	/* cast for signed division */
	*result_ua = vsense_uv * 1000 / (int)chip->r_sense_mohm;
	pr_debug("ibat=%duA\n", *result_ua);
	return 0;
}

static int get_battery_voltage(int *result_uv)
{
	int rc;
	struct qpnp_vadc_result adc_result;

	rc = qpnp_vadc_read(VBAT_SNS, &adc_result);
	if (rc) {
		pr_err("error reading adc channel = %d, rc = %d\n",
					VBAT_SNS, rc);
		return rc;
	}
	pr_debug("mvolts phy = %lld meas = 0x%llx\n", adc_result.physical,
						adc_result.measurement);
	*result_uv = (int)adc_result.physical;
	return 0;
}

#define CC_36_BIT_MASK 0xFFFFFFFFFLL

static int read_cc_raw(struct qpnp_bms_chip *chip, int64_t *reading)
{
	int64_t raw_reading;
	int rc;

	rc = qpnp_read_wrapper(chip, (u8 *)&raw_reading,
			chip->base + BMS1_CC_DATA0, 5);
	if (rc) {
		pr_err("Error reading cc: rc = %d\n", rc);
		return -ENXIO;
	}

	raw_reading = raw_reading & CC_36_BIT_MASK;
	/* convert 36 bit signed value into 64 signed value */
	*reading = (raw_reading >> 35) == 0LL ?
		raw_reading : ((-1LL ^ CC_36_BIT_MASK) | raw_reading);
	pr_debug("before conversion: %llx, after conversion: %llx\n",
			raw_reading, *reading);

	return 0;
}

static int calib_vadc(struct qpnp_bms_chip *chip)
{
	int rc;
	struct qpnp_vadc_result result;

	rc = qpnp_vadc_read(REF_625MV, &result);
	if (rc) {
		pr_debug("vadc read failed with rc = %d\n", rc);
		return rc;
	}
	chip->vadc_v0625 = result.physical;

	rc = qpnp_vadc_read(REF_125V, &result);
	if (rc) {
		pr_debug("vadc read failed with rc = %d\n", rc);
		return rc;
	}
	chip->vadc_v1250 = result.physical;
	pr_debug("vadc calib: 0625 = %d, 1250 = %d\n",
			chip->vadc_v0625, chip->vadc_v1250);
	return 0;
}

static void convert_and_store_ocv(struct qpnp_bms_chip *chip,
				struct raw_soc_params *raw)
{
	int rc;

	pr_debug("prev_last_good_ocv_raw = %d, last_good_ocv_raw = %d\n",
			chip->prev_last_good_ocv_raw,
			raw->last_good_ocv_raw);
	rc = calib_vadc(chip);
	if (rc)
		pr_err("Vadc reference voltage read failed, rc = %d\n", rc);
	chip->prev_last_good_ocv_raw = raw->last_good_ocv_raw;
	raw->last_good_ocv_uv = convert_vbatt_raw_to_uv(chip,
					raw->last_good_ocv_raw);
	chip->last_ocv_uv = raw->last_good_ocv_uv;
	pr_debug("last_good_ocv_uv = %d\n", raw->last_good_ocv_uv);
}

#define CLEAR_CC			BIT(7)
#define CLEAR_SW_CC			BIT(6)
/**
 * reset both cc and sw-cc.
 * note: this should only be ever called from one thread
 * or there may be a race condition where CC is never enabled
 * again
 */
static void reset_cc(struct qpnp_bms_chip *chip)
{
	int rc;

	pr_debug("resetting cc manually\n");
	rc = qpnp_masked_write(chip, BMS1_CC_CLEAR_CTL,
				CLEAR_CC | CLEAR_SW_CC,
				CLEAR_CC | CLEAR_SW_CC);
	if (rc)
		pr_err("cc reset failed: %d\n", rc);

	/* wait for 100us for cc to reset */
	udelay(100);

	rc = qpnp_masked_write(chip, BMS1_CC_CLEAR_CTL,
				CLEAR_CC | CLEAR_SW_CC, 0);
	if (rc)
		pr_err("cc reenable failed: %d\n", rc);
}

static int read_soc_params_raw(struct qpnp_bms_chip *chip,
				struct raw_soc_params *raw)
{
	int rc;

	mutex_lock(&chip->bms_output_lock);

	if (chip->prev_last_good_ocv_raw == 0) {
		/* software workaround for BMS 1.0
		 * The coulomb counter does not reset upon PON, so reset it
		 * manually upon probe. */
		if (chip->revision1 == 0 && chip->revision2 == 0)
			reset_cc(chip);
	}

	lock_output_data(chip);

	rc = qpnp_read_wrapper(chip, (u8 *)&raw->last_good_ocv_raw,
			chip->base + BMS1_OCV_FOR_SOC_DATA0, 2);
	if (rc) {
		pr_err("Error reading ocv: rc = %d\n", rc);
		return -ENXIO;
	}

	rc = read_cc_raw(chip, &raw->cc);
	if (rc) {
		pr_err("Failed to read raw cc data, rc = %d\n", rc);
		return rc;
	}

	unlock_output_data(chip);
	mutex_unlock(&chip->bms_output_lock);

	if (chip->prev_last_good_ocv_raw == 0) {
		convert_and_store_ocv(chip, raw);
		pr_debug("PON_OCV_UV = %d\n", chip->last_ocv_uv);
	} else if (chip->prev_last_good_ocv_raw != raw->last_good_ocv_raw) {
		convert_and_store_ocv(chip, raw);
		/* forget the old cc value upon ocv */
		chip->last_cc_uah = INT_MIN;
	} else {
		raw->last_good_ocv_uv = chip->last_ocv_uv;
	}

	/* fake a high OCV if done charging */
	if (chip->ocv_reading_at_100 != raw->last_good_ocv_raw) {
		chip->ocv_reading_at_100 = 0;
		chip->cc_reading_at_100 = 0;
	} else {
		/*
		 * force 100% ocv by selecting the highest voltage the
		 * battery could ever reach
		 */
		raw->last_good_ocv_uv = chip->max_voltage_uv;
		chip->last_ocv_uv = chip->max_voltage_uv;
	}
	pr_debug("last_good_ocv_raw= 0x%x, last_good_ocv_uv= %duV\n",
			raw->last_good_ocv_raw, raw->last_good_ocv_uv);
	pr_debug("cc_raw= 0x%llx\n", raw->cc);
	return 0;
}

static int calculate_pc(struct qpnp_bms_chip *chip, int ocv_uv,
							int batt_temp)
{
	int pc;

	pc = interpolate_pc(chip->pc_temp_ocv_lut,
			batt_temp / 10, ocv_uv / 1000);
	pr_debug("pc = %u %% for ocv = %d uv batt_temp = %d\n",
					pc, ocv_uv, batt_temp);
	/* Multiply the initial FCC value by the scale factor. */
	return pc;
}

static int calculate_fcc(struct qpnp_bms_chip *chip, int batt_temp)
{
	int fcc_uah;

	if (chip->adjusted_fcc_temp_lut == NULL) {
		/* interpolate_fcc returns a mv value. */
		fcc_uah = interpolate_fcc(chip->fcc_temp_lut,
						batt_temp) * 1000;
		pr_debug("fcc = %d uAh\n", fcc_uah);
		return fcc_uah;
	} else {
		return 1000 * interpolate_fcc(chip->adjusted_fcc_temp_lut,
				batt_temp);
	}
}

/* calculate remaining charge at the time of ocv */
static int calculate_ocv_charge(struct qpnp_bms_chip *chip,
						struct raw_soc_params *raw,
						int fcc_uah,
						int batt_temp)
{
	int  ocv_uv, pc;

	ocv_uv = raw->last_good_ocv_uv;
	pc = calculate_pc(chip, ocv_uv, batt_temp);
	pr_debug("ocv_uv = %d pc = %d\n", ocv_uv, pc);
	return (fcc_uah * pc) / 100;
}

#define CC_RESOLUTION_N		542535
#define CC_RESOLUTION_D		100000

static s64 cc_to_uv(s64 cc)
{
	return div_s64(cc * CC_RESOLUTION_N, CC_RESOLUTION_D);
}

#define CC_READING_TICKS	56
#define SLEEP_CLK_HZ		32764
#define SECONDS_PER_HOUR	3600

static s64 cc_uv_to_nvh(s64 cc_uv)
{
	return div_s64(cc_uv * CC_READING_TICKS * 1000,
			SLEEP_CLK_HZ * SECONDS_PER_HOUR);
}

/**
 * calculate_cc-
 * @chip:		the bms chip pointer
 * @cc:			the cc reading from bms h/w
 * @val:		return value
 * @coulomb_counter:	adjusted coulomb counter for 100%
 *
 * RETURNS: in val pointer coulomb counter based charger in uAh
 *          (micro Amp hour)
 */
static int calculate_cc(struct qpnp_bms_chip *chip, int64_t cc)
{
	int64_t cc_voltage_uv, cc_nvh, cc_uah;
	struct qpnp_iadc_calib calibration;

	qpnp_iadc_get_gain_and_offset(&calibration);
	cc_voltage_uv = cc;
	cc_voltage_uv -= chip->cc_reading_at_100;
	pr_debug("cc = %lld. after subtracting 0x%llx cc = %lld\n",
					cc, chip->cc_reading_at_100,
					cc_voltage_uv);
	cc_voltage_uv = cc_to_uv(cc_voltage_uv);
	cc_voltage_uv = cc_adjust_for_gain(cc_voltage_uv, calibration.gain_raw);
	pr_debug("cc_voltage_uv = %lld uv\n", cc_voltage_uv);
	cc_nvh = cc_uv_to_nvh(cc_voltage_uv);
	pr_debug("cc_nvh = %lld nano_volt_hour\n", cc_nvh);
	cc_uah = div_s64(cc_nvh, chip->r_sense_mohm);
	/* cc_raw had 4 bits of extra precision.
	   By now it should be within 32 bit range */
	return (int)cc_uah;
}

static int get_rbatt(struct qpnp_bms_chip *chip,
					int soc_rbatt_mohm, int batt_temp)
{
	int rbatt_mohm, scalefactor;

	rbatt_mohm = chip->default_rbatt_mohm;
	pr_debug("rbatt before scaling = %d\n", rbatt_mohm);
	if (chip->rbatt_sf_lut == NULL)  {
		pr_debug("RBATT = %d\n", rbatt_mohm);
		return rbatt_mohm;
	}
	/* Convert the batt_temp to DegC from deciDegC */
	batt_temp = batt_temp / 10;
	scalefactor = interpolate_scalingfactor(chip->rbatt_sf_lut,
						batt_temp, soc_rbatt_mohm);
	pr_debug("rbatt sf = %d for batt_temp = %d, soc_rbatt = %d\n",
				scalefactor, batt_temp, soc_rbatt_mohm);
	rbatt_mohm = (rbatt_mohm * scalefactor) / 100;

	rbatt_mohm += chip->r_conn_mohm;
	pr_debug("adding r_conn_mohm = %d rbatt = %d\n",
				chip->r_conn_mohm, rbatt_mohm);

	pr_debug("RBATT = %d\n", rbatt_mohm);
	return rbatt_mohm;
}

static void calculate_iavg(struct qpnp_bms_chip *chip, int cc_uah,
				int *iavg_ua, int delta_time_s)
{
	int delta_cc_uah = 0;

	/* if anything fails report the previous iavg_ua */
	*iavg_ua = chip->prev_iavg_ua;

	if (chip->last_cc_uah == INT_MIN) {
		get_battery_current(chip, iavg_ua);
		goto out;
	}

	/* use the previous iavg if called within 15 seconds */
	if (delta_time_s < 15) {
		*iavg_ua = chip->prev_iavg_ua;
		goto out;
	}

	delta_cc_uah = cc_uah - chip->last_cc_uah;

	*iavg_ua = div_s64((s64)delta_cc_uah * 3600, delta_time_s);

out:
	pr_debug("delta_cc = %d iavg_ua = %d\n", delta_cc_uah, (int)*iavg_ua);
	/* remember the iavg */
	chip->prev_iavg_ua = *iavg_ua;

	/* remember cc_uah */
	chip->last_cc_uah = cc_uah;
}

static int calculate_termination_uuc(struct qpnp_bms_chip *chip,
					struct soc_params *params,
					int batt_temp, int uuc_iavg_ma,
					int *ret_pc_unusable)
{
	int unusable_uv, pc_unusable, uuc_uah;
	int i = 0;
	int ocv_mv;
	int batt_temp_degc = batt_temp / 10;
	int rbatt_mohm;
	int delta_uv;
	int prev_delta_uv = 0;
	int prev_rbatt_mohm = 0;
	int uuc_rbatt_mohm;

	for (i = 0; i <= 100; i++) {
		ocv_mv = interpolate_ocv(chip->pc_temp_ocv_lut,
				batt_temp_degc, i);
		rbatt_mohm = get_rbatt(chip, i, batt_temp);
		unusable_uv = (rbatt_mohm * uuc_iavg_ma)
							+ (chip->v_cutoff_uv);
		delta_uv = ocv_mv * 1000 - unusable_uv;

		pr_debug("soc = %d ocv = %d rbat = %d u_uv = %d delta_v = %d\n",
				i, ocv_mv, rbatt_mohm, unusable_uv, delta_uv);

		if (delta_uv > 0)
			break;

		prev_delta_uv = delta_uv;
		prev_rbatt_mohm = rbatt_mohm;
	}

	uuc_rbatt_mohm = linear_interpolate(rbatt_mohm, delta_uv,
					prev_rbatt_mohm, prev_delta_uv,
					0);

	unusable_uv = (uuc_rbatt_mohm * uuc_iavg_ma) + (chip->v_cutoff_uv);

	pc_unusable = calculate_pc(chip, unusable_uv, batt_temp);
	uuc_uah = (params->fcc_uah * pc_unusable) / 100;
	pr_debug("For uuc_iavg_ma = %d, unusable_rbatt = %d unusable_uv = %d unusable_pc = %d uuc = %d\n",
					uuc_iavg_ma,
					uuc_rbatt_mohm, unusable_uv,
					pc_unusable, uuc_uah);
	*ret_pc_unusable = pc_unusable;
	return uuc_uah;
}

#define TIME_PER_PERCENT_UUC			60
static int adjust_uuc(struct qpnp_bms_chip *chip,
			struct soc_params *params,
			int new_pc_unusable,
			int new_uuc_uah,
			int batt_temp)
{
	int new_unusable_mv, new_iavg_ma;
	int batt_temp_degc = batt_temp / 10;
	int max_percent_change;

	max_percent_change = max(params->delta_time_s
				/ TIME_PER_PERCENT_UUC, 1);

	if (chip->prev_pc_unusable == -EINVAL
		|| abs(chip->prev_pc_unusable - new_pc_unusable)
			<= max_percent_change) {
		chip->prev_pc_unusable = new_pc_unusable;
		return new_uuc_uah;
	}

	/* the uuc is trying to change more than 1% restrict it */
	if (new_pc_unusable > chip->prev_pc_unusable)
		chip->prev_pc_unusable += max_percent_change;
	else
		chip->prev_pc_unusable -= max_percent_change;

	new_uuc_uah = (params->fcc_uah * chip->prev_pc_unusable) / 100;

	/* also find update the iavg_ma accordingly */
	new_unusable_mv = interpolate_ocv(chip->pc_temp_ocv_lut,
			batt_temp_degc, chip->prev_pc_unusable);
	if (new_unusable_mv < chip->v_cutoff_uv/1000)
		new_unusable_mv = chip->v_cutoff_uv/1000;

	new_iavg_ma = (new_unusable_mv * 1000 - chip->v_cutoff_uv)
						/ params->rbatt_mohm;
	if (new_iavg_ma == 0)
		new_iavg_ma = 1;
	chip->prev_uuc_iavg_ma = new_iavg_ma;
	pr_debug("Restricting UUC to %d (%d%%) unusable_mv = %d iavg_ma = %d\n",
					new_uuc_uah, chip->prev_pc_unusable,
					new_unusable_mv, new_iavg_ma);

	return new_uuc_uah;
}

#define CHARGING_IAVG_MA 250
#define MIN_SECONDS_FOR_VALID_SAMPLE	20
static int calculate_unusable_charge_uah(struct qpnp_bms_chip *chip,
					struct soc_params *params,
					int batt_temp)
{
	int uuc_uah_iavg;
	int i;
	int uuc_iavg_ma = params->iavg_ua / 1000;
	int pc_unusable;

	/*
	 * if called first time, fill all the samples with
	 * the shutdown_iavg_ma
	 */
	if (chip->first_time_calc_uuc && chip->shutdown_iavg_ma != 0) {
		pr_debug("Using shutdown_iavg_ma = %d in all samples\n",
				chip->shutdown_iavg_ma);
		for (i = 0; i < IAVG_SAMPLES; i++)
			chip->iavg_samples_ma[i] = chip->shutdown_iavg_ma;

		chip->iavg_index = 0;
		chip->iavg_num_samples = IAVG_SAMPLES;
	}

	/*
	 * if charging use a nominal avg current to keep
	 * a reasonable UUC while charging
	 */
	if (uuc_iavg_ma < 0)
		uuc_iavg_ma = CHARGING_IAVG_MA;
	chip->iavg_samples_ma[chip->iavg_index] = uuc_iavg_ma;
	chip->iavg_index = (chip->iavg_index + 1) % IAVG_SAMPLES;
	chip->iavg_num_samples++;
	if (chip->iavg_num_samples >= IAVG_SAMPLES)
		chip->iavg_num_samples = IAVG_SAMPLES;

	/* now that this sample is added calcualte the average */
	uuc_iavg_ma = 0;
	if (chip->iavg_num_samples != 0) {
		for (i = 0; i < chip->iavg_num_samples; i++) {
			pr_debug("iavg_samples_ma[%d] = %d\n", i,
					chip->iavg_samples_ma[i]);
			uuc_iavg_ma += chip->iavg_samples_ma[i];
		}

		uuc_iavg_ma = DIV_ROUND_CLOSEST(uuc_iavg_ma,
						chip->iavg_num_samples);
	}

	uuc_uah_iavg = calculate_termination_uuc(chip, params, uuc_iavg_ma,
						batt_temp, &pc_unusable);
	pr_debug("uuc_iavg_ma = %d uuc with iavg = %d\n",
						uuc_iavg_ma, uuc_uah_iavg);

	chip->prev_uuc_iavg_ma = uuc_iavg_ma;
	/* restrict the uuc such that it can increase only by one percent */
	uuc_uah_iavg = adjust_uuc(chip, params, pc_unusable,
					uuc_uah_iavg, batt_temp);

	chip->first_time_calc_uuc = 0;
	return uuc_uah_iavg;
}

static void find_ocv_for_soc(struct qpnp_bms_chip *chip,
				struct soc_params *params,
				int batt_temp,
				int shutdown_soc,
				int *ret_ocv_uv)
{
	s64 ocv_charge_uah;
	int pc, new_pc;
	int batt_temp_degc = batt_temp / 10;
	int ocv_uv;

	ocv_charge_uah = (s64)shutdown_soc
				* (params->fcc_uah - params->uuc_uah);
	ocv_charge_uah = div_s64(ocv_charge_uah, 100)
				+ params->cc_uah + params->uuc_uah;
	pc = DIV_ROUND_CLOSEST((int)ocv_charge_uah * 100, params->fcc_uah);
	pc = clamp(pc, 0, 100);

	ocv_uv = interpolate_ocv(chip->pc_temp_ocv_lut, batt_temp_degc, pc);

	pr_debug("s_soc = %d, fcc = %d uuc = %d rc = %d, pc = %d, ocv mv = %d\n",
					shutdown_soc, params->fcc_uah,
					params->uuc_uah, (int)ocv_charge_uah,
					pc, ocv_uv);
	new_pc = interpolate_pc(chip->pc_temp_ocv_lut, batt_temp_degc, ocv_uv);
	pr_debug("test revlookup pc = %d for ocv = %d\n", new_pc, ocv_uv);

	while (abs(new_pc - pc) > 1) {
		int delta_mv = 5;

		if (new_pc > pc)
			delta_mv = -1 * delta_mv;

		ocv_uv = ocv_uv + delta_mv;
		new_pc = interpolate_pc(chip->pc_temp_ocv_lut,
				batt_temp_degc, ocv_uv);
		pr_debug("test revlookup pc = %d for ocv = %d\n",
				new_pc, ocv_uv);
	}

	*ret_ocv_uv = ocv_uv * 1000;
	params->ocv_charge_uah = (int)ocv_charge_uah;
}

static int get_current_time(unsigned long *now_tm_sec)
{
	struct rtc_time tm;
	struct rtc_device *rtc;
	int rc;

	rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
	if (rtc == NULL) {
		pr_err("%s: unable to open rtc device (%s)\n",
			__FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
		rc = -EINVAL;
		goto close_time;
	}

	rc = rtc_read_time(rtc, &tm);
	if (rc) {
		pr_err("Error reading rtc device (%s) : %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}

	rc = rtc_valid_tm(&tm);
	if (rc) {
		pr_err("Invalid RTC time (%s): %d\n",
			CONFIG_RTC_HCTOSYS_DEVICE, rc);
		goto close_time;
	}
	rtc_tm_to_time(&tm, now_tm_sec);

close_time:
	rtc_class_close(rtc);
	return rc;
}

static int calculate_delta_time(struct qpnp_bms_chip *chip, int *delta_time_s)
{
	unsigned long now_tm_sec = 0;

	/* default to delta time = 0 if anything fails */
	*delta_time_s = 0;

	get_current_time(&now_tm_sec);

	*delta_time_s = (now_tm_sec - chip->tm_sec);
	pr_debug("tm_sec = %ld, now_tm_sec = %ld delta_s = %d\n",
		chip->tm_sec, now_tm_sec, *delta_time_s);

	/* remember this time */
	chip->tm_sec = now_tm_sec;
	return 0;
}

static void calculate_soc_params(struct qpnp_bms_chip *chip,
						struct raw_soc_params *raw,
						struct soc_params *params,
						int batt_temp)
{
	int soc_rbatt;

	calculate_delta_time(chip, &params->delta_time_s);
	params->fcc_uah = calculate_fcc(chip, batt_temp);
	pr_debug("FCC = %uuAh batt_temp = %d\n", params->fcc_uah, batt_temp);

	/* calculate remainging charge */
	params->ocv_charge_uah = calculate_ocv_charge(
						chip, raw,
						params->fcc_uah,
						batt_temp);
	pr_debug("ocv_charge_uah = %uuAh\n", params->ocv_charge_uah);

	/* calculate cc micro_volt_hour */
	params->cc_uah = calculate_cc(chip, raw->cc);
	pr_debug("cc_uah = %duAh raw->cc = %llx cc = %lld after subtracting %llx\n",
				params->cc_uah, raw->cc,
				(int64_t)raw->cc - chip->cc_reading_at_100,
				chip->cc_reading_at_100);

	soc_rbatt = ((params->ocv_charge_uah - params->cc_uah) * 100)
							/ params->fcc_uah;
	if (soc_rbatt < 0)
		soc_rbatt = 0;
	params->rbatt_mohm = get_rbatt(chip, soc_rbatt, batt_temp);

	calculate_iavg(chip, params->cc_uah, &params->iavg_ua,
						params->delta_time_s);

	params->uuc_uah = calculate_unusable_charge_uah(chip, params,
							batt_temp);
	pr_debug("UUC = %uuAh\n", params->uuc_uah);
}

static bool is_shutdown_soc_within_limits(struct qpnp_bms_chip *chip, int soc)
{
	if (chip->shutdown_soc_invalid) {
		pr_debug("NOT forcing shutdown soc = %d\n", chip->shutdown_soc);
		return 0;
	}

	if (abs(chip->shutdown_soc - soc) > chip->shutdown_soc_valid_limit) {
		pr_debug("rejecting shutdown soc = %d, soc = %d limit = %d\n",
			chip->shutdown_soc, soc,
			chip->shutdown_soc_valid_limit);
		chip->shutdown_soc_invalid = 1;
		return 0;
	}

	return 1;
}

#define BMS_OVERRIDE_MODE_EN_BIT	BIT(7)
#define EN_VBAT_BIT			BIT(0)
#define OVERRIDE_MODE_DELAY_MS		20
static int override_mode_batt_v_and_i(
		struct qpnp_bms_chip *chip, int *ibat_ua, int *vbat_uv)
{
	int16_t vsense_raw, vbat_raw;
	int vsense_uv, rc;
	u8 delay;

	mutex_lock(&chip->bms_output_lock);

	delay = 0x00;
	rc = qpnp_write_wrapper(chip, &delay,
			chip->base + BMS1_S1_DELAY_CTL, 1);
	if (rc)
		pr_err("unable to write into BMS1_S1_DELAY, rc: %d\n", rc);

	rc = qpnp_masked_write(chip, BMS1_MODE_CTL,
			BMS_OVERRIDE_MODE_EN_BIT | EN_VBAT_BIT,
			BMS_OVERRIDE_MODE_EN_BIT | EN_VBAT_BIT);
	if (rc)
		pr_err("unable to write into BMS1_MODE_CTL, rc: %d\n", rc);

	msleep(OVERRIDE_MODE_DELAY_MS);

	lock_output_data(chip);
	qpnp_read_wrapper(chip, (u8 *)&vsense_raw,
			chip->base + BMS1_VSENSE_AVG_DATA0, 2);
	qpnp_read_wrapper(chip, (u8 *)&vbat_raw,
			chip->base + BMS1_VBAT_AVG_DATA0, 2);
	unlock_output_data(chip);

	rc = qpnp_masked_write(chip, BMS1_MODE_CTL,
			BMS_OVERRIDE_MODE_EN_BIT | EN_VBAT_BIT, 0);

	delay = 0x0B;
	rc = qpnp_write_wrapper(chip, &delay,
			chip->base + BMS1_S1_DELAY_CTL, 1);
	if (rc)
		pr_err("unable to write into BMS1_S1_DELAY, rc: %d\n", rc);

	mutex_unlock(&chip->bms_output_lock);

	*vbat_uv = convert_vbatt_raw_to_uv(chip, vbat_raw);
	vsense_uv = convert_vsense_to_uv(chip, vsense_raw);
	*ibat_ua = vsense_uv * 1000 / (int)chip->r_sense_mohm;

	pr_debug("vsense_raw = 0x%x vbat_raw = 0x%x ibat_ua = %d vbat_uv = %d\n",
			(uint16_t)vsense_raw, (uint16_t)vbat_raw,
			*ibat_ua, *vbat_uv);
	return 0;
}

static bool is_battery_charging(struct qpnp_bms_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->batt_psy == NULL)
		chip->batt_psy = power_supply_get_by_name("battery");
	if (chip->batt_psy) {
		/* if battery has been registered, use the status property */
		chip->batt_psy->get_property(chip->batt_psy,
					POWER_SUPPLY_PROP_STATUS, &ret);
		return ret.intval == POWER_SUPPLY_STATUS_CHARGING;
	}

	/* Default to false if the battery power supply is not registered. */
	pr_debug("battery power supply is not registered\n");
	return false;
}

static bool is_batfet_open(struct qpnp_bms_chip *chip)
{
	union power_supply_propval ret = {0,};

	if (chip->batt_psy == NULL)
		chip->batt_psy = power_supply_get_by_name("battery");
	if (chip->batt_psy) {
		/* if battery has been registered, use the status property */
		chip->batt_psy->get_property(chip->batt_psy,
					POWER_SUPPLY_PROP_STATUS, &ret);
		return ret.intval == POWER_SUPPLY_STATUS_FULL;
	}

	/* Default to true if the battery power supply is not registered. */
	pr_debug("battery power supply is not registered\n");
	return true;
}

static int get_simultaneous_batt_v_and_i(struct qpnp_bms_chip *chip,
					int *ibat_ua, int *vbat_uv)
{
	int rc;

	if (is_batfet_open(chip)) {
		pr_debug("batfet is open using separate vbat and ibat meas\n");
		rc = get_battery_voltage(vbat_uv);
		if (rc < 0) {
			pr_err("adc vbat failed err = %d\n", rc);
			return rc;
		}
		rc = get_battery_current(chip, ibat_ua);
		if (rc < 0) {
			pr_err("bms ibat failed err = %d\n", rc);
			return rc;
		}
	} else {
		return override_mode_batt_v_and_i(chip, ibat_ua, vbat_uv);
	}

	return 0;
}

static int bound_soc(int soc)
{
	soc = max(0, soc);
	soc = min(100, soc);
	return soc;
}

static int charging_adjustments(struct qpnp_bms_chip *chip,
				struct soc_params *params, int soc,
				int vbat_uv, int ibat_ua, int batt_temp)
{
	int chg_soc;

	if (chip->soc_at_cv == -EINVAL) {
		/* In constant current charging return the calc soc */
		if (vbat_uv <= chip->max_voltage_uv)
			pr_debug("CC CHG SOC %d\n", soc);

		/* Note the CC to CV point */
		if (vbat_uv >= chip->max_voltage_uv) {
			chip->soc_at_cv = soc;
			chip->prev_chg_soc = soc;
			chip->ibat_at_cv_ua = ibat_ua;
			pr_debug("CC_TO_CV ibat_ua = %d CHG SOC %d\n",
					ibat_ua, soc);
		}
		return soc;
	}

	/*
	 * battery is in CV phase - begin liner inerpolation of soc based on
	 * battery charge current
	 */

	/*
	 * if voltage lessened (possibly because of a system load)
	 * keep reporting the prev chg soc
	 */
	if (vbat_uv <= chip->max_voltage_uv) {
		pr_debug("vbat %d < max = %d CC CHG SOC %d\n",
			vbat_uv, chip->max_voltage_uv, chip->prev_chg_soc);
		return chip->prev_chg_soc;
	}

	chg_soc = linear_interpolate(chip->soc_at_cv, chip->ibat_at_cv_ua,
					100, -100000,
					ibat_ua);
	chg_soc = bound_soc(chg_soc);

	/* always report a higher soc */
	if (chg_soc > chip->prev_chg_soc) {
		int new_ocv_uv;

		chip->prev_chg_soc = chg_soc;

		find_ocv_for_soc(chip, params, batt_temp, chg_soc, &new_ocv_uv);
		chip->last_ocv_uv = new_ocv_uv;
		pr_debug("CC CHG ADJ OCV = %d CHG SOC %d\n",
				new_ocv_uv,
				chip->prev_chg_soc);
	}

	pr_debug("Reporting CHG SOC %d\n", chip->prev_chg_soc);
	return chip->prev_chg_soc;
}

static int adjust_soc(struct qpnp_bms_chip *chip, struct soc_params *params,
							int soc, int batt_temp)
{
	int ibat_ua = 0, vbat_uv = 0;
	int ocv_est_uv = 0, soc_est = 0, pc_est = 0, pc = 0;
	int delta_ocv_uv = 0;
	int n = 0;
	int rc_new_uah = 0;
	int pc_new = 0;
	int soc_new = 0;
	int slope = 0;
	int rc = 0;
	int delta_ocv_uv_limit = 0;

	rc = get_simultaneous_batt_v_and_i(chip, &ibat_ua, &vbat_uv);
	if (rc < 0) {
		pr_err("simultaneous vbat ibat failed err = %d\n", rc);
		goto out;
	}

	delta_ocv_uv_limit = DIV_ROUND_CLOSEST(ibat_ua, 1000);

	ocv_est_uv = vbat_uv + (ibat_ua * params->rbatt_mohm)/1000;

	chip->ibat_max_ua = (ocv_est_uv - chip->v_cutoff_uv) * 1000
					/ (params->rbatt_mohm);

	pc_est = calculate_pc(chip, ocv_est_uv, batt_temp);
	soc_est = div_s64((s64)params->fcc_uah * pc_est - params->uuc_uah*100,
				(s64)params->fcc_uah - params->uuc_uah);
	soc_est = bound_soc(soc_est);

	if (ibat_ua < 0 && !is_batfet_open(chip)) {
		soc = charging_adjustments(chip, params, soc, vbat_uv, ibat_ua,
				batt_temp);
		goto out;
	}

	/*
	 * do not adjust
	 * if soc is same as what bms calculated
	 * if soc_est is between 45 and 25, this is the flat portion of the
	 * curve where soc_est is not so accurate. We generally don't want to
	 * adjust when soc_est is inaccurate except for the cases when soc is
	 * way far off (higher than 50 or lesser than 20).
	 * Also don't adjust soc if it is above 90 becuase it might be pulled
	 * low and cause a bad user experience
	 */
	if (soc_est == soc
		|| (is_between(45, chip->adjust_soc_low_threshold, soc_est)
		&& is_between(50, chip->adjust_soc_low_threshold - 5, soc))
		|| soc >= 90)
		goto out;

	if (chip->last_soc_est == -EINVAL)
		chip->last_soc_est = soc;

	n = min(200, max(1 , soc + soc_est + chip->last_soc_est));
	chip->last_soc_est = soc_est;

	pc = calculate_pc(chip, chip->last_ocv_uv, batt_temp);
	if (pc > 0) {
		pc_new = calculate_pc(chip,
				chip->last_ocv_uv - (++slope * 1000),
				batt_temp);
		while (pc_new == pc) {
			/* start taking 10mV steps */
			slope = slope + 10;
			pc_new = calculate_pc(chip,
				chip->last_ocv_uv - (slope * 1000),
				batt_temp);
		}
	} else {
		/*
		 * pc is already at the lowest point,
		 * assume 1 millivolt translates to 1% pc
		 */
		pc = 1;
		pc_new = 0;
		slope = 1;
	}

	delta_ocv_uv = div_s64((soc - soc_est) * (s64)slope * 1000,
							n * (pc - pc_new));

	if (abs(delta_ocv_uv) > delta_ocv_uv_limit) {
		pr_debug("limiting delta ocv %d limit = %d\n", delta_ocv_uv,
				delta_ocv_uv_limit);

		if (delta_ocv_uv > 0)
			delta_ocv_uv = delta_ocv_uv_limit;
		else
			delta_ocv_uv = -1 * delta_ocv_uv_limit;
		pr_debug("new delta ocv = %d\n", delta_ocv_uv);
	}

	chip->last_ocv_uv -= delta_ocv_uv;

	if (chip->last_ocv_uv >= chip->max_voltage_uv)
		chip->last_ocv_uv = chip->max_voltage_uv;

	/* calculate the soc based on this new ocv */
	pc_new = calculate_pc(chip, chip->last_ocv_uv, batt_temp);
	rc_new_uah = (params->fcc_uah * pc_new) / 100;
	soc_new = (rc_new_uah - params->cc_uah - params->uuc_uah)*100
					/ (params->fcc_uah - params->uuc_uah);
	soc_new = bound_soc(soc_new);

	/*
	 * if soc_new is ZERO force it higher so that phone doesnt report soc=0
	 * soc = 0 should happen only when soc_est == 0
	 */
	if (soc_new == 0 && soc_est != 0)
		soc_new = 1;

	soc = soc_new;

out:
	pr_debug("ibat_ua = %d, vbat_uv = %d, ocv_est_uv = %d, pc_est = %d, soc_est = %d, n = %d, delta_ocv_uv = %d, last_ocv_uv = %d, pc_new = %d, soc_new = %d, rbatt = %d, slope = %d\n",
		ibat_ua, vbat_uv, ocv_est_uv, pc_est,
		soc_est, n, delta_ocv_uv, chip->last_ocv_uv,
		pc_new, soc_new, params->rbatt_mohm, slope);

	return soc;
}

static int clamp_soc_based_on_voltage(struct qpnp_bms_chip *chip, int soc)
{
	int rc, vbat_uv;
	struct qpnp_vadc_result result;

	rc = qpnp_vadc_read(VBAT_SNS, &result);
	if (rc) {
		pr_err("error reading vbat_sns adc channel = %d, rc = %d\n",
						VBAT_SNS, rc);
		return rc;
	}

	vbat_uv = (int)result.physical;
	if (soc == 0 && vbat_uv > chip->v_cutoff_uv) {
		pr_debug("clamping soc to 1, vbat (%d) > cutoff (%d)\n",
						vbat_uv, chip->v_cutoff_uv);
		return 1;
	} else if (soc > 0 && vbat_uv < chip->v_cutoff_uv) {
		pr_debug("forcing soc to 0, vbat (%d) < cutoff (%d)\n",
						vbat_uv, chip->v_cutoff_uv);
		return 0;
	} else {
		pr_debug("not clamping, using soc = %d, vbat = %d and cutoff = %d\n",
				soc, vbat_uv, chip->v_cutoff_uv);
		return soc;
	}
}

static int calculate_state_of_charge(struct qpnp_bms_chip *chip,
					struct raw_soc_params *raw,
					int batt_temp)
{
	int soc, new_ocv_uv;
	int shutdown_soc, new_calculated_soc, remaining_usable_charge_uah;
	struct soc_params params;

	calculate_soc_params(chip, raw, &params, batt_temp);
	/* calculate remaining usable charge */
	remaining_usable_charge_uah = params.ocv_charge_uah
					- params.cc_uah
					- params.uuc_uah;

	pr_debug("RUC = %duAh\n", remaining_usable_charge_uah);
	if (params.fcc_uah - params.uuc_uah <= 0) {
		pr_debug("FCC = %duAh, UUC = %duAh forcing soc = 0\n",
						params.fcc_uah,
						params.uuc_uah);
		soc = 0;
	} else {
		soc = DIV_ROUND_CLOSEST((remaining_usable_charge_uah * 100),
					(params.fcc_uah
						- params.uuc_uah));
	}

	if (chip->first_time_calc_soc && soc < 0) {
		/*
		 * first time calcualtion and the pon ocv  is too low resulting
		 * in a bad soc. Adjust ocv to get 0 soc
		 */
		pr_debug("soc is %d, adjusting pon ocv to make it 0\n", soc);
		find_ocv_for_soc(chip, &params, batt_temp, 0, &new_ocv_uv);
		chip->last_ocv_uv = new_ocv_uv;

		remaining_usable_charge_uah = params.ocv_charge_uah
					- params.cc_uah
					- params.uuc_uah;

		soc = DIV_ROUND_CLOSEST((remaining_usable_charge_uah * 100),
					(params.fcc_uah
						- params.uuc_uah));
		pr_debug("DONE for O soc is %d, pon ocv adjusted to %duV\n",
				soc, chip->last_ocv_uv);
	}

	if (soc > 100)
		soc = 100;

	if (soc < 0) {
		pr_debug("bad rem_usb_chg = %d rem_chg %d, cc_uah %d, unusb_chg %d\n",
				remaining_usable_charge_uah,
				params.ocv_charge_uah,
				params.cc_uah, params.uuc_uah);

		pr_debug("for bad rem_usb_chg last_ocv_uv = %d batt_temp = %d fcc = %d soc =%d\n",
				chip->last_ocv_uv, batt_temp,
				params.fcc_uah, soc);
		soc = 0;
	}

	mutex_lock(&chip->soc_invalidation_mutex);
	shutdown_soc = chip->shutdown_soc;

	if (chip->first_time_calc_soc && soc != shutdown_soc
			&& is_shutdown_soc_within_limits(chip, soc)) {
		/*
		 * soc for the first time - use shutdown soc
		 * to adjust pon ocv since it is a small percent away from
		 * the real soc
		 */
		pr_debug("soc = %d before forcing shutdown_soc = %d\n",
							soc, shutdown_soc);
		find_ocv_for_soc(chip, &params, batt_temp,
					shutdown_soc, &new_ocv_uv);
		chip->pon_ocv_uv = chip->last_ocv_uv;
		chip->last_ocv_uv = new_ocv_uv;

		remaining_usable_charge_uah = params.ocv_charge_uah
					- params.cc_uah
					- params.uuc_uah;

		soc = DIV_ROUND_CLOSEST((remaining_usable_charge_uah * 100),
					(params.fcc_uah
						- params.uuc_uah));

		pr_debug("DONE for shutdown_soc = %d soc is %d, adjusted ocv to %duV\n",
				shutdown_soc, soc, chip->last_ocv_uv);
	}
	mutex_unlock(&chip->soc_invalidation_mutex);

	pr_debug("SOC before adjustment = %d\n", soc);
	new_calculated_soc = adjust_soc(chip, &params, soc, batt_temp);

	/* clamp soc due to BMS HW inaccuracies in pm8941v2.0 */
	if (chip->revision1 == 0 && chip->revision2 == 0)
		new_calculated_soc = clamp_soc_based_on_voltage(chip,
						new_calculated_soc);

	if (new_calculated_soc != chip->calculated_soc
			&& chip->bms_psy.name != NULL) {
		power_supply_changed(&chip->bms_psy);
		pr_debug("power supply changed\n");
	}

	chip->calculated_soc = new_calculated_soc;
	pr_debug("CC based calculated SOC = %d\n", chip->calculated_soc);
	chip->first_time_calc_soc = 0;
	return chip->calculated_soc;
}

static int read_vbat(struct qpnp_bms_chip *chip)
{
	int rc;
	struct qpnp_vadc_result result;

	rc = qpnp_vadc_read(VBAT_SNS, &result);
	if (rc) {
		pr_err("error reading vadc VBAT_SNS = %d, rc = %d\n",
					VBAT_SNS, rc);
		return rc;
	}
	pr_debug("read %duv from vadc\n", (int)result.physical);
	return (int)result.physical;
}

static int calculate_soc_from_voltage(struct qpnp_bms_chip *chip)
{
	int voltage_range_uv, voltage_remaining_uv, voltage_based_soc;
	int vbat_uv;

	vbat_uv = read_vbat(chip);

	voltage_range_uv = chip->max_voltage_uv - chip->v_cutoff_uv;
	voltage_remaining_uv = vbat_uv - chip->v_cutoff_uv;
	voltage_based_soc = voltage_remaining_uv * 100 / voltage_range_uv;

	voltage_based_soc = clamp(voltage_based_soc, 0, 100);

	if (chip->prev_voltage_based_soc != voltage_based_soc
				&& chip->bms_psy.name != NULL) {
		power_supply_changed(&chip->bms_psy);
		pr_debug("power supply changed\n");
	}
	chip->prev_voltage_based_soc = voltage_based_soc;

	pr_debug("vbat used = %duv\n", vbat_uv);
	pr_debug("Calculated voltage based soc = %d\n", voltage_based_soc);
	return voltage_based_soc;
}

static void calculate_soc_work(struct work_struct *work)
{
	struct qpnp_bms_chip *chip = container_of(work,
				struct qpnp_bms_chip,
				calculate_soc_delayed_work.work);
	int batt_temp, rc, soc;
	struct qpnp_vadc_result result;
	struct raw_soc_params raw;

	if (chip->use_voltage_soc) {
		soc = calculate_soc_from_voltage(chip);
	} else {
		rc = qpnp_vadc_read(LR_MUX1_BATT_THERM, &result);
		if (rc) {
			pr_err("error reading vadc LR_MUX1_BATT_THERM = %d, rc = %d\n",
						LR_MUX1_BATT_THERM, rc);
			return;
		}
		pr_debug("batt_temp phy = %lld meas = 0x%llx\n",
						result.physical,
						result.measurement);
		batt_temp = (int)result.physical;

		mutex_lock(&chip->last_ocv_uv_mutex);
		read_soc_params_raw(chip, &raw);
		soc = calculate_state_of_charge(chip, &raw, batt_temp);
		mutex_unlock(&chip->last_ocv_uv_mutex);
	}

	if (soc < chip->low_soc_calc_threshold)
		schedule_delayed_work(&chip->calculate_soc_delayed_work,
			round_jiffies_relative(msecs_to_jiffies
			(chip->low_soc_calculate_soc_ms)));
	else
		schedule_delayed_work(&chip->calculate_soc_delayed_work,
			round_jiffies_relative(msecs_to_jiffies
			(chip->calculate_soc_ms)));
}

static void backup_soc_and_iavg(struct qpnp_bms_chip *chip, int batt_temp,
				int soc)
{
	u8 temp;
	int rc;
	int iavg_ma = chip->prev_uuc_iavg_ma;

	if (iavg_ma > IAVG_START)
		temp = (iavg_ma - IAVG_START) / IAVG_STEP_SIZE_MA;
	else
		temp = 0;

	rc = qpnp_write_wrapper(chip, &temp,
			chip->base + IAVG_STORAGE_REG, 1);

	if (soc == 0)
		temp = SOC_ZERO;
	else
		temp = soc;

	/* don't store soc if temperature is below 5degC */
	if (batt_temp > IGNORE_SOC_TEMP_DECIDEG)
		rc = qpnp_write_wrapper(chip, &temp,
				chip->base + SOC_STORAGE_REG, 1);
}

#define SOC_CATCHUP_SEC_MAX		600
#define SOC_CATCHUP_SEC_PER_PERCENT	60
#define MAX_CATCHUP_SOC	(SOC_CATCHUP_SEC_MAX/SOC_CATCHUP_SEC_PER_PERCENT)
static int scale_soc_while_chg(struct qpnp_bms_chip *chip,
				int delta_time_us, int new_soc, int prev_soc)
{
	int chg_time_sec;
	int catch_up_sec;
	int scaled_soc;
	int numerator;

	/*
	 * The device must be charging for reporting a higher soc, if
	 * not ignore this soc and continue reporting the prev_soc.
	 * Also don't report a high value immediately slowly scale the
	 * value from prev_soc to the new soc based on a charge time
	 * weighted average
	 */

	/* if not charging, return last soc */
	if (!is_battery_charging(chip))
		return prev_soc;

	chg_time_sec = DIV_ROUND_UP(chip->charge_time_us, USEC_PER_SEC);
	catch_up_sec = DIV_ROUND_UP(chip->catch_up_time_us, USEC_PER_SEC);
	pr_debug("cts= %d catch_up_sec = %d\n", chg_time_sec, catch_up_sec);

	/*
	 * if charging for more than catch_up time, simply return
	 * new soc
	 */
	if (chg_time_sec > catch_up_sec)
		return new_soc;

	numerator = (catch_up_sec - chg_time_sec) * prev_soc
			+ chg_time_sec * new_soc;
	scaled_soc = numerator / catch_up_sec;

	pr_debug("cts = %d new_soc = %d prev_soc = %d scaled_soc = %d\n",
			chg_time_sec, new_soc, prev_soc, scaled_soc);

	return scaled_soc;
}

/*
 * bms_fake_battery is set in setups where a battery emulator is used instead
 * of a real battery. This makes the bms driver report a different/fake value
 * regardless of the calculated state of charge.
 */
static int bms_fake_battery = -EINVAL;
module_param(bms_fake_battery, int, 0644);

static int report_voltage_based_soc(struct qpnp_bms_chip *chip)
{
	pr_debug("Reported voltage based soc = %d\n",
			chip->prev_voltage_based_soc);
	return chip->prev_voltage_based_soc;
}

static int report_cc_based_soc(struct qpnp_bms_chip *chip)
{
	int soc;
	int delta_time_us;
	struct timespec now;
	struct qpnp_vadc_result result;
	int batt_temp;
	int rc;

	soc = chip->calculated_soc;

	rc = qpnp_vadc_read(LR_MUX1_BATT_THERM, &result);

	if (rc) {
		pr_err("error reading adc channel = %d, rc = %d\n",
					LR_MUX1_BATT_THERM, rc);
		return rc;
	}
	pr_debug("batt_temp phy = %lld meas = 0x%llx\n", result.physical,
						result.measurement);
	batt_temp = (int)result.physical;

	do_posix_clock_monotonic_gettime(&now);
	if (chip->t_soc_queried.tv_sec != 0) {
		delta_time_us
		= (now.tv_sec - chip->t_soc_queried.tv_sec) * USEC_PER_SEC
			+ (now.tv_nsec - chip->t_soc_queried.tv_nsec) / 1000;
	} else {
		/* calculation for the first time */
		delta_time_us = 0;
	}

	/*
	 * account for charge time - limit it to SOC_CATCHUP_SEC to
	 * avoid overflows when charging continues for extended periods
	 */
	if (is_battery_charging(chip)) {
		if (chip->charge_time_us == 0) {
			/*
			 * calculating soc for the first time
			 * after start of chg. Initialize catchup time
			 */
			if (abs(soc - chip->last_soc) < MAX_CATCHUP_SOC)
				chip->catch_up_time_us =
				(soc - chip->last_soc)
					* SOC_CATCHUP_SEC_PER_PERCENT
					* USEC_PER_SEC;
			else
				chip->catch_up_time_us =
				SOC_CATCHUP_SEC_MAX * USEC_PER_SEC;

			if (chip->catch_up_time_us < 0)
				chip->catch_up_time_us = 0;
		}

		/* add charge time */
		if (chip->charge_time_us < SOC_CATCHUP_SEC_MAX * USEC_PER_SEC)
			chip->charge_time_us += delta_time_us;

		/* end catchup if calculated soc and last soc are same */
		if (chip->last_soc == soc)
			chip->catch_up_time_us = 0;
	}

	/* last_soc < soc  ... scale and catch up */
	if (chip->last_soc != -EINVAL && chip->last_soc < soc
			&& soc != 100 && chip->catch_up_time_us != 0)
		soc = scale_soc_while_chg(chip, delta_time_us,
						soc, chip->last_soc);

	pr_debug("last_soc = %d, calculated_soc = %d, soc = %d\n",
			chip->last_soc, chip->calculated_soc, soc);
	chip->last_soc = soc;
	backup_soc_and_iavg(chip, batt_temp, chip->last_soc);
	pr_debug("Reported SOC = %d\n", chip->last_soc);
	chip->t_soc_queried = now;

	return chip->last_soc;
}

static int report_state_of_charge(struct qpnp_bms_chip *chip)
{
	if (bms_fake_battery != -EINVAL) {
		pr_debug("Returning Fake SOC = %d%%\n", bms_fake_battery);
		return bms_fake_battery;
	} else if (chip->use_voltage_soc)
		return report_voltage_based_soc(chip);
	else
		return report_cc_based_soc(chip);
}

/* Returns capacity as a SoC percentage between 0 and 100 */
static int get_prop_bms_capacity(struct qpnp_bms_chip *chip)
{
	return report_state_of_charge(chip);
}

/* Returns estimated max current that the battery can supply in uA */
static int get_prop_bms_current_max(struct qpnp_bms_chip *chip)
{
	return chip->ibat_max_ua;
}

/* Returns instantaneous current in uA */
static int get_prop_bms_current_now(struct qpnp_bms_chip *chip)
{
	int rc, result_ua;

	rc = get_battery_current(chip, &result_ua);
	if (rc) {
		pr_err("failed to get current: %d\n", rc);
		return rc;
	}
	return result_ua;
}

/* Returns full charge design in uAh */
static int get_prop_bms_charge_full_design(struct qpnp_bms_chip *chip)
{
	return chip->fcc;
}

static bool get_prop_bms_online(struct qpnp_bms_chip *chip)
{
	return chip->online;
}

static int get_prop_bms_status(struct qpnp_bms_chip *chip)
{
	return chip->charger_status;
}

static void set_prop_bms_online(struct qpnp_bms_chip *chip, bool online)
{
	chip->online = online;
}

static void set_prop_bms_status(struct qpnp_bms_chip *chip, int status)
{
	chip->charger_status = status;
}

static void qpnp_bms_external_power_changed(struct power_supply *psy)
{
}

static int qpnp_bms_power_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct qpnp_bms_chip *chip = container_of(psy, struct qpnp_bms_chip,
								bms_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = get_prop_bms_capacity(chip);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = get_prop_bms_current_now(chip);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = get_prop_bms_current_max(chip);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = get_prop_bms_charge_full_design(chip);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = get_prop_bms_status(chip);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = get_prop_bms_online(chip);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int qpnp_bms_power_set_property(struct power_supply *psy,
					enum power_supply_property psp,
					const union power_supply_propval *val)
{
	struct qpnp_bms_chip *chip = container_of(psy, struct qpnp_bms_chip,
								bms_psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		set_prop_bms_online(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_STATUS:
		set_prop_bms_status(chip, (bool)val->intval);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void read_shutdown_soc_and_iavg(struct qpnp_bms_chip *chip)
{
	int rc;
	u8 temp;

	if (chip->ignore_shutdown_soc) {
		chip->shutdown_soc_invalid = 1;
		chip->shutdown_soc = 0;
		chip->shutdown_iavg_ma = 0;
	} else {
		rc = qpnp_read_wrapper(chip, &temp,
				chip->base + IAVG_STORAGE_REG, 1);
		if (rc) {
			pr_err("failed to read addr = %d %d assuming %d\n",
					chip->base + IAVG_STORAGE_REG, rc,
					IAVG_START);
			chip->shutdown_iavg_ma = IAVG_START;
		} else {
			if (temp == 0) {
				chip->shutdown_iavg_ma = IAVG_START;
			} else {
				chip->shutdown_iavg_ma = IAVG_START
					+ IAVG_STEP_SIZE_MA * (temp + 1);
			}
		}

		rc = qpnp_read_wrapper(chip, &temp,
				chip->base + SOC_STORAGE_REG, 1);
		if (rc) {
			pr_err("failed to read addr = %d %d\n",
					chip->base + SOC_STORAGE_REG, rc);
		} else {
			chip->shutdown_soc = temp;

			if (chip->shutdown_soc == 0) {
				pr_debug("No shutdown soc available\n");
				chip->shutdown_soc_invalid = 1;
				chip->shutdown_iavg_ma = 0;
			} else if (chip->shutdown_soc == SOC_ZERO) {
				chip->shutdown_soc = 0;
			}
		}
	}

	pr_debug("shutdown_soc = %d shutdown_iavg = %d shutdown_soc_invalid = %d\n",
			chip->shutdown_soc,
			chip->shutdown_iavg_ma,
			chip->shutdown_soc_invalid);
}

#define PALLADIUM_ID_MIN	0x7F40
#define PALLADIUM_ID_MAX	0x7F5A
#define DESAY_5200_ID_MIN	0x7F7F
#define DESAY_5200_ID_MAX	0x802F
static int32_t read_battery_id(struct qpnp_bms_chip *chip)
{
	int rc;
	struct qpnp_vadc_result result;

	rc = qpnp_vadc_read(LR_MUX2_BAT_ID, &result);
	if (rc) {
		pr_err("error reading batt id channel = %d, rc = %d\n",
					LR_MUX2_BAT_ID, rc);
		return rc;
	}
	pr_debug("batt_id phy = %lld meas = 0x%llx\n", result.physical,
						result.measurement);
	pr_debug("raw_code = 0x%x\n", result.adc_code);
	return result.adc_code;
}

static int set_battery_data(struct qpnp_bms_chip *chip)
{
	int64_t battery_id;

	if (chip->batt_type == BATT_DESAY)
		goto desay;
	else if (chip->batt_type == BATT_PALLADIUM)
		goto palladium;

	battery_id = read_battery_id(chip);
	if (battery_id < 0) {
		pr_err("cannot read battery id err = %lld\n", battery_id);
		return battery_id;
	}

	if (is_between(PALLADIUM_ID_MIN, PALLADIUM_ID_MAX, battery_id)) {
		goto palladium;
	} else if (is_between(DESAY_5200_ID_MIN, DESAY_5200_ID_MAX,
				battery_id)) {
		goto desay;
	} else {
		pr_warn("invalid battid, palladium 1500 assumed batt_id %llx\n",
				battery_id);
		goto palladium;
	}

palladium:
		chip->fcc = palladium_1500_data.fcc;
		chip->fcc_temp_lut = palladium_1500_data.fcc_temp_lut;
		chip->fcc_sf_lut = palladium_1500_data.fcc_sf_lut;
		chip->pc_temp_ocv_lut = palladium_1500_data.pc_temp_ocv_lut;
		chip->pc_sf_lut = palladium_1500_data.pc_sf_lut;
		chip->rbatt_sf_lut = palladium_1500_data.rbatt_sf_lut;
		chip->default_rbatt_mohm
				= palladium_1500_data.default_rbatt_mohm;
		goto check_lut;
desay:
		chip->fcc = desay_5200_data.fcc;
		chip->fcc_temp_lut = desay_5200_data.fcc_temp_lut;
		chip->pc_temp_ocv_lut = desay_5200_data.pc_temp_ocv_lut;
		chip->pc_sf_lut = desay_5200_data.pc_sf_lut;
		chip->rbatt_sf_lut = desay_5200_data.rbatt_sf_lut;
		chip->default_rbatt_mohm = desay_5200_data.default_rbatt_mohm;
		goto check_lut;
check_lut:
		if (chip->pc_temp_ocv_lut == NULL) {
			pr_err("temp ocv lut table is NULL\n");
			return -EINVAL;
		}
		return 0;
}

#define SPMI_PROP_READ(chip_prop, qpnp_spmi_property, retval)		\
do {									\
	retval = of_property_read_u32(chip->spmi->dev.of_node,		\
				"qcom,bms-" qpnp_spmi_property,		\
					&chip->chip_prop);		\
	if (retval) {							\
		pr_err("Error reading " #qpnp_spmi_property		\
						" property %d\n", rc);	\
		return -EINVAL;						\
	}								\
} while (0)

static inline int bms_read_properties(struct qpnp_bms_chip *chip)
{
	int rc;

	SPMI_PROP_READ(r_sense_mohm, "r-sense-mohm", rc);
	SPMI_PROP_READ(v_cutoff_uv, "v-cutoff-uv", rc);
	SPMI_PROP_READ(max_voltage_uv, "max-voltage-uv", rc);
	SPMI_PROP_READ(r_conn_mohm, "r-conn-mohm", rc);
	SPMI_PROP_READ(chg_term_ua, "chg-term-ua", rc);
	SPMI_PROP_READ(shutdown_soc_valid_limit,
			"shutdown-soc-valid-limit", rc);
	SPMI_PROP_READ(adjust_soc_high_threshold,
			"adjust-soc-high-threshold", rc);
	SPMI_PROP_READ(adjust_soc_low_threshold,
			"adjust-soc-low-threshold", rc);
	SPMI_PROP_READ(batt_type, "batt-type", rc);
	SPMI_PROP_READ(low_soc_calc_threshold,
			"low-soc-calculate-soc-threshold", rc);
	SPMI_PROP_READ(low_soc_calculate_soc_ms,
			"low-soc-calculate-soc-ms", rc);
	SPMI_PROP_READ(calculate_soc_ms, "calculate-soc-ms", rc);
	chip->use_external_rsense = of_property_read_bool(
			chip->spmi->dev.of_node,
			"qcom,bms-use-external-rsense");
	chip->ignore_shutdown_soc = of_property_read_bool(
			chip->spmi->dev.of_node,
			"qcom,bms-ignore-shutdown-soc");
	chip->use_voltage_soc = of_property_read_bool(chip->spmi->dev.of_node,
			"qcom,bms-use-voltage-soc");

	if (chip->adjust_soc_low_threshold >= 45)
		chip->adjust_soc_low_threshold = 45;

	pr_debug("dts data: r_sense_mohm:%d, v_cutoff_uv:%d, max_v:%d\n",
			chip->r_sense_mohm, chip->v_cutoff_uv,
			chip->max_voltage_uv);
	pr_debug("r_conn:%d, shutdown_soc: %d, adjust_soc_low:%d\n",
			chip->r_conn_mohm, chip->shutdown_soc_valid_limit,
			chip->adjust_soc_low_threshold);
	pr_debug("adjust_soc_high:%d, chg_term_ua:%d, batt_type:%d\n",
			chip->adjust_soc_high_threshold, chip->chg_term_ua,
			chip->batt_type);
	pr_debug("ignore_shutdown_soc:%d, use_voltage_soc:%d\n",
			chip->ignore_shutdown_soc, chip->use_voltage_soc);
	pr_debug("use external rsense: %d\n", chip->use_external_rsense);
	return 0;
}

static inline void bms_initialize_constants(struct qpnp_bms_chip *chip)
{
	chip->prev_pc_unusable = -EINVAL;
	chip->soc_at_cv = -EINVAL;
	chip->calculated_soc = -EINVAL;
	chip->last_soc = -EINVAL;
	chip->last_soc_est = -EINVAL;
	chip->last_cc_uah = INT_MIN;
	chip->first_time_calc_soc = 1;
	chip->first_time_calc_uuc = 1;
}

#define REG_OFFSET_PERP_TYPE			0x04
#define REG_OFFSET_PERP_SUBTYPE			0x05
#define BMS_BMS_TYPE				0xD
#define BMS_BMS_SUBTYPE				0x1
#define BMS_IADC_TYPE				0x8
#define BMS_IADC_SUBTYPE			0x3

static int register_spmi(struct qpnp_bms_chip *chip, struct spmi_device *spmi)
{
	struct spmi_resource *spmi_resource;
	struct resource *resource;
	int rc;
	u8 type, subtype;

	chip->dev = &(spmi->dev);
	chip->spmi = spmi;

	spmi_for_each_container_dev(spmi_resource, spmi) {
		if (!spmi_resource) {
			pr_err("qpnp_bms: spmi resource absent\n");
			return -ENXIO;
		}

		resource = spmi_get_resource(spmi, spmi_resource,
						IORESOURCE_MEM, 0);
		if (!(resource && resource->start)) {
			pr_err("node %s IO resource absent!\n",
				spmi->dev.of_node->full_name);
			return -ENXIO;
		}

		rc = qpnp_read_wrapper(chip, &type,
				resource->start + REG_OFFSET_PERP_TYPE, 1);
		if (rc) {
			pr_err("Peripheral type read failed rc=%d\n", rc);
			return rc;
		}
		rc = qpnp_read_wrapper(chip, &subtype,
				resource->start + REG_OFFSET_PERP_SUBTYPE, 1);
		if (rc) {
			pr_err("Peripheral subtype read failed rc=%d\n", rc);
			return rc;
		}

		if (type == BMS_BMS_TYPE && subtype == BMS_BMS_SUBTYPE) {
			chip->base = resource->start;
		} else if (type == BMS_IADC_TYPE
					&& subtype == BMS_IADC_SUBTYPE) {
			chip->iadc_base = resource->start;
		} else {
			pr_err("Invalid peripheral start=0x%x type=0x%x, subtype=0x%x\n",
					resource->start, type, subtype);
		}
	}

	if (chip->base == 0) {
		dev_err(&spmi->dev, "BMS peripheral was not registered\n");
		return -EINVAL;
	}
	if (chip->iadc_base == 0) {
		dev_err(&spmi->dev, "BMS_IADC peripheral was not registered\n");
		return -EINVAL;
	}

	return 0;
}

#define ADC_CH_SEL_MASK			0x7
static int read_iadc_channel_select(struct qpnp_bms_chip *chip)
{
	u8 iadc_channel_select;
	int32_t rds_rsense_nohm;
	int rc;

	rc = qpnp_read_wrapper(chip, &iadc_channel_select,
			chip->iadc_base + IADC1_BMS_ADC_CH_SEL_CTL, 1);
	if (rc) {
		pr_err("Error reading bms_iadc channel register %d\n", rc);
		return rc;
	}

	iadc_channel_select &= ADC_CH_SEL_MASK;
	if (iadc_channel_select != EXTERNAL_RSENSE
			&& iadc_channel_select != INTERNAL_RSENSE) {
		pr_err("IADC1_BMS_IADC configured incorrectly. Selected channel = %d\n",
						iadc_channel_select);
		return -EINVAL;
	}

	if (chip->use_external_rsense) {
		pr_debug("External rsense selected\n");
		if (iadc_channel_select == INTERNAL_RSENSE) {
			pr_debug("Internal rsense detected; Changing rsense to external\n");
			rc = qpnp_masked_write_iadc(chip,
					IADC1_BMS_ADC_CH_SEL_CTL,
					ADC_CH_SEL_MASK,
					EXTERNAL_RSENSE);
			if (rc) {
				pr_err("Unable to set IADC1_BMS channel %x to %x: %d\n",
						IADC1_BMS_ADC_CH_SEL_CTL,
						EXTERNAL_RSENSE, rc);
				return rc;
			}
			reset_cc(chip);
		}
	} else {
		pr_debug("Internal rsense selected\n");
		if (iadc_channel_select == EXTERNAL_RSENSE) {
			pr_debug("External rsense detected; Changing rsense to internal\n");
			rc = qpnp_masked_write_iadc(chip,
					IADC1_BMS_ADC_CH_SEL_CTL,
					ADC_CH_SEL_MASK,
					INTERNAL_RSENSE);
			if (rc) {
				pr_err("Unable to set IADC1_BMS channel %x to %x: %d\n",
						IADC1_BMS_ADC_CH_SEL_CTL,
						INTERNAL_RSENSE, rc);
				return rc;
			}
			reset_cc(chip);
		}

		rc = qpnp_iadc_get_rsense(&rds_rsense_nohm);
		if (rc) {
			pr_err("Unable to read RDS resistance value from IADC; rc = %d\n",
								rc);
			return rc;
		}
		chip->r_sense_mohm = rds_rsense_nohm/1000000;
		pr_debug("rds_rsense = %d nOhm, saved as %d mOhm\n",
					rds_rsense_nohm, chip->r_sense_mohm);
	}
	return 0;
}

static int __devinit qpnp_bms_probe(struct spmi_device *spmi)
{
	struct qpnp_bms_chip *chip;
	int rc, vbatt;

	chip = kzalloc(sizeof *chip, GFP_KERNEL);

	if (chip == NULL) {
		pr_err("kzalloc() failed.\n");
		return -ENOMEM;
	}

	rc = qpnp_vadc_is_ready();
	if (rc) {
		pr_info("vadc not ready: %d, deferring probe\n", rc);
		goto error_read;
	}

	rc = qpnp_iadc_is_ready();
	if (rc) {
		pr_info("iadc not ready: %d, deferring probe\n", rc);
		goto error_read;
	}

	rc = register_spmi(chip, spmi);
	if (rc) {
		pr_err("error registering spmi resource %d\n", rc);
		goto error_resource;
	}

	rc = qpnp_read_wrapper(chip, &chip->revision1,
			chip->base + BMS1_REVISION1, 1);
	if (rc) {
		pr_err("error reading version register %d\n", rc);
		goto error_read;
	}

	rc = qpnp_read_wrapper(chip, &chip->revision2,
			chip->base + BMS1_REVISION2, 1);
	if (rc) {
		pr_err("Error reading version register %d\n", rc);
		goto error_read;
	}
	pr_debug("BMS version: %hhu.%hhu\n", chip->revision2, chip->revision1);

	rc = bms_read_properties(chip);
	if (rc) {
		pr_err("Unable to read all bms properties, rc = %d\n", rc);
		goto error_read;
	}

	rc = read_iadc_channel_select(chip);
	if (rc) {
		pr_err("Unable to get iadc selected channel = %d\n", rc);
		goto error_read;
	}

	rc = set_battery_data(chip);
	if (rc) {
		pr_err("Bad battery data %d\n", rc);
		goto error_read;
	}

	bms_initialize_constants(chip);

	mutex_init(&chip->bms_output_lock);
	mutex_init(&chip->last_ocv_uv_mutex);
	mutex_init(&chip->soc_invalidation_mutex);

	INIT_DELAYED_WORK(&chip->calculate_soc_delayed_work,
			calculate_soc_work);

	read_shutdown_soc_and_iavg(chip);

	dev_set_drvdata(&spmi->dev, chip);
	device_init_wakeup(&spmi->dev, 1);

	calculate_soc_work(&(chip->calculate_soc_delayed_work.work));

	/* setup & register the battery power supply */
	chip->bms_psy.name = "bms";
	chip->bms_psy.type = POWER_SUPPLY_TYPE_BMS;
	chip->bms_psy.properties = msm_bms_power_props;
	chip->bms_psy.num_properties = ARRAY_SIZE(msm_bms_power_props);
	chip->bms_psy.get_property = qpnp_bms_power_get_property;
	chip->bms_psy.set_property = qpnp_bms_power_set_property;
	chip->bms_psy.external_power_changed =
		qpnp_bms_external_power_changed;
	chip->bms_psy.supplied_to = qpnp_bms_supplicants;
	chip->bms_psy.num_supplicants = ARRAY_SIZE(qpnp_bms_supplicants);

	rc = power_supply_register(chip->dev, &chip->bms_psy);

	if (rc < 0) {
		pr_err("power_supply_register bms failed rc = %d\n", rc);
		goto unregister_dc;
	}

	vbatt = 0;
	get_battery_voltage(&vbatt);

	pr_info("probe success: soc =%d vbatt = %d ocv = %d r_sense_mohm = %u\n",
				get_prop_bms_capacity(chip),
				vbatt, chip->last_ocv_uv, chip->r_sense_mohm);
	return 0;

unregister_dc:
	power_supply_unregister(&chip->bms_psy);
	dev_set_drvdata(&spmi->dev, NULL);
error_resource:
error_read:
	kfree(chip);
	return rc;
}

static int __devexit
qpnp_bms_remove(struct spmi_device *spmi)
{
	struct qpnp_bms_chip *chip = dev_get_drvdata(&spmi->dev);

	dev_set_drvdata(&spmi->dev, NULL);
	kfree(chip);
	return 0;
}

static struct spmi_driver qpnp_bms_driver = {
	.probe		= qpnp_bms_probe,
	.remove		= __devexit_p(qpnp_bms_remove),
	.driver		= {
		.name		= QPNP_BMS_DEV_NAME,
		.owner		= THIS_MODULE,
		.of_match_table	= qpnp_bms_match_table,
	},
};

static int __init qpnp_bms_init(void)
{
	pr_info("QPNP BMS INIT\n");
	return spmi_driver_register(&qpnp_bms_driver);
}

static void __exit qpnp_bms_exit(void)
{
	pr_info("QPNP BMS EXIT\n");
	return spmi_driver_unregister(&qpnp_bms_driver);
}

module_init(qpnp_bms_init);
module_exit(qpnp_bms_exit);

MODULE_DESCRIPTION("QPNP BMS Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" QPNP_BMS_DEV_NAME);
