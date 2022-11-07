/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_device.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "vdma_device.hpp"
#include "vdma_descriptor_list.hpp"
#include "context_switch/multi_context/vdma_config_manager.hpp"
#include "pcie_device.hpp"
#include "core_device.hpp"


#include <new>
#include <algorithm>

namespace hailort
{

#ifndef HAILO_EMULATOR
static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT(1000);
#else /* ifndef HAILO_EMULATOR */
static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT(50000);
#endif /* ifndef HAILO_EMULATOR */

VdmaDevice::VdmaDevice(HailoRTDriver &&driver, Device::Type type) :
    DeviceBase::DeviceBase(type),
    m_driver(std::move(driver))
{
}

Expected<std::unique_ptr<VdmaDevice>> VdmaDevice::create(const std::string &device_id)
{
    const bool DONT_LOG_ON_FAILURE = false;
    if (CoreDevice::DEVICE_ID == device_id) {
        auto device = CoreDevice::create();
        CHECK_EXPECTED(device);;
        return std::unique_ptr<VdmaDevice>(device.release());
    }
    else if (auto pcie_info = PcieDevice::parse_pcie_device_info(device_id, DONT_LOG_ON_FAILURE)) {
        auto device = PcieDevice::create(pcie_info.release());
        CHECK_EXPECTED(device);
        return std::unique_ptr<VdmaDevice>(device.release());
    }
    else {
        LOGGER__ERROR("Invalid device id {}", device_id);
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

hailo_status VdmaDevice::wait_for_wakeup()
{
    return HAILO_SUCCESS;
}

Expected<D2H_EVENT_MESSAGE_t> VdmaDevice::read_notification()
{
    auto notification_buffer = m_driver.read_notification();
    if (!notification_buffer.has_value()) {
        return make_unexpected(notification_buffer.status());
    }

    D2H_EVENT_MESSAGE_t notification;
    CHECK_AS_EXPECTED(sizeof(notification) >= notification_buffer->size(), HAILO_GET_D2H_EVENT_MESSAGE_FAIL,
        "buffer len is not valid = {}", notification_buffer->size());
    memcpy(&notification, notification_buffer->data(), notification_buffer->size());
    return notification;
}

hailo_status VdmaDevice::disable_notifications()
{
    return m_driver.disable_notifications();
}

hailo_status VdmaDevice::fw_interact_impl(uint8_t *request_buffer, size_t request_size,
        uint8_t *response_buffer, size_t *response_size, hailo_cpu_id_t cpu_id)
{
    uint8_t request_md5[PCIE_EXPECTED_MD5_LENGTH];
    MD5_CTX ctx;

    MD5_Init(&ctx);
    MD5_Update(&ctx, request_buffer, request_size);
    MD5_Final(request_md5, &ctx);

    uint8_t response_md5[PCIE_EXPECTED_MD5_LENGTH];
    uint8_t expected_response_md5[PCIE_EXPECTED_MD5_LENGTH];

    auto status = m_driver.fw_control(request_buffer, request_size, request_md5,
        response_buffer, response_size, response_md5,
        DEFAULT_TIMEOUT, cpu_id);
    CHECK_SUCCESS(status, "Failed to send fw control");

    MD5_Init(&ctx);
    MD5_Update(&ctx, response_buffer, (*response_size));
    MD5_Final(expected_response_md5, &ctx);

    auto memcmp_result = memcmp(expected_response_md5, response_md5, sizeof(response_md5));
    CHECK(0 == memcmp_result, HAILO_INTERNAL_FAILURE, "MD5 validation of control response failed.");

    return HAILO_SUCCESS;
}

ExpectedRef<ConfigManager> VdmaDevice::get_config_manager()
{
    auto status = mark_as_used();
    CHECK_SUCCESS_AS_EXPECTED(status);

    if (!m_context_switch_manager) {
        auto local_context_switch_manager = VdmaConfigManager::create(*this);
        CHECK_EXPECTED(local_context_switch_manager);

        m_context_switch_manager = make_unique_nothrow<VdmaConfigManager>(local_context_switch_manager.release());
        CHECK_AS_EXPECTED(nullptr != m_context_switch_manager, HAILO_OUT_OF_HOST_MEMORY);
    }

    return std::ref(*m_context_switch_manager);
}

Expected<size_t> VdmaDevice::read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id)
{
    size_t read_bytes = 0;
    hailo_status status = HAILO_UNINITIALIZED;
    status = m_driver.read_log(buffer.data(), buffer.size(), &read_bytes, cpu_id);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return read_bytes;
}

void VdmaDevice::increment_control_sequence()
{
    // To support multiprocess the sequence must remain 0 which is a number the FW ignores.
    // Otherwise the FW might get the same sequence number from several processes which
    // cause the command to be discarded.
    m_control_sequence = 0;
}

hailo_reset_device_mode_t VdmaDevice::get_default_reset_mode()
{
    return HAILO_RESET_DEVICE_MODE_SOFT;
}

uint16_t VdmaDevice::get_default_desc_page_size() const
{
    return m_driver.calc_desc_page_size(DEFAULT_DESC_PAGE_SIZE);
}

hailo_status VdmaDevice::mark_as_used()
{
    return m_driver.mark_as_used();
}

} /* namespace hailort */
