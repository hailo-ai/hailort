/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

AsyncInferJobHrpcClient::AsyncInferJobHrpcClient(EventPtr event) : m_event(event), m_job_status(HAILO_UNINITIALIZED)
{
}

hailo_status AsyncInferJobHrpcClient::wait(std::chrono::milliseconds timeout)
{
    auto status = m_event->wait(timeout);
    if (HAILO_UNINITIALIZED != m_job_status) {
        return m_job_status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status AsyncInferJobHrpcClient::set_status(hailo_status status)
{
    m_job_status = status;
    return m_event->signal();
}

Expected<std::shared_ptr<ConfiguredInferModelHrpcClient>> ConfiguredInferModelHrpcClient::create(std::shared_ptr<Client> client,
    rpc_object_handle_t handle_id, std::vector<hailo_vstream_info_t> &&input_vstream_infos,
    std::vector<hailo_vstream_info_t> &&output_vstream_infos, uint32_t max_ongoing_transfers, rpc_object_handle_t infer_model_id,
    const std::unordered_map<std::string, size_t> inputs_frame_sizes, const std::unordered_map<std::string, size_t> outputs_frame_sizes)
{
    // TODO: consider create a separate client object here - HRT-13687
    TRY(auto callback_dispatcher, client->callback_dispatcher_manager()->new_dispatcher(RpcCallbackType::RUN_ASYNC, true));
    auto ptr = make_shared_nothrow<ConfiguredInferModelHrpcClient>(client, handle_id, std::move(input_vstream_infos),
        std::move(output_vstream_infos), max_ongoing_transfers, callback_dispatcher, infer_model_id, inputs_frame_sizes,
        outputs_frame_sizes);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

ConfiguredInferModelHrpcClient::~ConfiguredInferModelHrpcClient()
{
    if (INVALID_HANDLE_ID == m_handle_id) {
        return;
    }

    auto client = m_client.lock();
    if (client) {
        auto request_buffer = client->allocate_request_buffer();
        if (!request_buffer) {
            LOGGER__CRITICAL("Failed to create buffer for ConfiguredInferModel_release request");
            return;
        }

        auto request_size = DestroyConfiguredInferModelSerializer::serialize_request(m_handle_id, MemoryView(**request_buffer));
        if (!request_size) {
            LOGGER__CRITICAL("Failed to serialize ConfiguredInferModel_release request");
            return;
        }

        auto expected_result = client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__DESTROY, MemoryView(request_buffer.value()->data(), *request_size));
        if (!expected_result) {
            LOGGER__CRITICAL("Failed to destroy configured infer model! status = {}", expected_result.status());
            return;
        }
        auto result = expected_result.release();

        auto status = DestroyConfiguredInferModelSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size));
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Failed to destroy configured infer model! status = {}", status);
        }

        status = client->callback_dispatcher_manager()->remove_dispatcher(m_callback_dispatcher->id());
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Failed to remove callback dispatcher! status = {}", status);
        }
    }
}

