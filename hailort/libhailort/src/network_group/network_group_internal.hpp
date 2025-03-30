/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_internal.hpp
 * @brief TODO: HRT-9547 - Change doc after moving NGs to global context + add explanation on lagacy names
 *        Class declaration for ConfiguredNetworkGroupBase and ActivatedCoreOp that implement the basic ConfiguredNetworkGroup
 *        and ActivatedNetworkGroup interfaces. All internal classes that are relevant should inherit from the
 *        ConfiguredNetworkGroupBase and ActivatedCoreOp classes.
 *        Hence, the hierarchy is as follows:
 *        --------------------------------------------------------------------------------------------------------------
 *        |                                         ConfiguredNetworkGroup                                             |  (External "interface")
 *        |                   ________________________________|___________________________                             |
 *        |                  /                                                            \                            |
 *        |                 ConfiguredNetworkGroupBase                         ConfiguredNetworkGroupClient            |  (Base classes)
 *        |                            |                                                                               |
 *        |                            |                                                                               |
 *        |                   vector of CoreOps                                                                        |  (Actual implementations)
 *        -------------------------------------------------------------------------------------------------------------|
 *        |                         ActivatedNetworkGroup                                                              |  (External "interface")
 *        |                                   |                                                                        |
 *        |                       ActivatedNetworkGroupImpl                                                            |  (Class implementation)
 *        --------------------------------------------------------------------------------------------------------------
 **/

#ifndef _HAILO_NETWORK_GROUP_INTERNAL_HPP_
#define _HAILO_NETWORK_GROUP_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"

#include "common/latency_meter.hpp"
#include "common/internal_env_vars.hpp"

#include "core_op/active_core_op_holder.hpp"
#include "core_op/core_op.hpp"

#include "net_flow/ops_metadata/nms_op_metadata.hpp"

namespace hailort
{
using stream_name_t = std::string;
using op_name_t = std::string;


class ConfiguredNetworkGroupBase : public ConfiguredNetworkGroup
{
public:
    static Expected<std::shared_ptr<ConfiguredNetworkGroupBase>> create(const ConfigureNetworkParams &config_params,
        std::vector<std::shared_ptr<CoreOp>> &&core_ops, NetworkGroupMetadata &&metadata);

    virtual ~ConfiguredNetworkGroupBase() = default;
    ConfiguredNetworkGroupBase(const ConfiguredNetworkGroupBase &other) = delete;
    ConfiguredNetworkGroupBase &operator=(const ConfiguredNetworkGroupBase &other) = delete;
    ConfiguredNetworkGroupBase &operator=(ConfiguredNetworkGroupBase &&other) = delete;
    ConfiguredNetworkGroupBase(ConfiguredNetworkGroupBase &&other) = delete;

    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate(
        const hailo_activate_network_group_params_t &network_group_params) override;
    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout) override;

    hailo_status activate_impl(uint16_t dynamic_batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE);
    hailo_status deactivate_impl();

    virtual hailo_status shutdown() override;

    virtual const std::string& get_network_group_name() const override;
    virtual const std::string& name() const override;

    virtual Expected<InputStreamRefVector> get_input_streams_by_network(const std::string &network_name="") override;
    virtual Expected<OutputStreamRefVector> get_output_streams_by_network(const std::string &network_name="") override;
    virtual InputStreamRefVector get_input_streams() override;
    virtual OutputStreamRefVector get_output_streams() override;
    virtual std::vector<std::reference_wrapper<InputStream>> get_input_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual std::vector<std::reference_wrapper<OutputStream>> get_output_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual ExpectedRef<InputStream> get_input_stream_by_name(const std::string& name) override;
    virtual ExpectedRef<OutputStream> get_output_stream_by_name(const std::string& name) override;
    virtual Expected<OutputStreamWithParamsVector> get_output_streams_from_vstream_names(
        const std::map<std::string, hailo_vstream_params_t> &outputs_params) override;
    virtual Expected<LatencyMeasurementResult> get_latency_measurement(const std::string &network_name="") override;

    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_input_vstream_params(
        bool unused, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") override;
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_output_vstream_params(
        bool unused, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") override;
        
    virtual Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> make_output_vstream_params_groups(
        bool unused, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size) override;

    virtual Expected<std::vector<std::vector<std::string>>> get_output_vstream_groups() override;

    virtual Expected<std::vector<hailo_network_info_t>> get_network_infos() const override;
    virtual Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &network_name="") const override;
    virtual AccumulatorPtr get_activation_time_accumulator() const override;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const override;

    virtual bool is_multi_context() const override;
    virtual const ConfigureNetworkParams get_config_params() const override;

    virtual Expected<HwInferResults> run_hw_infer_estimator() override;

    // TODO: HRT-9551 - Change to get_core_op_by_name() when multiple core_ops supported
    std::shared_ptr<CoreOp> get_core_op() const;
    // TODO: HRT-9546 Remove
    const std::shared_ptr<CoreOpMetadata> get_core_op_metadata() const;

    const SupportedFeatures &get_supported_features();

    Expected<uint16_t> get_stream_batch_size(const std::string &stream_name);

    virtual Expected<std::vector<std::string>> get_sorted_output_names() override;
    virtual Expected<std::vector<std::string>> get_stream_names_from_vstream_name(const std::string &vstream_name) override;
    virtual Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name) override;

