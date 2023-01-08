/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file eth_device.hpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#ifndef HAILO_ETH_DEVICE_H_
#define HAILO_ETH_DEVICE_H_

#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include "device_internal.hpp"
#include "udp.hpp"
#include "context_switch/single_context/hcp_config_network_group.hpp"

namespace hailort
{

class EthernetDevice : public DeviceBase {
public:
    virtual hailo_status fw_interact_impl(uint8_t *request_buffer, size_t request_size,
        uint8_t *response_buffer, size_t *response_size, hailo_cpu_id_t cpu_id) override;
    virtual Expected<size_t> read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id) override;
    virtual hailo_status wait_for_wakeup() override;
    virtual void increment_control_sequence() override;
    virtual hailo_reset_device_mode_t get_default_reset_mode() override;
    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) override;

    virtual bool is_stream_interface_supported(const hailo_stream_interface_t &stream_interface) const override
    {
        switch (stream_interface) {
        case HAILO_STREAM_INTERFACE_PCIE:
        case HAILO_STREAM_INTERFACE_CORE:
            return false;
        case HAILO_STREAM_INTERFACE_ETH:
        case HAILO_STREAM_INTERFACE_MIPI:
            return true;
        default:
            LOGGER__ERROR("Invalid stream interface");
            return false;
        }
    }

    static Expected<std::vector<hailo_eth_device_info_t>> scan(const std::string &interface_name,
        std::chrono::milliseconds timeout);
    static Expected<std::vector<hailo_eth_device_info_t>> scan_by_host_address(const std::string &host_address,
        std::chrono::milliseconds timeout);
    static Expected<hailo_eth_device_info_t> parse_eth_device_info(const std::string &ip_addr, bool log_on_failure);

    static Expected<std::unique_ptr<EthernetDevice>> create(const hailo_eth_device_info_t &device_info);
    static Expected<std::unique_ptr<EthernetDevice>> create(const std::string &ip_addr);
    virtual Expected<hailo_device_architecture_t> get_architecture() const override;
    hailo_eth_device_info_t get_device_info() const;
    virtual const char* get_dev_id() const override;

protected:
    virtual Expected<D2H_EVENT_MESSAGE_t> read_notification() override;
    virtual hailo_status disable_notifications() override;
    virtual Expected<ConfiguredNetworkGroupVector> add_hef(Hef &hef, const NetworkGroupsParamsMap &configure_params) override;

private:
    EthernetDevice(const hailo_eth_device_info_t &device_info, Udp &&control_udp, hailo_status &status);

    const hailo_eth_device_info_t m_device_info;
    std::string m_device_id;
    Udp m_control_udp;
    std::vector<std::shared_ptr<HcpConfigNetworkGroup>> m_network_groups;
    ActiveNetGroupHolder m_active_net_group_holder;
};

} /* namespace hailort */

#endif /* HAILO_ETH_DEVICE_H_ */