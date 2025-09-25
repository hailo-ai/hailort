/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_client.hpp
 * @brief Network Group client for HailoRT gRPC Service
 **/

#ifndef _HAILO_NETWORK_GROUP_CLIENT_HPP_
#define _HAILO_NETWORK_GROUP_CLIENT_HPP_

#include "hailo/hailort.h"
#include "service/buffer_pool_per_stream.hpp"
#include "network_group/network_group_internal.hpp"
#include "service/hailort_rpc_client.hpp"
#include "rpc/rpc_definitions.hpp"

namespace hailort
{

class ConfiguredNetworkGroupClient : public ConfiguredNetworkGroup
{
public:
    static Expected<std::shared_ptr<ConfiguredNetworkGroupClient>> create(std::unique_ptr<HailoRtRpcClient> client,
        NetworkGroupIdentifier &&identifier);
    ConfiguredNetworkGroupClient(std::unique_ptr<HailoRtRpcClient> client, NetworkGroupIdentifier &&identifier,
        const std::string &network_group_name, std::unordered_set<std::string> &&input_streams_names,
        std::unordered_set<std::string> &&output_streams_names, std::shared_ptr<BufferPoolPerStream> buffer_pool_per_stream);

    virtual ~ConfiguredNetworkGroupClient();
    ConfiguredNetworkGroupClient(const ConfiguredNetworkGroupClient &other) = delete;
    ConfiguredNetworkGroupClient &operator=(const ConfiguredNetworkGroupClient &other) = delete;
    ConfiguredNetworkGroupClient &operator=(ConfiguredNetworkGroupClient &&other) = delete;
    ConfiguredNetworkGroupClient(ConfiguredNetworkGroupClient &&other) = delete;

    virtual const std::string& get_network_group_name() const override;
    virtual const std::string& name() const override;
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() override;
    virtual std::vector<std::reference_wrapper<InputStream>> get_input_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual std::vector<std::reference_wrapper<OutputStream>> get_output_streams_by_interface(hailo_stream_interface_t stream_interface) override;
    virtual ExpectedRef<InputStream> get_input_stream_by_name(const std::string &name) override;
    virtual ExpectedRef<OutputStream> get_output_stream_by_name(const std::string &name) override;
    virtual Expected<InputStreamRefVector> get_input_streams_by_network(const std::string &network_name="") override;
    virtual Expected<OutputStreamRefVector> get_output_streams_by_network(const std::string &network_name="") override;
    virtual InputStreamRefVector get_input_streams() override;
    virtual OutputStreamRefVector get_output_streams() override;
    virtual Expected<OutputStreamWithParamsVector> get_output_streams_from_vstream_names(
        const std::map<std::string, hailo_vstream_params_t> &outputs_params) override;

    virtual Expected<LatencyMeasurementResult> get_latency_measurement(const std::string &network_name="") override;
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate(const hailo_activate_network_group_params_t &network_group_params) override;
    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout) override;
    virtual hailo_status shutdown() override;

    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_input_vstream_params(
        bool unused, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") override;
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_output_vstream_params(
        bool unused, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") override;
    virtual Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> make_output_vstream_params_groups(
        bool unused, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size) override;
    virtual Expected<std::vector<std::vector<std::string>>> get_output_vstream_groups() override;

    virtual Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_network_info_t>> get_network_infos() const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &network_name="") const override;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &network_name="") const override;

