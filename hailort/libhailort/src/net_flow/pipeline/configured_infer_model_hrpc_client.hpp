/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
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

using callback_id_t = uint32_t;

class InferStreamOnStack final
{
public:
    InferStreamOnStack(MemoryView buffer) : m_buffer(buffer) {}
    Expected<MemoryView> get_buffer();

private:
    MemoryView m_buffer;
};

class OutputBindingsOnStack final
{
public:
    static Expected<OutputBindingsOnStack> create(ConfiguredInferModel::Bindings bindings,
        const std::vector<std::string> &outputs_names);
    Expected<InferStreamOnStack> output();
    Expected<InferStreamOnStack> output(const std::string &name);

private:
    OutputBindingsOnStack(std::unordered_map<std::string, InferStreamOnStack> &&output_streams) :
        m_output_streams(std::move(output_streams)) {}

    std::unordered_map<std::string, InferStreamOnStack> m_output_streams;
};

class AsyncInferJobHrpcClient : public AsyncInferJobBase
{
public:
    AsyncInferJobHrpcClient(EventPtr event);

    virtual hailo_status wait(std::chrono::milliseconds timeout) override;

private:
    EventPtr m_event;
};

class CallbacksQueue
{
public:
    CallbacksQueue(std::shared_ptr<hrpc::Client> client, const std::vector<std::string> &outputs_names);
    ~CallbacksQueue();

    CallbacksQueue(const CallbacksQueue &other) = delete;
    CallbacksQueue& operator=(const CallbacksQueue &other) = delete;
    CallbacksQueue(CallbacksQueue &&other) = delete;
    CallbacksQueue& operator=(CallbacksQueue &&other) = delete;

    Expected<std::shared_ptr<AsyncInferJobHrpcClient>> register_callback(callback_id_t id,
        ConfiguredInferModel::Bindings bindings,
        std::function<void(const AsyncInferCompletionInfo&)> callback);

private:
    const std::vector<std::string> m_outputs_names;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<callback_id_t> m_callbacks_queue;
    std::unordered_map<callback_id_t, std::function<void(const AsyncInferCompletionInfo&)>> m_callbacks;
    std::atomic_bool m_is_running;
    std::thread m_callback_thread;
    std::unordered_map<callback_id_t, OutputBindingsOnStack> m_bindings;
    std::unordered_map<callback_id_t, hailo_status> m_callbacks_status;
};

class ConfiguredInferModelHrpcClient : public ConfiguredInferModelBase
{
public:
    static Expected<std::shared_ptr<ConfiguredInferModelHrpcClient>> create(std::shared_ptr<hrpc::Client> client,
        rpc_object_handle_t handle_id, std::vector<hailo_vstream_info_t> &&input_vstream_infos,
        std::vector<hailo_vstream_info_t> &&output_vstream_infos, uint32_t max_ongoing_transfers,
        std::unique_ptr<CallbacksQueue> &&callbacks_queue, rpc_object_handle_t infer_model_handle_id,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes,
        const std::unordered_map<std::string, size_t> outputs_frame_sizes);
    ConfiguredInferModelHrpcClient(std::shared_ptr<hrpc::Client> client, rpc_object_handle_t handle_id,
        std::vector<hailo_vstream_info_t> &&input_vstream_infos, std::vector<hailo_vstream_info_t> &&output_vstream_infos,
        uint32_t max_ongoing_transfers, std::unique_ptr<CallbacksQueue> &&callbacks_queue, rpc_object_handle_t infer_model_handle_id,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes,
        const std::unordered_map<std::string, size_t> outputs_frame_sizes) :
            ConfiguredInferModelBase(inputs_frame_sizes, outputs_frame_sizes),
            m_client(client), m_handle_id(handle_id), m_input_vstream_infos(std::move(input_vstream_infos)),
            m_output_vstream_infos(std::move(output_vstream_infos)), m_max_ongoing_transfers(max_ongoing_transfers),
            m_ongoing_transfers(0), m_callbacks_queue(std::move(callbacks_queue)), m_infer_model_handle_id(infer_model_handle_id),
            m_callbacks_counter(0) {}
    virtual ~ConfiguredInferModelHrpcClient();

    ConfiguredInferModelHrpcClient(const ConfiguredInferModelHrpcClient &) = delete;
    ConfiguredInferModelHrpcClient &operator=(const ConfiguredInferModelHrpcClient &) = delete;
    ConfiguredInferModelHrpcClient(ConfiguredInferModelHrpcClient &&) = delete;
    ConfiguredInferModelHrpcClient &operator=(ConfiguredInferModelHrpcClient &&) = delete;

    virtual Expected<ConfiguredInferModel::Bindings> create_bindings() override;
    virtual hailo_status wait_for_async_ready(std::chrono::milliseconds timeout, uint32_t frames_count) override;

    virtual hailo_status activate() override;
    virtual hailo_status deactivate() override;

    virtual Expected<AsyncInferJob> run_async(ConfiguredInferModel::Bindings bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback) override;

    virtual Expected<LatencyMeasurementResult> get_hw_latency_measurement() override;

    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold) override;
    virtual hailo_status set_scheduler_priority(uint8_t priority) override;

    virtual Expected<size_t> get_async_queue_size() override;

    virtual hailo_status shutdown() override;

private:
    virtual hailo_status validate_bindings(ConfiguredInferModel::Bindings bindings);
    Expected<AsyncInferJob> run_async_impl(ConfiguredInferModel::Bindings bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback);

    std::weak_ptr<hrpc::Client> m_client;
    rpc_object_handle_t m_handle_id;
    std::vector<hailo_vstream_info_t> m_input_vstream_infos;
    std::vector<hailo_vstream_info_t> m_output_vstream_infos;
    uint32_t m_max_ongoing_transfers;
    std::mutex m_ongoing_transfers_mutex;
    std::condition_variable m_cv;
    std::atomic_uint32_t m_ongoing_transfers;
    std::unique_ptr<CallbacksQueue> m_callbacks_queue;
    rpc_object_handle_t m_infer_model_handle_id;
    std::atomic_uint32_t m_callbacks_counter;
    std::mutex m_infer_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_CONFIGURED_INFER_MODEL_HRPC_CLIENT_HPP_ */
