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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/iopoll.h>

#include <mach/rpm-regulator-smd.h>
#include <mach/socinfo.h>
#include <mach/rpm-smd.h>

#include "clock-local2.h"
#include "clock-pll.h"
#include "clock-rpm.h"
#include "clock-voter.h"
#include "clock.h"

enum {
	GCC_BASE,
	MMSS_BASE,
	LPASS_BASE,
	APCS_BASE,
	N_BASES,
};

static void __iomem *virt_bases[N_BASES];

#define GCC_REG_BASE(x) (void __iomem *)(virt_bases[GCC_BASE] + (x))
#define MMSS_REG_BASE(x) (void __iomem *)(virt_bases[MMSS_BASE] + (x))
#define LPASS_REG_BASE(x) (void __iomem *)(virt_bases[LPASS_BASE] + (x))
#define APCS_REG_BASE(x) (void __iomem *)(virt_bases[APCS_BASE] + (x))

#define                     GPLL0_MODE	    0x0000
#define                    GPLL0_L_VAL	    0x0004
#define                    GPLL0_M_VAL	    0x0008
#define                    GPLL0_N_VAL	    0x000C
#define                 GPLL0_USER_CTL	    0x0010
#define                   GPLL0_STATUS	    0x001C
#define                     GPLL2_MODE	    0x0080
#define                    GPLL2_L_VAL	    0x0084
#define                    GPLL2_M_VAL	    0x0088
#define                    GPLL2_N_VAL	    0x008C
#define                 GPLL2_USER_CTL	    0x0090
#define                   GPLL2_STATUS	    0x009C
#define                 CONFIG_NOC_BCR	    0x0140
#define                       MMSS_BCR	    0x0240
#define          MMSS_NOC_CFG_AHB_CBCR	    0x024C
#define               MSS_CFG_AHB_CBCR	    0x0280
#define           MSS_Q6_BIMC_AXI_CBCR	    0x0284
#define                     USB_HS_BCR	    0x0480
#define             USB_HS_SYSTEM_CBCR	    0x0484
#define                USB_HS_AHB_CBCR	    0x0488
#define         USB_HS_SYSTEM_CMD_RCGR	    0x0490
#define                  USB2A_PHY_BCR	    0x04A8
#define           USB2A_PHY_SLEEP_CBCR	    0x04AC
#define                      SDCC1_BCR	    0x04C0
#define            SDCC1_APPS_CMD_RCGR	    0x04D0
#define                SDCC1_APPS_CBCR	    0x04C4
#define                 SDCC1_AHB_CBCR	    0x04C8
#define                      SDCC2_BCR	    0x0500
#define            SDCC2_APPS_CMD_RCGR	    0x0510
#define                SDCC2_APPS_CBCR	    0x0504
#define                 SDCC2_AHB_CBCR	    0x0508
#define                      BLSP1_BCR	    0x05C0
#define                 BLSP1_AHB_CBCR	    0x05C4
#define                 BLSP1_QUP1_BCR	    0x0640
#define       BLSP1_QUP1_SPI_APPS_CBCR	    0x0644
#define       BLSP1_QUP1_I2C_APPS_CBCR	    0x0648
#define   BLSP1_QUP1_SPI_APPS_CMD_RCGR	    0x064C
#define                BLSP1_UART1_BCR	    0x0680
#define          BLSP1_UART1_APPS_CBCR	    0x0684
#define           BLSP1_UART1_SIM_CBCR	    0x0688
#define      BLSP1_UART1_APPS_CMD_RCGR	    0x068C
#define                 BLSP1_QUP2_BCR	    0x06C0
#define       BLSP1_QUP2_SPI_APPS_CBCR	    0x06C4
#define       BLSP1_QUP2_I2C_APPS_CBCR	    0x06C8
#define   BLSP1_QUP2_SPI_APPS_CMD_RCGR	    0x06CC
#define                BLSP1_UART2_BCR	    0x0700
#define          BLSP1_UART2_APPS_CBCR	    0x0704
#define           BLSP1_UART2_SIM_CBCR	    0x0708
#define      BLSP1_UART2_APPS_CMD_RCGR	    0x070C
#define                 BLSP1_QUP3_BCR	    0x0740
#define       BLSP1_QUP3_SPI_APPS_CBCR	    0x0744
#define       BLSP1_QUP3_I2C_APPS_CBCR	    0x0748
#define   BLSP1_QUP3_SPI_APPS_CMD_RCGR	    0x074C
#define                BLSP1_UART3_BCR	    0x0780
#define          BLSP1_UART3_APPS_CBCR	    0x0784
#define           BLSP1_UART3_SIM_CBCR	    0x0788
#define      BLSP1_UART3_APPS_CMD_RCGR	    0x078C
#define                 BLSP1_QUP4_BCR	    0x07C0
#define       BLSP1_QUP4_SPI_APPS_CBCR	    0x07C4
#define       BLSP1_QUP4_I2C_APPS_CBCR	    0x07C8
#define   BLSP1_QUP4_SPI_APPS_CMD_RCGR	    0x07CC
#define                BLSP1_UART4_BCR	    0x0800
#define          BLSP1_UART4_APPS_CBCR	    0x0804
#define           BLSP1_UART4_SIM_CBCR	    0x0808
#define      BLSP1_UART4_APPS_CMD_RCGR	    0x080C
#define                 BLSP1_QUP5_BCR	    0x0840
#define       BLSP1_QUP5_SPI_APPS_CBCR	    0x0844
#define       BLSP1_QUP5_I2C_APPS_CBCR	    0x0848
#define   BLSP1_QUP5_SPI_APPS_CMD_RCGR	    0x084C
#define                BLSP1_UART5_BCR	    0x0880
#define          BLSP1_UART5_APPS_CBCR	    0x0884
#define           BLSP1_UART5_SIM_CBCR	    0x0888
#define      BLSP1_UART5_APPS_CMD_RCGR	    0x088C
#define                 BLSP1_QUP6_BCR	    0x08C0
#define       BLSP1_QUP6_SPI_APPS_CBCR	    0x08C4
#define       BLSP1_QUP6_I2C_APPS_CBCR	    0x08C8
#define   BLSP1_QUP6_SPI_APPS_CMD_RCGR	    0x08CC
#define                BLSP1_UART6_BCR	    0x0900
#define          BLSP1_UART6_APPS_CBCR	    0x0904
#define           BLSP1_UART6_SIM_CBCR	    0x0908
#define      BLSP1_UART6_APPS_CMD_RCGR	    0x090C
#define                        PDM_BCR	    0x0CC0
#define                   PDM_AHB_CBCR	    0x0CC4
#define                      PDM2_CBCR	    0x0CCC
#define                  PDM2_CMD_RCGR	    0x0CD0
#define                       PRNG_BCR	    0x0D00
#define                  PRNG_AHB_CBCR	    0x0D04
#define                   BOOT_ROM_BCR	    0x0E00
#define              BOOT_ROM_AHB_CBCR	    0x0E04
#define                        CE1_BCR	    0x1040
#define                   CE1_CMD_RCGR	    0x1050
#define                       CE1_CBCR	    0x1044
#define                   CE1_AXI_CBCR	    0x1048
#define                   CE1_AHB_CBCR	    0x104C
#define            COPSS_SMMU_AHB_CBCR      0x015C
#define             LPSS_SMMU_AHB_CBCR      0x0158
#define              LPASS_Q6_AXI_CBCR	    0x11C0
#define             APCS_GPLL_ENA_VOTE	    0x1480
#define     APCS_CLOCK_BRANCH_ENA_VOTE	    0x1484
#define      APCS_CLOCK_SLEEP_ENA_VOTE	    0x1488
#define                       GP1_CBCR	    0x1900
#define                   GP1_CMD_RCGR	    0x1904
#define                       GP2_CBCR	    0x1940
#define                   GP2_CMD_RCGR	    0x1944
#define                       GP3_CBCR	    0x1980
#define                   GP3_CMD_RCGR	    0x1984
#define                        XO_CBCR	    0x0034

#define                MMPLL0_PLL_MODE	    0x0000
#define               MMPLL0_PLL_L_VAL	    0x0004
#define               MMPLL0_PLL_M_VAL	    0x0008
#define               MMPLL0_PLL_N_VAL	    0x000C
#define            MMPLL0_PLL_USER_CTL	    0x0010
#define              MMPLL0_PLL_STATUS	    0x001C
#define         MMSS_PLL_VOTE_APCS_REG      0x0100
#define                MMPLL1_PLL_MODE	    0x4100
#define               MMPLL1_PLL_L_VAL	    0x4104
#define               MMPLL1_PLL_M_VAL	    0x4108
#define               MMPLL1_PLL_N_VAL	    0x410C
#define            MMPLL1_PLL_USER_CTL	    0x4110
#define              MMPLL1_PLL_STATUS	    0x411C
#define              DSI_PCLK_CMD_RCGR	    0x2000
#define                   DSI_CMD_RCGR	    0x2020
#define             MDP_VSYNC_CMD_RCGR	    0x2080
#define              DSI_BYTE_CMD_RCGR	    0x2120
#define               DSI_ESC_CMD_RCGR	    0x2160
#define                        DSI_BCR	    0x2200
#define                   DSI_BYTE_BCR	    0x2204
#define                    DSI_ESC_BCR	    0x2208
#define                    DSI_AHB_BCR	    0x220C
#define                   DSI_PCLK_BCR	    0x2214
#define                   MDP_LCDC_BCR	    0x2218
#define                    MDP_DSI_BCR	    0x221C
#define                  MDP_VSYNC_BCR	    0x2220
#define                    MDP_AXI_BCR	    0x2224
#define                    MDP_AHB_BCR	    0x2228
#define                   MDP_AXI_CBCR	    0x2314
#define                 MDP_VSYNC_CBCR	    0x231C
#define                   MDP_AHB_CBCR	    0x2318
#define                  DSI_PCLK_CBCR	    0x233C
#define                GMEM_GFX3D_CBCR      0x4038
#define                  MDP_LCDC_CBCR	    0x2340
#define                   MDP_DSI_CBCR	    0x2320
#define                       DSI_CBCR	    0x2324
#define                  DSI_BYTE_CBCR	    0x2328
#define                   DSI_ESC_CBCR	    0x232C
#define                   DSI_AHB_CBCR	    0x2330
#define          CSI0PHYTIMER_CMD_RCGR	    0x3000
#define               CSI0PHYTIMER_BCR	    0x3020
#define              CSI0PHYTIMER_CBCR	    0x3024
#define          CSI1PHYTIMER_CMD_RCGR	    0x3030
#define               CSI1PHYTIMER_BCR	    0x3050
#define              CSI1PHYTIMER_CBCR	    0x3054
#define                  CSI0_CMD_RCGR	    0x3090
#define                       CSI0_BCR	    0x30B0
#define                      CSI0_CBCR	    0x30B4
#define                    CSI_AHB_BCR	    0x30B8
#define                   CSI_AHB_CBCR	    0x30BC
#define                    CSI0PHY_BCR	    0x30C0
#define                   CSI0PHY_CBCR	    0x30C4
#define                    CSI0RDI_BCR	    0x30D0
#define                   CSI0RDI_CBCR	    0x30D4
#define                    CSI0PIX_BCR	    0x30E0
#define                   CSI0PIX_CBCR	    0x30E4
#define                  CSI1_CMD_RCGR	    0x3100
#define                       CSI1_BCR	    0x3120
#define                      CSI1_CBCR	    0x3124
#define                    CSI1PHY_BCR	    0x3130
#define                   CSI1PHY_CBCR	    0x3134
#define                    CSI1RDI_BCR	    0x3140
#define                   CSI1RDI_CBCR	    0x3144
#define                    CSI1PIX_BCR	    0x3150
#define                   CSI1PIX_CBCR	    0x3154
#define                 MCLK0_CMD_RCGR	    0x3360
#define                      MCLK0_BCR	    0x3380
#define                     MCLK0_CBCR	    0x3384
#define                 MCLK1_CMD_RCGR	    0x3390
#define                      MCLK1_BCR	    0x33B0
#define                     MCLK1_CBCR	    0x33B4
#define                   VFE_CMD_RCGR	    0x3600
#define                        VFE_BCR	    0x36A0
#define                    VFE_AHB_BCR	    0x36AC
#define                    VFE_AXI_BCR	    0x36B0
#define                       VFE_CBCR	    0x36A8
#define                   VFE_AHB_CBCR	    0x36B8
#define                   VFE_AXI_CBCR	    0x36BC
#define                    CSI_VFE_BCR	    0x3700
#define                   CSI_VFE_CBCR	    0x3704
#define                 GFX3D_CMD_RCGR	    0x4000
#define               OXILI_GFX3D_CBCR	    0x4028
#define                OXILI_GFX3D_BCR	    0x4030
#define                  OXILI_AHB_BCR	    0x4044
#define                 OXILI_AHB_CBCR	    0x403C
#define                   AHB_CMD_RCGR	    0x5000
#define                 MMSSNOCAHB_BCR	    0x5020
#define             MMSSNOCAHB_BTO_BCR	    0x5030
#define              MMSS_MISC_AHB_BCR	    0x5034
#define          MMSS_MMSSNOC_AHB_CBCR	    0x5024
#define      MMSS_MMSSNOC_BTO_AHB_CBCR	    0x5028
#define             MMSS_MISC_AHB_CBCR	    0x502C
#define                   AXI_CMD_RCGR	    0x5040
#define                 MMSSNOCAXI_BCR	    0x5060
#define                MMSS_S0_AXI_BCR	    0x5068
#define               MMSS_S0_AXI_CBCR	    0x5064
#define          MMSS_MMSSNOC_AXI_CBCR	    0x506C
#define                   BIMC_GFX_BCR	    0x5090
#define                  BIMC_GFX_CBCR	    0x5094

