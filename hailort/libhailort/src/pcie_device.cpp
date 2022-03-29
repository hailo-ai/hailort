/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_device.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "pcie_device.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "hailo/device.hpp"
#include "hailo/hef.hpp"
#include "control.hpp"
#include "hlpcie.hpp"
#include "common/compiler_extensions_compat.hpp"
#include "os/hailort_driver.hpp"
#include "context_switch/multi_context/resource_manager.hpp"

#include <new>
#include <algorithm>

namespace hailort
{

#define ISTATUS_HOST_FW_IRQ_EVENT        0x2000000    /* IN BCS_ISTATUS_HOST_FW_IRQ_MASK  IN ISTATUS_HOST */
#define ISTATUS_HOST_FW_IRQ_CONTROL      0x4000000    /* IN BCS_ISTATUS_HOST_FW_IRQ_MASK  IN ISTATUS_HOST */
#define ISTATUS_HOST_FW_IRQ_FW_LOAD      0x6000000    /* IN BCS_ISTATUS_HOST_FW_IRQ_MASK  IN ISTATUS_HOST */


#ifndef HAILO_EMULATOR
static const uint32_t PCIE_DEFAULT_TIMEOUT_MS = 1000;
#else /* ifndef HAILO_EMULATOR */
static const uint32_t PCIE_DEFAULT_TIMEOUT_MS = 50000;
#endif /* ifndef HAILO_EMULATOR */


Expected<std::vector<hailo_pcie_device_info_t>> PcieDevice::scan()
{
    auto scan_results = HailoRTDriver::scan_pci();
    if (!scan_results) {
        LOGGER__ERROR("scan pci failed");
        return make_unexpected(scan_results.status());
    }

    std::vector<hailo_pcie_device_info_t> out_results(scan_results->size());
    std::transform(scan_results->begin(), scan_results->end(), std::begin(out_results), [](const auto &scan_result) {
        hailo_pcie_device_info_t device_info = {};
        device_info.domain = scan_result.domain;
        device_info.bus = scan_result.bus;
        device_info.device = scan_result.device;
        device_info.func = scan_result.func;
        return device_info;
    });
    return out_results;
}

Expected<std::unique_ptr<PcieDevice>> PcieDevice::create()
{
    // Take the first device
    auto scan_result = scan();
    CHECK_EXPECTED(scan_result, "Failed scanning pcie devices");
    CHECK_AS_EXPECTED(scan_result->size() == 1, HAILO_INVALID_OPERATION,
        "Expected only 1 PCIe device. Pass `hailo_pcie_device_info_t` to create a specific PCIe device");
    return create(scan_result->at(0));
}

Expected<std::unique_ptr<PcieDevice>> PcieDevice::create(const hailo_pcie_device_info_t &device_info)
{
    auto scan_results = HailoRTDriver::scan_pci();
    if (!scan_results) {
        LOGGER__ERROR("scan pci failed");
        return make_unexpected(scan_results.status());
    }

    // Find device index based on the information from "device_info"
    auto device_found = std::find_if(scan_results->cbegin(), scan_results->cend(),
         [device_info](const auto &compared_board) {
            return (device_info.bus == compared_board.bus) && 
                   (device_info.device == compared_board.device) &&
                   (device_info.func == compared_board.func) &&
                   ((HAILO_PCIE_ANY_DOMAIN == device_info.domain) || (device_info.domain == compared_board.domain));
        });

    if (device_found == std::end(scan_results.value())) {
        LOGGER__ERROR("Requested device not found");
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    auto driver = HailoRTDriver::create(device_found->dev_path);
    if (!driver) {
        LOGGER__ERROR("Failed to initialize HailoRTDriver");
        return make_unexpected(driver.status());
    }

    hailo_status status = HAILO_UNINITIALIZED;
    auto device = std::unique_ptr<PcieDevice>(new (std::nothrow) PcieDevice(driver.release(), device_info, status));
    CHECK_AS_EXPECTED((nullptr != device), HAILO_OUT_OF_HOST_MEMORY);

    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed creating PcieDevice");
        return make_unexpected(status);
    }

    return device;
}

// same format as in lspci - [<domain>].<bus>.<device>.<func> 
// domain (0 to ffff) bus (0 to ff), device (0 to 1f) and function (0 to 7).
static const char *DEVICE_ID_STRING_FMT_SHORT = "%02x:%02x.%d";
static constexpr int DEVICE_ID_ELEMENTS_COUNT_SHORT = 3;
static constexpr int DEVICE_ID_STRING_LENGTH_SHORT = 7; // Length without null terminator

static const char *DEVICE_ID_STRING_FMT_LONG = "%04x:%02x:%02x.%d";
static constexpr int DEVICE_ID_ELEMENTS_COUNT_LONG = 4;
static constexpr int DEVICE_ID_STRING_LENGTH_LONG = 12; // Length without null terminator

static constexpr int DEVICE_ID_MAX_STRING_LENGTH = std::max(DEVICE_ID_STRING_LENGTH_SHORT, DEVICE_ID_STRING_LENGTH_LONG);

Expected<hailo_pcie_device_info_t> PcieDevice::parse_pcie_device_info(const std::string &device_info_str)
{
    hailo_pcie_device_info_t device_info{};
    int scanf_res = sscanf(device_info_str.c_str(), DEVICE_ID_STRING_FMT_LONG,
        &device_info.domain, &device_info.bus, &device_info.device, &device_info.func);
    if (DEVICE_ID_ELEMENTS_COUNT_LONG != scanf_res) {
        // Domain not included, trying short
        device_info.domain = HAILO_PCIE_ANY_DOMAIN;
        scanf_res = sscanf(device_info_str.c_str(), DEVICE_ID_STRING_FMT_SHORT,
            &device_info.bus, &device_info.device, &device_info.func);
        CHECK_AS_EXPECTED(DEVICE_ID_ELEMENTS_COUNT_SHORT == scanf_res,
            HAILO_INVALID_ARGUMENT,
            "Invalid device info string (format is [<domain>].<bus>.<device>.<func>) {}");
    }

    return device_info;
}

Expected<std::string> PcieDevice::pcie_device_info_to_string(const hailo_pcie_device_info_t &device_info)
{
    char device_string[DEVICE_ID_MAX_STRING_LENGTH + 1] = { 0 };

    if (HAILO_PCIE_ANY_DOMAIN != device_info.domain) {
        int res = snprintf(device_string, DEVICE_ID_STRING_LENGTH_LONG + 1, DEVICE_ID_STRING_FMT_LONG, 
            device_info.domain, device_info.bus, device_info.device, device_info.func);
        // If the users give invalid device_info on release, they will get an invalid string.
        CHECK_AS_EXPECTED((DEVICE_ID_STRING_LENGTH_LONG) == res, HAILO_INVALID_ARGUMENT, "Invalid device info");
    }
    else {
        int res = snprintf(device_string, DEVICE_ID_STRING_LENGTH_SHORT + 1, DEVICE_ID_STRING_FMT_SHORT, 
            device_info.bus, device_info.device, device_info.func);
        // If the users gives invalid device_info on release, they will get an invalid string.
        CHECK_AS_EXPECTED((DEVICE_ID_STRING_LENGTH_SHORT) == res, HAILO_INVALID_ARGUMENT, "Invalid device info");
    }

    return std::string(device_string);
}

PcieDevice::PcieDevice(HailoRTDriver &&driver, const hailo_pcie_device_info_t &device_info, hailo_status &status) :
    VdmaDevice::VdmaDevice(std::move(driver), Device::Type::PCIE),
    m_fw_up(false),
    m_device_info(device_info)
{
    // Send identify if FW is loaded
    status = HAILO_PCIE__read_atr_to_validate_fw_is_up(m_driver, &m_fw_up);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("HAILO_PCIE__read_atr_to_validate_fw_is_up failed with status {}", status);
        return;
    }