    virtual bool is_scheduled() const override;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) override;
    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) override;

    virtual AccumulatorPtr get_activation_time_accumulator() const override;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const override;

    virtual bool is_multi_context() const override;
    virtual const ConfigureNetworkParams get_config_params() const override;

    virtual Expected<std::vector<std::string>> get_sorted_output_names() override;
    virtual Expected<std::vector<std::string>> get_stream_names_from_vstream_name(const std::string &vstream_name) override;
    virtual Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name) override;

    virtual Expected<HwInferResults> run_hw_infer_estimator() override;

    virtual Expected<std::vector<InputVStream>> create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params);
    virtual Expected<std::vector<OutputVStream>> create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params);
    virtual Expected<size_t> infer_queue_size() const override;

    virtual hailo_status before_fork() override;
    virtual hailo_status after_fork_in_parent() override;
    virtual hailo_status after_fork_in_child() override;

    virtual Expected<uint32_t> get_client_handle() const override
    {
        auto val = m_identifier.m_network_group_handle;
        return val;
    };

    virtual Expected<uint32_t> get_vdevice_client_handle() const override
    {
        auto val = m_identifier.m_vdevice_identifier.m_vdevice_handle;
        return val;
    };

    static Expected<std::shared_ptr<ConfiguredNetworkGroupClient>> duplicate_network_group_client(uint32_t handle, uint32_t vdevice_handle,
        const std::string &network_group_name);

    virtual hailo_status infer_async(const NamedBuffersCallbacks &named_buffers_callbacks,
        const std::function<void(hailo_status)> &infer_request_done_cb) override;
    hailo_status execute_callback(const ProtoCallbackIdentifier &cb_id);

    void execute_callbacks_on_error(hailo_status error_status);

    virtual Expected<std::unique_ptr<LayerInfo>> get_layer_info(const std::string &stream_name) override;
    virtual Expected<std::vector<net_flow::PostProcessOpMetadataPtr>> get_ops_metadata() override;

    virtual hailo_status set_nms_score_threshold(const std::string &edge_name, float32_t nms_score_threshold) override;
    virtual hailo_status set_nms_iou_threshold(const std::string &edge_name, float32_t iou_threshold) override;
    virtual hailo_status set_nms_max_bboxes_per_class(const std::string &edge_name, uint32_t max_bboxes_per_class) override;
    virtual hailo_status set_nms_max_bboxes_total(const std::string &edge_name, uint32_t max_bboxes_total) override;
    virtual hailo_status set_nms_max_accumulated_mask_size(const std::string &edge_name, uint32_t max_accumulated_mask_size) override;

    virtual hailo_status init_cache(uint32_t read_offset) override;
    virtual hailo_status update_cache_offset(int32_t offset_delta_entries) override;
    virtual Expected<std::vector<uint32_t>> get_cache_ids() const override;
    virtual Expected<Buffer> read_cache_buffer(uint32_t cache_id) override;
    virtual hailo_status write_cache_buffer(uint32_t cache_id, MemoryView buffer) override;

private:
    ConfiguredNetworkGroupClient(NetworkGroupIdentifier &&identifier, const std::string &network_group_name);
    hailo_status create_client();
    hailo_status dup_handle();
    callback_idx_t get_unique_callback_idx();
    hailo_status execute_infer_request_callback(const ProtoCallbackIdentifier &cb_id);
    hailo_status execute_transfer_callback(const ProtoCallbackIdentifier &cb_id);
    Expected<std::vector<StreamCbParamsPtr>> create_streams_callbacks_params(const NamedBuffersCallbacks &named_buffers_callbacks);
    hailo_status copy_data_from_shm_buffer(StreamCbParamsPtr stream_callback, const ProtoCallbackIdentifier &cb_id);

    std::unique_ptr<HailoRtRpcClient> m_client;
    NetworkGroupIdentifier m_identifier;
    std::string m_network_group_name;
    std::atomic<callback_idx_t> m_current_cb_index;
    std::unordered_set<std::string> m_input_streams_names;
    std::unordered_set<std::string> m_output_streams_names;
    std::shared_ptr<BufferPoolPerStream> m_buffer_pool_per_stream;
    std::mutex m_mutex;
    std::unordered_map<callback_idx_t, StreamCbParamsPtr> m_idx_to_callbacks;
    std::unordered_map<callback_idx_t, std::function<void(hailo_status)>> m_infer_request_idx_to_callbacks;
    std::mutex m_shutdown_mutex;
    std::atomic<bool> m_is_shutdown;
};

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_CLIENT_HPP_ */
