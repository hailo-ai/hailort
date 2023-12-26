/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline_internal.cpp
 * @brief Implemention of the pipeline elements
 **/
#include "net_flow/pipeline/pipeline_internal.hpp"
#include "net_flow/pipeline/async_infer_runner.hpp"
#include "common/os_utils.hpp"
#include "common/runtime_statistics_internal.hpp"

namespace hailort
{

PipelineElementInternal::PipelineElementInternal(const std::string &name, DurationCollector &&duration_collector,
                                 std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                 PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction),
    m_async_pipeline(async_pipeline)
{}

void PipelineElementInternal::handle_non_recoverable_async_error(hailo_status error_status)
{
    hailo_status pipeline_status = m_pipeline_status->load();
    if ((HAILO_SUCCESS == pipeline_status) && (error_status != HAILO_SHUTDOWN_EVENT_SIGNALED)) {
        LOGGER__ERROR("Non-recoverable Async Infer Pipeline error. status error code: {}", error_status);
        m_is_terminating_element = true;
        if (auto async_pipeline = m_async_pipeline.lock()) {
            async_pipeline->shutdown(error_status);
        }
    }
}

SourceElement::SourceElement(const std::string &name, DurationCollector &&duration_collector,
                             std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                             PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline)
{
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
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

SinkElement::SinkElement(const std::string &name, DurationCollector &&duration_collector,
                         std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                         PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline)
{
    m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
}

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

hailo_status AsyncPushQueueElement::execute_dequeue_user_buffers(hailo_status error_status)
{
    auto status = m_queue.clear();
    CHECK_SUCCESS(PipelineElement::execute_dequeue_user_buffers(error_status));
    return status;
}

IntermediateElement::IntermediateElement(const std::string &name, DurationCollector &&duration_collector,
                                         std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                         PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline)
{
    m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
}

std::vector<PipelinePad*> IntermediateElement::execution_pads()
{
    std::vector<PipelinePad*> result{&next_pad()};
    return result;
}

FilterElement::FilterElement(const std::string &name, DurationCollector &&duration_collector,
                             std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                             PipelineDirection pipeline_direction, BufferPoolPtr buffer_pool,
                             std::chrono::milliseconds timeout, std::shared_ptr<AsyncPipeline> async_pipeline) :
    IntermediateElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
    m_pool(buffer_pool),
    m_timeout(timeout)
{}

hailo_status FilterElement::run_push(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    auto output = action(std::move(buffer), PipelineBuffer());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == output.status()) {
        return output.status();
    }
    CHECK_EXPECTED_AS_STATUS(output);

    hailo_status status = next_pad().run_push(output.release());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("run_push of {} was shutdown!", name());
        return status;
    }
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("run_push of {} was aborted!", name());
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

void FilterElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    assert(m_pipeline_direction == PipelineDirection::PUSH);
    if (HAILO_SUCCESS != buffer.action_status()) {
        auto buffer_from_pool = m_pool->get_available_buffer(PipelineBuffer(), m_timeout);
        if (HAILO_SUCCESS != buffer_from_pool.status()) {
            handle_non_recoverable_async_error(buffer_from_pool.status());
        } else {
            buffer_from_pool->set_action_status(buffer.action_status());

            auto exec_done_cb = buffer.get_exec_done_cb();
            exec_done_cb(buffer.action_status());

            next_pad().run_push_async(buffer_from_pool.release());
        }
        return;
    }

    auto output = action(std::move(buffer), PipelineBuffer());
    if (HAILO_SUCCESS == output.status()) {
        next_pad().run_push_async(output.release());
    } else {
        next_pad().run_push_async(PipelineBuffer(output.status()));
    }
    return;
}

Expected<PipelineBuffer> FilterElement::run_pull(PipelineBuffer &&optional, const PipelinePad &/*source*/)
{
    auto buffer = next_pad().run_pull();
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        LOGGER__INFO("run_pull in FilterElement was shutdown!");
        return make_unexpected(buffer.status());
    }
    CHECK_EXPECTED(buffer);
    return action(buffer.release(), std::move(optional));
}

std::vector<AccumulatorPtr> FilterElement::get_queue_size_accumulators()
{
    if (nullptr == m_pool || nullptr == m_pool->get_queue_size_accumulator()) {
        return std::vector<AccumulatorPtr>();
    }
    return {m_pool->get_queue_size_accumulator()};
}

