/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_infer_runner.cpp
 * @brief Implemention of the async HL infer
 **/

#include <iostream>

#include "common/utils.hpp"
#include "common/os_utils.hpp"
#include "hailo/event.hpp"
#include "utils/dma_buffer_utils.hpp"
#include "hailo/hailort_defaults.hpp"
#include "hailo/hailort_common.hpp"
#include "net_flow/pipeline/async_infer_runner.hpp"
#include "net_flow/pipeline/infer_model_internal.hpp"
#include "net_flow/pipeline/pipeline_internal.hpp"
#include "net_flow/ops_metadata/op_metadata.hpp"

namespace hailort
{

Expected<std::shared_ptr<AsyncPipeline>> AsyncPipeline::create_shared(const std::string &network_name)
{
    auto async_pipeline_ptr = make_shared_nothrow<AsyncPipeline>(network_name);
    CHECK_NOT_NULL_AS_EXPECTED(async_pipeline_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return async_pipeline_ptr;
}

AsyncPipeline::AsyncPipeline(const std::string &network_name) : m_is_multi_planar(false), m_network_name(network_name) {
    m_pipeline_unique_id = generate_unique_id();
}

void AsyncPipeline::add_element_to_pipeline(std::shared_ptr<PipelineElement> pipeline_element)
{
    m_pipeline_elements.push_back(pipeline_element);
}

void AsyncPipeline::set_async_hw_element(std::shared_ptr<AsyncHwElement> async_hw_element)
{
    m_async_hw_element = async_hw_element;
}

void AsyncPipeline::add_entry_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &input_name)
{
    assert(!contains(m_entry_elements, input_name));
    m_entry_elements[input_name] = pipeline_element;
}

void AsyncPipeline::add_last_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &output_name)
{
    assert(!contains(m_last_elements, output_name));
    m_last_elements[output_name] = pipeline_element;
}

void AsyncPipeline::set_build_params(ElementBuildParams &build_params)
{
    m_build_params = build_params;
}

void AsyncPipeline::shutdown(hailo_status error_status)
/*
Async pipeline shutdown handling:
Shutdown the pipeline is considered unrecoverable, so the entire pipeline and core op (or net_group) are assumed to be unusable*.
Shutdown can originate from internal sources:
- Errors from the core.
- Errors from getting buffers from one of the buffer pools
- Error from enqueue/dequeue buffers in a AsyncPushQueue element

or external sources:
- User request to abort.

The flow in case of shutdown is:
1. Set the pipeline_status to error_status - will block new infer requests from coming
2. Shutdown threads in all elements
3. Dequeue all user buffers with the error_status**

*  - TODO: add support for resume flow
** - if there are user buffers in an AsyncPushQueueElement (inside the queue itself) - the element will clear them after stop_thread() is complete


*/
{
    if (HAILO_STREAM_ABORT == error_status) {
        LOGGER__INFO("Pipeline was aborted. Shutting it down");
    } else {
        LOGGER__ERROR("Shutting down the pipeline with status {}", error_status);
    }
    m_build_params.pipeline_status->store(error_status);
    auto status = m_build_params.shutdown_event->signal();
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Executing pipeline shutdown failed with status {}", status);
    }

    for (auto &element : get_entry_elements()) {
        status = element.second->terminate(error_status);
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Executing pipeline terminate failed with status {}", status);
        }
    }

    for (auto &element : get_entry_elements()) {
        status = element.second->dequeue_user_buffers(error_status);
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Dequeueing external buffers failed with status {}", status);
        }
    }
}

const std::vector<std::shared_ptr<PipelineElement>>& AsyncPipeline::get_pipeline() const
{
    return m_pipeline_elements;
}

const std::unordered_map<std::string, std::shared_ptr<PipelineElement>>& AsyncPipeline::get_entry_elements() const
{
    return m_entry_elements;
}

const std::unordered_map<std::string, std::shared_ptr<PipelineElement>>& AsyncPipeline::get_last_elements() const
{
    return m_last_elements;
}

