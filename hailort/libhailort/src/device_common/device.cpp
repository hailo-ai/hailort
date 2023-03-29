/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file device.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "hailo/hailort.h"
#include "hailo/device.hpp"

#include "common/utils.hpp"

#include "device_common/control.hpp"
#include "vdma/pcie/pcie_device.hpp"
#include "vdma/integrated/integrated_device.hpp"
#include "eth/eth_device.hpp"

#include "byte_order.h"
#include "firmware_header_utils.h"
#include "control_protocol.h"
#include <memory>
#ifndef _MSC_VER
#include <sys/utsname.h>
#endif


namespace hailort
{

#define WRITE_CHUNK_SIZE (1024)
#define DEVICE_WORD_SIZE (4)

Device::Device(Type type) : 
    m_type(type),
    m_control_sequence(0),
    m_is_control_version_supported(false),
    m_device_architecture(HAILO_ARCH_MAX_ENUM)
{
#ifndef _MSC_VER
    struct utsname uname_data;
    if (-1 != uname(&uname_data)) {
        LOGGER__INFO("OS Version: {} {} {} {}", uname_data.sysname, uname_data.release,
            uname_data.version,uname_data.machine);
    } else {
        LOGGER__ERROR("uname failed (errno = {})", errno);
    }
#endif
}

Expected<std::vector<std::string>> Device::scan()
{
    // TODO: HRT-7530 support both CORE and PCIE
    if (IntegratedDevice::is_loaded()) {
        return std::vector<std::string>{IntegratedDevice::DEVICE_ID};
    }
    else {
        auto pcie_device_infos = PcieDevice::scan();
        CHECK_EXPECTED(pcie_device_infos);

        std::vector<std::string> results;
        results.reserve(pcie_device_infos->size());

        for (const auto pcie_device_info : pcie_device_infos.release()) {
            auto device_id = pcie_device_info_to_string(pcie_device_info);
            CHECK_EXPECTED(device_id);
            results.emplace_back(device_id.release());
        }

        return results;
    }
}

Expected<std::vector<hailo_pcie_device_info_t>> Device::scan_pcie()
{
    return PcieDevice::scan();
}

Expected<std::vector<hailo_eth_device_info_t>> Device::scan_eth(const std::string &interface_name,
    std::chrono::milliseconds timeout)
{
    return EthernetDevice::scan(interface_name, timeout);
}

Expected<std::vector<hailo_eth_device_info_t>> Device::scan_eth_by_host_address(const std::string &host_address,
    std::chrono::milliseconds timeout)
{
    return EthernetDevice::scan_by_host_address(host_address, timeout);
}

Expected<std::unique_ptr<Device>> Device::create()
{
    auto device_ids = scan();
    CHECK_EXPECTED(device_ids, "Failed scan devices");
    CHECK_AS_EXPECTED(device_ids->size() == 1, HAILO_INVALID_OPERATION,
        "Expected only 1 device on the system (found {}). Pass device_id to create a specific device", device_ids->size());

    return Device::create(device_ids->at(0));
}

Expected<std::unique_ptr<Device>> Device::create(const std::string &device_id)
{
    const bool DONT_LOG_ON_FAILURE = false;
    if (IntegratedDevice::DEVICE_ID == device_id) {
        return create_core();
    }
    else if (auto pcie_info = PcieDevice::parse_pcie_device_info(device_id, DONT_LOG_ON_FAILURE)) {
        return create_pcie(pcie_info.release());
    }
    else if (auto eth_info = EthernetDevice::parse_eth_device_info(device_id, DONT_LOG_ON_FAILURE)) {
        return create_eth(eth_info.release());
    }
    else {
        LOGGER__ERROR("Invalid device id {}", device_id);
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

Expected<std::unique_ptr<Device>> Device::create_pcie()
{
    auto pcie_device = PcieDevice::create();
    CHECK_EXPECTED(pcie_device);
    // Upcasting to Device unique_ptr (from PcieDevice unique_ptr)
    auto device = std::unique_ptr<Device>(pcie_device.release());
    return device;
}

Expected<std::unique_ptr<Device>> Device::create_pcie(const hailo_pcie_device_info_t &device_info)
{
    auto pcie_device = PcieDevice::create(device_info);
    CHECK_EXPECTED(pcie_device);
    // Upcasting to Device unique_ptr (from PcieDevice unique_ptr)
    auto device = std::unique_ptr<Device>(pcie_device.release());
    return device;
}

Expected<std::unique_ptr<Device>> Device::create_eth(const hailo_eth_device_info_t &device_info)
{
    auto eth_device = EthernetDevice::create(device_info);
    CHECK_EXPECTED(eth_device);
    // Upcasting to Device unique_ptr (from EthernetDevice unique_ptr)
    auto device = std::unique_ptr<Device>(eth_device.release());
    return device;
}

Expected<std::unique_ptr<Device>> Device::create_eth(const std::string &ip_addr)
{
    auto eth_device = EthernetDevice::create(ip_addr);
    CHECK_EXPECTED(eth_device);
    // Upcasting to Device unique_ptr (from EthernetDevice unique_ptr)
    auto device = std::unique_ptr<Device>(eth_device.release());
    return device;
}

Expected<hailo_pcie_device_info_t> Device::parse_pcie_device_info(const std::string &device_info_str)
{
    const bool LOG_ON_FAILURE = true;
    return PcieDevice::parse_pcie_device_info(device_info_str, LOG_ON_FAILURE);
}

Expected<std::string> Device::pcie_device_info_to_string(const hailo_pcie_device_info_t &device_info)
{
    return PcieDevice::pcie_device_info_to_string(device_info);
}

Expected<Device::Type> Device::get_device_type(const std::string &device_id)
{
    const bool DONT_LOG_ON_FAILURE = false;
    if (IntegratedDevice::DEVICE_ID == device_id) {
        return Type::INTEGRATED;
    }
    else if (auto pcie_info = PcieDevice::parse_pcie_device_info(device_id, DONT_LOG_ON_FAILURE)) {
        return Type::PCIE;
    }
    else if (auto eth_info = EthernetDevice::parse_eth_device_info(device_id, DONT_LOG_ON_FAILURE)) {
        return Type::ETH;
    }
    else {
        LOGGER__ERROR("Invalid device id {}", device_id);
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

uint32_t Device::get_control_sequence()
{
    return m_control_sequence;
}

bool Device::is_control_version_supported()
{
    return m_is_control_version_supported;
}

Device::Type Device::get_type() const
{
    return m_type;
}

Expected<hailo_stream_interface_t> Device::get_default_streams_interface() const
{
    switch(m_type) {
    case Type::PCIE:
        return HAILO_STREAM_INTERFACE_PCIE;
    case Type::INTEGRATED:
        return HAILO_STREAM_INTERFACE_INTEGRATED;
    case Type::ETH:
        return HAILO_STREAM_INTERFACE_ETH;
    default:
        LOGGER__ERROR("Failed to get default streams interface.");
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

hailo_status Device::set_fw_logger(hailo_fw_logger_level_t level, uint32_t interface_mask)
{
    return Control::set_fw_logger(*this, level, interface_mask);
}

hailo_status Device::set_throttling_state(bool should_activate)
{
    return Control::set_throttling_state(*this, should_activate);
}

Expected<bool> Device::get_throttling_state()
{
    return Control::get_throttling_state(*this);
}

hailo_status Device::write_memory(uint32_t address, const MemoryView &data)
{
    return Control::write_memory(*this, address, data.data(), static_cast<uint32_t>(data.size()));
}

hailo_status Device::read_memory(uint32_t address, MemoryView &data)
{
    return Control::read_memory(*this, address, data.data(), static_cast<uint32_t>(data.size()));
}

hailo_status Device::wd_enable(hailo_cpu_id_t cpu_id)
{
    return static_cast<hailo_status>(Control::wd_enable(*this, static_cast<uint8_t>(cpu_id), true));
}

hailo_status Device::wd_disable(hailo_cpu_id_t cpu_id)
{
    return Control::wd_enable(*this, static_cast<uint8_t>(cpu_id), false);
}

hailo_status Device::wd_config(hailo_cpu_id_t cpu_id, uint32_t wd_cycles, hailo_watchdog_mode_t wd_mode)
{
    CONTROL_PROTOCOL__WATCHDOG_MODE_t wd_type = CONTROL_PROTOCOL__WATCHDOG_NUM_MODES; // set invalid value
    switch(wd_mode) {
    case HAILO_WATCHDOG_MODE_HW_SW:
        wd_type = CONTROL_PROTOCOL__WATCHDOG_MODE_HW_SW;
        break;
    case HAILO_WATCHDOG_MODE_HW_ONLY:
        wd_type = CONTROL_PROTOCOL__WATCHDOG_MODE_HW_ONLY;
        break;
    default:
        LOGGER__ERROR("Invalid wd_mode");
        return HAILO_INVALID_ARGUMENT;
    }
    return Control::wd_config(*this, static_cast<uint8_t>(cpu_id), wd_cycles, wd_type);
}

Expected<uint32_t> Device::previous_system_state(hailo_cpu_id_t cpu_id)
{
    CONTROL_PROTOCOL__system_state_t res = {};
    auto status = Control::previous_system_state(*this, static_cast<uint8_t>(cpu_id), &res);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return res;
}

hailo_status Device::set_pause_frames(bool rx_pause_frames_enable)
{
    return Control::set_pause_frames(*this, rx_pause_frames_enable);
}

hailo_status Device::i2c_read(const hailo_i2c_slave_config_t &slave_config, uint32_t register_address, MemoryView &data)
{
    return Control::i2c_read(*this, &slave_config, register_address, data.data(), static_cast<uint32_t>(data.size()));
}

hailo_status Device::i2c_write(const hailo_i2c_slave_config_t &slave_config, uint32_t register_address, const MemoryView &data)
{
    return Control::i2c_write(*this, &slave_config, register_address, data.data(), static_cast<uint32_t>(data.size()));
}

Expected<float32_t> Device::power_measurement(hailo_dvm_options_t dvm, hailo_power_measurement_types_t measurement_type)
{
    float32_t res = 0;
    auto status = Control::power_measurement(*this, static_cast<CONTROL_PROTOCOL__dvm_options_t>(dvm),
        static_cast<CONTROL_PROTOCOL__power_measurement_types_t>(measurement_type), &res);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return res;
}

hailo_status Device::start_power_measurement(hailo_averaging_factor_t averaging_factor, hailo_sampling_period_t sampling_period)
{
    return Control::start_power_measurement(*this, static_cast<CONTROL_PROTOCOL__averaging_factor_t>(averaging_factor),
        static_cast<CONTROL_PROTOCOL__sampling_period_t>(sampling_period));
}

hailo_status Device::set_power_measurement(hailo_measurement_buffer_index_t buffer_index, hailo_dvm_options_t dvm, hailo_power_measurement_types_t measurement_type)
{
    return Control::set_power_measurement(*this, buffer_index, static_cast<CONTROL_PROTOCOL__dvm_options_t>(dvm), static_cast<CONTROL_PROTOCOL__power_measurement_types_t>(measurement_type));
}

Expected<hailo_power_measurement_data_t> Device::get_power_measurement(hailo_measurement_buffer_index_t buffer_index, bool should_clear)
{
    hailo_power_measurement_data_t measurement_data = {};
    auto status = Control::get_power_measurement(*this, buffer_index, should_clear, &measurement_data);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return measurement_data;
}

hailo_status Device::stop_power_measurement()
{
    return Control::stop_power_measurement(*this);
}

Expected<hailo_chip_temperature_info_t> Device::get_chip_temperature()
{
    hailo_chip_temperature_info_t res = {};
    auto status = Control::get_chip_temperature(*this, &res);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return res;
}

hailo_status Device::test_chip_memories()
{
    return Control::test_chip_memories(*this);
}

hailo_status Device::set_sleep_state(hailo_sleep_state_t sleep_state)
{
    return Control::set_sleep_state(*this, sleep_state);
}

hailo_status Device::direct_write_memory(uint32_t address, const void *buffer, uint32_t size)
{
    (void) address;
    (void) buffer;
    (void) size;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status Device::direct_read_memory(uint32_t address, void *buffer, uint32_t size)
{
    (void) address;
    (void) buffer;
    (void) size;
    return HAILO_NOT_IMPLEMENTED;
}

Expected<hailo_device_identity_t> Device::identify()
{
    return Control::identify(*this);
}

Expected<hailo_core_information_t> Device::core_identify()
{
    hailo_core_information_t res = {};
    auto status = Control::core_identify(*this, &res);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return res;
}

Expected<hailo_extended_device_information_t> Device::get_extended_device_information()
{
    return Control::get_extended_device_information(*this);
}

// Note: This function needs to be called after each reset/fw_update if we want the device's
//       state to remain valid after these ops (see HRT-3116)
hailo_status Device::update_fw_state()
{
    // Assuming FW is loaded, send identify
    auto board_info_expected = Control::identify(*this);
    CHECK_EXPECTED_AS_STATUS(board_info_expected);
    hailo_device_identity_t board_info = board_info_expected.release();

    if ((FIRMWARE_VERSION_MAJOR == board_info.fw_version.major) &&
         (FIRMWARE_VERSION_MINOR == board_info.fw_version.minor)) {
        m_is_control_version_supported = true;
    } else {
        LOGGER__WARNING("Unsupported firmware operation. Host: {}.{}.{}, Device: {}.{}.{}{}",
                FIRMWARE_VERSION_MAJOR,
                FIRMWARE_VERSION_MINOR,
                FIRMWARE_VERSION_REVISION,
                board_info.fw_version.major,
                board_info.fw_version.minor,
                board_info.fw_version.revision, 
                DEV_STRING_NOTE(board_info.is_release));
        m_is_control_version_supported = false;
    }
    m_device_architecture = board_info.device_architecture;

    return HAILO_SUCCESS;
}

hailo_status Device::fw_interact(uint8_t *request_buffer, size_t request_size,
    uint8_t *response_buffer, size_t *response_size)
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__request_t *request = (CONTROL_PROTOCOL__request_t *)(request_buffer);
    uint32_t opcode = HAILO_CONTROL_OPCODE_COUNT;
    ASSERT(NULL != request_buffer);
    ASSERT(NULL != response_buffer);
    hailo_cpu_id_t cpu_id;

    opcode = BYTE_ORDER__ntohl(request->header.common_header.opcode);
    /* Make sure that the version is supported or opcode is critical */
    if (!m_is_control_version_supported && 
            !g_CONTROL_PROTOCOL__is_critical[opcode]){
        LOGGER__ERROR(
                "Operation {} is not allowed when FW version in not supported. Host supported FW version is {}.{}.{}",
                BYTE_ORDER__ntohl(request->header.common_header.opcode),
                FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_REVISION
                );     
        return HAILO_UNSUPPORTED_FW_VERSION;
    }
    /* Get the CPU ID */
    cpu_id = (hailo_cpu_id_t)g_CONTROL_PROTOCOL__cpu_id[opcode];
    
    status = this->fw_interact_impl(request_buffer, request_size, response_buffer, response_size, cpu_id);

    // Always increment sequence
    this->increment_control_sequence();
    // Check this->fw_interact_impl
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Device::set_overcurrent_state(bool should_activate)
{
    return Control::set_overcurrent_state(*this, should_activate);
}

Expected<bool> Device::get_overcurrent_state()
{
    return Control::get_overcurrent_state(*this);
}

Expected<hailo_health_info_t> Device::get_health_information()
{
    return Control::get_health_information(*this);
}

Expected<std::vector<uint8_t>> Device::get_number_of_dynamic_contexts_per_network_group()
{
    CONTROL_PROTOCOL__context_switch_main_header_t context_switch_main_header{};
    const auto status = Control::get_context_switch_main_header(*this, &context_switch_main_header);
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::vector<uint8_t> number_of_contexts_per_network_group;
    for (auto network_group_index = 0; network_group_index < context_switch_main_header.application_count; network_group_index++) {
        const uint32_t num_contexts = context_switch_main_header.application_header[network_group_index].dynamic_contexts_count;
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(num_contexts), HAILO_INTERNAL_FAILURE, "num_contexts must fit in one byte");
        number_of_contexts_per_network_group.emplace_back(static_cast<uint8_t>(num_contexts));
    }

    return number_of_contexts_per_network_group;
}

Expected<Buffer> Device::download_context_action_list(uint32_t network_group_id, uint8_t context_type,
    uint8_t context_index, uint32_t *base_address, uint32_t *batch_counter, uint16_t max_size)
{
    CHECK_ARG_NOT_NULL_AS_EXPECTED(base_address);
    CHECK_ARG_NOT_NULL_AS_EXPECTED(batch_counter);

    // Allocate room for an action list of at most max_size bytes
    auto action_list = Buffer::create(max_size);
    CHECK_EXPECTED(action_list);

    uint32_t base_address_local = 0;
    uint32_t batch_counter_local = 0;
    uint16_t actual_size = 0;
    const auto status = Control::download_context_action_list(*this, network_group_id,
        (CONTROL_PROTOCOL__context_switch_context_type_t)context_type, context_index, action_list->size(),
        &base_address_local, action_list->data(), &actual_size, &batch_counter_local);
    CHECK_SUCCESS_AS_EXPECTED(status);
    CHECK_AS_EXPECTED(actual_size <= max_size, HAILO_INTERNAL_FAILURE);

    // Create a copy of the list, truncating to the needed size
    auto final_action_list = Buffer::create(action_list->data(), actual_size);
    CHECK_EXPECTED(action_list);

    // Transfer ownership of out params
    *base_address = base_address_local;
    *batch_counter = batch_counter_local;

    return final_action_list.release();
}

hailo_status Device::set_context_action_list_timestamp_batch(uint16_t batch_index)
{
    static const bool ENABLE_USER_CONFIG = true;
    return Control::config_context_switch_timestamp(*this, batch_index, ENABLE_USER_CONFIG);
}

hailo_status Device::set_context_switch_breakpoint(uint8_t breakpoint_id, bool break_at_any_network_group_index,
    uint8_t network_group_index, bool break_at_any_batch_index, uint16_t batch_index, bool break_at_any_context_index,
    uint8_t context_index, bool break_at_any_action_index, uint16_t action_index) 
{
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data = {
        break_at_any_network_group_index,
        network_group_index,
        break_at_any_batch_index,
        batch_index,
        break_at_any_context_index,
        context_index,
        break_at_any_action_index,
        action_index};

    auto status = Control::config_context_switch_breakpoint(*this, breakpoint_id,
        CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_SET, &breakpoint_data);
    CHECK_SUCCESS(status, "Failed Setting context switch breakpoint in continue breakpoint");

    return HAILO_SUCCESS;
}

hailo_status Device::continue_context_switch_breakpoint(uint8_t breakpoint_id) 
{
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data = {false, 0, false, 0, false, 0, false, 0};

    auto status = Control::config_context_switch_breakpoint(*this, breakpoint_id, 
            CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_CONTINUE, &breakpoint_data);
    CHECK_SUCCESS(status, "Failed Setting context switch breakpoint in continue breakpoint");

    return HAILO_SUCCESS;
}

hailo_status Device::clear_context_switch_breakpoint(uint8_t breakpoint_id) 
{
    CONTROL_PROTOCOL__context_switch_breakpoint_data_t breakpoint_data = {false, 0, false, 0, false, 0, false, 0};

    auto status = Control::config_context_switch_breakpoint(*this, breakpoint_id,
            CONTROL_PROTOCOL__CONTEXT_SWITCH_BREAKPOINT_CONTROL_CLEAR, &breakpoint_data);
    CHECK_SUCCESS(status, "Failed Setting context switch breakpoint in clear breakpoint");

    return HAILO_SUCCESS;
}

Expected<uint8_t> Device::get_context_switch_breakpoint_status(uint8_t breakpoint_id)
{
    CONTROL_PROTOCOL__context_switch_debug_sys_status_t breakpoint_status = 
        CONTROL_PROTOCOL__CONTEXT_SWITCH_DEBUG_SYS_STATUS_COUNT;

    auto status = Control::get_context_switch_breakpoint_status(*this, breakpoint_id,
            &breakpoint_status);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed getting context switch breakpoint");

    return static_cast<uint8_t>(breakpoint_status);
}

Expected<std::unique_ptr<Device>> Device::create_core()
{
    auto integrated_device = IntegratedDevice::create();
    CHECK_EXPECTED(integrated_device);
    // Upcasting to Device unique_ptr (from IntegratedDevice unique_ptr)
    auto device = std::unique_ptr<Device>(integrated_device.release());
    return device;
}

Expected<NetworkGroupsParamsMap> Device::create_configure_params(Hef &hef) const
{
    auto stream_interface = get_default_streams_interface();
    CHECK_EXPECTED(stream_interface, "Failed to get default streams interface");

    return hef.create_configure_params(stream_interface.release());
}

Expected<ConfigureNetworkParams> Device::create_configure_params(Hef &hef, const std::string &network_group_name) const
{
    auto stream_interface = get_default_streams_interface();
    CHECK_EXPECTED(stream_interface, "Failed to get default streams interface");

    return hef.create_configure_params(stream_interface.release(), network_group_name);
}

} /* namespace hailort */