Expected<ConfiguredInferModel::Bindings> ConfiguredInferModelHrpcClient::create_bindings(const std::map<std::string, MemoryView> &buffers)
{
    std::unordered_map<std::string, ConfiguredInferModel::Bindings::InferStream> inputs;
    std::unordered_map<std::string, ConfiguredInferModel::Bindings::InferStream> outputs;

    uint32_t used_buffers = 0;

    for (const auto &vstream_info : m_input_vstream_infos) {
        TRY(auto stream, ConfiguredInferModelBase::create_infer_stream(vstream_info));
        auto name = std::string(vstream_info.name);
        inputs.emplace(name, std::move(stream));
        if (contains(buffers, name)) {
            inputs.at(name).set_buffer(buffers.at(name));
            used_buffers++;
        }
    }

    for (const auto &vstream_info : m_output_vstream_infos) {
        TRY(auto stream, ConfiguredInferModelBase::create_infer_stream(vstream_info));
        auto name = std::string(vstream_info.name);
        outputs.emplace(name, std::move(stream));
        if (contains(buffers, name)) {
            outputs.at(name).set_buffer(buffers.at(name));
            used_buffers++;
        }
    }

    TRY(auto bindings, ConfiguredInferModelBase::create_bindings(std::move(inputs), std::move(outputs)));
    CHECK_AS_EXPECTED(used_buffers == buffers.size(), HAILO_INVALID_ARGUMENT, "Given 'buffers' contains names which arent model edges.");
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

    auto callback_id = m_callbacks_counter++;
    TRY(auto input_buffer_sizes, get_input_buffer_sizes(bindings));

    TRY(auto event_ptr, Event::create_shared(Event::State::not_signalled));
    auto job_ptr = make_shared_nothrow<AsyncInferJobHrpcClient>(event_ptr);
    CHECK_NOT_NULL(job_ptr, HAILO_OUT_OF_HOST_MEMORY);

    m_callback_dispatcher->add_additional_reads(callback_id,
        [this, bindings] (const RpcCallback &rpc_callback) -> Expected<std::vector<TransferBuffer>> {
            std::vector<TransferBuffer> buffers;
            if (HAILO_SUCCESS == rpc_callback.data.run_async.status) {
                buffers.reserve(m_output_vstream_infos.size());
                for (const auto &vstream_info : m_output_vstream_infos) {
                    TRY(auto buffer, bindings.output(vstream_info.name)->get_buffer());
                    buffers.emplace_back(MemoryView(buffer));
                }
            }
            return buffers;
        });
    m_callback_dispatcher->register_callback(callback_id,
        [this, callback, event_ptr]
        (const RpcCallback &rpc_callback, hailo_status shutdown_status) {
            {
                std::unique_lock<std::mutex> transfers_lock(m_ongoing_transfers_mutex);
                m_ongoing_transfers--;
            }
            m_cv.notify_one();
            if (!callback) {
                return;
            }

            hailo_status status = (shutdown_status != HAILO_UNINITIALIZED) ? shutdown_status : rpc_callback.data.run_async.status;
            AsyncInferCompletionInfo info(status);
            callback(info);
            status = event_ptr->signal();
            if (HAILO_SUCCESS != status) {
                LOGGER__CRITICAL("Failed to signal event, status = {}", status);
            }
        });

    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, RunAsyncSerializer::serialize_request({m_handle_id, m_infer_model_handle_id,
        callback_id, m_callback_dispatcher->id(), input_buffer_sizes}, MemoryView(*serialized_request)));

    auto status = client->wait_for_execute_request_ready(MemoryView(serialized_request->data(), request_size), REQUEST_TIMEOUT);
    CHECK_SUCCESS(status);

    auto request_sent_callback = [serialized_request] (hailo_status status) {
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to send request, status = {}", status);
        }
    };
    auto reply_received_callback = [job_ptr] (hailo_status status, rpc_message_t reply) {
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed getting reply, status = {}", status);
            return;
        }

        status = RunAsyncSerializer::deserialize_reply(MemoryView(reply.buffer->data(), reply.header.size));
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to run async, status = {}", status);
            hailo_status job_status = status;
            status = job_ptr->set_status(job_status);
            if (HAILO_SUCCESS != status) {
                LOGGER__CRITICAL("Failed to set job status, status = {}", status);
            }
        }
    };

    TRY(auto additional_buffers, get_async_inputs(bindings));
    status = client->execute_request_async(HailoRpcActionID::CONFIGURED_INFER_MODEL__RUN_ASYNC,
        MemoryView(serialized_request->data(), request_size),
        request_sent_callback, reply_received_callback, std::move(additional_buffers));
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

