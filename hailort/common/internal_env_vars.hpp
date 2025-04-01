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

/* Service, hrpc-server, comunication */

/* Changes the default address for grpc communication. used for the service-over-ip feature */
#define HAILORT_SERVICE_ADDRESS_ENV_VAR ("HAILORT_SERVICE_ADDRESS")

/* Indicates to the HailoRT gRPC Service whether to use shared memory for the tesnors data.
    Note: Cannot be used for service-over-ip */
#define HAILO_SERVICE_SHARED_MEMORY_ENV_VAR ("HAILO_SERVICE_SHARED_MEMORY_OFF")
#define HAILO_SERVICE_SHARED_MEMORY_OFF "1"

/* Forces the client to use socket-based communication on a specific address. if not set, socket communicaiton wont be used. */
#define HAILO_SOCKET_COM_ADDR_CLIENT_ENV_VAR ("HAILO_SOCKET_COM_ADDR_CLIENT")

/* Forces the hrpc-server to use socket-based communication on a specific address. if not set, socket communicaiton wont be used. */
#define HAILO_SOCKET_COM_ADDR_SERVER_ENV_VAR ("HAILO_SOCKET_COM_ADDR_SERVER")

/* Forces Hailo session based on socket to use a specific device. This env var should be set to the iface name (i.e eth0)  */
#define HAILO_SOCKET_BIND_TO_INTERFACE_ENV_VAR ("HAILO_SOCKET_BIND_TO_INTERFACE")

/* HAILO_SOCKET_COM_ADDR_CLIENT_ENV_VAR and HAILO_SOCKET_COM_ADDR_SERVER_ENV_VAR can be set to either <ip> ("X.X.X.X"),
    or to HAILO_SOCKET_COM_ADDR_UNIX_SOCKET which forces working with unix-socket*/
#define HAILO_SOCKET_COM_ADDR_UNIX_SOCKET ("localhost")

/* Overrides hRPC/gRPC requests timeout. value in seconds */
#define HAILO_REQUEST_TIMEOUT_SECONDS ("HAILO_REQUEST_TIMEOUT_SECONDS")

/* General */

/* Defines whether the offset of the kv cache will be updated automatically or not.
    can be set to either HAILORT_AUTO_UPDATE_CACHE_OFFSET_ENV_VAR_DEFAULT or
    HAILORT_AUTO_UPDATE_CACHE_OFFSET_ENV_VAR_DISABLED, or to a numeric value defining the offset update value in entries`*/
#define HAILORT_AUTO_UPDATE_CACHE_OFFSET_ENV_VAR ("HAILORT_AUTO_UPDATE_CACHE_OFFSET")
#define HAILORT_AUTO_UPDATE_CACHE_OFFSET_ENV_VAR_DEFAULT ("default")
#define HAILORT_AUTO_UPDATE_CACHE_OFFSET_ENV_VAR_DISABLED ("disabled")

/* Corresponds to CacheManager::update_cache_offset(check_snapshots, require_changes) */
#define HAILORT_CHECK_CACHE_UPDATE_ENV_VAR ("HAILORT_CHECK_CACHE_UPDATE")
#define HAILORT_REQUIRE_CACHE_CHANGES_ENV_VAR ("HAILORT_REQUIRE_CACHE_CHANGES")

/* Used for the internal CLI mode `measure-nnc-performance` */
#define HAILO_CONFIGURE_FOR_HW_INFER_ENV_VAR ("HAILO_CONFIGURE_FOR_HW_INFER")

/* Disable context switch intermediate buffer reuse (naive plan) */
#define HAILO_FORCE_NAIVE_PER_BUFFER_TYPE_ALOCATION_ENV_VAR ("HAILO_FORCE_NAIVE_PER_BUFFER_TYPE_ALOCATION")

/* forces the minimum FD used events to be above `HIGH_FD_OFFSET`.
    useful for systems with limitations on the FD count and values */
#define HAILO_USE_HIGH_FD_ENV_VAR ("HAILO_USE_HIGH_FD")

/* Force hailo15m partial cluster layout bitmap (which clusters are activated) */
#define FORCE_LAYOUT_INTERNAL_ENV_VAR ("FORCE_LAYOUT_INTERNAL")


/* Logger */

/* Forces flush of the logger to file on every trace, instead of the default (warnings and above) */
#define HAILORT_LOGGER_FLUSH_EVERY_PRINT_ENV_VAR ("HAILORT_LOGGER_FLUSH_EVERY_PRINT")

/* Force QNX Driver logs to be flushed to specific file - or if left undefined - to stderr */
#define HAILO_QNX_DRIVER_LOG_STDERR_ENV_VAR ("HAILO_QNX_DRIVER_LOG_STDERR")


/* Inference */

/* Disables the hrt-multiplexer */
#define DISABLE_MULTIPLEXER_ENV_VAR ("HAILO_DISABLE_MULTIPLEXER_INTERNAL")

/* Disable scheduler Idle optimization */
#define HAILO_DISABLE_IDLE_OPT_ENV_VAR ("HAILO_DISABLE_IDLE_OPT")


/* Model configuration */

/* If not set, hailort will try to use default desc-size, and only then fallback to larger desc-sizes */
#define HAILO_LEGACY_BOUNDARY_CHANNEL_PAGE_SIZE_ENV_VAR ("HAILO_LEGACY_BOUNDARY_CHANNEL_PAGE_SIZE")

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

/* Forces copying the hef file content to a mapped buffer before configuring it's network groups.
    When working with Hef as a file, we need this copy in order to work with the aligned ccws feature */
#define HAILO_COPY_HEF_CONTENT_TO_A_MAPPED_BUFFER_PRE_CONFIGURE_ENV_VAR ("HAILO_COPY_HEF_CONTENT_TO_A_MAPPED_BUFFER_PRE_CONFIGURE")

/* Disables the aligned ccws feature - in case this env var is set, the aligned_ccws feature won't be used.
    Instead - we will alocate aligned config buffers and will copy the CCWs to them */
#define HAILO_DISABLE_ALIGNED_CCWS_ENV_VAR ("HAILO_DISABLE_ALIGNED_CCWS")

/* Forces using descriptor-lists instead of CCB for ddr-channels on h1x devices */
#define HAILO_FORCE_DDR_CHANNEL_OVER_CCB_ENV_VAR ("HAILO_FORCE_DDR_CHANNEL_OVER_CCB")

/* Sets the default power-mode of the ConfiguredNetworkGroups to `HAILO_POWER_MODE_ULTRA_PERFORMANCE` */
#define FORCE_POWER_MODE_ULTRA_PERFORMANCE_ENV_VAR ("FORCE_POWER_MODE_ULTRA_PERFORMANCE")

/* Set HW infer Tool to use CCB for Boundary Channels*/
#define HAILO_HW_INFER_BOUNDARY_CHANNELS_OVER_CCB_ENV_VAR ("HAILO_HW_INFER_BOUNDARY_CHANNELS_OVER_CCB")

} /* namespace hailort */

#endif /* HAILO_INTERNAL_ENV_VARS_HPP_ */