hailo_status FilterElement::enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name)
{
    (void)source_name;

    auto status = m_pool->enqueue_buffer(mem_view, exec_done);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status FilterElement::execute_dequeue_user_buffers(hailo_status error_status)
{
    auto status = empty_buffer_pool(m_pool, error_status, m_timeout);
    CHECK_SUCCESS(status);
    return PipelineElement::execute_dequeue_user_buffers(error_status);
}

Expected<bool> FilterElement::can_push_buffer_upstream(const uint32_t /*source_index*/)
{
    return !m_pool->is_full();
}

hailo_status FilterElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t /*source_index*/)
{
    auto status = m_pool->allocate_buffers(is_dma_able, num_of_buffers);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<bool> FilterElement::can_push_buffer_upstream(const std::string &source_name)
{
    auto source_index = get_source_index_from_source_name(source_name);
    CHECK_EXPECTED(source_index);
    return can_push_buffer_upstream(*source_index);
}

hailo_status FilterElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &source_name)
{
    auto source_index = get_source_index_from_source_name(source_name);
    CHECK_EXPECTED_AS_STATUS(source_index);
    return fill_buffer_pool(is_dma_able, num_of_buffers, *source_index);
}

Expected<SpscQueue<PipelineBuffer>> BaseQueueElement::create_queue(size_t queue_size, EventPtr shutdown_event)
{
    auto queue = SpscQueue<PipelineBuffer>::create(queue_size, shutdown_event);
    CHECK_EXPECTED(queue);

    return queue.release();
}

BaseQueueElement::BaseQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
                                   std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
                                   AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   Event &&activation_event, Event &&deactivation_event, PipelineDirection pipeline_direction,
                                   std::shared_ptr<AsyncPipeline> async_pipeline) :
    IntermediateElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
    m_queue(std::move(queue)),
    m_shutdown_event(shutdown_event),
    m_timeout(timeout),
    m_is_thread_running(true),
    m_activation_event(std::move(activation_event)),
    m_deactivation_event(std::move(deactivation_event)),
    m_queue_size_accumulator(std::move(queue_size_accumulator)),
    m_is_run_in_thread_running(false)
{}

BaseQueueElement::~BaseQueueElement()
{
    LOGGER__INFO("Queue element {} has {} frames in his Queue on destruction", name(), m_queue.size_approx());
}

void BaseQueueElement::start_thread()
{
    m_thread = std::thread([this] () {
        OsUtils::set_current_thread_name(thread_name());
        while (m_is_thread_running.load()) {
            auto status = m_activation_event.wait(INIFINITE_TIMEOUT());

            if (!m_is_thread_running) {
                LOGGER__INFO("Thread in element {} is not running anymore, exiting..", this->name());
                break;
            }
            if (HAILO_SUCCESS == status) {
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_is_run_in_thread_running = true;
                }
                m_cv.notify_all();

                status = run_in_thread();

                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_is_run_in_thread_running = false;
                }
                m_cv.notify_all();
            }

            if (HAILO_SUCCESS != status) {
                if (HAILO_SHUTDOWN_EVENT_SIGNALED != status) {
                    // We do not want to log error for HAILO_STREAM_ABORTED_BY_USER
                    if (HAILO_STREAM_ABORTED_BY_USER != status) {
                        LOGGER__ERROR("Queue element {} run in thread function failed! status = {}", this->name(), status);
                    }

                    // Store the real error in pipeline_status
                    m_pipeline_status->store(status);

                    // Signal other threads to stop
                    hailo_status shutdown_status = m_shutdown_event->signal();
                    if (HAILO_SUCCESS != shutdown_status) {
                        LOGGER__CRITICAL("Failed shutting down queue with status {}", shutdown_status);
                    }
                }
                //Thread has done its execution. Mark to the thread to wait for activation again
                hailo_status event_status = m_activation_event.reset();
                if (HAILO_SUCCESS != event_status) {
                    LOGGER__CRITICAL("Failed reset activation event of element {}, with status {}", this->name(), event_status);
                }

                // Mark to deactivation function that the thread is done
                event_status = m_deactivation_event.signal();
                if (HAILO_SUCCESS != event_status) {
                    LOGGER__CRITICAL("Failed signaling deactivation event of element {}, with status {}", this->name(), event_status);
                }
            }
        }
    });
}

void BaseQueueElement::stop_thread()
{
    m_shutdown_event->signal();

    // Mark thread as not running, then wake it in case it is waiting on m_activation_event
    m_is_thread_running = false;
    m_activation_event.signal();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

std::vector<AccumulatorPtr> BaseQueueElement::get_queue_size_accumulators()
{
    if (nullptr == m_queue_size_accumulator) {
        return std::vector<AccumulatorPtr>();
    }
    return {m_queue_size_accumulator};
}

hailo_status BaseQueueElement::execute_activate()
{
    hailo_status status = PipelineElementInternal::execute_activate();
    CHECK_SUCCESS(status);

    status = m_activation_event.signal();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status BaseQueueElement::execute_post_deactivate(bool should_clear_abort)
{
    hailo_status status = m_deactivation_event.wait(INIFINITE_TIMEOUT());
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to post_deactivate() in {} with status {}", name(), status);
    }

    status = m_deactivation_event.reset();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to reset of deactivation event in {} with status {}", name(), status);
    }

    return PipelineElementInternal::execute_post_deactivate(should_clear_abort);
}

