/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file configured_infer_model_hrpc_client.cpp
 * @brief ConfiguredInferModel HRPC client implementation
 **/

#include "configured_infer_model_hrpc_client.hpp"
#include "hailo/hailort.h"

namespace hailort
{

Expected<std::shared_ptr<ConfiguredInferModelHrpcClient>> ConfiguredInferModelHrpcClient::create(std::shared_ptr<Client> client,
    rpc_object_handle_t handle_id, std::vector<hailo_vstream_info_t> &&input_vstream_infos,
    std::vector<hailo_vstream_info_t> &&output_vstream_infos, uint32_t max_ongoing_transfers,
    std::shared_ptr<CallbacksQueue> callbacks_queue, rpc_object_handle_t infer_model_id,
    const std::unordered_map<std::string, size_t> inputs_frame_sizes,
    const std::unordered_map<std::string, size_t> outputs_frame_sizes)
{
    // TODO: consider create a separate client object here - HRT-13687
    auto ptr = make_shared_nothrow<ConfiguredInferModelHrpcClient>(client, handle_id, std::move(input_vstream_infos),
        std::move(output_vstream_infos), max_ongoing_transfers, callbacks_queue, infer_model_id, inputs_frame_sizes,
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
            return;
        }

        auto status = DestroyConfiguredInferModelSerializer::deserialize_reply(MemoryView(*result));
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Failed to destroy configured infer model! status = {}", status);
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
    CHECK(done, HAILO_TIMEOUT, "Waiting for async pipeline to be ready has timed out!");

    return HAILO_SUCCESS;
}

Expected<AsyncInferJob> ConfiguredInferModelHrpcClient::run_async(const ConfiguredInferModel::Bindings &bindings,
    std::function<void(const AsyncInferCompletionInfo &)> callback)
{
    auto async_job = run_async_impl(bindings, callback);
    if (HAILO_SUCCESS != async_job.status()) {
        shutdown();
        return make_unexpected(async_job.status());
    }
    return async_job.release();
}

Expected<AsyncInferJob> ConfiguredInferModelHrpcClient::run_async_impl(const ConfiguredInferModel::Bindings &bindings,
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

    TRY(auto input_buffer_sizes, get_input_buffer_sizes(bindings));
    TRY(auto request, RunAsyncSerializer::serialize_request({m_handle_id, m_infer_model_handle_id,
        m_callbacks_counter, input_buffer_sizes}));
    auto request_ptr = make_shared_nothrow<Buffer>(std::move(request));
    CHECK_NOT_NULL(request_ptr, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto job_ptr, m_callbacks_queue->register_callback(m_callbacks_counter, bindings, callback_wrapper));

    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");

    auto status = client->wait_for_execute_request_ready(MemoryView(*request_ptr), REQUEST_TIMEOUT);
    CHECK_SUCCESS(status);

    auto request_sent_callback = [request_ptr] (hailo_status status) {
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to send request, status = {}", status);
        }
    };
    auto reply_received_callback = [job_ptr] (hailo_status status, Buffer &&reply) {
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed getting reply, status = {}", status);
            return;
        }

        status = RunAsyncSerializer::deserialize_reply(MemoryView(reply));
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to run async, status = {}", status);
            hailo_status job_status = status;
            status = job_ptr->set_status(job_status);
            if (HAILO_SUCCESS != status) {
                LOGGER__CRITICAL("Failed to set job status, status = {}", status);
            }
        }
    };
    auto additional_writes_lambda = [this, &bindings] (RpcConnection connection) -> hailo_status {
        return write_async_inputs(bindings, connection);
    };
    status = client->execute_request_async(HailoRpcActionID::CONFIGURED_INFER_MODEL__RUN_ASYNC, MemoryView(*request_ptr),
        request_sent_callback, reply_received_callback, additional_writes_lambda);
    CHECK_SUCCESS(status);

    {
        std::unique_lock<std::mutex> transfers_lock(m_ongoing_transfers_mutex);
        m_ongoing_transfers++;
    }

    return AsyncInferJobBase::create(job_ptr);
}

Expected<std::vector<uint32_t>> ConfiguredInferModelHrpcClient::get_input_buffer_sizes(const ConfiguredInferModel::Bindings &bindings)
{
    std::vector<uint32_t> buffer_sizes;
    for (const auto &input_vstream : m_input_vstream_infos) {
        TRY(auto input, bindings.input(input_vstream.name));
        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(input);
        switch(buffer_type) {
        case BufferType::VIEW:
        {
            TRY(auto buffer, input.get_buffer());
            buffer_sizes.push_back(static_cast<uint32_t>(buffer.size()));
            break;
        }
        case BufferType::PIX_BUFFER:
        {
            TRY(auto pix_buffer, input.get_pix_buffer());
            CHECK_AS_EXPECTED(HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == pix_buffer.memory_type, HAILO_NOT_SUPPORTED,
                "Currently, only userptr pix buffers are supported in HRPC!"); // TODO: HRT-14391
            for (uint32_t i = 0; i < pix_buffer.number_of_planes; i++) {
                buffer_sizes.push_back(pix_buffer.planes[i].bytes_used);
            }
            break;
        }
        case BufferType::DMA_BUFFER:
            LOGGER__CRITICAL("DMA_BUFFER is not supported in HRPC");
            return make_unexpected(HAILO_NOT_IMPLEMENTED);
        default:
            LOGGER__CRITICAL("Unknown buffer type");
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }
    }
    return buffer_sizes;
}

hailo_status ConfiguredInferModelHrpcClient::write_async_inputs(const ConfiguredInferModel::Bindings &bindings,
    RpcConnection connection)
{
    for (const auto &input_vstream : m_input_vstream_infos) {
        TRY(auto input, bindings.input(input_vstream.name));
        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(input);
        switch(buffer_type) {
        case BufferType::VIEW:
        {
            TRY(auto buffer, input.get_buffer());
            auto status = connection.write_buffer_async(MemoryView(buffer), [] (hailo_status status) {
                if (HAILO_SUCCESS != status) {
                    LOGGER__ERROR("Failed to write buffer, status = {}", status);
                }
            });
            CHECK_SUCCESS(status);
            break;
        }
        case BufferType::PIX_BUFFER:
        {
            TRY(auto pix_buffer, input.get_pix_buffer());
            CHECK(HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == pix_buffer.memory_type, HAILO_NOT_SUPPORTED,
                "Currently, only userptr pix buffers are supported in HRPC!"); // TODO: HRT-14391
            for (uint32_t i = 0; i < pix_buffer.number_of_planes; i++) {
                auto status = connection.write_buffer_async(MemoryView(pix_buffer.planes[i].user_ptr, pix_buffer.planes[i].bytes_used),
                    [] (hailo_status status) {
                        if (HAILO_SUCCESS != status) {
                            LOGGER__ERROR("Failed to write buffer, status = {}", status);
                        }
                    });
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

hailo_status ConfiguredInferModelHrpcClient::validate_bindings(const ConfiguredInferModel::Bindings &bindings)
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

hailo_status ConfiguredInferModelHrpcClient::shutdown_impl()
{
    TRY(auto serialized_request, ShutdownSerializer::serialize_request(m_handle_id));
    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__SHUTDOWN, MemoryView(serialized_request)));

    CHECK_SUCCESS(ShutdownSerializer::deserialize_reply(MemoryView(result)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::shutdown()
{
    auto status = shutdown_impl();

    if (status != HAILO_SUCCESS) {
        CHECK_SUCCESS(m_callbacks_queue->shutdown(status));
    }

    return status;
}

} // namespace hailort
