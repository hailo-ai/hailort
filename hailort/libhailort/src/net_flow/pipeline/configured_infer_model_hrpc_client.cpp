/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file configured_infer_model_hrpc_client.cpp
 * @brief ConfiguredInferModel HRPC client implementation
 **/

#include "configured_infer_model_hrpc_client.hpp"
#include "common/logger_macros.hpp"
#include "hrpc/connection_context.hpp"
#include "hrpc_protocol/serializer.hpp"

namespace hailort
{

static constexpr std::chrono::milliseconds MESSAGE_LOOP_FINISHED_POLL_INTERVAL(100);

Expected<std::shared_ptr<AsyncInferJobHrpcClient>> AsyncInferJobHrpcClient::create_shared()
{
    TRY(auto event_ptr, Event::create_shared(Event::State::not_signalled));
    auto job_ptr = make_shared_nothrow<AsyncInferJobHrpcClient>(event_ptr);
    CHECK_NOT_NULL(job_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return job_ptr;
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
    auto jobs_state = make_shared_nothrow<AsyncJobsState>();
    CHECK_NOT_NULL(jobs_state, HAILO_OUT_OF_HOST_MEMORY);

    auto ptr = make_shared_nothrow<ConfiguredInferModelHrpcClient>(client, handle_id, std::move(input_vstream_infos),
        std::move(output_vstream_infos), max_ongoing_transfers, infer_model_id, inputs_frame_sizes,
        outputs_frame_sizes, std::move(jobs_state));
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

ConfiguredInferModelHrpcClient::ConfiguredInferModelHrpcClient(std::shared_ptr<Client> client, rpc_object_handle_t handle_id,
    std::vector<hailo_vstream_info_t> &&input_vstream_infos, std::vector<hailo_vstream_info_t> &&output_vstream_infos,
    uint32_t max_ongoing_transfers, rpc_object_handle_t infer_model_handle_id,
    const std::unordered_map<std::string, size_t> inputs_frame_sizes,
    const std::unordered_map<std::string, size_t> outputs_frame_sizes, std::shared_ptr<AsyncJobsState> jobs_state) :
        ConfiguredInferModelBase(inputs_frame_sizes, outputs_frame_sizes),
        m_client(client), m_client_message_loop_thread_id(client->message_loop_thread_id()),
        m_is_client_message_loop_finished(client->message_loop_finished_flag()),
        m_handle_id(handle_id), m_input_vstream_infos(std::move(input_vstream_infos)),
        m_output_vstream_infos(std::move(output_vstream_infos)), m_max_ongoing_transfers(max_ongoing_transfers),
        m_jobs_state(std::move(jobs_state)), m_infer_model_handle_id(infer_model_handle_id)
{
    m_input_names.reserve(m_input_vstream_infos.size());
    for (const auto &vstream_info : m_input_vstream_infos) {
        m_input_names.emplace_back(vstream_info.name);
    }
    m_output_names.reserve(m_output_vstream_infos.size());
    for (const auto &vstream_info : m_output_vstream_infos) {
        m_output_names.emplace_back(vstream_info.name);
    }
}

ConfiguredInferModelHrpcClient::~ConfiguredInferModelHrpcClient()
{
    DEFER((void)wait_for_pending_callbacks());
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
        auto request_size = DestroyConfiguredInferModelSerializer::serialize_request(m_handle_id,
            MemoryView(**request_buffer));
        if (!request_size) {
            LOGGER__CRITICAL("Failed to serialize ConfiguredInferModel_release request");
            return;
        }
        auto expected_result = client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__DESTROY),
            MemoryView(request_buffer.value()->data(), *request_size));
        DTOR_LOG_ON_FAILURE(expected_result, "Failed to destroy configured infer model! status = {}");
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
    CHECK_AS_EXPECTED(used_buffers == buffers.size(), HAILO_INVALID_ARGUMENT,
        "Given buffers contain names which aren't model edges.");

    return bindings;
}

hailo_status ConfiguredInferModelHrpcClient::wait_for_async_ready(std::chrono::milliseconds timeout, uint32_t frames_count)
{
    std::unique_lock<std::mutex> lock(m_jobs_state->mutex);
    bool done = m_jobs_state->cv.wait_for(lock, timeout, [this, frames_count] () {
        return (m_max_ongoing_transfers - m_jobs_state->ongoing_transfers) >= frames_count;
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
    CHECK_SUCCESS(validate_bindings(bindings));

    std::unique_lock<std::mutex> lock(m_infer_mutex);

    TRY(const auto buffer_infos, get_buffer_infos(bindings));
    const RunAsyncSerializer::Request infer_request {m_handle_id, m_infer_model_handle_id, buffer_infos};

    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(const auto request_buffer, client->allocate_request_buffer());
    TRY(const auto request_size, RunAsyncSerializer::serialize_request(infer_request, request_buffer->as_view()));
    const auto request_view = MemoryView(request_buffer->data(), request_size);

    auto status = client->wait_for_execute_request_ready(request_view, HRPC_REQUEST_TIMEOUT);
    CHECK_SUCCESS(status);

    TRY(auto job, AsyncInferJobHrpcClient::create_shared());
    auto wrapped_callback = [jobs_state = m_jobs_state, request_buffer, callback, job] (rpc_message_t reply) {
        jobs_state->release_transfer_slot();

        auto status = static_cast<hailo_status>(reply.header.status);
        if (callback) {
            callback(AsyncInferCompletionInfo(status));
        }

        status = job->set_status(status);
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Failed to signal event with status: {}", status);
        }

        // Released after the user callback, so that waiting for pending callbacks also waits for it.
        jobs_state->release_pending_callback();
    };
    TRY(auto write_buffers, get_write_buffers(bindings));
    TRY(auto read_buffers, get_read_buffers(bindings));
    m_jobs_state->add_job();
    status = client->execute_request_async(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__RUN_ASYNC),
        request_view, wrapped_callback, std::move(write_buffers), std::move(read_buffers));
    if (HAILO_SUCCESS != status) {
        m_jobs_state->cancel_job_without_callback();
    }
    CHECK_SUCCESS(status);

    return AsyncInferJobBase::create(job);
}

void AsyncJobsState::add_job()
{
    std::unique_lock<std::mutex> lock(mutex);
    ongoing_transfers++;
    pending_callbacks++;
}

void AsyncJobsState::cancel_job_without_callback()
{
    std::unique_lock<std::mutex> lock(mutex);
    ongoing_transfers--;
    pending_callbacks--;
    cv.notify_all();
}

void AsyncJobsState::release_transfer_slot()
{
    std::unique_lock<std::mutex> lock(mutex);
    ongoing_transfers--;
    cv.notify_all();
}

void AsyncJobsState::release_pending_callback()
{
    std::unique_lock<std::mutex> lock(mutex);
    pending_callbacks--;
    cv.notify_all();
}

hailo_status ConfiguredInferModelHrpcClient::wait_for_pending_callbacks()
{
    // All callbacks run on the client's message loop, so waiting there would only time out.
    if (std::this_thread::get_id() == m_client_message_loop_thread_id) {
        return HAILO_SUCCESS;
    }

    std::unique_lock<std::mutex> lock(m_jobs_state->mutex);
    const auto is_wait_done = [this] () {
        return (0 == m_jobs_state->pending_callbacks) || *m_is_client_message_loop_finished;
    };
    // After a timed-out wait, a full wait would only add another timeout to the teardown.
    const auto timeout = m_has_pending_callbacks_wait_timed_out ? std::chrono::milliseconds(0)
        : WAIT_FOR_ASYNC_IN_DTOR_TIMEOUT;
    // The end of the message loop is not signaled, so it is polled.
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto is_done = is_wait_done();
    while (!is_done && (std::chrono::steady_clock::now() < deadline)) {
        is_done = m_jobs_state->cv.wait_for(lock, MESSAGE_LOOP_FINISHED_POLL_INTERVAL, is_wait_done);
    }
    if (is_done) {
        return HAILO_SUCCESS;
    }
    CHECK(!m_has_pending_callbacks_wait_timed_out, HAILO_TIMEOUT, "{} pending callbacks did not return",
        m_jobs_state->pending_callbacks);

    m_has_pending_callbacks_wait_timed_out = true;
    LOGGER__ERROR("Timed out waiting for {} pending callbacks to return", m_jobs_state->pending_callbacks);
    return HAILO_TIMEOUT;
}

Expected<std::vector<RunAsyncSerializer::BufferInfo>> ConfiguredInferModelHrpcClient::get_buffer_infos(const ConfiguredInferModel::Bindings &bindings)
{
    std::vector<RunAsyncSerializer::BufferInfo> buffer_infos;
    buffer_infos.reserve(m_input_names.size() + m_output_names.size());
    auto status = push_input_buffer_infos(bindings, buffer_infos);
    CHECK_SUCCESS(status);

    status = push_output_buffer_infos(bindings, buffer_infos);
    CHECK_SUCCESS(status);

    return buffer_infos;
}

hailo_status ConfiguredInferModelHrpcClient::push_input_buffer_infos(
    const ConfiguredInferModel::Bindings &bindings, std::vector<RunAsyncSerializer::BufferInfo> &buffer_infos)
{
    for (const auto &input_vstream_name : m_input_names) {
        TRY(auto input, bindings.input(input_vstream_name));
        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(input);
        switch(buffer_type) {
        case BufferType::VIEW:
        {
            TRY(auto buffer, input.get_buffer());
            buffer_infos.push_back({static_cast<uint32_t>(buffer.size()), static_cast<uint32_t>(BufferType::VIEW)});
            break;
        }
        case BufferType::PIX_BUFFER:
        {
            TRY(auto pix_buffer, input.get_pix_buffer());
            auto pix_buffer_type = HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == pix_buffer.memory_type ?
                BufferType::VIEW : BufferType::DMA_BUFFER;
            for (uint32_t i = 0; i < pix_buffer.number_of_planes; i++) {
                buffer_infos.push_back({static_cast<uint32_t>(pix_buffer.planes[i].bytes_used),
                    static_cast<uint32_t>(pix_buffer_type)});
            }
            break;
        }
        case BufferType::DMA_BUFFER:
        {
            TRY(const auto &dma_buffer, input.get_dma_buffer());
            buffer_infos.push_back({static_cast<uint32_t>(dma_buffer.size), static_cast<uint32_t>(BufferType::DMA_BUFFER)});
            break;
        }
        default:
            LOGGER__CRITICAL("Unknown buffer type");
            return HAILO_INTERNAL_FAILURE;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::push_output_buffer_infos(
    const ConfiguredInferModel::Bindings &bindings, std::vector<RunAsyncSerializer::BufferInfo> &buffer_infos)
{
    for (const auto &output_vstream_name : m_output_names) {
        TRY(auto output, bindings.output(output_vstream_name));
        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(output);
        if (BufferType::DMA_BUFFER == buffer_type) {
            TRY(const auto &dma_buffer, output.get_dma_buffer());
            buffer_infos.push_back({static_cast<uint32_t>(dma_buffer.size), static_cast<uint32_t>(BufferType::DMA_BUFFER)});
        } else {
            TRY(auto buffer, output.get_buffer());
            buffer_infos.push_back({static_cast<uint32_t>(buffer.size()), static_cast<uint32_t>(BufferType::VIEW)});
        }
    }

    return HAILO_SUCCESS;
}

Expected<std::vector<TransferBuffer>> ConfiguredInferModelHrpcClient::get_write_buffers(const ConfiguredInferModel::Bindings &bindings)
{
    std::vector<TransferBuffer> buffers;
    buffers.reserve(m_input_names.size());

    for (const auto &input_vstream_name : m_input_names) {
        TRY(auto input, bindings.input(input_vstream_name));
        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(input);
        switch(buffer_type) {
        case BufferType::VIEW:
        {
            TRY(auto buffer, input.get_buffer());
            buffers.push_back(MemoryView(buffer));
            break;
        }
        case BufferType::PIX_BUFFER:
        {
            TRY(auto pix_buffer, input.get_pix_buffer());
            for (uint32_t i = 0; i < pix_buffer.number_of_planes; i++) {
                if (HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == pix_buffer.memory_type) {
                    buffers.push_back(MemoryView(pix_buffer.planes[i].user_ptr, pix_buffer.planes[i].bytes_used));
                } else {
                    buffers.push_back(hailo_dma_buffer_t {pix_buffer.planes[i].fd, pix_buffer.planes[i].bytes_used});
                }
            }
            break;
        }
        case BufferType::DMA_BUFFER:
        {
            TRY(const auto &dma_buffer, input.get_dma_buffer());
            buffers.push_back(dma_buffer);
            break;
        }
        default:
            LOGGER__CRITICAL("Unknown buffer type");
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }
    }
    if (m_client.lock()->device_id() == SERVER_ADDR_USE_UNIX_SOCKET) {
        for (const auto &output_vstream_name : m_output_names) {
            TRY(auto output, bindings.output(output_vstream_name));
            auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(output);
            if (BufferType::DMA_BUFFER == buffer_type) {
                TRY(const auto &dma_buffer, output.get_dma_buffer());
                buffers.push_back(dma_buffer);
            }
        }
    }

    return buffers;
}

Expected<std::vector<TransferBuffer>> ConfiguredInferModelHrpcClient::get_read_buffers(const ConfiguredInferModel::Bindings &bindings)
{
    std::vector<TransferBuffer> buffers;
    buffers.reserve(m_output_names.size());

    for (const auto &output_vstream_name : m_output_names) {
        TRY(auto output, bindings.output(output_vstream_name));
        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(output);

        if (BufferType::DMA_BUFFER != buffer_type) {
            TRY(auto buffer, output.get_buffer());
            buffers.emplace_back(MemoryView(buffer));
        } else if (m_client.lock()->device_id() != SERVER_ADDR_USE_UNIX_SOCKET) {
            TRY(auto buffer, output.get_dma_buffer());
            buffers.emplace_back(buffer);
        }
    }

    return buffers;
}

hailo_status ConfiguredInferModelHrpcClient::set_scheduler_timeout(const std::chrono::milliseconds &timeout)
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, SetSchedulerTimeoutSerializer::serialize_request(m_handle_id, timeout,
        MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_TIMEOUT),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::set_scheduler_threshold(uint32_t threshold)
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, SetSchedulerThresholdSerializer::serialize_request(m_handle_id, threshold, MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_THRESHOLD),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::set_scheduler_priority(uint8_t priority)
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, SetSchedulerPrioritySerializer::serialize_request(m_handle_id, priority,
        MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__SET_SCHEDULER_PRIORITY),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
}

Expected<LatencyMeasurementResult> ConfiguredInferModelHrpcClient::get_hw_latency_measurement()
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, GetHwLatencyMeasurementSerializer::serialize_request(m_handle_id,
        MemoryView(*serialized_request)));
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_NOT_AVAILABLE,
        auto result, client->execute_request(
            static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__GET_HW_LATENCY_MEASUREMENT),
            MemoryView(serialized_request->data(), request_size)));

    TRY(auto avg_hw_latency,
        GetHwLatencyMeasurementSerializer::deserialize_reply(MemoryView(result.body.data(), result.header.size)));

    return LatencyMeasurementResult{avg_hw_latency};
};

