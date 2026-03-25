/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file device_internal.cpp
 * @brief Implementation of DeviceBase class
 **/

#include "hailo/hailort.h"

#include "common/os_utils.hpp"

#include "device_common/control.hpp"
#include "device_common/device_internal.hpp"
#include "network_group/network_group_internal.hpp"
#include "utils/sensor_config_utils.hpp"
#include "utils/logger_fetcher.hpp"
#include "hef/hef_internal.hpp"

#include <sys/stat.h>
#include <thread>

namespace hailort
{

DeviceBase::DeviceBase(Type type) :
    Device::Device(type),
    m_d2h_notification_queue(),
    m_d2h_notification_thread(),
    m_notif_fetch_thread_params(make_shared_nothrow<NotificationThreadSharedParams>()),
    m_d2h_callbacks{{0,0}},
    m_callbacks_lock(),
    m_is_shutdown_core_ops_called(false)
    // TODO: Handle m_notif_fetch_thread_params null pointer
{
#ifndef NDEBUG
    LOGGER__WARNING("libhailort is running in \"debug\" mode. Overall performance might be affected!");
#endif
#ifdef HAILO_EMULATOR
    LOGGER__WARNING("libhailort is running in \"Emulator\" mode.");
#endif
}

DeviceBase::~DeviceBase()
{
    stop_d2h_notification_thread();
}

Expected<ConfiguredNetworkGroupVector> DeviceBase::configure(Hef &hef,
    const NetworkGroupsParamsMap &configure_params)
{
    auto start_time = std::chrono::steady_clock::now();

    auto status = check_hef_is_compatible(hef);
    CHECK_SUCCESS_AS_EXPECTED(status);

    TRY(auto network_groups, add_hef(hef, configure_params));

    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
    LOGGER__INFO("Configuring HEF took {} milliseconds", elapsed_time_ms);

    return network_groups;
}

hailo_status DeviceBase::reset(hailo_reset_device_mode_t mode)
{
    CONTROL_PROTOCOL__reset_type_t reset_type = CONTROL_PROTOCOL__RESET_TYPE__COUNT; // set invalid value
    switch(mode) {
    case HAILO_RESET_DEVICE_MODE_CHIP:
        reset_type = CONTROL_PROTOCOL__RESET_TYPE__CHIP;
        break;
    case HAILO_RESET_DEVICE_MODE_NN_CORE:
        reset_type = CONTROL_PROTOCOL__RESET_TYPE__NN_CORE;
        break;
    case HAILO_RESET_DEVICE_MODE_SOFT:
        reset_type = CONTROL_PROTOCOL__RESET_TYPE__SOFT;
        break;
    case HAILO_RESET_DEVICE_MODE_FORCED_SOFT:
        reset_type = CONTROL_PROTOCOL__RESET_TYPE__FORCED_SOFT;
        break; 
    default:
        return HAILO_INVALID_ARGUMENT;
    }
    return reset_impl(reset_type);
}

hailo_status DeviceBase::set_notification_callback(const NotificationCallback &func, hailo_notification_id_t notification_id, void *opaque)
{
    CHECK((0 <= notification_id) && (HAILO_NOTIFICATION_ID_COUNT > notification_id), HAILO_INVALID_ARGUMENT,
        "Notification id value is invalid");
    CHECK_ARG_NOT_NULL(func);

    auto func_ptr = make_shared_nothrow<NotificationCallback>(func);
    CHECK_NOT_NULL(func_ptr, HAILO_OUT_OF_HOST_MEMORY);

    const std::lock_guard<std::mutex> lock(m_callbacks_lock);
    m_d2h_callbacks[notification_id].func = func_ptr;
    m_d2h_callbacks[notification_id].opaque = opaque;
    return HAILO_SUCCESS;
}

hailo_status DeviceBase::remove_notification_callback(hailo_notification_id_t notification_id)
{
    CHECK((0 <= notification_id) && (HAILO_NOTIFICATION_ID_COUNT > notification_id), HAILO_INVALID_ARGUMENT,
        "Notification id value is invalid");

    const std::lock_guard<std::mutex> lock(m_callbacks_lock);
    m_d2h_callbacks[notification_id].func = nullptr;
    m_d2h_callbacks[notification_id].opaque = nullptr;

    return HAILO_SUCCESS;
}

void DeviceBase::activate_notifications(const std::string &device_id)
{
    this->start_d2h_notification_thread(device_id);
    this->start_notification_fetch_thread(&m_d2h_notification_queue);
}

hailo_status DeviceBase::stop_notification_fetch_thread()
{
    hailo_status status = HAILO_SUCCESS; // best effort
    
    if (m_notif_fetch_thread_params->is_running) {
        m_notif_fetch_thread_params->is_running = false;
        auto disable_status = this->disable_notifications();
        if (HAILO_SUCCESS != disable_status) {
            status = disable_status;
            LOGGER__WARNING("Failed disabling notifications using ioctl command");
        }
    }

    // join thread even if disable_notifications failed - so we don't have non-joined thread
    if (m_notification_fetch_thread.joinable()) {
        m_notification_fetch_thread.join();
    }

    return status;
}

void DeviceBase::start_notification_fetch_thread(D2hEventQueue *write_queue)
{
    m_notif_fetch_thread_params->write_queue = write_queue;
    m_notif_fetch_thread_params->is_running = true;
    m_notification_fetch_thread = std::thread(&DeviceBase::notification_fetch_thread, this, m_notif_fetch_thread_params);
}

void DeviceBase::notification_fetch_thread(std::shared_ptr<NotificationThreadSharedParams> params)
{
    OsUtils::set_current_thread_name("NOTIFY_READ");
    while (params->is_running) {
        auto expected_notification = this->read_notification();
        if (HAILO_SUCCESS != expected_notification.status()) {
            if (params->is_running) {
                LOGGER__ERROR("Read notification failed with status={}", expected_notification.status());
            }
            break;
        }
        params->write_queue->push(expected_notification.release());
    }
}

Expected<firmware_type_t> DeviceBase::get_fw_type()
{
    firmware_type_t firmware_type;
    TRY(const auto architecture, get_architecture());

    if ((architecture == HAILO_ARCH_HAILO8) || (architecture == HAILO_ARCH_HAILO8L)) {
        firmware_type = FIRMWARE_TYPE_HAILO8;
    }
    else if ((architecture == HAILO_ARCH_HAILO15H ) || (architecture == HAILO_ARCH_HAILO15M)) {
        firmware_type = FIRMWARE_TYPE_HAILO15;
    }
    else if (architecture == HAILO_ARCH_HAILO15L) {
        firmware_type = FIRMWARE_TYPE_HAILO15L;
    } else if (architecture == HAILO_ARCH_HAILO12L) {
        firmware_type = FIRMWARE_TYPE_MARS;
    }
    else {
        LOGGER__ERROR("Invalid device arcitecture. {}", static_cast<int>(architecture));
        return make_unexpected(HAILO_INVALID_DEVICE_ARCHITECTURE);
    }

    return Expected<firmware_type_t>(firmware_type);
}

Expected<Buffer> DeviceBase::read_board_config()
{
    TRY(auto result, Buffer::create(BOARD_CONFIG_SIZE, 0));
    auto status = Control::read_board_config(*this, result.data(), static_cast<uint32_t>(result.size()));
    CHECK_SUCCESS_AS_EXPECTED(status);

    return result;
}

hailo_status DeviceBase::write_board_config(const MemoryView &buffer)
{
    return Control::write_board_config(*this, buffer.data(), static_cast<uint32_t>(buffer.size()));
}

void DeviceBase::start_d2h_notification_thread(const std::string &device_id)
{
    m_d2h_notification_thread = std::thread([this, device_id]() {
        OsUtils::set_current_thread_name("NOTIFY_PROC");
        d2h_notification_thread_main(device_id);
    });
}

void DeviceBase::stop_d2h_notification_thread()
{
    static const D2H_EVENT_MESSAGE_t TERMINATE {{0, 0, 0, 0, TERMINATE_EVENT_ID, 0, 0}, {}};
    m_d2h_notification_queue.clear();
    if (m_d2h_notification_thread.joinable()) {
        m_d2h_notification_queue.push(TERMINATE);
        m_d2h_notification_thread.join();
    }
}

void DeviceBase::d2h_notification_thread_main(const std::string &device_id)
{
    while (true) {
        auto notification = m_d2h_notification_queue.pop();
        if (notification.header.event_id == TERMINATE_EVENT_ID) {
            LOGGER__DEBUG("[{}] D2H notification thread got terminate signal, returning..", device_id);
            return;
        }
        /* Parse and print the Event info */
        auto d2h_status = D2H_EVENTS__parse_event(&notification);
        if (HAILO_COMMON_STATUS__SUCCESS != d2h_status) {
            LOGGER__ERROR("[{}] Fail to Parse firmware notification {} status is {}", device_id, notification.header.event_id, static_cast<int>(d2h_status));
            continue;
        }

        uint32_t notification_fw_id = notification.header.event_id;

        if (HEALTH_MONITOR_CLOSED_STREAMS_D2H_EVENT_ID == notification_fw_id) {
            if (!m_is_shutdown_core_ops_called) {
                LOGGER__WARNING("Aborting Infer, Device {} got closed streams notification from \'Health Monitor\'", device_id);
                shutdown_core_ops();
                m_is_shutdown_core_ops_called = true;
            }
        }

        hailo_notification_t callback_notification;
        hailo_notification_id_t hailo_notification_id;
        hailo_status status = fw_notification_id_to_hailo((D2H_EVENT_ID_t)notification_fw_id, &hailo_notification_id);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("[{}] Got invalid notification id from fw: {}", device_id, notification_fw_id);
            continue;
        }

        std::shared_ptr<NotificationCallback> callback_func = nullptr;
        void *callback_opaque = nullptr;
        {
            const std::lock_guard<std::mutex> lock(m_callbacks_lock);
            callback_func = m_d2h_callbacks[hailo_notification_id].func;
            callback_opaque = m_d2h_callbacks[hailo_notification_id].opaque;
            // m_callbacks_lock is freed here because user can call to a function in the callback that will
            // try to acquire it as well - resulting in a dead lock. I did not used recursive_mutex
            // because of the overhead
        }

        if (nullptr != callback_func) {
            callback_notification.id = hailo_notification_id;
            callback_notification.sequence = notification.header.sequence;
            static_assert(sizeof(callback_notification.body) == sizeof(notification.message_parameters), "D2H notification size mismatch");
            memcpy(&callback_notification.body, &notification.message_parameters, sizeof(notification.message_parameters));
            (*callback_func)(*this, callback_notification, callback_opaque);
        }
    }
}

