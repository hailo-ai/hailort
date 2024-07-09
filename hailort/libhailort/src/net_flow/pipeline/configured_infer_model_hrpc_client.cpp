/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file configured_infer_model_hrpc_client.cpp
 * @brief ConfiguredInferModel HRPC client implementation
 **/

#include "configured_infer_model_hrpc_client.hpp"
#include "hailo/hailort.h"
#include <iostream>

namespace hailort
{

Expected<MemoryView> InferStreamOnStack::get_buffer()
{
    return MemoryView(m_buffer);
}

Expected<OutputBindingsOnStack> OutputBindingsOnStack::create(ConfiguredInferModel::Bindings bindings,
    const std::vector<std::string> &outputs_names)
{
    std::unordered_map<std::string, InferStreamOnStack> output_streams;
    for (const auto &output_name : outputs_names) {
        TRY(auto output, bindings.output(output_name));
        TRY(auto buffer, output.get_buffer());
        output_streams.emplace(output_name, InferStreamOnStack(buffer));
    }
    return OutputBindingsOnStack(std::move(output_streams));
}

Expected<InferStreamOnStack> OutputBindingsOnStack::output()
{
    CHECK_AS_EXPECTED(1 == m_output_streams.size(), HAILO_INVALID_OPERATION, "Model has more than one output!");
    auto copy = m_output_streams.begin()->second;
    return copy;
}

Expected<InferStreamOnStack> OutputBindingsOnStack::output(const std::string &name)
{
    CHECK_AS_EXPECTED(contains(m_output_streams, name), HAILO_NOT_FOUND, "Output {}, not found!", name);
    auto copy = m_output_streams.at(name);
    return copy;
}

AsyncInferJobHrpcClient::AsyncInferJobHrpcClient(EventPtr event) : m_event(event)
{
}

hailo_status AsyncInferJobHrpcClient::wait(std::chrono::milliseconds timeout)
{
    return m_event->wait(timeout);
}

CallbacksQueue::CallbacksQueue(std::shared_ptr<hrpc::Client> client, const std::vector<std::string> &outputs_names) :
    m_outputs_names(outputs_names)
{
    client->register_custom_reply(HailoRpcActionID::CALLBACK_CALLED,
    [this, &outputs_names] (const MemoryView &serialized_reply, hrpc::RpcConnection connection) -> hailo_status {
        TRY(auto tuple, CallbackCalledSerializer::deserialize_reply(serialized_reply));

        auto callback_status = std::get<0>(tuple);
        auto callback_handle_id = std::get<1>(tuple);

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            CHECK(contains(m_callbacks, callback_handle_id), HAILO_NOT_FOUND, "Callback handle not found!");
            m_callbacks_status[callback_handle_id] = callback_status;

            if (HAILO_SUCCESS == callback_status) {
                CHECK(contains(m_bindings, callback_handle_id), HAILO_NOT_FOUND, "Callback handle not found!");
                for (const auto &output_name : outputs_names) {
                    TRY(auto buffer, m_bindings.at(callback_handle_id).output(output_name)->get_buffer());
                    auto status = connection.read_buffer(buffer);
                    // TODO: Errors here should be unrecoverable (HRT-14275)
                    CHECK_SUCCESS(status);
                }
            }
            m_callbacks_queue.push(callback_handle_id);
        }

        m_cv.notify_one();
        return HAILO_SUCCESS;
    });

    m_is_running = true;
    m_callback_thread = std::thread([this] {
        while (true) {
            callback_id_t callback_id;
            hailo_status info_status = HAILO_UNINITIALIZED;
            std::function<void(const AsyncInferCompletionInfo&)> cb;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return !m_is_running || !m_callbacks_queue.empty(); });
                if (!m_is_running) {
                    break;
                }

                callback_id = m_callbacks_queue.front();
                m_callbacks_queue.pop();

                m_cv.wait(lock, [this, callback_id] { return !m_is_running || (m_callbacks.find(callback_id) != m_callbacks.end()); });
                if (!m_is_running) {
                    break;
                }

                info_status = m_callbacks_status[callback_id];
                cb = m_callbacks[callback_id];
                m_callbacks.erase(callback_id);
                m_callbacks_status.erase(callback_id);
                m_bindings.erase(callback_id);
            }
            AsyncInferCompletionInfo info(info_status);
            cb(info);
        }
    });
}