#define				AUDIO_CORE_GDSCR	    0x7000
#define                                 SPDM_BCR	    0x1000
#define                        LPAAUDIO_PLL_MODE	    0x0000
#define                       LPAAUDIO_PLL_L_VAL	    0x0004
#define                       LPAAUDIO_PLL_M_VAL	    0x0008
#define                       LPAAUDIO_PLL_N_VAL	    0x000C
#define                    LPAAUDIO_PLL_USER_CTL	    0x0010
#define                      LPAAUDIO_PLL_STATUS	    0x001C
#define                           LPAQ6_PLL_MODE	    0x1000
#define                       LPAQ6_PLL_USER_CTL	    0x1010
#define                         LPAQ6_PLL_STATUS	    0x101C
#define                        LPA_PLL_VOTE_APPS            0x2000
#define                  AUDIO_CORE_BCR_SLP_CBCR	    0x4004
#define                        Q6SS_BCR_SLP_CBCR	    0x6004
#define                  AUDIO_CORE_GDSC_XO_CBCR	    0x7004
#define                AUDIO_CORE_LPAIF_DMA_CBCR	    0x9000
#define                AUDIO_CORE_LPAIF_CSR_CBCR	    0x9004
#define                      LPAIF_SPKR_CMD_RCGR	    0xA000
#define     AUDIO_CORE_LPAIF_CODEC_SPKR_OSR_CBCR	    0xA014
#define    AUDIO_CORE_LPAIF_CODEC_SPKR_IBIT_CBCR	    0xA018
#define    AUDIO_CORE_LPAIF_CODEC_SPKR_EBIT_CBCR	    0xA01C
#define                       LPAIF_PRI_CMD_RCGR	    0xB000
#define            AUDIO_CORE_LPAIF_PRI_OSR_CBCR	    0xB014
#define           AUDIO_CORE_LPAIF_PRI_IBIT_CBCR	    0xB018
#define           AUDIO_CORE_LPAIF_PRI_EBIT_CBCR	    0xB01C
#define                       LPAIF_SEC_CMD_RCGR	    0xC000
#define            AUDIO_CORE_LPAIF_SEC_OSR_CBCR	    0xC014
#define           AUDIO_CORE_LPAIF_SEC_IBIT_CBCR	    0xC018
#define           AUDIO_CORE_LPAIF_SEC_EBIT_CBCR	    0xC01C
#define                       LPAIF_TER_CMD_RCGR	    0xD000
#define            AUDIO_CORE_LPAIF_TER_OSR_CBCR	    0xD014
#define           AUDIO_CORE_LPAIF_TER_IBIT_CBCR	    0xD018
#define           AUDIO_CORE_LPAIF_TER_EBIT_CBCR	    0xD01C
#define                      LPAIF_QUAD_CMD_RCGR	    0xE000
#define           AUDIO_CORE_LPAIF_QUAD_OSR_CBCR	    0xE014
#define          AUDIO_CORE_LPAIF_QUAD_IBIT_CBCR	    0xE018
#define          AUDIO_CORE_LPAIF_QUAD_EBIT_CBCR	    0xE01C
#define                      LPAIF_PCM0_CMD_RCGR	    0xF000
#define          AUDIO_CORE_LPAIF_PCM0_IBIT_CBCR	    0xF014
#define          AUDIO_CORE_LPAIF_PCM0_EBIT_CBCR	    0xF018
#define                      LPAIF_PCM1_CMD_RCGR	   0x10000
#define          AUDIO_CORE_LPAIF_PCM1_IBIT_CBCR	   0x10014
#define          AUDIO_CORE_LPAIF_PCM1_EBIT_CBCR	   0x10018
#define                         SLIMBUS_CMD_RCGR           0x12000
#define             AUDIO_CORE_SLIMBUS_CORE_CBCR           0x12014
#define                     LPAIF_PCMOE_CMD_RCGR	   0x13000
#define        AUDIO_CORE_LPAIF_PCM_DATA_OE_CBCR	   0x13014
#define                          Q6CORE_CMD_RCGR	   0x14000
#define                           SLEEP_CMD_RCGR	   0x15000
#define                            SPDM_CMD_RCGR	   0x16000
#define                  AUDIO_WRAPPER_SPDM_CBCR	   0x16014
#define                              XO_CMD_RCGR	   0x17000
#define                       AHBFABRIC_CMD_RCGR	   0x18000
#define                      AUDIO_CORE_LPM_CBCR	   0x19000
#define               AUDIO_CORE_AVSYNC_CSR_CBCR	   0x1A000
#define                AUDIO_CORE_AVSYNC_XO_CBCR	   0x1A004
#define             AUDIO_CORE_AVSYNC_BT_XO_CBCR	   0x1A008
#define             AUDIO_CORE_AVSYNC_FM_XO_CBCR	   0x1A00C
#define                 AUDIO_CORE_IXFABRIC_CBCR	   0x1B000
#define               AUDIO_WRAPPER_EFABRIC_CBCR	   0x1B004
#define                AUDIO_CORE_TCM_SLAVE_CBCR	   0x1C000
#define                      AUDIO_CORE_CSR_CBCR	   0x1D000
#define                      AUDIO_CORE_DML_CBCR	   0x1E000
#define                   AUDIO_CORE_SYSNOC_CBCR	   0x1F000
#define           AUDIO_WRAPPER_SYSNOC_SWAY_CBCR	   0x1F004
#define                  AUDIO_CORE_TIMEOUT_CBCR	   0x20000
#define               AUDIO_WRAPPER_TIMEOUT_CBCR	   0x20004
#define                 AUDIO_CORE_SECURITY_CBCR	   0x21000
#define              AUDIO_WRAPPER_SECURITY_CBCR	   0x21004
#define                     Q6SS_AHB_LFABIF_CBCR	   0x22000
#define                           Q6SS_AHBM_CBCR	   0x22004
#define               AUDIO_WRAPPER_LCC_CSR_CBCR	   0x23000
#define                    AUDIO_WRAPPER_BR_CBCR	   0x24000
#define                  AUDIO_WRAPPER_SMEM_CBCR	   0x25000
#define                             Q6SS_XO_CBCR	   0x26000
#define                            Q6SS_SLP_CBCR	   0x26004
#define                           LPASS_Q6SS_BCR           0x6000
#define                AUDIO_WRAPPER_STM_XO_CBCR	   0x27000
#define      AUDIO_CORE_IXFABRIC_SPDMTM_CSR_CBCR	   0x28000
#define    AUDIO_WRAPPER_EFABRIC_SPDMTM_CSR_CBCR	   0x28004

/* Mux source select values */
#define        gcc_xo_source_val 0
#define         gpll0_source_val 1
#define           gnd_source_val 5
#define     mmpll0_mm_source_val 1
#define     mmpll1_mm_source_val 2
#define      gpll0_mm_source_val 5
#define     gcc_xo_mm_source_val 0
#define        mm_gnd_source_val 6
#define     cxo_lpass_source_val 0
#define lpapll0_lpass_source_val 1
#define   gpll0_lpass_source_val 5
#define     dsipll_mm_source_val 1

