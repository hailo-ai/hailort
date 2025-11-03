/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include <stdint.h>
#include <string.h>
#include "common/utils.hpp"
#include "d2h_events.h"
#include "byte_order.h"
#include "firmware_status.h"


using namespace hailort;

/* Function prototype for control operations */
typedef HAILO_COMMON_STATUS_t (*firmware_notifications_parser_t) (D2H_EVENT_MESSAGE_t *d2h_notification_message);

#define CHECK_COMMON_STATUS(cond, ret_val, ...) \
    _CHECK((cond), (ret_val), CONSTRUCT_MSG("CHECK failed", ##__VA_ARGS__))

/**********************************************************************
 * Private Declarations
 **********************************************************************/
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_rx_error(D2H_EVENT_MESSAGE_t *d2h_notification_message) ;
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_host_info_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_temperature_alarm_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_closed_streams_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_overcurrent_alert_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_lcu_ecc_nonfatal_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_lcu_ecc_fatal_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_cpu_ecc_error_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_cpu_ecc_fatal_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_context_switch_breakpoint_reached(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_clock_changed_event_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_hw_infer_manager_infer_done_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_context_switch_run_time_error_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_nn_core_crc_error_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message);

/**********************************************************************
 * Globals
 **********************************************************************/
firmware_notifications_parser_t g_firmware_notifications_parser[D2H_EVENT_ID_COUNT] = {
    D2H_EVENTS__parse_rx_error,
    D2H_EVENTS__parse_host_info_notification,
    D2H_EVENTS__parse_health_monitor_temperature_alarm_notification,
    D2H_EVENTS__parse_health_monitor_closed_streams_notification,
    D2H_EVENTS__parse_health_monitor_overcurrent_alert_notification,
    D2H_EVENTS__parse_health_monitor_lcu_ecc_nonfatal_notification,
    D2H_EVENTS__parse_health_monitor_lcu_ecc_fatal_notification,
    D2H_EVENTS__parse_health_monitor_cpu_ecc_error_notification,
    D2H_EVENTS__parse_health_monitor_cpu_ecc_fatal_notification,
    D2H_EVENTS__parse_context_switch_breakpoint_reached,
    D2H_EVENTS__parse_health_monitor_clock_changed_event_notification,
    D2H_EVENTS__parse_hw_infer_manager_infer_done_notification,
    D2H_EVENTS__parse_context_switch_run_time_error_notification,
    D2H_EVENTS__parse_nn_core_crc_error_notification,
};
/**********************************************************************
 * Internal Functions
 **********************************************************************/
static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_rx_error(D2H_EVENT_MESSAGE_t *d2h_notification_message) 
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_RX_ERROR_EVENT_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h notification invalid parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }

    if(d2h_notification_message->header.payload_length != sizeof(d2h_notification_message->message_parameters.rx_error_event)) {
        LOGGER__ERROR("d2h notification invalid payload_length: {}", d2h_notification_message->header.payload_length);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH;
        goto l_exit;
    }
   
    LOGGER__INFO("Got Rx Error {} Event From module_id {} with error {}, queue {}",((D2H_EVENT_PRIORITY_CRITICAL == d2h_notification_message->header.priority) ?"Critical":"Info"),
    d2h_notification_message->header.module_id, d2h_notification_message->message_parameters.rx_error_event.error, d2h_notification_message->message_parameters.rx_error_event.queue_number);
    
    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_host_info_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message) 
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_HOST_INFO_EVENT_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h notification invalid parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }

    if(d2h_notification_message->header.payload_length != sizeof(d2h_notification_message->message_parameters.host_info_event)) {
        LOGGER__ERROR("d2h notification invalid payload_length: {}", d2h_notification_message->header.payload_length);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH;
        goto l_exit;
    }
   
    LOGGER__INFO("Got host config {} Event From module_id {} with connection type {}",((D2H_EVENT_PRIORITY_CRITICAL == d2h_notification_message->header.priority) ?"Critical":"Info"),
    d2h_notification_message->header.module_id, ((D2H_EVENT_COMMUNICATION_TYPE_UDP == d2h_notification_message->message_parameters.host_info_event.connection_type) ?"UDP":"PCIe"));
    
    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_temperature_alarm_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message) 
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_HEALTH_MONITOR_TEMPERATURE_ALARM_EVENT_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h notification invalid parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }

    switch (d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.temperature_zone) {
        case HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__GREEN:
            LOGGER__INFO("Got health monitor notification - temperature reached green zone. sensor id={}, TS00={}c, TS01={}c", 
                            d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.alarm_ts_id,
                            d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.ts0_temperature,
                            d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.ts1_temperature);
            break;

        case HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__ORANGE:
            LOGGER__WARNING("Got health monitor notification - temperature reached orange zone. sensor id={}, TS00={}c, TS01={}c", 
                                d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.alarm_ts_id,
                                d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.ts0_temperature,
                                d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.ts1_temperature);
            break;

        case HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__RED:
            LOGGER__CRITICAL("Got health monitor notification - temperature reached red zone. sensor id={}, TS00={}c, TS01={}c", 
                                d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.alarm_ts_id,
                                d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.ts0_temperature,
                                d2h_notification_message->message_parameters.health_monitor_temperature_alarm_event.ts1_temperature);
            break;

        default:
            LOGGER__ERROR("Got invalid health monitor notification - temperature zone could not be parsed.");
            status = HAILO_STATUS__D2H_EVENTS__INVALID_ARGUMENT;
            goto l_exit;
    }

    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_clock_changed_event_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_HEALTH_MONITOR_CLOCK_CHANGED_EVENT_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h notification invalid parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }
    LOGGER__WARNING("Got health monitor notification - System's clock has been changed from {} to {}",
                        d2h_notification_message->message_parameters.health_monitor_clock_changed_event.previous_clock,
                        d2h_notification_message->message_parameters.health_monitor_clock_changed_event.current_clock);

    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_hw_infer_manager_infer_done_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_HW_INFER_MANAGER_INFER_DONE_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h notification invalid parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }

    LOGGER__INFO("Got hw infer done notification - Infer took {} cycles",
        d2h_notification_message->message_parameters.hw_infer_manager_infer_done_event.infer_cycles);

    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_closed_streams_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message) 
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_HEALTH_MONITOR_CLOSED_STREAMS_EVENT_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h notification invalid parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }

    if(d2h_notification_message->header.payload_length != sizeof(d2h_notification_message->message_parameters.health_monitor_closed_streams_event)) {
        LOGGER__ERROR("d2h notification invalid payload_length: {} vs {}", d2h_notification_message->header.payload_length,
			 sizeof(d2h_notification_message->message_parameters.health_monitor_closed_streams_event));
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH;
        goto l_exit;
    }

    LOGGER__CRITICAL("Got health monitor closed streams notification. temperature: TS00={} c, TS01={} c",
        d2h_notification_message->message_parameters.health_monitor_closed_streams_event.ts0_temperature,
        d2h_notification_message->message_parameters.health_monitor_closed_streams_event.ts1_temperature);

    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_overcurrent_alert_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_HEALTH_MONITOR_OVERCURRENT_ALERT_EVENT_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h event invalid parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }

    if(d2h_notification_message->header.payload_length != sizeof(d2h_notification_message->message_parameters.health_monitor_overcurrent_alert_event)) {
        LOGGER__ERROR("d2h event invalid payload_length: {}", d2h_notification_message->header.payload_length);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH;
        goto l_exit;
    }

    if (d2h_notification_message->message_parameters.health_monitor_overcurrent_alert_event.is_last_overcurrent_violation_reached) {
        LOGGER__WARNING("Got health monitor notification - last overcurrent violation allow alert state. The exceeded alert threshold is {} mA", 
                d2h_notification_message->message_parameters.health_monitor_overcurrent_alert_event.exceeded_alert_threshold);
    } else {
        switch (d2h_notification_message->message_parameters.health_monitor_overcurrent_alert_event.overcurrent_zone) {
            case HAILO_OVERCURRENT_PROTECTION_OVERCURRENT_ZONE__GREEN:
                LOGGER__INFO("Got health monitor notification - overcurrent reached green zone. clk frequency decrease process was stopped. The exceeded alert threshold is {} mA", 
                        d2h_notification_message->message_parameters.health_monitor_overcurrent_alert_event.exceeded_alert_threshold);
                break;
            case HAILO_OVERCURRENT_PROTECTION_OVERCURRENT_ZONE__RED:
                LOGGER__CRITICAL("Got health monitor notification - overcurrent reached red zone. clk frequency decrease process was started. The exceeded alert threshold is {} mA", 
                        d2h_notification_message->message_parameters.health_monitor_overcurrent_alert_event.exceeded_alert_threshold);
                break;
            default:
                LOGGER__ERROR("Got invalid health monitor notification - overcurrent alert state could not be parsed.");
                status = HAILO_STATUS__D2H_EVENTS__INVALID_ARGUMENT;
                goto l_exit;
        }
    }

    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;

}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_lcu_ecc_nonfatal_notification(
    D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_HEALTH_MONITOR_LCU_ECC_ERROR_EVENT_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h event lcu ecc uncorrectable error invalid parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }

    if(sizeof(d2h_notification_message->message_parameters.health_monitor_lcu_ecc_error_event) != d2h_notification_message->header.payload_length) {
        LOGGER__ERROR("d2h event invalid payload_length: {}", d2h_notification_message->header.payload_length);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH;
        goto l_exit;
    }

    LOGGER__WARNING("Got health monitor LCU ECC correctable error event. cluster_bitmap={}",
        d2h_notification_message->message_parameters.health_monitor_lcu_ecc_error_event.cluster_bitmap);

    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_lcu_ecc_fatal_notification(
    D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_HEALTH_MONITOR_LCU_ECC_ERROR_EVENT_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h event invalid lcu ecc uncorrectable error parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }

    if(sizeof(d2h_notification_message->message_parameters.health_monitor_lcu_ecc_error_event) != d2h_notification_message->header.payload_length) {
        LOGGER__ERROR("d2h event invalid payload_length: {}", d2h_notification_message->header.payload_length);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH;
        goto l_exit;
    }

    LOGGER__CRITICAL("Got health monitor LCU ECC uncorrectable error event. cluster_bitmap={}",
        d2h_notification_message->message_parameters.health_monitor_lcu_ecc_error_event.cluster_bitmap);

    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_cpu_ecc_error_notification(
    D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    CHECK_COMMON_STATUS(D2H_EVENT_HEALTH_MONITOR_CPU_ECC_EVENT_PARAMETER_COUNT == d2h_notification_message->header.parameter_count,
            HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT,
            "d2h event invalid parameter count: {}", d2h_notification_message->header.parameter_count);

    CHECK_COMMON_STATUS(sizeof(d2h_notification_message->message_parameters.health_monitor_cpu_ecc_event) == d2h_notification_message->header.payload_length,
            HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH,
            "d2h event invalid payload_length: {}", d2h_notification_message->header.payload_length);

    LOGGER__ERROR("Got health monitor CPU ECC error event. memory_bitmap={}",
        d2h_notification_message->message_parameters.health_monitor_cpu_ecc_event.memory_bitmap);

    status = HAILO_COMMON_STATUS__SUCCESS;

    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_health_monitor_cpu_ecc_fatal_notification(
    D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_HEALTH_MONITOR_CPU_ECC_EVENT_PARAMETER_COUNT != d2h_notification_message->header.parameter_count) {
        LOGGER__ERROR("d2h event invalid cpu ecc uncorrectable error parameter count: {}", d2h_notification_message->header.parameter_count);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT;
        goto l_exit;
    }

    if(sizeof(d2h_notification_message->message_parameters.health_monitor_cpu_ecc_event) != d2h_notification_message->header.payload_length) {
        LOGGER__ERROR("d2h event invalid payload_length: {}", d2h_notification_message->header.payload_length);
        status = HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH;
        goto l_exit;
    }

    LOGGER__CRITICAL("Got health monitor CPU ECC fatal event. memory_bitmap={}",
        d2h_notification_message->message_parameters.health_monitor_cpu_ecc_event.memory_bitmap);

    status = HAILO_COMMON_STATUS__SUCCESS;

l_exit:
    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_context_switch_breakpoint_reached(D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    CHECK_COMMON_STATUS(D2H_EVENT_CONTEXT_SWITCH_BREAKPOINT_REACHED_EVENT_PARAMETER_COUNT == d2h_notification_message->header.parameter_count,
            HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT,
            "d2h event invalid parameter count: {}", d2h_notification_message->header.parameter_count);

    CHECK_COMMON_STATUS(d2h_notification_message->header.payload_length == 
            sizeof(d2h_notification_message->message_parameters.context_switch_breakpoint_reached_event),
            HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH,
            "d2h event invalid payload_length: {}", d2h_notification_message->header.payload_length);
    
    LOGGER__INFO("Got Context switch breakpoint with net_group index {}, batch index {}, context index {}, action index {}",
            d2h_notification_message->message_parameters.context_switch_breakpoint_reached_event.application_index,
            d2h_notification_message->message_parameters.context_switch_breakpoint_reached_event.batch_index,
            d2h_notification_message->message_parameters.context_switch_breakpoint_reached_event.context_index,
            d2h_notification_message->message_parameters.context_switch_breakpoint_reached_event.action_index);

    status = HAILO_COMMON_STATUS__SUCCESS;

    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_context_switch_run_time_error_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;
    const char *run_time_error_status_text = NULL;
    uint32_t run_time_error_status = 0;

    CHECK_COMMON_STATUS(D2H_EVENT_CONTEXT_SWITCH_RUN_TIME_ERROR_EVENT_PARAMETER_COUNT == d2h_notification_message->header.parameter_count,
            HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_COUNT,
            "d2h event invalid parameter count: {}", d2h_notification_message->header.parameter_count);

    CHECK_COMMON_STATUS(d2h_notification_message->header.payload_length == 
            sizeof(d2h_notification_message->message_parameters.context_switch_run_time_error_event),
            HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH,
            "d2h event invalid payload_length: {}", d2h_notification_message->header.payload_length);

    run_time_error_status = d2h_notification_message->message_parameters.context_switch_run_time_error_event.exit_status;
    
    status = FIRMWARE_STATUS__get_textual((FIRMWARE_STATUS_t)run_time_error_status, &run_time_error_status_text);
    CHECK_COMMON_STATUS((HAILO_COMMON_STATUS__SUCCESS == status), status, 
        "Cannot find textual address for run time status {:#x}, status = {}", static_cast<int>((FIRMWARE_STATUS_t)run_time_error_status), static_cast<int>(status));

    LOGGER__ERROR("Got Context switch run time error on net_group index {}, batch index {}, context index {}, action index {} with status {}",
        d2h_notification_message->message_parameters.context_switch_run_time_error_event.application_index,
        d2h_notification_message->message_parameters.context_switch_run_time_error_event.batch_index,
        d2h_notification_message->message_parameters.context_switch_run_time_error_event.context_index,
        d2h_notification_message->message_parameters.context_switch_run_time_error_event.action_index,
        run_time_error_status_text);

    status = HAILO_COMMON_STATUS__SUCCESS;

    return status;
}

static HAILO_COMMON_STATUS_t D2H_EVENTS__parse_nn_core_crc_error_notification(D2H_EVENT_MESSAGE_t *d2h_notification_message)
{
    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    CHECK_COMMON_STATUS(0 == d2h_notification_message->header.payload_length,
            HAILO_STATUS__D2H_EVENTS__INCORRECT_PARAMETER_LENGTH,
            "d2h event invalid payload_length: {}", d2h_notification_message->header.payload_length);

    LOGGER__ERROR("Got NN-Core CRC Error in CSM unit");

    status = HAILO_COMMON_STATUS__SUCCESS;

    return status;
}

/**********************************************************************
 * Public Functions
 **********************************************************************/
HAILO_COMMON_STATUS_t D2H_EVENTS__parse_event(D2H_EVENT_MESSAGE_t *d2h_notification_message){

    HAILO_COMMON_STATUS_t status = HAILO_COMMON_STATUS__UNINITIALIZED;

    if (D2H_EVENT_ID_COUNT < d2h_notification_message->header.event_id){
        LOGGER__ERROR("d2h notification invalid notification_id: {}", d2h_notification_message->header.event_id);
        status = HAILO_STATUS__D2H_EVENTS__INVALID_ARGUMENT;
        goto l_exit;
    }
    status = g_firmware_notifications_parser[d2h_notification_message->header.event_id](d2h_notification_message); 

l_exit:
    return status;
}
