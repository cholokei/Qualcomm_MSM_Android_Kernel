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

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/iommu.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

#include <mach/iommu_hw-v2.h>
#include <mach/iommu.h>
#include <mach/iommu_perfmon.h>

static int msm_iommu_parse_bfb_settings(struct platform_device *pdev,
				    struct msm_iommu_drvdata *drvdata)
{
	struct msm_iommu_bfb_settings *bfb_settings;
	u32 nreg, nval;
	int ret, i;

	/*
	 * It is not valid for a device to have the qcom,iommu-bfb-regs
	 * property but not the qcom,iommu-bfb-data property, and vice versa.
	 */
	if (!of_get_property(pdev->dev.of_node, "qcom,iommu-bfb-regs", &nreg)) {
		if (of_get_property(pdev->dev.of_node, "qcom,iommu-bfb-data",
				    &nval))
			return -EINVAL;
		return 0;
	}

	if (!of_get_property(pdev->dev.of_node, "qcom,iommu-bfb-data", &nval))
		return -EINVAL;

	if (nreg >= sizeof(bfb_settings->regs))
		return -EINVAL;

	if (nval >= sizeof(bfb_settings->data))
		return -EINVAL;

	if (nval != nreg)
		return -EINVAL;

	bfb_settings = devm_kzalloc(&pdev->dev, sizeof(*bfb_settings),
				    GFP_KERNEL);
	if (!bfb_settings)
		return -ENOMEM;

	ret = of_property_read_u32_array(pdev->dev.of_node,
					 "qcom,iommu-bfb-regs",
					 bfb_settings->regs,
					 nreg / sizeof(*bfb_settings->regs));
	if (ret)
		return ret;

	ret = of_property_read_u32_array(pdev->dev.of_node,
					 "qcom,iommu-bfb-data",
					 bfb_settings->data,
					 nval / sizeof(*bfb_settings->data));
	if (ret)
		return ret;

	bfb_settings->length = nreg / sizeof(*bfb_settings->regs);

	for (i = 0; i < bfb_settings->length; i++)
		if (bfb_settings->regs[i] < IMPLDEF_OFFSET ||
		    bfb_settings->regs[i] >= IMPLDEF_OFFSET + IMPLDEF_LENGTH)
			return -EINVAL;

	drvdata->bfb_settings = bfb_settings;
	return 0;
}

static int msm_iommu_parse_dt(struct platform_device *pdev,
				struct msm_iommu_drvdata *drvdata)
{
	struct device_node *child;
	int ret = 0;
	struct resource *r;

	drvdata->dev = &pdev->dev;
	msm_iommu_add_drv(drvdata);

	ret = msm_iommu_parse_bfb_settings(pdev, drvdata);
	if (ret)
		goto fail;

	for_each_child_of_node(pdev->dev.of_node, child) {
		drvdata->ncb++;
		if (!of_platform_device_create(child, NULL, &pdev->dev))
			pr_err("Failed to create %s device\n", child->name);
	}

	ret = of_property_read_string(pdev->dev.of_node, "label",
				      &drvdata->name);
	if (ret)
		goto fail;

	drvdata->sec_id = -1;
	of_property_read_u32(pdev->dev.of_node, "qcom,iommu-secure-id",
				&drvdata->sec_id);

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "clk_base");
	if (r) {
		drvdata->clk_reg_virt = devm_ioremap(&pdev->dev, r->start,
						     resource_size(r));
		if (!drvdata->clk_reg_virt) {
			pr_err("Failed to map 0x%x for iommu clk\n",
				r->start);
			ret = -ENOMEM;
			goto fail;
		}
	}

	return 0;
fail:
	return ret;
}

static int msm_iommu_pmon_parse_dt(struct platform_device *pdev,
					struct iommu_info *pmon_info)
{
	int ret = 0;
	int irq = platform_get_irq(pdev, 0);

	if (irq > 0) {
		pmon_info->evt_irq = platform_get_irq(pdev, 0);
	} else {
		pmon_info->evt_irq = -1;
		ret = irq;
	}
	return ret;
}

