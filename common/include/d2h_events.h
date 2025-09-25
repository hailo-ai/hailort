/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file d2h_events.h
 * @brief Declares all d2h event manager structures relevant in the host.
**/

#ifndef __D2H_EVENTS_H__
#define __D2H_EVENTS_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "status.h"
#include "stdfloat.h"

#pragma pack(push, 1)

/**
 *  @brief The d2h event manager structures relevant in the host
 */

typedef enum {
    D2H_EVENT_PRIORITY_INFO = 0,
    D2H_EVENT_PRIORITY_CRITICAL,

    /* Must be last */
    D2H_EVENT_PRIORITY_COUNT
} D2H_EVENT_PRIORITY_t;

typedef enum {
    D2H_EVENT_COMMUNICATION_TYPE_UDP = 0,
    D2H_EVENT_COMMUNICATION_TYPE_VDMA,
    D2H_EVENT_COMMUNICATION_TYPE__COUNT
} D2H_EVENT_COMMUNICATION_TYPE_t;

typedef struct {
    uint32_t version;
    uint32_t sequence;
    uint32_t priority;
    uint32_t module_id;
    uint32_t event_id;
    uint32_t parameter_count;
    uint32_t payload_length;
} D2H_EVENT_HEADER_t;

/* D2H_EVENT_ID_t Should be in the same order as the structs in D2H_EVENT__message_parameters_t union, since the host will parse according to this enum */
/* For example ETHERNET_SERVICE_RX_ERROR_EVENT_ID is 0, so D2H_EVENT_rx_error_event_message_t is the first struct in D2H_EVENT__message_parameters_t */
typedef enum {
    ETHERNET_SERVICE_RX_ERROR_EVENT_ID = 0,
    D2H_HOST_INFO_EVENT_ID,
    HEALTH_MONITOR_TEMPERATURE_ALARM_D2H_EVENT_ID,
    HEALTH_MONITOR_CLOSED_STREAMS_D2H_EVENT_ID,
    HEALTH_MONITOR_OVERCURRENT_PROTECTION_ALERT_EVENT_ID,
    HEALTH_MONITOR_LCU_ECC_CORRECTABLE_EVENT_ID,
    HEALTH_MONITOR_LCU_ECC_UNCORRECTABLE_EVENT_ID,
    HEALTH_MONITOR_CPU_ECC_ERROR_EVENT_ID,
    HEALTH_MONITOR_CPU_ECC_FATAL_EVENT_ID,
    CONTEXT_SWITCH_BREAKPOINT_REACHED,
    HEALTH_MONITOR_CLOCK_CHANGED_EVENT_ID,
    HW_INFER_MANAGER_INFER_DONE,
    CONTEXT_SWITCH_RUN_TIME_ERROR,
    START_UPDATE_CACHE_OFFSET_ID,

    D2H_EVENT_ID_COUNT /* Must be last*/
} D2H_EVENT_ID_t;

/* D2H_EVENT_rx_error_event_message_t should be the same as hailo_rx_error_notification_message_t */
typedef struct {
    uint32_t error; 
    uint32_t queue_number;
    uint32_t rx_errors_count;
} D2H_EVENT_rx_error_event_message_t;
#define D2H_EVENT_RX_ERROR_EVENT_PARAMETER_COUNT  (3)

/* D2H_EVENT_host_info_event_message_t should be the same as hailo_debug_notification_message_t */
typedef struct {
    uint32_t connection_status;
    uint32_t connection_type;
    uint32_t vdma_is_active;
    uint32_t host_port;
    uint32_t host_ip_addr;
} D2H_EVENT_host_info_event_message_t;
#define D2H_EVENT_HOST_INFO_EVENT_PARAMETER_COUNT  (5)

/* D2H_EVENT_health_monitor_closed_streams_event_message_t should be the same as hailo_health_monitor_dataflow_shutdown_notification_message_t */
typedef struct {
    float32_t ts0_temperature;
    float32_t ts1_temperature;
} D2H_EVENT_health_monitor_closed_streams_event_message_t;

#define D2H_EVENT_HEALTH_MONITOR_CLOSED_STREAMS_EVENT_PARAMETER_COUNT  (2)

/* D2H_EVENT_health_monitor_temperature_alarm_event_message_t should be the same as hailo_health_monitor_temperature_alarm_notification_message_t */
typedef struct {
    uint32_t temperature_zone;
    uint32_t alarm_ts_id;
    float32_t ts0_temperature;
    float32_t ts1_temperature;
} D2H_EVENT_health_monitor_temperature_alarm_event_message_t;

#define D2H_EVENT_HEALTH_MONITOR_TEMPERATURE_ALARM_EVENT_PARAMETER_COUNT  (4)

/* D2H_EVENT_health_monitor_overcurrent_alert_event_message_t should be the same as hailo_health_monitor_overcurrent_alert_notification_message_t */
typedef struct {
    uint32_t overcurrent_zone;
    float32_t exceeded_alert_threshold;
    bool is_last_overcurrent_violation_reached;
} D2H_EVENT_health_monitor_overcurrent_alert_event_message_t;

