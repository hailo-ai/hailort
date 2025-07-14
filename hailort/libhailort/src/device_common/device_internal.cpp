/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "hef/hef_internal.hpp"

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
    }
    else {
        LOGGER__ERROR("Invalid device arcitecture. {}", static_cast<int>(architecture));
        return make_unexpected(HAILO_INVALID_DEVICE_ARCHITECTURE);
    }

    return Expected<firmware_type_t>(firmware_type);
}

hailo_status DeviceBase::firmware_update(const MemoryView &firmware_binary, bool should_reset)
{
    HAILO_COMMON_STATUS_t fw_header_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    hailo_status status = HAILO_UNINITIALIZED;
    firmware_version_t *current_fw_version = NULL;
    firmware_version_t new_app_fw_version = {};
    firmware_version_t new_core_fw_version = {};
    uint32_t offset = 0;
    uint32_t chunk_size = 0;
    MD5_CTX md5_ctx = {};
    MD5_SUM_t md5_sum = {};
    firmware_header_t *new_app_firmware_header = NULL;
    firmware_header_t *new_core_firmware_header = NULL;

    MD5_Init(&md5_ctx);
    MD5_Update(&md5_ctx, firmware_binary.data(), firmware_binary.size());
    MD5_Final(md5_sum, &md5_ctx);

    TRY(const auto firmware_type, get_fw_type());

    fw_header_status = FIRMWARE_HEADER_UTILS__validate_fw_headers((uintptr_t)firmware_binary.data(),
        static_cast<uint32_t>(firmware_binary.size()), false, &new_app_firmware_header,
        &new_core_firmware_header, NULL, firmware_type);
    CHECK(HAILO_COMMON_STATUS__SUCCESS == fw_header_status, HAILO_INVALID_FIRMWARE,
        "FW update validation failed with status {}", static_cast<int>(fw_header_status));

    // TODO: Are we ok with doing another identify here?
    TRY(auto board_info_before_update, Control::identify(*this));

    if (board_info_before_update.device_architecture != HAILO_ARCH_HAILO8_A0) {
        if ((new_app_firmware_header->firmware_major != new_core_firmware_header->firmware_major) ||
            (new_app_firmware_header->firmware_minor != new_core_firmware_header->firmware_minor) ||
            (GET_REVISION_NUMBER_VALUE(new_app_firmware_header->firmware_revision) != GET_REVISION_NUMBER_VALUE(new_core_firmware_header->firmware_revision))) {
            LOGGER__ERROR("FW versions mismatch between APP and CORE firmwares.");
            return HAILO_INVALID_FIRMWARE;
        }
    }

    new_app_fw_version.firmware_major = new_app_firmware_header->firmware_major;
    new_app_fw_version.firmware_minor = new_app_firmware_header->firmware_minor;
    new_app_fw_version.firmware_revision = new_app_firmware_header->firmware_revision;

    new_core_fw_version.firmware_major = new_core_firmware_header->firmware_major;
    new_core_fw_version.firmware_minor = new_core_firmware_header->firmware_minor;
    new_core_fw_version.firmware_revision = new_core_firmware_header->firmware_revision;

    status = validate_fw_version_for_platform(board_info_before_update, new_app_fw_version, FW_BINARY_TYPE_APP_FIRMWARE);
    CHECK_SUCCESS(status, "Invalid APP firmware binary was supplied");
    status = validate_fw_version_for_platform(board_info_before_update, new_core_fw_version, FW_BINARY_TYPE_CORE_FIRMWARE);
    CHECK_SUCCESS(status, "Invalid CORE firmware binary was supplied");

    if (IS_REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER(new_app_firmware_header->firmware_revision) || 
            IS_REVISION_EXTENDED_CONTEXT_SWITCH_BUFFER(new_core_firmware_header->firmware_revision)) {
        LOGGER__ERROR("Can't update to \"extended context switch buffer\" firmware (no ethernet support).");
        return HAILO_INVALID_FIRMWARE;
    }
    
    // TODO: Fix cast, we are assuming they are the same (HRT-3177)
    current_fw_version = reinterpret_cast<firmware_version_t*>(&(board_info_before_update.fw_version));

    LOGGER__INFO("Current Version: {}.{}.{}{}. Updating to version: {}.{}.{}{}", current_fw_version->firmware_major,
      current_fw_version->firmware_minor, current_fw_version->firmware_revision, 
      DEV_STRING_NOTE(board_info_before_update.is_release),
      new_app_fw_version.firmware_major, new_app_fw_version.firmware_minor,
      GET_REVISION_NUMBER_VALUE(new_app_fw_version.firmware_revision), 
      DEV_STRING_NOTE((!IS_REVISION_DEV(new_app_fw_version.firmware_revision))));


    if (IS_REVISION_DEV(new_app_fw_version.firmware_revision)) {
        LOGGER__INFO("New firmware version is a develop version, and may be unstable!");
    }

    if (FIRMWARE_HEADER_UTILS__is_binary_being_downgraded(current_fw_version, &new_app_fw_version)) {
        LOGGER__INFO("Firmware is being downgraded.");
    }

    status = Control::start_firmware_update(*this);
    CHECK_SUCCESS(status);
    LOGGER__INFO("Update started.");

    while (offset < firmware_binary.size()) {
        chunk_size = MIN(WRITE_CHUNK_SIZE, (static_cast<uint32_t>(firmware_binary.size()) - offset));
        LOGGER__DEBUG("Writing {} of data to offset {} / {}", chunk_size, offset, firmware_binary.size());
        status = Control::write_firmware_update(*this, offset, firmware_binary.data() + offset, chunk_size);
        CHECK_SUCCESS(status);
        offset += chunk_size;
    }
    LOGGER__INFO("Finished writing.");

    status = Control::validate_firmware_update(*this, &md5_sum, static_cast<uint32_t>(firmware_binary.size()));
    CHECK_SUCCESS(status);

    LOGGER__INFO("Firmware validation done.");

    status = Control::finish_firmware_update(*this);
    CHECK_SUCCESS(status);
    LOGGER__INFO("Firmware update finished.");

    if (should_reset) {
        LOGGER__INFO("Resetting...");
        status = reset(get_default_reset_mode());
        CHECK(HAILO_COMMON_STATUS__SUCCESS == fw_header_status, HAILO_INVALID_FIRMWARE,
            "FW update validation failed with status {}", static_cast<int>(fw_header_status));
        CHECK((status == HAILO_SUCCESS) || (status == HAILO_UNSUPPORTED_CONTROL_PROTOCOL_VERSION), status);

        auto board_info_after_install_expected = Control::identify(*this);
        if (board_info_after_install_expected.status() == HAILO_UNSUPPORTED_CONTROL_PROTOCOL_VERSION) {
            LOGGER__INFO("Successfully updated firmware. Protocol version has changed so firmware cannot be specified");
            return HAILO_SUCCESS;
        }

        CHECK_EXPECTED_AS_STATUS(board_info_after_install_expected); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here
        hailo_device_identity_t board_info_after_install = board_info_after_install_expected.release();
    
        LOGGER__INFO("New App FW version: {}.{}.{}{}", board_info_after_install.fw_version.major, board_info_after_install.fw_version.minor, 
            board_info_after_install.fw_version.revision, DEV_STRING_NOTE(board_info_after_install.is_release));

        // Validating that the new fw version is as expected
        if ((board_info_after_install.fw_version.major != new_app_fw_version.firmware_major) ||
            (board_info_after_install.fw_version.minor != new_app_fw_version.firmware_minor) ||
            (GET_REVISION_NUMBER_VALUE(board_info_after_install.fw_version.revision) != GET_REVISION_NUMBER_VALUE(new_app_fw_version.firmware_revision))) {
            LOGGER__WARNING("New App FW version is different than expected!");
        }
        
        if (board_info_after_install.device_architecture != HAILO_ARCH_HAILO8_A0) {
            hailo_core_information_t core_info_after_install{};
            status = Control::core_identify(*this, &core_info_after_install);
            CHECK_SUCCESS(status);
            LOGGER__INFO("New Core FW version: {}.{}.{}{}", core_info_after_install.fw_version.major, core_info_after_install.fw_version.minor, 
                core_info_after_install.fw_version.revision, DEV_STRING_NOTE(core_info_after_install.is_release));
            if ((core_info_after_install.fw_version.major != new_app_fw_version.firmware_major) ||
                (core_info_after_install.fw_version.minor != new_app_fw_version.firmware_minor) ||
                (GET_REVISION_NUMBER_VALUE(core_info_after_install.fw_version.revision) != GET_REVISION_NUMBER_VALUE(new_app_fw_version.firmware_revision))) {
                LOGGER__WARNING("New Core FW version is different than expected!");
            }
        }
    }

    return HAILO_SUCCESS;
}

