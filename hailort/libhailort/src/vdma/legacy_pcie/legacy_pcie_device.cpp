/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_device.cpp
 * @brief Implementation of PCIe device
 **/

#include "vdma/legacy_pcie/legacy_pcie_device.hpp"
#include "device_common/pcie_utils.hpp"
#include "device_common/control.hpp"


namespace hailort
{

Expected<std::unique_ptr<Device>> LegacyPcieDevice::create()
{
    auto scan_results = HailoRTDriver::scan_devices();
    CHECK_EXPECTED(scan_results);
    
    CHECK_AS_EXPECTED(scan_results->size() >= 1, HAILO_INVALID_OPERATION,
        "There are no PCIe devices on the system");
    if (scan_results->size() > 1) {
        auto first_acc_type = scan_results->at(0).device_type;
        for (const auto &scan_result : scan_results.value()) {
            CHECK_AS_EXPECTED(first_acc_type == scan_result.device_type, HAILO_INVALID_OPERATION,
                "Multiple accelerator types detected (Hailo8, Hailo10). Please specify the device to use.");
        }
    }

    // choose first device
    TRY(auto pcie_info, PcieUtils::parse_pcie_device_info(scan_results->at(0).device_id));
    return create(pcie_info);
}

Expected<std::unique_ptr<Device>> LegacyPcieDevice::create(const hailo_pcie_device_info_t &pcie_device_info)
{
    auto device_info = find_device_info(pcie_device_info);
    CHECK_EXPECTED(device_info);

    CHECK(HailoRTDriver::DeviceType::INTEGRATED == device_info->device_type, HAILO_INVALID_OPERATION,
        "PCIe devices are only supported for Hailo-8 devices. Use the Device::create() method instead.");

    auto driver = HailoRTDriver::create(device_info->device_id, device_info->dev_path);
    CHECK_EXPECTED(driver);

    hailo_status status = HAILO_UNINITIALIZED;
    auto pcie_device = std::unique_ptr<LegacyPcieDevice>(new (std::nothrow) LegacyPcieDevice(driver.release(), status));
    CHECK_NOT_NULL_AS_EXPECTED(pcie_device, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating LegacyPcieDevice");

    // Check if the device is supported
    TRY(auto device_arch, pcie_device->get_architecture());
    CHECK((device_arch != HAILO_ARCH_HAILO8_A0) && (device_arch != HAILO_ARCH_HAILO8) && (device_arch != HAILO_ARCH_HAILO8L), 
        HAILO_NOT_SUPPORTED, "Hailo8 devices are only supported in versions 4.x.x and earlier");

    // Upcasting to Device unique_ptr (from LegacyPcieDevice unique_ptr)
    auto device = std::unique_ptr<Device>(std::move(pcie_device));
    return device;
}

LegacyPcieDevice::LegacyPcieDevice(std::unique_ptr<HailoRTDriver> &&driver, hailo_status &status) :
    VdmaDevice(std::move(driver), Device::Type::PCIE, status)
{
    if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed to create VdmaDevice");
        return;
    }

    if (m_driver->is_fw_loaded()) {
        status = update_fw_state();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("update_fw_state() failed with status {}", status);
            return;
        }
    } else {
        LOGGER__WARNING("FW is not loaded to the device. Please load FW before using the device.");
        m_is_control_version_supported = false;
    }

    status = set_default_notification_callbacks();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to set default notification callbacks for LegacyPcieDevice: {}", status);
        return;
    }

    status = HAILO_SUCCESS;
}

void LegacyPcieDevice::set_is_control_version_supported(bool value)
{
    m_is_control_version_supported = value;
}

Expected<hailo_device_architecture_t> LegacyPcieDevice::get_architecture() const
{
    if (!m_driver->is_fw_loaded()) {
        LOGGER__WARNING("FW is not loaded to the device. Please load FW before using the device.");
        return make_unexpected(HAILO_INVALID_OPERATION);
    }

    return Expected<hailo_device_architecture_t>(m_device_architecture);
}

hailo_status LegacyPcieDevice::reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type)
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
            &payload, &request, *this);
        CHECK_SUCCESS(status);
        CHECK(is_expecting_response, HAILO_INTERNAL_FAILURE, "Recived valid response from FW for control who is not expecting one.");
    } else if ((HAILO_DRIVER_TIMEOUT == status) && (!is_expecting_response)){
        status = HAILO_SUCCESS;
    } else {
        return status;
    }

    LOGGER__DEBUG("Board has been reset successfully");
    return HAILO_SUCCESS;
}

Expected<HailoRTDriver::DeviceInfo> LegacyPcieDevice::find_device_info(const hailo_pcie_device_info_t &pcie_device_info)
{
    auto scan_results = HailoRTDriver::scan_devices();
    CHECK_EXPECTED(scan_results);

    // Find device index based on the information from "device_info"
    for (const auto &scan_result : scan_results.value()) {
        auto scanned_info = PcieUtils::parse_pcie_device_info(scan_result.device_id);
        if (!scanned_info) {
            continue;
        }

        const bool match = (pcie_device_info.bus == scanned_info->bus) &&
           (pcie_device_info.device == scanned_info->device) &&
           (pcie_device_info.func == scanned_info->func) &&
           ((HAILO_PCIE_ANY_DOMAIN == pcie_device_info.domain) || (pcie_device_info.domain == scanned_info->domain));
        if (match) {
            return HailoRTDriver::DeviceInfo(scan_result);
        }
    }

    LOGGER__ERROR("Requested device not found");
    return make_unexpected(HAILO_INVALID_ARGUMENT);
}

} /* namespace hailort */