    // Note: m_fw_up needs to be called after each reset/fw_update if we want the device's
    //       state to remain valid after these ops (see HRT-3116)
    if (m_fw_up) {
        status = update_fw_state();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("update_fw_state() failed with status {}", status);
            return;
        }
    } else {
        LOGGER__WARNING("FW is not loaded to the device. Please load FW before using the device.");
        m_is_control_version_supported = false;
    }

    auto message = pcie_device_info_to_string(device_info);
    if (HAILO_SUCCESS != message.status()) {
        status = message.status();
        LOGGER__ERROR("pcie_device_info_to_string() failed with status {}", status);
        return;
    }
    m_device_id = message.release();

    this->activate_notifications(m_device_id);

    status = HAILO_SUCCESS;
}

PcieDevice::~PcieDevice()
{
    auto status = stop_notification_fetch_thread();
    if (HAILO_SUCCESS != status) {
        LOGGER__WARNING("Stopping notification thread ungracefully");
    }
}

hailo_status PcieDevice::fw_interact_impl(uint8_t *request_buffer, size_t request_size, uint8_t *response_buffer, 
                                          size_t *response_size, hailo_cpu_id_t cpu_id)
{
    return HAILO_PCIE__fw_interact(m_driver, request_buffer, (uint32_t)request_size,  response_buffer, 
                                    response_size, PCIE_DEFAULT_TIMEOUT_MS, cpu_id);
}

