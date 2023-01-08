/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_device.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "vdma_device.hpp"
#include "vdma_descriptor_list.hpp"
#include "context_switch/multi_context/vdma_config_manager.hpp"
#include "pcie_device.hpp"
#include "core_device.hpp"
#include "control.hpp"
#include "context_switch/resource_manager_builder.hpp"

#include <new>
#include <algorithm>

namespace hailort
{

#ifndef HAILO_EMULATOR
static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT(1000);
#else /* ifndef HAILO_EMULATOR */
static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT(50000);
#endif /* ifndef HAILO_EMULATOR */

VdmaDevice::VdmaDevice(HailoRTDriver &&driver, Device::Type type, const std::string &device_id) :
    DeviceBase::DeviceBase(type),
    m_driver(std::move(driver)), m_is_configured(false)
{
    activate_notifications(device_id);
}

Expected<std::unique_ptr<VdmaDevice>> VdmaDevice::create(const std::string &device_id)
{
    const bool DONT_LOG_ON_FAILURE = false;
    if (CoreDevice::DEVICE_ID == device_id) {
        auto device = CoreDevice::create();
        CHECK_EXPECTED(device);;
        return std::unique_ptr<VdmaDevice>(device.release());
    }
    else if (auto pcie_info = PcieDevice::parse_pcie_device_info(device_id, DONT_LOG_ON_FAILURE)) {
        auto device = PcieDevice::create(pcie_info.release());
        CHECK_EXPECTED(device);
        return std::unique_ptr<VdmaDevice>(device.release());
    }
    else {
        LOGGER__ERROR("Invalid device id {}", device_id);
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }
}

hailo_status VdmaDevice::wait_for_wakeup()
{
    return HAILO_SUCCESS;
}

Expected<D2H_EVENT_MESSAGE_t> VdmaDevice::read_notification()
{
    auto notification_buffer = m_driver.read_notification();
    if (!notification_buffer.has_value()) {
        return make_unexpected(notification_buffer.status());
    }

    D2H_EVENT_MESSAGE_t notification;
    CHECK_AS_EXPECTED(sizeof(notification) >= notification_buffer->size(), HAILO_GET_D2H_EVENT_MESSAGE_FAIL,
        "buffer len is not valid = {}", notification_buffer->size());
    memcpy(&notification, notification_buffer->data(), notification_buffer->size());
    return notification;
}

hailo_status VdmaDevice::disable_notifications()
{
    return m_driver.disable_notifications();
}

hailo_status VdmaDevice::fw_interact_impl(uint8_t *request_buffer, size_t request_size,
        uint8_t *response_buffer, size_t *response_size, hailo_cpu_id_t cpu_id)
{
    uint8_t request_md5[PCIE_EXPECTED_MD5_LENGTH];
    MD5_CTX ctx;

    MD5_Init(&ctx);
    MD5_Update(&ctx, request_buffer, request_size);
    MD5_Final(request_md5, &ctx);

    uint8_t response_md5[PCIE_EXPECTED_MD5_LENGTH];
    uint8_t expected_response_md5[PCIE_EXPECTED_MD5_LENGTH];

    auto status = m_driver.fw_control(request_buffer, request_size, request_md5,
        response_buffer, response_size, response_md5,
        DEFAULT_TIMEOUT, cpu_id);
    CHECK_SUCCESS(status, "Failed to send fw control");

    MD5_Init(&ctx);
    MD5_Update(&ctx, response_buffer, (*response_size));
    MD5_Final(expected_response_md5, &ctx);

    auto memcmp_result = memcmp(expected_response_md5, response_md5, sizeof(response_md5));
    CHECK(0 == memcmp_result, HAILO_INTERNAL_FAILURE, "MD5 validation of control response failed.");

    return HAILO_SUCCESS;
}

Expected<ConfiguredNetworkGroupVector> VdmaDevice::add_hef(Hef &hef, const NetworkGroupsParamsMap &configure_params)
{
    auto status = mark_as_used();
    CHECK_SUCCESS_AS_EXPECTED(status);

    if (!m_is_configured) {
        // TODO: Do we need this control after fixing HRT-7519?
        // Reset context_switch state machine - it may have been in an active state if a previous VdmaDevice
        // wasn't dtor'd (due to SIGKILL for example)
        static const auto REMOVE_NN_CONFIG_DURING_RESET = false;
        status = Control::reset_context_switch_state_machine(*this, REMOVE_NN_CONFIG_DURING_RESET);
        CHECK_SUCCESS_AS_EXPECTED(status);

        status = Control::clear_configured_apps(*this);
        CHECK_SUCCESS_AS_EXPECTED(status, "Failed to clear configured network groups with status {}", status);

        m_is_configured = true;
    }

    auto device_arch = get_architecture();
    CHECK_EXPECTED(device_arch);

    auto partial_clusters_layout_bitmap_exp = Control::get_partial_clusters_layout_bitmap(*this);
    CHECK_EXPECTED(partial_clusters_layout_bitmap_exp);
    auto partial_clusters_layout_bitmap = partial_clusters_layout_bitmap_exp.release();

    auto &hef_net_groups = hef.pimpl->network_groups();
    ConfiguredNetworkGroupVector added_network_groups;
    // TODO: can be optimized (add another loop the allocate the network group we're adding)
    added_network_groups.reserve(hef_net_groups.size());
    auto configure_params_copy = configure_params;
    for (const auto &hef_net_group : hef_net_groups) {
        const std::string &network_group_name = HefUtils::get_network_group_name(*hef_net_group, SupportedFeatures());
        auto hef_core_ops = hef.pimpl->core_ops(network_group_name);
        assert(hef_core_ops.size() == 1);
        std::vector<std::shared_ptr<NetworkGroupMetadata>> network_group_metadata_ptrs;
        network_group_metadata_ptrs.reserve(hef_core_ops.size());
        const auto prev_network_group_count = m_network_groups.size();
        const auto total_network_group_count = prev_network_group_count + hef_core_ops.size();
        CHECK_AS_EXPECTED(CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS >= total_network_group_count,
            HAILO_INVALID_OPERATION,
            "Can't add {} network groups from HEF. Currently {} network groups are configured; maximum allowed network groups: {}.",
            hef_core_ops.size(), prev_network_group_count, CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS);

        auto hef_arch = hef.pimpl->get_device_arch();

        auto current_net_group_index = static_cast<uint8_t>(prev_network_group_count);
        for (const auto &core_op : hef_core_ops) {
            auto expected_partial_core_op = Hef::Impl::get_core_op_per_arch(core_op, hef_arch, device_arch.value(),
                partial_clusters_layout_bitmap);
            CHECK_EXPECTED(expected_partial_core_op);
            auto partial_core_op = expected_partial_core_op.release();
            status = Hef::Impl::validate_core_op_unique_layer_names(*partial_core_op);
            CHECK_SUCCESS_AS_EXPECTED(status);

            // TODO: keep metadata per core_op (HRT-8639)
            // TODO: decide about core_op names - align with the Compiler
            auto network_group_metadata = hef.pimpl->get_network_group_metadata(network_group_name, partial_clusters_layout_bitmap);
            CHECK_EXPECTED(network_group_metadata);

            auto network_group_metadata_ptr = make_shared_nothrow<NetworkGroupMetadata>(network_group_metadata.release());
            CHECK_AS_EXPECTED(nullptr != network_group_metadata_ptr, HAILO_OUT_OF_HOST_MEMORY);
            network_group_metadata_ptrs.push_back(network_group_metadata_ptr);
        }

        /* If NG params are present, use them
        If no configure params are given, use default*/
        ConfigureNetworkParams config_params{};
        if (contains(configure_params, network_group_name)) {
            config_params = configure_params_copy.at(network_group_name);
            configure_params_copy.erase(network_group_name);
        } else if (configure_params.empty()) {
            auto stream_interface = get_default_streams_interface();
            CHECK_EXPECTED(stream_interface);
            auto config_params_exp = hef.create_configure_params(stream_interface.value(), network_group_name);
            CHECK_EXPECTED(config_params_exp);
            config_params = config_params_exp.release();
        } else {
            continue;
        }
        /* Validate batch size (network group batch size vs network batch size) */
        status = Hef::Impl::update_network_batch_size(config_params);
        CHECK_SUCCESS_AS_EXPECTED(status);
        auto network_group = create_configured_network_group(network_group_metadata_ptrs,
            hef, config_params, current_net_group_index);
        CHECK_EXPECTED(network_group);
        added_network_groups.emplace_back(network_group.release());
        current_net_group_index++;
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

Expected<std::shared_ptr<ConfiguredNetworkGroup>> VdmaDevice::create_configured_network_group(
    const std::vector<std::shared_ptr<NetworkGroupMetadata>> &network_group_metadatas,
    Hef &hef, const ConfigureNetworkParams &config_params,
    uint8_t network_group_index)
{
    // TODO: keep metadata per core_op (HRT-8639)
    assert(network_group_metadatas.size() == 1);
    auto network_group_metadata = network_group_metadatas[0];

    /* build HEF supported features */
    auto resource_manager = ResourcesManagerBuilder::build(network_group_index,
        *this, get_driver(), config_params, network_group_metadata, hef.pimpl->get_device_arch());
    CHECK_EXPECTED(resource_manager);

    auto net_flow_ops = hef.pimpl->post_process_ops(network_group_metadata->network_group_name());

    auto net_group = VdmaConfigNetworkGroup::create(m_active_net_group_holder, config_params,
        resource_manager.release(), hef.hash(), network_group_metadata, std::move(net_flow_ops));

    auto net_group_ptr = make_shared_nothrow<VdmaConfigNetworkGroup>(net_group.release());
    CHECK_AS_EXPECTED(nullptr != net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    // TODO: move this func into VdmaConfigNetworkGroup c'tor
    auto status = net_group_ptr->create_streams_from_config_params(*this);
    CHECK_SUCCESS_AS_EXPECTED(status);

    m_network_groups.emplace_back(net_group_ptr);

    // Check that all boundary streams were created
    status = hef.pimpl->validate_boundary_streams_were_created(network_group_metadata->network_group_name(), *net_group_ptr);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return Expected<std::shared_ptr<ConfiguredNetworkGroup>>(net_group_ptr);
}

Expected<size_t> VdmaDevice::read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id)
{
    size_t read_bytes = 0;
    hailo_status status = HAILO_UNINITIALIZED;
    status = m_driver.read_log(buffer.data(), buffer.size(), &read_bytes, cpu_id);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return read_bytes;
}

void VdmaDevice::increment_control_sequence()
{
    // To support multiprocess the sequence must remain 0 which is a number the FW ignores.
    // Otherwise the FW might get the same sequence number from several processes which
    // cause the command to be discarded.
    m_control_sequence = 0;
}

hailo_reset_device_mode_t VdmaDevice::get_default_reset_mode()
{
    return HAILO_RESET_DEVICE_MODE_SOFT;
}

uint16_t VdmaDevice::get_default_desc_page_size() const
{
    return m_driver.calc_desc_page_size(DEFAULT_DESC_PAGE_SIZE);
}

hailo_status VdmaDevice::mark_as_used()
{
    return m_driver.mark_as_used();
}

VdmaDevice::~VdmaDevice()
{
    auto status = stop_notification_fetch_thread();
    if (HAILO_SUCCESS != status) {
        LOGGER__WARNING("Stopping notification thread ungracefully");
    }
    if (m_is_configured) {
        status = Control::clear_configured_apps(*this);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to clear conigured network groups with status {}", status);
        }
    }
}

} /* namespace hailort */
