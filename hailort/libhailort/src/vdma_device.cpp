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


#include <new>
#include <algorithm>

namespace hailort
{

VdmaDevice::VdmaDevice(HailoRTDriver &&driver, Device::Type type) :
    DeviceBase::DeviceBase(type),
    m_driver(std::move(driver)),
    m_context_switch_manager(nullptr)
{
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
