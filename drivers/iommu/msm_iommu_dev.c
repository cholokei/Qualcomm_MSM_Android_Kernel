/* Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
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
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

#include <mach/iommu_hw-8xxx.h>
#include <mach/iommu.h>

static DEFINE_MUTEX(iommu_list_lock);
static LIST_HEAD(iommu_list);

void msm_iommu_add_drv(struct msm_iommu_drvdata *drv)
{
	mutex_lock(&iommu_list_lock);
	list_add(&drv->list, &iommu_list);
	mutex_unlock(&iommu_list_lock);
}

void msm_iommu_remove_drv(struct msm_iommu_drvdata *drv)
{
	mutex_lock(&iommu_list_lock);
	list_del(&drv->list);
	mutex_unlock(&iommu_list_lock);
}

static int find_iommu_ctx(struct device *dev, void *data)
{
	struct msm_iommu_ctx_drvdata *c;

	c = dev_get_drvdata(dev);
	if (!c || !c->name)
		return 0;

	return !strcmp(data, c->name);
}

static struct device *find_context(struct device *dev, const char *name)
{
	return device_find_child(dev, (void *)name, find_iommu_ctx);
}

struct device *msm_iommu_get_ctx(const char *ctx_name)
{
	struct msm_iommu_drvdata *drv;
	struct device *dev = NULL;

	mutex_lock(&iommu_list_lock);
	list_for_each_entry(drv, &iommu_list, list) {
		dev = find_context(drv->dev, ctx_name);
		if (dev)
			break;
	}
	mutex_unlock(&iommu_list_lock);

	if (!dev || !dev_get_drvdata(dev))
		pr_err("Could not find context <%s>\n", ctx_name);
	put_device(dev);

	return dev;
}
EXPORT_SYMBOL(msm_iommu_get_ctx);

static void msm_iommu_reset(void __iomem *base, void __iomem *glb_base, int ncb)
{
	int ctx;

	SET_RPUE(glb_base, 0);
	SET_RPUEIE(glb_base, 0);
	SET_ESRRESTORE(glb_base, 0);
	SET_TBE(glb_base, 0);
	SET_CR(glb_base, 0);
	SET_SPDMBE(glb_base, 0);
	SET_TESTBUSCR(glb_base, 0);
	SET_TLBRSW(glb_base, 0);
	SET_GLOBAL_TLBIALL(glb_base, 0);
	SET_RPU_ACR(glb_base, 0);
	SET_TLBLKCRWE(glb_base, 1);

	for (ctx = 0; ctx < ncb; ctx++) {
		SET_BPRCOSH(glb_base, ctx, 0);
		SET_BPRCISH(glb_base, ctx, 0);
		SET_BPRCNSH(glb_base, ctx, 0);
		SET_BPSHCFG(glb_base, ctx, 0);
		SET_BPMTCFG(glb_base, ctx, 0);
		SET_ACTLR(base, ctx, 0);
		SET_SCTLR(base, ctx, 0);
		SET_FSRRESTORE(base, ctx, 0);
		SET_TTBR0(base, ctx, 0);
		SET_TTBR1(base, ctx, 0);
		SET_TTBCR(base, ctx, 0);
		SET_BFBCR(base, ctx, 0);
		SET_PAR(base, ctx, 0);
		SET_FAR(base, ctx, 0);
		SET_TLBFLPTER(base, ctx, 0);
		SET_TLBSLPTER(base, ctx, 0);
		SET_TLBLKCR(base, ctx, 0);
		SET_CTX_TLBIALL(base, ctx, 0);
		SET_TLBIVA(base, ctx, 0);
		SET_PRRR(base, ctx, 0);
		SET_NMRR(base, ctx, 0);
		SET_CONTEXTIDR(base, ctx, 0);
	}
	mb();
}

static int msm_iommu_parse_dt(struct platform_device *pdev,
				struct msm_iommu_drvdata *drvdata)
{
#ifdef CONFIG_OF_DEVICE
	struct device_node *child;
	struct resource *r;
	u32 glb_offset = 0;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		pr_err("%s: Missing property reg\n", __func__);
		return -EINVAL;
	}
	drvdata->base = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!drvdata->base) {
		pr_err("%s: Unable to ioremap address %x size %x\n", __func__,
			r->start, resource_size(r));
		return -ENOMEM;
	}
	drvdata->glb_base = drvdata->base;

	if (!of_property_read_u32(pdev->dev.of_node, "qcom,glb-offset",
			&glb_offset)) {
		drvdata->glb_base += glb_offset;
	} else {
		pr_err("%s: Missing property qcom,glb-offset\n", __func__);
		return -EINVAL;
	}

	for_each_child_of_node(pdev->dev.of_node, child) {
		drvdata->ncb++;
		if (!of_platform_device_create(child, NULL, &pdev->dev))
			pr_err("Failed to create %s device\n", child->name);
	}

	drvdata->name = dev_name(&pdev->dev);
	drvdata->sec_id = -1;
	drvdata->ttbr_split = 0;
#endif
	return 0;
}

static int __get_clocks(struct platform_device *pdev,
				 struct msm_iommu_drvdata *drvdata)
{
	int ret = 0;

	drvdata->pclk = clk_get(&pdev->dev, "iface_clk");
	if (IS_ERR(drvdata->pclk)) {
		ret = PTR_ERR(drvdata->pclk);
		drvdata->pclk = NULL;
		pr_err("Unable to get %s clock for %s IOMMU device\n",
			dev_name(&pdev->dev), drvdata->name);
		goto fail;
	}

	drvdata->clk = clk_get(&pdev->dev, "core_clk");

	if (!IS_ERR(drvdata->clk)) {
		if (clk_get_rate(drvdata->clk) == 0) {
			ret = clk_round_rate(drvdata->clk, 1000);
			clk_set_rate(drvdata->clk, ret);
		}
	} else {
		drvdata->clk = NULL;
	}
	return 0;
fail:
	return ret;
}

static void __put_clocks(struct msm_iommu_drvdata *drvdata)
{
	if (drvdata->clk)
		clk_put(drvdata->clk);
	clk_put(drvdata->pclk);
}

static int __enable_clocks(struct msm_iommu_drvdata *drvdata)
{
	int ret;

	ret = clk_prepare_enable(drvdata->pclk);
	if (ret)
		goto fail;

	if (drvdata->clk) {
		ret = clk_prepare_enable(drvdata->clk);
		if (ret)
			clk_disable_unprepare(drvdata->pclk);
	}
fail:
	return ret;
}

static void __disable_clocks(struct msm_iommu_drvdata *drvdata)
{
	if (drvdata->clk)
		clk_disable_unprepare(drvdata->clk);
	clk_disable_unprepare(drvdata->pclk);
}

/*
 * Do a basic check of the IOMMU by performing an ATS operation
 * on context bank 0.
 */