hailo_status DeviceBase::check_hef_is_compatible(Hef &hef)
{
    TRY(const auto device_arch, get_architecture(), "Can't get device architecture (is the FW loaded?)");

    TRY(auto compatible_archs, hef.get_compatible_device_archs());
    if (!contains(compatible_archs, device_arch)) {
        auto device_arch_str = HailoRTCommon::get_device_arch_str(device_arch);
        std::string compatible_archs_str = "";
        for (const auto &arch : compatible_archs) {
            compatible_archs_str += HailoRTCommon::get_device_arch_str(arch) + " ";
        }

        LOGGER__ERROR("HEF format is not compatible with device. Device arch: {}, HEF compatible for: {}",
            device_arch_str.c_str(), compatible_archs_str.c_str());
        return HAILO_HEF_NOT_COMPATIBLE_WITH_DEVICE;
    }

    // TODO: MSW-227 check clock rate for hailo15 as well.
    if ((HAILO_ARCH_HAILO8 == device_arch) || (HAILO_ARCH_HAILO8L == device_arch)) {
        TRY(auto extended_device_information, Control::get_extended_device_information(*this), "Can't get device extended info");
        check_clock_rate_for_hailo8(extended_device_information.neural_network_core_clock_rate,
            static_cast<HEFHwArch>(hef.pimpl->get_device_arch()));
    }

    if ((static_cast<ProtoHEFHwArch>(HEFHwArch::HW_ARCH__HAILO8L) == hef.pimpl->get_device_arch()) &&
        (HAILO_ARCH_HAILO8 == device_arch)) {
        LOGGER__WARNING("HEF was compiled for Hailo8L device, while the device itself is Hailo8. " \
        "This will result in lower performance.");
    } else if ((static_cast<ProtoHEFHwArch>(HEFHwArch::HW_ARCH__HAILO15M) == hef.pimpl->get_device_arch()) &&
        (HAILO_ARCH_HAILO15H == device_arch)) {
        LOGGER__WARNING("HEF was compiled for Hailo15M device, while the device itself is Hailo15H. " \
        "This will result in lower performance.");
    }

    return HAILO_SUCCESS;
}