const std::shared_ptr<AsyncHwElement> AsyncPipeline::get_async_hw_element()
{
    return m_async_hw_element;
}

const ElementBuildParams AsyncPipeline::get_build_params()
{
    return m_build_params;
}

std::shared_ptr<std::atomic<hailo_status>> AsyncPipeline::get_pipeline_status()
{
    return m_build_params.pipeline_status;
}

void AsyncPipeline::set_as_multi_planar()
{
    m_is_multi_planar = true;
}

bool AsyncPipeline::is_multi_planar()
{
    return m_is_multi_planar;
}

std::string AsyncPipeline::get_network_name() const
{
    return m_network_name;
}

uint64_t AsyncPipeline::generate_unique_id()
{
    // ID layout: [ 32 bits timestamp | 16 bits PID | 8 bits counter | 8 bits job_id ]
    // job_id is used to identify a job_id in the pipeline (initialized to 0)
    static std::atomic<uint8_t> counter{0};
    uint8_t job_id = 0;

    auto now = std::chrono::high_resolution_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                             now.time_since_epoch()
                         ).count();

    uint64_t c   = counter.fetch_add(1) & 0xFF;
    uint64_t pid = static_cast<uint64_t>(OsUtils::get_curr_pid()) & 0xFFFF;

    return (timestamp << 32) | (pid << 16) | (c << 8) | job_id;
}

uint64_t AsyncPipeline::pipeline_unique_id()
{
    return m_pipeline_unique_id;
}

Expected<std::shared_ptr<AsyncInferRunnerImpl>> AsyncInferRunnerImpl::create(std::shared_ptr<ConfiguredNetworkGroup> net_group,
    const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
    const uint32_t timeout)
{
    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto async_pipeline,
        AsyncPipelineBuilder::create_pipeline(net_group, inputs_formats, outputs_formats, timeout, pipeline_status));

    auto async_infer_runner_ptr = make_shared_nothrow<AsyncInferRunnerImpl>(std::move(async_pipeline), pipeline_status);
    CHECK_NOT_NULL_AS_EXPECTED(async_infer_runner_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = async_infer_runner_ptr->start_pipeline();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return async_infer_runner_ptr;
}

AsyncInferRunnerImpl::AsyncInferRunnerImpl(std::shared_ptr<AsyncPipeline> async_pipeline, std::shared_ptr<std::atomic<hailo_status>> pipeline_status) :
    m_async_pipeline(async_pipeline),
    m_is_activated(false),
    m_is_aborted(false),
    m_pipeline_status(pipeline_status)
{}

AsyncInferRunnerImpl::~AsyncInferRunnerImpl()
{
    (void)stop_pipeline();
}

hailo_status AsyncInferRunnerImpl::stop_pipeline()
{
    hailo_status status = HAILO_SUCCESS;
    if (m_is_activated) {
        m_is_activated = false;
        for (auto &entry_element : m_async_pipeline->get_entry_elements()) {
            status = entry_element.second->deactivate();
            if (HAILO_SUCCESS != status) {
                LOGGER__WARNING("Failed deactivate of element {} status {}", entry_element.second->name(), status);
            }

            auto should_clear_abort = (!m_is_aborted);
            status = entry_element.second->post_deactivate(should_clear_abort);
            if (HAILO_SUCCESS != status) {
                LOGGER__WARNING("Failed post deactivate of element {} status {}", entry_element.second->name(), status);
            }
        }
    }
    return status;
}

hailo_status AsyncInferRunnerImpl::start_pipeline()
{
    hailo_status status = HAILO_SUCCESS;
    for (auto &entry_element : m_async_pipeline->get_entry_elements()) {
        status = entry_element.second->activate();
        CHECK_SUCCESS(status);
    }

    m_is_activated = true;

    return status;
}

