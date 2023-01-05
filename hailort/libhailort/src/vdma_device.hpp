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
#include "context_switch/network_group_internal.hpp"
#include "os/hailort_driver.hpp"

namespace hailort
{

class VdmaDevice : public DeviceBase {
public:
    static Expected<std::unique_ptr<VdmaDevice>> create(const std::string &device_id);

    virtual ~VdmaDevice();

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
    VdmaDevice(HailoRTDriver &&driver, Type type, const std::string &device_id);

    virtual Expected<D2H_EVENT_MESSAGE_t> read_notification() override;
    virtual hailo_status disable_notifications() override;
    virtual hailo_status fw_interact_impl(uint8_t *request_buffer, size_t request_size,
        uint8_t *response_buffer, size_t *response_size, hailo_cpu_id_t cpu_id) override;
    virtual Expected<ConfiguredNetworkGroupVector> add_hef(Hef &hef, const NetworkGroupsParamsMap &configure_params) override;

    HailoRTDriver m_driver;
    std::vector<std::shared_ptr<VdmaConfigNetworkGroup>> m_network_groups;
    ActiveNetGroupHolder m_active_net_group_holder;
    bool m_is_configured;

private:
    Expected<std::shared_ptr<ConfiguredNetworkGroup>> create_configured_network_group(
        const std::vector<std::shared_ptr<NetworkGroupMetadata>> &network_group_metadatas,
        Hef &hef, const ConfigureNetworkParams &config_params,
        uint8_t network_group_index);
};

} /* namespace hailort */

#endif /* HAILO_VDMA_DEVICE_H_ */