#define D2H_EVENT_HEALTH_MONITOR_OVERCURRENT_ALERT_EVENT_PARAMETER_COUNT  (4)

/* D2H_EVENT_health_monitor_lcu_ecc_error_event_message_t should be the same as hailo_health_monitor_lcu_ecc_error_notification_message_t */
typedef struct {
    uint16_t cluster_bitmap;
} D2H_EVENT_health_monitor_lcu_ecc_error_event_message_t;

/* D2H_EVENT_health_monitor_cpu_ecc_event_message_t should be the same as hailo_health_monitor_cpu_ecc_error_notification_message_t */
#define D2H_EVENT_HEALTH_MONITOR_LCU_ECC_ERROR_EVENT_PARAMETER_COUNT  (1)
typedef struct {
    uint32_t memory_bitmap;
} D2H_EVENT_health_monitor_cpu_ecc_event_message_t;

#define D2H_EVENT_HEALTH_MONITOR_CPU_ECC_EVENT_PARAMETER_COUNT  (1)

/* D2H_EVENT_context_switch_breakpoint_reached_event_message_t should be the same as 
 * CONTROL_PROTOCOL__context_switch_breakpoint_data_t and hailo_context_switch_breakpoint_reached_notification_message_t */
typedef struct {
    uint8_t application_index;
    uint16_t batch_index;
    uint16_t context_index;
    uint16_t action_index;
} D2H_EVENT_context_switch_breakpoint_reached_event_message_t;

#define D2H_EVENT_CONTEXT_SWITCH_BREAKPOINT_REACHED_EVENT_PARAMETER_COUNT  (4)

typedef struct {
    uint32_t previous_clock;
    uint32_t current_clock;
} D2H_EVENT_health_monitor_clock_changed_event_message_t;

#define D2H_EVENT_HEALTH_MONITOR_CLOCK_CHANGED_EVENT_PARAMETER_COUNT  (2)

typedef struct {
    uint32_t infer_cycles;
} D2H_EVENT_hw_infer_mamager_infer_done_message_t;

#define D2H_EVENT_HW_INFER_MANAGER_INFER_DONE_PARAMETER_COUNT  (1)

typedef struct {
    uint32_t exit_status;
    uint8_t application_index;
    uint16_t batch_index;
    uint16_t context_index;
    uint16_t action_index;
} D2H_EVENT_context_switch_run_time_error_event_message_t;

#define D2H_EVENT_CONTEXT_SWITCH_RUN_TIME_ERROR_EVENT_PARAMETER_COUNT  (5)

typedef struct {
    uint64_t cache_id_bitmask;
} D2H_EVENT_start_update_cache_offset_message_t;

#define D2H_EVENT_START_UPDATE_CACHE_OFFSET_PARAMETER_COUNT  (1)

/* D2H_EVENT__message_parameters_t should be in the same order as hailo_notification_message_parameters_t */
typedef union {
   D2H_EVENT_rx_error_event_message_t rx_error_event;
   D2H_EVENT_host_info_event_message_t host_info_event;
   D2H_EVENT_health_monitor_closed_streams_event_message_t health_monitor_closed_streams_event;
   D2H_EVENT_health_monitor_temperature_alarm_event_message_t health_monitor_temperature_alarm_event;
   D2H_EVENT_health_monitor_overcurrent_alert_event_message_t health_monitor_overcurrent_alert_event;
   D2H_EVENT_health_monitor_lcu_ecc_error_event_message_t health_monitor_lcu_ecc_error_event;
   D2H_EVENT_health_monitor_cpu_ecc_event_message_t health_monitor_cpu_ecc_event;
   D2H_EVENT_context_switch_breakpoint_reached_event_message_t context_switch_breakpoint_reached_event;
   D2H_EVENT_health_monitor_clock_changed_event_message_t health_monitor_clock_changed_event;
   D2H_EVENT_hw_infer_mamager_infer_done_message_t hw_infer_manager_infer_done_event;
   D2H_EVENT_context_switch_run_time_error_event_message_t context_switch_run_time_error_event;
   D2H_EVENT_start_update_cache_offset_message_t start_update_cache_offset_event;
} D2H_EVENT__message_parameters_t;

typedef struct {
    D2H_EVENT_HEADER_t header;
    D2H_EVENT__message_parameters_t message_parameters;
} D2H_EVENT_MESSAGE_t;

#define D2H_EVENT_BUFFER_NOT_IN_USE (0)
#define D2H_EVENT_BUFFER_IN_USE (1)
#define D2H_EVENT_MAX_SIZE (0x370)

typedef struct {
    uint16_t is_buffer_in_use;
    uint16_t buffer_len;
    uint8_t buffer[D2H_EVENT_MAX_SIZE];
} D2H_event_buffer_t;

#pragma pack(pop)

/**********************************************************************
 * Public Functions
 **********************************************************************/
HAILO_COMMON_STATUS_t D2H_EVENTS__parse_event(D2H_EVENT_MESSAGE_t *d2h_event_message);
#ifdef __cplusplus
}
#endif
#endif /* __D2H_EVENTS_H__ */
