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
    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) override;

private:
    CoreDevice(HailoRTDriver &&driver, hailo_status &status);
};


} /* namespace hailort */

#endif /* _HAILO_CORE_DEVICE_HPP_ */