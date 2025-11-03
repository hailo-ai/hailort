/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_device.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "vdma/vdma_device.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "vdma/vdma_config_manager.hpp"
#include "vdma/pcie/pcie_device.hpp"
#include "vdma/integrated/integrated_device.hpp"
#include "device_common/control.hpp"
#include "device_common/device_internal.hpp"
#include "core_op/resource_manager/resource_manager_builder.hpp"
#include "core_op/core_op.hpp"
#include "common/os_utils.hpp"
#include "hef/hef_internal.hpp"
#include "utils/buffer_storage.hpp"

#include <new>
#include <algorithm>


namespace hailort
{

#ifndef HAILO_EMULATOR
static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT(1000);
#else /* ifndef HAILO_EMULATOR */
static constexpr std::chrono::milliseconds DEFAULT_TIMEOUT(50000);
#endif /* ifndef HAILO_EMULATOR */

VdmaDevice::VdmaDevice(std::unique_ptr<HailoRTDriver> &&driver, Device::Type type, hailo_status &status) :
    DeviceBase::DeviceBase(type),
    m_driver(std::move(driver)),
    m_is_configured(false),
    m_amount_of_sram_used(0)
{
    activate_notifications(get_dev_id());

    status = HAILO_SUCCESS;
}

hailo_status VdmaDevice::wait_for_wakeup()
{
    return HAILO_SUCCESS;
}

Expected<D2H_EVENT_MESSAGE_t> VdmaDevice::read_notification()
{
    auto notification_buffer = m_driver->read_notification();
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
    return m_driver->disable_notifications();
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

    auto status = m_driver->fw_control(request_buffer, request_size, request_md5,
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

hailo_status VdmaDevice::clear_configured_apps()
{
    auto status = Control::reset_context_switch_state_machine(*this);
    CHECK_SUCCESS(status);

    // In case of integrated device need to reset nn core before activating network group to clear prior nn core state
    if (Device::Type::INTEGRATED == get_type()) {
        // On core device, the nn_manager is not responsible to reset the nn-core so
        // we use the SCU control for that.
        status = m_driver->reset_nn_core();
        CHECK_SUCCESS(status);
    }

    status = Control::clear_configured_apps(*this);
    CHECK_SUCCESS(status, "Failed to clear configured network groups with status {}", status);

    m_amount_of_sram_used = 0;

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
        status = clear_configured_apps();
        CHECK_SUCCESS_AS_EXPECTED(status);

        assert(nullptr == m_cache_manager);
        TRY(m_cache_manager, CacheManager::create_shared(get_driver()));

        assert(nullptr == m_vdma_interrupts_dispatcher);
        TRY(m_vdma_interrupts_dispatcher, vdma::InterruptsDispatcher::create(get_driver()));

        assert(nullptr == m_vdma_transfer_launcher);
        TRY(m_vdma_transfer_launcher, vdma::TransferLauncher::create());

        m_is_configured = true;
    }

    auto added_network_groups = create_networks_group_vector(hef, configure_params);
    CHECK_EXPECTED(added_network_groups);

    return added_network_groups;
}

// TODO: HRT-9551 Create CoreOpMetadata and CoreOp in the same loop
Expected<std::shared_ptr<ConfiguredNetworkGroup>> VdmaDevice::create_configured_network_group(
    std::vector<std::shared_ptr<CoreOpMetadata>> &core_ops_metadata,
    Hef &hef, const ConfigureNetworkParams &config_params,
    uint8_t current_core_op_index)
{
    std::vector<std::shared_ptr<CoreOp>> core_ops;
    core_ops.reserve(core_ops_metadata.size());

    // TODO: keep metadata per core_op (HRT-9551)
    assert(core_ops_metadata.size() == 1);
    auto core_op_metadata = core_ops_metadata[0];

    auto status = m_cache_manager->create_caches_from_core_op(core_op_metadata);
    CHECK_SUCCESS(status);

    TRY(auto resource_manager, ResourcesManagerBuilder::build(current_core_op_index,
        *this, get_driver(), m_cache_manager, config_params, core_op_metadata,
        static_cast<HEFHwArch>(hef.pimpl->get_device_arch()), hef));

    TRY(auto core_op_ptr, VdmaConfigCoreOp::create_shared(m_active_core_op_holder, config_params,
        resource_manager, m_cache_manager, core_op_metadata));

    // TODO: move this func into VdmaConfigCoreOp c'tor
    status = core_op_ptr->create_streams_from_config_params(*this);
    CHECK_SUCCESS(status);

    // Check that all boundary streams were created
    status = hef.pimpl->validate_boundary_streams_were_created(core_op_metadata->core_op_name(), core_op_ptr);
    CHECK_SUCCESS(status);

    core_ops.emplace_back(core_op_ptr);
    m_core_ops.emplace_back(core_op_ptr);

    auto metadata = hef.pimpl->network_group_metadata(core_op_metadata->core_op_name());
    auto network_group_expected = ConfiguredNetworkGroupBase::create(config_params, std::move(core_ops), std::move(metadata));
    CHECK_EXPECTED(network_group_expected);
    auto network_group_ptr = network_group_expected.release();

    return Expected<std::shared_ptr<ConfiguredNetworkGroup>>(network_group_ptr);
}

Expected<size_t> VdmaDevice::read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id)
{
    size_t read_bytes = 0;
    hailo_status status = HAILO_UNINITIALIZED;
    status = m_driver->read_log(buffer.data(), buffer.size(), &read_bytes, cpu_id);
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

// TODO - HRT-13234, move to DeviceBase
void VdmaDevice::shutdown_core_ops()
{
    for (auto core_op_weak : m_core_ops) {
        if (auto core_op = core_op_weak.lock()) {
            auto status = core_op->shutdown();
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to shutdown core op with status {}", status);
            }
        }
    }
}

hailo_status VdmaDevice::mark_as_used()
{
    return m_driver->mark_as_used();
}

ExpectedRef<vdma::InterruptsDispatcher> VdmaDevice::get_vdma_interrupts_dispatcher()
{
    CHECK_AS_EXPECTED(m_vdma_interrupts_dispatcher, HAILO_INTERNAL_FAILURE, "vDMA interrupt dispatcher wasn't created");
    return std::ref(*m_vdma_interrupts_dispatcher);
}

ExpectedRef<vdma::TransferLauncher> VdmaDevice::get_vdma_transfer_launcher()
{
    CHECK_AS_EXPECTED(m_vdma_transfer_launcher, HAILO_INTERNAL_FAILURE, "vDMA transfer launcher wasn't created");
    return std::ref(*m_vdma_transfer_launcher);
}

VdmaDevice::~VdmaDevice()
{
    auto status = stop_notification_fetch_thread();
    if (HAILO_SUCCESS != status) {
        LOGGER__WARNING("Stopping notification thread ungracefully");
    }
    if (m_is_configured) {
        status = clear_configured_apps();
        if (HAILO_SUCCESS != status) {
            LOGGER__WARNING("clear configured apps ended with status {}", status);
        }
    }
}

hailo_status VdmaDevice::dma_map(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    return dma_map_impl(*m_driver.get(), address, size, data_direction);
}

hailo_status VdmaDevice::dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    return dma_unmap_impl(*m_driver.get(), address, size, data_direction);
}

