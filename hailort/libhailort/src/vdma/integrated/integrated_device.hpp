/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file integrated_device
 * @brief Device used by Hailo-15
 *
 **/

#ifndef _HAILO_INTEGRATED_DEVICE_HPP_
#define _HAILO_INTEGRATED_DEVICE_HPP_

#include "hailo/expected.hpp"
#include "hailo/hailort.h"

#include "vdma/vdma_device.hpp"
#include "vdma/memory/continuous_buffer.hpp"

#include <memory>


namespace hailort
{

class IntegratedDevice : public VdmaDevice {
public:
    static bool is_loaded();
    static Expected<std::unique_ptr<IntegratedDevice>> create();

    virtual ~IntegratedDevice() = default;

    Expected<size_t> read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id);

    virtual bool is_stream_interface_supported(const hailo_stream_interface_t &stream_interface) const override
    {
        switch (stream_interface) {
        case HAILO_STREAM_INTERFACE_INTEGRATED:
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

    static constexpr const char *DEVICE_ID = HailoRTDriver::INTEGRATED_NNC_DEVICE_ID;

    Expected<std::pair<void*, uint64_t>> allocate_infinite_action_list_buffer(size_t size);

protected:
    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) override;

private:
    IntegratedDevice(std::unique_ptr<HailoRTDriver> &&driver, vdma::ContinuousBuffer &&pool, hailo_status &status);

    vdma::ContinuousBuffer m_device_infinite_action_list_pool;
    size_t m_device_infinite_action_list_pool_allocation_offset;
};


} /* namespace hailort */

#endif /* _HAILO_INTEGRATED_DEVICE_HPP_ */