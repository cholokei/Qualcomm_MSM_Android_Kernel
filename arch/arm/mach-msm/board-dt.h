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

extern struct sys_timer msm_dt_timer;
void __init msm_dt_init_irq(void);
void __init msm_dt_init_irq_nompm(void);
void __init msm_dt_init_irq_l2x0(void);
extern int __init msm_gpio_of_init(struct device_node *,
				struct device_node *);
