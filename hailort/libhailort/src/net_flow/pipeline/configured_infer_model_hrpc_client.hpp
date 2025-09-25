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
#include "rpc_callbacks/rpc_callbacks_dispatcher.hpp"

namespace hailort
{

class AsyncInferJobHrpcClient : public AsyncInferJobBase
{
public:
    AsyncInferJobHrpcClient(EventPtr event);

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
        uint32_t max_ongoing_transfers, std::shared_ptr<ClientCallbackDispatcher> callback_dispatcher, rpc_object_handle_t infer_model_handle_id,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes,
        const std::unordered_map<std::string, size_t> outputs_frame_sizes) :
            ConfiguredInferModelBase(inputs_frame_sizes, outputs_frame_sizes),
            m_client(client), m_handle_id(handle_id), m_input_vstream_infos(std::move(input_vstream_infos)),
            m_output_vstream_infos(std::move(output_vstream_infos)), m_max_ongoing_transfers(max_ongoing_transfers),
            m_ongoing_transfers(0), m_callback_dispatcher(std::move(callback_dispatcher)), m_infer_model_handle_id(infer_model_handle_id),
            m_callbacks_counter(0) {}
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

private:
    virtual hailo_status validate_bindings(const ConfiguredInferModel::Bindings &bindings) override;
    Expected<AsyncInferJob> run_async_impl(const ConfiguredInferModel::Bindings &bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback);
    Expected<std::vector<uint32_t>> get_input_buffer_sizes(const ConfiguredInferModel::Bindings &bindings);
    Expected<std::vector<TransferBuffer>> get_async_inputs(const ConfiguredInferModel::Bindings &bindings);
    hailo_status shutdown_impl();

    std::weak_ptr<Client> m_client;
    rpc_object_handle_t m_handle_id;
    std::vector<hailo_vstream_info_t> m_input_vstream_infos;
    std::vector<hailo_vstream_info_t> m_output_vstream_infos;
    uint32_t m_max_ongoing_transfers;
    std::mutex m_ongoing_transfers_mutex;
    std::condition_variable m_cv;
    std::atomic_uint32_t m_ongoing_transfers;
    std::shared_ptr<ClientCallbackDispatcher> m_callback_dispatcher;
    rpc_object_handle_t m_infer_model_handle_id;
    std::atomic_uint32_t m_callbacks_counter;
    std::mutex m_infer_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_CONFIGURED_INFER_MODEL_HRPC_CLIENT_HPP_ */
