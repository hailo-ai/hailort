/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file eth_device.hpp
 * @brief EthernetDevice class.
 *
 * DEPRECATED: ethernet/UDP firmware control is no longer supported. This class
 * is retained as a header-only stub so that legacy call sites continue to
 * compile; every method now returns HAILO_NOT_SUPPORTED. The class will be
 * removed in a future release.
 **/

#ifndef HAILO_ETH_DEVICE_H_
#define HAILO_ETH_DEVICE_H_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "device_common/device_internal.hpp"
#include "eth/udp.hpp"
#include "eth/hcp_config_core_op.hpp"


namespace hailort
{

// DEPRECATED: ethernet/UDP fw-control is no longer supported; all methods are stubs.
class EthernetDevice : public DeviceBase {
public:
    virtual hailo_status fw_interact_impl(uint8_t *request_buffer, size_t request_size, uint8_t *response_buffer) override
    {
        (void)request_buffer;
        (void)request_size;
        (void)response_buffer;
        return HAILO_NOT_SUPPORTED;
    }

    virtual Expected<size_t> read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id) override
    {
        (void)buffer;
        (void)cpu_id;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual hailo_status wait_for_wakeup() override
    {
        return HAILO_NOT_SUPPORTED;
    }

    virtual void shutdown_core_ops() override {}

    virtual hailo_reset_device_mode_t get_default_reset_mode() override
    {
        return HAILO_RESET_DEVICE_MODE_CHIP;
    }

    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) override
    {
        (void)reset_type;
        return HAILO_NOT_SUPPORTED;
    }

    virtual bool is_stream_interface_supported(const hailo_stream_interface_t &stream_interface) const override
    {
        (void)stream_interface;
        return false;
    }

    static Expected<std::vector<hailo_eth_device_info_t>> scan(const std::string &interface_name,
        std::chrono::milliseconds timeout)
    {
        (void)interface_name;
        (void)timeout;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    static Expected<std::vector<hailo_eth_device_info_t>> scan_by_host_address(const std::string &host_address,
        std::chrono::milliseconds timeout)
    {
        (void)host_address;
        (void)timeout;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    static Expected<hailo_eth_device_info_t> parse_eth_device_info(const std::string &ip_addr, bool log_on_failure)
    {
        (void)ip_addr;
        (void)log_on_failure;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    static Expected<std::unique_ptr<EthernetDevice>> create(const hailo_eth_device_info_t &device_info)
    {
        (void)device_info;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    static Expected<std::unique_ptr<EthernetDevice>> create(const std::string &ip_addr)
    {
        (void)ip_addr;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    hailo_eth_device_info_t get_device_info() const
    {
        return {};
    }

    virtual const char* get_dev_id() const override
    {
        return "";
    }

protected:
    virtual Expected<D2H_EVENT_MESSAGE_t> read_notification() override
    {
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

    virtual hailo_status disable_notifications() override
    {
        return HAILO_NOT_SUPPORTED;
    }

    virtual Expected<ConfiguredNetworkGroupVector> add_hef(Hef &hef, const NetworkGroupsParamsMap &configure_params) override
    {
        (void)hef;
        (void)configure_params;
        return make_unexpected(HAILO_NOT_SUPPORTED);
    }

private:
    EthernetDevice(const hailo_eth_device_info_t &device_info, Udp &&control_udp, hailo_status &status) :
        DeviceBase::DeviceBase(Device::Type::ETH)
    {
        (void)device_info;
        (void)control_udp;
        status = HAILO_NOT_SUPPORTED;
    }
};

} /* namespace hailort */

#endif /* HAILO_ETH_DEVICE_H_ */