hailo_status BaseQueueElement::execute_clear()
{
    auto status = PipelineElementInternal::execute_clear();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to clear() in {} with status {}", name(), status);
    }

    auto queue_status = m_queue.clear();
    CHECK_SUCCESS(queue_status, "Failed to clear() queue in {} with status {}", name(), status);

    return status;
}

hailo_status BaseQueueElement::execute_wait_for_finish()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this] () {
        return !m_is_run_in_thread_running;
    });
    return HAILO_SUCCESS;
}

hailo_status BaseQueueElement::enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name)
{
    (void)source_name;
    return m_sinks[0].prev()->element().enqueue_execution_buffer(mem_view, exec_done, m_sinks[0].prev()->name());
}

Expected<bool> BaseQueueElement::can_push_buffer_upstream(const uint32_t source_index)
{
    return m_sinks[0].prev()->element().can_push_buffer_upstream(source_index);
}

Expected<bool> BaseQueueElement::can_push_buffer_downstream(const uint32_t /*source_index*/)
{
    return !m_queue.is_queue_full();
}

hailo_status BaseQueueElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t source_index)
{
    return m_sinks[0].prev()->element().fill_buffer_pool(is_dma_able, num_of_buffers, source_index);
}

Expected<bool> BaseQueueElement::can_push_buffer_upstream(const std::string &source_name)
{
    return m_sinks[0].prev()->element().can_push_buffer_upstream(source_name);
}

Expected<bool> BaseQueueElement::can_push_buffer_downstream(const std::string &/*source_name*/)
{
    return !m_queue.is_queue_full();
}

hailo_status BaseQueueElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &source_name)
{
    return m_sinks[0].prev()->element().fill_buffer_pool(is_dma_able, num_of_buffers, source_name);
}

hailo_status PushQueueElement::execute_abort()
{
    auto status = m_shutdown_event->reset();
    CHECK_SUCCESS(status);

    m_pipeline_status->store(HAILO_STREAM_ABORTED_BY_USER);

    status = PipelineElementInternal::execute_abort();
    CHECK_SUCCESS(status);

    status = m_activation_event.signal();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status BaseQueueElement::execute_clear_abort()
{
    auto status = m_shutdown_event->reset();
    CHECK_SUCCESS(status);

    m_pipeline_status->store(HAILO_SUCCESS);
    return PipelineElementInternal::execute_clear_abort();
}

hailo_status BaseQueueElement::set_timeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
    return HAILO_SUCCESS;
}

std::string BaseQueueElement::description() const
{
    std::stringstream element_description;

    element_description << "(" << this->name();
    if (HAILO_INFINITE != this->m_timeout.count()) {
        element_description << " | timeout: "  << std::chrono::duration_cast<std::chrono::seconds>(this->m_timeout).count() << "s";
    }
    element_description << ")";

    return element_description.str();
}

hailo_status BaseQueueElement::pipeline_status()
{
    auto status = m_pipeline_status->load();

    // We treat HAILO_STREAM_ABORTED_BY_USER as success because it is caused by user action (aborting streams)
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        return HAILO_SUCCESS;
    }
    return status;
}

Expected<std::shared_ptr<PushQueueElement>> PushQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction,
        std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto queue = BaseQueueElement::create_queue(queue_size, shutdown_event);
    CHECK_EXPECTED(queue);

    auto activation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(activation_event);

    auto deactivation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(deactivation_event);

    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    auto duration_collector = DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE);
    CHECK_EXPECTED(duration_collector);

    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto queue_ptr = make_shared_nothrow<PushQueueElement>(queue.release(), shutdown_event, name, timeout,
        duration_collector.release(), std::move(queue_size_accumulator), std::move(pipeline_status),
        activation_event.release(), deactivation_event.release(), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PushQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->name());

    return queue_ptr;
}

Expected<std::shared_ptr<PushQueueElement>> PushQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction,
        std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PushQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms), vstream_params.queue_size,
    vstream_params.pipeline_elements_stats_flags, shutdown_event, pipeline_status, pipeline_direction, async_pipeline);
}

PushQueueElement::PushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
                                   std::chrono::milliseconds timeout, DurationCollector &&duration_collector, 
                                   AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   Event &&activation_event, Event &&deactivation_event, PipelineDirection pipeline_direction,
                                   std::shared_ptr<AsyncPipeline> async_pipeline, bool should_start_thread) :
    BaseQueueElement(std::move(queue), shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
                     std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event), pipeline_direction, async_pipeline)
{
    if (should_start_thread) {
        start_thread();
    }
}

PushQueueElement::~PushQueueElement()
{
    stop_thread();
}