Expected<std::vector<TransferBuffer>> ConfiguredInferModelHrpcClient::get_async_inputs(
    const ConfiguredInferModel::Bindings &bindings)
{
    std::vector<TransferBuffer> inputs;
    for (const auto &input_vstream : m_input_vstream_infos) {
        TRY(auto input, bindings.input(input_vstream.name));
        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(input);
        switch(buffer_type) {
        case BufferType::VIEW:
        {
            TRY(auto buffer, input.get_buffer());
            inputs.push_back(MemoryView(buffer));
            break;
        }
        case BufferType::PIX_BUFFER:
        {
            TRY(auto pix_buffer, input.get_pix_buffer());
            CHECK(HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == pix_buffer.memory_type, HAILO_NOT_SUPPORTED,
                "Currently, only userptr pix buffers are supported in HRPC!"); // TODO: HRT-14391
            for (uint32_t i = 0; i < pix_buffer.number_of_planes; i++) {
                inputs.push_back(MemoryView(pix_buffer.planes[i].user_ptr, pix_buffer.planes[i].bytes_used));
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
    return inputs;
}

hailo_status ConfiguredInferModelHrpcClient::set_scheduler_timeout(const std::chrono::milliseconds &timeout)
{
    auto client = m_client.lock();

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, SetSchedulerTimeoutSerializer::serialize_request(m_handle_id, timeout, MemoryView(*serialized_request)));
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_TIMEOUT, MemoryView(serialized_request->data(), request_size)));
    CHECK_SUCCESS(SetSchedulerTimeoutSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::set_scheduler_threshold(uint32_t threshold)
{
    auto client = m_client.lock();

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, SetSchedulerThresholdSerializer::serialize_request(m_handle_id, threshold, MemoryView(*serialized_request)));
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_THRESHOLD, MemoryView(serialized_request->data(), request_size)));
    CHECK_SUCCESS(SetSchedulerThresholdSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::set_scheduler_priority(uint8_t priority)
{
    auto client = m_client.lock();

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, SetSchedulerPrioritySerializer::serialize_request(m_handle_id, priority, MemoryView(*serialized_request)));
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_PRIORITY, MemoryView(serialized_request->data(), request_size)));
    CHECK_SUCCESS(SetSchedulerPrioritySerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    return HAILO_SUCCESS;
}

Expected<LatencyMeasurementResult> ConfiguredInferModelHrpcClient::get_hw_latency_measurement()
{
    auto client = m_client.lock();

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, GetHwLatencyMeasurementSerializer::serialize_request(m_handle_id, MemoryView(*serialized_request)));
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__GET_HW_LATENCY_MEASUREMENT, MemoryView(serialized_request->data(), request_size)));

    TRY(auto tuple, GetHwLatencyMeasurementSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

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
    auto client = m_client.lock();

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, ActivateSerializer::serialize_request(m_handle_id, MemoryView(*serialized_request)));
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__ACTIVATE, MemoryView(serialized_request->data(), request_size)));

    CHECK_SUCCESS(ActivateSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    return HAILO_SUCCESS;
};

hailo_status ConfiguredInferModelHrpcClient::deactivate()
{
    auto client = m_client.lock();

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, DeactivateSerializer::serialize_request(m_handle_id, MemoryView(*serialized_request)));
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__DEACTIVATE, MemoryView(serialized_request->data(), request_size)));

    CHECK_SUCCESS(DeactivateSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    return HAILO_SUCCESS;
};

Expected<size_t> ConfiguredInferModelHrpcClient::get_async_queue_size() const
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
    auto client = m_client.lock();

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, ShutdownSerializer::serialize_request(m_handle_id, MemoryView(*serialized_request)));
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the ConfiguredInferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::CONFIGURED_INFER_MODEL__SHUTDOWN, MemoryView(serialized_request->data(), request_size)));

    CHECK_SUCCESS(ShutdownSerializer::deserialize_reply(MemoryView(result.buffer->data(), result.header.size)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::shutdown()
{
    auto status = shutdown_impl();

    if (status != HAILO_SUCCESS) {
        CHECK_SUCCESS(m_callback_dispatcher->shutdown(status));
    }

    return status;
}

hailo_status ConfiguredInferModelHrpcClient::update_cache_offset(int32_t /*offset_delta_entries*/)
{
    LOGGER__ERROR("update_cache_offset is not supported for HrpcClient");
    return HAILO_NOT_IMPLEMENTED;
}

} // namespace hailort
