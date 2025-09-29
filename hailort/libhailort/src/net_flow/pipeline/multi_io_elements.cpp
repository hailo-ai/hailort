/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_io_elements.cpp
 * @brief Implementation of the multiple input/outputs elements
 **/

#include "net_flow/pipeline/vstream_internal.hpp"
#include "net_flow/pipeline/multi_io_elements.hpp"
#include "utils/profiler/tracer_macros.hpp"

namespace hailort
{

BaseMuxElement::BaseMuxElement(size_t sink_count, const std::string &name, std::chrono::milliseconds timeout,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
    m_timeout(timeout)
{
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
    m_sinks.reserve(sink_count);
    for (uint32_t i = 0; i < sink_count; ++i) {
        m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
        m_sink_name_to_index[m_sinks[i].name()] = i;
    }
}

std::vector<PipelinePad*> BaseMuxElement::execution_pads()
{
    if (m_next_pads.size() == 0) {
        if (PipelineDirection::PUSH == m_pipeline_direction) {
            m_next_pads.reserve(m_sources.size());
            for (auto &source : m_sources ) {
                m_next_pads.push_back(source.next());
            }
        } else {
            m_next_pads.reserve(m_sinks.size());
            for (auto &sink : m_sinks ) {
                m_next_pads.push_back(sink.prev());
            }
        }
    }
    return m_next_pads;
}

hailo_status BaseMuxElement::execute_terminate(hailo_status error_status)
{
    if (m_is_terminated) {
        return HAILO_SUCCESS;
    }

    auto terminate_status = PipelineElement::execute_terminate(error_status);
    CHECK_SUCCESS(terminate_status);

    return HAILO_SUCCESS;
}


hailo_status BaseMuxElement::run_push(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

void BaseMuxElement::run_push_async(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    // Should use run_push_async_multi for mux elements
    LOGGER__CRITICAL("run_push_async is not supported for {}", name());
    assert(false);
}

void BaseMuxElement::run_push_async_multi(std::vector<PipelineBuffer> &&buffers)
{
    TRACE(RunPushAsyncStartTrace, name(), pipeline_unique_id(), network_name());
    auto next_pads = execution_pads();
    assert(PipelineDirection::PUSH == m_pipeline_direction);
    assert(next_pads.size() == 1);

    if (HAILO_SUCCESS == m_pipeline_status->load()) {

        for (auto &input_buffer : buffers) {
            if (HAILO_SUCCESS != input_buffer.action_status()) {
                handle_non_recoverable_async_error(input_buffer.action_status());
                return;
            }
        }

        auto output = action(std::move(buffers), PipelineBuffer());
        if (HAILO_SUCCESS == output.status()) {
            next_pads[0]->run_push_async(output.release());
        } else {
            next_pads[0]->run_push_async(PipelineBuffer(output.status()));
        }
    }
    TRACE(RunPushAsyncEndTrace, name(), pipeline_unique_id(), network_name());
}

Expected<PipelineBuffer> BaseMuxElement::run_pull(PipelineBuffer &&optional, const PipelinePad &/*source*/)
{
    CHECK_AS_EXPECTED(m_pipeline_direction == PipelineDirection::PULL, HAILO_INVALID_OPERATION,
        "PostInferElement {} does not support run_pull operation", name());
    std::vector<PipelineBuffer> inputs;
    inputs.reserve(m_sinks.size());
    for (auto &sink : m_sinks) {
        auto buffer = sink.prev()->run_pull();
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
            return make_unexpected(buffer.status());
        }
        CHECK_EXPECTED(buffer);

        inputs.push_back(buffer.release());
    }

    auto output = action(std::move(inputs), std::move(optional));
    CHECK_EXPECTED(output);

    return output;
}

Expected<std::shared_ptr<NmsPostProcessMuxElement>> NmsPostProcessMuxElement::create(std::shared_ptr<net_flow::Op> nms_op,
    const std::string &name, std::chrono::milliseconds timeout, hailo_pipeline_elem_stats_flags_t elem_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    assert(nms_op->outputs_metadata().size() == 1);
    auto vstream_info = nms_op->metadata()->get_output_vstream_info();
    CHECK_EXPECTED(vstream_info);

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto nms_elem_ptr = make_shared_nothrow<NmsPostProcessMuxElement>(nms_op, name, timeout,
        duration_collector.release(), std::move(pipeline_status), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != nms_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", nms_elem_ptr->description());
    return nms_elem_ptr;
}

Expected<std::shared_ptr<NmsPostProcessMuxElement>> NmsPostProcessMuxElement::create(std::shared_ptr<net_flow::Op> nms_op,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return NmsPostProcessMuxElement::create(nms_op, name, build_params.timeout,
        build_params.elem_stats_flags,
        build_params.pipeline_status, pipeline_direction, async_pipeline);
}

Expected<std::shared_ptr<NmsPostProcessMuxElement>> NmsPostProcessMuxElement::create(std::shared_ptr<net_flow::Op> nms_op,
       const std::string &name, const hailo_vstream_params_t &vstream_params,
       std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction,
       std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return NmsPostProcessMuxElement::create(nms_op, name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.pipeline_elements_stats_flags,
        pipeline_status, pipeline_direction, async_pipeline);
}

NmsPostProcessMuxElement::NmsPostProcessMuxElement(std::shared_ptr<net_flow::Op> nms_op,
    const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline) :
    BaseMuxElement(nms_op->inputs_metadata().size(), name, timeout, std::move(duration_collector), std::move(pipeline_status),
        pipeline_direction, async_pipeline),
    m_nms_op(nms_op),
    m_sinks_names(nms_op->inputs_metadata().size())
{}

Expected<PipelineBuffer> NmsPostProcessMuxElement::action(std::vector<PipelineBuffer> &&input_buffers, PipelineBuffer &&optional)
{
    std::map<std::string, MemoryView> inputs;
    std::map<std::string, MemoryView> outputs;
    for (size_t i = 0; i < input_buffers.size(); ++i) {
        TRY(auto src, input_buffers[i].as_view(BufferProtection::READ));
        inputs.insert({m_sinks_names[i], src});
    }
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    auto acquired_buffer = pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == acquired_buffer.status()) {
        return make_unexpected(acquired_buffer.status());
    }

    if (!acquired_buffer) {
        for (auto &input : input_buffers) {
            input.set_action_status(acquired_buffer.status());
        }
    }
    CHECK_EXPECTED(acquired_buffer);
    TRY(auto dst, acquired_buffer->as_view(BufferProtection::WRITE));
    outputs.insert({"", dst}); // TODO: fill with correct name
    m_duration_collector.start_measurement();

    auto post_process_result = m_nms_op->execute(inputs, outputs);
    m_duration_collector.complete_measurement();

    for (auto &input : input_buffers) {
        input.set_action_status(post_process_result);
    }
    acquired_buffer->set_action_status(post_process_result);

    if (post_process_result != HAILO_INSUFFICIENT_BUFFER) {
        // In YOLOv5-Seg there is an option for the user to change the frame size.
        // Therefore we want to return an error status if the buffer is not big enough for all the detections found.
        // We return the actual buffer and the error status,
        // so the user will be able to choose if the change the frame_size or ignore the rest of the detections.
        CHECK_SUCCESS_AS_EXPECTED(post_process_result);
    }
    return acquired_buffer;
}

std::string NmsPostProcessMuxElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << " | " << m_nms_op->metadata()->get_op_description() << ")";
    return element_description.str();
}

static hailo_nms_info_t fuse_nms_info(const std::vector<hailo_nms_info_t> &nms_infos)
{
    hailo_nms_info_t fused_info = nms_infos[0];
    fused_info.is_defused = false;
    fused_info.number_of_classes = 0;
    for (const auto &nms_info : nms_infos) {
        fused_info.number_of_classes += nms_info.number_of_classes;
        assert(nms_infos[0].max_bboxes_per_class == nms_info.max_bboxes_per_class);
        assert(nms_infos[0].bbox_size == nms_info.bbox_size);
        assert(nms_infos[0].chunks_per_frame == nms_info.chunks_per_frame);
        assert(nms_infos[0].burst_size == nms_info.burst_size);
        assert(nms_infos[0].burst_type == nms_info.burst_type);
    }
    return fused_info;
}

Expected<std::shared_ptr<NmsMuxElement>> NmsMuxElement::create(const std::vector<hailo_nms_info_t> &nms_infos,
    const std::string &name, std::chrono::milliseconds timeout, hailo_pipeline_elem_stats_flags_t elem_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    const auto &fused_info = fuse_nms_info(nms_infos);

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto nms_elem_ptr = make_shared_nothrow<NmsMuxElement>(nms_infos, fused_info, name, timeout,
        duration_collector.release(), std::move(pipeline_status), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != nms_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", nms_elem_ptr->description());

    return nms_elem_ptr;
}

Expected<std::shared_ptr<NmsMuxElement>> NmsMuxElement::create(const std::vector<hailo_nms_info_t> &nms_infos, const std::string &name,
    const hailo_vstream_params_t &vstream_params, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return NmsMuxElement::create(nms_infos, name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.pipeline_elements_stats_flags, pipeline_status, pipeline_direction,
        async_pipeline);
}

Expected<std::shared_ptr<NmsMuxElement>> NmsMuxElement::create(const std::vector<hailo_nms_info_t> &nms_infos,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return NmsMuxElement::create(nms_infos, name, build_params.timeout, build_params.elem_stats_flags,
        build_params.pipeline_status, pipeline_direction, async_pipeline);
}

NmsMuxElement::NmsMuxElement(const std::vector<hailo_nms_info_t> &nms_infos, const hailo_nms_info_t &fused_nms_info,
    const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline) :
    BaseMuxElement(nms_infos.size(), name, timeout, std::move(duration_collector), std::move(pipeline_status), pipeline_direction,
        async_pipeline),
    m_nms_infos(nms_infos),
    m_fused_nms_info(fused_nms_info)
{}

const hailo_nms_info_t &NmsMuxElement::get_fused_nms_info() const
{
    return m_fused_nms_info;
}

Expected<PipelineBuffer> NmsMuxElement::action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional)
{
    std::vector<MemoryView> input_views;

    input_views.reserve(inputs.size());
    for (auto &input_buf : inputs) {
        TRY(auto src, input_buf.as_view(BufferProtection::READ));
        input_views.push_back(src);
    }
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    auto acquired_buffer = pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == acquired_buffer.status()) {
        return make_unexpected(acquired_buffer.status());
    }