hailo_status DeviceBase::fw_notification_id_to_hailo(D2H_EVENT_ID_t fw_notification_id,
    hailo_notification_id_t* hailo_notification_id)
{
    hailo_status status = HAILO_UNINITIALIZED;

    switch (fw_notification_id) {
        case ETHERNET_SERVICE_RX_ERROR_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_ETHERNET_RX_ERROR;
            break;
        case D2H_HOST_INFO_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_DEBUG;
            break;
        case HEALTH_MONITOR_TEMPERATURE_ALARM_D2H_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_HEALTH_MONITOR_TEMPERATURE_ALARM;
            break;
        case HEALTH_MONITOR_CLOSED_STREAMS_D2H_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_HEALTH_MONITOR_DATAFLOW_SHUTDOWN;
            break;
        case HEALTH_MONITOR_OVERCURRENT_PROTECTION_ALERT_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM;
            break;
        case HEALTH_MONITOR_LCU_ECC_CORRECTABLE_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_LCU_ECC_CORRECTABLE_ERROR;
            break;
        case HEALTH_MONITOR_LCU_ECC_UNCORRECTABLE_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_LCU_ECC_UNCORRECTABLE_ERROR;
            break;
        case HEALTH_MONITOR_CPU_ECC_ERROR_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_CPU_ECC_ERROR;
            break;
        case HEALTH_MONITOR_CPU_ECC_FATAL_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_CPU_ECC_FATAL;
            break;
        case CONTEXT_SWITCH_BREAKPOINT_REACHED:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_CONTEXT_SWITCH_BREAKPOINT_REACHED;
            break;
        case HEALTH_MONITOR_CLOCK_CHANGED_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_HEALTH_MONITOR_CLOCK_CHANGED_EVENT;
            break;
        case HW_INFER_MANAGER_INFER_DONE:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_HW_INFER_MANAGER_INFER_DONE;
            break;
        case CONTEXT_SWITCH_RUN_TIME_ERROR:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_CONTEXT_SWITCH_RUN_TIME_ERROR_EVENT;
            break;
        case NN_CORE_CRC_ERROR_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_NN_CORE_CRC_ERROR_EVENT;
            break;
        case THROTTLING_STATE_CHANGE_EVENT_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_THROTTLING_STATE_CHANGE_EVENT;
            break;
        default:
            status = HAILO_INVALID_ARGUMENT;
            goto l_exit;
    }

    status = HAILO_SUCCESS;
l_exit:
    return status;
}