static int iommu_sanity_check(struct msm_iommu_drvdata *drvdata)
{
	int par;
	int ret = 0;

	SET_M(drvdata->base, 0, 1);
	SET_PAR(drvdata->base, 0, 0);
	SET_V2PCFG(drvdata->base, 0, 1);
	SET_V2PPR(drvdata->base, 0, 0);
	mb();
	par = GET_PAR(drvdata->base, 0);
	SET_V2PCFG(drvdata->base, 0, 0);
	SET_M(drvdata->base, 0, 0);
	mb();

	if (!par) {
		pr_err("%s: Invalid PAR value detected\n", drvdata->name);
		ret = -ENODEV;
	}
	return ret;
}

static int msm_iommu_probe(struct platform_device *pdev)
{
	struct msm_iommu_drvdata *drvdata;
	struct msm_iommu_dev *iommu_dev = pdev->dev.platform_data;
	int ret;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);

	if (!drvdata) {
		ret = -ENOMEM;
		goto fail;
	}

	if (pdev->dev.of_node) {
		ret = msm_iommu_parse_dt(pdev, drvdata);
		if (ret)
			goto fail;
	} else if (pdev->dev.platform_data) {
		struct resource *r, *r2;
		resource_size_t	len;

		r = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"physbase");

		if (!r) {
			ret = -ENODEV;
			goto fail;
		}

		len = resource_size(r);

		r2 = request_mem_region(r->start, len, r->name);
		if (!r2) {
			pr_err("Could not request memory region: start=%p, len=%d\n",
							(void *) r->start, len);
			ret = -EBUSY;
			goto fail;
		}

		drvdata->base = devm_ioremap(&pdev->dev, r2->start, len);

		if (!drvdata->base) {
			pr_err("Could not ioremap: start=%p, len=%d\n",
				 (void *) r2->start, len);
			ret = -EBUSY;
			goto fail;
		}
		/*
		 * Global register space offset for legacy IOMMUv1 hardware
		 * is always 0xFF000
		 */
		drvdata->glb_base = drvdata->base + 0xFF000;
		drvdata->name = iommu_dev->name;
		drvdata->dev = &pdev->dev;
		drvdata->ncb = iommu_dev->ncb;
		drvdata->ttbr_split = iommu_dev->ttbr_split;
	} else {
		ret = -ENODEV;
		goto fail;
	}

	drvdata->dev = &pdev->dev;

	ret = __get_clocks(pdev, drvdata);

	if (ret)
		goto fail;

	__enable_clocks(drvdata);

	msm_iommu_reset(drvdata->base, drvdata->glb_base, drvdata->ncb);

	ret = iommu_sanity_check(drvdata);
	if (ret)
		goto fail_clk;

	pr_info("device %s mapped at %p, with %d ctx banks\n",
		drvdata->name, drvdata->base, drvdata->ncb);

	msm_iommu_add_drv(drvdata);
	platform_set_drvdata(pdev, drvdata);

	__disable_clocks(drvdata);

	return 0;

