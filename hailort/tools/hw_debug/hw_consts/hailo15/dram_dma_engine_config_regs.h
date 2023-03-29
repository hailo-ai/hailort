/*-------------------------------------------------------------------------------------
//	Copyright (c) 2022 by Hailotech This model is the confidential and
//	proprietary property of Hailotech and the possession or use of this
//	file requires a written license from Hailotech.
-------------------------------------------------------------------------------------*/



#include <stdint.h>

#ifndef DRAM_DMA_ENGINE_CONFIG_REGS_H
#define DRAM_DMA_ENGINE_CONFIG_REGS_H

#include "dram_dma_package_macros.h"
#include "dram_dma_engine_config_macros.h"

typedef struct DRAM_DMA_ENGINE_CONFIG_regs_s  {
	volatile uint32_t QddcEnable[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                      /* offset: 0x0 ; repeat: [16]       */
	volatile uint32_t QddcReset[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                       /* offset: 0x40 ; repeat: [16]       */
	volatile uint32_t QddcMode[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                        /* offset: 0x80 ; repeat: [16]       */
	volatile uint32_t QddcAddBurstVal[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                 /* offset: 0xc0 ; repeat: [16]       */
	volatile uint32_t QddcMaxDesc[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                     /* offset: 0x100 ; repeat: [16]       */
	volatile uint32_t QddcShmifoId[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                    /* offset: 0x140 ; repeat: [16]       */
	volatile uint32_t QddcShmifoCreditSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                            /* offset: 0x180 ; repeat: [16]       */
	volatile uint32_t QddcShmifoInitCredit[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                            /* offset: 0x1c0 ; repeat: [16]       */
	volatile uint32_t QsdcEnable[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                      /* offset: 0x200 ; repeat: [16]       */
	volatile uint32_t QsdcReset[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                       /* offset: 0x240 ; repeat: [16]       */
	volatile uint32_t QsdcMaxDesc[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                     /* offset: 0x280 ; repeat: [16]       */
	volatile uint32_t QsdcShmifoId[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                    /* offset: 0x2c0 ; repeat: [16]       */
	volatile uint32_t QsdcShmifoCreditSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                            /* offset: 0x300 ; repeat: [16]       */
	volatile uint32_t QsdcFullNumPatterns[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_FULL_PATTERN];                                       /* offset: 0x340 ; repeat: [4]        */
	volatile uint32_t QsdcFullPatternNumLines[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_FULL_PATTERN][DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_MAX_PATTERNS];/* offset: 0x350 ; repeat: [4, 4]     */
	volatile uint32_t QsdcFullPatternNumPages[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_FULL_PATTERN][DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_MAX_PATTERNS];/* offset: 0x390 ; repeat: [4, 4]     */
	volatile uint32_t QsdcFullPatternPageSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_FULL_PATTERN][DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_MAX_PATTERNS];/* offset: 0x3d0 ; repeat: [4, 4]     */
	volatile uint32_t QsdcFullPatternResiduePageSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_FULL_PATTERN][DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_MAX_PATTERNS];/* offset: 0x410 ; repeat: [4, 4]     */
	volatile uint32_t QsdcSimpPatternNumPages[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_SIMP_PATTERN];                                   /* offset: 0x450 ; repeat: [12]       */
	volatile uint32_t QsdcSimpPatternPageSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_SIMP_PATTERN];                                   /* offset: 0x480 ; repeat: [12]       */
	volatile uint32_t QsdcSimpPatternResiduePageSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_SIMP_PATTERN];                            /* offset: 0x4b0 ; repeat: [12]       */
	volatile uint32_t QdmcEnable[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                      /* offset: 0x4e0 ; repeat: [16]       */
	volatile uint32_t QdmcReset[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                       /* offset: 0x520 ; repeat: [16]       */
	volatile uint32_t QdmcMemBaseAddr[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                 /* offset: 0x560 ; repeat: [16]       */
	volatile uint32_t QdmcMemCcbSizeLog2[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_REGULAR_CH];                                          /* offset: 0x5a0 ; repeat: [12]       */
	volatile uint32_t QdmcDescCsInterrupt[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                             /* offset: 0x5d0 ; repeat: [16]       */
	volatile uint32_t QdmcBankInterleaveMode[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                          /* offset: 0x610 ; repeat: [16]       */
	volatile uint32_t QdmcMode[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_ENHANCED_CH];                                                   /* offset: 0x650 ; repeat: [4]        */
	volatile uint32_t QdmcAddBurstVal[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_ENHANCED_CH];                                            /* offset: 0x660 ; repeat: [4]        */
	volatile uint32_t QdmcMemCcbSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_ENHANCED_CH];                                             /* offset: 0x670 ; repeat: [4]        */
	volatile uint32_t QdmcDescPeriphInterrupt[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_ENHANCED_CH];                                    /* offset: 0x680 ; repeat: [4]        */
	volatile uint32_t QdmcCcbProcessedIndex[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_ENHANCED_CH];                                      /* offset: 0x690 ; repeat: [4]        */
	volatile uint32_t QsmcEnable[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                      /* offset: 0x6a0 ; repeat: [16]       */
	volatile uint32_t QsmcReset[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                       /* offset: 0x6e0 ; repeat: [16]       */
	volatile uint32_t QsmcMode[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                        /* offset: 0x720 ; repeat: [16]       */
	volatile uint32_t QsmcC2cSel[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                      /* offset: 0x760 ; repeat: [16]       */
	volatile uint32_t QsmcAddBurstVal[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                 /* offset: 0x7a0 ; repeat: [16]       */
	volatile uint32_t QsmcMemBaseAddr[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                 /* offset: 0x7e0 ; repeat: [16]       */
	volatile uint32_t QsmcMemCcbSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                  /* offset: 0x820 ; repeat: [16]       */
	volatile uint32_t QsmcPageSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                                    /* offset: 0x860 ; repeat: [16]       */
	volatile uint32_t QsmcSimpPatternNumPages[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                         /* offset: 0x8a0 ; repeat: [16]       */
	volatile uint32_t QsmcSimpPatternResiduePageSize[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                  /* offset: 0x8e0 ; repeat: [16]       */
	volatile uint32_t QsmcBankInterleaveMode[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_DIR_CH];                                          /* offset: 0x920 ; repeat: [16]       */
	volatile uint32_t QsmcDescPeriphInterrupt[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_ENHANCED_CH];                                    /* offset: 0x960 ; repeat: [4]        */
	volatile uint32_t QsmcCcbFreeIndex[DRAM_DMA_PACKAGE__DRAM_DMA_ENGINE__N_ENHANCED_CH];                                           /* offset: 0x970 ; repeat: [4]        */
	volatile uint32_t engine_cs_intr_mask;                                                                                          /* offset: 0x980 ; repeat: [1]        */
	volatile uint32_t engine_cs_intr_status;                                                                                        /* offset: 0x984 ; repeat: [1]        */
	volatile uint32_t engine_cs_intr_w1c;                                                                                           /* offset: 0x988 ; repeat: [1]        */
	volatile uint32_t engine_cs_intr_w1s;                                                                                           /* offset: 0x98c ; repeat: [1]        */
	volatile uint32_t engine_ap_intr_mask;                                                                                          /* offset: 0x990 ; repeat: [1]        */
	volatile uint32_t engine_ap_intr_status;                                                                                        /* offset: 0x994 ; repeat: [1]        */
	volatile uint32_t engine_ap_intr_w1c;                                                                                           /* offset: 0x998 ; repeat: [1]        */
	volatile uint32_t engine_ap_intr_w1s;                                                                                           /* offset: 0x99c ; repeat: [1]        */
	volatile uint32_t engine_dsp_intr_mask;                                                                                         /* offset: 0x9a0 ; repeat: [1]        */
	volatile uint32_t engine_dsp_intr_status;                                                                                       /* offset: 0x9a4 ; repeat: [1]        */
	volatile uint32_t engine_dsp_intr_w1c;                                                                                          /* offset: 0x9a8 ; repeat: [1]        */
	volatile uint32_t engine_dsp_intr_w1s;                                                                                          /* offset: 0x9ac ; repeat: [1]        */
	volatile uint32_t engine_err_intr_mask;                                                                                         /* offset: 0x9b0 ; repeat: [1]        */
	volatile uint32_t engine_err_intr_status;                                                                                       /* offset: 0x9b4 ; repeat: [1]        */
	volatile uint32_t desc_err_intr_mask;                                                                                           /* offset: 0x9b8 ; repeat: [1]        */
	volatile uint32_t desc_err_intr_status;                                                                                         /* offset: 0x9bc ; repeat: [1]        */
	volatile uint32_t desc_err_intr_w1c;                                                                                            /* offset: 0x9c0 ; repeat: [1]        */
	volatile uint32_t desc_err_intr_w1s;                                                                                            /* offset: 0x9c4 ; repeat: [1]        */
	volatile uint32_t qddc_crd_ovf_err_intr_mask;                                                                                   /* offset: 0x9c8 ; repeat: [1]        */
	volatile uint32_t qddc_crd_ovf_err_intr_status;                                                                                 /* offset: 0x9cc ; repeat: [1]        */
	volatile uint32_t qddc_crd_ovf_err_intr_w1c;                                                                                    /* offset: 0x9d0 ; repeat: [1]        */
	volatile uint32_t qddc_crd_ovf_err_intr_w1s;                                                                                    /* offset: 0x9d4 ; repeat: [1]        */
	volatile uint32_t qsdc_crd_ovf_err_intr_mask;                                                                                   /* offset: 0x9d8 ; repeat: [1]        */
	volatile uint32_t qsdc_crd_ovf_err_intr_status;                                                                                 /* offset: 0x9dc ; repeat: [1]        */
	volatile uint32_t qsdc_crd_ovf_err_intr_w1c;                                                                                    /* offset: 0x9e0 ; repeat: [1]        */
	volatile uint32_t qsdc_crd_ovf_err_intr_w1s;                                                                                    /* offset: 0x9e4 ; repeat: [1]        */
	volatile uint32_t EngErrInterruptSource;                                                                                        /* offset: 0x9e8 ; repeat: [1]        */
	volatile uint32_t EngErrRemainPageSize;                                                                                         /* offset: 0x9ec ; repeat: [1]        */
	volatile uint32_t EngTransferPageSize;                                                                                          /* offset: 0x9f0 ; repeat: [1]        */
	volatile uint32_t VdmaSoftReset;                                                                                                /* offset: 0x9f4 ; repeat: [1]        */
	volatile uint32_t vdma_sharedbus;                                                                                               /* offset: 0x9f8 ; repeat: [1]        */
	volatile uint32_t cfg_qddc_redundant_en;                                                                                        /* offset: 0x9fc ; repeat: [1]        */
	volatile uint32_t cfg_qsdc_redundant_en;                                                                                        /* offset: 0xa00 ; repeat: [1]        */
	volatile uint32_t cfg_qdmc_redundant_en;                                                                                        /* offset: 0xa04 ; repeat: [1]        */
	volatile uint32_t cfg_qsmc_redundant_en;                                                                                        /* offset: 0xa08 ; repeat: [1]        */
	volatile uint32_t qddc_redundant_asf_int_mask;                                                                                  /* offset: 0xa0c ; repeat: [1]        */
	volatile uint32_t qddc_redundant_asf_int_status;                                                                                /* offset: 0xa10 ; repeat: [1]        */
	volatile uint32_t qddc_redundant_asf_int_w1c;                                                                                   /* offset: 0xa14 ; repeat: [1]        */
	volatile uint32_t qddc_redundant_asf_int_w1s;                                                                                   /* offset: 0xa18 ; repeat: [1]        */
	volatile uint32_t qsdc_redundant_asf_int_mask;                                                                                  /* offset: 0xa1c ; repeat: [1]        */
	volatile uint32_t qsdc_redundant_asf_int_status;                                                                                /* offset: 0xa20 ; repeat: [1]        */
	volatile uint32_t qsdc_redundant_asf_int_w1c;                                                                                   /* offset: 0xa24 ; repeat: [1]        */
	volatile uint32_t qsdc_redundant_asf_int_w1s;                                                                                   /* offset: 0xa28 ; repeat: [1]        */
	volatile uint32_t qdmc_redundant_asf_int_mask;                                                                                  /* offset: 0xa2c ; repeat: [1]        */
	volatile uint32_t qdmc_redundant_asf_int_status;                                                                                /* offset: 0xa30 ; repeat: [1]        */
	volatile uint32_t qdmc_redundant_asf_int_w1c;                                                                                   /* offset: 0xa34 ; repeat: [1]        */
	volatile uint32_t qdmc_redundant_asf_int_w1s;                                                                                   /* offset: 0xa38 ; repeat: [1]        */
	volatile uint32_t qsmc_redundant_asf_int_mask;                                                                                  /* offset: 0xa3c ; repeat: [1]        */
	volatile uint32_t qsmc_redundant_asf_int_status;                                                                                /* offset: 0xa40 ; repeat: [1]        */
	volatile uint32_t qsmc_redundant_asf_int_w1c;                                                                                   /* offset: 0xa44 ; repeat: [1]        */
	volatile uint32_t qsmc_redundant_asf_int_w1s;                                                                                   /* offset: 0xa48 ; repeat: [1]        */
	volatile uint32_t PrioIsLp;                                                                                                     /* offset: 0xa4c ; repeat: [1]        */
	volatile uint32_t ReadLpToQosValue;                                                                                             /* offset: 0xa50 ; repeat: [1]        */
	volatile uint32_t ReadHpToQosValue;                                                                                             /* offset: 0xa54 ; repeat: [1]        */
	volatile uint32_t WriteLpToQosValue;                                                                                            /* offset: 0xa58 ; repeat: [1]        */
	volatile uint32_t WriteHpToQosValue;                                                                                            /* offset: 0xa5c ; repeat: [1]        */
	volatile uint32_t DescReadQosValue;                                                                                             /* offset: 0xa60 ; repeat: [1]        */
	volatile uint32_t DescWriteQosValue;                                                                                            /* offset: 0xa64 ; repeat: [1]        */
	volatile uint32_t vdma_arb;                                                                                                     /* offset: 0xa68 ; repeat: [1]        */
	volatile uint32_t qm_cfg_cg_delay;                                                                                              /* offset: 0xa6c ; repeat: [1]        */
	volatile uint32_t qddc_cfg_cg_bypass;                                                                                           /* offset: 0xa70 ; repeat: [1]        */
	volatile uint32_t qsdc_cfg_cg_bypass;                                                                                           /* offset: 0xa74 ; repeat: [1]        */
	volatile uint32_t qdmc_cfg_cg_bypass;                                                                                           /* offset: 0xa78 ; repeat: [1]        */
	volatile uint32_t qsmc_cfg_cg_bypass;                                                                                           /* offset: 0xa7c ; repeat: [1]        */
	volatile uint32_t engine_asf_int_mask;                                                                                          /* offset: 0xa80 ; repeat: [1]        */
	volatile uint32_t engine_asf_int_status;                                                                                        /* offset: 0xa84 ; repeat: [1]        */
	volatile uint32_t engine_asf_int_w1c;                                                                                           /* offset: 0xa88 ; repeat: [1]        */
	volatile uint32_t engine_asf_int_w1s;                                                                                           /* offset: 0xa8c ; repeat: [1]        */
	volatile uint32_t engine_rw_parity_bist_mode;                                                                                   /* offset: 0xa90 ; repeat: [1]        */
	volatile uint32_t vdma_stop_lp;                                                                                                 /* offset: 0xa94 ; repeat: [1]        */
	volatile uint32_t vdma_sch;                                                                                                     /* offset: 0xa98 ; repeat: [1]        */
	volatile uint32_t cfg_src_desc_trace;                                                                                           /* offset: 0xa9c ; repeat: [1]        */
	volatile uint32_t cfg_src_desc_trace_base_addr;                                                                                 /* offset: 0xaa0 ; repeat: [1]        */
	volatile uint32_t cfg_dst_desc_trace;                                                                                           /* offset: 0xaa4 ; repeat: [1]        */
	volatile uint32_t cfg_dst_desc_trace_base_addr;                                                                                 /* offset: 0xaa8 ; repeat: [1]        */
	volatile uint32_t cfg_debug_timestamp;                                                                                          /* offset: 0xaac ; repeat: [1]        */
	volatile uint32_t debug_timestamp;                                                                                              /* offset: 0xab0 ; repeat: [1]        */
	volatile uint32_t auto_address_err_cb_indication;                                                                               /* offset: 0xab4 ; repeat: [1]        */
} DRAM_DMA_ENGINE_CONFIG_t;

#endif /* DRAM_DMA_ENGINE_CONFIG_REGS_H */