hailo_status DeviceBase::validate_binary_version_for_platform(firmware_version_t *new_binary_version, 
    firmware_version_t *min_supported_binary_version, FW_BINARY_TYPE_t fw_binary_type)
{
    HAILO_COMMON_STATUS_t binary_status = FIRMWARE_HEADER_UTILS__validate_binary_version(new_binary_version, min_supported_binary_version,
                                                                                         fw_binary_type);
    CHECK(HAILO_COMMON_STATUS__SUCCESS == binary_status, HAILO_INVALID_FIRMWARE,
                    "FW binary version validation failed with status {}", static_cast<int>(binary_status));
    return HAILO_SUCCESS;
}

hailo_status DeviceBase::validate_fw_version_for_platform(const hailo_device_identity_t &board_info, firmware_version_t fw_version, FW_BINARY_TYPE_t fw_binary_type)
{
    firmware_version_t min_supported_fw_version = {0, 0, 0};
    const firmware_version_t evb_mdot2_min_version = {2, 1, 0}; 
    const firmware_version_t mpcie_min_version = {2, 2, 0};
    
    if (0 == strncmp(EVB_PART_NUMBER_PREFIX, board_info.part_number, PART_NUMBER_PREFIX_LENGTH) || 
        0 == strncmp(MDOT2_PART_NUMBER_PREFIX, board_info.part_number, PART_NUMBER_PREFIX_LENGTH)) {
        min_supported_fw_version = evb_mdot2_min_version;
    }

    else if (0 == strncmp(MPCIE_PART_NUMBER_PREFIX, board_info.part_number, PART_NUMBER_PREFIX_LENGTH)) {
        min_supported_fw_version = mpcie_min_version;
    }
    else {
        min_supported_fw_version = evb_mdot2_min_version;
    }

    return validate_binary_version_for_platform(&fw_version, &min_supported_fw_version, fw_binary_type);
}