CallbacksQueue::~CallbacksQueue()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_is_running = false;
    }
    m_cv.notify_one();
    m_callback_thread.join();
}
Expected<std::shared_ptr<AsyncInferJobHrpcClient>> CallbacksQueue::register_callback(callback_id_t id,
    ConfiguredInferModel::Bindings bindings,
    std::function<void(const AsyncInferCompletionInfo&)> callback)
{
    TRY(auto event_ptr, Event::create_shared(Event::State::not_signalled));

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        TRY(auto output_bindings, OutputBindingsOnStack::create(bindings, m_outputs_names));
        m_bindings.emplace(id, output_bindings);
        m_callbacks_status[id] = HAILO_SUCCESS;
        m_callbacks[id] = [callback, event_ptr] (const AsyncInferCompletionInfo &info) {
            auto status = event_ptr->signal();
            if (HAILO_SUCCESS != status) {
                LOGGER__CRITICAL("Could not signal event! status = {}", status);
            }
            callback(info);
        };
    }
    m_cv.notify_one();

    auto ptr = make_shared_nothrow<AsyncInferJobHrpcClient>(event_ptr);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

Expected<std::shared_ptr<ConfiguredInferModelHrpcClient>> ConfiguredInferModelHrpcClient::create(std::shared_ptr<hrpc::Client> client,
    rpc_object_handle_t handle_id, std::vector<hailo_vstream_info_t> &&input_vstream_infos,
    std::vector<hailo_vstream_info_t> &&output_vstream_infos, uint32_t max_ongoing_transfers,
    std::unique_ptr<CallbacksQueue> &&callbacks_queue, rpc_object_handle_t infer_model_id,
    const std::unordered_map<std::string, size_t> inputs_frame_sizes,
    const std::unordered_map<std::string, size_t> outputs_frame_sizes)
{
    // TODO: consider create a separate client object here - HRT-13687
    auto ptr = make_shared_nothrow<ConfiguredInferModelHrpcClient>(client, handle_id, std::move(input_vstream_infos),
        std::move(output_vstream_infos), max_ongoing_transfers, std::move(callbacks_queue), infer_model_id, inputs_frame_sizes,
        outputs_frame_sizes);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

ConfiguredInferModelHrpcClient::~ConfiguredInferModelHrpcClient()
{
    if (INVALID_HANDLE_ID == m_handle_id) {
        return;
    }

    auto request = DestroyConfiguredInferModelSerializer::serialize_request(m_handle_id);
    if (!request) {
        LOGGER__CRITICAL("Failed to serialize ConfiguredInferModel_release request");
        return;
    }

    auto client = m_client.lock();
    if (client) {
        auto result = client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__DESTROY, MemoryView(*request));
        if (!result) {
            LOGGER__CRITICAL("Failed to destroy configured infer model! status = {}", result.status());
        }

        if (HAILO_SUCCESS != DestroyConfiguredInferModelSerializer::deserialize_reply(MemoryView(*result))) {
            LOGGER__CRITICAL("Failed to destroy configured infer model! status = {}", result.status());
        }
    }
}

Expected<ConfiguredInferModel::Bindings> ConfiguredInferModelHrpcClient::create_bindings()
{
    std::unordered_map<std::string, ConfiguredInferModel::Bindings::InferStream> inputs;
    std::unordered_map<std::string, ConfiguredInferModel::Bindings::InferStream> outputs;

    for (const auto &vstream_info : m_input_vstream_infos) {
        TRY(auto stream, ConfiguredInferModelBase::create_infer_stream(vstream_info));
        inputs.emplace(vstream_info.name, std::move(stream));
    }

    for (const auto &vstream_info : m_output_vstream_infos) {
        TRY(auto stream, ConfiguredInferModelBase::create_infer_stream(vstream_info));
        outputs.emplace(vstream_info.name, std::move(stream));
    }

    TRY(auto bindings, ConfiguredInferModelBase::create_bindings(std::move(inputs), std::move(outputs)));
    return bindings;
}

hailo_status ConfiguredInferModelHrpcClient::wait_for_async_ready(std::chrono::milliseconds timeout, uint32_t frames_count)
{
    std::unique_lock<std::mutex> lock(m_ongoing_transfers_mutex);
    bool done = m_cv.wait_for(lock, timeout, [this, frames_count] () {
        return (m_max_ongoing_transfers - m_ongoing_transfers.load()) >= frames_count;
    });
    CHECK(done, HAILO_TIMEOUT);

    return HAILO_SUCCESS;
}

Expected<AsyncInferJob> ConfiguredInferModelHrpcClient::run_async(ConfiguredInferModel::Bindings bindings,
    std::function<void(const AsyncInferCompletionInfo &)> callback)
{
    auto async_job = run_async_impl(bindings, callback);
    if (HAILO_SUCCESS != async_job.status()) {
        shutdown();
        return make_unexpected(async_job.status());
    }
    return async_job.release();
}