hailo_status VdmaDevice::dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    return dma_map_dmabuf_impl(*m_driver.get(), dmabuf_fd, size, data_direction);
}

hailo_status VdmaDevice::dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t data_direction)
{
    return dma_unmap_dmabuf_impl(*m_driver.get(), dmabuf_fd, size, data_direction);
}

static std::pair<void *, size_t> aligned_part_to_map(void *original, size_t size)
{
    const auto dma_alignment = OsUtils::get_dma_able_alignment();
    const auto aligned_address = HailoRTCommon::align_to(original, dma_alignment);
    const auto unaligned_part = reinterpret_cast<uintptr_t>(aligned_address) - reinterpret_cast<uintptr_t>(original);
    const auto aligned_size = size > unaligned_part ? size - unaligned_part : 0;
    return std::make_pair(aligned_address, aligned_size);
}

hailo_status VdmaDevice::dma_map_impl(HailoRTDriver &driver, void *address,
    size_t size, hailo_dma_buffer_direction_t data_direction)
{
    // Since we can't map unaligned addresses (to dma alignment), we map only the aligned part of the buffer. The other
    // unaligned part will be copied into some bounce buffer (which is already mapped).
    std::tie(address, size) = aligned_part_to_map(address, size);

    if (size == 0) {
        // The aligned part is not in range (Can happen when the buffer is smaller than the dma alignment), nothing to
        // map.
        return HAILO_SUCCESS;
    }

    CHECK_EXPECTED(driver.vdma_buffer_map(reinterpret_cast<uintptr_t>(address), size, to_hailo_driver_direction(data_direction),
        HailoRTDriver::DmaBufferType::USER_PTR_BUFFER));
    return HAILO_SUCCESS;
}

hailo_status VdmaDevice::dma_unmap_impl(HailoRTDriver &driver, void *address,
    size_t size, hailo_dma_buffer_direction_t data_direction)
{
    // Since we can't map unaligned addresses (to dma alignment), we map only the aligned part of the buffer. The other
    // unaligned part will be copied into some bounce buffer (which is already mapped).
    std::tie(address, size) = aligned_part_to_map(address, size);
    if (size == 0) {
        // The aligned part is not in range (Can happen when the buffer is smaller than the dma alignment), nothing to
        // map.
        return HAILO_SUCCESS;
    }
    return driver.vdma_buffer_unmap(reinterpret_cast<uintptr_t>(address), size,
        to_hailo_driver_direction(data_direction), HailoRTDriver::DmaBufferType::USER_PTR_BUFFER);
}

