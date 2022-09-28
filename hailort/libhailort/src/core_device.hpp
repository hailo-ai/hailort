/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file core_device
 * @brief Device used by Hailo-15
 *
 **/

#ifndef _HAILO_CORE_DEVICE_HPP_
#define _HAILO_CORE_DEVICE_HPP_

#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include "vdma_device.hpp"

#include <memory>

namespace hailort
{

class CoreDevice : public VdmaDevice {
public:
    virtual ~CoreDevice();
    static bool is_loaded();
    static Expected<std::unique_ptr<CoreDevice>> create();

    virtual Expected<hailo_device_architecture_t> get_architecture() const override;
    virtual const char* get_dev_id() const override {return DEVICE_ID;}
    Expected<size_t> read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id);

    virtual bool is_stream_interface_supported(const hailo_stream_interface_t &stream_interface) const override
    {
        switch (stream_interface) {
        case HAILO_STREAM_INTERFACE_CORE:
            return true;
        case HAILO_STREAM_INTERFACE_PCIE:
        case HAILO_STREAM_INTERFACE_ETH:
        case HAILO_STREAM_INTERFACE_MIPI:
            return false;
        default:
            LOGGER__ERROR("Invalid stream interface");
            return false;
        }
    }

    static constexpr const char *DEVICE_ID = "[core]";

protected:
    virtual hailo_status fw_interact_impl(uint8_t *request_buffer, size_t request_size,
        uint8_t *response_buffer, size_t *response_size, hailo_cpu_id_t cpu_id) override;
    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) override;
    virtual ExpectedRef<ConfigManager> get_config_manager() override;

private:
    CoreDevice(HailoRTDriver &&driver, hailo_status &status);

    // TODO: (HRT-7535) This member needs to be held in the object that impls fw_interact_impl func,
    //       because VdmaConfigManager calls a control (which in turn calls fw_interact_impl).
    //       (otherwise we'll get a "pure virtual method called" runtime error in the Device's dtor)
    //       Once we merge CoreDevice::fw_interact_impl and PcieDevice::fw_interact_impl we can
    //       move the m_context_switch_manager member and get_config_manager() func to VdmaDevice.
    std::unique_ptr<ConfigManager> m_context_switch_manager;
};


} /* namespace hailort */

#endif /* _HAILO_CORE_DEVICE_HPP_ */