hailo_status PushQueueElement::run_push(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_queue.size_approx()));
    }
    auto status = m_pipeline_status->load();
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("run_push of {} was aborted!", name());
        return status;
    }
    CHECK_SUCCESS(m_pipeline_status->load());
    status = m_queue.enqueue(std::move(buffer), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        auto queue_thread_status = pipeline_status();
        CHECK_SUCCESS(queue_thread_status,
            "Shutdown event was signaled in enqueue of queue element {} because thread has failed with status={}!", name(),
            queue_thread_status);
        LOGGER__INFO("Shutdown event was signaled in enqueue of queue element {}!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

void PushQueueElement::run_push_async(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/) {
    LOGGER__ERROR("run_push_async is not supported for {}", name());
    assert(false);
}

Expected<PipelineBuffer> PushQueueElement::run_pull(PipelineBuffer &&/*optional*/, const PipelinePad &/*source*/)
{
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status PushQueueElement::execute_deactivate()
{
    // Mark to the threads that deactivate() was called.
    hailo_status status = m_queue.enqueue(PipelineBuffer(PipelineBuffer::Type::DEACTIVATE));
    if (HAILO_SUCCESS != status) {
        // We want to deactivate source even if enqueue failed
        auto deactivation_status = PipelineElementInternal::execute_deactivate();
        CHECK_SUCCESS(deactivation_status);
        if ((HAILO_STREAM_ABORTED_BY_USER == status) || (HAILO_SHUTDOWN_EVENT_SIGNALED == status)) {
            LOGGER__INFO("enqueue() in element {} was aborted, got status = {}", name(), status);
        }
        else {
             LOGGER__ERROR("enqueue() in element {} failed, got status = {}", name(), status);
             return status;
        }
    }

    return HAILO_SUCCESS;
}

PipelinePad &PushQueueElement::next_pad()
{
    // Note: The next elem to be run is downstream from this elem (i.e. buffers are pushed)
    return *m_sources[0].next();
}

hailo_status PushQueueElement::run_in_thread()
{
    auto buffer = m_queue.dequeue(INIFINITE_TIMEOUT());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        LOGGER__INFO("Shutdown event was signaled in dequeue of queue element {}!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    CHECK_EXPECTED_AS_STATUS(buffer);

    // Return if deactivated
    if (PipelineBuffer::Type::DEACTIVATE == buffer->get_type()) {
        hailo_status status = m_shutdown_event->signal();
        CHECK_SUCCESS(status);

        status = next_pad().deactivate();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Deactivate of source in {} has failed with status {}", name(), status);
        }

        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }

    hailo_status status = next_pad().run_push(buffer.release());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("run_push of {} was aborted!", name());
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<AsyncPushQueueElement>> AsyncPushQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::shared_ptr<AsyncPipeline> async_pipeline, PipelineDirection pipeline_direction)
{
    auto queue = BaseQueueElement::create_queue(queue_size, shutdown_event);
    CHECK_EXPECTED(queue);

    auto activation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(activation_event);

    auto deactivation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(deactivation_event);

    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    auto duration_collector = DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE);
    CHECK_EXPECTED(duration_collector);

    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto queue_ptr = make_shared_nothrow<AsyncPushQueueElement>(queue.release(), shutdown_event, name, timeout,
        duration_collector.release(), std::move(queue_size_accumulator), std::move(pipeline_status),
        activation_event.release(), deactivation_event.release(), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PushQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->name());

    return queue_ptr;
}

Expected<std::shared_ptr<AsyncPushQueueElement>> AsyncPushQueueElement::create(const std::string &name, const ElementBuildParams &build_params,
    std::shared_ptr<AsyncPipeline> async_pipeline, PipelineDirection pipeline_direction)
{
    return AsyncPushQueueElement::create(name, build_params.timeout, build_params.buffer_pool_size_edges,
            build_params.elem_stats_flags, build_params.shutdown_event, build_params.pipeline_status, async_pipeline, pipeline_direction);
}

AsyncPushQueueElement::AsyncPushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
                                   std::chrono::milliseconds timeout, DurationCollector &&duration_collector, 
                                   AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   Event &&activation_event, Event &&deactivation_event, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PushQueueElement(std::move(queue), shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
                     std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event), pipeline_direction, async_pipeline, false)
{
    start_thread();
}

void AsyncPushQueueElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_queue.size_approx()));
    }

    auto status = m_queue.enqueue(std::move(buffer), m_timeout);
    if (HAILO_SUCCESS != status && HAILO_SHUTDOWN_EVENT_SIGNALED != status) {
        handle_non_recoverable_async_error(status);
        stop_thread();
    }
}

void AsyncPushQueueElement::start_thread()
{
    m_thread = std::thread([this] () {
        OsUtils::set_current_thread_name(thread_name());
        while (m_is_thread_running.load()) {
            auto status = m_pipeline_status->load();
            if (HAILO_SUCCESS != status) {
                LOGGER__INFO("Thread in element {} is not running anymore, exiting..", name());
                m_is_thread_running = false;
                break;
            }

            status = run_in_thread();
            if (HAILO_SUCCESS != status) {
                handle_non_recoverable_async_error(status);
                m_is_thread_running = false;
                break;
            }
        }
    });
}