    virtual Expected<std::vector<InputVStream>> create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params) override;
    virtual Expected<std::vector<OutputVStream>> create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params) override;
    virtual Expected<size_t> infer_queue_size() const override;

    Expected<std::shared_ptr<InputStreamBase>> get_shared_input_stream_by_name(const std::string &stream_name)
    {
        return get_core_op()->get_shared_input_stream_by_name(stream_name);
    }

    Expected<std::shared_ptr<OutputStreamBase>> get_shared_output_stream_by_name(const std::string &stream_name)
    {
        return get_core_op()->get_shared_output_stream_by_name(stream_name);
    }

    EventPtr get_core_op_activated_event()
    {
        return get_core_op()->m_core_op_activated_event;
    }

    Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters()
    {
        return get_core_op()->get_latency_meters();
    }

    Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name)
    {
        return get_core_op()->get_boundary_vdma_channel_by_stream_name(stream_name);
    }

    virtual bool is_scheduled() const override
    {
        return get_core_op()->is_scheduled();
    }

    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override
    {
        return get_core_op()->set_scheduler_timeout(timeout, network_name);
    }

    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override
    {
        return get_core_op()->set_scheduler_threshold(threshold, network_name);
    }

    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override
    {
        return get_core_op()->get_default_streams_interface();
    }

    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) override
    {
        return get_core_op()->set_scheduler_priority(priority, network_name);
    }

    std::vector<std::shared_ptr<CoreOp>> &get_core_ops()
    {
        return m_core_ops;
    }

    virtual hailo_status before_fork() override;

    Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &key);
    Expected<OutputStreamPtrVector> get_output_streams_by_vstream_name(const std::string &name);

    virtual hailo_status infer_async(const NamedBuffersCallbacks &named_buffers_callbacks,
        const std::function<void(hailo_status)> &infer_request_done_cb) override;

    virtual Expected<std::unique_ptr<LayerInfo>> get_layer_info(const std::string &stream_name) override;
    virtual Expected<std::vector<net_flow::PostProcessOpMetadataPtr>> get_ops_metadata() override;
    Expected<net_flow::PostProcessOpMetadataPtr> get_op_meta_data(const std::string &edge_name);

    virtual hailo_status set_nms_score_threshold(const std::string &edge_name, float32_t nms_score_threshold) override;
    virtual hailo_status set_nms_iou_threshold(const std::string &edge_name, float32_t iou_threshold) override;
    virtual hailo_status set_nms_max_bboxes_per_class(const std::string &edge_name, uint32_t max_bboxes_per_class) override;
    virtual hailo_status set_nms_max_bboxes_total(const std::string &edge_name, uint32_t max_bboxes_total) override;
    virtual hailo_status set_nms_max_accumulated_mask_size(const std::string &edge_name, uint32_t max_accumulated_mask_size) override;

    Expected<std::shared_ptr<net_flow::NmsOpMetadata>> get_nms_meta_data(const std::string &edge_name);

    virtual hailo_status init_cache(uint32_t read_offset) override;
    virtual hailo_status update_cache_offset(int32_t offset_delta_entries) override;
    virtual Expected<std::vector<uint32_t>> get_cache_ids() const override;
    virtual Expected<Buffer> read_cache_buffer(uint32_t cache_id) override;
    virtual hailo_status write_cache_buffer(uint32_t cache_id, MemoryView buffer) override;

private:
    ConfiguredNetworkGroupBase(const ConfigureNetworkParams &config_params,
        std::vector<std::shared_ptr<CoreOp>> &&core_ops, NetworkGroupMetadata &&metadata);

    hailo_status add_mux_streams_by_edges_names(OutputStreamWithParamsVector &result,
        const std::unordered_map<std::string, hailo_vstream_params_t> &outputs_edges_params);

    hailo_status activate_low_level_streams();
    hailo_status deactivate_low_level_streams();
    Expected<uint32_t> get_cache_length() const;
    Expected<uint32_t> get_cache_read_length() const;
    Expected<uint32_t> get_cache_write_length() const;
    Expected<uint32_t> get_cache_entry_size(uint32_t cache_id) const;

    const ConfigureNetworkParams m_config_params;
    std::vector<std::shared_ptr<CoreOp>> m_core_ops;
    NetworkGroupMetadata m_network_group_metadata;
    bool m_is_shutdown = false;
    bool m_is_forked;

    std::mutex m_mutex;

    friend class VDeviceCoreOp;
    friend class AsyncPipelineBuilder;
};

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_INTERNAL_HPP_ */