void AsyncInferRunnerImpl::abort()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_is_aborted = true;
    m_async_pipeline->shutdown(HAILO_STREAM_ABORT);
    return;
}

Expected<std::pair<bool, std::string>> AsyncInferRunnerImpl::can_push_buffers(uint32_t frames_count)
{
    for (auto &last_element : m_async_pipeline->get_last_elements()) {
        TRY(const auto can_push_buffer, last_element.second->can_push_buffer_upstream(frames_count));
        if (!can_push_buffer) {
            return std::make_pair(false, last_element.first);
        }
    }

    for (auto &entry_element : m_async_pipeline->get_entry_elements()) {
        TRY(const auto can_push_buffer, entry_element.second->can_push_buffer_downstream(frames_count));
        if (!can_push_buffer) {
            return std::make_pair(false, entry_element.first);
        }
    }

    return std::make_pair(true, std::string(""));
}

hailo_status AsyncInferRunnerImpl::set_buffers(std::unordered_map<std::string, PipelineBuffer> &inputs,
    std::unordered_map<std::string, PipelineBuffer> &outputs)
{
    for (auto &last_element : m_async_pipeline->get_last_elements()) {
        // TODO: handle the non-recoverable case where one buffer is enqueued successfully and the second isn't (HRT-11783)
        auto status = last_element.second->enqueue_execution_buffer(std::move(outputs.at(last_element.first)));
        CHECK_SUCCESS(status);
    }

    for (auto &entry_element : m_async_pipeline->get_entry_elements()) {
        entry_element.second->sinks()[0].run_push_async(std::move(inputs.at(entry_element.first)));
    }

    return HAILO_SUCCESS;
}

void AsyncInferRunnerImpl::set_pix_buffer_inputs(std::unordered_map<std::string, PipelineBuffer> &inputs, hailo_pix_buffer_t pix_buffer,
    TransferDoneCallbackAsyncInfer input_done, const std::string &input_name)
{
    if (1 == pix_buffer.number_of_planes) {
        if (HAILO_PIX_BUFFER_MEMORY_TYPE_DMABUF == pix_buffer.memory_type) {
            hailo_dma_buffer_t dma_buffer = {pix_buffer.planes[0].fd, pix_buffer.planes[0].plane_size};
            inputs[input_name] = PipelineBuffer(dma_buffer, input_done);
        } else if (HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == pix_buffer.memory_type) {
            inputs[input_name] = PipelineBuffer(MemoryView(pix_buffer.planes[0].user_ptr, pix_buffer.planes[0].bytes_used), input_done);
        } else {
            LOGGER__ERROR("Buffer type Pix buffer supports only memory of types USERPTR or DMABUF.");
            inputs[input_name] = PipelineBuffer(HAILO_INVALID_OPERATION, input_done);
        }
    } else if (m_async_pipeline->is_multi_planar()) {
        // If model is multi-planar
        inputs[input_name] = PipelineBuffer(pix_buffer, input_done);
    } else {
        // Other cases - return error, as on async flow we do not support copy to new buffer
        LOGGER__ERROR("HEF was compiled for single input layer, while trying to pass non-contiguous planes buffers.");
        inputs[input_name] = PipelineBuffer(HAILO_INVALID_OPERATION, input_done);
    }
}

