/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#include "hlpcie.hpp"
#include "vdma_channel.hpp"
#include "vdma_device.hpp"

namespace hailort
{

class PcieDevice : public VdmaDevice {
public:
    static Expected<std::vector<hailo_pcie_device_info_t>> scan();
    static Expected<std::unique_ptr<PcieDevice>> create();
    static Expected<std::unique_ptr<PcieDevice>> create(const hailo_pcie_device_info_t &device_info);
    static Expected<hailo_pcie_device_info_t> parse_pcie_device_info(const std::string &device_info_str,
        bool log_on_failure);
    static Expected<std::string> pcie_device_info_to_string(const hailo_pcie_device_info_t &device_info);

    virtual ~PcieDevice();

    virtual hailo_status fw_interact_impl(uint8_t *request_buffer, size_t request_size,
        uint8_t *response_buffer, size_t *response_size, hailo_cpu_id_t cpu_id) override;

    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) override;
    virtual hailo_status direct_write_memory(uint32_t address, const void *buffer, uint32_t size) override;
    virtual hailo_status direct_read_memory(uint32_t address, void *buffer, uint32_t size) override;
    virtual bool is_stream_interface_supported(const hailo_stream_interface_t& stream_interface) const override
    {
        switch (stream_interface) {
        case HAILO_STREAM_INTERFACE_ETH:
        case HAILO_STREAM_INTERFACE_CORE:
            return false;
        case HAILO_STREAM_INTERFACE_PCIE:
        case HAILO_STREAM_INTERFACE_MIPI:
            return true;
        default:
            LOGGER__ERROR("Invalid stream interface");
            return false;
        }
    }
    virtual ExpectedRef<ConfigManager> get_config_manager() override;

    // TODO: used for tests
    void set_is_control_version_supported(bool value);
    virtual Expected<hailo_device_architecture_t> get_architecture() const override;

    const hailo_pcie_device_info_t get_device_info() const
    {
        return m_device_info;
    }
    virtual const char* get_dev_id() const override;

private:
    PcieDevice(HailoRTDriver &&driver, const hailo_pcie_device_info_t &device_info, hailo_status &status);

    hailo_status close_all_vdma_channels();

    bool m_fw_up;
    const hailo_pcie_device_info_t m_device_info;
    std::string m_device_id;
    // TODO: (HRT-7535) This member needs to be held in the object that impls fw_interact_impl func,
    //       because VdmaConfigManager calls a control (which in turn calls fw_interact_impl).
    //       (otherwise we'll get a "pure virtual method called" runtime error in the Device's dtor)
    //       Once we merge CoreDevice::fw_interact_impl and PcieDevice::fw_interact_impl we can
    //       move the m_context_switch_manager member and get_config_manager() func to VdmaDevice.
    std::unique_ptr<ConfigManager> m_context_switch_manager;
};

} /* namespace hailort */

#endif /* HAILO_PCIE_DEVICE_H_ */