hailo_status AsyncPushQueueElement::run_push(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

hailo_status AsyncPushQueueElement::run_in_thread()
{
    auto buffer = m_queue.dequeue(INIFINITE_TIMEOUT());
    auto buffer_status = buffer.status();
    switch (buffer_status) {
    case HAILO_SHUTDOWN_EVENT_SIGNALED:
        break;

    case HAILO_SUCCESS:
        // Return if deactivated
        if (PipelineBuffer::Type::DEACTIVATE == buffer->get_type()) {
            hailo_status status = m_shutdown_event->signal();
            CHECK_SUCCESS(status);

            status = next_pad().deactivate();
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Deactivate of source in {} has failed with status {}", name(), status);
            }

            return HAILO_SHUTDOWN_EVENT_SIGNALED;
        }

        next_pad().run_push_async(buffer.release());
        break;

    default:
        next_pad().run_push_async(PipelineBuffer(buffer_status));
    }

    return buffer_status;
}

hailo_status AsyncPushQueueElement::execute_deactivate()
{
    // Mark to the threads that deactivate() was called.
    hailo_status status = m_queue.enqueue(PipelineBuffer(PipelineBuffer::Type::DEACTIVATE));
    if (HAILO_SUCCESS != status) {
        // We want to deactivate source even if enqueue failed
        auto deactivation_status = PipelineElementInternal::execute_deactivate();
        CHECK_SUCCESS(deactivation_status);
        if ((HAILO_STREAM_ABORTED_BY_USER == status) || (HAILO_SHUTDOWN_EVENT_SIGNALED == status)) {
            LOGGER__INFO("enqueue() in element {} was aborted, got status = {}", name(), status);
        } else {
             LOGGER__ERROR("enqueue() in element {} failed, got status = {}", name(), status);
             return status;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status AsyncPushQueueElement::execute_post_deactivate(bool should_clear_abort)
{
    // We marked thread to stop with PipelineBuffer::Type::DEACTIVATE, now we wait for it to finish
    stop_thread();
    return PipelineElementInternal::execute_post_deactivate(should_clear_abort);
}

hailo_status AsyncPushQueueElement::execute_terminate(hailo_status error_status)
{
    if (m_is_terminated) {
        return HAILO_SUCCESS;
    }

    auto terminate_status = PipelineElement::execute_terminate(error_status);

    if ((!next_pad().element().is_terminating_element())) {
        stop_thread();
    }

    CHECK_SUCCESS(terminate_status);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<PullQueueElement>> PullQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction)
{
    auto queue = BaseQueueElement::create_queue(queue_size, shutdown_event);
    CHECK_EXPECTED(queue);

    auto activation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(activation_event);

    auto deactivation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(deactivation_event);

    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    auto duration_collector = DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE);
    CHECK_EXPECTED(duration_collector);

    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto queue_ptr = make_shared_nothrow<PullQueueElement>(queue.release(), shutdown_event, name, timeout,
        duration_collector.release(), std::move(queue_size_accumulator), std::move(pipeline_status),
        activation_event.release(), deactivation_event.release(), pipeline_direction);
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PullQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->name());

    return queue_ptr;
}
Expected<std::shared_ptr<PullQueueElement>> PullQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction)
{
    return PullQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.queue_size, vstream_params.pipeline_elements_stats_flags, shutdown_event, pipeline_status, pipeline_direction);
}

PullQueueElement::PullQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
                                   std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
                                   AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   Event &&activation_event, Event &&deactivation_event, PipelineDirection pipeline_direction) :
    BaseQueueElement(std::move(queue), shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
                     std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event), pipeline_direction, nullptr)
{
    start_thread();
}

PullQueueElement::~PullQueueElement()
{
    stop_thread();
}

hailo_status PullQueueElement::run_push(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

void PullQueueElement::run_push_async(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    LOGGER__ERROR("run_push_async is not supported for {}", name());
    assert(false);
}

Expected<PipelineBuffer> PullQueueElement::run_pull(PipelineBuffer &&optional, const PipelinePad &/*sink*/)
{
    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    CHECK_AS_EXPECTED(!optional, HAILO_INVALID_ARGUMENT, "Optional buffer is not allowed in queue element!");

    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_queue.size_approx()));
    }
    auto output = m_queue.dequeue(m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == output.status()) {
        auto queue_thread_status = pipeline_status();
        CHECK_SUCCESS_AS_EXPECTED(queue_thread_status,
            "Shutdown event was signaled in dequeue of queue element {} because thread has failed with status={}!", name(),
            queue_thread_status);
        LOGGER__INFO("Shutdown event was signaled in dequeue of queue element {}!", name());
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }
    CHECK_EXPECTED(output);

    return output;
}