std::vector<hailo_device_architecture_t> DeviceBase::hef_arch_to_device_compatible_archs(HEFHwArch hef_arch)
{
    switch (hef_arch) {
        case HEFHwArch::HW_ARCH__SAGE_A0:
        return {HAILO_ARCH_HAILO8_A0};
    case HEFHwArch::HW_ARCH__HAILO8:
    case HEFHwArch::HW_ARCH__HAILO8P:
    case HEFHwArch::HW_ARCH__HAILO8R:
    case HEFHwArch::HW_ARCH__SAGE_B0:
    case HEFHwArch::HW_ARCH__PAPRIKA_B0:
        return {HAILO_ARCH_HAILO8};
    case HEFHwArch::HW_ARCH__HAILO8L:
        return {HAILO_ARCH_HAILO8L, HAILO_ARCH_HAILO8};
    case HEFHwArch::HW_ARCH__HAILO1XH:
    case HEFHwArch::HW_ARCH__GINGER:
    case HEFHwArch::HW_ARCH__LAVENDER:
        return {HAILO_ARCH_HAILO15H, HAILO_ARCH_HAILO10H};
    case HEFHwArch::HW_ARCH__PLUTO:
    case HEFHwArch::HW_ARCH__HAILO15L:
        return {HAILO_ARCH_HAILO15L};
    case HEFHwArch::HW_ARCH__HAILO15M:
        return {HAILO_ARCH_HAILO15M, HAILO_ARCH_HAILO15H, HAILO_ARCH_HAILO10H};
    case HEFHwArch::HW_ARCH__MARS:
        return {HAILO_ARCH_HAILO12L};
    default:
        return {HAILO_ARCH_MAX_ENUM};
    }
}

Expected<size_t> DeviceBase::fetch_logs(MemoryView buffer, hailo_log_type_t log_type)
{
#ifndef __linux__
    (void)(buffer);
    (void)(log_type);
    LOGGER__ERROR("fetch_logs is supported only on Linux systems");
    return make_unexpected(HAILO_NOT_SUPPORTED);
#else

    TRY(auto device_arch, get_architecture());
    CHECK((device_arch == HAILO_ARCH_HAILO15H) || (device_arch == HAILO_ARCH_HAILO15L) || (device_arch == HAILO_ARCH_HAILO15M) ||
        (device_arch == HAILO_ARCH_HAILO10H) || (device_arch == HAILO_ARCH_HAILO12L), HAILO_INVALID_DEVICE_ARCHITECTURE,
        "fetch_logs is not supported for device arch {}", HailoRTCommon::get_device_arch_str(device_arch));

    TRY(auto max_logs_size, get_max_logs_size(log_type));

    CHECK(buffer.size() == max_logs_size,
        HAILO_INSUFFICIENT_BUFFER, "Buffer size must be equal to the maximum log size: {} bytes, got: {} bytes",
        max_logs_size, buffer.size());

    TRY(auto logger_fetcher, LoggerFetcherFactory::create(log_type));

    return logger_fetcher->fetch_log(buffer, *this);
#endif
}

void DeviceBase::check_clock_rate_for_hailo8(uint32_t clock_rate, HEFHwArch hef_hw_arch)
{
    uint32_t expected_clock_rate = (hef_hw_arch == HEFHwArch::HW_ARCH__HAILO8R) ? HAILO8R_CLOCK_RATE : HAILO8_CLOCK_RATE;
    if (expected_clock_rate != clock_rate) {
        LOGGER__WARNING(
            "HEF was compiled assuming clock rate of {} MHz, while the device clock rate is {} MHz. " \
            "FPS calculations might not be accurate.",
            (expected_clock_rate / CLOCKS_IN_MHZ),
            (clock_rate / CLOCKS_IN_MHZ));
    }
}

} /* namespace hailort */