static int __devinit msm_iommu_probe(struct platform_device *pdev)
{
	struct iommu_info *pmon_info;
	struct msm_iommu_drvdata *drvdata;
	struct resource *r;
	int ret, needs_alt_core_clk;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	r = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iommu_base");
	if (!r)
		return -EINVAL;

	drvdata->base = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!drvdata->base)
		return -ENOMEM;

	drvdata->glb_base = drvdata->base;

	drvdata->gdsc = devm_regulator_get(&pdev->dev, "vdd");
	if (IS_ERR(drvdata->gdsc))
		return -EINVAL;

	drvdata->alt_gdsc = devm_regulator_get(&pdev->dev, "qcom,alt-vdd");
	if (IS_ERR(drvdata->alt_gdsc))
		drvdata->alt_gdsc = NULL;

	drvdata->pclk = devm_clk_get(&pdev->dev, "iface_clk");
	if (IS_ERR(drvdata->pclk))
		return PTR_ERR(drvdata->pclk);

	drvdata->clk = devm_clk_get(&pdev->dev, "core_clk");
	if (IS_ERR(drvdata->clk))
		return PTR_ERR(drvdata->clk);

	needs_alt_core_clk = of_property_read_bool(pdev->dev.of_node,
						   "qcom,needs-alt-core-clk");
	if (needs_alt_core_clk) {
		drvdata->aclk = devm_clk_get(&pdev->dev, "alt_core_clk");
		if (IS_ERR(drvdata->aclk))
			return PTR_ERR(drvdata->aclk);
	}

	if (clk_get_rate(drvdata->clk) == 0) {
		ret = clk_round_rate(drvdata->clk, 1);
		clk_set_rate(drvdata->clk, ret);
	}

	if (drvdata->aclk && clk_get_rate(drvdata->aclk) == 0) {
		ret = clk_round_rate(drvdata->aclk, 1);
		clk_set_rate(drvdata->aclk, ret);
	}

	ret = msm_iommu_parse_dt(pdev, drvdata);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "device %s mapped at %p, with %d ctx banks\n",
		drvdata->name, drvdata->base, drvdata->ncb);

	platform_set_drvdata(pdev, drvdata);

	pmon_info = msm_iommu_pm_alloc(&pdev->dev);
	if (pmon_info != NULL) {
		ret = msm_iommu_pmon_parse_dt(pdev, pmon_info);
		if (ret) {
			msm_iommu_pm_free(&pdev->dev);
			pr_info("%s: pmon not available.\n", drvdata->name);
		} else {
			pmon_info->base = drvdata->base;
			pmon_info->ops = &iommu_access_ops;
			pmon_info->iommu_name = drvdata->name;
			ret = msm_iommu_pm_iommu_register(pmon_info);
			if (ret) {
				pr_err("%s iommu register fail\n",
								drvdata->name);
				msm_iommu_pm_free(&pdev->dev);
			} else {
				pr_debug("%s iommu registered for pmon\n",
						pmon_info->iommu_name);
			}
		}
	}
	return 0;
}

static int __devexit msm_iommu_remove(struct platform_device *pdev)
{
	struct msm_iommu_drvdata *drv = NULL;

	msm_iommu_pm_iommu_unregister(&pdev->dev);
	msm_iommu_pm_free(&pdev->dev);

	drv = platform_get_drvdata(pdev);
	if (drv) {
		msm_iommu_remove_drv(drv);
		if (drv->clk)
			clk_put(drv->clk);
		clk_put(drv->pclk);
		platform_set_drvdata(pdev, NULL);
	}
	return 0;
}

static int msm_iommu_ctx_parse_dt(struct platform_device *pdev,
				struct msm_iommu_ctx_drvdata *ctx_drvdata)
{
	struct resource *r, rp;
	int irq, ret;
	u32 nsid;

	irq = platform_get_irq(pdev, 0);
	if (irq > 0) {
		ret = request_threaded_irq(irq, NULL,
				msm_iommu_fault_handler_v2,
				IRQF_ONESHOT | IRQF_SHARED,
				"msm_iommu_nonsecure_irq", pdev);
		if (ret) {
			pr_err("Request IRQ %d failed with ret=%d\n", irq, ret);
			return ret;
		}
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r)
		return -EINVAL;