#define F(f, s, div, m, n) \
	{ \
		.freq_hz = (f), \
		.src_clk = &s##_clk_src.c, \
		.m_val = (m), \
		.n_val = ~((n)-(m)) * !!(n), \
		.d_val = ~(n),\
		.div_src_val = BVAL(4, 0, (int)(2*(div) - 1)) \
			| BVAL(10, 8, s##_source_val), \
	}

#define F_MM(f, s, div, m, n) \
	{ \
		.freq_hz = (f), \
		.src_clk = &s##_clk_src.c, \
		.m_val = (m), \
		.n_val = ~((n)-(m)) * !!(n), \
		.d_val = ~(n),\
		.div_src_val = BVAL(4, 0, (int)(2*(div) - 1)) \
			| BVAL(10, 8, s##_mm_source_val), \
	}

#define F_HDMI(f, s, div, m, n) \
	{ \
		.freq_hz = (f), \
		.src_clk = &s##_clk_src, \
		.m_val = (m), \
		.n_val = ~((n)-(m)) * !!(n), \
		.d_val = ~(n),\
		.div_src_val = BVAL(4, 0, (int)(2*(div) - 1)) \
			| BVAL(10, 8, s##_mm_source_val), \
	}

#define F_MDSS(f, s, div, m, n) \
	{ \
		.freq_hz = (f), \
		.m_val = (m), \
		.n_val = ~((n)-(m)) * !!(n), \
		.d_val = ~(n),\
		.div_src_val = BVAL(4, 0, (int)(2*(div) - 1)) \
			| BVAL(10, 8, s##_mm_source_val), \
	}

#define F_LPASS(f, s, div, m, n) \
	{ \
		.freq_hz = (f), \
		.src_clk = &s##_clk_src.c, \
		.m_val = (m), \
		.n_val = ~((n)-(m)) * !!(n), \
		.d_val = ~(n),\
		.div_src_val = BVAL(4, 0, (int)(2*(div) - 1)) \
			| BVAL(10, 8, s##_lpass_source_val), \
	}

#define VDD_DIG_FMAX_MAP1(l1, f1) \
	.vdd_class = &vdd_dig,			\
	.fmax = (unsigned long[VDD_DIG_NUM]) {	\
		[VDD_DIG_##l1] = (f1),		\
	},					\
	.num_fmax = VDD_DIG_NUM
#define VDD_DIG_FMAX_MAP2(l1, f1, l2, f2) \
	.vdd_class = &vdd_dig,			\
	.fmax = (unsigned long[VDD_DIG_NUM]) {	\
		[VDD_DIG_##l1] = (f1),		\
		[VDD_DIG_##l2] = (f2),		\
	},					\
	.num_fmax = VDD_DIG_NUM
#define VDD_DIG_FMAX_MAP3(l1, f1, l2, f2, l3, f3) \
	.vdd_class = &vdd_dig,			\
	.fmax = (unsigned long[VDD_DIG_NUM]) {	\
		[VDD_DIG_##l1] = (f1),		\
		[VDD_DIG_##l2] = (f2),		\
		[VDD_DIG_##l3] = (f3),		\
	},					\
	.num_fmax = VDD_DIG_NUM

enum vdd_dig_levels {
	VDD_DIG_NONE,
	VDD_DIG_LOW,
	VDD_DIG_NOMINAL,
	VDD_DIG_HIGH,
	VDD_DIG_NUM
};

static const int vdd_corner[] = {
	[VDD_DIG_NONE]	  = RPM_REGULATOR_CORNER_NONE,
	[VDD_DIG_LOW]	  = RPM_REGULATOR_CORNER_SVS_SOC,
	[VDD_DIG_NOMINAL] = RPM_REGULATOR_CORNER_NORMAL,
	[VDD_DIG_HIGH]	  = RPM_REGULATOR_CORNER_SUPER_TURBO,
};

static struct rpm_regulator *vdd_dig_reg;

static int set_vdd_dig(struct clk_vdd_class *vdd_class, int level)
{
	return rpm_regulator_set_voltage(vdd_dig_reg, vdd_corner[level],
					RPM_REGULATOR_CORNER_SUPER_TURBO);
}

static DEFINE_VDD_CLASS(vdd_dig, set_vdd_dig, VDD_DIG_NUM);

#define RPM_MISC_CLK_TYPE	0x306b6c63
#define RPM_BUS_CLK_TYPE	0x316b6c63
#define RPM_MEM_CLK_TYPE	0x326b6c63

#define RPM_SMD_KEY_ENABLE	0x62616E45

#define CXO_ID			0x0
#define QDSS_ID			0x1
#define RPM_SCALING_ENABLE_ID	0x2

#define PNOC_ID		0x0
#define SNOC_ID		0x1
#define CNOC_ID		0x2
#define MMSSNOC_AHB_ID  0x3

#define BIMC_ID		0x0
#define OXILI_ID	0x1
#define OCMEM_ID	0x2

#define D0_ID		 1
#define D1_ID		 2
#define A0_ID		 3
#define A1_ID		 4
#define A2_ID		 5
#define DIFF_CLK_ID	 7
#define DIV_CLK_ID	11

DEFINE_CLK_RPM_SMD(pnoc_clk, pnoc_a_clk, RPM_BUS_CLK_TYPE, PNOC_ID, NULL);
DEFINE_CLK_RPM_SMD(snoc_clk, snoc_a_clk, RPM_BUS_CLK_TYPE, SNOC_ID, NULL);
DEFINE_CLK_RPM_SMD(cnoc_clk, cnoc_a_clk, RPM_BUS_CLK_TYPE, CNOC_ID, NULL);
DEFINE_CLK_RPM_SMD(mmssnoc_ahb_clk, mmssnoc_ahb_a_clk, RPM_BUS_CLK_TYPE,
			MMSSNOC_AHB_ID, NULL);

DEFINE_CLK_RPM_SMD(bimc_clk, bimc_a_clk, RPM_MEM_CLK_TYPE, BIMC_ID, NULL);

DEFINE_CLK_RPM_SMD_BRANCH(gcc_xo_clk_src, gcc_xo_a_clk_src,
				RPM_MISC_CLK_TYPE, CXO_ID, 19200000);
DEFINE_CLK_RPM_SMD_QDSS(qdss_clk, qdss_a_clk, RPM_MISC_CLK_TYPE, QDSS_ID);

DEFINE_CLK_RPM_SMD_XO_BUFFER(cxo_d0, cxo_d0_a, D0_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER(cxo_d1, cxo_d1_a, D1_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER(cxo_a0, cxo_a0_a, A0_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER(cxo_a1, cxo_a1_a, A1_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER(cxo_a2, cxo_a2_a, A2_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER(div_clk, div_a_clk, DIV_CLK_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER(diff_clk, diff_a_clk, DIFF_CLK_ID);

DEFINE_CLK_RPM_SMD_XO_BUFFER_PINCTRL(cxo_d0_pin, cxo_d0_a_pin, D0_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER_PINCTRL(cxo_d1_pin, cxo_d1_a_pin, D1_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER_PINCTRL(cxo_a0_pin, cxo_a0_a_pin, A0_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER_PINCTRL(cxo_a1_pin, cxo_a1_a_pin, A1_ID);
DEFINE_CLK_RPM_SMD_XO_BUFFER_PINCTRL(cxo_a2_pin, cxo_a2_a_pin, A2_ID);

static DEFINE_CLK_VOTER(pnoc_msmbus_clk, &pnoc_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(snoc_msmbus_clk, &snoc_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(cnoc_msmbus_clk, &cnoc_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(pnoc_msmbus_a_clk, &pnoc_a_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(snoc_msmbus_a_clk, &snoc_a_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(cnoc_msmbus_a_clk, &cnoc_a_clk.c, LONG_MAX);

static DEFINE_CLK_VOTER(bimc_msmbus_clk, &bimc_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(bimc_msmbus_a_clk, &bimc_a_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(bimc_acpu_a_clk, &bimc_a_clk.c, LONG_MAX);

static DEFINE_CLK_VOTER(pnoc_sps_clk, &pnoc_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(pnoc_iommu_clk, &pnoc_clk.c, LONG_MAX);
static DEFINE_CLK_VOTER(pnoc_qseecom_clk, &pnoc_clk.c, LONG_MAX);

static struct pll_vote_clk gpll0_clk_src = {
	.en_reg = (void __iomem *)APCS_GPLL_ENA_VOTE,
	.en_mask = BIT(0),
	.status_reg = (void __iomem *)GPLL0_STATUS,
	.status_mask = BIT(17),
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.rate = 600000000,
		.dbg_name = "gpll0_clk_src",
		.ops = &clk_ops_pll_vote,
		CLK_INIT(gpll0_clk_src.c),
	},
};

static struct pll_vote_clk mmpll0_clk_src = {
	.en_reg = (void __iomem *)MMSS_PLL_VOTE_APCS_REG,
	.en_mask = BIT(0),
	.status_reg = (void __iomem *)MMPLL0_PLL_STATUS,
	.status_mask = BIT(17),
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.dbg_name = "mmpll0_clk_src",
		.rate = 800000000,
		.ops = &clk_ops_pll_vote,
		CLK_INIT(mmpll0_clk_src.c),
	},
};

static struct pll_config_regs mmpll0_regs __initdata = {
	.l_reg = (void __iomem *)MMPLL0_PLL_L_VAL,
	.m_reg = (void __iomem *)MMPLL0_PLL_M_VAL,
	.n_reg = (void __iomem *)MMPLL0_PLL_N_VAL,
	.config_reg = (void __iomem *)MMPLL0_PLL_USER_CTL,
	.mode_reg = (void __iomem *)MMPLL0_PLL_MODE,
	.base = &virt_bases[MMSS_BASE],
};

static struct pll_clk mmpll1_clk_src = {
	.mode_reg = (void __iomem *)MMPLL1_PLL_MODE,
	.status_reg = (void __iomem *)MMPLL1_PLL_STATUS,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.dbg_name = "mmpll1_clk_src",
		.rate = 1200000000,
		.ops = &clk_ops_local_pll,
		CLK_INIT(mmpll1_clk_src.c),
	},
};

static struct pll_config_regs mmpll1_regs __initdata = {
	.l_reg = (void __iomem *)MMPLL1_PLL_L_VAL,
	.m_reg = (void __iomem *)MMPLL1_PLL_M_VAL,
	.n_reg = (void __iomem *)MMPLL1_PLL_N_VAL,
	.config_reg = (void __iomem *)MMPLL1_PLL_USER_CTL,
	.mode_reg = (void __iomem *)MMPLL1_PLL_MODE,
	.base = &virt_bases[MMSS_BASE],
};

static struct pll_vote_clk lpapll0_clk_src = {
	.en_reg = (void __iomem *)LPA_PLL_VOTE_APPS,
	.en_mask = BIT(0),
	.status_reg = (void __iomem *)LPAAUDIO_PLL_STATUS,
	.status_mask = BIT(17),
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.rate = 491520000,
		.dbg_name = "lpapll0_clk_src",
		.ops = &clk_ops_pll_vote,
		CLK_INIT(lpapll0_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_gcc_blsp1_qup1_6_spi_apps_clk[] = {
	F(  960000, gcc_xo, 10, 1, 2),
	F( 4800000, gcc_xo,  4, 0, 0),
	F( 9600000, gcc_xo,  2, 0, 0),
	F(15000000,  gpll0, 10, 1, 4),
	F(19200000, gcc_xo,  1, 0, 0),
	F(25000000,  gpll0, 12, 1, 2),
	F(50000000,  gpll0, 12, 0, 0),
	F_END,
};

static struct rcg_clk blsp1_qup1_spi_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_QUP1_SPI_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_qup1_spi_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 25000000, NOMINAL, 50000000),
		CLK_INIT(blsp1_qup1_spi_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_qup2_spi_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_QUP2_SPI_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_qup2_spi_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 25000000, NOMINAL, 50000000),
		CLK_INIT(blsp1_qup2_spi_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_qup3_spi_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_QUP3_SPI_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_qup3_spi_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 25000000, NOMINAL, 50000000),
		CLK_INIT(blsp1_qup3_spi_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_qup4_spi_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_QUP4_SPI_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_qup4_spi_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 25000000, NOMINAL, 50000000),
		CLK_INIT(blsp1_qup4_spi_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_qup5_spi_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_QUP5_SPI_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_qup5_spi_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 25000000, NOMINAL, 50000000),
		CLK_INIT(blsp1_qup5_spi_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_qup6_spi_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_QUP6_SPI_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_qup1_6_spi_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_qup6_spi_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 25000000, NOMINAL, 50000000),
		CLK_INIT(blsp1_qup6_spi_apps_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_gcc_blsp1_uart1_6_apps_clk[] = {
	F( 3686400,  gpll0,    1,   96, 15625),
	F( 7372800,  gpll0,    1,  192, 15625),
	F(14745600,  gpll0,    1,  384, 15625),
	F(16000000,  gpll0,    5,    2,    15),
	F(19200000, gcc_xo,    1,    0,     0),
	F(24000000,  gpll0,    5,    1,     5),
	F(32000000,  gpll0,    1,    4,    75),
	F(40000000,  gpll0,   15,    0,     0),
	F(46400000,  gpll0,    1,   29,   375),
	F(48000000,  gpll0, 12.5,    0,     0),
	F(51200000,  gpll0,    1,   32,   375),
	F(56000000,  gpll0,    1,    7,    75),
	F(58982400,  gpll0,    1, 1536, 15625),
	F(60000000,  gpll0,   10,    0,     0),
	F_END,
};

static struct rcg_clk blsp1_uart1_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_UART1_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_uart1_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 31580000, NOMINAL, 63160000),
		CLK_INIT(blsp1_uart1_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_uart2_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_UART2_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_uart2_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 31580000, NOMINAL, 63160000),
		CLK_INIT(blsp1_uart2_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_uart3_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_UART3_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_uart3_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 31580000, NOMINAL, 63160000),
		CLK_INIT(blsp1_uart3_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_uart4_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_UART4_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_uart4_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 31580000, NOMINAL, 63160000),
		CLK_INIT(blsp1_uart4_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_uart5_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_UART5_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_uart5_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 31580000, NOMINAL, 63160000),
		CLK_INIT(blsp1_uart5_apps_clk_src.c),
	},
};

static struct rcg_clk blsp1_uart6_apps_clk_src = {
	.cmd_rcgr_reg =  BLSP1_UART6_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_blsp1_uart1_6_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "blsp1_uart6_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 31580000, NOMINAL, 63160000),
		CLK_INIT(blsp1_uart6_apps_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_gcc_ce1_clk[] = {
	F(50000000, gpll0, 12, 0, 0),
	F(100000000, gpll0, 6, 0, 0),
	F_END,
};

static struct rcg_clk ce1_clk_src = {
	.cmd_rcgr_reg = CE1_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_gcc_ce1_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "ce1_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP2(LOW, 50000000, NOMINAL, 100000000),
		CLK_INIT(ce1_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_gcc_gp1_3_clk[] = {
	F(19200000, gcc_xo, 1, 0, 0),
	F_END,
};

static struct rcg_clk gp1_clk_src = {
	.cmd_rcgr_reg =  GP1_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_gp1_3_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gp1_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(gp1_clk_src.c),
	},
};

static struct rcg_clk gp2_clk_src = {
	.cmd_rcgr_reg =  GP2_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_gp1_3_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gp2_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(gp2_clk_src.c),
	},
};

static struct rcg_clk gp3_clk_src = {
	.cmd_rcgr_reg =  GP3_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_gp1_3_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gp3_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(gp3_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_gcc_pdm2_clk[] = {
	F(60000000, gpll0, 10, 0, 0),
	F_END,
};

static struct rcg_clk pdm2_clk_src = {
	.cmd_rcgr_reg = PDM2_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_gcc_pdm2_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "pdm2_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP1(LOW, 120000000),
		CLK_INIT(pdm2_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_gcc_sdcc1_2_apps_clk[] = {
	F(   144000, gcc_xo, 16, 3, 25),
	F(   400000, gcc_xo, 12, 1,  4),
	F( 20000000,  gpll0, 15, 1,  2),
	F( 25000000,  gpll0, 12, 1,  2),
	F( 50000000,  gpll0, 12, 0,  0),
	F(100000000,  gpll0,  6, 0,  0),
	F(200000000,  gpll0,  3, 0,  0),
	F_END,
};

static struct rcg_clk sdcc1_apps_clk_src = {
	.cmd_rcgr_reg =  SDCC1_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_sdcc1_2_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "sdcc1_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(sdcc1_apps_clk_src.c),
	},
};

static struct rcg_clk sdcc2_apps_clk_src = {
	.cmd_rcgr_reg =  SDCC2_APPS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_gcc_sdcc1_2_apps_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "sdcc2_apps_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(sdcc2_apps_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_gcc_usb_hs_system_clk[] = {
	F(75000000, gpll0, 8, 0, 0),
	F_END,
};

static struct rcg_clk usb_hs_system_clk_src = {
	.cmd_rcgr_reg = USB_HS_SYSTEM_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_gcc_usb_hs_system_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "usb_hs_system_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP2(LOW, 60000000, NOMINAL, 100000000),
		CLK_INIT(usb_hs_system_clk_src.c),
	},
};

static struct local_vote_clk gcc_blsp1_ahb_clk = {
	.cbcr_reg = BLSP1_AHB_CBCR,
	.vote_reg = APCS_CLOCK_BRANCH_ENA_VOTE,
	.en_mask = BIT(17),
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_blsp1_ahb_clk",
		.ops = &clk_ops_vote,
		CLK_INIT(gcc_blsp1_ahb_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup1_i2c_apps_clk = {
	.cbcr_reg = BLSP1_QUP1_I2C_APPS_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.dbg_name = "gcc_blsp1_qup1_i2c_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup1_i2c_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup1_spi_apps_clk = {
	.cbcr_reg = BLSP1_QUP1_SPI_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_qup1_spi_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_qup1_spi_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup1_spi_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup2_i2c_apps_clk = {
	.cbcr_reg = BLSP1_QUP2_I2C_APPS_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.dbg_name = "gcc_blsp1_qup2_i2c_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup2_i2c_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup2_spi_apps_clk = {
	.cbcr_reg = BLSP1_QUP2_SPI_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_qup2_spi_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_qup2_spi_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup2_spi_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup3_i2c_apps_clk = {
	.cbcr_reg = BLSP1_QUP3_I2C_APPS_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.dbg_name = "gcc_blsp1_qup3_i2c_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup3_i2c_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup3_spi_apps_clk = {
	.cbcr_reg = BLSP1_QUP3_SPI_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_qup3_spi_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_qup3_spi_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup3_spi_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup4_i2c_apps_clk = {
	.cbcr_reg = BLSP1_QUP4_I2C_APPS_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.dbg_name = "gcc_blsp1_qup4_i2c_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup4_i2c_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup4_spi_apps_clk = {
	.cbcr_reg = BLSP1_QUP4_SPI_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_qup4_spi_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_qup4_spi_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup4_spi_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup5_i2c_apps_clk = {
	.cbcr_reg = BLSP1_QUP5_I2C_APPS_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.dbg_name = "gcc_blsp1_qup5_i2c_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup5_i2c_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup5_spi_apps_clk = {
	.cbcr_reg = BLSP1_QUP5_SPI_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_qup5_spi_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_qup5_spi_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup5_spi_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup6_i2c_apps_clk = {
	.cbcr_reg = BLSP1_QUP6_I2C_APPS_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.dbg_name = "gcc_blsp1_qup6_i2c_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup6_i2c_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_qup6_spi_apps_clk = {
	.cbcr_reg = BLSP1_QUP6_SPI_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_qup6_spi_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_qup6_spi_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_qup6_spi_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_uart1_apps_clk = {
	.cbcr_reg = BLSP1_UART1_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_uart1_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_uart1_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_uart1_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_uart2_apps_clk = {
	.cbcr_reg = BLSP1_UART2_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_uart2_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_uart2_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_uart2_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_uart3_apps_clk = {
	.cbcr_reg = BLSP1_UART3_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_uart3_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_uart3_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_uart3_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_uart4_apps_clk = {
	.cbcr_reg = BLSP1_UART4_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_uart4_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_uart4_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_uart4_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_uart5_apps_clk = {
	.cbcr_reg = BLSP1_UART5_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_uart5_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_uart5_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_uart5_apps_clk.c),
	},
};

static struct branch_clk gcc_blsp1_uart6_apps_clk = {
	.cbcr_reg = BLSP1_UART6_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &blsp1_uart6_apps_clk_src.c,
		.dbg_name = "gcc_blsp1_uart6_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_blsp1_uart6_apps_clk.c),
	},
};

static struct local_vote_clk gcc_boot_rom_ahb_clk = {
	.cbcr_reg = BOOT_ROM_AHB_CBCR,
	.vote_reg = APCS_CLOCK_BRANCH_ENA_VOTE,
	.en_mask = BIT(10),
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_boot_rom_ahb_clk",
		.ops = &clk_ops_vote,
		CLK_INIT(gcc_boot_rom_ahb_clk.c),
	},
};

static struct local_vote_clk gcc_ce1_ahb_clk = {
	.cbcr_reg = CE1_AHB_CBCR,
	.vote_reg = APCS_CLOCK_BRANCH_ENA_VOTE,
	.en_mask = BIT(3),
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_ce1_ahb_clk",
		.ops = &clk_ops_vote,
		CLK_INIT(gcc_ce1_ahb_clk.c),
	},
};

static struct local_vote_clk gcc_ce1_axi_clk = {
	.cbcr_reg = CE1_AXI_CBCR,
	.vote_reg = APCS_CLOCK_BRANCH_ENA_VOTE,
	.en_mask = BIT(4),
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_ce1_axi_clk",
		.ops = &clk_ops_vote,
		CLK_INIT(gcc_ce1_axi_clk.c),
	},
};

static struct local_vote_clk gcc_ce1_clk = {
	.cbcr_reg = CE1_CBCR,
	.vote_reg = APCS_CLOCK_BRANCH_ENA_VOTE,
	.en_mask = BIT(5),
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_ce1_clk",
		.ops = &clk_ops_vote,
		CLK_INIT(gcc_ce1_clk.c),
	},
};

static struct branch_clk gcc_copss_smmu_ahb_clk = {
	.cbcr_reg = COPSS_SMMU_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_copss_smmu_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_copss_smmu_ahb_clk.c),
	},
};

static struct branch_clk gcc_lpss_smmu_ahb_clk = {
	.cbcr_reg = LPSS_SMMU_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
			.dbg_name = "gcc_lpss_smmu_ahb_clk",
			.ops = &clk_ops_branch,
			CLK_INIT(gcc_lpss_smmu_ahb_clk.c),
	},
};

static struct branch_clk gcc_gp1_clk = {
	.cbcr_reg = GP1_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gp1_clk_src.c,
		.dbg_name = "gcc_gp1_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_gp1_clk.c),
	},
};

static struct branch_clk gcc_gp2_clk = {
	.cbcr_reg = GP2_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gp2_clk_src.c,
		.dbg_name = "gcc_gp2_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_gp2_clk.c),
	},
};

static struct branch_clk gcc_gp3_clk = {
	.cbcr_reg = GP3_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &gp3_clk_src.c,
		.dbg_name = "gcc_gp3_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_gp3_clk.c),
	},
};

static struct branch_clk gcc_lpass_q6_axi_clk = {
	.cbcr_reg = LPASS_Q6_AXI_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	 /* FIXME: Remove this once simulation is fixed. */
	.halt_check = DELAY,
	.c = {
		.dbg_name = "gcc_lpass_q6_axi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_lpass_q6_axi_clk.c),
	},
};

static struct branch_clk gcc_mmss_noc_cfg_ahb_clk = {
	.cbcr_reg = MMSS_NOC_CFG_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_mmss_noc_cfg_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_mmss_noc_cfg_ahb_clk.c),
	},
};

static struct branch_clk gcc_mss_cfg_ahb_clk = {
	.cbcr_reg = MSS_CFG_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_mss_cfg_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_mss_cfg_ahb_clk.c),
	},
};

static struct branch_clk gcc_mss_q6_bimc_axi_clk = {
	.cbcr_reg = MSS_Q6_BIMC_AXI_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_mss_q6_bimc_axi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_mss_q6_bimc_axi_clk.c),
	},
};

static struct branch_clk gcc_pdm2_clk = {
	.cbcr_reg = PDM2_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &pdm2_clk_src.c,
		.dbg_name = "gcc_pdm2_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_pdm2_clk.c),
	},
};

static struct branch_clk gcc_pdm_ahb_clk = {
	.cbcr_reg = PDM_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_pdm_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_pdm_ahb_clk.c),
	},
};

static struct local_vote_clk gcc_prng_ahb_clk = {
	.cbcr_reg = PRNG_AHB_CBCR,
	.vote_reg = APCS_CLOCK_BRANCH_ENA_VOTE,
	.en_mask = BIT(13),
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_prng_ahb_clk",
		.ops = &clk_ops_vote,
		CLK_INIT(gcc_prng_ahb_clk.c),
	},
};

static struct branch_clk gcc_sdcc1_ahb_clk = {
	.cbcr_reg = SDCC1_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_sdcc1_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_sdcc1_ahb_clk.c),
	},
};

static struct branch_clk gcc_sdcc1_apps_clk = {
	.cbcr_reg = SDCC1_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &sdcc1_apps_clk_src.c,
		.dbg_name = "gcc_sdcc1_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_sdcc1_apps_clk.c),
	},
};

static struct branch_clk gcc_sdcc2_ahb_clk = {
	.cbcr_reg = SDCC2_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_sdcc2_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_sdcc2_ahb_clk.c),
	},
};

static struct branch_clk gcc_sdcc2_apps_clk = {
	.cbcr_reg = SDCC2_APPS_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &sdcc2_apps_clk_src.c,
		.dbg_name = "gcc_sdcc2_apps_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_sdcc2_apps_clk.c),
	},
};

static struct branch_clk gcc_usb2a_phy_sleep_clk = {
	.cbcr_reg = USB2A_PHY_SLEEP_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_usb2a_phy_sleep_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_usb2a_phy_sleep_clk.c),
	},
};

static struct branch_clk gcc_usb_hs_ahb_clk = {
	.cbcr_reg = USB_HS_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.dbg_name = "gcc_usb_hs_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_usb_hs_ahb_clk.c),
	},
};

static struct branch_clk gcc_usb_hs_system_clk = {
	.cbcr_reg = USB_HS_SYSTEM_CBCR,
	.has_sibling = 0,
	.bcr_reg = USB_HS_BCR,
	.base = &virt_bases[GCC_BASE],
	.c = {
		.parent = &usb_hs_system_clk_src.c,
		.dbg_name = "gcc_usb_hs_system_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gcc_usb_hs_system_clk.c),
	},
};

static struct clk_freq_tbl ftbl_csi0_1_clk[] = {
	F_MM(100000000,  gpll0, 6, 0, 0),
	F_MM(200000000, mmpll0, 4, 0, 0),
	F_END,
};

static struct rcg_clk csi0_clk_src = {
	.cmd_rcgr_reg = CSI0_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_csi0_1_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "csi0_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(csi0_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_mmss_mmssnoc_ahb_clk[] = {
	F_MM(19200000, gcc_xo,  1, 0, 0),
	F_MM(40000000,  gpll0, 15, 0, 0),
	F_MM(80000000, mmpll0, 10, 0, 0),
	F_END,
};

static struct rcg_clk ahb_clk_src = {
	.cmd_rcgr_reg = AHB_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_mmss_mmssnoc_ahb_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "ahb_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP2(LOW, 40000000, NOMINAL, 80000000),
		CLK_INIT(ahb_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_mmss_mmssnoc_axi_clk[] = {
	F_MM( 19200000, gcc_xo,  1, 0, 0),
	F_MM( 37500000,  gpll0, 16, 0, 0),
	F_MM( 50000000,  gpll0, 12, 0, 0),
	F_MM( 75000000,  gpll0,  8, 0, 0),
	F_MM(100000000,  gpll0,  6, 0, 0),
	F_MM(150000000,  gpll0,  4, 0, 0),
	F_MM(200000000, mmpll0,  4, 0, 0),
	F_END,
};

static struct rcg_clk axi_clk_src = {
	.cmd_rcgr_reg = AXI_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_mmss_mmssnoc_axi_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "axi_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(axi_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_dsi_pclk_clk[] = {
	F_MDSS( 50000000, dsipll, 10, 0, 0),
	F_MDSS(103330000, dsipll,  9, 0, 0),
	F_END,
};

static struct rcg_clk dsi_pclk_clk_src = {
	.cmd_rcgr_reg =  DSI_PCLK_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_dsi_pclk_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "dsi_pclk_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 50000000, NOMINAL, 103330000),
		CLK_INIT(dsi_pclk_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_oxili_gfx3d_clk[] = {
	F_MM( 19200000, gcc_xo,  1, 0, 0),
	F_MM( 37500000,  gpll0, 16, 0, 0),
	F_MM( 50000000,  gpll0, 12, 0, 0),
	F_MM( 75000000,  gpll0,  8, 0, 0),
	F_MM(100000000,  gpll0,  6, 0, 0),
	F_MM(150000000,  gpll0,  4, 0, 0),
	F_MM(200000000,  gpll0,  3, 0, 0),
	F_MM(300000000,  gpll0,  2, 0, 0),
	F_MM(400000000, mmpll1,  3, 0, 0),
	F_END,
};

static struct rcg_clk gfx3d_clk_src = {
	.cmd_rcgr_reg = GFX3D_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_oxili_gfx3d_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "gfx3d_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP3(LOW, 150000000, NOMINAL, 300000000, HIGH,
					400000000),
		CLK_INIT(gfx3d_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_vfe_clk[] = {
	F_MM( 37500000,  gpll0,  16, 0, 0),
	F_MM( 50000000,  gpll0,  12, 0, 0),
	F_MM( 60000000,  gpll0,  10, 0, 0),
	F_MM( 80000000,  gpll0, 7.5, 0, 0),
	F_MM(100000000,  gpll0,   6, 0, 0),
	F_MM(109090000,  gpll0, 5.5, 0, 0),
	F_MM(133330000,  gpll0, 4.5, 0, 0),
	F_MM(200000000,  gpll0,   3, 0, 0),
	F_MM(228570000, mmpll0, 3.5, 0, 0),
	F_MM(266670000, mmpll0,   3, 0, 0),
	F_MM(320000000, mmpll0, 2.5, 0, 0),
	F_END,
};

static struct rcg_clk vfe_clk_src = {
	.cmd_rcgr_reg = VFE_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_vfe_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "vfe_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP3(LOW, 133330000, NOMINAL, 266670000, HIGH,
					320000000),
		CLK_INIT(vfe_clk_src.c),
	},
};

static struct rcg_clk csi1_clk_src = {
	.cmd_rcgr_reg = CSI1_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_csi0_1_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "csi1_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(csi1_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_csi0_1phytimer_clk[] = {
	F_MM(100000000,  gpll0, 6, 0, 0),
	F_MM(200000000, mmpll0, 4, 0, 0),
	F_END,
};

static struct rcg_clk csi0phytimer_clk_src = {
	.cmd_rcgr_reg = CSI0PHYTIMER_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_csi0_1phytimer_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "csi0phytimer_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(csi0phytimer_clk_src.c),
	},
};

static struct rcg_clk csi1phytimer_clk_src = {
	.cmd_rcgr_reg = CSI1PHYTIMER_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_csi0_1phytimer_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "csi1phytimer_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP2(LOW, 100000000, NOMINAL, 200000000),
		CLK_INIT(csi1phytimer_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_dsi_clk[] = {
	F_MDSS(155000000,  dsipll, 6, 0, 0),
	F_MDSS(310000000,  dsipll, 3, 0, 0),
	F_END,
};

static struct rcg_clk dsi_clk_src = {
	.cmd_rcgr_reg =  DSI_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_dsi_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "dsi_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 155000000, NOMINAL, 310000000),
		CLK_INIT(dsi_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_dsi_byte_clk[] = {
	F_MDSS( 62500000, dsipll, 12, 0, 0),
	F_MDSS(125000000, dsipll,  6, 0, 0),
	F_END,
};

static struct rcg_clk dsi_byte_clk_src = {
	.cmd_rcgr_reg = DSI_BYTE_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_dsi_byte_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "dsi_byte_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP2(LOW, 62500000, NOMINAL, 125000000),
		CLK_INIT(dsi_byte_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_dsi_esc_clk[] = {
	F_MM(19200000, gcc_xo, 1, 0, 0),
	F_END,
};

static struct rcg_clk dsi_esc_clk_src = {
	.cmd_rcgr_reg = DSI_ESC_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_dsi_esc_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "dsi_esc_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP1(LOW, 19200000),
		CLK_INIT(dsi_esc_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_mclk0_1_clk[] = {
	F_MM(66670000, gpll0, 9, 0, 0),
	F_END,
};

static struct rcg_clk mclk0_clk_src = {
	.cmd_rcgr_reg =  MCLK0_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_mclk0_1_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "mclk0_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP1(LOW, 66670000),
		CLK_INIT(mclk0_clk_src.c),
	},
};

static struct rcg_clk mclk1_clk_src = {
	.cmd_rcgr_reg =  MCLK1_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_mclk0_1_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "mclk1_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP1(LOW, 66670000),
		CLK_INIT(mclk1_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_mdp_vsync_clk[] = {
	F_MM(19200000, gcc_xo, 1, 0, 0),
	F_END,
};

static struct rcg_clk mdp_vsync_clk_src = {
	.cmd_rcgr_reg = MDP_VSYNC_CMD_RCGR,
	.set_rate = set_rate_hid,
	.freq_tbl = ftbl_mdp_vsync_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "mdp_vsync_clk_src",
		.ops = &clk_ops_rcg,
		VDD_DIG_FMAX_MAP1(LOW, 19200000),
		CLK_INIT(mdp_vsync_clk_src.c),
	},
};

static struct branch_clk bimc_gfx_clk = {
	.cbcr_reg = BIMC_GFX_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	 /* FIXME: Remove this once simulation is fixed. */
	.halt_check = DELAY,
	.c = {
		.dbg_name = "bimc_gfx_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(bimc_gfx_clk.c),
	},
};

static struct branch_clk csi0_clk = {
	.cbcr_reg = CSI0_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi0_clk_src.c,
		.dbg_name = "csi0_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi0_clk.c),
	},
};

static struct branch_clk csi0phy_clk = {
	.cbcr_reg = CSI0PHY_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi0_clk_src.c,
		.dbg_name = "csi0phy_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi0phy_clk.c),
	},
};

static struct branch_clk csi0phytimer_clk = {
	.cbcr_reg = CSI0PHYTIMER_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi0phytimer_clk_src.c,
		.dbg_name = "csi0phytimer_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi0phytimer_clk.c),
	},
};

static struct branch_clk csi0pix_clk = {
	.cbcr_reg = CSI0PIX_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi0_clk_src.c,
		.dbg_name = "csi0pix_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi0pix_clk.c),
	},
};

static struct branch_clk csi0rdi_clk = {
	.cbcr_reg = CSI0RDI_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi0_clk_src.c,
		.dbg_name = "csi0rdi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi0rdi_clk.c),
	},
};

static struct branch_clk csi1_clk = {
	.cbcr_reg = CSI1_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi1_clk_src.c,
		.dbg_name = "csi1_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi1_clk.c),
	},
};

static struct branch_clk csi1phy_clk = {
	.cbcr_reg = CSI1PHY_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi1_clk_src.c,
		.dbg_name = "csi1phy_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi1phy_clk.c),
	},
};

static struct branch_clk csi1phytimer_clk = {
	.cbcr_reg = CSI1PHYTIMER_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi1phytimer_clk_src.c,
		.dbg_name = "csi1phytimer_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi1phytimer_clk.c),
	},
};

static struct branch_clk csi1pix_clk = {
	.cbcr_reg = CSI1PIX_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi1_clk_src.c,
		.dbg_name = "csi1pix_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi1pix_clk.c),
	},
};

static struct branch_clk csi1rdi_clk = {
	.cbcr_reg = CSI1RDI_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &csi1_clk_src.c,
		.dbg_name = "csi1rdi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi1rdi_clk.c),
	},
};

static struct branch_clk csi_ahb_clk = {
	.cbcr_reg = CSI_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "csi_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi_ahb_clk.c),
	},
};

static struct branch_clk csi_vfe_clk = {
	.cbcr_reg = CSI_VFE_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &vfe_clk_src.c,
		.dbg_name = "csi_vfe_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(csi_vfe_clk.c),
	},
};

static struct branch_clk dsi_clk = {
	.cbcr_reg = DSI_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &dsi_clk_src.c,
		.dbg_name = "dsi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(dsi_clk.c),
	},
};

static struct branch_clk dsi_ahb_clk = {
	.cbcr_reg = DSI_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "dsi_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(dsi_ahb_clk.c),
	},
};

static struct branch_clk dsi_byte_clk = {
	.cbcr_reg = DSI_BYTE_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &dsi_byte_clk_src.c,
		.dbg_name = "dsi_byte_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(dsi_byte_clk.c),
	},
};

static struct branch_clk dsi_esc_clk = {
	.cbcr_reg = DSI_ESC_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &dsi_esc_clk_src.c,
		.dbg_name = "dsi_esc_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(dsi_esc_clk.c),
	},
};

static struct branch_clk dsi_pclk_clk = {
	.cbcr_reg = DSI_PCLK_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &dsi_pclk_clk_src.c,
		.dbg_name = "dsi_pclk_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(dsi_pclk_clk.c),
	},
};

static struct branch_clk gmem_gfx3d_clk = {
	.cbcr_reg = GMEM_GFX3D_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &gfx3d_clk_src.c,
		.dbg_name = "gmem_gfx3d_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(gmem_gfx3d_clk.c),
	},
};