void PcieDevice::set_is_control_version_supported(bool value)
{
    m_is_control_version_supported = value;
}

Expected<hailo_device_architecture_t> PcieDevice::get_architecture() const
{
    if (!m_fw_up) {
        LOGGER__WARNING("FW is not loaded to the device. Please load FW before using the device.");
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    return Expected<hailo_device_architecture_t>(m_device_architecture);
}

hailo_status PcieDevice::direct_write_memory(uint32_t address, const void *buffer, uint32_t size)
{
    return HAILO_PCIE__write_memory(m_driver, address, buffer, size);
}

hailo_status PcieDevice::direct_read_memory(uint32_t address, void *buffer, uint32_t size)
{
    return HAILO_PCIE__read_memory(m_driver, address, buffer, size);
}

const char *PcieDevice::get_dev_id() const
{
    return m_device_id.c_str();
}

hailo_status PcieDevice::close_all_vdma_channels()
{
    auto status = HAILO_UNINITIALIZED;

    // TODO: Add one icotl to stop all channels at once (HRT-6097)
    for (int channel_index = 0; channel_index <= MAX_H2D_CHANNEL_INDEX; channel_index++) {
        auto host_registers = VdmaChannelRegs(m_driver, channel_index, HailoRTDriver::DmaDirection::H2D);
        status = host_registers.stop_channel();
        CHECK_SUCCESS(status);

        auto device_registers = VdmaChannelRegs(m_driver, channel_index, HailoRTDriver::DmaDirection::D2H);
        status = device_registers.stop_channel();
        CHECK_SUCCESS(status);
    }

    for (int channel_index = MIN_D2H_CHANNEL_INDEX; channel_index <= MAX_D2H_CHANNEL_INDEX; channel_index++) {
        auto host_registers = VdmaChannelRegs(m_driver, channel_index, HailoRTDriver::DmaDirection::D2H);
        status = host_registers.stop_channel();
        CHECK_SUCCESS(status);

        auto device_registers = VdmaChannelRegs(m_driver, channel_index, HailoRTDriver::DmaDirection::H2D);
        status = device_registers.stop_channel();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status PcieDevice::reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    bool is_expecting_response = true;

    CHECK(CONTROL_PROTOCOL__RESET_TYPE__CHIP != reset_type, HAILO_INVALID_OPERATION,
        "Chip reset is not supported for PCIe device.");

    if ((CONTROL_PROTOCOL__RESET_TYPE__FORCED_SOFT == reset_type) || (CONTROL_PROTOCOL__RESET_TYPE__SOFT == reset_type)) {
        is_expecting_response = false; // TODO: Check boot source, set is_expecting_response = (boot_source != pcie)
        status = close_all_vdma_channels();
        CHECK_SUCCESS(status);
    }

    common_status = CONTROL_PROTOCOL__pack_reset_request(&request, &request_size, m_control_sequence, reset_type);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    LOGGER__DEBUG("Sending reset request");
    status = this->fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    // fw_interact should return failure if response is not expected
    // TODO: fix logic with respect to is_expecting_response, implement wait_for_wakeup();
    if (HAILO_SUCCESS == status) {
        status = Control::parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header,
            &payload, &request);
        CHECK_SUCCESS(status);
        CHECK(is_expecting_response, HAILO_INTERNAL_FAILURE, "Recived valid response from FW for control who is not expecting one.");
    } else if ((HAILO_FW_CONTROL_FAILURE == status) && (!is_expecting_response)){
        status = HAILO_SUCCESS;
    } else {
        return status;
    }

    LOGGER__DEBUG("Board has been reset successfully");
    return HAILO_SUCCESS;
}

} /* namespace hailort */