hailo_status DeviceBase::second_stage_update(uint8_t* second_stage_binary, uint32_t second_stage_binary_length)
{
    HAILO_COMMON_STATUS_t second_stage_header_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    hailo_status status = HAILO_UNINITIALIZED;
    firmware_version_t new_second_stage_version = {};
    firmware_version_t minimum_second_stage_version = {1, 1, 0};
    uint32_t offset = 0;
    uint32_t chunk_size = 0;
    MD5_CTX md5_ctx = {};
    MD5_SUM_t md5_sum = {};
    firmware_header_t *new_second_stage_header = NULL;

    /* Validate arguments */
    CHECK_ARG_NOT_NULL(second_stage_binary);

    MD5_Init(&md5_ctx);
    MD5_Update(&md5_ctx, second_stage_binary, second_stage_binary_length);
    MD5_Final(md5_sum, &md5_ctx);

    TRY(const auto firmware_type, get_fw_type());

    second_stage_header_status = FIRMWARE_HEADER_UTILS__validate_second_stage_headers((uintptr_t)second_stage_binary,
        second_stage_binary_length, &new_second_stage_header, firmware_type);
    CHECK(HAILO_COMMON_STATUS__SUCCESS == second_stage_header_status, HAILO_INVALID_SECOND_STAGE,
            "Second stage update validation failed with status {}", static_cast<int>(second_stage_header_status));

    new_second_stage_version.firmware_major = new_second_stage_header->firmware_major;
    new_second_stage_version.firmware_minor = new_second_stage_header->firmware_minor;
    new_second_stage_version.firmware_revision = new_second_stage_header->firmware_revision;

    status = validate_binary_version_for_platform(&new_second_stage_version,
                                                           &minimum_second_stage_version,
                                                           FW_BINARY_TYPE_SECOND_STAGE_BOOT);
    CHECK_SUCCESS(status);

    LOGGER__INFO("Updating to version: {}.{}.{}",
      new_second_stage_version.firmware_major, new_second_stage_version.firmware_minor,
      GET_REVISION_NUMBER_VALUE(new_second_stage_version.firmware_revision));

    LOGGER__INFO("Writing second stage to internal memory");
    while (offset < second_stage_binary_length) {
        chunk_size = MIN(WRITE_CHUNK_SIZE, (second_stage_binary_length - offset));
        LOGGER__INFO("Writing {} of data to offset {} / {}", chunk_size, offset, second_stage_binary_length);
        status = Control::write_second_stage_to_internal_memory(*this, offset, second_stage_binary + offset, chunk_size);
        CHECK_SUCCESS(status);
        offset += chunk_size;
    }
    status = Control::copy_second_stage_to_flash(*this, &md5_sum, second_stage_binary_length);
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Second stage failed in a critical stage, Please contact Hailo support and DO NOT power off the device");
    }
    CHECK_SUCCESS(status);

    LOGGER__INFO("Finished copying second stage to flash.");

    return HAILO_SUCCESS;
}