static struct branch_clk mclk0_clk = {
	.cbcr_reg = MCLK0_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &mclk0_clk_src.c,
		.dbg_name = "mclk0_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mclk0_clk.c),
	},
};

static struct branch_clk mclk1_clk = {
	.cbcr_reg = MCLK1_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &mclk1_clk_src.c,
		.dbg_name = "mclk1_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mclk1_clk.c),
	},
};

static struct branch_clk mdp_ahb_clk = {
	.cbcr_reg = MDP_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "mdp_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mdp_ahb_clk.c),
	},
};

static struct branch_clk mdp_axi_clk = {
	.cbcr_reg = MDP_AXI_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	 /* FIXME: Remove this once simulation is fixed. */
	.halt_check = DELAY,
	.c = {
		.parent = &axi_clk_src.c,
		.dbg_name = "mdp_axi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mdp_axi_clk.c),
	},
};

static struct branch_clk mdp_dsi_clk = {
	.cbcr_reg = MDP_DSI_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &dsi_pclk_clk_src.c,
		.dbg_name = "mdp_dsi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mdp_dsi_clk.c),
	},
};

static struct branch_clk mdp_lcdc_clk = {
	.cbcr_reg = MDP_LCDC_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &dsi_pclk_clk_src.c,
		.dbg_name = "mdp_lcdc_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mdp_lcdc_clk.c),
	},
};

