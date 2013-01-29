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
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/iommu.h>
#include <linux/clk.h>
#include <linux/scatterlist.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/sizes.h>

#include <mach/iommu_hw-v2.h>
#include <mach/iommu.h>
#include <mach/scm.h>

/* bitmap of the page sizes currently supported */
#define MSM_IOMMU_PGSIZES	(SZ_4K | SZ_64K | SZ_1M | SZ_16M)

#define IOMMU_SECURE_CFG	2
#define IOMMU_SECURE_PTBL_SIZE  3
#define IOMMU_SECURE_PTBL_INIT  4
#define IOMMU_SECURE_MAP	6
#define IOMMU_SECURE_UNMAP      7

static DEFINE_MUTEX(msm_iommu_lock);

struct msm_priv {
	struct list_head list_attached;
};

struct msm_scm_paddr_list {
	unsigned int list;
	unsigned int list_size;
	unsigned int size;
};

struct msm_scm_mapping_info {
	unsigned int id;
	unsigned int ctx_id;
	unsigned int va;
	unsigned int size;
};

struct msm_scm_map_req {
	struct msm_scm_paddr_list plist;
	struct msm_scm_mapping_info info;
};

static int msm_iommu_sec_ptbl_init(void)
{
	struct device_node *np;
	struct msm_scm_ptbl_init {
		unsigned int paddr;
		unsigned int size;
		unsigned int spare;
	} pinit;
	unsigned int *buf;
	int psize[2] = {0, 0};
	unsigned int spare;
	int ret, ptbl_ret = 0;

	for_each_compatible_node(np, NULL, "qcom,msm-smmu-v2")
		if (of_find_property(np, "qcom,iommu-secure-id", NULL))
			break;

	if (!np)
		return 0;

	of_node_put(np);
	ret = scm_call(SCM_SVC_CP, IOMMU_SECURE_PTBL_SIZE, &spare,
			sizeof(spare), psize, sizeof(psize));
	if (ret) {
		pr_err("scm call IOMMU_SECURE_PTBL_SIZE failed\n");
		goto fail;
	}

	if (psize[1]) {
		pr_err("scm call IOMMU_SECURE_PTBL_SIZE failed\n");
		goto fail;
	}

	buf = kmalloc(psize[0], GFP_KERNEL);
	if (!buf) {
		pr_err("%s: Failed to allocate %d bytes for PTBL\n",
			__func__, psize[0]);
		ret = -ENOMEM;
		goto fail;
	}

	pinit.paddr = virt_to_phys(buf);
	pinit.size = psize[0];

	ret = scm_call(SCM_SVC_CP, IOMMU_SECURE_PTBL_INIT, &pinit,
			sizeof(pinit), &ptbl_ret, sizeof(ptbl_ret));
	if (ret) {
		pr_err("scm call IOMMU_SECURE_PTBL_INIT failed\n");
		goto fail_mem;
	}
	if (ptbl_ret) {
		pr_err("scm call IOMMU_SECURE_PTBL_INIT extended ret fail\n");
		goto fail_mem;
	}

	return 0;

fail_mem:
	kfree(buf);
fail:
	return ret;
}

int msm_iommu_sec_program_iommu(int sec_id)
{
	struct msm_scm_sec_cfg {
		unsigned int id;
		unsigned int spare;
	} cfg;
	int ret, scm_ret = 0;

	cfg.id = sec_id;

	ret = scm_call(SCM_SVC_CP, IOMMU_SECURE_CFG, &cfg, sizeof(cfg),
			&scm_ret, sizeof(scm_ret));
	if (ret || scm_ret) {
		pr_err("scm call IOMMU_SECURE_CFG failed\n");
		return ret ? ret : -EINVAL;
	}

	return ret;
}

