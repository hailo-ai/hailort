/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include "utils/profiler/tracer_macros.hpp"
#include "vdma/vdma_config_core_op.hpp"
#include "network_group/network_group_internal.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"
#include "device_common/control.hpp"

namespace hailort
{


Expected<std::shared_ptr<VdmaConfigCoreOp>> VdmaConfigCoreOp::create_shared(ActiveCoreOpHolder &active_core_op_holder,
        const ConfigureNetworkParams &config_params,
        std::shared_ptr<ResourcesManager> resources_manager,
        std::shared_ptr<CacheManager> cache_manager,
        std::shared_ptr<CoreOpMetadata> metadata)
{
    auto status = HAILO_UNINITIALIZED;

    auto core_op = make_shared_nothrow<VdmaConfigCoreOp>(active_core_op_holder, config_params, std::move(resources_manager), cache_manager,
        metadata, status);
    CHECK_NOT_NULL(core_op, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS(status);

    return core_op;
}

VdmaConfigCoreOp::~VdmaConfigCoreOp()
{
    (void)shutdown_impl();
}

VdmaConfigCoreOp::VdmaConfigCoreOp(ActiveCoreOpHolder &active_core_op_holder, const ConfigureNetworkParams &config_params,
                                   std::shared_ptr<ResourcesManager> &&resources_manager, std::shared_ptr<CacheManager> cache_manager,
                                   std::shared_ptr<CoreOpMetadata> metadata, hailo_status &status) :
    CoreOp(config_params, metadata, active_core_op_holder, status),
    m_resources_manager(std::move(resources_manager)),
    m_cache_manager(cache_manager)
{}


hailo_status VdmaConfigCoreOp::cancel_pending_transfers()
{
    // Best effort
    auto status = HAILO_SUCCESS;
    auto deactivate_status = HAILO_UNINITIALIZED;
    for (const auto &name_pair : m_input_streams) {
        deactivate_status = name_pair.second->cancel_pending_transfers();
        if (HAILO_SUCCESS != deactivate_status) {
            LOGGER__ERROR("Failed to cancel pending transfers for input stream {}", name_pair.first);
            status = deactivate_status;
        }
    }
    for (const auto &name_pair : m_output_streams) {
        deactivate_status = name_pair.second->cancel_pending_transfers();
        if (HAILO_SUCCESS != deactivate_status) {
            LOGGER__ERROR("Failed to cancel pending transfers for output stream {}", name_pair.first);
            status = deactivate_status;
        }
    }

    return status;
}

hailo_status VdmaConfigCoreOp::activate_impl(uint16_t dynamic_batch_size)
{
    auto status = register_cache_update_callback();
    CHECK_SUCCESS(status, "Failed to register cache update callback");

    auto start_time = std::chrono::steady_clock::now();
    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE != dynamic_batch_size) {
        CHECK(dynamic_batch_size <= get_smallest_configured_batch_size(get_config_params()),
            HAILO_INVALID_ARGUMENT, "Batch size given is {} although max is {}", dynamic_batch_size,
            get_smallest_configured_batch_size(get_config_params()));
    }

    status = m_resources_manager->enable_state_machine(dynamic_batch_size);
    CHECK_SUCCESS(status, "Failed to activate state-machine");

    CHECK_SUCCESS(activate_host_resources(), "Failed to activate host resources");

    //TODO: HRT-13019 - Unite with the calculation in core_op.cpp
    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    TRACE(ActivateCoreOpTrace, std::string(m_resources_manager->get_dev_id()), vdevice_core_op_handle(), elapsed_time_ms, dynamic_batch_size);

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::deactivate_impl()
{
    auto start_time = std::chrono::steady_clock::now();

    auto status = deactivate_host_resources();
    CHECK_SUCCESS(status);

    status = m_resources_manager->reset_state_machine();
    CHECK_SUCCESS(status, "Failed to reset context switch state machine");

    // After the state machine has been reset the vdma channels are no longer active, so we
    // can cancel pending transfers, thus allowing vdma buffers linked to said transfers to be freed
    status = cancel_pending_transfers();
    CHECK_SUCCESS(status, "Failed to cancel pending transfers");

    //TODO: HRT-13019 - Unite with the calculation in core_op.cpp
    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    TRACE(DeactivateCoreOpTrace, std::string(m_resources_manager->get_dev_id()), vdevice_core_op_handle(), elapsed_time_ms);

    status = unregister_cache_update_callback();
    CHECK_SUCCESS(status, "Failed to unregister cache update callback");

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::register_cache_update_callback()
{
    const auto cache_offset_env_var = get_env_variable(HAILORT_AUTO_UPDATE_CACHE_OFFSET_ENV_VAR);
    if (cache_offset_env_var.has_value() && has_caches()) {
        std::string policy;
        int32_t offset_delta = 0;
        TRY(const auto cache_write_length, get_cache_write_length());
        if (cache_offset_env_var.value() == HAILORT_AUTO_UPDATE_CACHE_OFFSET_ENV_VAR_DEFAULT) {
            offset_delta = cache_write_length;
            policy = "cache write size (default)";
        } else if (cache_offset_env_var.value() == HAILORT_AUTO_UPDATE_CACHE_OFFSET_ENV_VAR_DISABLED) {
            LOGGER__INFO("Skipping cache offset updates");
            return HAILO_SUCCESS;
        } else {
            offset_delta = std::stoi(cache_offset_env_var.value());
            policy = "environment variable";
            CHECK(offset_delta <= static_cast<int32_t>(cache_write_length), HAILO_INVALID_ARGUMENT, "Invalid cache offset delta");
        }

        auto &vdma_device = m_resources_manager->get_device();
        vdma_device.set_notification_callback([this, offset_delta](Device &, const hailo_notification_t &notification, void *) {
            if (HAILO_NOTIFICATION_ID_START_UPDATE_CACHE_OFFSET != notification.id) {
                LOGGER__ERROR("Notification id passed to callback is invalid");
                return;
            }

            const auto status = this->update_cache_offset(static_cast<int32_t>(offset_delta));
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to update cache offset");
            }
        }, HAILO_NOTIFICATION_ID_START_UPDATE_CACHE_OFFSET, nullptr);

        LOGGER__INFO("Cache offsets will automatically be updated by {} [{}]", offset_delta, policy);
    }

    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::unregister_cache_update_callback()
{
    auto &vdma_device = m_resources_manager->get_device();
    auto status = vdma_device.remove_notification_callback(HAILO_NOTIFICATION_ID_START_UPDATE_CACHE_OFFSET);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::shutdown()
{
    return shutdown_impl();
}

hailo_status VdmaConfigCoreOp::shutdown_impl()
{
    hailo_status status = HAILO_SUCCESS; // Success oriented

    if (m_is_shutdown.exchange(true)) {
        return HAILO_SUCCESS;
    }

    auto abort_status = abort_low_level_streams();
    if (HAILO_SUCCESS != abort_status) {
        LOGGER__ERROR("Failed abort low level streams {}", abort_status);
        status = abort_status;
    }

    // On VdmaConfigCoreOp, shutdown is the same as deactivate. In the future, we can release the resources inside
    // the resource manager and free space in the firmware SRAM
    auto deactivate_status = deactivate_impl();
    if (HAILO_SUCCESS != deactivate_status) {
        LOGGER__ERROR("Failed deactivate core op with status {}", deactivate_status);
        status = deactivate_status;
    }

    return status;
}

hailo_status VdmaConfigCoreOp::activate_host_resources()
{
    CHECK_SUCCESS(m_resources_manager->start_vdma_transfer_launcher(), "Failed to start vdma transfer launcher");
    CHECK_SUCCESS(m_resources_manager->start_vdma_interrupts_dispatcher(), "Failed to start vdma interrupts");
    CHECK_SUCCESS(activate_low_level_streams(), "Failed to activate low level streams");
    return HAILO_SUCCESS;
}

hailo_status VdmaConfigCoreOp::deactivate_host_resources()
{
    CHECK_SUCCESS(deactivate_low_level_streams(), "Failed to deactivate low level streams");
    CHECK_SUCCESS(m_resources_manager->stop_vdma_interrupts_dispatcher(), "Failed to stop vdma interrupts");
    CHECK_SUCCESS(m_resources_manager->stop_vdma_transfer_launcher(), "Failed to stop vdma transfers pending launch");
    return HAILO_SUCCESS;
}

Expected<hailo_stream_interface_t> VdmaConfigCoreOp::get_default_streams_interface()
{
    return m_resources_manager->get_default_streams_interface();
}

bool VdmaConfigCoreOp::is_scheduled() const
{
    // Scheduler allowed only when working with VDevice and scheduler enabled.
    return false;
}

hailo_status VdmaConfigCoreOp::set_scheduler_timeout(const std::chrono::milliseconds &/*timeout*/, const std::string &/*network_name*/)
{
    LOGGER__ERROR("Setting scheduler's timeout is only allowed when working with VDevice and scheduler enabled");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaConfigCoreOp::set_scheduler_threshold(uint32_t /*threshold*/, const std::string &/*network_name*/)
{
    LOGGER__ERROR("Setting scheduler's threshold is only allowed when working with VDevice and scheduler enabled");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaConfigCoreOp::set_scheduler_priority(uint8_t /*priority*/, const std::string &/*network_name*/)
{
    LOGGER__ERROR("Setting scheduler's priority is only allowed when working with VDevice and scheduler enabled");
    return HAILO_INVALID_OPERATION;
}

hailo_status VdmaConfigCoreOp::bind_buffers(std::unordered_map<std::string, TransferRequest> &transfers)
{
    for (auto &input : m_input_streams) {
        auto transfer = transfers.find(input.second->name());
        CHECK(transfer != transfers.end(), HAILO_INTERNAL_FAILURE, "Invalid stream {}", input.second->name());
        if (transfer->second.transfer_buffers.size() > 1) {
            break;
        }
        CHECK_SUCCESS(input.second->bind_buffer(TransferRequest{transfer->second}));
    }

    for (auto &output : m_output_streams) {
        auto transfer = transfers.find(output.second->name());
        CHECK(transfer != transfers.end(), HAILO_INTERNAL_FAILURE, "Invalid stream {}", output.second->name());
        if (transfer->second.transfer_buffers.size() > 1) {
            break;
        }
        CHECK_SUCCESS(output.second->bind_buffer(TransferRequest{transfer->second}));
    }

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<LatencyMetersMap>> VdmaConfigCoreOp::get_latency_meters()
{
    auto latency_meters = m_resources_manager->get_latency_meters();
    auto res = make_shared_nothrow<LatencyMetersMap>(latency_meters);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
}

Expected<vdma::BoundaryChannelPtr> VdmaConfigCoreOp::get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
{
    return m_resources_manager->get_boundary_vdma_channel_by_stream_name(stream_name);
}

Expected<HwInferResults> VdmaConfigCoreOp::run_hw_infer_estimator()
{
    return m_resources_manager->run_hw_only_infer();
}

Expected<Buffer> VdmaConfigCoreOp::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    return m_resources_manager->read_intermediate_buffer(key);
}

bool VdmaConfigCoreOp::has_caches() const
{
    const auto cache_buffers = m_cache_manager->get_cache_buffers(name());
    return cache_buffers && !(cache_buffers->get()).empty();
}

Expected<uint32_t> VdmaConfigCoreOp::get_cache_length_impl(std::function<size_t(const CacheBuffer&)> length_getter,
    const std::string &length_type) const
{
    size_t length = 0;
    TRY(const auto cache_buffers, m_cache_manager->get_cache_buffers(name()));
    for (auto &cache_buffer : cache_buffers.get()) {
        const auto curr_length = length_getter(cache_buffer.second);
        if (length == 0) {
            length = curr_length;
        } else {
            CHECK(length == curr_length, HAILO_INTERNAL_FAILURE,
                "Cache buffer {} has {} length {}. Expected: {}",
                cache_buffer.first, length_type, curr_length, length);
        }
    }

    return static_cast<uint32_t>(length);
}

Expected<uint32_t> VdmaConfigCoreOp::get_cache_length() const
{
    return get_cache_length_impl([](const CacheBuffer &buffer) { return buffer.cache_length(); }, "cache");
}

Expected<uint32_t> VdmaConfigCoreOp::get_cache_read_length() const
{
    return get_cache_length_impl([](const CacheBuffer &buffer) { return buffer.input_length(); }, "input");
}

Expected<uint32_t> VdmaConfigCoreOp::get_cache_write_length() const
{
    return get_cache_length_impl([](const CacheBuffer &buffer) { return buffer.output_length(); }, "output");
}

Expected<uint32_t> VdmaConfigCoreOp::get_cache_entry_size(uint32_t cache_id) const
{
    TRY(const auto cache_buffers, m_cache_manager->get_cache_buffers(name()));
    auto cache_buffer_it = cache_buffers.get().find(cache_id);
    CHECK(cache_buffer_it != cache_buffers.get().end(), HAILO_INVALID_ARGUMENT, "Cache buffer with id {} not found", cache_id);
    return cache_buffer_it->second.entry_size();
}

hailo_status VdmaConfigCoreOp::init_cache(uint32_t read_offset)
{
    CHECK(has_caches(), HAILO_INVALID_OPERATION, "No caches in core-op");
    return m_cache_manager->init_caches(read_offset);
}

hailo_status VdmaConfigCoreOp::update_cache_offset(int32_t offset_delta_entries)
{
    CHECK(has_caches(), HAILO_INVALID_OPERATION, "No caches in core-op");

    // TODO: figure out how to do this s.t. it'll work with the sched (HRT-14287)
    // auto status = wait_for_activation(std::chrono::milliseconds(0));
    // CHECK_SUCCESS(status, "Core op must be activated before updating cache offset");

    // Update the offsets in the cache manager
    const auto check_cache_snapshots = is_env_variable_on(HAILORT_CHECK_CACHE_UPDATE_ENV_VAR);
    const auto require_cache_changes_env = is_env_variable_on(HAILORT_REQUIRE_CACHE_CHANGES_ENV_VAR);
    auto status = m_cache_manager->update_cache_offset(offset_delta_entries, check_cache_snapshots, require_cache_changes_env);
    CHECK_SUCCESS(status);

    // Signal to the fw that the cache offset has been updated
    status = Control::context_switch_signal_cache_updated(m_resources_manager->get_device());
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<std::vector<uint32_t>> VdmaConfigCoreOp::get_cache_ids() const
{
    TRY(const auto cache_buffers, m_cache_manager->get_cache_buffers(name()));

    std::vector<uint32_t> result;
    result.reserve(cache_buffers.get().size());
    for (const auto &id_buffer_pair : cache_buffers.get()) {
        result.emplace_back(id_buffer_pair.first);
    }

    return result;
}

Expected<Buffer> VdmaConfigCoreOp::read_cache_buffer(uint32_t cache_id)
{
    TRY(const auto cache_buffers, m_cache_manager->get_cache_buffers(name()));
    auto cache_buffer_it = cache_buffers.get().find(cache_id);
    CHECK(cache_buffer_it != cache_buffers.get().end(), HAILO_INVALID_ARGUMENT, "Cache buffer with id {} not found", cache_id);
    return cache_buffer_it->second.read_cache();
}

hailo_status VdmaConfigCoreOp::write_cache_buffer(uint32_t cache_id, MemoryView buffer)
{
    TRY(const auto cache_buffers, m_cache_manager->get_cache_buffers(name()));
    auto cache_buffer_it = cache_buffers.get().find(cache_id);
    CHECK(cache_buffer_it != cache_buffers.get().end(), HAILO_INVALID_ARGUMENT, "Cache buffer with id {} not found", cache_id);
    return cache_buffer_it->second.write_cache(buffer);
}

} /* namespace hailort */