static struct branch_clk mdp_vsync_clk = {
	.cbcr_reg = MDP_VSYNC_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &mdp_vsync_clk_src.c,
		.dbg_name = "mdp_vsync_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mdp_vsync_clk.c),
	},
};

static struct branch_clk mmss_misc_ahb_clk = {
	.cbcr_reg = MMSS_MISC_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "mmss_misc_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mmss_misc_ahb_clk.c),
	},
};

static struct branch_clk mmss_mmssnoc_axi_clk = {
	.cbcr_reg = MMSS_MMSSNOC_AXI_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &axi_clk_src.c,
		.dbg_name = "mmss_mmssnoc_axi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mmss_mmssnoc_axi_clk.c),
	},
};

static struct branch_clk mmss_s0_axi_clk = {
	.cbcr_reg = MMSS_S0_AXI_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &axi_clk_src.c,
		.dbg_name = "mmss_s0_axi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mmss_s0_axi_clk.c),
		.depends = &mmss_mmssnoc_axi_clk.c,
	},
};

static struct branch_clk mmss_mmssnoc_ahb_clk = {
	.cbcr_reg = MMSS_MMSSNOC_AHB_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &ahb_clk_src.c,
		.dbg_name = "mmss_mmssnoc_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mmss_mmssnoc_ahb_clk.c),
	},
};

static struct branch_clk mmss_mmssnoc_bto_ahb_clk = {
	.cbcr_reg = MMSS_MMSSNOC_BTO_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "mmss_mmssnoc_bto_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(mmss_mmssnoc_bto_ahb_clk.c),
	},
};

static struct branch_clk oxili_ahb_clk = {
	.cbcr_reg = OXILI_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "oxili_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(oxili_ahb_clk.c),
	},
};

static struct branch_clk oxili_gfx3d_clk = {
	.cbcr_reg = OXILI_GFX3D_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &gfx3d_clk_src.c,
		.dbg_name = "oxili_gfx3d_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(oxili_gfx3d_clk.c),
	},
};

static struct branch_clk vfe_clk = {
	.cbcr_reg = VFE_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.parent = &vfe_clk_src.c,
		.dbg_name = "vfe_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(vfe_clk.c),
	},
};

static struct branch_clk vfe_ahb_clk = {
	.cbcr_reg = VFE_AHB_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	.c = {
		.dbg_name = "vfe_ahb_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(vfe_ahb_clk.c),
	},
};

static struct branch_clk vfe_axi_clk = {
	.cbcr_reg = VFE_AXI_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[MMSS_BASE],
	 /* FIXME: Remove this once simulation is fixed. */
	.halt_check = DELAY,
	.c = {
		.parent = &axi_clk_src.c,
		.dbg_name = "vfe_axi_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(vfe_axi_clk.c),
	},
};

static struct clk_freq_tbl ftbl_audio_core_lpaif_clk[] = {
	F_LPASS(  512000, lpapll0, 16, 1, 60),
	F_LPASS(  768000, lpapll0, 16, 1, 40),
	F_LPASS( 1024000, lpapll0, 16, 1, 30),
	F_LPASS( 1536000, lpapll0, 16, 1, 20),
	F_LPASS( 2048000, lpapll0, 16, 1, 15),
	F_LPASS( 3072000, lpapll0, 16, 1, 10),
	F_LPASS( 4096000, lpapll0, 15, 1,  8),
	F_LPASS( 6144000, lpapll0, 10, 1,  8),
	F_LPASS( 8192000, lpapll0, 15, 1,  4),
	F_LPASS(12288000, lpapll0, 10, 1,  4),
	F_END,
};

static struct rcg_clk lpaif_pri_clk_src = {
	.cmd_rcgr_reg =  LPAIF_PRI_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_audio_core_lpaif_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "lpaif_pri_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 12290000, NOMINAL, 24580000),
		CLK_INIT(lpaif_pri_clk_src.c),
	},
};

static struct rcg_clk lpaif_quad_clk_src = {
	.cmd_rcgr_reg =  LPAIF_QUAD_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_audio_core_lpaif_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "lpaif_quad_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 12290000, NOMINAL, 24580000),
		CLK_INIT(lpaif_quad_clk_src.c),
	},
};

static struct rcg_clk lpaif_sec_clk_src = {
	.cmd_rcgr_reg =  LPAIF_SEC_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_audio_core_lpaif_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "lpaif_sec_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 12290000, NOMINAL, 24580000),
		CLK_INIT(lpaif_sec_clk_src.c),
	},
};

static struct rcg_clk lpaif_spkr_clk_src = {
	.cmd_rcgr_reg =  LPAIF_SPKR_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_audio_core_lpaif_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "lpaif_spkr_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 12290000, NOMINAL, 24580000),
		CLK_INIT(lpaif_spkr_clk_src.c),
	},
};

static struct rcg_clk lpaif_ter_clk_src = {
	.cmd_rcgr_reg =  LPAIF_TER_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_audio_core_lpaif_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "lpaif_ter_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 12290000, NOMINAL, 24580000),
		CLK_INIT(lpaif_ter_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_audio_core_lpaif_pcm0_1_clk[] = {
	F_LPASS( 512000, lpapll0, 16, 1, 60),
	F_LPASS( 768000, lpapll0, 16, 1, 40),
	F_LPASS(1024000, lpapll0, 16, 1, 30),
	F_LPASS(1536000, lpapll0, 16, 1, 20),
	F_LPASS(2048000, lpapll0, 16, 1, 15),
	F_LPASS(3072000, lpapll0, 16, 1, 10),
	F_LPASS(4096000, lpapll0, 15, 1,  8),
	F_LPASS(6144000, lpapll0, 10, 1,  8),
	F_LPASS(8192000, lpapll0, 15, 1,  4),
	F_END,
};

static struct rcg_clk lpaif_pcm0_clk_src = {
	.cmd_rcgr_reg =  LPAIF_PCM0_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_audio_core_lpaif_pcm0_1_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "lpaif_pcm0_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 4100000, NOMINAL, 8192000),
		CLK_INIT(lpaif_pcm0_clk_src.c),
	},
};

static struct rcg_clk lpaif_pcm1_clk_src = {
	.cmd_rcgr_reg =  LPAIF_PCM1_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_audio_core_lpaif_pcm0_1_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "lpaif_pcm1_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 4100000, NOMINAL, 8192000),
		CLK_INIT(lpaif_pcm1_clk_src.c),
	},
};

static struct rcg_clk lpaif_pcmoe_clk_src = {
	.cmd_rcgr_reg =  LPAIF_PCMOE_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_audio_core_lpaif_pcm0_1_clk,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "lpaif_pcmoe_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 6140000, NOMINAL, 12290000),
		CLK_INIT(lpaif_pcmoe_clk_src.c),
	},
};

static struct clk_freq_tbl ftbl_audio_core_slimbus_core_clock[] = {
	F_LPASS(24576000, lpapll0, 4, 1, 5),
	F_END
};

static struct rcg_clk audio_core_slimbus_core_clk_src = {
	.cmd_rcgr_reg = SLIMBUS_CMD_RCGR,
	.set_rate = set_rate_mnd,
	.freq_tbl = ftbl_audio_core_slimbus_core_clock,
	.current_freq = &rcg_dummy_freq,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_core_slimbus_core_clk_src",
		.ops = &clk_ops_rcg_mnd,
		VDD_DIG_FMAX_MAP2(LOW, 12935000, NOMINAL, 25869000),
		CLK_INIT(audio_core_slimbus_core_clk_src.c),
	},
};

static struct branch_clk audio_core_slimbus_core_clk = {
	.cbcr_reg = AUDIO_CORE_SLIMBUS_CORE_CBCR,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &audio_core_slimbus_core_clk_src.c,
		.dbg_name = "audio_core_slimbus_core_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_slimbus_core_clk.c),
	},
};

static struct branch_clk audio_core_ixfabric_clk = {
	.cbcr_reg = AUDIO_CORE_IXFABRIC_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_core_ixfabric_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_ixfabric_clk.c),
	},
};

static struct branch_clk audio_wrapper_br_clk = {
	.cbcr_reg = AUDIO_WRAPPER_BR_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_wrapper_br_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_wrapper_br_clk.c),
	},
};

static struct branch_clk q6ss_ahb_lfabif_clk = {
	.cbcr_reg = Q6SS_AHB_LFABIF_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "q6ss_ahb_lfabif_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(q6ss_ahb_lfabif_clk.c),
	},
};

static struct branch_clk q6ss_ahbm_clk = {
	.cbcr_reg = Q6SS_AHBM_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "q6ss_ahbm_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(q6ss_ahbm_clk.c),
	},
};

static struct branch_clk q6ss_xo_clk = {
	.cbcr_reg = Q6SS_XO_CBCR,
	.has_sibling = 1,
	.bcr_reg = LPASS_Q6SS_BCR,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &gcc_xo_clk_src.c,
		.dbg_name = "q6ss_xo_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(q6ss_xo_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_pcm_data_oe_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_PCM_DATA_OE_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_pcmoe_clk_src.c,
		.dbg_name = "audio_core_lpaif_pcm_data_oe_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_pcm_data_oe_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_pri_ebit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_PRI_EBIT_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_core_lpaif_pri_ebit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_pri_ebit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_pri_ibit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_PRI_IBIT_CBCR,
	.has_sibling = 0,
	.max_div = 511,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_pri_clk_src.c,
		.dbg_name = "audio_core_lpaif_pri_ibit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_pri_ibit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_pri_osr_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_PRI_OSR_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_pri_clk_src.c,
		.dbg_name = "audio_core_lpaif_pri_osr_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_pri_osr_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_pcm0_ebit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_PCM0_EBIT_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_core_lpaif_pcm0_ebit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_pcm0_ebit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_pcm0_ibit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_PCM0_IBIT_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_pcm0_clk_src.c,
		.dbg_name = "audio_core_lpaif_pcm0_ibit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_pcm0_ibit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_quad_ebit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_QUAD_EBIT_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_core_lpaif_quad_ebit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_quad_ebit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_quad_ibit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_QUAD_IBIT_CBCR,
	.has_sibling = 0,
	.max_div = 511,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_quad_clk_src.c,
		.dbg_name = "audio_core_lpaif_quad_ibit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_quad_ibit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_quad_osr_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_QUAD_OSR_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_quad_clk_src.c,
		.dbg_name = "audio_core_lpaif_quad_osr_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_quad_osr_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_sec_ebit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_SEC_EBIT_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_core_lpaif_sec_ebit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_sec_ebit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_sec_ibit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_SEC_IBIT_CBCR,
	.has_sibling = 0,
	.max_div = 511,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_sec_clk_src.c,
		.dbg_name = "audio_core_lpaif_sec_ibit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_sec_ibit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_sec_osr_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_SEC_OSR_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_sec_clk_src.c,
		.dbg_name = "audio_core_lpaif_sec_osr_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_sec_osr_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_pcm1_ebit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_PCM1_EBIT_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_core_lpaif_pcm1_ebit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_pcm1_ebit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_pcm1_ibit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_PCM1_IBIT_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_pcm1_clk_src.c,
		.dbg_name = "audio_core_lpaif_pcm1_ibit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_pcm1_ibit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_codec_spkr_ebit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_CODEC_SPKR_EBIT_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_core_lpaif_codec_spkr_ebit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_codec_spkr_ebit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_codec_spkr_ibit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_CODEC_SPKR_IBIT_CBCR,
	.has_sibling = 1,
	.max_div = 511,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_spkr_clk_src.c,
		.dbg_name = "audio_core_lpaif_codec_spkr_ibit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_codec_spkr_ibit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_codec_spkr_osr_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_CODEC_SPKR_OSR_CBCR,
	.has_sibling = 1,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_spkr_clk_src.c,
		.dbg_name = "audio_core_lpaif_codec_spkr_osr_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_codec_spkr_osr_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_ter_ebit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_TER_EBIT_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.dbg_name = "audio_core_lpaif_ter_ebit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_ter_ebit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_ter_ibit_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_TER_IBIT_CBCR,
	.has_sibling = 0,
	.max_div = 511,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_ter_clk_src.c,
		.dbg_name = "audio_core_lpaif_ter_ibit_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_ter_ibit_clk.c),
	},
};

static struct branch_clk audio_core_lpaif_ter_osr_clk = {
	.cbcr_reg = AUDIO_CORE_LPAIF_TER_OSR_CBCR,
	.has_sibling = 0,
	.base = &virt_bases[LPASS_BASE],
	.c = {
		.parent = &lpaif_ter_clk_src.c,
		.dbg_name = "audio_core_lpaif_ter_osr_clk",
		.ops = &clk_ops_branch,
		CLK_INIT(audio_core_lpaif_ter_osr_clk.c),
	},
};

#ifdef CONFIG_DEBUG_FS

struct measure_mux_entry {
	struct clk *c;
	int base;
	u32 debug_mux;
};