hailo_status VdmaDevice::dma_map_dmabuf_impl(HailoRTDriver &driver, int dmabuf_fd,
    size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK(dmabuf_fd >= 0, HAILO_INVALID_ARGUMENT, "cannot map dmabuf with fd: {}", dmabuf_fd);
    CHECK_EXPECTED(driver.vdma_buffer_map(static_cast<uint32_t>(dmabuf_fd), size, to_hailo_driver_direction(data_direction),
        HailoRTDriver::DmaBufferType::DMABUF_BUFFER));
    return HAILO_SUCCESS;
}

hailo_status VdmaDevice::dma_unmap_dmabuf_impl(HailoRTDriver &driver, int dmabuf_fd,
    size_t size, hailo_dma_buffer_direction_t data_direction)
{
    CHECK(dmabuf_fd >= 0, HAILO_INVALID_ARGUMENT, "cannot map dmabuf with fd: {}", dmabuf_fd);
    return driver.vdma_buffer_unmap(static_cast<uint32_t>(dmabuf_fd), size, to_hailo_driver_direction(data_direction),
        HailoRTDriver::DmaBufferType::DMABUF_BUFFER);
}

Expected<ConfiguredNetworkGroupVector> VdmaDevice::create_networks_group_vector(Hef &hef, const NetworkGroupsParamsMap &configure_params)
{
    auto partial_clusters_layout_bitmap_exp = Control::get_partial_clusters_layout_bitmap(*this);
    CHECK_EXPECTED(partial_clusters_layout_bitmap_exp);
    auto partial_clusters_layout_bitmap = partial_clusters_layout_bitmap_exp.release();

    auto &hef_net_groups = hef.pimpl->network_groups();
    auto configure_params_copy = configure_params;
    ConfiguredNetworkGroupVector added_network_groups;
    // TODO: can be optimized (add another loop the allocate the network group we're adding)
    added_network_groups.reserve(hef_net_groups.size());
    for (const auto &hef_net_group : hef_net_groups) {
        const std::string &network_group_name = HefUtils::get_network_group_name(*hef_net_group, SupportedFeatures());
        const auto prev_core_op_count = m_core_ops.size();
        auto current_core_op_index = static_cast<uint8_t>(prev_core_op_count);

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
        auto status = Hef::Impl::update_network_batch_size(config_params);
        CHECK_SUCCESS_AS_EXPECTED(status);

        auto core_ops_metadata_ptrs = create_core_ops_metadata(hef, network_group_name, partial_clusters_layout_bitmap);
        CHECK_EXPECTED(core_ops_metadata_ptrs);

        auto network_group_expected = create_configured_network_group(core_ops_metadata_ptrs.value(),
            hef, config_params, current_core_op_index);
        CHECK_EXPECTED(network_group_expected);
        auto network_group_ptr = network_group_expected.release();

        added_network_groups.emplace_back(network_group_ptr);
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

Expected<std::vector<std::shared_ptr<CoreOpMetadata>>> VdmaDevice::create_core_ops_metadata(Hef &hef, const std::string &network_group_name, uint32_t partial_clusters_layout_bitmap)
{
    auto hef_core_ops = hef.pimpl->core_ops(network_group_name);
    assert(1 == hef_core_ops.size());

    std::vector<std::shared_ptr<CoreOpMetadata>> core_ops_metadata_ptrs;
    core_ops_metadata_ptrs.reserve(hef_core_ops.size());
    const auto prev_core_ops_count = m_core_ops.size();
    const auto total_core_ops_count = prev_core_ops_count + hef_core_ops.size();
    CHECK_AS_EXPECTED(CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS >= total_core_ops_count,
        HAILO_INVALID_OPERATION,
        "Can't add {} core-ops from HEF. Currently {} core-ops are configured; maximum allowed core-ops: {}.",
        hef_core_ops.size(), prev_core_ops_count, CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS);

    auto hef_arch = hef.pimpl->get_device_arch();
    auto device_arch = get_architecture();
    CHECK_EXPECTED(device_arch);

    for (const auto &hef_core_op : hef_core_ops) {
        auto expected_partial_core_op = Hef::Impl::get_core_op_per_arch(hef_core_op, hef_arch, device_arch.value(),
            partial_clusters_layout_bitmap);
        CHECK_EXPECTED(expected_partial_core_op);
        auto partial_core_op = expected_partial_core_op.release();
        auto status = Hef::Impl::validate_core_op_unique_layer_names(*partial_core_op);
        CHECK_SUCCESS_AS_EXPECTED(status);

        // TODO: keep metadata per core_op (HRT-9551)
        // TODO: decide about core_op names - align with the Compiler
        auto core_op_metadata = hef.pimpl->get_core_op_metadata(network_group_name, partial_clusters_layout_bitmap);
        CHECK_EXPECTED(core_op_metadata);
        core_ops_metadata_ptrs.emplace_back(core_op_metadata.release());
    }

    return core_ops_metadata_ptrs;
}

} /* namespace hailort */