Expected<AsyncInferJob> ConfiguredInferModelHrpcClient::run_async_impl(ConfiguredInferModel::Bindings bindings,
    std::function<void(const AsyncInferCompletionInfo &)> callback)
{
    CHECK_SUCCESS_AS_EXPECTED(validate_bindings(bindings));
    std::unique_lock<std::mutex> lock(m_infer_mutex);
    m_callbacks_counter++;
    auto callback_wrapper = [this, callback] (const AsyncInferCompletionInfo &info) {
        {
            std::unique_lock<std::mutex> transfers_lock(m_ongoing_transfers_mutex);
            m_ongoing_transfers--;
        }
        m_cv.notify_one();
        if (callback) {
            callback(info);
        }
    };

    TRY(auto job_ptr, m_callbacks_queue->register_callback(m_callbacks_counter, bindings, callback_wrapper));

    TRY(auto request, RunAsyncSerializer::serialize_request(m_handle_id, m_infer_model_handle_id,
        m_callbacks_counter));

    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto serialized_result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__RUN_ASYNC,
        MemoryView(request), [this, &bindings] (hrpc::RpcConnection connection) -> hailo_status {
        for (const auto &input_vstream : m_input_vstream_infos) {
            TRY(auto input, bindings.input(input_vstream.name));
            auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(input);
            switch(buffer_type) {
            case BufferType::VIEW:
            {
                TRY(auto buffer, input.get_buffer());
                auto status = connection.write_buffer(MemoryView(buffer));
                CHECK_SUCCESS(status);
                break;
            }
            case BufferType::PIX_BUFFER:
            {
                TRY(auto pix_buffer, input.get_pix_buffer());
                for (uint32_t i = 0; i < pix_buffer.number_of_planes; i++) {
                    auto status = connection.write_buffer(MemoryView(pix_buffer.planes[i].user_ptr, pix_buffer.planes[i].bytes_used));
                    CHECK_SUCCESS(status);
                }
                break;
            }
            case BufferType::DMA_BUFFER:
                LOGGER__CRITICAL("DMA_BUFFER is not supported in HRPC");
                return HAILO_NOT_IMPLEMENTED;
            default:
                LOGGER__CRITICAL("Unknown buffer type");
                return HAILO_INTERNAL_FAILURE;
            }
        }
        return HAILO_SUCCESS;
    }));
    auto status = RunAsyncSerializer::deserialize_reply(MemoryView(serialized_result));
    CHECK_SUCCESS_AS_EXPECTED(status);

    {
        std::unique_lock<std::mutex> transfers_lock(m_ongoing_transfers_mutex);
        m_ongoing_transfers++;
    }

    return AsyncInferJobBase::create(job_ptr);
}

hailo_status ConfiguredInferModelHrpcClient::set_scheduler_timeout(const std::chrono::milliseconds &timeout)
{
    TRY(auto serialized_request, SetSchedulerTimeoutSerializer::serialize_request(m_handle_id, timeout));
    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_TIMEOUT, MemoryView(serialized_request)));
    CHECK_SUCCESS(SetSchedulerTimeoutSerializer::deserialize_reply(MemoryView(result)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::set_scheduler_threshold(uint32_t threshold)
{
    TRY(auto serialized_request, SetSchedulerThresholdSerializer::serialize_request(m_handle_id, threshold));
    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_THRESHOLD, MemoryView(serialized_request)));
    CHECK_SUCCESS(SetSchedulerThresholdSerializer::deserialize_reply(MemoryView(result)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::set_scheduler_priority(uint8_t priority)
{
    TRY(auto serialized_request, SetSchedulerPrioritySerializer::serialize_request(m_handle_id, priority));
    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_PRIORITY, MemoryView(serialized_request)));
    CHECK_SUCCESS(SetSchedulerPrioritySerializer::deserialize_reply(MemoryView(result)));

    return HAILO_SUCCESS;
}

Expected<LatencyMeasurementResult> ConfiguredInferModelHrpcClient::get_hw_latency_measurement()
{
    TRY(auto serialized_request, GetHwLatencyMeasurementSerializer::serialize_request(m_handle_id));
    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__GET_HW_LATENCY_MEASUREMENT, MemoryView(serialized_request)));

    TRY(auto tuple, GetHwLatencyMeasurementSerializer::deserialize_reply(MemoryView(result)));

    auto status = std::get<0>(tuple);
    if (HAILO_NOT_AVAILABLE == status) {
        return make_unexpected(HAILO_NOT_AVAILABLE);
    }
    CHECK_SUCCESS(status);

    auto avg_hw_latency = std::get<1>(tuple);
    LatencyMeasurementResult latency_measurement_result {avg_hw_latency};

    return latency_measurement_result;
};