static struct measure_mux_entry measure_mux[] = {
	{                   &snoc_clk.c, GCC_BASE, 0x0000},
	{                   &cnoc_clk.c, GCC_BASE, 0x0008},
	{     &gcc_copss_smmu_ahb_clk.c, GCC_BASE, 0x000c},
	{      &gcc_lpss_smmu_ahb_clk.c, GCC_BASE, 0x000d},
	{                   &pnoc_clk.c, GCC_BASE, 0x0010},
	{   &gcc_mmss_noc_cfg_ahb_clk.c, GCC_BASE, 0x002a},
	{        &gcc_mss_cfg_ahb_clk.c, GCC_BASE, 0x0030},
	{    &gcc_mss_q6_bimc_axi_clk.c, GCC_BASE, 0x0031},
	{      &gcc_usb_hs_system_clk.c, GCC_BASE, 0x0060},
	{         &gcc_usb_hs_ahb_clk.c, GCC_BASE, 0x0061},
	{    &gcc_usb2a_phy_sleep_clk.c, GCC_BASE, 0x0063},
	{         &gcc_sdcc1_apps_clk.c, GCC_BASE, 0x0068},
	{          &gcc_sdcc1_ahb_clk.c, GCC_BASE, 0x0069},
	{         &gcc_sdcc2_apps_clk.c, GCC_BASE, 0x0070},
	{          &gcc_sdcc2_ahb_clk.c, GCC_BASE, 0x0071},
	{          &gcc_blsp1_ahb_clk.c, GCC_BASE, 0x0088},
	{&gcc_blsp1_qup1_spi_apps_clk.c, GCC_BASE, 0x008a},
	{&gcc_blsp1_qup1_i2c_apps_clk.c, GCC_BASE, 0x008b},
	{   &gcc_blsp1_uart1_apps_clk.c, GCC_BASE, 0x008c},
	{&gcc_blsp1_qup2_spi_apps_clk.c, GCC_BASE, 0x008e},
	{&gcc_blsp1_qup2_i2c_apps_clk.c, GCC_BASE, 0x0090},
	{   &gcc_blsp1_uart2_apps_clk.c, GCC_BASE, 0x0091},
	{&gcc_blsp1_qup3_spi_apps_clk.c, GCC_BASE, 0x0093},
	{&gcc_blsp1_qup3_i2c_apps_clk.c, GCC_BASE, 0x0094},
	{   &gcc_blsp1_uart3_apps_clk.c, GCC_BASE, 0x0095},
	{&gcc_blsp1_qup4_spi_apps_clk.c, GCC_BASE, 0x0098},
	{&gcc_blsp1_qup4_i2c_apps_clk.c, GCC_BASE, 0x0099},
	{   &gcc_blsp1_uart4_apps_clk.c, GCC_BASE, 0x009a},
	{&gcc_blsp1_qup5_spi_apps_clk.c, GCC_BASE, 0x009c},
	{&gcc_blsp1_qup5_i2c_apps_clk.c, GCC_BASE, 0x009d},
	{   &gcc_blsp1_uart5_apps_clk.c, GCC_BASE, 0x009e},
	{&gcc_blsp1_qup6_spi_apps_clk.c, GCC_BASE, 0x00a1},
	{&gcc_blsp1_qup6_i2c_apps_clk.c, GCC_BASE, 0x00a2},
	{   &gcc_blsp1_uart6_apps_clk.c, GCC_BASE, 0x00a3},
	{            &gcc_pdm_ahb_clk.c, GCC_BASE, 0x00d0},
	{               &gcc_pdm2_clk.c, GCC_BASE, 0x00d2},
	{           &gcc_prng_ahb_clk.c, GCC_BASE, 0x00d8},
	{       &gcc_boot_rom_ahb_clk.c, GCC_BASE, 0x00f8},
	{                &gcc_ce1_clk.c, GCC_BASE, 0x0138},
	{            &gcc_ce1_axi_clk.c, GCC_BASE, 0x0139},
	{            &gcc_ce1_ahb_clk.c, GCC_BASE, 0x013a},
	{             &gcc_xo_clk_src.c, GCC_BASE, 0x0149},
	{                   &bimc_clk.c, GCC_BASE, 0x0154},
	{       &gcc_lpass_q6_axi_clk.c, GCC_BASE, 0x0160},

	{&mmss_mmssnoc_ahb_clk.c, MMSS_BASE, 0x0001},
	{   &mmss_misc_ahb_clk.c, MMSS_BASE, 0x0003},
	{&mmss_mmssnoc_axi_clk.c, MMSS_BASE, 0x0004},
	{     &mmss_s0_axi_clk.c, MMSS_BASE, 0x0005},
	{       &oxili_ahb_clk.c, MMSS_BASE, 0x0007},
	{     &oxili_gfx3d_clk.c, MMSS_BASE, 0x0008},
	{      &gmem_gfx3d_clk.c, MMSS_BASE, 0x0009},
	{         &mdp_axi_clk.c, MMSS_BASE, 0x000a},
	{       &mdp_vsync_clk.c, MMSS_BASE, 0x000b},
	{         &mdp_ahb_clk.c, MMSS_BASE, 0x000c},
	{        &dsi_pclk_clk.c, MMSS_BASE, 0x000d},
	{         &mdp_dsi_clk.c, MMSS_BASE, 0x000e},
	{        &mdp_lcdc_clk.c, MMSS_BASE, 0x000f},
	{             &dsi_clk.c, MMSS_BASE, 0x0010},
	{        &dsi_byte_clk.c, MMSS_BASE, 0x0011},
	{         &dsi_esc_clk.c, MMSS_BASE, 0x0012},
	{         &dsi_ahb_clk.c, MMSS_BASE, 0x0013},
	{           &mclk0_clk.c, MMSS_BASE, 0x0015},
	{           &mclk1_clk.c, MMSS_BASE, 0x0016},
	{    &csi0phytimer_clk.c, MMSS_BASE, 0x0017},
	{    &csi1phytimer_clk.c, MMSS_BASE, 0x0018},
	{             &vfe_clk.c, MMSS_BASE, 0x0019},
	{         &vfe_ahb_clk.c, MMSS_BASE, 0x001a},
	{         &vfe_axi_clk.c, MMSS_BASE, 0x001b},
	{         &csi_vfe_clk.c, MMSS_BASE, 0x001c},
	{            &csi0_clk.c, MMSS_BASE, 0x001d},
	{         &csi_ahb_clk.c, MMSS_BASE, 0x001e},
	{         &csi0phy_clk.c, MMSS_BASE, 0x001f},
	{         &csi0rdi_clk.c, MMSS_BASE, 0x0020},
	{         &csi0pix_clk.c, MMSS_BASE, 0x0021},
	{            &csi1_clk.c, MMSS_BASE, 0x0022},
	{         &csi1phy_clk.c, MMSS_BASE, 0x0023},
	{         &csi1rdi_clk.c, MMSS_BASE, 0x0024},
	{         &csi1pix_clk.c, MMSS_BASE, 0x0025},
	{        &bimc_gfx_clk.c, MMSS_BASE, 0x0032},

	{             &lpaif_pcmoe_clk_src.c, LPASS_BASE, 0x000f},
	{              &lpaif_pcm1_clk_src.c, LPASS_BASE, 0x0012},
	{              &lpaif_pcm0_clk_src.c, LPASS_BASE, 0x0013},
	{              &lpaif_quad_clk_src.c, LPASS_BASE, 0x0014},
	{               &lpaif_ter_clk_src.c, LPASS_BASE, 0x0015},
	{               &lpaif_sec_clk_src.c, LPASS_BASE, 0x0016},
	{               &lpaif_pri_clk_src.c, LPASS_BASE, 0x0017},
	{              &lpaif_spkr_clk_src.c, LPASS_BASE, 0x0018},
	{                   &q6ss_ahbm_clk.c, LPASS_BASE, 0x001d},
	{             &q6ss_ahb_lfabif_clk.c, LPASS_BASE, 0x001e},
	{            &audio_wrapper_br_clk.c, LPASS_BASE, 0x0022},
	{                     &q6ss_xo_clk.c, LPASS_BASE, 0x002b},
	{&audio_core_lpaif_pcm_data_oe_clk.c, LPASS_BASE, 0x0030},
	{         &audio_core_ixfabric_clk.c, LPASS_BASE, 0x0059},

	{&dummy_clk, N_BASES, 0x0000},
};

#define GCC_DEBUG_CLK_CTL		0x1880
#define MMSS_DEBUG_CLK_CTL		0x0900
#define LPASS_DEBUG_CLK_CTL		0x29000
#define GLB_CLK_DIAG			0x001C

static int measure_clk_set_parent(struct clk *c, struct clk *parent)
{
	struct measure_clk *clk = to_measure_clk(c);
	unsigned long flags;
	u32 regval, clk_sel, i;

	if (!parent)
		return -EINVAL;

	for (i = 0; i < (ARRAY_SIZE(measure_mux) - 1); i++)
		if (measure_mux[i].c == parent)
			break;

	if (measure_mux[i].c == &dummy_clk)
		return -EINVAL;

	spin_lock_irqsave(&local_clock_reg_lock, flags);
	/*
	 * Program the test vector, measurement period (sample_ticks)
	 * and scaling multiplier.
	 */
	clk->sample_ticks = 0x10000;
	clk->multiplier = 1;

	switch (measure_mux[i].base) {

	case GCC_BASE:
		writel_relaxed(0, GCC_REG_BASE(GCC_DEBUG_CLK_CTL));
		clk_sel = measure_mux[i].debug_mux;
		break;

	case MMSS_BASE:
		writel_relaxed(0, MMSS_REG_BASE(MMSS_DEBUG_CLK_CTL));
		clk_sel = 0x02C;
		regval = BVAL(11, 0, measure_mux[i].debug_mux);
		writel_relaxed(regval, MMSS_REG_BASE(MMSS_DEBUG_CLK_CTL));

		/* Activate debug clock output */
		regval |= BIT(16);
		writel_relaxed(regval, MMSS_REG_BASE(MMSS_DEBUG_CLK_CTL));
		break;

	case LPASS_BASE:
		writel_relaxed(0, LPASS_REG_BASE(LPASS_DEBUG_CLK_CTL));
		clk_sel = 0x161;
		regval = BVAL(11, 0, measure_mux[i].debug_mux);
		writel_relaxed(regval, LPASS_REG_BASE(LPASS_DEBUG_CLK_CTL));

		/* Activate debug clock output */
		regval |= BIT(20);
		writel_relaxed(regval, LPASS_REG_BASE(LPASS_DEBUG_CLK_CTL));
		break;

	case APCS_BASE:
		clk->multiplier = 4;
		clk_sel = 0x16A;
		regval = measure_mux[i].debug_mux;
		writel_relaxed(regval, APCS_REG_BASE(GLB_CLK_DIAG));
		break;

	default:
		return -EINVAL;
	}

	/* Set debug mux clock index */
	regval = BVAL(8, 0, clk_sel);
	writel_relaxed(regval, GCC_REG_BASE(GCC_DEBUG_CLK_CTL));

	/* Activate debug clock output */
	regval |= BIT(16);
	writel_relaxed(regval, GCC_REG_BASE(GCC_DEBUG_CLK_CTL));

	/* Make sure test vector is set before starting measurements. */
	mb();
	spin_unlock_irqrestore(&local_clock_reg_lock, flags);

	return 0;
}

#define CLOCK_FRQ_MEASURE_CTL		0x1884
#define CLOCK_FRQ_MEASURE_STATUS	0x1888

/* Sample clock for 'ticks' reference clock ticks. */
static u32 run_measurement(unsigned ticks)
{
	/* Stop counters and set the XO4 counter start value. */
	writel_relaxed(ticks, GCC_REG_BASE(CLOCK_FRQ_MEASURE_CTL));

	/* Wait for timer to become ready. */
	while ((readl_relaxed(GCC_REG_BASE(CLOCK_FRQ_MEASURE_STATUS)) &
			BIT(25)) != 0)
		cpu_relax();

	/* Run measurement and wait for completion. */
	writel_relaxed(BIT(20)|ticks, GCC_REG_BASE(CLOCK_FRQ_MEASURE_CTL));
	while ((readl_relaxed(GCC_REG_BASE(CLOCK_FRQ_MEASURE_STATUS)) &
			BIT(25)) == 0)
		cpu_relax();

	/* Return measured ticks. */
	return readl_relaxed(GCC_REG_BASE(CLOCK_FRQ_MEASURE_STATUS)) &
				BM(24, 0);
}

#define GCC_XO_DIV4_CBCR	0x10C8
#define PLLTEST_PAD_CFG		0x188C

/*
 * Perform a hardware rate measurement for a given clock.
 * FOR DEBUG USE ONLY: Measurements take ~15 ms!
 */
static unsigned long measure_clk_get_rate(struct clk *c)
{
	unsigned long flags;
	u32 gcc_xo4_reg_backup;
	u64 raw_count_short, raw_count_full;
	struct measure_clk *clk = to_measure_clk(c);
	unsigned ret;

	ret = clk_prepare_enable(&gcc_xo_clk_src.c);
	if (ret) {
		pr_warning("CXO clock failed to enable. Can't measure\n");
		return 0;
	}

	spin_lock_irqsave(&local_clock_reg_lock, flags);

	/* Enable CXO/4 and RINGOSC branch. */
	gcc_xo4_reg_backup = readl_relaxed(GCC_REG_BASE(GCC_XO_DIV4_CBCR));
	writel_relaxed(0x1, GCC_REG_BASE(GCC_XO_DIV4_CBCR));

	/*
	 * The ring oscillator counter will not reset if the measured clock
	 * is not running.  To detect this, run a short measurement before
	 * the full measurement.  If the raw results of the two are the same
	 * then the clock must be off.
	 */

	/* Run a short measurement. (~1 ms) */
	raw_count_short = run_measurement(0x1000);
	/* Run a full measurement. (~14 ms) */
	raw_count_full = run_measurement(clk->sample_ticks);

	writel_relaxed(gcc_xo4_reg_backup, GCC_REG_BASE(GCC_XO_DIV4_CBCR));

	/* Return 0 if the clock is off. */
	if (raw_count_full == raw_count_short) {
		ret = 0;
	} else {
		/* Compute rate in Hz. */
		raw_count_full = ((raw_count_full * 10) + 15) * 4800000;
		do_div(raw_count_full, ((clk->sample_ticks * 10) + 35));
		ret = (raw_count_full * clk->multiplier);
	}

	writel_relaxed(0x51A00, GCC_REG_BASE(PLLTEST_PAD_CFG));
	spin_unlock_irqrestore(&local_clock_reg_lock, flags);

	clk_disable_unprepare(&gcc_xo_clk_src.c);

	return ret;
}
#else /* !CONFIG_DEBUG_FS */
static int measure_clk_set_parent(struct clk *clk, struct clk *parent)
{
	return -EINVAL;
}

static unsigned long measure_clk_get_rate(struct clk *clk)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static struct clk_ops clk_ops_measure = {
	.set_parent = measure_clk_set_parent,
	.get_rate = measure_clk_get_rate,
};

static struct measure_clk measure_clk = {
	.c = {
		.dbg_name = "measure_clk",
		.ops = &clk_ops_measure,
		CLK_INIT(measure_clk.c),
	},
	.multiplier = 1,
};