fail_clk:
	__disable_clocks(drvdata);
	__put_clocks(drvdata);
fail:
	return ret;
}

static int msm_iommu_remove(struct platform_device *pdev)
{
	struct msm_iommu_drvdata *drv = NULL;

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
	u32 nmid_array_size;
	u32 nmid;

	irq = platform_get_irq(pdev, 0);
	if (irq > 0) {
		ret = request_threaded_irq(irq, NULL,
				msm_iommu_fault_handler,
				IRQF_ONESHOT | IRQF_SHARED,
				"msm_iommu_nonsecure_irq", pdev);
		if (ret) {
			pr_err("Request IRQ %d failed with ret=%d\n", irq, ret);
			return ret;
		}
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		pr_err("Could not find reg property for context bank\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(pdev->dev.parent->of_node, 0, &rp);
	if (ret) {
		pr_err("of_address_to_resource failed\n");
		return -EINVAL;
	}

	/* Calculate the context bank number using the base addresses. CB0
	 * starts at the base address.
	 */
	ctx_drvdata->num = ((r->start - rp.start) >> CTX_SHIFT);

	if (of_property_read_string(pdev->dev.of_node, "label",
					&ctx_drvdata->name)) {
		pr_err("Could not find label property\n");
		return -EINVAL;
	}

	if (!of_get_property(pdev->dev.of_node, "qcom,iommu-ctx-mids",
			     &nmid_array_size)) {
		pr_err("Could not find iommu-ctx-mids property\n");
		return -EINVAL;
	}
	if (nmid_array_size >= sizeof(ctx_drvdata->sids)) {
		pr_err("Too many mids defined - array size: %u, mids size: %u\n",
			nmid_array_size, sizeof(ctx_drvdata->sids));
		return -EINVAL;
	}
	nmid = nmid_array_size / sizeof(*ctx_drvdata->sids);

	if (of_property_read_u32_array(pdev->dev.of_node, "qcom,iommu-ctx-mids",
				       ctx_drvdata->sids, nmid)) {
		pr_err("Could not find iommu-ctx-mids property\n");
		return -EINVAL;
	}
	ctx_drvdata->nsid = nmid;

	return 0;
}

static void __program_m2v_tables(struct msm_iommu_drvdata *drvdata,
				struct msm_iommu_ctx_drvdata *ctx_drvdata)
{
	int i;

	/* Program the M2V tables for this context */
	for (i = 0; i < ctx_drvdata->nsid; i++) {
		int sid = ctx_drvdata->sids[i];
		int num = ctx_drvdata->num;

		SET_M2VCBR_N(drvdata->glb_base, sid, 0);
		SET_CBACR_N(drvdata->glb_base, num, 0);

		/* Route page faults to the non-secure interrupt */
		SET_IRPTNDX(drvdata->glb_base, num, 1);

		/* Set VMID = 0 */
		SET_VMID(drvdata->glb_base, sid, 0);

		/* Set the context number for that SID to this context */
		SET_CBNDX(drvdata->glb_base, sid, num);

		/* Set SID associated with this context bank to 0 */
		SET_CBVMID(drvdata->glb_base, num, 0);

		/* Set the ASID for TLB tagging for this context to 0 */
		SET_CONTEXTIDR_ASID(drvdata->base, num, 0);

		/* Set security bit override to be Non-secure */
		SET_NSCFG(drvdata->glb_base, sid, 3);
	}
	mb();
}

static int msm_iommu_ctx_probe(struct platform_device *pdev)
{
	struct msm_iommu_drvdata *drvdata;
	struct msm_iommu_ctx_drvdata *ctx_drvdata = NULL;
	int i, ret, irq;
	if (!pdev->dev.parent) {
		ret = -EINVAL;
		goto fail;
	}

	drvdata = dev_get_drvdata(pdev->dev.parent);

	if (!drvdata) {
		ret = -ENODEV;
		goto fail;
	}

	ctx_drvdata = devm_kzalloc(&pdev->dev, sizeof(*ctx_drvdata),
					GFP_KERNEL);
	if (!ctx_drvdata) {
		ret = -ENOMEM;
		goto fail;
	}

	ctx_drvdata->pdev = pdev;
	INIT_LIST_HEAD(&ctx_drvdata->attached_elm);
	platform_set_drvdata(pdev, ctx_drvdata);
	ctx_drvdata->attach_count = 0;

	if (pdev->dev.of_node) {
		ret = msm_iommu_ctx_parse_dt(pdev, ctx_drvdata);
		if (ret)
			goto fail;
	} else if (pdev->dev.platform_data) {
		struct msm_iommu_ctx_dev *c = pdev->dev.platform_data;

		ctx_drvdata->num = c->num;
		ctx_drvdata->name = c->name;

		for (i = 0;  i < MAX_NUM_MIDS; ++i) {
			if (c->mids[i] == -1) {
				ctx_drvdata->nsid = i;
				break;
			}
			ctx_drvdata->sids[i] = c->mids[i];
		}
		irq = platform_get_irq_byname(
					to_platform_device(pdev->dev.parent),
					"nonsecure_irq");
		if (irq < 0) {
			ret = -ENODEV;
			goto fail;
		}

		ret = request_threaded_irq(irq, NULL, msm_iommu_fault_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					"msm_iommu_nonsecure_irq", ctx_drvdata);

		if (ret) {
			pr_err("request_threaded_irq %d failed: %d\n", irq,
								       ret);
			goto fail;
		}
	} else {
		ret = -ENODEV;
		goto fail;
	}

	__enable_clocks(drvdata);
	__program_m2v_tables(drvdata, ctx_drvdata);
	__disable_clocks(drvdata);

	dev_info(&pdev->dev, "context %s using bank %d\n", ctx_drvdata->name,
							   ctx_drvdata->num);
	return 0;
fail:
	return ret;
}

static int __devexit msm_iommu_ctx_remove(struct platform_device *pdev)
{
	platform_set_drvdata(pdev, NULL);
	return 0;
}


static struct of_device_id msm_iommu_match_table[] = {
	{ .compatible = "qcom,msm-smmu-v1", },
	{}
};

static struct platform_driver msm_iommu_driver = {
	.driver = {
		.name	= "msm_iommu",
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
		.name	= "msm_iommu_ctx",
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
MODULE_AUTHOR("Stepan Moskovchenko <stepanm@codeaurora.org>");
