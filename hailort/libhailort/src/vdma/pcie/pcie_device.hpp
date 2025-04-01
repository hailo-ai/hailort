/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_device.hpp
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

class PcieDevice : public VdmaDevice {
public:
    static Expected<std::vector<hailo_pcie_device_info_t>> scan();
    static Expected<std::unique_ptr<Device>> create();
    static Expected<std::unique_ptr<Device>> create(const hailo_pcie_device_info_t &device_info);
    static Expected<hailo_pcie_device_info_t> parse_pcie_device_info(const std::string &device_info_str,
        bool log_on_failure);
    static Expected<std::string> pcie_device_info_to_string(const hailo_pcie_device_info_t &device_info);
    static bool pcie_device_infos_equal(const hailo_pcie_device_info_t &first, const hailo_pcie_device_info_t &second);

    virtual ~PcieDevice() = default;

    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) override;
    virtual hailo_status direct_write_memory(uint32_t address, const void *buffer, uint32_t size) override;
    virtual hailo_status direct_read_memory(uint32_t address, void *buffer, uint32_t size) override;
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
    static Expected<std::vector<hailo_pcie_device_info_t>> get_pcie_devices_infos(const std::vector<HailoRTDriver::DeviceInfo> &scan_results);

    PcieDevice(std::unique_ptr<HailoRTDriver> &&driver, hailo_status &status);

    static Expected<HailoRTDriver::DeviceInfo> find_device_info(const hailo_pcie_device_info_t &pcie_device_info);
};

} /* namespace hailort */

#endif /* HAILO_PCIE_DEVICE_H_ */