hailo_status AsyncInferRunnerImpl::run(const ConfiguredInferModel::Bindings &bindings, TransferDoneCallbackAsyncInfer transfer_done)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    hailo_status status = m_async_pipeline->get_pipeline_status()->load();
    CHECK_SUCCESS(status, "Can't handle inference request since pipeline status is {}.", status);

    TRY(auto are_pools_ready_pair, can_push_buffers(1));
    CHECK(are_pools_ready_pair.first, HAILO_QUEUE_IS_FULL, "Can't handle infer request since a queue in the pipeline is full.");

    std::unordered_map<std::string, PipelineBuffer> outputs;

    for (auto &last_element : m_async_pipeline->get_last_elements()) {
        auto buff_type = bindings.output(last_element.first)->m_pimpl->get_type();
        bool is_user_buffer = true;
        if (BufferType::DMA_BUFFER == buff_type) {
            TRY(auto dma_buffer, bindings.output(last_element.first)->get_dma_buffer(), "Couldnt find output buffer for '{}'", last_element.first);
            outputs[last_element.first] = PipelineBuffer(dma_buffer, transfer_done, HAILO_SUCCESS, is_user_buffer);
        } else {
            TRY(auto buffer, bindings.output(last_element.first)->get_buffer(), "Couldnt find output buffer for '{}'", last_element.first);
            outputs[last_element.first] = PipelineBuffer(buffer, transfer_done, HAILO_SUCCESS, is_user_buffer);
        }
    }

    std::unordered_map<std::string, PipelineBuffer> inputs;
    for (auto &entry_element : m_async_pipeline->get_entry_elements()) {
        auto buff_type = bindings.input(entry_element.first)->m_pimpl->get_type();

        switch (buff_type) {
        case BufferType::VIEW:
        {
            TRY(auto buffer, bindings.input(entry_element.first)->get_buffer(), "Couldnt find input buffer for '{}'", entry_element.first);
            inputs[entry_element.first] = PipelineBuffer(buffer, transfer_done);
            break;
        }
        case BufferType::DMA_BUFFER:
        {
            TRY(auto dma_buffer, bindings.input(entry_element.first)->get_dma_buffer(), "Couldnt find input buffer for '{}'", entry_element.first);
            inputs[entry_element.first] = PipelineBuffer(dma_buffer, transfer_done);
            break;
        }
        case BufferType::PIX_BUFFER:
        {
            TRY(auto pix_buffer, bindings.input(entry_element.first)->get_pix_buffer(), "Couldnt find input buffer for '{}'", entry_element.first);

            set_pix_buffer_inputs(inputs, pix_buffer, transfer_done, entry_element.first);
            break;
        }

        default:
            CHECK(false, HAILO_NOT_FOUND, "Couldnt find input buffer for '{}'", entry_element.first);
        }
    }

    status = set_buffers(inputs, outputs);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

void AsyncInferRunnerImpl::add_element_to_pipeline(std::shared_ptr<PipelineElement> pipeline_element)
{
    m_async_pipeline->add_element_to_pipeline(pipeline_element);
}

void AsyncInferRunnerImpl::add_entry_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &input_name)
{
    m_async_pipeline->add_entry_element(pipeline_element, input_name);
}

void AsyncInferRunnerImpl::add_last_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &output_name)
{
    m_async_pipeline->add_last_element(pipeline_element, output_name);
}

std::unordered_map<std::string, std::shared_ptr<PipelineElement>> AsyncInferRunnerImpl::get_entry_elements()
{
    return m_async_pipeline->get_entry_elements();
}

std::unordered_map<std::string, std::shared_ptr<PipelineElement>> AsyncInferRunnerImpl::get_last_elements()
{
    return m_async_pipeline->get_last_elements();
}

std::vector<std::shared_ptr<PipelineElement>> AsyncInferRunnerImpl::get_pipeline() const
{
    return m_async_pipeline->get_pipeline();
}

std::string AsyncInferRunnerImpl::get_pipeline_description() const
{
    std::stringstream pipeline_str;
    pipeline_str << "Async infer pipeline description:\n";
    for (const auto &element : get_pipeline()) {
	    pipeline_str << " >> " << element->description();
    }
    return pipeline_str.str();
}

hailo_status AsyncInferRunnerImpl::get_pipeline_status() const
{
    return m_pipeline_status->load();
}

std::shared_ptr<AsyncPipeline> AsyncInferRunnerImpl::get_async_pipeline() const
{
    return m_async_pipeline;
}

uint64_t AsyncInferRunnerImpl::pipeline_unique_id() const
{
    return m_async_pipeline->pipeline_unique_id();
}

} /* namespace hailort */