	ret = of_address_to_resource(pdev->dev.parent->of_node, 0, &rp);
	if (ret)
		return -EINVAL;

	/* Calculate the context bank number using the base addresses. The
	 * first 8 pages belong to the global address space which is followed
	 * by the context banks, hence subtract by 8 to get the context bank
	 * number.
	 */
	ctx_drvdata->num = ((r->start - rp.start) >> CTX_SHIFT) - 8;

	if (of_property_read_string(pdev->dev.of_node, "label",
					&ctx_drvdata->name))
		ctx_drvdata->name = dev_name(&pdev->dev);

	if (!of_get_property(pdev->dev.of_node, "qcom,iommu-ctx-sids", &nsid))
		return -EINVAL;

	if (nsid >= sizeof(ctx_drvdata->sids))
		return -EINVAL;

	if (of_property_read_u32_array(pdev->dev.of_node, "qcom,iommu-ctx-sids",
				       ctx_drvdata->sids,
				       nsid / sizeof(*ctx_drvdata->sids))) {
		return -EINVAL;
	}
	ctx_drvdata->nsid = nsid;

	ctx_drvdata->secure_context = of_property_read_bool(pdev->dev.of_node,
							"qcom,secure-context");
	ctx_drvdata->asid = -1;
	return 0;
}

static int __devinit msm_iommu_ctx_probe(struct platform_device *pdev)
{
	struct msm_iommu_ctx_drvdata *ctx_drvdata = NULL;
	int ret;

	if (!pdev->dev.parent)
		return -EINVAL;

	ctx_drvdata = devm_kzalloc(&pdev->dev, sizeof(*ctx_drvdata),
					GFP_KERNEL);
	if (!ctx_drvdata)
		return -ENOMEM;

	ctx_drvdata->pdev = pdev;
	INIT_LIST_HEAD(&ctx_drvdata->attached_elm);
	platform_set_drvdata(pdev, ctx_drvdata);

	ret = msm_iommu_ctx_parse_dt(pdev, ctx_drvdata);
	if (!ret)
		dev_info(&pdev->dev, "context %s using bank %d\n",
			 ctx_drvdata->name, ctx_drvdata->num);

	return ret;
}

static int __devexit msm_iommu_ctx_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct of_device_id msm_iommu_match_table[] = {
	{ .compatible = "qcom,msm-smmu-v2", },
	{}
};

static struct platform_driver msm_iommu_driver = {
	.driver = {
		.name	= "msm_iommu_v2",
		.of_match_table = msm_iommu_match_table,
	},
	.probe		= msm_iommu_probe,
	.remove		= __devexit_p(msm_iommu_remove),
};

static struct of_device_id msm_iommu_ctx_match_table[] = {
	{ .name = "qcom,iommu-ctx", },
	{}
};

static struct platform_driver msm_iommu_ctx_driver = {
	.driver = {
		.name	= "msm_iommu_ctx_v2",
		.of_match_table = msm_iommu_ctx_match_table,
	},
	.probe		= msm_iommu_ctx_probe,
	.remove		= __devexit_p(msm_iommu_ctx_remove),
};

static int __init msm_iommu_driver_init(void)
{
	int ret;

	ret = platform_driver_register(&msm_iommu_driver);
	if (ret != 0) {
		pr_err("Failed to register IOMMU driver\n");
		goto error;
	}

	ret = platform_driver_register(&msm_iommu_ctx_driver);
	if (ret != 0) {
		pr_err("Failed to register IOMMU context driver\n");
		goto error;
	}

error:
	return ret;
}

static void __exit msm_iommu_driver_exit(void)
{
	platform_driver_unregister(&msm_iommu_ctx_driver);
	platform_driver_unregister(&msm_iommu_driver);
}

subsys_initcall(msm_iommu_driver_init);
module_exit(msm_iommu_driver_exit);

MODULE_LICENSE("GPL v2");
