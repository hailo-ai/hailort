/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_device.hpp
 * @brief Base class for devices that uses vdma and comunicate using HailoRTDriver
 *
 **/

#ifndef HAILO_VDMA_DEVICE_H_
#define HAILO_VDMA_DEVICE_H_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "device_internal.hpp"
#include "os/hailort_driver.hpp"

namespace hailort
{

class VdmaDevice : public DeviceBase {
public:

    virtual ~VdmaDevice() = default;

    virtual hailo_status wait_for_wakeup() override;
    virtual void increment_control_sequence() override;
    virtual hailo_reset_device_mode_t get_default_reset_mode() override;
    uint16_t get_default_desc_page_size() const;

    hailo_status mark_as_used();
    virtual Expected<size_t> read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id) override;

    HailoRTDriver &get_driver() {
        return std::ref(m_driver);
    };

protected:
    VdmaDevice(HailoRTDriver &&driver, Type type);

    virtual Expected<D2H_EVENT_MESSAGE_t> read_notification() override;
    virtual hailo_status disable_notifications() override;
    virtual ExpectedRef<ConfigManager> get_config_manager() override;

    HailoRTDriver m_driver;
    std::unique_ptr<ConfigManager> m_context_switch_manager;
};

} /* namespace hailort */

#endif /* HAILO_VDMA_DEVICE_H_ */
