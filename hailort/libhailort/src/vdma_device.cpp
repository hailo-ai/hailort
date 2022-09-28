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
        CHECK_EXPECTED(device);;
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
    return m_driver.read_notification();
}

hailo_status VdmaDevice::disable_notifications()
{
    return m_driver.disable_notifications();
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