    if (!acquired_buffer) {
        for (auto &input : inputs) {
            input.set_action_status(acquired_buffer.status());
        }
    }    
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != acquired_buffer.status(), HAILO_TIMEOUT,
        "{} failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_timeout.count());
    CHECK_EXPECTED(acquired_buffer);

    m_duration_collector.start_measurement();
    TRY(auto dst, acquired_buffer.value().as_view(BufferProtection::WRITE));
    const auto status = fuse_buffers(input_views, m_nms_infos, dst);
    m_duration_collector.complete_measurement();

    for (auto &input : inputs) {
        input.set_action_status(status);
    }
    acquired_buffer->set_action_status(status);

    CHECK_SUCCESS_AS_EXPECTED(status);

    return acquired_buffer.release();
}

BaseDemuxElement::BaseDemuxElement(size_t source_count, const std::string &name, std::chrono::milliseconds timeout,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
    m_timeout(timeout),
    m_is_activated(false),
    m_was_stream_aborted(false),
    m_source_name_to_index(),
    m_was_source_called(source_count, false),
    m_buffers_for_action()
{
    m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
    m_sources.reserve(source_count);
    for (uint32_t i = 0; i < source_count; i++) {
        m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
        m_source_name_to_index[m_sources[i].name()] = i;
    }
}

hailo_status BaseDemuxElement::run_push(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    CHECK(PipelineDirection::PUSH == m_pipeline_direction, HAILO_INVALID_OPERATION,
        "BaseDemuxElement {} does not support run_push operation", name());

    auto outputs = action(std::move(buffer));
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == outputs.status()) {
        return outputs.status();
    }
    CHECK_EXPECTED_AS_STATUS(outputs);

    for (const auto &pad : execution_pads()) {
        assert(m_source_name_to_index.count(pad->prev()->name()) > 0);
        auto source_index = m_source_name_to_index[pad->prev()->name()];
        auto status = pad->run_push(std::move(outputs.value()[source_index]));

        if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
            LOGGER__INFO("run_push of {} was shutdown!", name());
            return status;
        }
        if (HAILO_STREAM_ABORT == status) {
            LOGGER__INFO("run_push of {} was aborted!", name());
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

void BaseDemuxElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    TRACE(RunPushAsyncStartTrace, name(), pipeline_unique_id(), network_name());
    assert(PipelineDirection::PUSH == m_pipeline_direction);
    if (HAILO_SUCCESS != buffer.action_status()) {
        for (const auto &pad : execution_pads()) {
            auto source_index = m_source_name_to_index[pad->prev()->name()];
            auto pool = m_sources[source_index].next()->get_buffer_pool();
            assert(pool);

            auto acquired_buffer = pool->acquire_buffer(m_timeout);
            if (HAILO_SUCCESS == acquired_buffer.status()) {
                acquired_buffer->set_action_status(buffer.action_status());
                pad->run_push_async(acquired_buffer.release());
            } else {
                handle_non_recoverable_async_error(acquired_buffer.status());
            }
        }
        return;
    }

    auto outputs = action(std::move(buffer));

    for (const auto &pad : execution_pads()) {
        assert(m_source_name_to_index.count(pad->prev()->name()) > 0);
        auto source_index = m_source_name_to_index[pad->prev()->name()];
        if (HAILO_SUCCESS == outputs.status()) {
            pad->run_push_async(std::move(outputs.value()[source_index]));
        } else {
            pad->run_push_async(PipelineBuffer(outputs.status()));
        }
    }
    TRACE(RunPushAsyncEndTrace, name(), pipeline_unique_id(), network_name());
}

Expected<PipelineBuffer> BaseDemuxElement::run_pull(PipelineBuffer &&optional, const PipelinePad &source)
{
    CHECK_AS_EXPECTED(m_pipeline_direction == PipelineDirection::PULL, HAILO_INVALID_OPERATION,
        "BaseDemuxElement {} does not support run_pull operation", name());

    CHECK_AS_EXPECTED(!optional, HAILO_INVALID_ARGUMENT, "Optional buffer is not allowed in demux element!");

    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_is_activated) {
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }

    if (m_was_stream_aborted) {
        return make_unexpected(HAILO_STREAM_ABORT);
    }

    m_was_source_called[m_source_name_to_index[source.name()]] = true;

    if (were_all_srcs_arrived()) {
        // If all srcs arrived, execute the demux
        auto input = execution_pads()[0]->run_pull();
        if (HAILO_STREAM_ABORT == input.status()) {
            LOGGER__INFO("run_pull of demux element was aborted!");
            m_was_stream_aborted = true;
            lock.unlock();
            m_cv.notify_all();
            return make_unexpected(input.status());
        }
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == input.status()) {
            LOGGER__INFO("run_pull of demux element was aborted in {} because pipeline deactivated!", name());
            m_is_activated = false;
            lock.unlock();
            m_cv.notify_all();
            return make_unexpected(input.status());
        }
        CHECK_EXPECTED(input);

        auto outputs = action(input.release());
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == outputs.status()) {
            LOGGER__INFO("run_pull of demux element was aborted in {} because pipeline deactivated!", name());
            m_is_activated = false;
            lock.unlock();
            m_cv.notify_all();
            return make_unexpected(outputs.status());
        }
        CHECK_EXPECTED(outputs);

        m_buffers_for_action = outputs.release();

        for (uint32_t i = 0; i < m_was_source_called.size(); i++) {
            m_was_source_called[i] = false;
        }

        // Manual unlocking is done before notifying, to avoid waking up the waiting thread only to block again
        lock.unlock();
        m_cv.notify_all();
    } else {
        // If not all srcs arrived, wait until m_was_source_called is false (set to false after the demux execution)
        auto wait_successful = m_cv.wait_for(lock, m_timeout, [&](){
            return !m_was_source_called[m_source_name_to_index[source.name()]] || m_was_stream_aborted || !m_is_activated;
        });
        CHECK_AS_EXPECTED(wait_successful, HAILO_TIMEOUT, "Waiting for other threads in demux {} has reached a timeout (timeout={}ms)", name(), m_timeout.count());

        if (m_was_stream_aborted) {
            lock.unlock();
            m_cv.notify_all();
            return make_unexpected(HAILO_STREAM_ABORT);
        }

        // We check if the element is not activated in case notify_all() was called from deactivate()
        if (!m_is_activated) {
            lock.unlock();
            m_cv.notify_all();
            return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
        }
    }

    assert(m_source_name_to_index[source.name()] < m_buffers_for_action.size());
    return std::move(m_buffers_for_action[m_source_name_to_index[source.name()]]);
}