static int msm_iommu_sec_ptbl_map(struct msm_iommu_drvdata *iommu_drvdata,
			struct msm_iommu_ctx_drvdata *ctx_drvdata,
			unsigned long va, phys_addr_t pa, size_t len)
{
	struct msm_scm_map_req map;
	int ret = 0;

	map.plist.list = virt_to_phys(&pa);
	map.plist.list_size = 1;
	map.plist.size = len;
	map.info.id = iommu_drvdata->sec_id;
	map.info.ctx_id = ctx_drvdata->num;
	map.info.va = va;
	map.info.size = len;

	if (scm_call(SCM_SVC_CP, IOMMU_SECURE_MAP, &map, sizeof(map), &ret,
								sizeof(ret)))
		return -EINVAL;
	if (ret)
		return -EINVAL;

	return 0;
}

static unsigned int get_phys_addr(struct scatterlist *sg)
{
	/*
	 * Try sg_dma_address first so that we can
	 * map carveout regions that do not have a
	 * struct page associated with them.
	 */
	unsigned int pa = sg_dma_address(sg);
	if (pa == 0)
		pa = sg_phys(sg);
	return pa;
}

static int msm_iommu_sec_ptbl_map_range(struct msm_iommu_drvdata *iommu_drvdata,
			struct msm_iommu_ctx_drvdata *ctx_drvdata,
			unsigned long va, struct scatterlist *sg, size_t len)
{
	struct scatterlist *sgiter;
	struct msm_scm_map_req map;
	unsigned int *pa_list = 0;
	unsigned int pa, cnt;
	unsigned int offset = 0, chunk_offset = 0;
	int ret, scm_ret;

	map.info.id = iommu_drvdata->sec_id;
	map.info.ctx_id = ctx_drvdata->num;
	map.info.va = va;
	map.info.size = len;

	if (sg->length == len) {
		pa = get_phys_addr(sg);
		map.plist.list = virt_to_phys(&pa);
		map.plist.list_size = 1;
		map.plist.size = len;
	} else {
		sgiter = sg;
		cnt = sg->length / SZ_1M;
		while ((sgiter = sg_next(sgiter)))
			cnt += sgiter->length / SZ_1M;

		pa_list = kmalloc(cnt * sizeof(*pa_list), GFP_KERNEL);
		if (!pa_list)
			return -ENOMEM;

		sgiter = sg;
		cnt = 0;
		pa = get_phys_addr(sgiter);
		while (offset < len) {
			pa += chunk_offset;
			pa_list[cnt] = pa;
			chunk_offset += SZ_1M;
			offset += SZ_1M;
			cnt++;

			if (chunk_offset >= sgiter->length && offset < len) {
				chunk_offset = 0;
				sgiter = sg_next(sgiter);
				pa = get_phys_addr(sgiter);
			}
		}

		map.plist.list = virt_to_phys(pa_list);
		map.plist.list_size = cnt;
		map.plist.size = SZ_1M;
	}

	ret = scm_call(SCM_SVC_CP, IOMMU_SECURE_MAP, &map, sizeof(map),
			&scm_ret, sizeof(scm_ret));
	kfree(pa_list);
	return ret;
}

static int msm_iommu_sec_ptbl_unmap(struct msm_iommu_drvdata *iommu_drvdata,
			struct msm_iommu_ctx_drvdata *ctx_drvdata,
			unsigned long va, size_t len)
{
	struct msm_scm_mapping_info mi;
	int ret, scm_ret;

	mi.id = iommu_drvdata->sec_id;
	mi.ctx_id = ctx_drvdata->num;
	mi.va = va;
	mi.size = len;

	ret = scm_call(SCM_SVC_CP, IOMMU_SECURE_UNMAP, &mi, sizeof(mi),
			&scm_ret, sizeof(scm_ret));
	return ret;
}

static int __enable_clocks(struct msm_iommu_drvdata *drvdata)
{
	int ret;

	ret = clk_prepare_enable(drvdata->pclk);
	if (ret)
		goto fail;

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		clk_disable_unprepare(drvdata->pclk);

	if (drvdata->aclk) {
		ret = clk_prepare_enable(drvdata->aclk);
		if (ret) {
			clk_disable_unprepare(drvdata->clk);
			clk_disable_unprepare(drvdata->pclk);
		}
	}
fail:
	return ret;
}

static void __disable_clocks(struct msm_iommu_drvdata *drvdata)
{
	if (drvdata->aclk)
		clk_disable_unprepare(drvdata->aclk);
	clk_disable_unprepare(drvdata->clk);
	clk_disable_unprepare(drvdata->pclk);
}

