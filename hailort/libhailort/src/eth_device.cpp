/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file eth_device.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "eth_device.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "hailo/device.hpp"
#include "control.hpp"
#include "udp.hpp"
#include "common/ethernet_utils.hpp"
#include "hailo/hef.hpp"

#include <stdlib.h>
#include <errno.h>
#include <new>
#include <array>

namespace hailort
{

#define SCAN_SEQUENCE (0)
#define WAIT_FOR_DEVICE_WAKEUP_MAX_ATTEMPTS (10)
#define WAIT_FOR_DEVICE_WAKEUP_TIMEOUT (1000)
#define ETH_BROADCAST_IP ("255.255.255.255")


hailo_status EthernetDevice::fw_interact_impl(uint8_t *request_buffer, size_t request_size,
    uint8_t *response_buffer, size_t *response_size, hailo_cpu_id_t cpu_id)
{   
    /* CPU id is used only in PCIe, for Eth all control goes to APP CPU.*/
    (void)cpu_id;
    return m_control_udp.fw_interact(request_buffer, request_size, response_buffer, response_size, m_control_sequence);
}

hailo_status EthernetDevice::wait_for_wakeup()
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    
    /* Create udp socket */
    auto udp = Udp::create(m_device_info.device_address.sin_addr, m_device_info.device_address.sin_port,
                       m_device_info.host_address.sin_addr, m_device_info.host_address.sin_port);
    CHECK_EXPECTED_AS_STATUS(udp);

    status = udp->set_timeout(std::chrono::milliseconds(WAIT_FOR_DEVICE_WAKEUP_TIMEOUT));
    CHECK_SUCCESS(status);

    status = udp->set_max_number_of_attempts(WAIT_FOR_DEVICE_WAKEUP_MAX_ATTEMPTS);
    CHECK_SUCCESS(status);

    /* Create and send identify-control until it runs successfully */
    common_status = CONTROL_PROTOCOL__pack_identify_request(&request, &request_size, m_control_sequence);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);
    
    status = udp->fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size,
        m_control_sequence);

    // Always increment sequence
    m_control_sequence = (m_control_sequence + 1) % CONTROL__MAX_SEQUENCE;
    CHECK_SUCCESS(status);

    /* Parse and validate the response */
    return Control::parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header, &payload, &request);
}

Expected<std::unique_ptr<EthernetDevice>> EthernetDevice::create(const hailo_eth_device_info_t &device_info)
{
    hailo_status status = HAILO_UNINITIALIZED;

    // Creates control socket
    auto udp = Udp::create(device_info.device_address.sin_addr, device_info.device_address.sin_port,
                       device_info.host_address.sin_addr, device_info.host_address.sin_port);
    CHECK_EXPECTED(udp, "Failed to init control socket.");

    auto device = std::unique_ptr<EthernetDevice>(new (std::nothrow) EthernetDevice(device_info, udp.release(), status));
    CHECK_AS_EXPECTED((nullptr != device), HAILO_OUT_OF_HOST_MEMORY);

    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed creating EthernetDevice");
        return make_unexpected(status);
    }

    return device;
}

Expected<std::unique_ptr<EthernetDevice>> EthernetDevice::create(const std::string &ip_addr)
{
    const bool LOG_ON_FAILURE = true;
    auto device_info = parse_eth_device_info(ip_addr, LOG_ON_FAILURE);
    CHECK_EXPECTED(device_info, "Failed to parse ip address {}", ip_addr);
    return create(device_info.release());
}

