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

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/memory.h>
#include <asm/mach/map.h>
#include <asm/arch_timer.h>
#include <asm/hardware/gic.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <mach/board.h>
#include <mach/gpiomux.h>
#include <mach/msm_iomap.h>
#include <mach/restart.h>
#ifdef CONFIG_ION_MSM
#include <mach/ion.h>
#endif
#include <mach/msm_memtypes.h>
#include <mach/socinfo.h>
#include <mach/board.h>
#include <mach/clk-provider.h>
#include "board-dt.h"
#include "clock.h"
#include "platsmp.h"

static struct memtype_reserve msm8610_reserve_table[] __initdata = {
	[MEMTYPE_SMI] = {
	},
	[MEMTYPE_EBI0] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
	[MEMTYPE_EBI1] = {
		.flags	=	MEMTYPE_FLAGS_1M_ALIGN,
	},
};

static int msm8610_paddr_to_memtype(unsigned int paddr)
{
	return MEMTYPE_EBI1;
}

static struct of_dev_auxdata msm8610_auxdata_lookup[] __initdata = {
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF9824000, \
			"msm_sdcc.1", NULL),
	OF_DEV_AUXDATA("qcom,msm-sdcc", 0xF98A4000, \
			"msm_sdcc.2", NULL),
	{}
};

static struct reserve_info msm8610_reserve_info __initdata = {
	.memtype_reserve_table = msm8610_reserve_table,
	.paddr_to_memtype = msm8610_paddr_to_memtype,
};

static void __init msm8610_early_memory(void)
{
	reserve_info = &msm8610_reserve_info;
	of_scan_flat_dt(dt_scan_for_memory_reserve, msm8610_reserve_table);
}

static void __init msm8610_reserve(void)
{
	msm_reserve();
}

void __init msm8610_init(void)
{
	struct of_dev_auxdata *adata = msm8610_auxdata_lookup;

	if (socinfo_init() < 0)
		pr_err("%s: socinfo_init() failed\n", __func__);

	msm8610_init_gpiomux();

	if (machine_is_msm8610_rumi())
		msm_clock_init(&msm8610_rumi_clock_init_data);
	else
		msm_clock_init(&msm8610_clock_init_data);
	of_platform_populate(NULL, of_default_bus_match_table, adata, NULL);
}

static const char *msm8610_dt_match[] __initconst = {
	"qcom,msm8610",
	NULL
};

DT_MACHINE_START(MSM8610_DT, "Qualcomm MSM 8610 (Flattened Device Tree)")
	.map_io = msm_map_msm8610_io,
	.init_irq = msm_dt_init_irq_nompm,
	.init_machine = msm8610_init,
	.handle_irq = gic_handle_irq,
	.timer = &msm_dt_timer,
	.dt_compat = msm8610_dt_match,
	.restart = msm_restart,
	.reserve = msm8610_reserve,
	.init_very_early = msm8610_early_memory,
	.smp = &arm_smp_ops,
MACHINE_END