hailo_status ConfiguredInferModelHrpcClient::activate()
{
    TRY(auto serialized_request, ActivateSerializer::serialize_request(m_handle_id));
    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__ACTIVATE, MemoryView(serialized_request)));

    CHECK_SUCCESS(ActivateSerializer::deserialize_reply(MemoryView(result)));

    return HAILO_SUCCESS;
};

hailo_status ConfiguredInferModelHrpcClient::deactivate()
{
    TRY(auto serialized_request, DeactivateSerializer::serialize_request(m_handle_id));
    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__DEACTIVATE, MemoryView(serialized_request)));

    CHECK_SUCCESS(DeactivateSerializer::deserialize_reply(MemoryView(result)));

    return HAILO_SUCCESS;
};

Expected<size_t> ConfiguredInferModelHrpcClient::get_async_queue_size()
{
    size_t queue_size = m_max_ongoing_transfers;
    return queue_size;
}

hailo_status ConfiguredInferModelHrpcClient::validate_bindings(ConfiguredInferModel::Bindings bindings)
{
    for (const auto &input_vstream : m_input_vstream_infos) {
        TRY(auto input, bindings.input(input_vstream.name));

        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(input);
        switch (buffer_type) {
            case BufferType::VIEW:
            {
                auto buffer = input.get_buffer();
                CHECK_EXPECTED_AS_STATUS(buffer);
                CHECK(buffer->size() == m_inputs_frame_sizes.at(input_vstream.name), HAILO_INVALID_OPERATION,
                    "Input buffer size {} is different than expected {} for input '{}'", buffer->size(), m_inputs_frame_sizes.at(input_vstream.name), input_vstream.name);
                break;
            }
            case BufferType::PIX_BUFFER:
            {
                auto buffer = input.get_pix_buffer();
                CHECK_EXPECTED_AS_STATUS(buffer);
                size_t buffer_size = 0;
                for (size_t i = 0 ; i < buffer->number_of_planes ; i++) {
                    buffer_size += buffer->planes[i].bytes_used;
                }

                CHECK(buffer_size == m_inputs_frame_sizes.at(input_vstream.name), HAILO_INVALID_OPERATION,
                    "Input buffer size {} is different than expected {} for input '{}'", buffer_size, m_inputs_frame_sizes.at(input_vstream.name), input_vstream.name);
                break;
            }
            case BufferType::DMA_BUFFER:
            {
                auto buffer = input.get_dma_buffer();
                CHECK_EXPECTED_AS_STATUS(buffer);
                CHECK(buffer->size == m_inputs_frame_sizes.at(input_vstream.name), HAILO_INVALID_OPERATION,
                    "Input buffer size {} is different than expected {} for input '{}'", buffer->size, m_inputs_frame_sizes.at(input_vstream.name), input_vstream.name);
                break;
            }
            default:
                CHECK(false, HAILO_NOT_FOUND, "Couldnt find input buffer for '{}'", input_vstream.name);
        }
    }
    for (const auto &output_vstream : m_output_vstream_infos) {
        TRY(auto output, bindings.output(output_vstream.name));
        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(output);
        switch (buffer_type) {
            case BufferType::VIEW:
            {
                auto buffer = output.get_buffer();
                CHECK_EXPECTED_AS_STATUS(buffer);
                CHECK(buffer->size() == m_outputs_frame_sizes.at(output_vstream.name), HAILO_INVALID_OPERATION,
                    "Output buffer size {} is different than expected {} for output '{}'", buffer->size(), m_outputs_frame_sizes.at(output_vstream.name), output_vstream.name);
                break;
            }
            case BufferType::PIX_BUFFER:
            {
                CHECK(false, HAILO_NOT_SUPPORTED, "pix_buffer isn't supported for outputs in '{}'", output_vstream.name);
                break;
            }
            case BufferType::DMA_BUFFER:
            {
                auto buffer = output.get_dma_buffer();
                CHECK_EXPECTED_AS_STATUS(buffer);
                CHECK(buffer->size == m_outputs_frame_sizes.at(output_vstream.name), HAILO_INVALID_OPERATION,
                    "Output buffer size {} is different than expected {} for out '{}'", buffer->size, m_outputs_frame_sizes.at(output_vstream.name), output_vstream.name);
                break;
            }
            default:
                CHECK(false, HAILO_NOT_FOUND, "Couldnt find output buffer for '{}'", output_vstream.name);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::shutdown()
{
    TRY(auto serialized_request, ShutdownSerializer::serialize_request(m_handle_id));
    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__SHUTDOWN, MemoryView(serialized_request)));

    CHECK_SUCCESS(ShutdownSerializer::deserialize_reply(MemoryView(result)));

    return HAILO_SUCCESS;
}


} // namespace hailort
