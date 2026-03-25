/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file legacy_pcie_device.hpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#ifndef HAILO_PCIE_DEVICE_H_
#define HAILO_PCIE_DEVICE_H_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "vdma/vdma_device.hpp"


namespace hailort
{

class LegacyPcieDevice : public VdmaDevice {
public:
    static Expected<std::unique_ptr<Device>> create();
    static Expected<std::unique_ptr<Device>> create(const hailo_pcie_device_info_t &device_info);

    virtual ~LegacyPcieDevice() = default;

    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) override;
    virtual bool is_stream_interface_supported(const hailo_stream_interface_t& stream_interface) const override
    {
        switch (stream_interface) {
        case HAILO_STREAM_INTERFACE_ETH:
        case HAILO_STREAM_INTERFACE_INTEGRATED:
            return false;
        case HAILO_STREAM_INTERFACE_PCIE:
        case HAILO_STREAM_INTERFACE_MIPI:
            return true;
        default:
            LOGGER__ERROR("Invalid stream interface");
            return false;
        }
    }

    // TODO: used for tests
    void set_is_control_version_supported(bool value);
    virtual Expected<hailo_device_architecture_t> get_architecture() const override;

private:
    LegacyPcieDevice(std::unique_ptr<HailoRTDriver> &&driver, hailo_status &status);

    static Expected<HailoRTDriver::DeviceInfo> find_device_info(const hailo_pcie_device_info_t &pcie_device_info);
};

} /* namespace hailort */

#endif /* HAILO_PCIE_DEVICE_H_ */