hailo_status DeviceBase::store_sensor_config(uint32_t section_index, hailo_sensor_types_t sensor_type,
    uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps,
    const std::string &config_file_path, const std::string &config_name)
{    
    CHECK((section_index <= MAX_NON_ISP_SECTIONS), HAILO_INVALID_ARGUMENT, 
        "Cannot store sensor config in invalid section {}. Please choose section index (0-{}).", section_index, MAX_NON_ISP_SECTIONS);
    CHECK(sensor_type != HAILO_SENSOR_TYPES_HAILO8_ISP, HAILO_INVALID_ARGUMENT,
        "store_sensor_config intended only for sensor config, for ISP config use store_isp");

    TRY(auto control_buffers, SensorConfigUtils::read_config_file(config_file_path), "Failed reading config file");
    return store_sensor_control_buffers(control_buffers, section_index, sensor_type,
        reset_config_size, config_height, config_width, config_fps, config_name);
}

hailo_status DeviceBase::store_isp_config(uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps, 
    const std::string &isp_static_config_file_path, const std::string &isp_runtime_config_file_path, const std::string &config_name)
{    
    TRY(auto control_buffers, SensorConfigUtils::read_isp_config_file(isp_static_config_file_path, isp_runtime_config_file_path),
        "Failed reading ISP config file");
    return store_sensor_control_buffers(control_buffers, SENSOR_CONFIG__ISP_SECTION_INDEX, HAILO_SENSOR_TYPES_HAILO8_ISP,
        reset_config_size, config_height, config_width, config_fps, config_name);

}