hailo_status PullQueueElement::execute_deactivate()
{
    hailo_status status = PipelineElementInternal::execute_deactivate();
    auto shutdown_event_status = m_shutdown_event->signal();
    CHECK_SUCCESS(status);
    CHECK_SUCCESS(shutdown_event_status);

    return HAILO_SUCCESS;
}

PipelinePad &PullQueueElement::next_pad()
{
    // Note: The next elem to be run is upstream from this elem (i.e. buffers are pulled)
    return *m_sinks[0].prev();
}

hailo_status PullQueueElement::run_in_thread()
{
    auto buffer = next_pad().run_pull();
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        LOGGER__INFO("Shutdown event was signaled in run_pull of queue element {}!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    if (HAILO_STREAM_ABORTED_BY_USER == buffer.status()) {
        LOGGER__INFO("run_pull of queue element {} was aborted!", name());
        return HAILO_STREAM_ABORTED_BY_USER;
    }
    if (HAILO_NETWORK_GROUP_NOT_ACTIVATED == buffer.status()) {
        LOGGER__INFO("run_pull of queue element {} was called before network_group is activated!", name());
        return HAILO_NETWORK_GROUP_NOT_ACTIVATED;
    }
    CHECK_EXPECTED_AS_STATUS(buffer);
    
    hailo_status status = m_queue.enqueue(buffer.release(), INIFINITE_TIMEOUT());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Shutdown event was signaled in enqueue of queue element {}!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<UserBufferQueueElement>> UserBufferQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
    hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    PipelineDirection pipeline_direction)
{
    auto pending_buffer_queue = BaseQueueElement::create_queue(1, shutdown_event);
    CHECK_EXPECTED(pending_buffer_queue);

    auto full_buffer_queue = BaseQueueElement::create_queue(1, shutdown_event);
    CHECK_EXPECTED(full_buffer_queue);

    auto activation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(activation_event);

    auto deactivation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(deactivation_event);

    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    auto duration_collector = DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE);
    CHECK_EXPECTED(duration_collector);

    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto queue_ptr = make_shared_nothrow<UserBufferQueueElement>(pending_buffer_queue.release(),
        full_buffer_queue.release(), shutdown_event, name, timeout, duration_collector.release(),
        std::move(queue_size_accumulator), std::move(pipeline_status), activation_event.release(),
        deactivation_event.release(), pipeline_direction);
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating UserBufferQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->name());

    return queue_ptr;
}

Expected<std::shared_ptr<UserBufferQueueElement>> UserBufferQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction)
{
    return UserBufferQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.pipeline_elements_stats_flags, shutdown_event, pipeline_status, pipeline_direction);
}

UserBufferQueueElement::UserBufferQueueElement(SpscQueue<PipelineBuffer> &&queue, SpscQueue<PipelineBuffer> &&full_buffer_queue,
                                               EventPtr shutdown_event, const std::string &name, std::chrono::milliseconds timeout,
                                               DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
                                               std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                               Event &&activation_event, Event &&deactivation_event,
                                               PipelineDirection pipeline_direction) :
    PullQueueElement(std::move(queue), shutdown_event, name, timeout, std::move(duration_collector),
                     std::move(queue_size_accumulator), std::move(pipeline_status), std::move(activation_event),
                     std::move(deactivation_event),
                     pipeline_direction),
    m_full_buffer_queue(std::move(full_buffer_queue))
{}

Expected<PipelineBuffer> UserBufferQueueElement::run_pull(PipelineBuffer &&optional, const PipelinePad &/*source*/)
{
    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    CHECK_AS_EXPECTED(optional, HAILO_INVALID_ARGUMENT, "Optional buffer must be valid in {}!", name());

    hailo_status status = m_queue.enqueue(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Shutdown event was signaled in enqueue of queue element {}!", name());
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_full_buffer_queue.size_approx()));
    }
    auto output = m_full_buffer_queue.dequeue(m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == output.status()) {
        LOGGER__INFO("Shutdown event was signaled in dequeue of queue element {}!", name());
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != output.status(), HAILO_TIMEOUT, "{} (D2H) failed with status={} (timeout={}ms)",
        name(), HAILO_TIMEOUT, m_timeout.count());
    CHECK_EXPECTED(output);

    CHECK_AS_EXPECTED(output->data() == optional.data(), HAILO_INTERNAL_FAILURE, "The buffer received in {} was not the same as the user buffer!", name());
    return output;
}

hailo_status UserBufferQueueElement::execute_clear()
{
    auto status = PipelineElementInternal::execute_clear();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to clear() in {} with status {}", name(), status);
    }

    auto queue_clear_status = m_full_buffer_queue.clear();
    if (HAILO_SUCCESS != queue_clear_status) {
        LOGGER__ERROR("Failed to clear() in {} with status {}", name(), queue_clear_status);
        status = queue_clear_status;
    }

    queue_clear_status = m_queue.clear();
    if (HAILO_SUCCESS != queue_clear_status) {
        LOGGER__ERROR("Failed to clear() in {} with status {}", name(), queue_clear_status);
        status = queue_clear_status;
    }

    return status;
}