static int msm_iommu_domain_init(struct iommu_domain *domain, int flags)
{
	struct msm_priv *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv->list_attached);
	domain->priv = priv;
	return 0;
}

static void msm_iommu_domain_destroy(struct iommu_domain *domain)
{
	struct msm_priv *priv;

	mutex_lock(&msm_iommu_lock);
	priv = domain->priv;
	domain->priv = NULL;

	kfree(priv);
	mutex_unlock(&msm_iommu_lock);
}

static int msm_iommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct msm_priv *priv;
	struct msm_iommu_drvdata *iommu_drvdata;
	struct msm_iommu_ctx_drvdata *ctx_drvdata;
	struct msm_iommu_ctx_drvdata *tmp_drvdata;
	int ret = 0;

	mutex_lock(&msm_iommu_lock);

	priv = domain->priv;
	if (!priv || !dev) {
		ret = -EINVAL;
		goto fail;
	}

	iommu_drvdata = dev_get_drvdata(dev->parent);
	ctx_drvdata = dev_get_drvdata(dev);
	if (!iommu_drvdata || !ctx_drvdata) {
		ret = -EINVAL;
		goto fail;
	}

	if (!list_empty(&ctx_drvdata->attached_elm)) {
		ret = -EBUSY;
		goto fail;
	}

	list_for_each_entry(tmp_drvdata, &priv->list_attached, attached_elm)
		if (tmp_drvdata == ctx_drvdata) {
			ret = -EBUSY;
			goto fail;
		}

	ret = regulator_enable(iommu_drvdata->gdsc);
	if (ret)
		goto fail;

	ret = __enable_clocks(iommu_drvdata);
	if (ret) {
		regulator_disable(iommu_drvdata->gdsc);
		goto fail;
	}

	ret = msm_iommu_sec_program_iommu(iommu_drvdata->sec_id);

	/* bfb settings are always programmed by HLOS */
	program_iommu_bfb_settings(iommu_drvdata->base,
				   iommu_drvdata->bfb_settings);

	__disable_clocks(iommu_drvdata);
	if (ret) {
		regulator_disable(iommu_drvdata->gdsc);
		goto fail;
	}

	list_add(&(ctx_drvdata->attached_elm), &priv->list_attached);
	ctx_drvdata->attached_domain = domain;

fail:
	mutex_unlock(&msm_iommu_lock);
	return ret;
}

static void msm_iommu_detach_dev(struct iommu_domain *domain,
				 struct device *dev)
{
	struct msm_iommu_drvdata *iommu_drvdata;
	struct msm_iommu_ctx_drvdata *ctx_drvdata;

	mutex_lock(&msm_iommu_lock);
	if (!dev)
		goto fail;

	iommu_drvdata = dev_get_drvdata(dev->parent);
	ctx_drvdata = dev_get_drvdata(dev);
	if (!iommu_drvdata || !ctx_drvdata || !ctx_drvdata->attached_domain)
		goto fail;

	list_del_init(&ctx_drvdata->attached_elm);
	ctx_drvdata->attached_domain = NULL;

	regulator_disable(iommu_drvdata->gdsc);

fail:
	mutex_unlock(&msm_iommu_lock);
}

static int get_drvdata(struct iommu_domain *domain,
			struct msm_iommu_drvdata **iommu_drvdata,
			struct msm_iommu_ctx_drvdata **ctx_drvdata)
{
	struct msm_priv *priv = domain->priv;
	struct msm_iommu_ctx_drvdata *ctx;

	list_for_each_entry(ctx, &priv->list_attached, attached_elm) {
		if (ctx->attached_domain == domain)
			break;
	}

	if (ctx->attached_domain != domain)
		return -EINVAL;

	*ctx_drvdata = ctx;
	*iommu_drvdata = dev_get_drvdata(ctx->pdev->dev.parent);
	return 0;
}

