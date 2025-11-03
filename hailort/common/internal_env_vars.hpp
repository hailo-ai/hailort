/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file internal_env_Vars.hpp
 * @brief: defines a set of internal environment variables used for development
 * **/

#ifndef HAILO_INTERNAL_ENV_VARS_HPP_
#define HAILO_INTERNAL_ENV_VARS_HPP_


namespace hailort
{

/* hrpc-server, communication */

/* Forces Hailo session based on socket to use a specific device. This env var should be set to the iface name (i.e eth0) */
#define HAILO_SOCKET_BIND_TO_INTERFACE_ENV_VAR ("HAILO_SOCKET_BIND_TO_INTERFACE")

/* Overrides hRPC requests timeout. value in seconds */
#define HAILO_REQUEST_TIMEOUT_SECONDS ("HAILO_REQUEST_TIMEOUT_SECONDS")

/* General */

/* Corresponds to CacheManager::update_cache_offset(check_snapshots, require_changes) */
#define HAILORT_CHECK_CACHE_UPDATE_ENV_VAR ("HAILORT_CHECK_CACHE_UPDATE")
#define HAILORT_REQUIRE_CACHE_CHANGES_ENV_VAR ("HAILORT_REQUIRE_CACHE_CHANGES")

/* Controls the number of descriptor lists in cache MultiSgEdgeLayer (set for double buffering) */
#define HAILO_CACHE_DESC_LISTS_COUNT_ENV_VAR ("HAILO_CACHE_DESC_LISTS_COUNT")

/* Used for the internal CLI mode `measure-nnc-performance` */
#define HAILO_CONFIGURE_FOR_HW_INFER_ENV_VAR ("HAILO_CONFIGURE_FOR_HW_INFER")

/* Disable context switch intermediate buffer reuse (naive plan) */
#define HAILO_FORCE_NAIVE_PER_BUFFER_TYPE_ALOCATION_ENV_VAR ("HAILO_FORCE_NAIVE_PER_BUFFER_TYPE_ALOCATION")

/* forces the minimum FD used events to be above `HIGH_FD_OFFSET`.
    useful for systems with limitations on the FD count and values */
#define HAILO_USE_HIGH_FD_ENV_VAR ("HAILO_USE_HIGH_FD")

/* Force hailo15m partial cluster layout bitmap (which clusters are activated) */
#define FORCE_LAYOUT_INTERNAL_ENV_VAR ("FORCE_LAYOUT_INTERNAL")

/* Set a custom value for dma-alignment. Default is PAGE_SIZE. */
#define HAILO_CUSTOM_DMA_ALIGNMENT_ENV_VAR ("HAILO_CUSTOM_DMA_ALIGNMENT")

/* HRTPP runtime optimizations for performance.
   HAILORT_YOLOV5_SEG_PP_CROP_OPT does an early crop of masks.
   HAILORT_YOLOV5_SEG_NN_RESIZE uses resize nearest neighbor and cancels sigmoid calc */
#define HAILORT_YOLOV5_SEG_PP_CROP_OPT_ENV_VAR ("HAILORT_YOLOV5_SEG_PP_CROP_OPT")
#define HAILORT_YOLOV5_SEG_NN_RESIZE_ENV_VAR ("HAILORT_YOLOV5_SEG_NN_RESIZE")

/* Logger */

/* Forces flush of the logger to file on every trace, instead of the default (warnings and above) */
#define HAILORT_LOGGER_FLUSH_EVERY_PRINT_ENV_VAR ("HAILORT_LOGGER_FLUSH_EVERY_PRINT")

/* Force QNX Driver logs to be flushed to specific file - or if left undefined - to stderr */
#define HAILO_QNX_DRIVER_LOG_STDERR_ENV_VAR ("HAILO_QNX_DRIVER_LOG_STDERR")

/* If set, HailoRTLogger would add a sink to syslog.
    Required for getting all relevant H10 logs in one place, including logs from other sub-systems */
#define HAILORT_LOGGER_PRINT_TO_SYSLOG_ENV_VAR ("HAILO_PRINT_TO_SYSLOG")
#define HAILORT_LOGGER_PRINT_TO_SYSLOG_ENV_VAR_VALUE ("1")


/* Inference */

/* Disables the hrt-multiplexer */
#define DISABLE_MULTIPLEXER_ENV_VAR ("HAILO_DISABLE_MULTIPLEXER_INTERNAL")

/* Disable scheduler Idle optimization */
#define HAILO_ENABLE_IDLE_OPT_ENV_VAR ("HAILO_ENABLE_IDLE_OPT")


/* Model configuration */

/* If set - Action list will be sent to Firmware SRAM over DDR unrelated to the size of the action list
    (Otherwise - DDR will only be used if infinite action list is needed) */
#define DDR_ACTION_LIST_ENV_VAR ("HAILO_DDR_ACTION_LIST")
#define DDR_ACTION_LIST_ENV_VAR_VALUE ("1")

/* Forces using descriptor-lists instead of CCB for config-channels on h1x devices */
#define HAILO_FORCE_CONF_CHANNEL_OVER_DESC_ENV_VAR ("HAILO_FORCE_CONF_CHANNEL_OVER_DESC")

/* Forces using descriptor-lists instead of CCB for inter-context-channels on h1x devices */
#define HAILO_FORCE_INFER_CONTEXT_CHANNEL_OVER_DESC_ENV_VAR ("HAILO_FORCE_INFER_CONTEXT_CHANNEL_OVER_DESC")

/* Determines the size of each mapped buffer into which the ccws section will be splitted to.
    Relevant only when the aligned_ccws feature is enbabled */
#define HAILO_ALIGNED_CCWS_MAPPED_BUFFER_SIZE_ENV_VAR ("HAILO_ALIGNED_CCWS_MAPPED_BUFFER_SIZE")
#define HAILO_ALIGNED_CCWS_MAPPED_BUFFER_SIZE (2 * 1024 * 1024)

/* Disables the aligned ccws feature - in case this env var is set, the aligned_ccws feature won't be used.
    Instead - we will alocate aligned config buffers and will copy the CCWs to them */
#define HAILO_DISABLE_ALIGNED_CCWS_ENV_VAR ("HAILO_DISABLE_ALIGNED_CCWS")

/* Forces using descriptor-lists instead of CCB for ddr-channels on h1x devices */
#define HAILO_FORCE_DDR_CHANNEL_OVER_CCB_ENV_VAR ("HAILO_FORCE_DDR_CHANNEL_OVER_CCB")

/* Sets the default power-mode of the ConfiguredNetworkGroups to `HAILO_POWER_MODE_ULTRA_PERFORMANCE` */
#define FORCE_POWER_MODE_ULTRA_PERFORMANCE_ENV_VAR ("FORCE_POWER_MODE_ULTRA_PERFORMANCE")

/* Set HW infer Tool to use CCB for Boundary Channels*/
#define HAILO_HW_INFER_BOUNDARY_CHANNELS_OVER_CCB_ENV_VAR ("HAILO_HW_INFER_BOUNDARY_CHANNELS_OVER_CCB")

/* Disables the post process operations on HEFs that have it - Hailo10 only! */
#define HAILO_DISABLE_PP_ENV_VAR ("HAILO_DISABLE_PP")

/* Sets log level for the syslog sink - relevant for H10 usage. valid values: debug, info, warning, error, critical */
#define HAILORT_SYSLOG_LOGGER_LEVEL_ENV_VAR ("HAILORT_SYSLOG_LOGGER_LEVEL")

/* Disables strict versioning check for HEFs */
#define HAILO_IGNORE_STRICT_VERSION_ENV_VAR ("HAILO_IGNORE_STRICT_VERSION")

} /* namespace hailort */

#endif /* HAILO_INTERNAL_ENV_VARS_HPP_ */