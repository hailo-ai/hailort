/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file configured_infer_model_hrpc_client.hpp
 * @brief ConfiguredInferModel HRPC client, represents the user's handle to the ConfiguredInferModel object (held in the hailort server)
 **/

#ifndef _HAILO_CONFIGURED_INFER_MODEL_HRPC_CLIENT_HPP_
#define _HAILO_CONFIGURED_INFER_MODEL_HRPC_CLIENT_HPP_

#include "hailo/infer_model.hpp"
#include "infer_model_internal.hpp"
#include "hrpc/client.hpp"

namespace hailort
{

class AsyncInferJobHrpcClient : public AsyncInferJobBase
{
public:
    AsyncInferJobHrpcClient(EventPtr event) : m_event(event), m_job_status(HAILO_UNINITIALIZED) {}
    static Expected<std::shared_ptr<AsyncInferJobHrpcClient>> create_shared();

    virtual hailo_status wait(std::chrono::milliseconds timeout) override;
    hailo_status set_status(hailo_status status);
private:
    EventPtr m_event;
    std::atomic<hailo_status> m_job_status;
};

class ConfiguredInferModelHrpcClient : public ConfiguredInferModelBase
{
public:
    static Expected<std::shared_ptr<ConfiguredInferModelHrpcClient>> create(std::shared_ptr<Client> client,
        rpc_object_handle_t handle_id, std::vector<hailo_vstream_info_t> &&input_vstream_infos,
        std::vector<hailo_vstream_info_t> &&output_vstream_infos, uint32_t max_ongoing_transfers,
        rpc_object_handle_t infer_model_handle_id, const std::unordered_map<std::string, size_t> inputs_frame_sizes,
        const std::unordered_map<std::string, size_t> outputs_frame_sizes);
    ConfiguredInferModelHrpcClient(std::shared_ptr<Client> client, rpc_object_handle_t handle_id,
        std::vector<hailo_vstream_info_t> &&input_vstream_infos, std::vector<hailo_vstream_info_t> &&output_vstream_infos,
        uint32_t max_ongoing_transfers, rpc_object_handle_t infer_model_handle_id,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes,
        const std::unordered_map<std::string, size_t> outputs_frame_sizes);
    virtual ~ConfiguredInferModelHrpcClient();

    ConfiguredInferModelHrpcClient(const ConfiguredInferModelHrpcClient &) = delete;
    ConfiguredInferModelHrpcClient &operator=(const ConfiguredInferModelHrpcClient &) = delete;
    ConfiguredInferModelHrpcClient(ConfiguredInferModelHrpcClient &&) = delete;
    ConfiguredInferModelHrpcClient &operator=(ConfiguredInferModelHrpcClient &&) = delete;

    virtual Expected<ConfiguredInferModel::Bindings> create_bindings(const std::map<std::string, MemoryView> &buffers) override;
    virtual hailo_status wait_for_async_ready(std::chrono::milliseconds timeout, uint32_t frames_count) override;

    virtual hailo_status activate() override;
    virtual hailo_status deactivate() override;

    virtual Expected<AsyncInferJob> run_async(const ConfiguredInferModel::Bindings &bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback) override;

    virtual Expected<LatencyMeasurementResult> get_hw_latency_measurement() override;

    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold) override;
    virtual hailo_status set_scheduler_priority(uint8_t priority) override;

    virtual Expected<size_t> get_async_queue_size() const override;

    virtual hailo_status shutdown() override;
    virtual hailo_status update_cache_offset(int32_t offset_delta_entries) override;
    virtual hailo_status init_cache(uint32_t read_offset) override;
    virtual Expected<std::unordered_map<uint32_t, BufferPtr>> get_cache_buffers() override;
    virtual hailo_status update_cache_buffer(uint32_t cache_id, MemoryView buffer) override;
    virtual Expected<AsyncInferJob> run_async_for_duration(const ConfiguredInferModel::Bindings &bindings, uint32_t duration_ms,
        uint32_t sleep_between_frames_ms, std::function<void(const AsyncInferCompletionInfo &, uint32_t)> callback) override;

private:
    virtual hailo_status validate_bindings(const ConfiguredInferModel::Bindings &bindings) override;
    hailo_status validate_input_bindings(const ConfiguredInferModel::Bindings &bindings);
    hailo_status validate_output_bindings(const ConfiguredInferModel::Bindings &bindings);
    Expected<AsyncInferJob> run_async_impl(const ConfiguredInferModel::Bindings &bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback);
    Expected<std::vector<RunAsyncSerializer::BufferInfo>> get_buffer_infos(const ConfiguredInferModel::Bindings &bindings);
    hailo_status push_input_buffer_infos(const ConfiguredInferModel::Bindings &bindings, std::vector<RunAsyncSerializer::BufferInfo> &buffer_infos);
    hailo_status push_output_buffer_infos(const ConfiguredInferModel::Bindings &bindings, std::vector<RunAsyncSerializer::BufferInfo> &buffer_infos);
    Expected<std::vector<TransferBuffer>> get_write_buffers(const ConfiguredInferModel::Bindings &bindings);
    Expected<std::vector<TransferBuffer>> get_read_buffers(const ConfiguredInferModel::Bindings &bindings);

    std::weak_ptr<Client> m_client;
    rpc_object_handle_t m_handle_id;
    std::vector<hailo_vstream_info_t> m_input_vstream_infos;
    std::vector<hailo_vstream_info_t> m_output_vstream_infos;
    uint32_t m_max_ongoing_transfers;
    std::mutex m_ongoing_transfers_mutex;
    std::condition_variable m_cv;
    std::atomic_uint32_t m_ongoing_transfers;
    rpc_object_handle_t m_infer_model_handle_id;
    std::mutex m_infer_mutex;

    // Used in order to avoid implicitly converting the names from char* to std::string in every call to bindings input/output
    std::vector<std::string> m_input_names;
    std::vector<std::string> m_output_names;
};

} /* namespace hailort */

#endif /* _HAILO_CONFIGURED_INFER_MODEL_HRPC_CLIENT_HPP_ */