static int msm_iommu_map(struct iommu_domain *domain, unsigned long va,
			 phys_addr_t pa, size_t len, int prot)
{
	struct msm_iommu_drvdata *iommu_drvdata;
	struct msm_iommu_ctx_drvdata *ctx_drvdata;
	int ret = 0;

	mutex_lock(&msm_iommu_lock);

	ret = get_drvdata(domain, &iommu_drvdata, &ctx_drvdata);
	if (ret)
		goto fail;

	ret = msm_iommu_sec_ptbl_map(iommu_drvdata, ctx_drvdata,
					va, pa, len);
fail:
	mutex_unlock(&msm_iommu_lock);
	return ret;
}

static size_t msm_iommu_unmap(struct iommu_domain *domain, unsigned long va,
			    size_t len)
{
	struct msm_iommu_drvdata *iommu_drvdata;
	struct msm_iommu_ctx_drvdata *ctx_drvdata;
	int ret = -ENODEV;

	mutex_lock(&msm_iommu_lock);

	ret = get_drvdata(domain, &iommu_drvdata, &ctx_drvdata);
	if (ret)
		goto fail;

	ret = msm_iommu_sec_ptbl_unmap(iommu_drvdata, ctx_drvdata,
					va, len);
fail:
	mutex_unlock(&msm_iommu_lock);

	/* the IOMMU API requires us to return how many bytes were unmapped */
	len = ret ? 0 : len;
	return len;
}

static int msm_iommu_map_range(struct iommu_domain *domain, unsigned int va,
			       struct scatterlist *sg, unsigned int len,
			       int prot)
{
	int ret;
	struct msm_iommu_drvdata *iommu_drvdata;
	struct msm_iommu_ctx_drvdata *ctx_drvdata;

	mutex_lock(&msm_iommu_lock);

	ret = get_drvdata(domain, &iommu_drvdata, &ctx_drvdata);
	if (ret)
		goto fail;
	ret = msm_iommu_sec_ptbl_map_range(iommu_drvdata, ctx_drvdata,
						va, sg, len);
fail:
	mutex_unlock(&msm_iommu_lock);
	return ret;
}


static int msm_iommu_unmap_range(struct iommu_domain *domain, unsigned int va,
				 unsigned int len)
{
	struct msm_iommu_drvdata *iommu_drvdata;
	struct msm_iommu_ctx_drvdata *ctx_drvdata;
	int ret;

	mutex_lock(&msm_iommu_lock);

	ret = get_drvdata(domain, &iommu_drvdata, &ctx_drvdata);
	if (ret)
		goto fail;

	ret = msm_iommu_sec_ptbl_unmap(iommu_drvdata, ctx_drvdata, va, len);

fail:
	mutex_unlock(&msm_iommu_lock);
	return 0;
}

static phys_addr_t msm_iommu_iova_to_phys(struct iommu_domain *domain,
					  unsigned long va)
{
	return 0;
}

static int msm_iommu_domain_has_cap(struct iommu_domain *domain,
				    unsigned long cap)
{
	return 0;
}

static phys_addr_t msm_iommu_get_pt_base_addr(struct iommu_domain *domain)
{
	return 0;
}

static struct iommu_ops msm_iommu_ops = {
	.domain_init = msm_iommu_domain_init,
	.domain_destroy = msm_iommu_domain_destroy,
	.attach_dev = msm_iommu_attach_dev,
	.detach_dev = msm_iommu_detach_dev,
	.map = msm_iommu_map,
	.unmap = msm_iommu_unmap,
	.map_range = msm_iommu_map_range,
	.unmap_range = msm_iommu_unmap_range,
	.iova_to_phys = msm_iommu_iova_to_phys,
	.domain_has_cap = msm_iommu_domain_has_cap,
	.get_pt_base_addr = msm_iommu_get_pt_base_addr,
	.pgsize_bitmap = MSM_IOMMU_PGSIZES,
};

static int __init msm_iommu_sec_init(void)
{
	int ret;

	ret = bus_register(&msm_iommu_sec_bus_type);
	if (ret)
		goto fail;

	bus_set_iommu(&msm_iommu_sec_bus_type, &msm_iommu_ops);
	ret = msm_iommu_sec_ptbl_init();
fail:
	return ret;
}

subsys_initcall(msm_iommu_sec_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MSM SMMU Secure Driver");