hailo_status UserBufferQueueElement::run_in_thread()
{
    auto optional = m_queue.dequeue(INIFINITE_TIMEOUT());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == optional.status()) {
        LOGGER__INFO("Shutdown event was signaled in dequeue of {}!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    CHECK_EXPECTED_AS_STATUS(optional);

    auto buffer = next_pad().run_pull(optional.release());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        LOGGER__INFO("Shutdown event was signaled in run_pull of {}!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    if (HAILO_STREAM_ABORTED_BY_USER == buffer.status()) {
        LOGGER__INFO("run_pull of {} was aborted!", name());
        return HAILO_STREAM_ABORTED_BY_USER;
    }
    CHECK_EXPECTED_AS_STATUS(buffer);
    
    hailo_status status = m_full_buffer_queue.enqueue(buffer.release(), INIFINITE_TIMEOUT());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Shutdown event was signaled in enqueue of {}!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

BaseMuxElement::BaseMuxElement(size_t sink_count, const std::string &name, std::chrono::milliseconds timeout,
                               DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                               BufferPoolPtr buffer_pool, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
    m_timeout(timeout),
    m_pool(buffer_pool)
{
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
    m_sinks.reserve(sink_count);
    m_sink_has_arrived.reserve(sink_count);
    for (uint32_t i = 0; i < sink_count; ++i) {
        m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
        m_index_of_sink[m_sinks[i].name()] = i;
        m_sink_has_arrived[m_sinks[i].name()] = false;
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

    if (!m_is_terminating_element) {
        {
            // There is a case where the other thread is halted (via context switch) before the wait_for() function,
            // then we call notify_all() here, and then the wait_for() is called - resulting in a timeout.
            // notify_all() only works on threads which are already waiting, so that's why we acquire the lock here.
            std::unique_lock<std::mutex> lock(m_mutex);
        }
        m_cv.notify_all();
    }

    CHECK_SUCCESS(terminate_status);

    return HAILO_SUCCESS;
}


hailo_status BaseMuxElement::run_push(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

void BaseMuxElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink)
{
    assert(PipelineDirection::PUSH == m_pipeline_direction);
    assert(m_next_pads.size() == 1);

    std::unique_lock<std::mutex> lock(m_mutex);

    m_sink_has_arrived[sink.name()] = true;
    m_input_buffers[sink.name()] = std::move(buffer);
    if (has_all_sinks_arrived()) {
        hailo_status all_buffers_status = HAILO_SUCCESS;
        for (auto &input_buffer : m_input_buffers) {
            if (HAILO_SUCCESS != input_buffer.second.action_status()) {
                all_buffers_status = input_buffer.second.action_status();
                break;  // error from one buffer is enough
            }
        }

        if (HAILO_SUCCESS != all_buffers_status) {
            auto acquired_buffer = m_pool->get_available_buffer(PipelineBuffer(), m_timeout);
            if (HAILO_SUCCESS == acquired_buffer.status()) {
                acquired_buffer->set_action_status(all_buffers_status);

                auto exec_done_cb = m_input_buffers[sink.name()].get_exec_done_cb();
                exec_done_cb(m_input_buffers[sink.name()].action_status());

                m_next_pads[0]->run_push_async(acquired_buffer.release());
            } else {
                handle_non_recoverable_async_error(acquired_buffer.status());
            }
        } else {
            std::vector<PipelineBuffer> input_buffers;
            input_buffers.resize(m_input_buffers.size());
            for (auto &input_buffer : m_input_buffers) {
                input_buffers[m_index_of_sink[input_buffer.first]] = std::move(input_buffer.second);
            }

            auto output = action(std::move(input_buffers), PipelineBuffer());
            if (HAILO_SUCCESS == output.status()) {
                m_next_pads[0]->run_push_async(output.release());
            } else {
                m_next_pads[0]->run_push_async(PipelineBuffer(output.status()));
            }
        }

        for (const auto &curr_sink : m_sinks) {
            m_sink_has_arrived[curr_sink.name()] = false;
        }
        m_input_buffers.clear();

        // Manual unlocking is done before notifying, to avoid waking up the waiting thread only to block again
        lock.unlock();
        m_cv.notify_all();
    } else {
        auto done = m_cv.wait_for(lock, m_timeout, [&](){
            if (m_pipeline_status->load() != HAILO_SUCCESS) {
                return true; // so we can exit this flow
            }
            return !m_sink_has_arrived[sink.name()];
        });

        if (!done) {
            LOGGER__ERROR("Waiting for other threads in AsyncHwElement {} has reached a timeout (timeout={}ms)", name(), m_timeout.count());
            handle_non_recoverable_async_error(HAILO_TIMEOUT);
        }

        if (m_pipeline_status->load() == HAILO_STREAM_ABORTED_BY_USER) {
            lock.unlock();
            m_cv.notify_all();
        }
    }
}

bool BaseMuxElement::has_all_sinks_arrived()
{
    for (const auto &current_sink : m_sink_has_arrived) {
        if (!current_sink.second) {
            return false;
        }
    }
    return true;
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

hailo_status BaseMuxElement::enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name)
{
    (void)source_name;
    auto status = m_pool->enqueue_buffer(mem_view, exec_done);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status BaseMuxElement::execute_dequeue_user_buffers(hailo_status error_status)
{
    auto status = empty_buffer_pool(m_pool, error_status, m_timeout);
    CHECK_SUCCESS(status);
    return PipelineElement::execute_dequeue_user_buffers(error_status);
}

Expected<bool> BaseMuxElement::can_push_buffer_upstream(const uint32_t /*source_index*/)
{
    return !m_pool->is_full();
}

hailo_status BaseMuxElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t /*source_index*/)
{
    auto status = m_pool->allocate_buffers(is_dma_able, num_of_buffers);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<bool> BaseMuxElement::can_push_buffer_upstream(const std::string &source_name)
{
    auto source_index = get_source_index_from_source_name(source_name);
    CHECK_EXPECTED(source_index);
    return can_push_buffer_upstream(*source_index);
}

hailo_status BaseMuxElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &source_name)
{
    auto source_index = get_source_index_from_source_name(source_name);
    CHECK_EXPECTED_AS_STATUS(source_index);
    return fill_buffer_pool(is_dma_able, num_of_buffers, *source_index);
}

BaseDemuxElement::BaseDemuxElement(size_t source_count, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        std::vector<BufferPoolPtr> pools, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
    m_timeout(timeout),
    m_pools(pools),
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
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            LOGGER__INFO("run_push of {} was aborted!", name());
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

void BaseDemuxElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    assert(PipelineDirection::PUSH == m_pipeline_direction);
    if (HAILO_SUCCESS != buffer.action_status()) {
        for (const auto &pad : execution_pads()) {
            auto source_index = m_source_name_to_index[pad->prev()->name()];
            auto acquired_buffer = m_pools[source_index]->acquire_buffer(m_timeout);
            if (HAILO_SUCCESS == acquired_buffer.status()) {
                acquired_buffer->set_action_status(buffer.action_status());

                auto exec_done_cb = buffer.get_exec_done_cb();
                exec_done_cb(buffer.action_status());

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
        return make_unexpected(HAILO_STREAM_ABORTED_BY_USER);
    }

    m_was_source_called[m_source_name_to_index[source.name()]] = true;

    if (were_all_srcs_arrived()) {
        // If all srcs arrived, execute the demux
        auto input = execution_pads()[0]->run_pull();
        if (HAILO_STREAM_ABORTED_BY_USER == input.status()) {
            LOGGER__INFO("run_pull of demux element was aborted!");
            m_was_stream_aborted = true;
            lock.unlock();
            m_cv.notify_all();
            return make_unexpected(input.status());
        }
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == input.status()) {
            return make_unexpected(input.status());
        }
        CHECK_EXPECTED(input);

        auto outputs = action(input.release());
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == outputs.status()) {
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
            return make_unexpected(HAILO_STREAM_ABORTED_BY_USER);
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

hailo_status BaseDemuxElement::enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name)
{
    auto pool_id = m_source_name_to_index.at(source_name);
    auto status = m_pools[pool_id]->enqueue_buffer(mem_view, exec_done);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status BaseDemuxElement::execute_dequeue_user_buffers(hailo_status error_status)
{
    for (auto &pool : m_pools) {
        auto status = empty_buffer_pool(pool, error_status, m_timeout);
        CHECK_SUCCESS(status);
    }
    return PipelineElement::execute_dequeue_user_buffers(error_status);;
}

Expected<bool> BaseDemuxElement::can_push_buffer_upstream(const uint32_t source_index)
{
    CHECK_AS_EXPECTED(source_index < m_pools.size(), HAILO_INTERNAL_FAILURE);
    return !m_pools[source_index]->is_full();
}

hailo_status BaseDemuxElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t source_index)
{
    CHECK(source_index < m_pools.size(), HAILO_INTERNAL_FAILURE);
    CHECK_SUCCESS(m_pools[source_index]->allocate_buffers(is_dma_able, num_of_buffers));
    return HAILO_SUCCESS;
}

Expected<bool> BaseDemuxElement::can_push_buffer_upstream(const std::string &source_name)
{
    auto source_index = get_source_index_from_source_name(source_name);
    CHECK_EXPECTED(source_index);
    return can_push_buffer_upstream(*source_index);
}

hailo_status BaseDemuxElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &source_name)
{
    auto source_index = get_source_index_from_source_name(source_name);
    CHECK_EXPECTED_AS_STATUS(source_index);
    return fill_buffer_pool(is_dma_able, num_of_buffers, *source_index);
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

} /* namespace hailort */
