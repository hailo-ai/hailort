/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file edge_elements.cpp
 * @brief Implementation of the edge elements (sinks and sources)
 **/

#include "net_flow/pipeline/vstream_internal.hpp"
#include "net_flow/pipeline/edge_elements.hpp"
#include "utils/profiler/tracer_macros.hpp"

namespace hailort
{

PipelinePad &SinkElement::sink()
{
    return m_sinks[0];
}

std::vector<PipelinePad*> SinkElement::execution_pads()
{
    std::vector<PipelinePad*> result{&sink()};
    return result;
}

hailo_status SinkElement::execute_terminate(hailo_status /*error_status*/)
{
    return HAILO_SUCCESS;
}

hailo_status SinkElement::execute_dequeue_user_buffers(hailo_status /*error_status*/)
{
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<HwWriteElement>> HwWriteElement::create(std::shared_ptr<InputStreamBase> stream, const std::string &name,
    hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    PipelineDirection pipeline_direction)
{
    TRY(auto duration_collector, DurationCollector::create(elem_flags));
    TRY(auto got_flush_event, Event::create_shared(Event::State::not_signalled));

    // On HwWriteElement the stream always owns the buffer, hence, we set the mode explicitly.
    auto status = stream->set_buffer_mode(StreamBufferMode::OWNING);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto hw_write_elem_ptr = make_shared_nothrow<HwWriteElement>(stream, name,
        std::move(duration_collector), std::move(pipeline_status), std::move(got_flush_event), pipeline_direction);
    CHECK_AS_EXPECTED(nullptr != hw_write_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", hw_write_elem_ptr->description());

    return hw_write_elem_ptr;
}

HwWriteElement::HwWriteElement(std::shared_ptr<InputStreamBase> stream, const std::string &name, DurationCollector &&duration_collector,
                               std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr got_flush_event, PipelineDirection pipeline_direction) :
    SinkElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, nullptr),
    m_stream(stream), m_got_flush_event(got_flush_event)
{}

Expected<PipelineBuffer> HwWriteElement::run_pull(PipelineBuffer &&/*optional*/, const PipelinePad &/*source*/)
{
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status HwWriteElement::run_push(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    if (PipelineBuffer::Type::FLUSH == buffer.get_type()) {
        hailo_status flush_status = m_stream->flush();
        if (HAILO_STREAM_ABORT == flush_status) {
            LOGGER__INFO("Failed flushing input stream {} because stream was aborted", m_stream->to_string());
        } else if (HAILO_SUCCESS != flush_status) {
            LOGGER__ERROR("flush has failed in {} with status {}", name(), flush_status);
        }
        hailo_status status = m_got_flush_event->signal();
        CHECK_SUCCESS(status);
        return HAILO_SUCCESS;
    }

    m_duration_collector.start_measurement();
    const auto status = m_stream->write(MemoryView(buffer.data(), buffer.size()));
    m_duration_collector.complete_measurement();

    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("Failed to send on input stream {} because stream was aborted", m_stream->to_string());
        return HAILO_STREAM_ABORT;
    }
    CHECK_SUCCESS(status, "{} (H2D) failed with status={}", name(), status);

    return HAILO_SUCCESS;
}

void HwWriteElement::run_push_async(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    LOGGER__ERROR("run_push_async is not supported for {}", name());
    assert(false);
}

hailo_status HwWriteElement::execute_activate()
{
    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_deactivate()
{
    // The flush operation will block until all buffers currently in the pipeline will be processed.
    // We assume that no buffers are sent after the call for deactivate.
    hailo_status flush_status = m_stream->flush();
    if (HAILO_STREAM_ABORT == flush_status) {
        LOGGER__INFO("Failed flushing input stream {} because stream was aborted", m_stream->to_string());
        return HAILO_SUCCESS;
    } else if (HAILO_STREAM_NOT_ACTIVATED == flush_status) {
        LOGGER__INFO("Failed flushing input stream {} because stream is not activated", m_stream->to_string());
        return HAILO_SUCCESS;
    } else if (HAILO_SUCCESS != flush_status) {
        LOGGER__ERROR("flush has failed in {} with status {}", name(), flush_status);
    }

    auto abort_status = execute_abort();
    CHECK(((abort_status == HAILO_SUCCESS) || (abort_status == HAILO_STREAM_NOT_ACTIVATED)), abort_status,
        "Failed to abort stream in {}", name());
    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_post_deactivate(bool should_clear_abort)
{
    if (should_clear_abort) {
        auto status = execute_clear_abort();
        CHECK(((status == HAILO_SUCCESS) || (status == HAILO_STREAM_NOT_ACTIVATED)), status,
            "Failed to clear abort stream in {}", name());
    }
    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_clear()
{
    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_flush()
{
    hailo_status status = m_got_flush_event->wait(m_stream->get_timeout());
    CHECK_SUCCESS(status);

    status = m_got_flush_event->reset();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_abort()
{
    return m_stream->abort_impl();
}

hailo_status HwWriteElement::execute_clear_abort()
{
    return m_stream->clear_abort_impl();
}

std::string HwWriteElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << " | hw_frame_size: " << m_stream->get_info().hw_frame_size << ")";   

    return element_description.str();
}

Expected<std::shared_ptr<LastAsyncElement>> LastAsyncElement::create(const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
    hailo_vstream_stats_flags_t vstream_stats_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status, size_t queue_size,
    size_t frame_size, EventPtr shutdown_event, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(auto duration_collector, DurationCollector::create(elem_flags));

    auto is_empty = true; // LastAsync always holds user buffers, therefore its created empty
    auto is_dma_able = false;
    queue_size = queue_size * 2; // Multiplying by 2 to ensure dual-buffering when edge-element is the bottleneck
    TRY(auto buffer_pool,
        PipelineBufferPool::create(frame_size, queue_size, shutdown_event, elem_flags, vstream_stats_flags, is_empty, is_dma_able));

    auto last_async_elem_ptr = make_shared_nothrow<LastAsyncElement>(name,
        std::move(duration_collector), std::move(pipeline_status), std::move(buffer_pool), async_pipeline);
    CHECK_NOT_NULL_AS_EXPECTED(last_async_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", last_async_elem_ptr->description());

    return last_async_elem_ptr;
}

Expected<std::shared_ptr<LastAsyncElement>> LastAsyncElement::create(const std::string &name,
    const ElementBuildParams &build_params, size_t frame_size, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return LastAsyncElement::create(name, build_params.elem_stats_flags, build_params.vstream_stats_flags, build_params.pipeline_status,
        build_params.buffer_pool_size_edges, frame_size, build_params.shutdown_event, async_pipeline);
}

LastAsyncElement::LastAsyncElement(const std::string &name, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    PipelineBufferPoolPtr buffer_pool, std::shared_ptr<AsyncPipeline> async_pipeline):
    SinkElement(name, std::move(duration_collector), std::move(pipeline_status), PipelineDirection::PUSH, async_pipeline),
    m_pool(buffer_pool)
{}

Expected<PipelineBuffer> LastAsyncElement::run_pull(PipelineBuffer &&/*optional*/, const PipelinePad &/*source*/)
{
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status LastAsyncElement::run_push(PipelineBuffer &&/*optional*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

void LastAsyncElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    TRACE(RunPushAsyncStartTrace, name(), pipeline_unique_id(), network_name());

    // Call callback here on LastAsyncElement because if we wait for destructor to call callbacks they can be
    // called out of order
    buffer.call_exec_done();
    TRACE(RunPushAsyncEndTrace, name(), pipeline_unique_id(), network_name());
}

hailo_status LastAsyncElement::execute_activate()
{
    return HAILO_SUCCESS;
}

hailo_status LastAsyncElement::enqueue_execution_buffer(PipelineBuffer &&pipeline_buffer)
{
    return m_pool->enqueue_buffer(std::move(pipeline_buffer));
}

Expected<bool> LastAsyncElement::can_push_buffer_upstream(uint32_t frames_count)
{
    return (m_pool->num_of_buffers_in_pool() + frames_count) < m_pool->max_capacity();
}

SourceElement::SourceElement(const std::string &name, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline)
{
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
}

hailo_status LastAsyncElement::execute_dequeue_user_buffers(hailo_status error_status)
{
    auto empty_pool_status = empty_buffer_pool(m_pool, error_status, BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT);
    CHECK_SUCCESS(empty_pool_status);

    return HAILO_SUCCESS;
}

PipelinePad &SourceElement::source()
{
    return m_sources[0];
}

std::vector<PipelinePad*> SourceElement::execution_pads()
{
    std::vector<PipelinePad*> result{&source()};
    return result;
}

SinkElement::SinkElement(const std::string &name, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline)
{
    m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
}

Expected<std::shared_ptr<HwReadElement>> HwReadElement::create(std::shared_ptr<OutputStreamBase> stream, const std::string &name, 
    const ElementBuildParams &build_params, PipelineDirection pipeline_direction)
{
    // On HwReadElement the stream always owns the buffer, hence, we set the mode explicitly.
    auto status = stream->set_buffer_mode(StreamBufferMode::OWNING);
    CHECK_SUCCESS_AS_EXPECTED(status);

    TRY(auto duration_collector, DurationCollector::create(build_params.elem_stats_flags));

    auto pipeline_status = build_params.pipeline_status;

    TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));

    auto hw_read_elem_ptr = make_shared_nothrow<HwReadElement>(stream, name, build_params.timeout,
        std::move(duration_collector), std::move(shutdown_event), std::move(pipeline_status), pipeline_direction);
    CHECK_AS_EXPECTED(nullptr != hw_read_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", hw_read_elem_ptr->description());

    return hw_read_elem_ptr;
}

HwReadElement::HwReadElement(std::shared_ptr<OutputStreamBase> stream, const std::string &name,
    std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
    EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    PipelineDirection pipeline_direction) :
    SourceElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, nullptr),
    m_stream(stream),
    m_timeout(timeout),
    m_shutdown_event(shutdown_event),
    m_activation_wait_or_shutdown(stream->get_core_op_activated_event(), shutdown_event)
{}

uint32_t HwReadElement::get_invalid_frames_count()
{
    return m_stream->get_invalid_frames_count();
}

std::string HwReadElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << " | hw_frame_size: " << m_stream->get_info().hw_frame_size << ")";   

    return element_description.str();
}

hailo_status HwReadElement::execute_post_deactivate(bool should_clear_abort)
{
    if (should_clear_abort) {
        auto status = execute_clear_abort();
        CHECK(((HAILO_SUCCESS == status) || (HAILO_STREAM_NOT_ACTIVATED == status)), status,
            "Failed to clear abort stream in {}", name());
    }
    return HAILO_SUCCESS;
}

hailo_status HwReadElement::execute_clear()
{
    return HAILO_SUCCESS;
}

hailo_status HwReadElement::execute_flush()
{
    return HAILO_INVALID_OPERATION;
}

hailo_status HwReadElement::execute_abort()
{
    return m_stream->abort_impl();
}

hailo_status HwReadElement::execute_clear_abort()
{
    return m_stream->clear_abort_impl();
}

void HwReadElement::run_push_async(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    LOGGER__ERROR("run_push_async is not supported for {}", name());
    assert(false);
}

hailo_status HwReadElement::run_push(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

Expected<PipelineBuffer> HwReadElement::run_pull(PipelineBuffer &&optional, const PipelinePad &/*source*/)
{
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto buffer,
        pool->get_available_buffer(std::move(optional), m_timeout),
        "{} (D2H) failed.", name());

    while (true) {
        if (!m_stream->is_scheduled()) {
            auto status = m_activation_wait_or_shutdown.wait(m_timeout);
            if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
                return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
            }
            if (HAILO_TIMEOUT == status) {
                return make_unexpected(HAILO_NETWORK_GROUP_NOT_ACTIVATED);
            }
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else {
            auto status = m_activation_wait_or_shutdown.wait(std::chrono::milliseconds(0));
            if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
                return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
            }
        }

        TRY(MemoryView buffer_view, buffer.as_view(BufferProtection::NONE));

        m_duration_collector.start_measurement();
        auto status = m_stream->read(buffer_view);
        if (HAILO_INVALID_FRAME == status) {
            m_stream->increase_invalid_frames_count(1);
            status = HAILO_SUCCESS;
        }
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            // Try again
            continue;
        }
        if (HAILO_STREAM_ABORT == status) {
            LOGGER__INFO("Reading from stream was aborted!");
            return make_unexpected(HAILO_STREAM_ABORT);
        }
        CHECK_SUCCESS_AS_EXPECTED(status, "{} (D2H) failed with status={}", name(), status);
        m_duration_collector.complete_measurement();

        return buffer;
    }
}

hailo_status HwReadElement::execute_activate()
{
    CHECK_SUCCESS(m_shutdown_event->reset(), "Failed to reset shutdown event for {}", name());

    return HAILO_SUCCESS;
}

hailo_status HwReadElement::execute_deactivate()
{
    auto signal_shutdown_status = m_shutdown_event->signal();
    if (HAILO_SUCCESS != signal_shutdown_status) {
        LOGGER__ERROR("Signaling {} shutdown event failed with {}", name(), signal_shutdown_status);
    }

    auto abort_status = execute_abort();
    if ((HAILO_SUCCESS != abort_status) && (HAILO_STREAM_NOT_ACTIVATED != abort_status)) {
        LOGGER__ERROR("Abort {} failed with {}", name(), abort_status);
        return abort_status;
    }
 
    return signal_shutdown_status;
}


} /* namespace hailort */