static struct clk_lookup msm_clocks_8610[] = {
	CLK_LOOKUP("xo",	gcc_xo_clk_src.c, "msm_otg"),
	CLK_LOOKUP("xo",	gcc_xo_clk_src.c, "fe200000.qcom,lpass"),
	CLK_LOOKUP("xo",	gcc_xo_clk_src.c, "pil-q6v5-mss"),
	CLK_LOOKUP("xo",	gcc_xo_clk_src.c, "pil-mba"),
	CLK_LOOKUP("xo",	gcc_xo_clk_src.c, "fb000000.qcom,wcnss-wlan"),
	CLK_LOOKUP("xo",	gcc_xo_clk_src.c, "pil_pronto"),
	CLK_LOOKUP("measure",	measure_clk.c,	"debug"),

	CLK_LOOKUP("iface_clk",  gcc_blsp1_ahb_clk.c, "f991f000.serial"),
	CLK_LOOKUP("core_clk",  gcc_blsp1_uart3_apps_clk.c, "f991f000.serial"),

	CLK_LOOKUP("dfab_clk", pnoc_sps_clk.c, "msm_sps"),
	CLK_LOOKUP("bus_clk",  pnoc_qseecom_clk.c, "qseecom"),

	CLK_LOOKUP("bus_clk", snoc_clk.c, ""),
	CLK_LOOKUP("bus_clk", pnoc_clk.c, ""),
	CLK_LOOKUP("bus_clk", cnoc_clk.c, ""),
	CLK_LOOKUP("mem_clk", bimc_clk.c, ""),
	CLK_LOOKUP("bus_clk", snoc_a_clk.c, ""),
	CLK_LOOKUP("bus_clk", pnoc_a_clk.c, ""),
	CLK_LOOKUP("bus_clk", cnoc_a_clk.c, ""),
	CLK_LOOKUP("mem_clk", bimc_a_clk.c, ""),

	CLK_LOOKUP("bus_clk",	cnoc_msmbus_clk.c,	"msm_config_noc"),
	CLK_LOOKUP("bus_a_clk",	cnoc_msmbus_a_clk.c,	"msm_config_noc"),
	CLK_LOOKUP("bus_clk",	snoc_msmbus_clk.c,	"msm_sys_noc"),
	CLK_LOOKUP("bus_a_clk",	snoc_msmbus_a_clk.c,	"msm_sys_noc"),
	CLK_LOOKUP("bus_clk",	pnoc_msmbus_clk.c,	"msm_periph_noc"),
	CLK_LOOKUP("bus_a_clk",	pnoc_msmbus_a_clk.c,	"msm_periph_noc"),
	CLK_LOOKUP("mem_clk",	bimc_msmbus_clk.c,	"msm_bimc"),
	CLK_LOOKUP("mem_a_clk",	bimc_msmbus_a_clk.c,	"msm_bimc"),
	CLK_LOOKUP("mem_clk",	bimc_acpu_a_clk.c,	""),

	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-tmc-etr"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-tpiu"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-replicator"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-tmc-etf"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-funnel-merg"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-funnel-in0"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-funnel-in1"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-funnel-kpss"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-funnel-mmss"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-stm"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-etm0"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-etm1"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-etm2"),
	CLK_LOOKUP("core_clk", qdss_clk.c, "coresight-etm3"),

	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-tmc-etr"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-tpiu"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-replicator"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-tmc-etf"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-funnel-merg"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-funnel-in0"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-funnel-in1"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-funnel-kpss"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-funnel-mmss"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-stm"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-etm0"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-etm1"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-etm2"),
	CLK_LOOKUP("core_a_clk", qdss_a_clk.c, "coresight-etm3"),

	CLK_LOOKUP("core_clk_src", blsp1_qup1_spi_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src", blsp1_qup2_spi_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src", blsp1_qup3_spi_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src", blsp1_qup4_spi_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src", blsp1_qup5_spi_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src", blsp1_qup6_spi_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",    blsp1_uart1_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",    blsp1_uart2_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",    blsp1_uart3_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",    blsp1_uart4_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",    blsp1_uart5_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",    blsp1_uart6_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                 ce1_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                 gp1_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                 gp2_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                 gp3_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                pdm2_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",          sdcc1_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",          sdcc2_apps_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",       usb_hs_system_clk_src.c, ""),

