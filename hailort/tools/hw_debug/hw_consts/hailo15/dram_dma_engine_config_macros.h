/*-------------------------------------------------------------------------------------
//	Copyright (c) 2022 by Hailotech This model is the confidential and
//	proprietary property of Hailotech and the possession or use of this
//	file requires a written license from Hailotech.
-------------------------------------------------------------------------------------*/



#include <stdint.h>

#ifndef DRAM_DMA_ENGINE_CONFIG_MACRO_H
#define DRAM_DMA_ENGINE_CONFIG_MACRO_H


/*----------------------------------------------------------------------------------------------------*/
/*  QDDCENABLE : val		*/
/*  Description: Enable per channel,when disabled do not give credits to vDMA */
#define DRAM_DMA_ENGINE_CONFIG__QDDCENABLE__VAL__SHIFT                                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCENABLE__VAL__WIDTH                                             (1)
#define DRAM_DMA_ENGINE_CONFIG__QDDCENABLE__VAL__MASK                                              (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCENABLE__VAL__RESET                                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCENABLE__VAL__READ(reg_offset)                                  \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCENABLE__VAL__MODIFY(reg_offset, value)                         \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QDDCENABLE__VAL__SET(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QDDCENABLE__VAL__CLR(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDCRESET : val		*/
/*  Description: Soft reset per channel,when write 1'b1 should clear all internal credits/counter/status. Should be set when channel is disabled,usually with vDMA channel reset (abort). Write 1'b0 should do nothing. Read always return 1'b0. Implemented as external register type. */
#define DRAM_DMA_ENGINE_CONFIG__QDDCRESET__VAL__SHIFT                                              (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCRESET__VAL__WIDTH                                              (1)
#define DRAM_DMA_ENGINE_CONFIG__QDDCRESET__VAL__MASK                                               (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCRESET__VAL__RESET                                              (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCRESET__VAL__READ(reg_offset)                                   \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCRESET__VAL__MODIFY(reg_offset, value)                          \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QDDCRESET__VAL__SET(reg_offset)                                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QDDCRESET__VAL__CLR(reg_offset)                                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDCMODE : val		*/
/*  Description: 0 - CONT_MODE. 1 - BURST_MODE */
#define DRAM_DMA_ENGINE_CONFIG__QDDCMODE__VAL__SHIFT                                               (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMODE__VAL__WIDTH                                               (1)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMODE__VAL__MASK                                                (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMODE__VAL__RESET                                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMODE__VAL__READ(reg_offset)                                    \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMODE__VAL__MODIFY(reg_offset, value)                           \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QDDCMODE__VAL__SET(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QDDCMODE__VAL__CLR(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDCADDBURSTVAL : val		*/
/*  Description: Writing to this register increment the remain burst counter in QDDC by QddcAddBurstVal x 8 Bytes: RemainBurstCount += QddcAddBurstVal. Reading this register should return the current available credit counter (RemainBurstCount) in 2s complement format - can be negative. Implemented as external register type. */
#define DRAM_DMA_ENGINE_CONFIG__QDDCADDBURSTVAL__VAL__SHIFT                                        (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCADDBURSTVAL__VAL__WIDTH                                        (27)
#define DRAM_DMA_ENGINE_CONFIG__QDDCADDBURSTVAL__VAL__MASK                                         (0x07FFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDCADDBURSTVAL__VAL__RESET                                        (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCADDBURSTVAL__VAL__READ(reg_offset)                             \
			(((uint32_t)(reg_offset) & 0x07FFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCADDBURSTVAL__VAL__MODIFY(reg_offset, value)                    \
			(reg_offset) = (((reg_offset) & ~0x07FFFFFFL) | (((uint32_t)(value) << 0) & 0x07FFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDCADDBURSTVAL__VAL__SET(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x07FFFFFFL) | 0x07FFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDCADDBURSTVAL__VAL__CLR(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x07FFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDCMAXDESC : val		*/
/*  Description: Maximum in flight descriptors,this is a TH for number of descriptors the QM might give the vDMA. 3'd0 - 1 descriptor (debug mode). 3'd1 - N_QM_DESC*1/8 (2). 3'd2 - N_QM_DESC*2/8 (4). 3'd3 - N_QM_DESC*3/8 (6). 3'd4 - N_QM_DESC*2/4 (8). 3'd5 - N_QM_DESC*5/8 (10). 3'd6 - N_QM_DESC*6/8 (12). 3'd7 - N_QM_DESC-1 (15-maximum),default. */
#define DRAM_DMA_ENGINE_CONFIG__QDDCMAXDESC__VAL__SHIFT                                            (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMAXDESC__VAL__WIDTH                                            (3)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMAXDESC__VAL__MASK                                             (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMAXDESC__VAL__RESET                                            (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMAXDESC__VAL__READ(reg_offset)                                 \
			(((uint32_t)(reg_offset) & 0x00000007L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMAXDESC__VAL__MODIFY(reg_offset, value)                        \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | (((uint32_t)(value) << 0) & 0x00000007L))
#define DRAM_DMA_ENGINE_CONFIG__QDDCMAXDESC__VAL__SET(reg_offset)                                  \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | 0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCMAXDESC__VAL__CLR(reg_offset)                                  \
			(reg_offset) = (((reg_offset) & ~0x00000007L))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDCSHMIFOID : val		*/
/*  Description: The RX-SHMIFO ID. Used to know the SHMIFO base address (from a global parameter/define) and used to select the correct SHMIFO credit signal (nn_core_inbound_buffer_ready_pulse). 0-19: for DSM-RX 0-19. 20-23: for CSM 0-3. 24-30: reserved. 31: NULL ignore any credit from NN Core. */
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOID__VAL__SHIFT                                           (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOID__VAL__WIDTH                                           (5)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOID__VAL__MASK                                            (0x0000001FL)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOID__VAL__RESET                                           (0x0000001FL)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOID__VAL__READ(reg_offset)                                \
			(((uint32_t)(reg_offset) & 0x0000001FL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOID__VAL__MODIFY(reg_offset, value)                       \
			(reg_offset) = (((reg_offset) & ~0x0000001FL) | (((uint32_t)(value) << 0) & 0x0000001FL))
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOID__VAL__SET(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x0000001FL) | 0x0000001FL)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOID__VAL__CLR(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x0000001FL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDCSHMIFOCREDITSIZE : val		*/
/*  Description: The credit size in 8B granularity minus 1. 0 - indicates 8B 1 - indicates 16B ... 10'd1023 - indicates 8kB */
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOCREDITSIZE__VAL__SHIFT                                   (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOCREDITSIZE__VAL__WIDTH                                   (10)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOCREDITSIZE__VAL__MASK                                    (0x000003FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOCREDITSIZE__VAL__RESET                                   (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOCREDITSIZE__VAL__READ(reg_offset)                        \
			(((uint32_t)(reg_offset) & 0x000003FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOCREDITSIZE__VAL__MODIFY(reg_offset, value)               \
			(reg_offset) = (((reg_offset) & ~0x000003FFL) | (((uint32_t)(value) << 0) & 0x000003FFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOCREDITSIZE__VAL__SET(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x000003FFL) | 0x000003FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOCREDITSIZE__VAL__CLR(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x000003FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDCSHMIFOINITCREDIT : val		*/
/*  Description: Writing to this register set the amount of credit from SHMIFO RX (AvailableCredits),used to configure the initial amount of credits,reading this register should return the value of AvailableCredits. Implemented as external register type. */
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOINITCREDIT__VAL__SHIFT                                   (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOINITCREDIT__VAL__WIDTH                                   (13)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOINITCREDIT__VAL__MASK                                    (0x00001FFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOINITCREDIT__VAL__RESET                                   (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOINITCREDIT__VAL__READ(reg_offset)                        \
			(((uint32_t)(reg_offset) & 0x00001FFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOINITCREDIT__VAL__MODIFY(reg_offset, value)               \
			(reg_offset) = (((reg_offset) & ~0x00001FFFL) | (((uint32_t)(value) << 0) & 0x00001FFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOINITCREDIT__VAL__SET(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x00001FFFL) | 0x00001FFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDCSHMIFOINITCREDIT__VAL__CLR(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x00001FFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCENABLE : val		*/
/*  Description: Enable per channel,when disabled do not give credits to vDMA */
#define DRAM_DMA_ENGINE_CONFIG__QSDCENABLE__VAL__SHIFT                                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCENABLE__VAL__WIDTH                                             (1)
#define DRAM_DMA_ENGINE_CONFIG__QSDCENABLE__VAL__MASK                                              (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCENABLE__VAL__RESET                                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCENABLE__VAL__READ(reg_offset)                                  \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCENABLE__VAL__MODIFY(reg_offset, value)                         \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QSDCENABLE__VAL__SET(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QSDCENABLE__VAL__CLR(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCRESET : val		*/
/*  Description: Soft reset per channel,when write 1'b1 should clear all internal credits/counter/status. Should be set when channel is disabled,usually with vDMA channel reset (abort). Write 1'b0 should do nothing. Read always return 1'b0. Implemented as external register type. */
#define DRAM_DMA_ENGINE_CONFIG__QSDCRESET__VAL__SHIFT                                              (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCRESET__VAL__WIDTH                                              (1)
#define DRAM_DMA_ENGINE_CONFIG__QSDCRESET__VAL__MASK                                               (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCRESET__VAL__RESET                                              (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCRESET__VAL__READ(reg_offset)                                   \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCRESET__VAL__MODIFY(reg_offset, value)                          \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QSDCRESET__VAL__SET(reg_offset)                                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QSDCRESET__VAL__CLR(reg_offset)                                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCMAXDESC : val		*/
/*  Description: Maximum in flight descriptors,this is a TH for number of descriptors the QM might give the vDMA. 3'd0 - 1 descriptor (debug mode). 3'd1 - N_QM_DESC*1/8 (2). 3'd2 - N_QM_DESC*2/8 (4). 3'd3 - N_QM_DESC*3/8 (6). 3'd4 - N_QM_DESC*4/8 (8). 3'd5 - N_QM_DESC*5/8 (10). 3'd6 - N_QM_DESC*6/8 (12). 3'd7 - N_QM_DESC-1 (15-maximum),default. */
#define DRAM_DMA_ENGINE_CONFIG__QSDCMAXDESC__VAL__SHIFT                                            (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCMAXDESC__VAL__WIDTH                                            (3)
#define DRAM_DMA_ENGINE_CONFIG__QSDCMAXDESC__VAL__MASK                                             (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCMAXDESC__VAL__RESET                                            (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCMAXDESC__VAL__READ(reg_offset)                                 \
			(((uint32_t)(reg_offset) & 0x00000007L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCMAXDESC__VAL__MODIFY(reg_offset, value)                        \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | (((uint32_t)(value) << 0) & 0x00000007L))
#define DRAM_DMA_ENGINE_CONFIG__QSDCMAXDESC__VAL__SET(reg_offset)                                  \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | 0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCMAXDESC__VAL__CLR(reg_offset)                                  \
			(reg_offset) = (((reg_offset) & ~0x00000007L))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCSHMIFOID : val		*/
/*  Description: The TX-SHMIFO ID. Used to know the SHMIFO base address (from a global parameter/define) and used to select the correct SHMIFO credit signal (nn_core_outbound_buffer_valid_pulse). 0-19: for DSM-TX 0-19. 20-30: reserved. 31: NULL ignore any credit from NN Core. */
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOID__VAL__SHIFT                                           (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOID__VAL__WIDTH                                           (5)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOID__VAL__MASK                                            (0x0000001FL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOID__VAL__RESET                                           (0x0000001FL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOID__VAL__READ(reg_offset)                                \
			(((uint32_t)(reg_offset) & 0x0000001FL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOID__VAL__MODIFY(reg_offset, value)                       \
			(reg_offset) = (((reg_offset) & ~0x0000001FL) | (((uint32_t)(value) << 0) & 0x0000001FL))
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOID__VAL__SET(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x0000001FL) | 0x0000001FL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOID__VAL__CLR(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x0000001FL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCSHMIFOCREDITSIZE : val		*/
/*  Description: The credit size in 8B granularity minus 1. 0 - indicates 8B 1 - indicates 16B ... 10'd1023 - indicates 8kB */
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOCREDITSIZE__VAL__SHIFT                                   (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOCREDITSIZE__VAL__WIDTH                                   (10)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOCREDITSIZE__VAL__MASK                                    (0x000003FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOCREDITSIZE__VAL__RESET                                   (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOCREDITSIZE__VAL__READ(reg_offset)                        \
			(((uint32_t)(reg_offset) & 0x000003FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOCREDITSIZE__VAL__MODIFY(reg_offset, value)               \
			(reg_offset) = (((reg_offset) & ~0x000003FFL) | (((uint32_t)(value) << 0) & 0x000003FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOCREDITSIZE__VAL__SET(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x000003FFL) | 0x000003FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSHMIFOCREDITSIZE__VAL__CLR(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x000003FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCFULLNUMPATTERNS : val		*/
/*  Description: Number of patterns per pattern ID minus one. 0 - one pattern,1 - two patterns,...,3 - four patterns. */
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLNUMPATTERNS__VAL__SHIFT                                    (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLNUMPATTERNS__VAL__WIDTH                                    (2)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLNUMPATTERNS__VAL__MASK                                     (0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLNUMPATTERNS__VAL__RESET                                    (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLNUMPATTERNS__VAL__READ(reg_offset)                         \
			(((uint32_t)(reg_offset) & 0x00000003L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLNUMPATTERNS__VAL__MODIFY(reg_offset, value)                \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | (((uint32_t)(value) << 0) & 0x00000003L))
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLNUMPATTERNS__VAL__SET(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | 0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLNUMPATTERNS__VAL__CLR(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x00000003L))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCFULLPATTERNNUMLINES : val		*/
/*  Description: Number of lines per pattern. */
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMLINES__VAL__SHIFT                                (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMLINES__VAL__WIDTH                                (18)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMLINES__VAL__MASK                                 (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMLINES__VAL__RESET                                (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMLINES__VAL__READ(reg_offset)                     \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMLINES__VAL__MODIFY(reg_offset, value)            \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMLINES__VAL__SET(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMLINES__VAL__CLR(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCFULLPATTERNNUMPAGES : val		*/
/*  Description: Number of pages per line. */
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMPAGES__VAL__SHIFT                                (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMPAGES__VAL__WIDTH                                (18)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMPAGES__VAL__MASK                                 (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMPAGES__VAL__RESET                                (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMPAGES__VAL__READ(reg_offset)                     \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMPAGES__VAL__MODIFY(reg_offset, value)            \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMPAGES__VAL__SET(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNNUMPAGES__VAL__CLR(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCFULLPATTERNPAGESIZE : val		*/
/*  Description: page size in 8B granularity,minus one,per pattern. 0-8B,1-16B,...,511-4kB */
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNPAGESIZE__VAL__SHIFT                                (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNPAGESIZE__VAL__WIDTH                                (9)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNPAGESIZE__VAL__MASK                                 (0x000001FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNPAGESIZE__VAL__RESET                                (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNPAGESIZE__VAL__READ(reg_offset)                     \
			(((uint32_t)(reg_offset) & 0x000001FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNPAGESIZE__VAL__MODIFY(reg_offset, value)            \
			(reg_offset) = (((reg_offset) & ~0x000001FFL) | (((uint32_t)(value) << 0) & 0x000001FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNPAGESIZE__VAL__SET(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x000001FFL) | 0x000001FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNPAGESIZE__VAL__CLR(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x000001FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCFULLPATTERNRESIDUEPAGESIZE : val		*/
/*  Description: Residue page size in 8B granularity,minus one,per pattern. 0-8B,1-16B,...,511-4kB */
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNRESIDUEPAGESIZE__VAL__SHIFT                         (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNRESIDUEPAGESIZE__VAL__WIDTH                         (9)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNRESIDUEPAGESIZE__VAL__MASK                          (0x000001FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNRESIDUEPAGESIZE__VAL__RESET                         (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNRESIDUEPAGESIZE__VAL__READ(reg_offset)              \
			(((uint32_t)(reg_offset) & 0x000001FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNRESIDUEPAGESIZE__VAL__MODIFY(reg_offset, value)     \
			(reg_offset) = (((reg_offset) & ~0x000001FFL) | (((uint32_t)(value) << 0) & 0x000001FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNRESIDUEPAGESIZE__VAL__SET(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x000001FFL) | 0x000001FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCFULLPATTERNRESIDUEPAGESIZE__VAL__CLR(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x000001FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCSIMPPATTERNNUMPAGES : val		*/
/*  Description: Number of pages per line (simplified pattern has single line/pattern). */
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNNUMPAGES__VAL__SHIFT                                (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNNUMPAGES__VAL__WIDTH                                (18)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNNUMPAGES__VAL__MASK                                 (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNNUMPAGES__VAL__RESET                                (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNNUMPAGES__VAL__READ(reg_offset)                     \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNNUMPAGES__VAL__MODIFY(reg_offset, value)            \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNNUMPAGES__VAL__SET(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNNUMPAGES__VAL__CLR(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCSIMPPATTERNPAGESIZE : val		*/
/*  Description: Log2(Page size/512B),valid values are 0 to PAGE_SIZE_MAX-10. 0 - 512B,1 - 1kB,2 - 2kB,3 - 4kB */
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNPAGESIZE__VAL__SHIFT                                (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNPAGESIZE__VAL__WIDTH                                (2)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNPAGESIZE__VAL__MASK                                 (0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNPAGESIZE__VAL__RESET                                (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNPAGESIZE__VAL__READ(reg_offset)                     \
			(((uint32_t)(reg_offset) & 0x00000003L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNPAGESIZE__VAL__MODIFY(reg_offset, value)            \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | (((uint32_t)(value) << 0) & 0x00000003L))
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNPAGESIZE__VAL__SET(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | 0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNPAGESIZE__VAL__CLR(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x00000003L))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDCSIMPPATTERNRESIDUEPAGESIZE : val		*/
/*  Description: Residue page size in 8B granularity,minus one,per pattern. 0-8B,1-16B,...,511-4kB */
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNRESIDUEPAGESIZE__VAL__SHIFT                         (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNRESIDUEPAGESIZE__VAL__WIDTH                         (9)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNRESIDUEPAGESIZE__VAL__MASK                          (0x000001FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNRESIDUEPAGESIZE__VAL__RESET                         (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNRESIDUEPAGESIZE__VAL__READ(reg_offset)              \
			(((uint32_t)(reg_offset) & 0x000001FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNRESIDUEPAGESIZE__VAL__MODIFY(reg_offset, value)     \
			(reg_offset) = (((reg_offset) & ~0x000001FFL) | (((uint32_t)(value) << 0) & 0x000001FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNRESIDUEPAGESIZE__VAL__SET(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x000001FFL) | 0x000001FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDCSIMPPATTERNRESIDUEPAGESIZE__VAL__CLR(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x000001FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCENABLE : val		*/
/*  Description: Enable per channel,when disabled do not give credits to vDMA */
#define DRAM_DMA_ENGINE_CONFIG__QDMCENABLE__VAL__SHIFT                                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCENABLE__VAL__WIDTH                                             (1)
#define DRAM_DMA_ENGINE_CONFIG__QDMCENABLE__VAL__MASK                                              (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCENABLE__VAL__RESET                                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCENABLE__VAL__READ(reg_offset)                                  \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCENABLE__VAL__MODIFY(reg_offset, value)                         \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QDMCENABLE__VAL__SET(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QDMCENABLE__VAL__CLR(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCRESET : val		*/
/*  Description: Soft reset per channel,when write 1'b1 should clear all internal credits/counter/status. Should be set when channel is disabled,usually with vDMA channel reset (abort). Write 1'b0 should do nothing. Read always return 1'b0. Implemented as external register type. */
#define DRAM_DMA_ENGINE_CONFIG__QDMCRESET__VAL__SHIFT                                              (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCRESET__VAL__WIDTH                                              (1)
#define DRAM_DMA_ENGINE_CONFIG__QDMCRESET__VAL__MASK                                               (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCRESET__VAL__RESET                                              (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCRESET__VAL__READ(reg_offset)                                   \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCRESET__VAL__MODIFY(reg_offset, value)                          \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QDMCRESET__VAL__SET(reg_offset)                                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QDMCRESET__VAL__CLR(reg_offset)                                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCMEMBASEADDR : val		*/
/*  Description: Base address to the CCB in the DDR memory space. aligned to minimum page size of 512B. */
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMBASEADDR__VAL__SHIFT                                        (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMBASEADDR__VAL__WIDTH                                        (26)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMBASEADDR__VAL__MASK                                         (0x03FFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMBASEADDR__VAL__RESET                                        (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMBASEADDR__VAL__READ(reg_offset)                             \
			(((uint32_t)(reg_offset) & 0x03FFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMBASEADDR__VAL__MODIFY(reg_offset, value)                    \
			(reg_offset) = (((reg_offset) & ~0x03FFFFFFL) | (((uint32_t)(value) << 0) & 0x03FFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMBASEADDR__VAL__SET(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x03FFFFFFL) | 0x03FFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMBASEADDR__VAL__CLR(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x03FFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCMEMCCBSIZELOG2 : val		*/
/*  Description: The CCB size Log2(memory size/512B): 1 - 1kB (2 pages). 2 - 2kB. valid values are 1 to W_CCB_DESC_INDEX */
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZELOG2__VAL__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZELOG2__VAL__WIDTH                                     (5)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZELOG2__VAL__MASK                                      (0x0000001FL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZELOG2__VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZELOG2__VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x0000001FL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZELOG2__VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x0000001FL) | (((uint32_t)(value) << 0) & 0x0000001FL))
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZELOG2__VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000001FL) | 0x0000001FL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZELOG2__VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000001FL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCDESCCSINTERRUPT : val		*/
/*  Description: When > 0 the QDMC will interrupt the CS manager every written QdmcDescCsInterrupt descriptors. */
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCCSINTERRUPT__VAL__SHIFT                                    (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCCSINTERRUPT__VAL__WIDTH                                    (18)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCCSINTERRUPT__VAL__MASK                                     (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCCSINTERRUPT__VAL__RESET                                    (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCCSINTERRUPT__VAL__READ(reg_offset)                         \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCCSINTERRUPT__VAL__MODIFY(reg_offset, value)                \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCCSINTERRUPT__VAL__SET(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCCSINTERRUPT__VAL__CLR(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCBANKINTERLEAVEMODE : val		*/
/*  Description: Select the bank interleave mode: 2'd0 - interleave 8 banks (default),2'd1 - Interleave 4 banks,2'd2 - Interleave 2 banks,2'd3 - no interleave. */
#define DRAM_DMA_ENGINE_CONFIG__QDMCBANKINTERLEAVEMODE__VAL__SHIFT                                 (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCBANKINTERLEAVEMODE__VAL__WIDTH                                 (2)
#define DRAM_DMA_ENGINE_CONFIG__QDMCBANKINTERLEAVEMODE__VAL__MASK                                  (0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCBANKINTERLEAVEMODE__VAL__RESET                                 (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCBANKINTERLEAVEMODE__VAL__READ(reg_offset)                      \
			(((uint32_t)(reg_offset) & 0x00000003L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCBANKINTERLEAVEMODE__VAL__MODIFY(reg_offset, value)             \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | (((uint32_t)(value) << 0) & 0x00000003L))
#define DRAM_DMA_ENGINE_CONFIG__QDMCBANKINTERLEAVEMODE__VAL__SET(reg_offset)                       \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | 0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCBANKINTERLEAVEMODE__VAL__CLR(reg_offset)                       \
			(reg_offset) = (((reg_offset) & ~0x00000003L))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCMODE : val		*/
/*  Description: 0 - CONT_MODE. 1 - BURST_MODE */
#define DRAM_DMA_ENGINE_CONFIG__QDMCMODE__VAL__SHIFT                                               (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMODE__VAL__WIDTH                                               (1)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMODE__VAL__MASK                                                (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMODE__VAL__RESET                                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMODE__VAL__READ(reg_offset)                                    \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMODE__VAL__MODIFY(reg_offset, value)                           \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QDMCMODE__VAL__SET(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QDMCMODE__VAL__CLR(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCADDBURSTVAL : val		*/
/*  Description: Writing to this register increment the available descriptor counter in QDMC by QdmcAddBurstVal descriptors: AvailableDescsCounter += QdmcAddBurstVal. Reading this register should return the current available descriptors counter (AvailableDescsCounter). Implemented as external register type. */
#define DRAM_DMA_ENGINE_CONFIG__QDMCADDBURSTVAL__VAL__SHIFT                                        (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCADDBURSTVAL__VAL__WIDTH                                        (18)
#define DRAM_DMA_ENGINE_CONFIG__QDMCADDBURSTVAL__VAL__MASK                                         (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCADDBURSTVAL__VAL__RESET                                        (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCADDBURSTVAL__VAL__READ(reg_offset)                             \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCADDBURSTVAL__VAL__MODIFY(reg_offset, value)                    \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDMCADDBURSTVAL__VAL__SET(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCADDBURSTVAL__VAL__CLR(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCMEMCCBSIZE : val		*/
/*  Description: The CCB size Log2(memory size/512B): 1 - 1kB (2 pages). 2 - 2kB. valid values are 1 to W_CCB_DESC_INDEX */
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZE__VAL__SHIFT                                         (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZE__VAL__WIDTH                                         (18)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZE__VAL__MASK                                          (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZE__VAL__RESET                                         (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZE__VAL__READ(reg_offset)                              \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZE__VAL__MODIFY(reg_offset, value)                     \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZE__VAL__SET(reg_offset)                               \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCMEMCCBSIZE__VAL__CLR(reg_offset)                               \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCDESCPERIPHINTERRUPT : val		*/
/*  Description: When > 0 the QDMC will interrupt the peripheral every written QdmcDescPeriphInterrupt descriptors. */
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCPERIPHINTERRUPT__VAL__SHIFT                                (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCPERIPHINTERRUPT__VAL__WIDTH                                (18)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCPERIPHINTERRUPT__VAL__MASK                                 (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCPERIPHINTERRUPT__VAL__RESET                                (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCPERIPHINTERRUPT__VAL__READ(reg_offset)                     \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCPERIPHINTERRUPT__VAL__MODIFY(reg_offset, value)            \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCPERIPHINTERRUPT__VAL__SET(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCDESCPERIPHINTERRUPT__VAL__CLR(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMCCCBPROCESSEDINDEX : val		*/
/*  Description: Used by the peripheral to indicates how many data is ready in the CCB (process). This is the CcbIndex (free pointer in CCB). */
#define DRAM_DMA_ENGINE_CONFIG__QDMCCCBPROCESSEDINDEX__VAL__SHIFT                                  (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMCCCBPROCESSEDINDEX__VAL__WIDTH                                  (18)
#define DRAM_DMA_ENGINE_CONFIG__QDMCCCBPROCESSEDINDEX__VAL__MASK                                   (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMCCCBPROCESSEDINDEX__VAL__RESET                                  (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMCCCBPROCESSEDINDEX__VAL__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCENABLE : val		*/
/*  Description: Enable per channel,when disabled do not give credits to vDMA */
#define DRAM_DMA_ENGINE_CONFIG__QSMCENABLE__VAL__SHIFT                                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCENABLE__VAL__WIDTH                                             (1)
#define DRAM_DMA_ENGINE_CONFIG__QSMCENABLE__VAL__MASK                                              (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCENABLE__VAL__RESET                                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCENABLE__VAL__READ(reg_offset)                                  \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCENABLE__VAL__MODIFY(reg_offset, value)                         \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QSMCENABLE__VAL__SET(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QSMCENABLE__VAL__CLR(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCRESET : val		*/
/*  Description: Soft reset per channel,when write 1'b1 should clear all internal credits/counter/status. Should be set when channel is disabled,usually with vDMA channel reset (abort). Write 1'b0 should do nothing. Read always return 1'b0. Implemented as external register type. */
#define DRAM_DMA_ENGINE_CONFIG__QSMCRESET__VAL__SHIFT                                              (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCRESET__VAL__WIDTH                                              (1)
#define DRAM_DMA_ENGINE_CONFIG__QSMCRESET__VAL__MASK                                               (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCRESET__VAL__RESET                                              (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCRESET__VAL__READ(reg_offset)                                   \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCRESET__VAL__MODIFY(reg_offset, value)                          \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__QSMCRESET__VAL__SET(reg_offset)                                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__QSMCRESET__VAL__CLR(reg_offset)                                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCMODE : val		*/
/*  Description: QSMC mode of operation: 2'd0 - CONT_MODE 2'd1 - reserved. 2'd2 - BURST_MODE 2'd3 - C2C_MODE */
#define DRAM_DMA_ENGINE_CONFIG__QSMCMODE__VAL__SHIFT                                               (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMODE__VAL__WIDTH                                               (2)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMODE__VAL__MASK                                                (0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMODE__VAL__RESET                                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMODE__VAL__READ(reg_offset)                                    \
			(((uint32_t)(reg_offset) & 0x00000003L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMODE__VAL__MODIFY(reg_offset, value)                           \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | (((uint32_t)(value) << 0) & 0x00000003L))
#define DRAM_DMA_ENGINE_CONFIG__QSMCMODE__VAL__SET(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | 0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMODE__VAL__CLR(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0x00000003L))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCC2CSEL : val		*/
/*  Description: Selector for Channel-to-Channel credit input,selects QDMC channel as source for HW available descriptors */
#define DRAM_DMA_ENGINE_CONFIG__QSMCC2CSEL__VAL__SHIFT                                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCC2CSEL__VAL__WIDTH                                             (6)
#define DRAM_DMA_ENGINE_CONFIG__QSMCC2CSEL__VAL__MASK                                              (0x0000003FL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCC2CSEL__VAL__RESET                                             (0x0000003FL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCC2CSEL__VAL__READ(reg_offset)                                  \
			(((uint32_t)(reg_offset) & 0x0000003FL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCC2CSEL__VAL__MODIFY(reg_offset, value)                         \
			(reg_offset) = (((reg_offset) & ~0x0000003FL) | (((uint32_t)(value) << 0) & 0x0000003FL))
#define DRAM_DMA_ENGINE_CONFIG__QSMCC2CSEL__VAL__SET(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x0000003FL) | 0x0000003FL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCC2CSEL__VAL__CLR(reg_offset)                                   \
			(reg_offset) = (((reg_offset) & ~0x0000003FL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCADDBURSTVAL : val		*/
/*  Description: Writing to this register increment the available descriptor counter in QSMC by QsmcAddBurstVal descriptors: AvailableDescsCounter += QsmcAddBurstVal. Reading this register should return the current available descriptors counter (AvailableDescsCounter). Implemented as external register type. */
#define DRAM_DMA_ENGINE_CONFIG__QSMCADDBURSTVAL__VAL__SHIFT                                        (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCADDBURSTVAL__VAL__WIDTH                                        (18)
#define DRAM_DMA_ENGINE_CONFIG__QSMCADDBURSTVAL__VAL__MASK                                         (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCADDBURSTVAL__VAL__RESET                                        (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCADDBURSTVAL__VAL__READ(reg_offset)                             \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCADDBURSTVAL__VAL__MODIFY(reg_offset, value)                    \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMCADDBURSTVAL__VAL__SET(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCADDBURSTVAL__VAL__CLR(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCMEMBASEADDR : val		*/
/*  Description: Base address to the CCB in the DDR memory space. aligned to minimum page size of 512B. */
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMBASEADDR__VAL__SHIFT                                        (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMBASEADDR__VAL__WIDTH                                        (26)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMBASEADDR__VAL__MASK                                         (0x03FFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMBASEADDR__VAL__RESET                                        (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMBASEADDR__VAL__READ(reg_offset)                             \
			(((uint32_t)(reg_offset) & 0x03FFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMBASEADDR__VAL__MODIFY(reg_offset, value)                    \
			(reg_offset) = (((reg_offset) & ~0x03FFFFFFL) | (((uint32_t)(value) << 0) & 0x03FFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMBASEADDR__VAL__SET(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x03FFFFFFL) | 0x03FFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMBASEADDR__VAL__CLR(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x03FFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCMEMCCBSIZE : val		*/
/*  Description: The CCB size minus one in page size granularity. 0 - 1 desc 1 - 2 desc ... N_CCB_MAX_DESC-1 - N_CCB_MAX_DESC desc. */
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMCCBSIZE__VAL__SHIFT                                         (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMCCBSIZE__VAL__WIDTH                                         (18)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMCCBSIZE__VAL__MASK                                          (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMCCBSIZE__VAL__RESET                                         (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMCCBSIZE__VAL__READ(reg_offset)                              \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMCCBSIZE__VAL__MODIFY(reg_offset, value)                     \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMCCBSIZE__VAL__SET(reg_offset)                               \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCMEMCCBSIZE__VAL__CLR(reg_offset)                               \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCPAGESIZE : val		*/
/*  Description: M2D Memory page size. Valid values are: 0 - 512B,1 - 1KB,2 - 2KB,3 - 4KB,4 - 1536B. */
#define DRAM_DMA_ENGINE_CONFIG__QSMCPAGESIZE__VAL__SHIFT                                           (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCPAGESIZE__VAL__WIDTH                                           (3)
#define DRAM_DMA_ENGINE_CONFIG__QSMCPAGESIZE__VAL__MASK                                            (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCPAGESIZE__VAL__RESET                                           (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCPAGESIZE__VAL__READ(reg_offset)                                \
			(((uint32_t)(reg_offset) & 0x00000007L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCPAGESIZE__VAL__MODIFY(reg_offset, value)                       \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | (((uint32_t)(value) << 0) & 0x00000007L))
#define DRAM_DMA_ENGINE_CONFIG__QSMCPAGESIZE__VAL__SET(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | 0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCPAGESIZE__VAL__CLR(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x00000007L))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCSIMPPATTERNNUMPAGES : val		*/
/*  Description: Number of pages per line (simplified pattern has single line/pattern). */
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNNUMPAGES__VAL__SHIFT                                (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNNUMPAGES__VAL__WIDTH                                (18)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNNUMPAGES__VAL__MASK                                 (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNNUMPAGES__VAL__RESET                                (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNNUMPAGES__VAL__READ(reg_offset)                     \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNNUMPAGES__VAL__MODIFY(reg_offset, value)            \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNNUMPAGES__VAL__SET(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNNUMPAGES__VAL__CLR(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCSIMPPATTERNRESIDUEPAGESIZE : val		*/
/*  Description: Residue page size in 8B granularity,minus one,per pattern. 0-8B,1-16B,...,511-4kB */
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNRESIDUEPAGESIZE__VAL__SHIFT                         (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNRESIDUEPAGESIZE__VAL__WIDTH                         (9)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNRESIDUEPAGESIZE__VAL__MASK                          (0x000001FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNRESIDUEPAGESIZE__VAL__RESET                         (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNRESIDUEPAGESIZE__VAL__READ(reg_offset)              \
			(((uint32_t)(reg_offset) & 0x000001FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNRESIDUEPAGESIZE__VAL__MODIFY(reg_offset, value)     \
			(reg_offset) = (((reg_offset) & ~0x000001FFL) | (((uint32_t)(value) << 0) & 0x000001FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNRESIDUEPAGESIZE__VAL__SET(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x000001FFL) | 0x000001FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCSIMPPATTERNRESIDUEPAGESIZE__VAL__CLR(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x000001FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCBANKINTERLEAVEMODE : val		*/
/*  Description: Select the bank interleave mode: 2'd0 - interleave 8 banks (default),2'd1 - Interleave 4 banks,2'd2 - Interleave 2 banks,2'd3 - no interleave. */
#define DRAM_DMA_ENGINE_CONFIG__QSMCBANKINTERLEAVEMODE__VAL__SHIFT                                 (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCBANKINTERLEAVEMODE__VAL__WIDTH                                 (2)
#define DRAM_DMA_ENGINE_CONFIG__QSMCBANKINTERLEAVEMODE__VAL__MASK                                  (0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCBANKINTERLEAVEMODE__VAL__RESET                                 (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCBANKINTERLEAVEMODE__VAL__READ(reg_offset)                      \
			(((uint32_t)(reg_offset) & 0x00000003L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCBANKINTERLEAVEMODE__VAL__MODIFY(reg_offset, value)             \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | (((uint32_t)(value) << 0) & 0x00000003L))
#define DRAM_DMA_ENGINE_CONFIG__QSMCBANKINTERLEAVEMODE__VAL__SET(reg_offset)                       \
			(reg_offset) = (((reg_offset) & ~0x00000003L) | 0x00000003L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCBANKINTERLEAVEMODE__VAL__CLR(reg_offset)                       \
			(reg_offset) = (((reg_offset) & ~0x00000003L))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCDESCPERIPHINTERRUPT : val		*/
/*  Description: When > 0 the QSMC will interrupt the peripheral every read QsmcDescPeriphInterrupt descriptors. */
#define DRAM_DMA_ENGINE_CONFIG__QSMCDESCPERIPHINTERRUPT__VAL__SHIFT                                (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCDESCPERIPHINTERRUPT__VAL__WIDTH                                (18)
#define DRAM_DMA_ENGINE_CONFIG__QSMCDESCPERIPHINTERRUPT__VAL__MASK                                 (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCDESCPERIPHINTERRUPT__VAL__RESET                                (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCDESCPERIPHINTERRUPT__VAL__READ(reg_offset)                     \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCDESCPERIPHINTERRUPT__VAL__MODIFY(reg_offset, value)            \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | (((uint32_t)(value) << 0) & 0x0003FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMCDESCPERIPHINTERRUPT__VAL__SET(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL) | 0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCDESCPERIPHINTERRUPT__VAL__CLR(reg_offset)                      \
			(reg_offset) = (((reg_offset) & ~0x0003FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMCCCBFREEINDEX : val		*/
/*  Description: Used by the peripheral to indicates how many data is ready in the CCB for write (process). This is the CcbIndex (free pointer in CCB). */
#define DRAM_DMA_ENGINE_CONFIG__QSMCCCBFREEINDEX__VAL__SHIFT                                       (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMCCCBFREEINDEX__VAL__WIDTH                                       (18)
#define DRAM_DMA_ENGINE_CONFIG__QSMCCCBFREEINDEX__VAL__MASK                                        (0x0003FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMCCCBFREEINDEX__VAL__RESET                                       (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMCCCBFREEINDEX__VAL__READ(reg_offset)                            \
			(((uint32_t)(reg_offset) & 0x0003FFFFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_CS_INTR_MASK : val		*/
/*  Description: INT register bits[15:0] per M2D channel,indicating one of the following events: a. Internal desc - QSMC processed last CCB descriptor. Implemented by set the interrupt when CCB-free-index is wrapped (become zero),might be used for CONF channel - to indicates conf is done. bits[31:16] per D2M channel indicating one of the following events: Internal desc - QDMC processed descriptors per QdmcDescCsInterrupt (OR) External desc - domain#0 (local) source/destination event. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_MASK__VAL__SHIFT                                    (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_MASK__VAL__WIDTH                                    (32)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_MASK__VAL__MASK                                     (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_MASK__VAL__RESET                                    (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_MASK__VAL__READ(reg_offset)                         \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_MASK__VAL__MODIFY(reg_offset, value)                \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | (((uint32_t)(value) << 0) & 0xFFFFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_MASK__VAL__SET(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | 0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_MASK__VAL__CLR(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_CS_INTR_STATUS : val		*/
/*  Description: INT register bits[15:0] per M2D channel,indicating one of the following events: a. Internal desc - QSMC processed last CCB descriptor. Implemented by set the interrupt when CCB-free-index is wrapped (become zero),might be used for CONF channel - to indicates conf is done. bits[31:16] per D2M channel indicating one of the following events: Internal desc - QDMC processed descriptors per QdmcDescCsInterrupt (OR) External desc - domain#0 (local) source/destination event. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_STATUS__VAL__SHIFT                                  (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_STATUS__VAL__WIDTH                                  (32)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_STATUS__VAL__MASK                                   (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_STATUS__VAL__RESET                                  (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_STATUS__VAL__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_CS_INTR_W1C : val		*/
/*  Description: INT register bits[15:0] per M2D channel,indicating one of the following events: a. Internal desc - QSMC processed last CCB descriptor. Implemented by set the interrupt when CCB-free-index is wrapped (become zero),might be used for CONF channel - to indicates conf is done. bits[31:16] per D2M channel indicating one of the following events: Internal desc - QDMC processed descriptors per QdmcDescCsInterrupt (OR) External desc - domain#0 (local) source/destination event. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1C__VAL__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1C__VAL__WIDTH                                     (32)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1C__VAL__MASK                                      (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1C__VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1C__VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1C__VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | (((uint32_t)(value) << 0) & 0xFFFFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1C__VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | 0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1C__VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_CS_INTR_W1S : val		*/
/*  Description: INT register bits[15:0] per M2D channel,indicating one of the following events: a. Internal desc - QSMC processed last CCB descriptor. Implemented by set the interrupt when CCB-free-index is wrapped (become zero),might be used for CONF channel - to indicates conf is done. bits[31:16] per D2M channel indicating one of the following events: Internal desc - QDMC processed descriptors per QdmcDescCsInterrupt (OR) External desc - domain#0 (local) source/destination event. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1S__VAL__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1S__VAL__WIDTH                                     (32)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1S__VAL__MASK                                      (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1S__VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1S__VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1S__VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | (((uint32_t)(value) << 0) & 0xFFFFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1S__VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | 0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_CS_INTR_W1S__VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_AP_INTR_MASK : val		*/
/*  Description: INT register bit per direction/channel indicating one of the following events: Internal desc - QDMC processed descriptors per QdmcDescPeriphInterrupt (D2M enhanced channels only) (OR) Internal desc - QSMC processed descriptors per QsmcDescPeriphInterrupt (M2D enhanced channels only) (OR) External desc - domain#1 (host) source/destination event */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_MASK__VAL__SHIFT                                    (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_MASK__VAL__WIDTH                                    (32)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_MASK__VAL__MASK                                     (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_MASK__VAL__RESET                                    (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_MASK__VAL__READ(reg_offset)                         \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_MASK__VAL__MODIFY(reg_offset, value)                \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | (((uint32_t)(value) << 0) & 0xFFFFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_MASK__VAL__SET(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | 0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_MASK__VAL__CLR(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_AP_INTR_STATUS : val		*/
/*  Description: INT register bit per direction/channel indicating one of the following events: Internal desc - QDMC processed descriptors per QdmcDescPeriphInterrupt (D2M enhanced channels only) (OR) Internal desc - QSMC processed descriptors per QsmcDescPeriphInterrupt (M2D enhanced channels only) (OR) External desc - domain#1 (host) source/destination event */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_STATUS__VAL__SHIFT                                  (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_STATUS__VAL__WIDTH                                  (32)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_STATUS__VAL__MASK                                   (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_STATUS__VAL__RESET                                  (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_STATUS__VAL__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_AP_INTR_W1C : val		*/
/*  Description: INT register bit per direction/channel indicating one of the following events: Internal desc - QDMC processed descriptors per QdmcDescPeriphInterrupt (D2M enhanced channels only) (OR) Internal desc - QSMC processed descriptors per QsmcDescPeriphInterrupt (M2D enhanced channels only) (OR) External desc - domain#1 (host) source/destination event */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1C__VAL__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1C__VAL__WIDTH                                     (32)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1C__VAL__MASK                                      (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1C__VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1C__VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1C__VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | (((uint32_t)(value) << 0) & 0xFFFFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1C__VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | 0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1C__VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_AP_INTR_W1S : val		*/
/*  Description: INT register bit per direction/channel indicating one of the following events: Internal desc - QDMC processed descriptors per QdmcDescPeriphInterrupt (D2M enhanced channels only) (OR) Internal desc - QSMC processed descriptors per QsmcDescPeriphInterrupt (M2D enhanced channels only) (OR) External desc - domain#1 (host) source/destination event */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1S__VAL__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1S__VAL__WIDTH                                     (32)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1S__VAL__MASK                                      (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1S__VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1S__VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1S__VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | (((uint32_t)(value) << 0) & 0xFFFFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1S__VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | 0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_AP_INTR_W1S__VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_DSP_INTR_MASK : val		*/
/*  Description: INT register */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_MASK__VAL__SHIFT                                   (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_MASK__VAL__WIDTH                                   (8)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_MASK__VAL__MASK                                    (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_MASK__VAL__RESET                                   (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_MASK__VAL__READ(reg_offset)                        \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_MASK__VAL__MODIFY(reg_offset, value)               \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_MASK__VAL__SET(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_MASK__VAL__CLR(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_DSP_INTR_STATUS : val		*/
/*  Description: INT register */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_STATUS__VAL__SHIFT                                 (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_STATUS__VAL__WIDTH                                 (8)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_STATUS__VAL__MASK                                  (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_STATUS__VAL__RESET                                 (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_STATUS__VAL__READ(reg_offset)                      \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_DSP_INTR_W1C : val		*/
/*  Description: INT register */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1C__VAL__SHIFT                                    (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1C__VAL__WIDTH                                    (8)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1C__VAL__MASK                                     (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1C__VAL__RESET                                    (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1C__VAL__READ(reg_offset)                         \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1C__VAL__MODIFY(reg_offset, value)                \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1C__VAL__SET(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1C__VAL__CLR(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_DSP_INTR_W1S : val		*/
/*  Description: INT register */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1S__VAL__SHIFT                                    (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1S__VAL__WIDTH                                    (8)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1S__VAL__MASK                                     (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1S__VAL__RESET                                    (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1S__VAL__READ(reg_offset)                         \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1S__VAL__MODIFY(reg_offset, value)                \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1S__VAL__SET(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_DSP_INTR_W1S__VAL__CLR(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_ERR_INTR_MASK : desc_err		*/
/*  Description: Summary of desc_err_intr register. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__DESC_ERR__SHIFT                              (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__DESC_ERR__WIDTH                              (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__DESC_ERR__MASK                               (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__DESC_ERR__RESET                              (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__DESC_ERR__READ(reg_offset)                   \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__DESC_ERR__MODIFY(reg_offset, value)          \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__DESC_ERR__SET(reg_offset)                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__DESC_ERR__CLR(reg_offset)                    \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*  ENGINE_ERR_INTR_MASK : qddc_crd_ovf_err		*/
/*  Description: Summary of qddc_crd_ovf_err_intr register. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QDDC_CRD_OVF_ERR__SHIFT                      (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QDDC_CRD_OVF_ERR__WIDTH                      (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QDDC_CRD_OVF_ERR__MASK                       (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QDDC_CRD_OVF_ERR__RESET                      (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QDDC_CRD_OVF_ERR__READ(reg_offset)           \
			(((uint32_t)(reg_offset) & 0x00000002L) >> 1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QDDC_CRD_OVF_ERR__MODIFY(reg_offset, value)  \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | (((uint32_t)(value) << 1) & 0x00000002L))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QDDC_CRD_OVF_ERR__SET(reg_offset)            \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(1) << 1))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QDDC_CRD_OVF_ERR__CLR(reg_offset)            \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(0) << 1))

/*  ENGINE_ERR_INTR_MASK : qsdc_crd_ovf_err		*/
/*  Description: Summary of qsdc_crd_ovf_err_intr register. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QSDC_CRD_OVF_ERR__SHIFT                      (2)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QSDC_CRD_OVF_ERR__WIDTH                      (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QSDC_CRD_OVF_ERR__MASK                       (0x00000004L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QSDC_CRD_OVF_ERR__RESET                      (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QSDC_CRD_OVF_ERR__READ(reg_offset)           \
			(((uint32_t)(reg_offset) & 0x00000004L) >> 2)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QSDC_CRD_OVF_ERR__MODIFY(reg_offset, value)  \
			(reg_offset) = (((reg_offset) & ~0x00000004L) | (((uint32_t)(value) << 2) & 0x00000004L))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QSDC_CRD_OVF_ERR__SET(reg_offset)            \
			(reg_offset) = (((reg_offset) & ~0x00000004L) | ((uint32_t)(1) << 2))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_MASK__QSDC_CRD_OVF_ERR__CLR(reg_offset)            \
			(reg_offset) = (((reg_offset) & ~0x00000004L) | ((uint32_t)(0) << 2))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_ERR_INTR_STATUS : desc_err		*/
/*  Description: Summary of desc_err_intr register. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__DESC_ERR__SHIFT                            (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__DESC_ERR__WIDTH                            (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__DESC_ERR__MASK                             (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__DESC_ERR__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__DESC_ERR__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)

/*  ENGINE_ERR_INTR_STATUS : qddc_crd_ovf_err		*/
/*  Description: Summary of qddc_crd_ovf_err_intr register. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QDDC_CRD_OVF_ERR__SHIFT                    (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QDDC_CRD_OVF_ERR__WIDTH                    (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QDDC_CRD_OVF_ERR__MASK                     (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QDDC_CRD_OVF_ERR__RESET                    (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QDDC_CRD_OVF_ERR__READ(reg_offset)         \
			(((uint32_t)(reg_offset) & 0x00000002L) >> 1)

/*  ENGINE_ERR_INTR_STATUS : qsdc_crd_ovf_err		*/
/*  Description: Summary of qsdc_crd_ovf_err_intr register. */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QSDC_CRD_OVF_ERR__SHIFT                    (2)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QSDC_CRD_OVF_ERR__WIDTH                    (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QSDC_CRD_OVF_ERR__MASK                     (0x00000004L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QSDC_CRD_OVF_ERR__RESET                    (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ERR_INTR_STATUS__QSDC_CRD_OVF_ERR__READ(reg_offset)         \
			(((uint32_t)(reg_offset) & 0x00000004L) >> 2)

/*----------------------------------------------------------------------------------------------------*/
/*  DESC_ERR_INTR_MASK : DescStatus		*/
/*  Description: Interrupt bit per DESC_STATUS fields of vDMA descriptor which returned unexpected value (Note that successful descriptor returns status of 8'h1). Refer to EngErrInterruptSource register for the error origin. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DESCSTATUS__SHIFT                              (0)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DESCSTATUS__WIDTH                              (8)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DESCSTATUS__MASK                               (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DESCSTATUS__RESET                              (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DESCSTATUS__READ(reg_offset)                   \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DESCSTATUS__MODIFY(reg_offset, value)          \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DESCSTATUS__SET(reg_offset)                    \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DESCSTATUS__CLR(reg_offset)                    \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*  DESC_ERR_INTR_MASK : RemainPageSize		*/
/*  Description: non-zero REMAINING_PAGE_SIZE. Refer to EngErrInterruptSource register for the error origin. Refer to EngErrRemainPageSize register for the returned value. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__REMAINPAGESIZE__SHIFT                          (8)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__REMAINPAGESIZE__WIDTH                          (1)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__REMAINPAGESIZE__MASK                           (0x00000100L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__REMAINPAGESIZE__RESET                          (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__REMAINPAGESIZE__READ(reg_offset)               \
			(((uint32_t)(reg_offset) & 0x00000100L) >> 8)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__REMAINPAGESIZE__MODIFY(reg_offset, value)      \
			(reg_offset) = (((reg_offset) & ~0x00000100L) | (((uint32_t)(value) << 8) & 0x00000100L))
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__REMAINPAGESIZE__SET(reg_offset)                \
			(reg_offset) = (((reg_offset) & ~0x00000100L) | ((uint32_t)(1) << 8))
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__REMAINPAGESIZE__CLR(reg_offset)                \
			(reg_offset) = (((reg_offset) & ~0x00000100L) | ((uint32_t)(0) << 8))

/*  DESC_ERR_INTR_MASK : SrcDescWdataPar		*/
/*  Description: Source descriptor complete with error status. Refer to EngErrInterruptSource register for the error origin. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__SRCDESCWDATAPAR__SHIFT                         (9)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__SRCDESCWDATAPAR__WIDTH                         (1)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__SRCDESCWDATAPAR__MASK                          (0x00000200L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__SRCDESCWDATAPAR__RESET                         (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__SRCDESCWDATAPAR__READ(reg_offset)              \
			(((uint32_t)(reg_offset) & 0x00000200L) >> 9)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__SRCDESCWDATAPAR__MODIFY(reg_offset, value)     \
			(reg_offset) = (((reg_offset) & ~0x00000200L) | (((uint32_t)(value) << 9) & 0x00000200L))
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__SRCDESCWDATAPAR__SET(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x00000200L) | ((uint32_t)(1) << 9))
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__SRCDESCWDATAPAR__CLR(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x00000200L) | ((uint32_t)(0) << 9))

/*  DESC_ERR_INTR_MASK : DstDescWdataPar		*/
/*  Description: Destination descriptor complete with error status. Refer to EngErrInterruptSource register for the error origin. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DSTDESCWDATAPAR__SHIFT                         (10)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DSTDESCWDATAPAR__WIDTH                         (1)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DSTDESCWDATAPAR__MASK                          (0x00000400L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DSTDESCWDATAPAR__RESET                         (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DSTDESCWDATAPAR__READ(reg_offset)              \
			(((uint32_t)(reg_offset) & 0x00000400L) >> 10)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DSTDESCWDATAPAR__MODIFY(reg_offset, value)     \
			(reg_offset) = (((reg_offset) & ~0x00000400L) | (((uint32_t)(value) << 10) & 0x00000400L))
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DSTDESCWDATAPAR__SET(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x00000400L) | ((uint32_t)(1) << 10))
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_MASK__DSTDESCWDATAPAR__CLR(reg_offset)               \
			(reg_offset) = (((reg_offset) & ~0x00000400L) | ((uint32_t)(0) << 10))

/*----------------------------------------------------------------------------------------------------*/
/*  DESC_ERR_INTR_STATUS : DescStatus		*/
/*  Description: Interrupt bit per DESC_STATUS fields of vDMA descriptor which returned unexpected value (Note that successful descriptor returns status of 8'h1). Refer to EngErrInterruptSource register for the error origin. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DESCSTATUS__SHIFT                            (0)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DESCSTATUS__WIDTH                            (8)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DESCSTATUS__MASK                             (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DESCSTATUS__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DESCSTATUS__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)

/*  DESC_ERR_INTR_STATUS : RemainPageSize		*/
/*  Description: non-zero REMAINING_PAGE_SIZE. Refer to EngErrInterruptSource register for the error origin. Refer to EngErrRemainPageSize register for the returned value. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__REMAINPAGESIZE__SHIFT                        (8)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__REMAINPAGESIZE__WIDTH                        (1)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__REMAINPAGESIZE__MASK                         (0x00000100L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__REMAINPAGESIZE__RESET                        (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__REMAINPAGESIZE__READ(reg_offset)             \
			(((uint32_t)(reg_offset) & 0x00000100L) >> 8)

/*  DESC_ERR_INTR_STATUS : SrcDescWdataPar		*/
/*  Description: Source descriptor complete with error status. Refer to EngErrInterruptSource register for the error origin. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__SRCDESCWDATAPAR__SHIFT                       (9)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__SRCDESCWDATAPAR__WIDTH                       (1)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__SRCDESCWDATAPAR__MASK                        (0x00000200L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__SRCDESCWDATAPAR__RESET                       (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__SRCDESCWDATAPAR__READ(reg_offset)            \
			(((uint32_t)(reg_offset) & 0x00000200L) >> 9)

/*  DESC_ERR_INTR_STATUS : DstDescWdataPar		*/
/*  Description: Destination descriptor complete with error status. Refer to EngErrInterruptSource register for the error origin. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DSTDESCWDATAPAR__SHIFT                       (10)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DSTDESCWDATAPAR__WIDTH                       (1)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DSTDESCWDATAPAR__MASK                        (0x00000400L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DSTDESCWDATAPAR__RESET                       (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_STATUS__DSTDESCWDATAPAR__READ(reg_offset)            \
			(((uint32_t)(reg_offset) & 0x00000400L) >> 10)

/*----------------------------------------------------------------------------------------------------*/
/*  DESC_ERR_INTR_W1C : DescStatus		*/
/*  Description: Interrupt bit per DESC_STATUS fields of vDMA descriptor which returned unexpected value (Note that successful descriptor returns status of 8'h1). Refer to EngErrInterruptSource register for the error origin. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1C__DESCSTATUS__SHIFT                               (0)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1C__DESCSTATUS__WIDTH                               (8)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1C__DESCSTATUS__MASK                                (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1C__DESCSTATUS__RESET                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1C__DESCSTATUS__READ(reg_offset)                    \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1C__DESCSTATUS__MODIFY(reg_offset, value)           \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1C__DESCSTATUS__SET(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1C__DESCSTATUS__CLR(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  DESC_ERR_INTR_W1S : DescStatus		*/
/*  Description: Interrupt bit per DESC_STATUS fields of vDMA descriptor which returned unexpected value (Note that successful descriptor returns status of 8'h1). Refer to EngErrInterruptSource register for the error origin. */
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1S__DESCSTATUS__SHIFT                               (0)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1S__DESCSTATUS__WIDTH                               (8)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1S__DESCSTATUS__MASK                                (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1S__DESCSTATUS__RESET                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1S__DESCSTATUS__READ(reg_offset)                    \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1S__DESCSTATUS__MODIFY(reg_offset, value)           \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1S__DESCSTATUS__SET(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__DESC_ERR_INTR_W1S__DESCSTATUS__CLR(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDC_CRD_OVF_ERR_INTR_MASK : ch		*/
/*  Description: Interrupt bit per QDDC channel indicating overflow or underflow in Core credit counter. */
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_MASK__CH__SHIFT                              (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_MASK__CH__WIDTH                              (16)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_MASK__CH__MASK                               (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_MASK__CH__RESET                              (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_MASK__CH__READ(reg_offset)                   \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_MASK__CH__MODIFY(reg_offset, value)          \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_MASK__CH__SET(reg_offset)                    \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_MASK__CH__CLR(reg_offset)                    \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDC_CRD_OVF_ERR_INTR_STATUS : ch		*/
/*  Description: Interrupt bit per QDDC channel indicating overflow or underflow in Core credit counter. */
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_STATUS__CH__SHIFT                            (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_STATUS__CH__WIDTH                            (16)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_STATUS__CH__MASK                             (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_STATUS__CH__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_STATUS__CH__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  QDDC_CRD_OVF_ERR_INTR_W1C : ch		*/
/*  Description: Interrupt bit per QDDC channel indicating overflow or underflow in Core credit counter. */
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1C__CH__SHIFT                               (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1C__CH__WIDTH                               (16)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1C__CH__MASK                                (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1C__CH__RESET                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1C__CH__READ(reg_offset)                    \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1C__CH__MODIFY(reg_offset, value)           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1C__CH__SET(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1C__CH__CLR(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDC_CRD_OVF_ERR_INTR_W1S : ch		*/
/*  Description: Interrupt bit per QDDC channel indicating overflow or underflow in Core credit counter. */
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1S__CH__SHIFT                               (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1S__CH__WIDTH                               (16)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1S__CH__MASK                                (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1S__CH__RESET                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1S__CH__READ(reg_offset)                    \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1S__CH__MODIFY(reg_offset, value)           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1S__CH__SET(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CRD_OVF_ERR_INTR_W1S__CH__CLR(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDC_CRD_OVF_ERR_INTR_MASK : ch		*/
/*  Description: Interrupt bit per QSDC channel indicating overflow or underflow in Core credit counter. */
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_MASK__CH__SHIFT                              (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_MASK__CH__WIDTH                              (16)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_MASK__CH__MASK                               (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_MASK__CH__RESET                              (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_MASK__CH__READ(reg_offset)                   \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_MASK__CH__MODIFY(reg_offset, value)          \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_MASK__CH__SET(reg_offset)                    \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_MASK__CH__CLR(reg_offset)                    \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDC_CRD_OVF_ERR_INTR_STATUS : ch		*/
/*  Description: Interrupt bit per QSDC channel indicating overflow or underflow in Core credit counter. */
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_STATUS__CH__SHIFT                            (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_STATUS__CH__WIDTH                            (16)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_STATUS__CH__MASK                             (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_STATUS__CH__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_STATUS__CH__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  QSDC_CRD_OVF_ERR_INTR_W1C : ch		*/
/*  Description: Interrupt bit per QSDC channel indicating overflow or underflow in Core credit counter. */
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1C__CH__SHIFT                               (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1C__CH__WIDTH                               (16)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1C__CH__MASK                                (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1C__CH__RESET                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1C__CH__READ(reg_offset)                    \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1C__CH__MODIFY(reg_offset, value)           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1C__CH__SET(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1C__CH__CLR(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDC_CRD_OVF_ERR_INTR_W1S : ch		*/
/*  Description: Interrupt bit per QSDC channel indicating overflow or underflow in Core credit counter. */
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1S__CH__SHIFT                               (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1S__CH__WIDTH                               (16)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1S__CH__MASK                                (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1S__CH__RESET                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1S__CH__READ(reg_offset)                    \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1S__CH__MODIFY(reg_offset, value)           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1S__CH__SET(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CRD_OVF_ERR_INTR_W1S__CH__CLR(reg_offset)                     \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGERRINTERRUPTSOURCE : ChannelID		*/
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__CHANNELID__SHIFT                            (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__CHANNELID__WIDTH                            (4)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__CHANNELID__MASK                             (0x0000000FL)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__CHANNELID__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__CHANNELID__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x0000000FL) >> 0)

/*  ENGERRINTERRUPTSOURCE : Direction		*/
/*  Description: 0 - Destination. 1 - Source. */
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DIRECTION__SHIFT                            (4)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DIRECTION__WIDTH                            (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DIRECTION__MASK                             (0x00000010L)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DIRECTION__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DIRECTION__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x00000010L) >> 4)

/*  ENGERRINTERRUPTSOURCE : Domain		*/
/*  Description: 0 - Device. 1 - Memory. */
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DOMAIN__SHIFT                               (5)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DOMAIN__WIDTH                               (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DOMAIN__MASK                                (0x00000020L)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DOMAIN__RESET                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRINTERRUPTSOURCE__DOMAIN__READ(reg_offset)                    \
			(((uint32_t)(reg_offset) & 0x00000020L) >> 5)

/*----------------------------------------------------------------------------------------------------*/
/*  ENGERRREMAINPAGESIZE : val		*/
/*  Description: In case of non-zero REMAINING_PAGE_SIZE this register holds the latched value until cleared by writing to this register */
#define DRAM_DMA_ENGINE_CONFIG__ENGERRREMAINPAGESIZE__VAL__SHIFT                                   (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRREMAINPAGESIZE__VAL__WIDTH                                   (24)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRREMAINPAGESIZE__VAL__MASK                                    (0x00FFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRREMAINPAGESIZE__VAL__RESET                                   (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGERRREMAINPAGESIZE__VAL__READ(reg_offset)                        \
			(((uint32_t)(reg_offset) & 0x00FFFFFFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  ENGTRANSFERPAGESIZE : size		*/
/*  Description: TRANSFERRED_PAGE_SIZE value of last descriptor write to QDMC */
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__SIZE__SHIFT                                   (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__SIZE__WIDTH                                   (24)
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__SIZE__MASK                                    (0x00FFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__SIZE__RESET                                   (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__SIZE__READ(reg_offset)                        \
			(((uint32_t)(reg_offset) & 0x00FFFFFFL) >> 0)

/*  ENGTRANSFERPAGESIZE : ch_id		*/
/*  Description: QDMC Channel ID */
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__CH_ID__SHIFT                                  (24)
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__CH_ID__WIDTH                                  (4)
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__CH_ID__MASK                                   (0x0F000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__CH_ID__RESET                                  (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGTRANSFERPAGESIZE__CH_ID__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0x0F000000L) >> 24)

/*----------------------------------------------------------------------------------------------------*/
/*  VDMASOFTRESET : val		*/
/*  Description: Apply soft reset to vDMA. Must be cleared in order to release vDMA from soft reset. */
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__VAL__SHIFT                                          (0)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__VAL__WIDTH                                          (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__VAL__MASK                                           (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__VAL__RESET                                          (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__VAL__READ(reg_offset)                               \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__VAL__MODIFY(reg_offset, value)                      \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__VAL__SET(reg_offset)                                \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__VAL__CLR(reg_offset)                                \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*  VDMASOFTRESET : par		*/
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__PAR__SHIFT                                          (31)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__PAR__WIDTH                                          (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__PAR__MASK                                           (0x80000000L)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__PAR__RESET                                          (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__PAR__READ(reg_offset)                               \
			(((uint32_t)(reg_offset) & 0x80000000L) >> 31)
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__PAR__MODIFY(reg_offset, value)                      \
			(reg_offset) = (((reg_offset) & ~0x80000000L) | (((uint32_t)(value) << 31) & 0x80000000L))
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__PAR__SET(reg_offset)                                \
			(reg_offset) = (((reg_offset) & ~0x80000000L) | ((uint32_t)(1) << 31))
#define DRAM_DMA_ENGINE_CONFIG__VDMASOFTRESET__PAR__CLR(reg_offset)                                \
			(reg_offset) = (((reg_offset) & ~0x80000000L) | ((uint32_t)(0) << 31))

/*----------------------------------------------------------------------------------------------------*/
/*  VDMA_SHAREDBUS : cs_mask		*/
/*  Description: Bit mask on vDMA Sharedbus interrupt source for CS */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__CS_MASK__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__CS_MASK__WIDTH                                     (4)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__CS_MASK__MASK                                      (0x0000000FL)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__CS_MASK__RESET                                     (0x0000000AL)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__CS_MASK__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x0000000FL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__CS_MASK__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x0000000FL) | (((uint32_t)(value) << 0) & 0x0000000FL))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__CS_MASK__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000000FL) | 0x0000000FL)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__CS_MASK__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000000FL))

/*  VDMA_SHAREDBUS : ap_mask		*/
/*  Description: Bit mask on vDMA Sharedbus interrupt source for AP */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__AP_MASK__SHIFT                                     (4)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__AP_MASK__WIDTH                                     (4)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__AP_MASK__MASK                                      (0x000000F0L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__AP_MASK__RESET                                     (0x00000050L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__AP_MASK__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x000000F0L) >> 4)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__AP_MASK__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x000000F0L) | (((uint32_t)(value) << 4) & 0x000000F0L))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__AP_MASK__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x000000F0L) | 0x000000F0L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SHAREDBUS__AP_MASK__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x000000F0L))

/*----------------------------------------------------------------------------------------------------*/
/*  CFG_QDDC_REDUNDANT_EN : val		*/
/*  Description: Redundancy mode enable bit per QM pair. bit i makes QM[i*2+1] a redundancy for QM[i*2] */
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDDC_REDUNDANT_EN__VAL__SHIFT                                  (0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDDC_REDUNDANT_EN__VAL__WIDTH                                  (8)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDDC_REDUNDANT_EN__VAL__MASK                                   (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDDC_REDUNDANT_EN__VAL__RESET                                  (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDDC_REDUNDANT_EN__VAL__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDDC_REDUNDANT_EN__VAL__MODIFY(reg_offset, value)              \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDDC_REDUNDANT_EN__VAL__SET(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDDC_REDUNDANT_EN__VAL__CLR(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  CFG_QSDC_REDUNDANT_EN : val		*/
/*  Description: Redundancy mode enable bit per QM pair. bit i makes QM[i*2+1] a redundancy for QM[i*2] */
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSDC_REDUNDANT_EN__VAL__SHIFT                                  (0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSDC_REDUNDANT_EN__VAL__WIDTH                                  (8)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSDC_REDUNDANT_EN__VAL__MASK                                   (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSDC_REDUNDANT_EN__VAL__RESET                                  (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSDC_REDUNDANT_EN__VAL__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSDC_REDUNDANT_EN__VAL__MODIFY(reg_offset, value)              \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSDC_REDUNDANT_EN__VAL__SET(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSDC_REDUNDANT_EN__VAL__CLR(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  CFG_QDMC_REDUNDANT_EN : val		*/
/*  Description: Redundancy mode enable bit per QM pair. bit i makes QM[i*2+1] a redundancy for QM[i*2] */
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDMC_REDUNDANT_EN__VAL__SHIFT                                  (0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDMC_REDUNDANT_EN__VAL__WIDTH                                  (8)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDMC_REDUNDANT_EN__VAL__MASK                                   (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDMC_REDUNDANT_EN__VAL__RESET                                  (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDMC_REDUNDANT_EN__VAL__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDMC_REDUNDANT_EN__VAL__MODIFY(reg_offset, value)              \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDMC_REDUNDANT_EN__VAL__SET(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QDMC_REDUNDANT_EN__VAL__CLR(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  CFG_QSMC_REDUNDANT_EN : val		*/
/*  Description: Redundancy mode enable bit per QM pair. bit i makes QM[i*2+1] a redundancy for QM[i*2] */
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSMC_REDUNDANT_EN__VAL__SHIFT                                  (0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSMC_REDUNDANT_EN__VAL__WIDTH                                  (8)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSMC_REDUNDANT_EN__VAL__MASK                                   (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSMC_REDUNDANT_EN__VAL__RESET                                  (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSMC_REDUNDANT_EN__VAL__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSMC_REDUNDANT_EN__VAL__MODIFY(reg_offset, value)              \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSMC_REDUNDANT_EN__VAL__SET(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_QSMC_REDUNDANT_EN__VAL__CLR(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDC_REDUNDANT_ASF_INT_MASK : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_MASK__VAL__SHIFT                            (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_MASK__VAL__WIDTH                            (8)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_MASK__VAL__MASK                             (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_MASK__VAL__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_MASK__VAL__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_MASK__VAL__MODIFY(reg_offset, value)        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_MASK__VAL__SET(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_MASK__VAL__CLR(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDC_REDUNDANT_ASF_INT_STATUS : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_STATUS__VAL__SHIFT                          (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_STATUS__VAL__WIDTH                          (8)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_STATUS__VAL__MASK                           (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_STATUS__VAL__RESET                          (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_STATUS__VAL__READ(reg_offset)               \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  QDDC_REDUNDANT_ASF_INT_W1C : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1C__VAL__SHIFT                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1C__VAL__WIDTH                             (8)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1C__VAL__MASK                              (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1C__VAL__RESET                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1C__VAL__READ(reg_offset)                  \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1C__VAL__MODIFY(reg_offset, value)         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1C__VAL__SET(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1C__VAL__CLR(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDC_REDUNDANT_ASF_INT_W1S : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1S__VAL__SHIFT                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1S__VAL__WIDTH                             (8)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1S__VAL__MASK                              (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1S__VAL__RESET                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1S__VAL__READ(reg_offset)                  \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1S__VAL__MODIFY(reg_offset, value)         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1S__VAL__SET(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_REDUNDANT_ASF_INT_W1S__VAL__CLR(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDC_REDUNDANT_ASF_INT_MASK : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_MASK__VAL__SHIFT                            (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_MASK__VAL__WIDTH                            (8)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_MASK__VAL__MASK                             (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_MASK__VAL__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_MASK__VAL__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_MASK__VAL__MODIFY(reg_offset, value)        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_MASK__VAL__SET(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_MASK__VAL__CLR(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDC_REDUNDANT_ASF_INT_STATUS : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_STATUS__VAL__SHIFT                          (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_STATUS__VAL__WIDTH                          (8)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_STATUS__VAL__MASK                           (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_STATUS__VAL__RESET                          (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_STATUS__VAL__READ(reg_offset)               \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  QSDC_REDUNDANT_ASF_INT_W1C : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1C__VAL__SHIFT                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1C__VAL__WIDTH                             (8)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1C__VAL__MASK                              (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1C__VAL__RESET                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1C__VAL__READ(reg_offset)                  \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1C__VAL__MODIFY(reg_offset, value)         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1C__VAL__SET(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1C__VAL__CLR(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDC_REDUNDANT_ASF_INT_W1S : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1S__VAL__SHIFT                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1S__VAL__WIDTH                             (8)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1S__VAL__MASK                              (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1S__VAL__RESET                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1S__VAL__READ(reg_offset)                  \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1S__VAL__MODIFY(reg_offset, value)         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1S__VAL__SET(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_REDUNDANT_ASF_INT_W1S__VAL__CLR(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMC_REDUNDANT_ASF_INT_MASK : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_MASK__VAL__SHIFT                            (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_MASK__VAL__WIDTH                            (8)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_MASK__VAL__MASK                             (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_MASK__VAL__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_MASK__VAL__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_MASK__VAL__MODIFY(reg_offset, value)        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_MASK__VAL__SET(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_MASK__VAL__CLR(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMC_REDUNDANT_ASF_INT_STATUS : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_STATUS__VAL__SHIFT                          (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_STATUS__VAL__WIDTH                          (8)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_STATUS__VAL__MASK                           (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_STATUS__VAL__RESET                          (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_STATUS__VAL__READ(reg_offset)               \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  QDMC_REDUNDANT_ASF_INT_W1C : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1C__VAL__SHIFT                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1C__VAL__WIDTH                             (8)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1C__VAL__MASK                              (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1C__VAL__RESET                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1C__VAL__READ(reg_offset)                  \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1C__VAL__MODIFY(reg_offset, value)         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1C__VAL__SET(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1C__VAL__CLR(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMC_REDUNDANT_ASF_INT_W1S : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1S__VAL__SHIFT                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1S__VAL__WIDTH                             (8)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1S__VAL__MASK                              (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1S__VAL__RESET                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1S__VAL__READ(reg_offset)                  \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1S__VAL__MODIFY(reg_offset, value)         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1S__VAL__SET(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_REDUNDANT_ASF_INT_W1S__VAL__CLR(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMC_REDUNDANT_ASF_INT_MASK : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_MASK__VAL__SHIFT                            (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_MASK__VAL__WIDTH                            (8)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_MASK__VAL__MASK                             (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_MASK__VAL__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_MASK__VAL__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_MASK__VAL__MODIFY(reg_offset, value)        \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_MASK__VAL__SET(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_MASK__VAL__CLR(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMC_REDUNDANT_ASF_INT_STATUS : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_STATUS__VAL__SHIFT                          (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_STATUS__VAL__WIDTH                          (8)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_STATUS__VAL__MASK                           (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_STATUS__VAL__RESET                          (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_STATUS__VAL__READ(reg_offset)               \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  QSMC_REDUNDANT_ASF_INT_W1C : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1C__VAL__SHIFT                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1C__VAL__WIDTH                             (8)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1C__VAL__MASK                              (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1C__VAL__RESET                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1C__VAL__READ(reg_offset)                  \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1C__VAL__MODIFY(reg_offset, value)         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1C__VAL__SET(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1C__VAL__CLR(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMC_REDUNDANT_ASF_INT_W1S : val		*/
/*  Description: Redundancy mode compare mismatch for QM pair i */
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1S__VAL__SHIFT                             (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1S__VAL__WIDTH                             (8)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1S__VAL__MASK                              (0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1S__VAL__RESET                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1S__VAL__READ(reg_offset)                  \
			(((uint32_t)(reg_offset) & 0x000000FFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1S__VAL__MODIFY(reg_offset, value)         \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | (((uint32_t)(value) << 0) & 0x000000FFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1S__VAL__SET(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL) | 0x000000FFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_REDUNDANT_ASF_INT_W1S__VAL__CLR(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x000000FFL))

/*----------------------------------------------------------------------------------------------------*/
/*  PRIOISLP : val		*/
/*  Description: Indicates channel priority is low priority. */
#define DRAM_DMA_ENGINE_CONFIG__PRIOISLP__VAL__SHIFT                                               (0)
#define DRAM_DMA_ENGINE_CONFIG__PRIOISLP__VAL__WIDTH                                               (32)
#define DRAM_DMA_ENGINE_CONFIG__PRIOISLP__VAL__MASK                                                (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__PRIOISLP__VAL__RESET                                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__PRIOISLP__VAL__READ(reg_offset)                                    \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__PRIOISLP__VAL__MODIFY(reg_offset, value)                           \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | (((uint32_t)(value) << 0) & 0xFFFFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__PRIOISLP__VAL__SET(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL) | 0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__PRIOISLP__VAL__CLR(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0xFFFFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  READLPTOQOSVALUE : val		*/
/*  Description: The QOS toward DDR-AXI master for low priority read. */
#define DRAM_DMA_ENGINE_CONFIG__READLPTOQOSVALUE__VAL__SHIFT                                       (0)
#define DRAM_DMA_ENGINE_CONFIG__READLPTOQOSVALUE__VAL__WIDTH                                       (3)
#define DRAM_DMA_ENGINE_CONFIG__READLPTOQOSVALUE__VAL__MASK                                        (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__READLPTOQOSVALUE__VAL__RESET                                       (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__READLPTOQOSVALUE__VAL__READ(reg_offset)                            \
			(((uint32_t)(reg_offset) & 0x00000007L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__READLPTOQOSVALUE__VAL__MODIFY(reg_offset, value)                   \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | (((uint32_t)(value) << 0) & 0x00000007L))
#define DRAM_DMA_ENGINE_CONFIG__READLPTOQOSVALUE__VAL__SET(reg_offset)                             \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | 0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__READLPTOQOSVALUE__VAL__CLR(reg_offset)                             \
			(reg_offset) = (((reg_offset) & ~0x00000007L))

/*----------------------------------------------------------------------------------------------------*/
/*  READHPTOQOSVALUE : val		*/
/*  Description: The QOS toward DDR-AXI master for high priority read. */
#define DRAM_DMA_ENGINE_CONFIG__READHPTOQOSVALUE__VAL__SHIFT                                       (0)
#define DRAM_DMA_ENGINE_CONFIG__READHPTOQOSVALUE__VAL__WIDTH                                       (3)
#define DRAM_DMA_ENGINE_CONFIG__READHPTOQOSVALUE__VAL__MASK                                        (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__READHPTOQOSVALUE__VAL__RESET                                       (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__READHPTOQOSVALUE__VAL__READ(reg_offset)                            \
			(((uint32_t)(reg_offset) & 0x00000007L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__READHPTOQOSVALUE__VAL__MODIFY(reg_offset, value)                   \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | (((uint32_t)(value) << 0) & 0x00000007L))
#define DRAM_DMA_ENGINE_CONFIG__READHPTOQOSVALUE__VAL__SET(reg_offset)                             \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | 0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__READHPTOQOSVALUE__VAL__CLR(reg_offset)                             \
			(reg_offset) = (((reg_offset) & ~0x00000007L))

/*----------------------------------------------------------------------------------------------------*/
/*  WRITELPTOQOSVALUE : val		*/
/*  Description: The QOS toward DDR-AXI master for low priority write. */
#define DRAM_DMA_ENGINE_CONFIG__WRITELPTOQOSVALUE__VAL__SHIFT                                      (0)
#define DRAM_DMA_ENGINE_CONFIG__WRITELPTOQOSVALUE__VAL__WIDTH                                      (3)
#define DRAM_DMA_ENGINE_CONFIG__WRITELPTOQOSVALUE__VAL__MASK                                       (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__WRITELPTOQOSVALUE__VAL__RESET                                      (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__WRITELPTOQOSVALUE__VAL__READ(reg_offset)                           \
			(((uint32_t)(reg_offset) & 0x00000007L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__WRITELPTOQOSVALUE__VAL__MODIFY(reg_offset, value)                  \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | (((uint32_t)(value) << 0) & 0x00000007L))
#define DRAM_DMA_ENGINE_CONFIG__WRITELPTOQOSVALUE__VAL__SET(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | 0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__WRITELPTOQOSVALUE__VAL__CLR(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000007L))

/*----------------------------------------------------------------------------------------------------*/
/*  WRITEHPTOQOSVALUE : val		*/
/*  Description: The QOS toward DDR-AXI master for high priority write. */
#define DRAM_DMA_ENGINE_CONFIG__WRITEHPTOQOSVALUE__VAL__SHIFT                                      (0)
#define DRAM_DMA_ENGINE_CONFIG__WRITEHPTOQOSVALUE__VAL__WIDTH                                      (3)
#define DRAM_DMA_ENGINE_CONFIG__WRITEHPTOQOSVALUE__VAL__MASK                                       (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__WRITEHPTOQOSVALUE__VAL__RESET                                      (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__WRITEHPTOQOSVALUE__VAL__READ(reg_offset)                           \
			(((uint32_t)(reg_offset) & 0x00000007L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__WRITEHPTOQOSVALUE__VAL__MODIFY(reg_offset, value)                  \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | (((uint32_t)(value) << 0) & 0x00000007L))
#define DRAM_DMA_ENGINE_CONFIG__WRITEHPTOQOSVALUE__VAL__SET(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | 0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__WRITEHPTOQOSVALUE__VAL__CLR(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000007L))

/*----------------------------------------------------------------------------------------------------*/
/*  DESCREADQOSVALUE : val		*/
/*  Description: The QOS toward DDR-desc-AXI master for read. */
#define DRAM_DMA_ENGINE_CONFIG__DESCREADQOSVALUE__VAL__SHIFT                                       (0)
#define DRAM_DMA_ENGINE_CONFIG__DESCREADQOSVALUE__VAL__WIDTH                                       (3)
#define DRAM_DMA_ENGINE_CONFIG__DESCREADQOSVALUE__VAL__MASK                                        (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__DESCREADQOSVALUE__VAL__RESET                                       (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__DESCREADQOSVALUE__VAL__READ(reg_offset)                            \
			(((uint32_t)(reg_offset) & 0x00000007L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__DESCREADQOSVALUE__VAL__MODIFY(reg_offset, value)                   \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | (((uint32_t)(value) << 0) & 0x00000007L))
#define DRAM_DMA_ENGINE_CONFIG__DESCREADQOSVALUE__VAL__SET(reg_offset)                             \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | 0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__DESCREADQOSVALUE__VAL__CLR(reg_offset)                             \
			(reg_offset) = (((reg_offset) & ~0x00000007L))

/*----------------------------------------------------------------------------------------------------*/
/*  DESCWRITEQOSVALUE : val		*/
/*  Description: The QOS toward DDR-desc-AXI master for write. */
#define DRAM_DMA_ENGINE_CONFIG__DESCWRITEQOSVALUE__VAL__SHIFT                                      (0)
#define DRAM_DMA_ENGINE_CONFIG__DESCWRITEQOSVALUE__VAL__WIDTH                                      (3)
#define DRAM_DMA_ENGINE_CONFIG__DESCWRITEQOSVALUE__VAL__MASK                                       (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__DESCWRITEQOSVALUE__VAL__RESET                                      (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__DESCWRITEQOSVALUE__VAL__READ(reg_offset)                           \
			(((uint32_t)(reg_offset) & 0x00000007L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__DESCWRITEQOSVALUE__VAL__MODIFY(reg_offset, value)                  \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | (((uint32_t)(value) << 0) & 0x00000007L))
#define DRAM_DMA_ENGINE_CONFIG__DESCWRITEQOSVALUE__VAL__SET(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000007L) | 0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__DESCWRITEQOSVALUE__VAL__CLR(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000007L))

/*----------------------------------------------------------------------------------------------------*/
/*  VDMA_ARB : prio_en		*/
/*  Description: Enable 2 level priority based channel arbitration in vDMA */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PRIO_EN__SHIFT                                           (0)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PRIO_EN__WIDTH                                           (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PRIO_EN__MASK                                            (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PRIO_EN__RESET                                           (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PRIO_EN__READ(reg_offset)                                \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PRIO_EN__MODIFY(reg_offset, value)                       \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PRIO_EN__SET(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PRIO_EN__CLR(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*  VDMA_ARB : interleave_en		*/
/*  Description: Enable arbitration order to interleave between M2D and D2M channels */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__INTERLEAVE_EN__SHIFT                                     (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__INTERLEAVE_EN__WIDTH                                     (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__INTERLEAVE_EN__MASK                                      (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__INTERLEAVE_EN__RESET                                     (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__INTERLEAVE_EN__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x00000002L) >> 1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__INTERLEAVE_EN__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | (((uint32_t)(value) << 1) & 0x00000002L))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__INTERLEAVE_EN__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(1) << 1))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__INTERLEAVE_EN__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(0) << 1))

/*  VDMA_ARB : par		*/
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PAR__SHIFT                                               (31)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PAR__WIDTH                                               (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PAR__MASK                                                (0x80000000L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PAR__RESET                                               (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PAR__READ(reg_offset)                                    \
			(((uint32_t)(reg_offset) & 0x80000000L) >> 31)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PAR__MODIFY(reg_offset, value)                           \
			(reg_offset) = (((reg_offset) & ~0x80000000L) | (((uint32_t)(value) << 31) & 0x80000000L))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PAR__SET(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0x80000000L) | ((uint32_t)(1) << 31))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_ARB__PAR__CLR(reg_offset)                                     \
			(reg_offset) = (((reg_offset) & ~0x80000000L) | ((uint32_t)(0) << 31))

/*----------------------------------------------------------------------------------------------------*/
/*  QM_CFG_CG_DELAY : val		*/
/*  Description: Clock cycles to keep clock running after enable condition is met */
#define DRAM_DMA_ENGINE_CONFIG__QM_CFG_CG_DELAY__VAL__SHIFT                                        (0)
#define DRAM_DMA_ENGINE_CONFIG__QM_CFG_CG_DELAY__VAL__WIDTH                                        (4)
#define DRAM_DMA_ENGINE_CONFIG__QM_CFG_CG_DELAY__VAL__MASK                                         (0x0000000FL)
#define DRAM_DMA_ENGINE_CONFIG__QM_CFG_CG_DELAY__VAL__RESET                                        (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__QM_CFG_CG_DELAY__VAL__READ(reg_offset)                             \
			(((uint32_t)(reg_offset) & 0x0000000FL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QM_CFG_CG_DELAY__VAL__MODIFY(reg_offset, value)                    \
			(reg_offset) = (((reg_offset) & ~0x0000000FL) | (((uint32_t)(value) << 0) & 0x0000000FL))
#define DRAM_DMA_ENGINE_CONFIG__QM_CFG_CG_DELAY__VAL__SET(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x0000000FL) | 0x0000000FL)
#define DRAM_DMA_ENGINE_CONFIG__QM_CFG_CG_DELAY__VAL__CLR(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x0000000FL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDDC_CFG_CG_BYPASS : val		*/
/*  Description: Bypass QDDC CG */
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CFG_CG_BYPASS__VAL__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CFG_CG_BYPASS__VAL__WIDTH                                     (16)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CFG_CG_BYPASS__VAL__MASK                                      (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CFG_CG_BYPASS__VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CFG_CG_BYPASS__VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CFG_CG_BYPASS__VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CFG_CG_BYPASS__VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDDC_CFG_CG_BYPASS__VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSDC_CFG_CG_BYPASS : val		*/
/*  Description: Bypass QSDC CG */
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CFG_CG_BYPASS__VAL__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CFG_CG_BYPASS__VAL__WIDTH                                     (16)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CFG_CG_BYPASS__VAL__MASK                                      (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CFG_CG_BYPASS__VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CFG_CG_BYPASS__VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CFG_CG_BYPASS__VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CFG_CG_BYPASS__VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSDC_CFG_CG_BYPASS__VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QDMC_CFG_CG_BYPASS : val		*/
/*  Description: Bypass QDMC CG */
#define DRAM_DMA_ENGINE_CONFIG__QDMC_CFG_CG_BYPASS__VAL__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_CFG_CG_BYPASS__VAL__WIDTH                                     (16)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_CFG_CG_BYPASS__VAL__MASK                                      (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_CFG_CG_BYPASS__VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_CFG_CG_BYPASS__VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_CFG_CG_BYPASS__VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QDMC_CFG_CG_BYPASS__VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QDMC_CFG_CG_BYPASS__VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  QSMC_CFG_CG_BYPASS : val		*/
/*  Description: Bypass QSMC CG */
#define DRAM_DMA_ENGINE_CONFIG__QSMC_CFG_CG_BYPASS__VAL__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_CFG_CG_BYPASS__VAL__WIDTH                                     (16)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_CFG_CG_BYPASS__VAL__MASK                                      (0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_CFG_CG_BYPASS__VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_CFG_CG_BYPASS__VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x0000FFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_CFG_CG_BYPASS__VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | (((uint32_t)(value) << 0) & 0x0000FFFFL))
#define DRAM_DMA_ENGINE_CONFIG__QSMC_CFG_CG_BYPASS__VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL) | 0x0000FFFFL)
#define DRAM_DMA_ENGINE_CONFIG__QSMC_CFG_CG_BYPASS__VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x0000FFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_ASF_INT_MASK : parity_error_in_regfile		*/
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_MASK__PARITY_ERROR_IN_REGFILE__SHIFT                (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_MASK__PARITY_ERROR_IN_REGFILE__WIDTH                (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_MASK__PARITY_ERROR_IN_REGFILE__MASK                 (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_MASK__PARITY_ERROR_IN_REGFILE__RESET                (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_MASK__PARITY_ERROR_IN_REGFILE__READ(reg_offset)     \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_MASK__PARITY_ERROR_IN_REGFILE__MODIFY(reg_offset, value) \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_MASK__PARITY_ERROR_IN_REGFILE__SET(reg_offset)      \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_MASK__PARITY_ERROR_IN_REGFILE__CLR(reg_offset)      \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_ASF_INT_STATUS : parity_error_in_regfile		*/
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_STATUS__PARITY_ERROR_IN_REGFILE__SHIFT              (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_STATUS__PARITY_ERROR_IN_REGFILE__WIDTH              (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_STATUS__PARITY_ERROR_IN_REGFILE__MASK               (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_STATUS__PARITY_ERROR_IN_REGFILE__RESET              (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_STATUS__PARITY_ERROR_IN_REGFILE__READ(reg_offset)   \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_ASF_INT_W1C : parity_error_in_regfile		*/
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1C__PARITY_ERROR_IN_REGFILE__SHIFT                 (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1C__PARITY_ERROR_IN_REGFILE__WIDTH                 (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1C__PARITY_ERROR_IN_REGFILE__MASK                  (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1C__PARITY_ERROR_IN_REGFILE__RESET                 (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1C__PARITY_ERROR_IN_REGFILE__READ(reg_offset)      \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1C__PARITY_ERROR_IN_REGFILE__MODIFY(reg_offset, value) \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1C__PARITY_ERROR_IN_REGFILE__SET(reg_offset)       \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1C__PARITY_ERROR_IN_REGFILE__CLR(reg_offset)       \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_ASF_INT_W1S : parity_error_in_regfile		*/
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1S__PARITY_ERROR_IN_REGFILE__SHIFT                 (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1S__PARITY_ERROR_IN_REGFILE__WIDTH                 (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1S__PARITY_ERROR_IN_REGFILE__MASK                  (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1S__PARITY_ERROR_IN_REGFILE__RESET                 (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1S__PARITY_ERROR_IN_REGFILE__READ(reg_offset)      \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1S__PARITY_ERROR_IN_REGFILE__MODIFY(reg_offset, value) \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1S__PARITY_ERROR_IN_REGFILE__SET(reg_offset)       \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_ASF_INT_W1S__PARITY_ERROR_IN_REGFILE__CLR(reg_offset)       \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  ENGINE_RW_PARITY_BIST_MODE : val		*/
/*  Description: write 1 if want to work in rw_parity bist mode in which the parity bit is written by APB wdata and not from HW calculation */
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_RW_PARITY_BIST_MODE__VAL__SHIFT                             (0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_RW_PARITY_BIST_MODE__VAL__WIDTH                             (1)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_RW_PARITY_BIST_MODE__VAL__MASK                              (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_RW_PARITY_BIST_MODE__VAL__RESET                             (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_RW_PARITY_BIST_MODE__VAL__READ(reg_offset)                  \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_RW_PARITY_BIST_MODE__VAL__MODIFY(reg_offset, value)         \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_RW_PARITY_BIST_MODE__VAL__SET(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__ENGINE_RW_PARITY_BIST_MODE__VAL__CLR(reg_offset)                   \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*----------------------------------------------------------------------------------------------------*/
/*  VDMA_STOP_LP : dis		*/
/*  Description: Write 1 if want to disable LP Stop feature */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__DIS__SHIFT                                           (0)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__DIS__WIDTH                                           (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__DIS__MASK                                            (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__DIS__RESET                                           (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__DIS__READ(reg_offset)                                \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__DIS__MODIFY(reg_offset, value)                       \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__DIS__SET(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__DIS__CLR(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*  VDMA_STOP_LP : force_val		*/
/*  Description: Force Stop LP state when feature is enabled */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__FORCE_VAL__SHIFT                                     (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__FORCE_VAL__WIDTH                                     (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__FORCE_VAL__MASK                                      (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__FORCE_VAL__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__FORCE_VAL__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x00000002L) >> 1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__FORCE_VAL__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | (((uint32_t)(value) << 1) & 0x00000002L))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__FORCE_VAL__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(1) << 1))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_STOP_LP__FORCE_VAL__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(0) << 1))

/*----------------------------------------------------------------------------------------------------*/
/*  VDMA_SCH : stop_th		*/
/*  Description: Stop scheduling for this many cycles after each successful allocation */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_TH__SHIFT                                           (0)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_TH__WIDTH                                           (7)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_TH__MASK                                            (0x0000007FL)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_TH__RESET                                           (0x00000007L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_TH__READ(reg_offset)                                \
			(((uint32_t)(reg_offset) & 0x0000007FL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_TH__MODIFY(reg_offset, value)                       \
			(reg_offset) = (((reg_offset) & ~0x0000007FL) | (((uint32_t)(value) << 0) & 0x0000007FL))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_TH__SET(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x0000007FL) | 0x0000007FL)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_TH__CLR(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x0000007FL))

/*  VDMA_SCH : stop_en		*/
/*  Description: Enable periodic scheduling stopping mechanism */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_EN__SHIFT                                           (7)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_EN__WIDTH                                           (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_EN__MASK                                            (0x00000080L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_EN__RESET                                           (0x00000080L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_EN__READ(reg_offset)                                \
			(((uint32_t)(reg_offset) & 0x00000080L) >> 7)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_EN__MODIFY(reg_offset, value)                       \
			(reg_offset) = (((reg_offset) & ~0x00000080L) | (((uint32_t)(value) << 7) & 0x00000080L))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_EN__SET(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x00000080L) | ((uint32_t)(1) << 7))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__STOP_EN__CLR(reg_offset)                                 \
			(reg_offset) = (((reg_offset) & ~0x00000080L) | ((uint32_t)(0) << 7))

/*  VDMA_SCH : tsf24_mode		*/
/*  Description: Apply fix to increase maximum transfers to 24 */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF24_MODE__SHIFT                                        (8)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF24_MODE__WIDTH                                        (1)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF24_MODE__MASK                                         (0x00000100L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF24_MODE__RESET                                        (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF24_MODE__READ(reg_offset)                             \
			(((uint32_t)(reg_offset) & 0x00000100L) >> 8)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF24_MODE__MODIFY(reg_offset, value)                    \
			(reg_offset) = (((reg_offset) & ~0x00000100L) | (((uint32_t)(value) << 8) & 0x00000100L))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF24_MODE__SET(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x00000100L) | ((uint32_t)(1) << 8))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF24_MODE__CLR(reg_offset)                              \
			(reg_offset) = (((reg_offset) & ~0x00000100L) | ((uint32_t)(0) << 8))

/*  VDMA_SCH : tsf_af_threshold		*/
/*  Description: Almost Full at 13 allocated TSF (12+8=20). In tsf24_mode should be set to 12. */
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF_AF_THRESHOLD__SHIFT                                  (9)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF_AF_THRESHOLD__WIDTH                                  (5)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF_AF_THRESHOLD__MASK                                   (0x00003E00L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF_AF_THRESHOLD__RESET                                  (0x00002800L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF_AF_THRESHOLD__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0x00003E00L) >> 9)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF_AF_THRESHOLD__MODIFY(reg_offset, value)              \
			(reg_offset) = (((reg_offset) & ~0x00003E00L) | (((uint32_t)(value) << 9) & 0x00003E00L))
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF_AF_THRESHOLD__SET(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x00003E00L) | 0x00003E00L)
#define DRAM_DMA_ENGINE_CONFIG__VDMA_SCH__TSF_AF_THRESHOLD__CLR(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x00003E00L))

/*----------------------------------------------------------------------------------------------------*/
/*  CFG_SRC_DESC_TRACE : en		*/
/*  Description: Enable tracing of descriptors read from Source QMs */
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__EN__SHIFT                                      (0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__EN__WIDTH                                      (1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__EN__MASK                                       (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__EN__RESET                                      (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__EN__READ(reg_offset)                           \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__EN__MODIFY(reg_offset, value)                  \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__EN__SET(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__EN__CLR(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*  CFG_SRC_DESC_TRACE : stop_on_wrap		*/
/*  Description: Stop when reaching end of tracing buffer */
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__STOP_ON_WRAP__SHIFT                            (1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__STOP_ON_WRAP__WIDTH                            (1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__STOP_ON_WRAP__MASK                             (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__STOP_ON_WRAP__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__STOP_ON_WRAP__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x00000002L) >> 1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__STOP_ON_WRAP__MODIFY(reg_offset, value)        \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | (((uint32_t)(value) << 1) & 0x00000002L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__STOP_ON_WRAP__SET(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(1) << 1))
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__STOP_ON_WRAP__CLR(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(0) << 1))

/*  CFG_SRC_DESC_TRACE : mprot		*/
/*  Description: AWPROT value */
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MPROT__SHIFT                                   (2)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MPROT__WIDTH                                   (3)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MPROT__MASK                                    (0x0000001CL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MPROT__RESET                                   (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MPROT__READ(reg_offset)                        \
			(((uint32_t)(reg_offset) & 0x0000001CL) >> 2)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MPROT__MODIFY(reg_offset, value)               \
			(reg_offset) = (((reg_offset) & ~0x0000001CL) | (((uint32_t)(value) << 2) & 0x0000001CL))
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MPROT__SET(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x0000001CL) | 0x0000001CL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MPROT__CLR(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x0000001CL))

/*  CFG_SRC_DESC_TRACE : mcache		*/
/*  Description: AWCACHE value */
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MCACHE__SHIFT                                  (5)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MCACHE__WIDTH                                  (4)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MCACHE__MASK                                   (0x000001E0L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MCACHE__RESET                                  (0x00000020L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MCACHE__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0x000001E0L) >> 5)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MCACHE__MODIFY(reg_offset, value)              \
			(reg_offset) = (((reg_offset) & ~0x000001E0L) | (((uint32_t)(value) << 5) & 0x000001E0L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MCACHE__SET(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000001E0L) | 0x000001E0L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__MCACHE__CLR(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000001E0L))

/*  CFG_SRC_DESC_TRACE : buff_size_m1		*/
/*  Description: Buffer size minus 1 in 16B descriptors */
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__BUFF_SIZE_M1__SHIFT                            (16)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__BUFF_SIZE_M1__WIDTH                            (16)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__BUFF_SIZE_M1__MASK                             (0xFFFF0000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__BUFF_SIZE_M1__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__BUFF_SIZE_M1__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0xFFFF0000L) >> 16)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__BUFF_SIZE_M1__MODIFY(reg_offset, value)        \
			(reg_offset) = (((reg_offset) & ~0xFFFF0000L) | (((uint32_t)(value) << 16) & 0xFFFF0000L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__BUFF_SIZE_M1__SET(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0xFFFF0000L) | 0xFFFF0000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE__BUFF_SIZE_M1__CLR(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0xFFFF0000L))

/*----------------------------------------------------------------------------------------------------*/
/*  CFG_SRC_DESC_TRACE_BASE_ADDR : base_addr		*/
/*  Description: Buffer base address bits 34:4 aligned to 16B */
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE_BASE_ADDR__BASE_ADDR__SHIFT                     (0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE_BASE_ADDR__BASE_ADDR__WIDTH                     (31)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE_BASE_ADDR__BASE_ADDR__MASK                      (0x7FFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE_BASE_ADDR__BASE_ADDR__RESET                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE_BASE_ADDR__BASE_ADDR__READ(reg_offset)          \
			(((uint32_t)(reg_offset) & 0x7FFFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE_BASE_ADDR__BASE_ADDR__MODIFY(reg_offset, value) \
			(reg_offset) = (((reg_offset) & ~0x7FFFFFFFL) | (((uint32_t)(value) << 0) & 0x7FFFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE_BASE_ADDR__BASE_ADDR__SET(reg_offset)           \
			(reg_offset) = (((reg_offset) & ~0x7FFFFFFFL) | 0x7FFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_SRC_DESC_TRACE_BASE_ADDR__BASE_ADDR__CLR(reg_offset)           \
			(reg_offset) = (((reg_offset) & ~0x7FFFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  CFG_DST_DESC_TRACE : en		*/
/*  Description: Enable tracing of descriptors read from Source QMs */
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__EN__SHIFT                                      (0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__EN__WIDTH                                      (1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__EN__MASK                                       (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__EN__RESET                                      (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__EN__READ(reg_offset)                           \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__EN__MODIFY(reg_offset, value)                  \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__EN__SET(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__EN__CLR(reg_offset)                            \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*  CFG_DST_DESC_TRACE : stop_on_wrap		*/
/*  Description: Stop when reaching end of tracing buffer */
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__STOP_ON_WRAP__SHIFT                            (1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__STOP_ON_WRAP__WIDTH                            (1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__STOP_ON_WRAP__MASK                             (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__STOP_ON_WRAP__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__STOP_ON_WRAP__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0x00000002L) >> 1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__STOP_ON_WRAP__MODIFY(reg_offset, value)        \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | (((uint32_t)(value) << 1) & 0x00000002L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__STOP_ON_WRAP__SET(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(1) << 1))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__STOP_ON_WRAP__CLR(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(0) << 1))

/*  CFG_DST_DESC_TRACE : mprot		*/
/*  Description: AWPROT value */
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MPROT__SHIFT                                   (2)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MPROT__WIDTH                                   (3)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MPROT__MASK                                    (0x0000001CL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MPROT__RESET                                   (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MPROT__READ(reg_offset)                        \
			(((uint32_t)(reg_offset) & 0x0000001CL) >> 2)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MPROT__MODIFY(reg_offset, value)               \
			(reg_offset) = (((reg_offset) & ~0x0000001CL) | (((uint32_t)(value) << 2) & 0x0000001CL))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MPROT__SET(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x0000001CL) | 0x0000001CL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MPROT__CLR(reg_offset)                         \
			(reg_offset) = (((reg_offset) & ~0x0000001CL))

/*  CFG_DST_DESC_TRACE : mcache		*/
/*  Description: AWCACHE value. MER-3804 ECO: Note that bit 3 is double booked for timeout ExtRef default value which needs to be 1. In case debug tracing is enabled */
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MCACHE__SHIFT                                  (5)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MCACHE__WIDTH                                  (4)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MCACHE__MASK                                   (0x000001E0L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MCACHE__RESET                                  (0x00000120L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MCACHE__READ(reg_offset)                       \
			(((uint32_t)(reg_offset) & 0x000001E0L) >> 5)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MCACHE__MODIFY(reg_offset, value)              \
			(reg_offset) = (((reg_offset) & ~0x000001E0L) | (((uint32_t)(value) << 5) & 0x000001E0L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MCACHE__SET(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000001E0L) | 0x000001E0L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__MCACHE__CLR(reg_offset)                        \
			(reg_offset) = (((reg_offset) & ~0x000001E0L))

/*  CFG_DST_DESC_TRACE : buff_size_m1		*/
/*  Description: Buffer size minus 1 in 16B descriptors */
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__BUFF_SIZE_M1__SHIFT                            (16)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__BUFF_SIZE_M1__WIDTH                            (16)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__BUFF_SIZE_M1__MASK                             (0xFFFF0000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__BUFF_SIZE_M1__RESET                            (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__BUFF_SIZE_M1__READ(reg_offset)                 \
			(((uint32_t)(reg_offset) & 0xFFFF0000L) >> 16)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__BUFF_SIZE_M1__MODIFY(reg_offset, value)        \
			(reg_offset) = (((reg_offset) & ~0xFFFF0000L) | (((uint32_t)(value) << 16) & 0xFFFF0000L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__BUFF_SIZE_M1__SET(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0xFFFF0000L) | 0xFFFF0000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE__BUFF_SIZE_M1__CLR(reg_offset)                  \
			(reg_offset) = (((reg_offset) & ~0xFFFF0000L))

/*----------------------------------------------------------------------------------------------------*/
/*  CFG_DST_DESC_TRACE_BASE_ADDR : base_addr		*/
/*  Description: Buffer base address bits 34:4 aligned to 16B. MER-3804 ECO: Note that bits 17:16 are double booked for timeout ExtRef mux. In case debug tracing and ExtRef are required to be turned on this constrain the base address bits 17:16 to be the same as the timestamp mux */
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE_BASE_ADDR__BASE_ADDR__SHIFT                     (0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE_BASE_ADDR__BASE_ADDR__WIDTH                     (31)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE_BASE_ADDR__BASE_ADDR__MASK                      (0x7FFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE_BASE_ADDR__BASE_ADDR__RESET                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE_BASE_ADDR__BASE_ADDR__READ(reg_offset)          \
			(((uint32_t)(reg_offset) & 0x7FFFFFFFL) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE_BASE_ADDR__BASE_ADDR__MODIFY(reg_offset, value) \
			(reg_offset) = (((reg_offset) & ~0x7FFFFFFFL) | (((uint32_t)(value) << 0) & 0x7FFFFFFFL))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE_BASE_ADDR__BASE_ADDR__SET(reg_offset)           \
			(reg_offset) = (((reg_offset) & ~0x7FFFFFFFL) | 0x7FFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DST_DESC_TRACE_BASE_ADDR__BASE_ADDR__CLR(reg_offset)           \
			(reg_offset) = (((reg_offset) & ~0x7FFFFFFFL))

/*----------------------------------------------------------------------------------------------------*/
/*  CFG_DEBUG_TIMESTAMP : en		*/
/*  Description: Write 1 to enable timestamp counter for debug logic */
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__EN__SHIFT                                     (0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__EN__WIDTH                                     (1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__EN__MASK                                      (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__EN__RESET                                     (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__EN__READ(reg_offset)                          \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__EN__MODIFY(reg_offset, value)                 \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__EN__SET(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__EN__CLR(reg_offset)                           \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))

/*  CFG_DEBUG_TIMESTAMP : clr		*/
/*  Description: Write 1 to clear timestamp counter. After writing 1 to this field need to write 0 immediately */
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__CLR__SHIFT                                    (1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__CLR__WIDTH                                    (1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__CLR__MASK                                     (0x00000002L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__CLR__RESET                                    (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__CLR__READ(reg_offset)                         \
			(((uint32_t)(reg_offset) & 0x00000002L) >> 1)
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__CLR__MODIFY(reg_offset, value)                \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | (((uint32_t)(value) << 1) & 0x00000002L))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__CLR__SET(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(1) << 1))
#define DRAM_DMA_ENGINE_CONFIG__CFG_DEBUG_TIMESTAMP__CLR__CLR(reg_offset)                          \
			(reg_offset) = (((reg_offset) & ~0x00000002L) | ((uint32_t)(0) << 1))

/*----------------------------------------------------------------------------------------------------*/
/*  DEBUG_TIMESTAMP : val		*/
#define DRAM_DMA_ENGINE_CONFIG__DEBUG_TIMESTAMP__VAL__SHIFT                                        (0)
#define DRAM_DMA_ENGINE_CONFIG__DEBUG_TIMESTAMP__VAL__WIDTH                                        (32)
#define DRAM_DMA_ENGINE_CONFIG__DEBUG_TIMESTAMP__VAL__MASK                                         (0xFFFFFFFFL)
#define DRAM_DMA_ENGINE_CONFIG__DEBUG_TIMESTAMP__VAL__RESET                                        (0x00000000L)
#define DRAM_DMA_ENGINE_CONFIG__DEBUG_TIMESTAMP__VAL__READ(reg_offset)                             \
			(((uint32_t)(reg_offset) & 0xFFFFFFFFL) >> 0)

/*----------------------------------------------------------------------------------------------------*/
/*  AUTO_ADDRESS_ERR_CB_INDICATION : enable		*/
/*  Description: default is 1, meaning the address error is enabled, to hide the address error indication, set to 0 */
#define DRAM_DMA_ENGINE_CONFIG__AUTO_ADDRESS_ERR_CB_INDICATION__ENABLE__SHIFT                      (0)
#define DRAM_DMA_ENGINE_CONFIG__AUTO_ADDRESS_ERR_CB_INDICATION__ENABLE__WIDTH                      (1)
#define DRAM_DMA_ENGINE_CONFIG__AUTO_ADDRESS_ERR_CB_INDICATION__ENABLE__MASK                       (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__AUTO_ADDRESS_ERR_CB_INDICATION__ENABLE__RESET                      (0x00000001L)
#define DRAM_DMA_ENGINE_CONFIG__AUTO_ADDRESS_ERR_CB_INDICATION__ENABLE__READ(reg_offset)           \
			(((uint32_t)(reg_offset) & 0x00000001L) >> 0)
#define DRAM_DMA_ENGINE_CONFIG__AUTO_ADDRESS_ERR_CB_INDICATION__ENABLE__MODIFY(reg_offset, value)  \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | (((uint32_t)(value) << 0) & 0x00000001L))
#define DRAM_DMA_ENGINE_CONFIG__AUTO_ADDRESS_ERR_CB_INDICATION__ENABLE__SET(reg_offset)            \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(1) << 0))
#define DRAM_DMA_ENGINE_CONFIG__AUTO_ADDRESS_ERR_CB_INDICATION__ENABLE__CLR(reg_offset)            \
			(reg_offset) = (((reg_offset) & ~0x00000001L) | ((uint32_t)(0) << 0))


#endif /* DRAM_DMA_ENGINE_CONFIG_MACRO_H */