Expected<Buffer> DeviceBase::sensor_get_sections_info()
{
    TRY(auto buffer, Buffer::create(SENSOR_SECTIONS_INFO_SIZE));

    hailo_status status = Control::sensor_get_sections_info(*this, buffer.data());
    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer;
}

hailo_status DeviceBase::sensor_dump_config(uint32_t section_index, const std::string &config_file_path)
{
    CHECK(SENSOR_CONFIG__TOTAL_SECTIONS_BLOCK_COUNT > section_index, HAILO_INVALID_ARGUMENT, "Section {} is invalid. Section index must be in the range [0 - {}]", section_index, (SENSOR_CONFIG__TOTAL_SECTIONS_BLOCK_COUNT - 1));
    TRY(auto sections_info_buffer, sensor_get_sections_info());

    SENSOR_CONFIG__section_info_t *section_info_ptr = &((SENSOR_CONFIG__section_info_t *)sections_info_buffer.data())[section_index];
    CHECK(section_info_ptr->is_free == 0, HAILO_NOT_FOUND, "Section {} is not active", section_index);
    CHECK(0 == (section_info_ptr->config_size % sizeof(SENSOR_CONFIG__operation_cfg_t)), HAILO_INVALID_OPERATION, "Section config size is invalid.");

    /* Read config data from device */
    TRY(auto operation_cfg, Buffer::create(section_info_ptr->config_size));

    size_t read_full_buffer_count = (section_info_ptr->config_size / MAX_CONFIG_ENTRIES_DATA_SIZE);
    uint32_t residue_to_read = static_cast<uint32_t>(section_info_ptr->config_size - (read_full_buffer_count * MAX_CONFIG_ENTRIES_DATA_SIZE)); 
    uint32_t entries_count = (section_info_ptr->config_size / static_cast<uint32_t>(sizeof(SENSOR_CONFIG__operation_cfg_t)));
    uint32_t offset = 0;

    hailo_status status = HAILO_UNINITIALIZED;
    for (uint32_t i = 0; i < read_full_buffer_count; i++) {
        status = Control::sensor_get_config(*this, section_index, offset,
            (uint32_t)MAX_CONFIG_ENTRIES_DATA_SIZE, (operation_cfg.data() + offset));
        CHECK_SUCCESS(status);
        offset += static_cast<uint32_t>(MAX_CONFIG_ENTRIES_DATA_SIZE);
    }
    if (0 < residue_to_read) {
        status = Control::sensor_get_config(*this, section_index, offset, residue_to_read, (operation_cfg.data() + offset));
        CHECK_SUCCESS(status);
    }

    status = SensorConfigUtils::dump_config_to_csv((SENSOR_CONFIG__operation_cfg_t*)operation_cfg.data(), config_file_path, entries_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS; 
}

hailo_status DeviceBase::sensor_set_i2c_bus_index(hailo_sensor_types_t sensor_type, uint32_t bus_index)
{
    return Control::sensor_set_i2c_bus_index(*this, sensor_type, bus_index);
}

hailo_status DeviceBase::sensor_load_and_start_config(uint32_t section_index)
{
    CHECK((section_index <= MAX_NON_ISP_SECTIONS), HAILO_INVALID_ARGUMENT, 
        "Cannot load config from invalid section index {}. Please choose section index (0-{}).",
        section_index, MAX_NON_ISP_SECTIONS);
    return Control::sensor_load_and_start_config(*this, section_index);
}

hailo_status DeviceBase::sensor_reset(uint32_t section_index)
{
    CHECK((section_index <= MAX_NON_ISP_SECTIONS), HAILO_INVALID_ARGUMENT, 
        "Cannot reset sensor in invalid section index {}. Please choose section index (0-{}).",
            section_index, MAX_NON_ISP_SECTIONS);
    return Control::sensor_reset(*this, section_index);
}

hailo_status DeviceBase::sensor_set_generic_i2c_slave(uint16_t slave_address, uint8_t offset_size, uint8_t bus_index,
    uint8_t should_hold_bus, uint8_t slave_endianness)
{
    return Control::sensor_set_generic_i2c_slave(*this, slave_address, offset_size, bus_index, should_hold_bus, slave_endianness);
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

Expected<hailo_fw_user_config_information_t> DeviceBase::examine_user_config()
{
    hailo_fw_user_config_information_t result{};
    auto status = Control::examine_user_config(*this, &result);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return result;
}

Expected<Buffer> DeviceBase::read_user_config()
{
    TRY(auto user_config_info, examine_user_config(), "Failed to examine user config");
    TRY(auto result, Buffer::create(user_config_info.total_size, 0));

    auto status = Control::read_user_config(*this, result.data(), static_cast<uint32_t>(result.size()));
    CHECK_SUCCESS_AS_EXPECTED(status);

    return result;
}

hailo_status DeviceBase::write_user_config(const MemoryView &buffer)
{
    return Control::write_user_config(*this, buffer.data(), static_cast<uint32_t>(buffer.size()));
}

hailo_status DeviceBase::erase_user_config()
{
    return Control::erase_user_config(*this);
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

    if (!is_hef_compatible(device_arch, static_cast<HEFHwArch>(hef.pimpl->get_device_arch()))) {
        auto device_arch_str = HailoRTCommon::get_device_arch_str(device_arch);
        auto hef_arch_str =
            HailoRTCommon::get_device_arch_str(hef_arch_to_device_arch(static_cast<HEFHwArch>(hef.pimpl->get_device_arch())));

        LOGGER__ERROR("HEF format is not compatible with device. Device arch: {}, HEF arch: {}",
            device_arch_str.c_str(), hef_arch_str.c_str());
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
    if (!verify_legacy_hef_only(static_cast<HEFHwArch>(hef.pimpl->get_device_arch()))) {
        auto hef_arch_str =
            HailoRTCommon::get_device_arch_str(hef_arch_to_device_arch(static_cast<HEFHwArch>(hef.pimpl->get_device_arch())));
        LOGGER__ERROR("HEF arch {} is not valid. Please use HailoRT v5.x instead.",
            hef_arch_str.c_str());
        return HAILO_HEF_NOT_COMPATIBLE_WITH_DEVICE;
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
        case START_UPDATE_CACHE_OFFSET_ID:
            *hailo_notification_id = HAILO_NOTIFICATION_ID_START_UPDATE_CACHE_OFFSET;
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

bool DeviceBase::verify_legacy_hef_only(HEFHwArch hef_arch)
{
    switch (hef_arch) {
    case HEFHwArch::HW_ARCH__SAGE_A0:
    case HEFHwArch::HW_ARCH__HAILO8:
    case HEFHwArch::HW_ARCH__HAILO8P:
    case HEFHwArch::HW_ARCH__HAILO8R:
    case HEFHwArch::HW_ARCH__SAGE_B0:
    case HEFHwArch::HW_ARCH__PAPRIKA_B0:
    case HEFHwArch::HW_ARCH__HAILO8L:
        return true;
    default:
        return false;
    }
}

bool DeviceBase::is_hef_compatible(hailo_device_architecture_t device_arch, HEFHwArch hef_arch)
{
    switch (device_arch) {
    case HAILO_ARCH_HAILO8:
        return (hef_arch == HEFHwArch::HW_ARCH__HAILO8P) || (hef_arch == HEFHwArch::HW_ARCH__HAILO8R) || (hef_arch == HEFHwArch::HW_ARCH__HAILO8L);
    case HAILO_ARCH_HAILO8L:
        return (hef_arch == HEFHwArch::HW_ARCH__HAILO8L);
    case HAILO_ARCH_HAILO15H:
    case HAILO_ARCH_HAILO10H:
        // Compare with HW_ARCH__LAVENDER and HW_ARCH__GINGER to support hefs compiled for them
        return (hef_arch == HEFHwArch::HW_ARCH__GINGER) || (hef_arch == HEFHwArch::HW_ARCH__LAVENDER) ||
            (hef_arch == HEFHwArch::HW_ARCH__HAILO15H) || (hef_arch == HEFHwArch::HW_ARCH__HAILO15M) || (hef_arch == HEFHwArch::HW_ARCH__HAILO10H);
    case HAILO_ARCH_HAILO15L:
        return (hef_arch == HEFHwArch::HW_ARCH__HAILO15L) || (hef_arch == HEFHwArch::HW_ARCH__PLUTO);
    case HAILO_ARCH_HAILO15M:
        return (hef_arch == HEFHwArch::HW_ARCH__HAILO15M);
    default:
        return false;
    }
}

hailo_device_architecture_t DeviceBase::hef_arch_to_device_arch(HEFHwArch hef_arch)
{
    switch (hef_arch) {
    case HEFHwArch::HW_ARCH__SAGE_A0:
        return HAILO_ARCH_HAILO8_A0;
    case HEFHwArch::HW_ARCH__HAILO8:
    case HEFHwArch::HW_ARCH__HAILO8P:
    case HEFHwArch::HW_ARCH__HAILO8R:
    case HEFHwArch::HW_ARCH__SAGE_B0:
    case HEFHwArch::HW_ARCH__PAPRIKA_B0:
        return HAILO_ARCH_HAILO8;
    case HEFHwArch::HW_ARCH__HAILO8L:
        return HAILO_ARCH_HAILO8L;
    case HEFHwArch::HW_ARCH__HAILO15H:
    case HEFHwArch::HW_ARCH__GINGER:
    case HEFHwArch::HW_ARCH__LAVENDER:
        return HAILO_ARCH_HAILO15H;
    case HEFHwArch::HW_ARCH__PLUTO:
    case HEFHwArch::HW_ARCH__HAILO15L:
        return HAILO_ARCH_HAILO15L;
    case HEFHwArch::HW_ARCH__HAILO15M:
        return HAILO_ARCH_HAILO15M;
    case HEFHwArch::HW_ARCH__HAILO10H:
        return HAILO_ARCH_HAILO10H;
    case HEFHwArch::HW_ARCH__MARS:
        return HAILO_ARCH_MARS;

    default:
        return HAILO_ARCH_MAX_ENUM;
    }
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

hailo_status DeviceBase::store_sensor_control_buffers(const std::vector<SENSOR_CONFIG__operation_cfg_t> &control_buffers, uint32_t section_index, hailo_sensor_types_t sensor_type,
    uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps, const std::string &config_name)
{
    hailo_status status = HAILO_UNINITIALIZED;

    uint32_t total_data_size = static_cast<uint32_t>(control_buffers.size() * sizeof(control_buffers[0]));
    size_t config_info_full_buffer = control_buffers.size() / MAX_CONFIG_INFO_ENTRIES;
    uint32_t is_first = 1;
    uint32_t offset = 0;

    for(uint32_t i = 0; i < config_info_full_buffer; i++) {
        status = Control::sensor_store_config(*this, is_first, section_index, offset, reset_config_size, sensor_type, total_data_size,
            (uint8_t*)control_buffers.data() + offset, (uint32_t)MAX_CONFIG_ENTRIES_DATA_SIZE, 
            config_height, config_width, config_fps, static_cast<uint32_t>(config_name.size()), (uint8_t*)config_name.c_str());
        CHECK_SUCCESS(status, "Failed to store sensor config");
        
        offset += (uint32_t)MAX_CONFIG_ENTRIES_DATA_SIZE;
        is_first = 0;
    }

    if (offset < total_data_size) {
        status = Control::sensor_store_config(*this, is_first, section_index, offset, reset_config_size, sensor_type, total_data_size,
            (uint8_t*)control_buffers.data() + offset, total_data_size - offset, 
            config_height, config_width, config_fps, static_cast<uint32_t>(config_name.size()), (uint8_t*)config_name.c_str());
        CHECK_SUCCESS(status,"Failed to store sensor config");
    }

    return HAILO_SUCCESS;
}

} /* namespace hailort */