	CLK_LOOKUP("iface_clk",            gcc_blsp1_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup1_i2c_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup1_spi_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup2_i2c_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup2_spi_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup3_i2c_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup3_spi_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup4_i2c_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup4_spi_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup5_i2c_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup5_spi_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup6_i2c_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",  gcc_blsp1_qup6_spi_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",     gcc_blsp1_uart1_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",     gcc_blsp1_uart2_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",     gcc_blsp1_uart3_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",     gcc_blsp1_uart4_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",     gcc_blsp1_uart5_apps_clk.c, ""),
	CLK_LOOKUP("core_clk",     gcc_blsp1_uart6_apps_clk.c, ""),
	CLK_LOOKUP("iface_clk",         gcc_boot_rom_ahb_clk.c, ""),
	CLK_LOOKUP("iface_clk",              gcc_ce1_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",              gcc_ce1_axi_clk.c, ""),
	CLK_LOOKUP("core_clk",                  gcc_ce1_clk.c, ""),
	CLK_LOOKUP("iface_clk",       gcc_copss_smmu_ahb_clk.c, ""),
	CLK_LOOKUP("iface_clk",        gcc_lpss_smmu_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",                  gcc_gp1_clk.c, ""),
	CLK_LOOKUP("core_clk",                  gcc_gp2_clk.c, ""),
	CLK_LOOKUP("core_clk",                  gcc_gp3_clk.c, ""),
	CLK_LOOKUP("core_clk",         gcc_lpass_q6_axi_clk.c, ""),
	CLK_LOOKUP("iface_clk",          gcc_mss_cfg_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",      gcc_mss_q6_bimc_axi_clk.c, ""),
	CLK_LOOKUP("core_clk",                 gcc_pdm2_clk.c, ""),
	CLK_LOOKUP("iface_clk",              gcc_pdm_ahb_clk.c, ""),
	CLK_LOOKUP("iface_clk",             gcc_prng_ahb_clk.c, ""),
	CLK_LOOKUP("iface_clk",            gcc_sdcc1_ahb_clk.c, "msm_sdcc.1"),
	CLK_LOOKUP("core_clk",           gcc_sdcc1_apps_clk.c, "msm_sdcc.1"),
	CLK_LOOKUP("iface_clk",            gcc_sdcc2_ahb_clk.c, "msm_sdcc.2"),
	CLK_LOOKUP("core_clk",           gcc_sdcc2_apps_clk.c, "msm_sdcc.2"),
	CLK_LOOKUP("core_clk",      gcc_usb2a_phy_sleep_clk.c, ""),
	CLK_LOOKUP("iface_clk",           gcc_usb_hs_ahb_clk.c, "f9a55000.usb"),
	CLK_LOOKUP("core_clk",        gcc_usb_hs_system_clk.c, "f9a55000.usb"),

	CLK_LOOKUP("core_clk_src",                csi0_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                 axi_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",            dsi_pclk_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",               gfx3d_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                 vfe_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                csi1_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",        csi0phytimer_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",        csi1phytimer_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                 dsi_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",            dsi_byte_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",             dsi_esc_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",               mclk0_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",               mclk1_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",           mdp_vsync_clk_src.c, ""),

	CLK_LOOKUP("core_clk",                 bimc_gfx_clk.c, ""),
	CLK_LOOKUP("core_clk",                     csi0_clk.c, ""),
	CLK_LOOKUP("core_clk",                  csi0phy_clk.c, ""),
	CLK_LOOKUP("core_clk",             csi0phytimer_clk.c, ""),
	CLK_LOOKUP("core_clk",                  csi0pix_clk.c, ""),
	CLK_LOOKUP("core_clk",                  csi0rdi_clk.c, ""),
	CLK_LOOKUP("core_clk",                     csi1_clk.c, ""),
	CLK_LOOKUP("core_clk",                  csi1phy_clk.c, ""),
	CLK_LOOKUP("core_clk",             csi1phytimer_clk.c, ""),
	CLK_LOOKUP("core_clk",                  csi1pix_clk.c, ""),
	CLK_LOOKUP("core_clk",                  csi1rdi_clk.c, ""),
	CLK_LOOKUP("core_clk",                  csi_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",                  csi_vfe_clk.c, ""),
	CLK_LOOKUP("core_clk",                      dsi_clk.c, ""),
	CLK_LOOKUP("core_clk",                  dsi_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",                 dsi_byte_clk.c, ""),
	CLK_LOOKUP("core_clk",                  dsi_esc_clk.c, ""),
	CLK_LOOKUP("core_clk",                 dsi_pclk_clk.c, ""),
	CLK_LOOKUP("core_clk",               gmem_gfx3d_clk.c, ""),
	CLK_LOOKUP("core_clk",                    mclk0_clk.c, ""),
	CLK_LOOKUP("core_clk",                    mclk1_clk.c, ""),
	CLK_LOOKUP("core_clk",                  mdp_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",                  mdp_axi_clk.c, ""),
	CLK_LOOKUP("core_clk",                  mdp_dsi_clk.c, ""),
	CLK_LOOKUP("core_clk",                 mdp_lcdc_clk.c, ""),
	CLK_LOOKUP("core_clk",                mdp_vsync_clk.c, ""),
	CLK_LOOKUP("core_clk",            mmss_misc_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",              mmss_s0_axi_clk.c, ""),
	CLK_LOOKUP("core_clk",         mmss_mmssnoc_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",     mmss_mmssnoc_bto_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",         mmss_mmssnoc_axi_clk.c, ""),
	CLK_LOOKUP("core_clk",                      vfe_clk.c, ""),
	CLK_LOOKUP("core_clk",                  vfe_ahb_clk.c, ""),
	CLK_LOOKUP("core_clk",                  vfe_axi_clk.c, ""),

	CLK_LOOKUP("core_clk",   oxili_gfx3d_clk.c, "fdc00000.qcom,kgsl-3d0"),
	CLK_LOOKUP("iface_clk",    oxili_ahb_clk.c, "fdc00000.qcom,kgsl-3d0"),
	CLK_LOOKUP("mem_iface_clk", bimc_gfx_clk.c, "fdc00000.qcom,kgsl-3d0"),
	CLK_LOOKUP("mem_clk",     gmem_gfx3d_clk.c, "fdc00000.qcom,kgsl-3d0"),

	CLK_LOOKUP("iface_clk",           vfe_ahb_clk.c, "fd890000.qcom,iommu"),
	CLK_LOOKUP("core_clk",            vfe_axi_clk.c, "fd890000.qcom,iommu"),
	CLK_LOOKUP("iface_clk",           mdp_ahb_clk.c, "fd860000.qcom,iommu"),
	CLK_LOOKUP("core_clk",            mdp_axi_clk.c, "fd860000.qcom,iommu"),
	CLK_LOOKUP("iface_clk",           mdp_ahb_clk.c, "fd870000.qcom,iommu"),
	CLK_LOOKUP("core_clk",            mdp_axi_clk.c, "fd870000.qcom,iommu"),
	CLK_LOOKUP("iface_clk",         oxili_ahb_clk.c, "fd880000.qcom,iommu"),
	CLK_LOOKUP("core_clk",           bimc_gfx_clk.c, "fd880000.qcom,iommu"),
	CLK_LOOKUP("iface_clk", gcc_lpss_smmu_ahb_clk.c, "fd000000.qcom,iommu"),
	CLK_LOOKUP("core_clk",   gcc_lpass_q6_axi_clk.c, "fd000000.qcom,iommu"),
	CLK_LOOKUP("iface_clk", gcc_copss_smmu_ahb_clk.c,
							 "fd010000.qcom,iommu"),
	CLK_LOOKUP("core_clk",         pnoc_iommu_clk.c, "fd010000.qcom,iommu"),

	CLK_LOOKUP("core_clk_src",                 lpaif_pri_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                lpaif_quad_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                 lpaif_sec_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                lpaif_spkr_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                 lpaif_ter_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                lpaif_pcm0_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",                lpaif_pcm1_clk_src.c, ""),
	CLK_LOOKUP("core_clk_src",               lpaif_pcmoe_clk_src.c, ""),
	CLK_LOOKUP("core_clk",               audio_core_ixfabric_clk.c, ""),
	CLK_LOOKUP("core_clk",                  audio_wrapper_br_clk.c, ""),
	CLK_LOOKUP("core_clk",                   q6ss_ahb_lfabif_clk.c, ""),
	CLK_LOOKUP("core_clk",                         q6ss_ahbm_clk.c, ""),
	CLK_LOOKUP("core_clk",                           q6ss_xo_clk.c, ""),
	CLK_LOOKUP("core_clk",      audio_core_lpaif_pcm_data_oe_clk.c, ""),
	CLK_LOOKUP("core_clk",         audio_core_lpaif_pri_ebit_clk.c, ""),
	CLK_LOOKUP("core_clk",         audio_core_lpaif_pri_ibit_clk.c, ""),
	CLK_LOOKUP("core_clk",          audio_core_lpaif_pri_osr_clk.c, ""),
	CLK_LOOKUP("core_clk",        audio_core_lpaif_pcm0_ebit_clk.c, ""),
	CLK_LOOKUP("core_clk",        audio_core_lpaif_pcm0_ibit_clk.c, ""),
	CLK_LOOKUP("core_clk",        audio_core_lpaif_quad_ebit_clk.c, ""),
	CLK_LOOKUP("core_clk",        audio_core_lpaif_quad_ibit_clk.c, ""),
	CLK_LOOKUP("core_clk",         audio_core_lpaif_quad_osr_clk.c, ""),
	CLK_LOOKUP("core_clk",         audio_core_lpaif_sec_ebit_clk.c, ""),
	CLK_LOOKUP("core_clk",         audio_core_lpaif_sec_ibit_clk.c, ""),
	CLK_LOOKUP("core_clk",          audio_core_lpaif_sec_osr_clk.c, ""),
	CLK_LOOKUP("core_clk",        audio_core_lpaif_pcm1_ebit_clk.c, ""),
	CLK_LOOKUP("core_clk",        audio_core_lpaif_pcm1_ibit_clk.c, ""),
	CLK_LOOKUP("core_clk",  audio_core_lpaif_codec_spkr_ebit_clk.c, ""),
	CLK_LOOKUP("core_clk",  audio_core_lpaif_codec_spkr_ibit_clk.c, ""),
	CLK_LOOKUP("core_clk",   audio_core_lpaif_codec_spkr_osr_clk.c, ""),
	CLK_LOOKUP("core_clk",         audio_core_lpaif_ter_ebit_clk.c, ""),
	CLK_LOOKUP("core_clk",         audio_core_lpaif_ter_ibit_clk.c, ""),
	CLK_LOOKUP("core_clk",          audio_core_lpaif_ter_osr_clk.c, ""),

	CLK_LOOKUP("core_clk",         q6ss_xo_clk.c,  "fe200000.qcom,lpass"),
	CLK_LOOKUP("bus_clk", gcc_lpass_q6_axi_clk.c,  "fe200000.qcom,lpass"),
	CLK_LOOKUP("iface_clk", q6ss_ahb_lfabif_clk.c, "fe200000.qcom,lpass"),
	CLK_LOOKUP("reg_clk",        q6ss_ahbm_clk.c,  "fe200000.qcom,lpass"),
};

static struct clk_lookup msm_clocks_8610_rumi[] = {
	CLK_DUMMY("core_clk",   BLSP1_UART_CLK, "f991f000.serial", OFF),
	CLK_DUMMY("iface_clk",  BLSP1_UART_CLK, "f991f000.serial", OFF),
	CLK_DUMMY("iface_clk",  HSUSB_IFACE_CLK, "f9a55000.usb", OFF),
	CLK_DUMMY("core_clk",	HSUSB_CORE_CLK, "f9a55000.usb", OFF),
	CLK_DUMMY("iface_clk",	NULL,		"msm_sdcc.1", OFF),
	CLK_DUMMY("core_clk",	NULL,		"msm_sdcc.1", OFF),
	CLK_DUMMY("bus_clk",	NULL,		"msm_sdcc.1", OFF),
	CLK_DUMMY("iface_clk",	NULL,		"msm_sdcc.2", OFF),
	CLK_DUMMY("core_clk",	NULL,		"msm_sdcc.2", OFF),
	CLK_DUMMY("bus_clk",	NULL,		"msm_sdcc.2", OFF),
	CLK_DUMMY("dfab_clk",	DFAB_CLK,	"msm_sps", OFF),
	CLK_DUMMY("iface_clk",  NULL, "fd890000.qcom,iommu", OFF),
	CLK_DUMMY("core_clk",   NULL, "fd890000.qcom,iommu", OFF),
	CLK_DUMMY("iface_clk",  NULL, "fd860000.qcom,iommu", OFF),
	CLK_DUMMY("core_clk",   NULL, "fd860000.qcom,iommu", OFF),
	CLK_DUMMY("iface_clk",  NULL, "fd870000.qcom,iommu", OFF),
	CLK_DUMMY("core_clk",   NULL, "fd870000.qcom,iommu", OFF),
	CLK_DUMMY("iface_clk",  NULL, "fd880000.qcom,iommu", OFF),
	CLK_DUMMY("core_clk",   NULL, "fd880000.qcom,iommu", OFF),
	CLK_DUMMY("iface_clk",  NULL, "fd000000.qcom,iommu", OFF),
	CLK_DUMMY("core_clk",   NULL, "fd000000.qcom,iommu", OFF),
	CLK_DUMMY("iface_clk",  NULL, "fd010000.qcom,iommu", OFF),
	CLK_DUMMY("core_clk",   NULL, "fd010000.qcom,iommu", OFF),
};

struct clock_init_data msm8610_rumi_clock_init_data __initdata = {
	.table = msm_clocks_8610_rumi,
	.size = ARRAY_SIZE(msm_clocks_8610_rumi),
};

static struct pll_config_regs gpll0_regs __initdata = {
	.l_reg = (void __iomem *)GPLL0_L_VAL,
	.m_reg = (void __iomem *)GPLL0_M_VAL,
	.n_reg = (void __iomem *)GPLL0_N_VAL,
	.config_reg = (void __iomem *)GPLL0_USER_CTL,
	.mode_reg = (void __iomem *)GPLL0_MODE,
	.base = &virt_bases[GCC_BASE],
};

/* GPLL0 at 600 MHz, main output enabled. */
static struct pll_config gpll0_config __initdata = {
	.l = 0x1f,
	.m = 0x1,
	.n = 0x4,
	.vco_val = 0x0,
	.vco_mask = BM(21, 20),
	.pre_div_val = 0x0,
	.pre_div_mask = BM(14, 12),
	.post_div_val = 0x0,
	.post_div_mask = BM(9, 8),
	.mn_ena_val = BIT(24),
	.mn_ena_mask = BIT(24),
	.main_output_val = BIT(0),
	.main_output_mask = BIT(0),
};

/* MMPLL0 at 800 MHz, main output enabled. */
static struct pll_config mmpll0_config __initdata = {
	.l = 0x29,
	.m = 0x2,
	.n = 0x3,
	.vco_val = 0x0,
	.vco_mask = BM(21, 20),
	.pre_div_val = 0x0,
	.pre_div_mask = BM(14, 12),
	.post_div_val = 0x0,
	.post_div_mask = BM(9, 8),
	.mn_ena_val = BIT(24),
	.mn_ena_mask = BIT(24),
	.main_output_val = BIT(0),
	.main_output_mask = BIT(0),
};

/* MMPLL1 at 1200 MHz, main output enabled. */
static struct pll_config mmpll1_config __initdata = {
	.l = 0x3E,
	.m = 0x1,
	.n = 0x2,
	.vco_val = 0x0,
	.vco_mask = BM(21, 20),
	.pre_div_val = 0x0,
	.pre_div_mask = BM(14, 12),
	.post_div_val = 0x0,
	.post_div_mask = BM(9, 8),
	.mn_ena_val = BIT(24),
	.mn_ena_mask = BIT(24),
	.main_output_val = BIT(0),
	.main_output_mask = BIT(0),
};

static struct pll_config_regs lpapll0_regs __initdata = {
	.l_reg = (void __iomem *)LPAAUDIO_PLL_L_VAL,
	.m_reg = (void __iomem *)LPAAUDIO_PLL_M_VAL,
	.n_reg = (void __iomem *)LPAAUDIO_PLL_N_VAL,
	.config_reg = (void __iomem *)LPAAUDIO_PLL_USER_CTL,
	.mode_reg = (void __iomem *)LPAAUDIO_PLL_MODE,
	.base = &virt_bases[LPASS_BASE],
};

/* LPAPLL0 at 491.52 MHz, main output enabled. */
static struct pll_config lpapll0_config __initdata = {
	.l = 0x33,
	.m = 0x1,
	.n = 0x5,
	.vco_val = 0x0,
	.vco_mask = BM(21, 20),
	.pre_div_val = BVAL(14, 12, 0x1),
	.pre_div_mask = BM(14, 12),
	.post_div_val = 0x0,
	.post_div_mask = BM(9, 8),
	.mn_ena_val = BIT(24),
	.mn_ena_mask = BIT(24),
	.main_output_val = BIT(0),
	.main_output_mask = BIT(0),
};

#define PLL_AUX_OUTPUT_BIT 1
#define PLL_AUX2_OUTPUT_BIT 2

#define PWR_ON_MASK		BIT(31)
#define EN_REST_WAIT_MASK	(0xF << 20)
#define EN_FEW_WAIT_MASK	(0xF << 16)
#define CLK_DIS_WAIT_MASK	(0xF << 12)
#define SW_OVERRIDE_MASK	BIT(2)
#define HW_CONTROL_MASK		BIT(1)
#define SW_COLLAPSE_MASK	BIT(0)

/* Wait 2^n CXO cycles between all states. Here, n=2 (4 cycles). */
#define EN_REST_WAIT_VAL	(0x2 << 20)
#define EN_FEW_WAIT_VAL		(0x2 << 16)
#define CLK_DIS_WAIT_VAL	(0x2 << 12)
#define GDSC_TIMEOUT_US		50000

static void __init reg_init(void)
{
	u32 regval, status;
	int ret;

	if (!(readl_relaxed(GCC_REG_BASE(GPLL0_STATUS))
			& gpll0_clk_src.status_mask))
		configure_sr_hpm_lp_pll(&gpll0_config, &gpll0_regs, 1);

	configure_sr_hpm_lp_pll(&mmpll0_config, &mmpll0_regs, 1);
	configure_sr_hpm_lp_pll(&mmpll1_config, &mmpll1_regs, 1);
	configure_sr_hpm_lp_pll(&lpapll0_config, &lpapll0_regs, 1);

	/* Enable GPLL0's aux outputs. */
	regval = readl_relaxed(GCC_REG_BASE(GPLL0_USER_CTL));
	regval |= BIT(PLL_AUX_OUTPUT_BIT) | BIT(PLL_AUX2_OUTPUT_BIT);
	writel_relaxed(regval, GCC_REG_BASE(GPLL0_USER_CTL));

	/* Vote for GPLL0 to turn on. Needed by acpuclock. */
	regval = readl_relaxed(GCC_REG_BASE(APCS_GPLL_ENA_VOTE));
	regval |= BIT(0);
	writel_relaxed(regval, GCC_REG_BASE(APCS_GPLL_ENA_VOTE));

	/*
	 * TODO: Confirm that no clocks need to be voted on in this sleep vote
	 * register.
	 */
	writel_relaxed(0x0, GCC_REG_BASE(APCS_CLOCK_SLEEP_ENA_VOTE));

	/*
	 * TODO: The following sequence enables the LPASS audio core GDSC.
	 * Remove when this becomes unnecessary.
	 */

	/*
	 * Disable HW trigger: collapse/restore occur based on registers writes.
	 * Disable SW override: Use hardware state-machine for sequencing.
	 */
	regval = readl_relaxed(LPASS_REG_BASE(AUDIO_CORE_GDSCR));
	regval &= ~(HW_CONTROL_MASK | SW_OVERRIDE_MASK);

	/* Configure wait time between states. */
	regval &= ~(EN_REST_WAIT_MASK | EN_FEW_WAIT_MASK | CLK_DIS_WAIT_MASK);
	regval |= EN_REST_WAIT_VAL | EN_FEW_WAIT_VAL | CLK_DIS_WAIT_VAL;
	writel_relaxed(regval, LPASS_REG_BASE(AUDIO_CORE_GDSCR));

	regval = readl_relaxed(LPASS_REG_BASE(AUDIO_CORE_GDSCR));
	regval &= ~BIT(0);
	writel_relaxed(regval, LPASS_REG_BASE(AUDIO_CORE_GDSCR));

	ret = readl_poll_timeout(LPASS_REG_BASE(AUDIO_CORE_GDSCR), status,
				status & PWR_ON_MASK, 50, GDSC_TIMEOUT_US);
	WARN(ret, "LPASS Audio Core GDSC did not power on.\n");
}

static void __init msm8610_clock_post_init(void)
{
	/*
	 * Hold an active set vote for CXO; this is because CXO is expected
	 * to remain on whenever CPUs aren't power collapsed.
	 */
	clk_prepare_enable(&gcc_xo_a_clk_src.c);


	/* Set rates for single-rate clocks. */
	clk_set_rate(&usb_hs_system_clk_src.c,
			usb_hs_system_clk_src.freq_tbl[0].freq_hz);
	clk_set_rate(&pdm2_clk_src.c, pdm2_clk_src.freq_tbl[0].freq_hz);
	clk_set_rate(&mclk0_clk_src.c, mclk0_clk_src.freq_tbl[0].freq_hz);
	clk_set_rate(&mclk1_clk_src.c, mclk1_clk_src.freq_tbl[0].freq_hz);
	clk_set_rate(&audio_core_slimbus_core_clk_src.c,
			audio_core_slimbus_core_clk_src.freq_tbl[0].freq_hz);
}

#define GCC_CC_PHYS		0xFC400000
#define GCC_CC_SIZE		SZ_16K

#define MMSS_CC_PHYS		0xFD8C0000
#define MMSS_CC_SIZE		SZ_256K

#define LPASS_CC_PHYS		0xFE000000
#define LPASS_CC_SIZE		SZ_256K

#define APCS_GCC_CC_PHYS	0xF9011000
#define APCS_GCC_CC_SIZE	SZ_4K

static void __init msm8610_clock_pre_init(void)
{
	virt_bases[GCC_BASE] = ioremap(GCC_CC_PHYS, GCC_CC_SIZE);
	if (!virt_bases[GCC_BASE])
		panic("clock-8610: Unable to ioremap GCC memory!");

	virt_bases[MMSS_BASE] = ioremap(MMSS_CC_PHYS, MMSS_CC_SIZE);
	if (!virt_bases[MMSS_BASE])
		panic("clock-8610: Unable to ioremap MMSS_CC memory!");

	virt_bases[LPASS_BASE] = ioremap(LPASS_CC_PHYS, LPASS_CC_SIZE);
	if (!virt_bases[LPASS_BASE])
		panic("clock-8610: Unable to ioremap LPASS_CC memory!");

	virt_bases[APCS_BASE] = ioremap(APCS_GCC_CC_PHYS, APCS_GCC_CC_SIZE);
	if (!virt_bases[APCS_BASE])
		panic("clock-8610: Unable to ioremap APCS_GCC_CC memory!");

	clk_ops_local_pll.enable = sr_hpm_lp_pll_clk_enable;

	vdd_dig_reg = rpm_regulator_get(NULL, "vdd_dig");
	if (IS_ERR(vdd_dig_reg))
		panic("clock-8610: Unable to get the vdd_dig regulator!");

	/*
	 * TODO: Set a voltage and enable vdd_dig, leaving the voltage high
	 * until late_init. This may not be necessary with clock handoff;
	 * Investigate this code on a real non-simulator target to determine
	 * its necessity.
	 */
	vote_vdd_level(&vdd_dig, VDD_DIG_HIGH);
	rpm_regulator_enable(vdd_dig_reg);

	enable_rpm_scaling();

	/* Enable a clock to allow access to MMSS clock registers */
	clk_prepare_enable(&gcc_mmss_noc_cfg_ahb_clk.c),

	reg_init();

	/* TODO: Remove this once the bus driver is in place */
	clk_set_rate(&ahb_clk_src.c,  40000000);
	clk_set_rate(&axi_clk_src.c, 200000000);
	clk_prepare_enable(&mmss_mmssnoc_ahb_clk.c);
	clk_prepare_enable(&mmss_s0_axi_clk.c);

	/* TODO: Temporarily enable a clock to allow access to LPASS core
	 * registers.
	 */
	clk_prepare_enable(&audio_core_ixfabric_clk.c);
}

static int __init msm8610_clock_late_init(void)
{
	return unvote_vdd_level(&vdd_dig, VDD_DIG_HIGH);
}

struct clock_init_data msm8610_clock_init_data __initdata = {
	.table = msm_clocks_8610,
	.size = ARRAY_SIZE(msm_clocks_8610),
	.pre_init = msm8610_clock_pre_init,
	.post_init = msm8610_clock_post_init,
	.late_init = msm8610_clock_late_init,
};
