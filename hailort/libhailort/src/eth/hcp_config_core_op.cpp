/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include "eth/hcp_config_core_op.hpp"
#include "device_common/control.hpp"


#define OUTPUT_CHANNEL_INDEX_OFFSET (16)


namespace hailort
{

HcpConfigCoreOp::HcpConfigCoreOp(Device &device, ActiveCoreOpHolder &active_core_op_holder,
    std::vector<WriteMemoryInfo> &&config, const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> metadata,
    hailo_status &status)
        : CoreOp(config_params, metadata, active_core_op_holder, status),
    m_config(std::move(config)), m_device(device)
{}

Expected<hailo_stream_interface_t> HcpConfigCoreOp::get_default_streams_interface()
{
    return m_device.get_default_streams_interface();
}

bool HcpConfigCoreOp::is_scheduled() const
{
    // Scheduler not supported on HcpConfigCoreOp
    return false;
}

hailo_status HcpConfigCoreOp::set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    (void) timeout;
    (void) network_name;
    return HAILO_INVALID_OPERATION;
}

hailo_status HcpConfigCoreOp::set_scheduler_threshold(uint32_t threshold, const std::string &network_name)
{
    (void) threshold;
    (void) network_name;
    return HAILO_INVALID_OPERATION;
}

hailo_status HcpConfigCoreOp::set_scheduler_priority(uint8_t /*priority*/, const std::string &/*network_name*/)
{
    return HAILO_INVALID_OPERATION;
}

Expected<std::shared_ptr<LatencyMetersMap>> HcpConfigCoreOp::get_latency_meters()
{
    /* hcp does not support latnecy. return empty map */
    LatencyMetersMap empty_map; 
    auto res = make_shared_nothrow<LatencyMetersMap>(empty_map);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
}

Expected<vdma::BoundaryChannelPtr> HcpConfigCoreOp::get_boundary_vdma_channel_by_stream_name(
    const std::string &stream_name)
{
    LOGGER__ERROR("get_boundary_vdma_channel_by_stream_name function for stream name {} is not supported on ETH core-ops",
        stream_name);
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<HwInferResults> HcpConfigCoreOp::run_hw_infer_estimator()
{
    LOGGER__ERROR("run_hw_infer_estimator function is not supported on ETH core-ops");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

bool HcpConfigCoreOp::has_caches() const
{
    return false;
}

Expected<uint32_t> HcpConfigCoreOp::get_cache_length() const
{
    LOGGER__ERROR("get_cache_length function is not supported on ETH core-ops");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<uint32_t> HcpConfigCoreOp::get_cache_read_length() const
{
    LOGGER__ERROR("get_cache_read_length function is not supported on ETH core-ops");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<uint32_t> HcpConfigCoreOp::get_cache_write_length() const
{
    LOGGER__ERROR("get_cache_write_length function is not supported on ETH core-ops");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<uint32_t> HcpConfigCoreOp::get_cache_entry_size(uint32_t cache_id) const
{
    (void) cache_id;
    LOGGER__ERROR("get_cache_entry_size function is not supported on ETH core-ops");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status HcpConfigCoreOp::init_cache(uint32_t read_offset)
{
    (void) read_offset;
    LOGGER__ERROR("init_cache function is not supported on ETH core-ops");
    return HAILO_INVALID_OPERATION;
}

hailo_status HcpConfigCoreOp::update_cache_offset(int32_t offset_delta_entries)
{
    (void) offset_delta_entries;
    LOGGER__ERROR("update_cache_offset function is not supported on ETH core-ops");
    return HAILO_INVALID_OPERATION;
}

Expected<std::vector<uint32_t>> HcpConfigCoreOp::get_cache_ids() const
{
    LOGGER__ERROR("get_cache_ids function is not supported on ETH core-ops");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<Buffer> HcpConfigCoreOp::read_cache_buffer(uint32_t cache_id)
{
    (void) cache_id;
    LOGGER__ERROR("read_cache_buffer function is not supported on ETH core-ops");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status HcpConfigCoreOp::write_cache_buffer(uint32_t cache_id, MemoryView buffer)
{
    (void) cache_id;
    (void) buffer;
    LOGGER__ERROR("write_cache_buffer function is not supported on ETH core-ops");
    return HAILO_INVALID_OPERATION;
}

hailo_status HcpConfigCoreOp::activate_impl(uint16_t /* dynamic_batch_size */)
{
    // Close older dataflows
    auto status = Control::close_all_streams(m_device);
    CHECK_SUCCESS(status);

    // Reset nn_core before writing configurations
    status = m_device.reset(HAILO_RESET_DEVICE_MODE_NN_CORE);
    CHECK_SUCCESS(status);

    for (auto &m : m_config) {
        status = m_device.write_memory(m.address, MemoryView(m.data));
        CHECK_SUCCESS(status);
    }

    status = activate_low_level_streams();
    CHECK_SUCCESS(status, "Failed activating low level streams");

    return HAILO_SUCCESS;
}

hailo_status HcpConfigCoreOp::deactivate_impl()
{
    for (auto &name_pair : m_input_streams) {
        const auto status = name_pair.second->flush();
        CHECK_SUCCESS(status, "Failed to flush input stream {}", name_pair.first);
    }

    auto status = deactivate_low_level_streams();
    CHECK_SUCCESS(status, "Failed deactivating low level streams");

    return HAILO_SUCCESS;
}

hailo_status HcpConfigCoreOp::shutdown()
{
    return abort_low_level_streams();
}

} /* namespace hailort */