bool BaseDemuxElement::were_all_srcs_arrived()
{
    return std::all_of(m_was_source_called.begin(), m_was_source_called.end(), [](bool v) { return v; });
}

hailo_status BaseDemuxElement::execute_activate()
{
    if (m_is_activated) {
        return HAILO_SUCCESS;
    }
    m_is_activated = true;// TODO Should this always be true, no matter the status of source().activate()?
    m_was_stream_aborted = false;

    return PipelineElementInternal::execute_activate();
}

hailo_status BaseDemuxElement::execute_deactivate()
{
    if (!m_is_activated) {
        return HAILO_SUCCESS;
    }
    m_is_activated = false;

    // deactivate should be called before mutex acquire and notify_all because it is possible that all queues are waiting on
    // the run_pull of the source (HwRead) and the mutex is already acquired so this would prevent a timeout error
    hailo_status status = PipelineElementInternal::execute_deactivate();

    {
        // There is a case where the other thread is halted (via context switch) before the wait_for() function,
        // then we call notify_all() here, and then the wait_for() is called - resulting in a timeout.
        // notify_all() only works on threads which are already waiting, so that's why we acquire the lock here.
        std::unique_lock<std::mutex> lock(m_mutex);
    }
    m_cv.notify_all();

    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status BaseDemuxElement::execute_post_deactivate(bool should_clear_abort)
{
    for (uint32_t i = 0; i < m_was_source_called.size(); i++) {
        m_was_source_called[i] = false;
    }
    return PipelineElementInternal::execute_post_deactivate(should_clear_abort);
}

hailo_status BaseDemuxElement::execute_abort()
{
    auto status = PipelineElementInternal::execute_abort();
    CHECK_SUCCESS(status);
    {
        // There is a case where the other thread is halted (via context switch) before the wait_for() function,
        // then we call notify_all() here, and then the wait_for() is called - resulting in a timeout.
        // notify_all() only works on threads which are already waiting, so that's why we acquire the lock here.
        std::unique_lock<std::mutex> lock(m_mutex);
    }
    m_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status BaseDemuxElement::set_timeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
    return HAILO_SUCCESS;
}

Expected<uint32_t> BaseDemuxElement::get_source_index_from_source_name(const std::string &source_name)
{
    CHECK_AS_EXPECTED(contains(m_source_name_to_index, source_name), HAILO_NOT_FOUND);
    auto ret_val = m_source_name_to_index.at(source_name);
    return ret_val;
}

std::vector<PipelinePad*> BaseDemuxElement::execution_pads()
{
    if (m_next_pads.size() == 0)
    {
        if (PipelineDirection::PUSH == m_pipeline_direction) {
            m_next_pads.reserve(m_sources.size());
            for (auto &source : m_sources ) {
                m_next_pads.push_back(source.next());
            }
        } else {
            m_next_pads.reserve(m_sinks.size());
            for (auto &sink : m_sinks ) {
                m_next_pads.push_back(sink.prev());
            }
        }
    }
    return m_next_pads;
}

Expected<std::shared_ptr<TransformDemuxElement>> TransformDemuxElement::create(std::shared_ptr<OutputDemuxer> demuxer,
    const std::string &name, std::chrono::milliseconds timeout, hailo_pipeline_elem_stats_flags_t elem_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);


    auto demux_elem_ptr = make_shared_nothrow<TransformDemuxElement>(demuxer, name, timeout,
        duration_collector.release(), std::move(pipeline_status), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != demux_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return demux_elem_ptr;
}

Expected<std::shared_ptr<TransformDemuxElement>> TransformDemuxElement::create(std::shared_ptr<OutputDemuxer> demuxer,
    const std::string &name, const ElementBuildParams &build_params,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return TransformDemuxElement::create(demuxer, name, build_params.timeout, build_params.elem_stats_flags,
        build_params.pipeline_status, pipeline_direction, async_pipeline);
}

TransformDemuxElement::TransformDemuxElement(std::shared_ptr<OutputDemuxer> demuxer,
    const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline) :
    BaseDemuxElement(demuxer->get_edges_stream_info().size(), name, timeout, std::move(duration_collector),
        std::move(pipeline_status), pipeline_direction, async_pipeline),
    m_demuxer(demuxer) 
{}

Expected<std::vector<PipelineBuffer>> TransformDemuxElement::action(PipelineBuffer &&input)
{
    std::vector<PipelineBuffer> outputs;
    std::vector<MemoryView> raw_buffers;

    auto mux_edges = m_demuxer->get_edges_stream_info();
    outputs.reserve(mux_edges.size());
    raw_buffers.reserve(mux_edges.size());

    for (uint32_t i = 0; i < mux_edges.size(); i++) {

        auto pool = m_sources[i].next()->get_buffer_pool();
        assert(pool);

        auto acquired_buffer = pool->acquire_buffer(m_timeout);
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == acquired_buffer.status()) {
            return make_unexpected(acquired_buffer.status());
        }

        if (!acquired_buffer) {
                input.set_action_status(acquired_buffer.status());
        } 
        CHECK_EXPECTED(acquired_buffer, "Failed to acquire buffer");
        outputs.emplace_back(acquired_buffer.release());
        TRY(auto dst, outputs.back().as_view(BufferProtection::WRITE));
        raw_buffers.push_back(dst);
    }

    m_duration_collector.start_measurement();
    TRY(auto src, input.as_view(BufferProtection::READ));
    const auto status = m_demuxer->transform_demux(src, raw_buffers);
    m_duration_collector.complete_measurement();

    input.set_action_status(status);
    for (auto &output : outputs) {
        output.set_action_status(status);
    }

    CHECK_SUCCESS_AS_EXPECTED(status);

    return outputs;
}