hailo_status ConfiguredInferModelHrpcClient::activate()
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, ActivateSerializer::serialize_request(m_handle_id, MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__ACTIVATE),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
};

hailo_status ConfiguredInferModelHrpcClient::deactivate()
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, DeactivateSerializer::serialize_request(m_handle_id, MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__DEACTIVATE),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
};

Expected<size_t> ConfiguredInferModelHrpcClient::get_async_queue_size() const
{
    return m_max_ongoing_transfers;
}

hailo_status ConfiguredInferModelHrpcClient::validate_bindings(const ConfiguredInferModel::Bindings &bindings)
{
    auto status = validate_input_bindings(bindings);
    CHECK_SUCCESS(status);

    status = validate_output_bindings(bindings);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::validate_input_bindings(const ConfiguredInferModel::Bindings &bindings)
{
    for (const auto &input_vstream_name : m_input_names) {
        TRY(auto input, bindings.input(input_vstream_name));

        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(input);
        switch (buffer_type) {
            case BufferType::VIEW:
            {
                auto buffer = input.get_buffer();
                CHECK_EXPECTED_AS_STATUS(buffer);
                CHECK(buffer->size() == m_inputs_frame_sizes.at(input_vstream_name), HAILO_INVALID_OPERATION,
                    "Input buffer size {} is different than expected {} for input '{}'", buffer->size(), m_inputs_frame_sizes.at(input_vstream_name), input_vstream_name);
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

                CHECK(buffer_size == m_inputs_frame_sizes.at(input_vstream_name), HAILO_INVALID_OPERATION,
                    "Input buffer size {} is different than expected {} for input '{}'", buffer_size, m_inputs_frame_sizes.at(input_vstream_name), input_vstream_name);
                break;
            }
            case BufferType::DMA_BUFFER:
            {
                auto buffer = input.get_dma_buffer();
                CHECK_EXPECTED_AS_STATUS(buffer);
                CHECK(buffer->size == m_inputs_frame_sizes.at(input_vstream_name), HAILO_INVALID_OPERATION,
                    "Input buffer size {} is different than expected {} for input '{}'", buffer->size, m_inputs_frame_sizes.at(input_vstream_name), input_vstream_name);
                break;
            }
            default:
                CHECK(false, HAILO_NOT_FOUND, "Couldnt find input buffer for '{}'", input_vstream_name);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::validate_output_bindings(const ConfiguredInferModel::Bindings &bindings)
{
    for (const auto &output_vstream_name : m_output_names) {
        TRY(auto output, bindings.output(output_vstream_name));
        auto buffer_type = ConfiguredInferModelBase::get_infer_stream_buffer_type(output);
        switch (buffer_type) {
            case BufferType::VIEW:
            {
                auto buffer = output.get_buffer();
                CHECK_EXPECTED_AS_STATUS(buffer);
                CHECK(buffer->size() == m_outputs_frame_sizes.at(output_vstream_name), HAILO_INVALID_OPERATION,
                    "Output buffer size {} is different than expected {} for output '{}'", buffer->size(), m_outputs_frame_sizes.at(output_vstream_name), output_vstream_name);
                break;
            }
            case BufferType::PIX_BUFFER:
            {
                CHECK(false, HAILO_NOT_SUPPORTED, "pix_buffer isn't supported for outputs in '{}'", output_vstream_name);
                break;
            }
            case BufferType::DMA_BUFFER:
            {
                auto buffer = output.get_dma_buffer();
                CHECK_EXPECTED_AS_STATUS(buffer);
                CHECK(buffer->size == m_outputs_frame_sizes.at(output_vstream_name), HAILO_INVALID_OPERATION,
                    "Output buffer size {} is different than expected {} for output '{}'", buffer->size, m_outputs_frame_sizes.at(output_vstream_name), output_vstream_name);
                break;
            }
            default:
                CHECK(false, HAILO_NOT_FOUND, "Couldnt find output buffer for '{}'", output_vstream_name);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::shutdown()
{
    const auto request_status = request_shutdown();
    const auto wait_status = wait_for_pending_callbacks();
    if (HAILO_SUCCESS != request_status) {
        return request_status;
    }

    return wait_status;
}

hailo_status ConfiguredInferModelHrpcClient::request_shutdown()
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, ShutdownSerializer::serialize_request(m_handle_id, MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__SHUTDOWN),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::update_cache_offset(int32_t offset_delta_entries)
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, UpdateCacheOffsetSerializer::serialize_request(m_handle_id, offset_delta_entries,
        MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__UPDATE_CACHE_OFFSET),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelHrpcClient::init_cache(uint32_t read_offset)
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, InitCacheSerializer::serialize_request(m_handle_id, read_offset,
        MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__INIT_CACHE),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
}

Expected<std::unordered_map<uint32_t, BufferPtr>> ConfiguredInferModelHrpcClient::get_cache_buffers()
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, GetCacheBuffersSerializer::serialize_request(m_handle_id,
        MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__GET_CACHE_BUFFERS),
        MemoryView(serialized_request->data(), request_size)));

    return GetCacheBuffersSerializer::deserialize_reply(MemoryView(serialized_request->data(), request_size));
}

hailo_status ConfiguredInferModelHrpcClient::update_cache_buffer(uint32_t cache_id, MemoryView buffer)
{
    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(auto serialized_request, client->allocate_request_buffer());
    TRY(auto request_size, UpdateCacheBufferSerializer::serialize_request(m_handle_id, cache_id, buffer,
        MemoryView(*serialized_request)));
    CHECK_SUCCESS(client->execute_request(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__UPDATE_CACHE_BUFFER),
        MemoryView(serialized_request->data(), request_size)));

    return HAILO_SUCCESS;
}

// This function runs an infer for a certain duration on the server, by looping on a single frame
// Meaning no communication between client and server during infer, only at the start and the end
Expected<AsyncInferJob> ConfiguredInferModelHrpcClient::run_async_for_duration(const ConfiguredInferModel::Bindings &bindings,
    uint32_t duration_ms, uint32_t sleep_between_frames_ms, std::function<void(const AsyncInferCompletionInfo &, uint32_t)> callback)
{
    CHECK_SUCCESS(validate_input_bindings(bindings));
    std::unique_lock<std::mutex> lock(m_infer_mutex);

    std::vector<RunAsyncSerializer::BufferInfo> buffer_infos;
    buffer_infos.reserve(m_input_names.size());
    auto status = push_input_buffer_infos(bindings, buffer_infos);
    CHECK_SUCCESS(status);

    status = push_output_buffer_infos(bindings, buffer_infos);
    CHECK_SUCCESS(status);

    const RunAsyncForDurationSerializer::Request infer_request {m_handle_id, m_infer_model_handle_id, duration_ms, sleep_between_frames_ms, buffer_infos};

    auto client = m_client.lock();
    CHECK(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost communication with the server. This may happen if VDevice is released while the CIM is in use.");

    TRY(const auto request_buffer, client->allocate_request_buffer());
    TRY(const auto request_size, RunAsyncForDurationSerializer::serialize_request(infer_request, request_buffer->as_view()));
    const auto request_view = MemoryView(request_buffer->data(), request_size);

    status = client->wait_for_execute_request_ready(request_view, HRPC_REQUEST_TIMEOUT);
    CHECK_SUCCESS(status);

    TRY(auto job, AsyncInferJobHrpcClient::create_shared());
    auto wrapped_callback = [request_buffer, job, callback] (rpc_message_t reply) {
        auto status = static_cast<hailo_status>(reply.header.status);

        if (callback) {
            uint32_t fps = 0;
            if (HAILO_SUCCESS == status) {
                auto expected_fps = RunAsyncForDurationSerializer::deserialize_reply(MemoryView(reply.buffer->data(), reply.header.size));
                if (!expected_fps) {
                    status = expected_fps.status();
                } else {
                    fps = expected_fps.value();
                }
            } else {
                LOGGER__ERROR("Calling run async for duration has failed, status = {}", status);
            }
            callback(AsyncInferCompletionInfo(status), fps);
        }

        status = job->set_status(status);
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Failed to signal event with status: {}", status);
        }
    };
    TRY(auto write_buffers, get_write_buffers(bindings));
    TRY(auto read_buffers, get_read_buffers(bindings));
    auto expected = client->execute_request_async(static_cast<uint32_t>(HailoRpcActionID::CONFIGURED_INFER_MODEL__RUN_ASYNC_FOR_DURATION),
        request_view, wrapped_callback, std::move(write_buffers), std::move(read_buffers));
    CHECK_SUCCESS(expected);

    return AsyncInferJobBase::create(job);
}

} // namespace hailort