EthernetDevice::EthernetDevice(const hailo_eth_device_info_t &device_info, Udp &&control_udp, hailo_status &status) :
    DeviceBase::DeviceBase(Device::Type::ETH),
    m_device_info(device_info),
    m_control_udp(std::move(control_udp))
{
    char ip_buffer[INET_ADDRSTRLEN];
    status = Socket::ntop(AF_INET, &(device_info.device_address.sin_addr), ip_buffer, INET_ADDRSTRLEN);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Socket::ntop() failed with status {}", status);
        return;
    }
    m_device_id = std::string(ip_buffer);

    status = m_control_udp.set_timeout(std::chrono::milliseconds(m_device_info.timeout_millis));
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to init set timeout for control socket.");
        return;
    }

    status = m_control_udp.set_max_number_of_attempts(m_device_info.max_number_of_attempts);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to init set max_number_of_attempts for control socket.");
        return;
    }

    status = update_fw_state();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("update_fw_state() failed with status {}", status);
        return;
    }

    status = HAILO_SUCCESS;
}

Expected<size_t> EthernetDevice::read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id)
{
    (void) buffer;
    (void) cpu_id;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

static void eth_device__fill_eth_device_info(Udp &udp, hailo_eth_device_info_t *eth_device_info)
{
    eth_device_info->device_address.sin_family = AF_INET;
    eth_device_info->device_address.sin_addr = udp.m_device_address.sin_addr;
    eth_device_info->device_address.sin_port = HAILO_DEFAULT_ETH_CONTROL_PORT;

    eth_device_info->host_address.sin_family = AF_INET;
    eth_device_info->host_address.sin_addr.s_addr = INADDR_ANY;
    eth_device_info->host_address.sin_port = HAILO_ETH_PORT_ANY;

    eth_device_info->max_number_of_attempts = HAILO_DEFAULT_ETH_MAX_NUMBER_OF_RETRIES;
    eth_device_info->max_payload_size = HAILO_DEFAULT_ETH_MAX_PAYLOAD_SIZE;
    eth_device_info->timeout_millis = HAILO_DEFAULT_ETH_SCAN_TIMEOUT_MS;

    char textual_ip_address[INET_ADDRSTRLEN];
    auto inet = inet_ntop(AF_INET, &(udp.m_device_address.sin_addr), textual_ip_address, INET_ADDRSTRLEN);
    if (NULL != inet) {
        LOGGER__DEBUG("Found Hailo device: {}", textual_ip_address);
    }
}

static Expected<hailo_eth_device_info_t> eth_device__handle_available_data(Udp &udp)
{
    hailo_status status = HAILO_UNINITIALIZED;

    /* Try to receive data from the udp socket and log timeouts in debug level */
    status = udp.has_data(true);
    if (HAILO_TIMEOUT == status) {
        LOGGER__DEBUG("Scan timeout");
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    hailo_eth_device_info_t device_info{};
    eth_device__fill_eth_device_info(udp, &device_info);
    
    return device_info;
}

static Expected<std::vector<hailo_eth_device_info_t>> eth_device__receive_responses(Udp &udp)
{
    std::vector<hailo_eth_device_info_t> results;
    while (true) {
        auto next_device_info = eth_device__handle_available_data(udp);
        if (next_device_info.has_value()) {
            results.emplace_back(next_device_info.release());
        } else if (HAILO_TIMEOUT == next_device_info.status()) {
            // We excpect to stop receiving data due to timeout
            break;
        } else {
            // Any other reason indicates a problem
            return make_unexpected(next_device_info.status());
        }
    }

    return results;
}

Expected<std::vector<hailo_eth_device_info_t>> EthernetDevice::scan(const std::string &interface_name,
    std::chrono::milliseconds timeout)
{
    // Convert interface name to IP address
    std::array<char, IPV4_STRING_MAX_LENGTH> interface_ip_address{};
    auto status = EthernetUtils::get_ip_from_interface(interface_name.c_str(), interface_ip_address.data(), interface_ip_address.size());
    CHECK_SUCCESS_AS_EXPECTED(status);

    return scan_by_host_address(interface_ip_address.data(), timeout);
}

hailo_status get_udp_broadcast_params(const char *host_address, struct in_addr &interface_ip_address,
    struct in_addr &broadcast_ip_address)
{
    assert(nullptr != host_address);

    auto status = Socket::pton(AF_INET, host_address, &interface_ip_address);
    CHECK_SUCCESS(status, "Invalid host ip address {}", host_address);
    status = Socket::pton(AF_INET, ETH_BROADCAST_IP, &broadcast_ip_address);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<std::vector<hailo_eth_device_info_t>> EthernetDevice::scan_by_host_address(const std::string &host_address,
    std::chrono::milliseconds timeout)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request{};
    size_t request_size = 0;
    uint32_t sequence = SCAN_SEQUENCE;
    struct in_addr broadcast_ip_address{};
    struct in_addr interface_ip_address{};

    status = get_udp_broadcast_params(host_address.c_str(), interface_ip_address, broadcast_ip_address);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* Create broadcast udp object */
    auto udp_broadcast = Udp::create(broadcast_ip_address, HAILO_DEFAULT_ETH_CONTROL_PORT, interface_ip_address, 0);
    CHECK_EXPECTED(udp_broadcast);
    status = udp_broadcast->set_timeout(timeout);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* Build identify request */
    common_status = CONTROL_PROTOCOL__pack_identify_request(&request, &request_size, sequence);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* Send broadcast identify request */
    status = udp_broadcast->send((uint8_t *)&request, &request_size, false, MAX_UDP_PAYLOAD_SIZE);
    CHECK_SUCCESS_AS_EXPECTED(status);

    /* Receive all responses */
    return eth_device__receive_responses(*udp_broadcast);
}

Expected<hailo_eth_device_info_t> EthernetDevice::parse_eth_device_info(const std::string &ip_addr,
    bool log_on_failure)
{
    hailo_eth_device_info_t device_info{};

    device_info.host_address.sin_family = AF_INET;
    device_info.host_address.sin_port = HAILO_ETH_PORT_ANY;

    auto status = Socket::pton(AF_INET, HAILO_ETH_ADDRESS_ANY, &(device_info.host_address.sin_addr));
    CHECK_SUCCESS_AS_EXPECTED(status);

    device_info.device_address.sin_family = AF_INET;
    device_info.device_address.sin_port = HAILO_DEFAULT_ETH_CONTROL_PORT;
    status = Socket::pton(AF_INET, ip_addr.c_str(), &(device_info.device_address.sin_addr));
    if (status != HAILO_SUCCESS) {
        if (log_on_failure) {
            LOGGER__ERROR("Invalid ip address {}", ip_addr);
        }
        return make_unexpected(status);
    }

    device_info.timeout_millis = HAILO_DEFAULT_ETH_SCAN_TIMEOUT_MS;
    device_info.max_number_of_attempts = HAILO_DEFAULT_ETH_MAX_NUMBER_OF_RETRIES;
    device_info.max_payload_size = HAILO_DEFAULT_ETH_MAX_PAYLOAD_SIZE;

    return device_info;
}

void EthernetDevice::increment_control_sequence()
{
    m_control_sequence = (m_control_sequence + 1) % CONTROL__MAX_SEQUENCE;
}

hailo_reset_device_mode_t EthernetDevice::get_default_reset_mode()
{
    return HAILO_RESET_DEVICE_MODE_CHIP;
}

hailo_status EthernetDevice::reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type)
{
    hailo_status status = HAILO_UNINITIALIZED;
    HAILO_COMMON_STATUS_t common_status = HAILO_COMMON_STATUS__UNINITIALIZED;
    CONTROL_PROTOCOL__request_t request = {};
    size_t request_size = 0;
    uint8_t response_buffer[RESPONSE_MAX_BUFFER_SIZE] = {};
    size_t response_size = RESPONSE_MAX_BUFFER_SIZE;
    CONTROL_PROTOCOL__response_header_t *header = NULL;
    CONTROL_PROTOCOL__payload_t *payload = NULL;
    bool is_expecting_response = true;

    switch (reset_type) {
        case CONTROL_PROTOCOL__RESET_TYPE__CHIP:
            is_expecting_response = false;
            break;
        case CONTROL_PROTOCOL__RESET_TYPE__SOFT:
            /* Fallthrough */
        case CONTROL_PROTOCOL__RESET_TYPE__FORCED_SOFT:
            is_expecting_response = false; // TODO: Check boot source, set is_expecting_response = (boot_source != pcie)
            break;
        default:
            is_expecting_response = true;
            break;
    }

    common_status = CONTROL_PROTOCOL__pack_reset_request(&request, &request_size, m_control_sequence, reset_type);
    status = (HAILO_COMMON_STATUS__SUCCESS == common_status) ? HAILO_SUCCESS : HAILO_INTERNAL_FAILURE;
    CHECK_SUCCESS(status);

    /* On non-reponse controls we set the response_size to 0 */
    if (!is_expecting_response) {
        response_size = 0;
    }

    LOGGER__DEBUG("Sending reset request");
    status = this->fw_interact((uint8_t*)(&request), request_size, (uint8_t*)&response_buffer, &response_size);
    // fw_interact should return success even if response is not expected
    CHECK_SUCCESS(status);

    /* Parse response if expected */
    // TODO: fix logic with respect to is_expecting_response
    if (0 != response_size) {
        status = Control::parse_and_validate_response(response_buffer, (uint32_t)(response_size), &header,
            &payload, &request);
        CHECK_SUCCESS(status);
        CHECK(is_expecting_response, HAILO_INTERNAL_FAILURE,
            "Recived valid response from FW for control who is not expecting one.");
    } else {
        status = this->wait_for_wakeup();
        CHECK_SUCCESS(status);
    }

    LOGGER__DEBUG("Board has been reset successfully");
    return HAILO_SUCCESS;
}

Expected<hailo_device_architecture_t> EthernetDevice::get_architecture() const
{
    // FW is always up if we got here (EthernetDevice's ctor would fail otherwise)
    // Hence, just return it
    return Expected<hailo_device_architecture_t>(m_device_architecture);
}

hailo_eth_device_info_t EthernetDevice::get_device_info() const
{
    return m_device_info;
}

const char *EthernetDevice::get_dev_id() const
{
    return m_device_id.c_str();
}

Expected<D2H_EVENT_MESSAGE_t> EthernetDevice::read_notification()
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status EthernetDevice::disable_notifications()
{
    return HAILO_NOT_IMPLEMENTED;
}

Expected<ConfiguredNetworkGroupVector> EthernetDevice::add_hef(Hef &hef, const NetworkGroupsParamsMap &configure_params)
{
    auto device_arch_exp = get_architecture();
    CHECK_EXPECTED(device_arch_exp);
    auto device_arch = device_arch_exp.release();

    auto partial_clusters_layout_bitmap_exp = Control::get_partial_clusters_layout_bitmap(*this);
    CHECK_EXPECTED(partial_clusters_layout_bitmap_exp);
    auto partial_clusters_layout_bitmap = partial_clusters_layout_bitmap_exp.release();

    auto &hef_network_groups = hef.pimpl->network_groups();
    ConfiguredNetworkGroupVector added_network_groups;
    // TODO: can be optimized (add another loop the allocate the network group we're adding)
    added_network_groups.reserve(hef_network_groups.size());
    auto configure_params_copy = configure_params;
    auto hef_arch = hef.pimpl->get_device_arch();

    // Reset FW state_machine status - can be removed?
    static const auto REMOVE_NN_CONFIG_DURING_RESET = false;
    auto status = Control::reset_context_switch_state_machine(*this, REMOVE_NN_CONFIG_DURING_RESET);
    CHECK_SUCCESS_AS_EXPECTED(status);

    for (const auto &base_network_group_proto : hef_network_groups) {
        const std::string &network_group_name = base_network_group_proto->network_group_metadata().network_group_name();
        auto &hef_core_ops = hef.pimpl->core_ops(network_group_name);
        assert(hef_core_ops.size() == 1);
        const auto &core_op = hef_core_ops[0];

        auto expected_partial_core_op = Hef::Impl::get_core_op_per_arch(core_op, hef_arch, device_arch,
            partial_clusters_layout_bitmap);
        CHECK_EXPECTED(expected_partial_core_op);
        auto partial_core_op = expected_partial_core_op.release();

        // TODO: decide about core_op names - align with the Compiler

        /* If NG params are present, use them
           If no configure params are given, use default*/
        ConfigureNetworkParams config_params{};
        if (contains(configure_params, network_group_name)) {
            config_params = configure_params_copy.at(network_group_name);
            configure_params_copy.erase(network_group_name);
        } else if (configure_params.empty()) {
            auto interface = get_default_streams_interface();
            CHECK_EXPECTED(interface);
            auto config_params_exp = hef.create_configure_params(interface.value(), network_group_name);
            CHECK_EXPECTED(config_params_exp);
            config_params = config_params_exp.release();
        } else {
            continue;
        }

        /* Validate that all network_groups are single context */
        CHECK(1 == partial_core_op->contexts.size(), make_unexpected(HAILO_INTERNAL_FAILURE),
            "Only single_context network_groups is supported!. Network group {} has {} contexts.",
            network_group_name, partial_core_op->contexts.size());
        CHECK_AS_EXPECTED(!(Hef::Impl::contains_ddr_layers(*partial_core_op)), HAILO_INVALID_OPERATION,
            "DDR layers are only supported for PCIe device. Network group {} contains DDR layers.",
            network_group_name);
        status = Hef::Impl::validate_core_op_unique_layer_names(*partial_core_op);
        CHECK_SUCCESS_AS_EXPECTED(status);

        /* Update preliminary_config and dynamic_contexts recepies */
        auto &proto_preliminary_config = partial_core_op->preliminary_config;
        auto net_group_config = Hef::Impl::create_single_context_network_group_config(proto_preliminary_config);
        CHECK_EXPECTED(net_group_config);

        auto network_group_metadata = hef.pimpl->get_network_group_metadata(network_group_name);
        CHECK_EXPECTED(network_group_metadata);

        auto net_flow_ops = hef.pimpl->post_process_ops(network_group_metadata->network_group_name());

        auto single_context_app = HcpConfigNetworkGroup(*this, m_active_net_group_holder, net_group_config.release(),
            config_params, network_group_metadata.release(), status, std::move(net_flow_ops));
        CHECK_SUCCESS_AS_EXPECTED(status);

        auto net_group_shared_ptr = make_shared_nothrow<HcpConfigNetworkGroup>(std::move(single_context_app));
        CHECK_AS_EXPECTED(nullptr != net_group_shared_ptr, HAILO_OUT_OF_HOST_MEMORY);
        m_network_groups.emplace_back(net_group_shared_ptr);
        added_network_groups.emplace_back(std::static_pointer_cast<ConfiguredNetworkGroup>(net_group_shared_ptr));

        // TODO: move this func into HcpConfigNetworkGroup c'tor
        status = net_group_shared_ptr->create_streams_from_config_params(*this);
        CHECK_SUCCESS_AS_EXPECTED(status);

        // Check that all boundary streams were created
        status = hef.pimpl->validate_boundary_streams_were_created(network_group_name, *net_group_shared_ptr);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }
    std::string unmatched_keys = "";
    for (const auto &pair : configure_params_copy) {
        unmatched_keys.append(" ");
        unmatched_keys.append(pair.first);
    }
    CHECK_AS_EXPECTED(unmatched_keys.size() == 0, HAILO_INVALID_ARGUMENT,
        "Some network group names in the configuration are not found in the hef file:{}", unmatched_keys);

    return added_network_groups;
}

} /* namespace hailort */