PixBufferElement::PixBufferElement(const std::string &name, std::chrono::milliseconds timeout,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    hailo_format_order_t order, std::shared_ptr<AsyncPipeline> async_pipeline) :
        BaseDemuxElement(((order == HAILO_FORMAT_ORDER_I420) ? NUMBER_OF_PLANES_I420 : NUMBER_OF_PLANES_NV12_NV21),
            name, timeout, std::move(duration_collector), std::move(pipeline_status),
            PipelineDirection::PUSH, async_pipeline),
        m_order(order)
{}

Expected<std::shared_ptr<PixBufferElement>> PixBufferElement::create(const std::string &name,
    std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, hailo_format_order_t order,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto pix_buffer_splitter_elem_ptr = make_shared_nothrow<PixBufferElement>(name, timeout,
        std::move(duration_collector), std::move(pipeline_status), order, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != pix_buffer_splitter_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return pix_buffer_splitter_elem_ptr;
}

Expected<std::vector<PipelineBuffer>> PixBufferElement::action(PipelineBuffer &&input)
{
    // splits the planes into buffers
    m_duration_collector.start_measurement();
    std::vector<PipelineBuffer> outputs;

    auto input_pix_buffer_expected = input.as_hailo_pix_buffer(m_order);

    if (!input_pix_buffer_expected) {
        input.set_action_status(input_pix_buffer_expected.status());
    }
    CHECK_EXPECTED(input_pix_buffer_expected);
    auto input_pix_buffer = input_pix_buffer_expected.release();

    if (PipelineBuffer::Type::FLUSH == input.get_type()) {
        for (uint32_t i = 0; i < input_pix_buffer.number_of_planes; i++) {
            outputs.emplace_back(PipelineBuffer(PipelineBuffer::Type::FLUSH));
        }
    } else {
        auto shared_input_buff = make_shared_nothrow<PipelineBuffer>(std::move(input));
        if (!shared_input_buff) {
            handle_non_recoverable_async_error(HAILO_OUT_OF_HOST_MEMORY);
        }
        CHECK_NOT_NULL_AS_EXPECTED(shared_input_buff, HAILO_OUT_OF_HOST_MEMORY);

        for (uint32_t i = 0; i < input_pix_buffer.number_of_planes; i++) {
            if (HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == input_pix_buffer.memory_type) {
                outputs.emplace_back(MemoryView(input_pix_buffer.planes[i].user_ptr, input_pix_buffer.planes[i].bytes_used),
                    [input_ptr = shared_input_buff](hailo_status status)
                    {
                        if (HAILO_SUCCESS != status) {
                            input_ptr->set_action_status(status);
                        }
                    });
            } else if (HAILO_PIX_BUFFER_MEMORY_TYPE_DMABUF == input_pix_buffer.memory_type) {
                hailo_dma_buffer_t dma_buffer;
                dma_buffer.fd = input_pix_buffer.planes[i].fd;
                dma_buffer.size = input_pix_buffer.planes[i].plane_size;
                TransferDoneCallbackAsyncInfer exec_done = [input_ptr = shared_input_buff](hailo_status status) {
                    if (HAILO_SUCCESS != status) {
                        input_ptr->set_action_status(status);
                    }};
                hailo_status action_status = HAILO_SUCCESS;
                PipelineBufferPoolPtr pool = m_sources[i].next()->get_buffer_pool();
                outputs.emplace_back(PipelineBuffer(dma_buffer, exec_done, action_status, pool->is_holding_user_buffers(), pool));
            } else {
                return make_unexpected(HAILO_INVALID_ARGUMENT);
            }
        }
    }

    m_duration_collector.complete_measurement();
    return outputs;
}

Expected<std::shared_ptr<AsyncHwElement>> AsyncHwElement::create(const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos,
    std::chrono::milliseconds timeout, hailo_pipeline_elem_stats_flags_t elem_flags, const std::string &name,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::shared_ptr<ConfiguredNetworkGroup> net_group,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    TRY(auto queue_size, net_group->infer_queue_size());

    auto status = HAILO_UNINITIALIZED;
    auto elem_ptr = make_shared_nothrow<AsyncHwElement>(named_stream_infos, timeout, name,
        duration_collector.release(), std::move(pipeline_status), pipeline_direction, async_pipeline, net_group,
        queue_size, status);
    CHECK_AS_EXPECTED(nullptr != elem_ptr, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    LOGGER__INFO("Created {}", elem_ptr->description());

    return elem_ptr;
}

AsyncHwElement::AsyncHwElement(const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos, std::chrono::milliseconds timeout,
    const std::string &name, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline, std::shared_ptr<ConfiguredNetworkGroup> net_group,
    const size_t max_ongoing_transfers, hailo_status &status) :
        PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
        m_timeout(timeout),
        m_net_group(net_group),
        m_max_ongoing_transfers(max_ongoing_transfers)
{
    uint32_t sinks_count = 0;
    uint32_t sources_count = 0;
    for (const auto &stream_info_pair : named_stream_infos) {
        if (HAILO_D2H_STREAM == stream_info_pair.second.direction) {
            m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
            const auto &source_name = m_sources[sources_count++].name();
            m_source_name_to_stream_name[source_name] = stream_info_pair.first;

            m_source_name_to_index[source_name] = static_cast<uint32_t>(m_sources.size() - 1);
        } else {
            m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
            const auto &sink_name = m_sinks[sinks_count++].name();
            m_sink_name_to_stream_name[sink_name] = stream_info_pair.first;
            m_sink_name_to_index[sink_name] = static_cast<uint32_t>(m_sinks.size() - 1);
        }
    }
    m_barrier = make_shared_nothrow<Barrier>(sinks_count);
    if (nullptr == m_barrier) {
        status = HAILO_OUT_OF_HOST_MEMORY;
        return;
    }
    status = HAILO_SUCCESS;
}

// This func overrides the regular dataflow of this element and calls all next elements run_push_async directly
// (normally, the run_push_async of the next elements will be called by the LL async read_done)
void AsyncHwElement::handle_error_in_hw_async_elem(hailo_status error_status)
{
    for (auto &name_output_stream_pair : m_source_name_to_index) {
        auto source_index = name_output_stream_pair.second;
        assert(source_index < m_sources.size());

        auto pool = m_sources[source_index].next()->get_buffer_pool();
        assert(pool);

        auto expected_buffer = pool->acquire_buffer(m_timeout);
        if (HAILO_SUCCESS == expected_buffer.status()) {
            expected_buffer->set_action_status(error_status);
            m_sources[source_index].next()->run_push_async(expected_buffer.release());
        } else {
            m_sources[source_index].next()->run_push_async(PipelineBuffer(error_status));
        }
    }

    return;
}

void AsyncHwElement::action()
{
    // Assuming m_input_buffers is full (has a valid buffer for all sinks)
    for (auto &input_buffer : m_input_buffers) {
        if (HAILO_SUCCESS != input_buffer.second.action_status()) {
            handle_error_in_hw_async_elem(input_buffer.second.action_status());
            m_input_buffers.clear();
            return;
        }
    }

    // TODO: HRT-13324 Change to be map of <std::string, PipelineBuffer>
    std::unordered_map<std::string, std::shared_ptr<PipelineBuffer>> source_name_to_output_buffer;
    for (auto &name_to_index_pair : m_source_name_to_index) {
        auto pool = m_sources[name_to_index_pair.second].next()->get_buffer_pool();
        assert(pool);

        auto expected_buffer = pool->acquire_buffer(m_timeout);
        if (HAILO_SUCCESS != expected_buffer.status()) {
            handle_non_recoverable_async_error(expected_buffer.status());
            m_input_buffers.clear();
            m_barrier->terminate();
            return;
        }
        source_name_to_output_buffer[name_to_index_pair.first] = make_shared_nothrow<PipelineBuffer>(expected_buffer.release());
    }

    NamedBuffersCallbacks named_buffers_callbacks;

    for (auto &input_buffer : m_input_buffers) {
        const auto &stream_name = m_sink_name_to_stream_name.at(input_buffer.first);
        // std::function requires its lambda to be copyable, so using shared_ptr<PipelineBuffer>
        auto buffer_shared = make_shared_nothrow<PipelineBuffer>(std::move(input_buffer.second));
        if (nullptr == buffer_shared) {
            handle_non_recoverable_async_error(HAILO_OUT_OF_HOST_MEMORY);
            m_input_buffers.clear();
            m_barrier->terminate();
            return;
        }

        BufferRepresentation buffer_representation{};
        if (buffer_shared->get_buffer_type() == BufferType::VIEW) {
            auto src = buffer_shared->as_view(BufferProtection::READ);
            if (HAILO_SUCCESS != src.status()) {
                handle_non_recoverable_async_error(src.status());
                m_input_buffers.clear();
                m_barrier->terminate();
                return;
            }
            buffer_representation.buffer_type = BufferType::VIEW;
            buffer_representation.view = src.value();
        } else if (buffer_shared->get_buffer_type() == BufferType::DMA_BUFFER) {
            auto dma_buffer = buffer_shared->get_metadata().get_additional_data<DmaBufferPipelineData>();
            buffer_representation.buffer_type = BufferType::DMA_BUFFER;
            buffer_representation.dma_buffer = dma_buffer->m_dma_buffer;
        } else {
            handle_non_recoverable_async_error(HAILO_INVALID_ARGUMENT);
            m_input_buffers.clear();
            m_barrier->terminate();
            return;
        }

        named_buffers_callbacks.emplace(stream_name, std::make_pair(buffer_representation,
            [buffer_shared](hailo_status status) { buffer_shared->set_action_status(status); }));
    }

    for (auto &output_buffer : source_name_to_output_buffer) {
        const auto &stream_name = m_source_name_to_stream_name.at(output_buffer.first);

        BufferRepresentation buffer_representation{};
        if (output_buffer.second->get_buffer_type() == BufferType::VIEW) {
            auto dst = output_buffer.second->as_view(BufferProtection::WRITE);
            if (HAILO_SUCCESS != dst.status()) {
                handle_non_recoverable_async_error(dst.status());
                m_input_buffers.clear();
                m_barrier->terminate();
                return;
            }
            buffer_representation.buffer_type = BufferType::VIEW;
            buffer_representation.view = dst.value();
        } else if (output_buffer.second->get_buffer_type() == BufferType::DMA_BUFFER) {
            auto dma_buffer = output_buffer.second->get_metadata().get_additional_data<DmaBufferPipelineData>();
            buffer_representation.buffer_type = BufferType::DMA_BUFFER;
            buffer_representation.dma_buffer = dma_buffer->m_dma_buffer;
        } else {
            handle_non_recoverable_async_error(HAILO_INVALID_ARGUMENT);
            m_input_buffers.clear();
            m_barrier->terminate();
            return;
        }

        named_buffers_callbacks.emplace(stream_name, std::make_pair(buffer_representation,
            [this, buffer = output_buffer.second, source_name = output_buffer.first](hailo_status status){
                buffer->set_action_status(status);
                if (HAILO_SUCCESS == m_pipeline_status->load()) {
                    assert(contains(m_source_name_to_index, source_name));
                    // If pipeline_status is not success, someone already handled this error and no reason for this buffer to be pushed
                    assert(contains(m_source_name_to_index, source_name));
                    TRACE(RunPushAsyncStartTrace, m_sources[m_source_name_to_index[source_name]].next()->name(),
                        m_sources[m_source_name_to_index[source_name]].next()->pipeline_unique_id(), network_name());
                    m_sources[m_source_name_to_index[source_name]].next()->run_push_async(std::move(*buffer));
                    TRACE(RunPushAsyncEndTrace, m_sources[m_source_name_to_index[source_name]].next()->name(),
                        m_sources[m_source_name_to_index[source_name]].next()->pipeline_unique_id(), network_name());
                }
        }));
    }

    auto cng = m_net_group.lock();
    if (!cng) {
        // Error - cng (VDevice) was released mid inference
        handle_non_recoverable_async_error(HAILO_INTERNAL_FAILURE);
        m_input_buffers.clear();
        m_barrier->terminate();
        return;
    }

    auto status = cng->wait_for_ongoing_callbacks_count_under(m_max_ongoing_transfers);
    if (HAILO_SUCCESS != status ) {
        handle_non_recoverable_async_error(status);
        m_input_buffers.clear();
        m_barrier->terminate();
        return;
    }

    status = cng->infer_async(named_buffers_callbacks, [](hailo_status){});
    if (HAILO_SUCCESS != status ) {
        handle_non_recoverable_async_error(status);
        m_input_buffers.clear();
        m_barrier->terminate();
        return;
    }

    m_input_buffers.clear();
}

void AsyncHwElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink)
{
    TRACE(RunPushAsyncStartTrace, name(), pipeline_unique_id(), network_name());
    assert(contains(m_sink_name_to_index, sink.name()));

    m_barrier->arrive_and_wait();
    std::unique_lock<std::mutex> lock(m_mutex);
    if (HAILO_SUCCESS == m_pipeline_status->load()) {
        m_input_buffers[sink.name()] = std::move(buffer);
        if (m_input_buffers.size() == m_sink_name_to_index.size()) { // Last sink to set its buffer
            action();
        }
    } else {
        m_input_buffers.clear();
    }
    TRACE(RunPushAsyncEndTrace, name(), pipeline_unique_id(), network_name());
}

hailo_status AsyncHwElement::run_push(PipelineBuffer &&/*optional*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

Expected<uint32_t> AsyncHwElement::get_source_index_from_output_stream_name(const std::string &output_stream_name)
{
    for (const auto &name_pair : m_source_name_to_stream_name) {
        if (name_pair.second == output_stream_name) {
            assert(contains(m_source_name_to_index, name_pair.first));
            uint32_t ret_val = m_source_name_to_index.at(name_pair.first);
            return ret_val;
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

Expected<uint32_t> AsyncHwElement::get_source_index_from_source_name(const std::string &source_name)
{
    CHECK_AS_EXPECTED(contains(m_source_name_to_index, source_name), HAILO_NOT_FOUND, "couldnt find src '{}'", source_name);
    auto ret_val = m_source_name_to_index.at(source_name);
    return ret_val;
}

Expected<uint8_t> AsyncHwElement::get_sink_index_from_input_stream_name(const std::string &input_stream_name)
{
    for (const auto &name_pair : m_sink_name_to_stream_name) {
        if (name_pair.second == input_stream_name) {
            auto cpy = static_cast<uint8_t>(m_sink_name_to_index.at(name_pair.first));
            return cpy;
        }
    }
    return make_unexpected(HAILO_INVALID_ARGUMENT);
}

Expected<PipelineBuffer> AsyncHwElement::run_pull(PipelineBuffer &&/*optional*/, const PipelinePad &/*source*/)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

std::vector<PipelinePad*> AsyncHwElement::execution_pads()
{
    std::vector<PipelinePad*> result;
    result.reserve(m_sources.size());
    for (auto& pad : m_sources) {
        result.push_back(pad.next());
    }
    return result;
}

hailo_status AsyncHwElement::execute_terminate(hailo_status error_status)
{
    if (m_is_terminated) {
        return HAILO_SUCCESS;
    }

    m_barrier->terminate();

    // Best effort - if cng is already released - nothing to shut-down
    auto shutdown_status = HAILO_SUCCESS;
    auto cng = m_net_group.lock();
    if (cng) {
        shutdown_status = cng->shutdown();
    }

    // Checking success of shutdown is best effort (terminate should be called even if shutdown fails)
    auto terminate_status = PipelineElement::execute_terminate(error_status);
    CHECK_SUCCESS(shutdown_status);
    CHECK_SUCCESS(terminate_status);

    return HAILO_SUCCESS;
}

std::vector<std::shared_ptr<PipelineBufferPool>> AsyncHwElement::get_hw_interacted_buffer_pools_h2d()
{
    std::vector<std::shared_ptr<PipelineBufferPool>> res;
    for (auto &sink : m_sinks) {
        res.push_back(sink.prev()->get_buffer_pool());
    }
    return res;
}

std::vector<std::shared_ptr<PipelineBufferPool>> AsyncHwElement::get_hw_interacted_buffer_pools_d2h()
{
    std::vector<std::shared_ptr<PipelineBufferPool>> res;
    for (auto &source : m_sources) {
        auto pools = source.get_buffer_pool();
        res.push_back(source.next()->get_buffer_pool());
    }
    return res;
}

} /* namespace hailort */
