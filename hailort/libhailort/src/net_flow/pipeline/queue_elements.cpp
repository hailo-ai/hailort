/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file queue_elements.cpp
 * @brief Implementation of the queue elements
 **/

#include "net_flow/pipeline/vstream_internal.hpp"
#include "net_flow/pipeline/queue_elements.hpp"
#include "common/os_utils.hpp"
#include "common/runtime_statistics_internal.hpp"

namespace hailort
{

Expected<SpscQueue<PipelineBuffer>> BaseQueueElement::create_queue(size_t queue_size, EventPtr shutdown_event)
{
    auto queue = SpscQueue<PipelineBuffer>::create(queue_size, shutdown_event);
    CHECK_EXPECTED(queue);

    return queue.release();
}

BaseQueueElement::BaseQueueElement(SpscQueue<PipelineBuffer> &&queue, BufferPoolPtr buffer_pool, EventPtr shutdown_event, const std::string &name,
    std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    IntermediateElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
    m_queue(std::move(queue)),
    m_shutdown_event(shutdown_event),
    m_timeout(timeout),
    m_is_thread_running(true),
    m_activation_event(std::move(activation_event)),
    m_deactivation_event(std::move(deactivation_event)),
    m_queue_size_accumulator(std::move(queue_size_accumulator)),
    m_pool(buffer_pool)
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
                status = run_in_thread();
            }

            if (HAILO_SUCCESS != status) {
                if (HAILO_SHUTDOWN_EVENT_SIGNALED != status) {
                    // We do not want to log error for HAILO_STREAM_ABORT
                    if (HAILO_STREAM_ABORT != status) {
                        LOGGER__ERROR("Queue element {} run in thread function failed! status = {}", this->name(), status);
                    }

                    // Store the real error in pipeline_status
                    m_pipeline_status->store(status);
                }
                // Signal other threads to stop
                hailo_status shutdown_status = m_shutdown_event->signal();
                if (HAILO_SUCCESS != shutdown_status) {
                    LOGGER__CRITICAL("Failed shutting down queue with status {}", shutdown_status);
                }

                // Thread has done its execution. Mark to the thread to wait for activation again
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
    auto status = m_shutdown_event->reset();
    CHECK_SUCCESS(status);

    status = PipelineElementInternal::execute_activate();
    CHECK_SUCCESS(status);

    status = m_deactivation_event.reset();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to reset of deactivation event in {} with status {}", name(), status);
    }

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

    return PipelineElementInternal::execute_post_deactivate(should_clear_abort);
}

hailo_status BaseQueueElement::clear_queue()
{
    std::unique_lock<std::mutex> lock(m_dequeue_mutex);
    return m_queue.clear();
}

hailo_status BaseQueueElement::execute_clear()
{
    auto status = PipelineElementInternal::execute_clear();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to clear() in {} with status {}", name(), status);
    }

    auto queue_clear_status = clear_queue();
    if (HAILO_SUCCESS != queue_clear_status) {
        LOGGER__ERROR("Failed to clear() in {} with status {}", name(), queue_clear_status);
        status = queue_clear_status;
    }

    auto pool_clear_status = empty_buffer_pool(m_pool, HAILO_SUCCESS, BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT);
    if (HAILO_SUCCESS != pool_clear_status) {
        LOGGER__ERROR("Failed to clear() in {} with status {}", name(), pool_clear_status);
        status = pool_clear_status;
    }

    return status;
}

hailo_status PushQueueElement::execute_abort()
{
    auto status = m_shutdown_event->reset();
    CHECK_SUCCESS(status);

    m_pipeline_status->store(HAILO_STREAM_ABORT);

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

    // We treat HAILO_STREAM_ABORT as success because it is caused by user action (aborting streams)
    if (HAILO_STREAM_ABORT == status) {
        return HAILO_SUCCESS;
    }
    return status;
}

Expected<std::shared_ptr<PushQueueElement>> PushQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
    size_t queue_size, size_t frame_size, hailo_pipeline_elem_stats_flags_t flags, hailo_vstream_stats_flags_t vs_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    auto queue = BaseQueueElement::create_queue(queue_size, shutdown_event);
    CHECK_EXPECTED(queue);

    auto activation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(activation_event);

    auto deactivation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(deactivation_event);

    // We do not measure duration for Q elements
    auto duration_collector = DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE);
    CHECK_EXPECTED(duration_collector);

    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto buffer_pool = BufferPool::create(frame_size, queue_size, shutdown_event, flags, vs_flags);
    CHECK_EXPECTED(buffer_pool);

    auto queue_ptr = make_shared_nothrow<PushQueueElement>(queue.release(), buffer_pool.release(), shutdown_event, name, timeout,
        duration_collector.release(), std::move(queue_size_accumulator), std::move(pipeline_status),
        activation_event.release(), deactivation_event.release(), async_pipeline, true);
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PushQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->description());

    return queue_ptr;
}

Expected<std::shared_ptr<PushQueueElement>> PushQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        size_t frame_size, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PushQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms), vstream_params.queue_size,
        frame_size, vstream_params.pipeline_elements_stats_flags, vstream_params.vstream_stats_flags,
        pipeline_status, async_pipeline);
}

PushQueueElement::PushQueueElement(SpscQueue<PipelineBuffer> &&queue, BufferPoolPtr buffer_pool, EventPtr shutdown_event, const std::string &name,
    std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
    std::shared_ptr<AsyncPipeline> async_pipeline, bool should_start_thread) :
    BaseQueueElement(std::move(queue), buffer_pool, shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
        std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event), PipelineDirection::PUSH, async_pipeline)
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
    auto status = m_pipeline_status->load();
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("run_push of {} was aborted!", name());
        return status;
    }
    CHECK_SUCCESS(m_pipeline_status->load());

    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_queue.size_approx()));
    }

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
        if ((HAILO_STREAM_ABORT == status) || (HAILO_SHUTDOWN_EVENT_SIGNALED == status)) {
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
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("run_push of {} was aborted!", name());
        return status;
    }
    else if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("run_push of {} stopped because Shutdown event was signaled!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<AsyncPushQueueElement>> AsyncPushQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
    size_t queue_size, size_t frame_size, bool is_empty, bool interacts_with_hw, hailo_pipeline_elem_stats_flags_t flags,
    hailo_vstream_stats_flags_t vstream_stats_flags, EventPtr shutdown_event,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::shared_ptr<AsyncPipeline> async_pipeline, bool is_entry)
{
    if (is_entry) {
        // Multiplying by 2 to ensure dual-buffering when edge-element is the bottleneck
        queue_size = queue_size * 2;
    }

    auto queue = BaseQueueElement::create_queue(queue_size, shutdown_event);
    CHECK_EXPECTED(queue);

    auto activation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(activation_event);

    auto deactivation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(deactivation_event);

    // We do not measure duration for Q elements
    auto duration_collector = DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE);
    CHECK_EXPECTED(duration_collector);

    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto buffer_pool = BufferPool::create(frame_size, queue_size, shutdown_event, flags, vstream_stats_flags, is_empty, interacts_with_hw);
    CHECK_EXPECTED(buffer_pool);

    auto queue_ptr = make_shared_nothrow<AsyncPushQueueElement>(queue.release(), buffer_pool.release(),
        shutdown_event, name, timeout, duration_collector.release(), std::move(queue_size_accumulator),
        std::move(pipeline_status), activation_event.release(), deactivation_event.release(), async_pipeline);
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PushQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->description());

    return queue_ptr;
}

Expected<std::shared_ptr<AsyncPushQueueElement>> AsyncPushQueueElement::create(const std::string &name, const ElementBuildParams &build_params,
    size_t frame_size, bool is_empty, bool interacts_with_hw, std::shared_ptr<AsyncPipeline> async_pipeline, bool is_entry)
{
    // Pools that interacts with HW should be as big as the edges pools (user-buffers)
    auto queue_size = (interacts_with_hw) ? build_params.buffer_pool_size_edges : build_params.buffer_pool_size_internal;
    return AsyncPushQueueElement::create(name, build_params.timeout, queue_size, frame_size, is_empty, interacts_with_hw,
        build_params.elem_stats_flags, build_params.vstream_stats_flags, build_params.shutdown_event, build_params.pipeline_status, async_pipeline,
        is_entry);
}

AsyncPushQueueElement::AsyncPushQueueElement(SpscQueue<PipelineBuffer> &&queue, BufferPoolPtr buffer_pool, EventPtr shutdown_event,
    const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,  AccumulatorPtr &&queue_size_accumulator,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
    std::shared_ptr<AsyncPipeline> async_pipeline) :
    PushQueueElement(std::move(queue), buffer_pool, shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
        std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event), async_pipeline, false)
{
    start_thread();
}

void AsyncPushQueueElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    // We do not measure duration for Q elements
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
    PipelineBuffer buffer;
    hailo_status buffer_status = HAILO_UNINITIALIZED;
    {
        std::unique_lock<std::mutex> lock(m_dequeue_mutex);
        auto buffer_exp = m_queue.dequeue(INIFINITE_TIMEOUT());
        buffer_status = buffer_exp.status();
        if (HAILO_SUCCESS == buffer_status) {
            buffer = buffer_exp.release();
        }
    }

    switch (buffer_status) {
    case HAILO_SHUTDOWN_EVENT_SIGNALED:
        break;

    case HAILO_SUCCESS:
        // Return if deactivated
        if (PipelineBuffer::Type::DEACTIVATE == buffer.get_type()) {
            hailo_status status = m_shutdown_event->signal();
            CHECK_SUCCESS(status);

            status = next_pad().deactivate();
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Deactivate of source in {} has failed with status {}", name(), status);
            }

            return HAILO_SHUTDOWN_EVENT_SIGNALED;
        }

        next_pad().run_push_async(std::move(buffer));
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
        if ((HAILO_STREAM_ABORT == status) || (HAILO_SHUTDOWN_EVENT_SIGNALED == status)) {
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

hailo_status AsyncPushQueueElement::execute_dequeue_user_buffers(hailo_status error_status)
{
    auto dequeue_status = PipelineElement::execute_dequeue_user_buffers(error_status);

    auto clear_queues_status = clear_queue();
    auto empty_pool_status = empty_buffer_pool(m_pool, error_status, m_timeout);

    CHECK_SUCCESS(dequeue_status);
    CHECK_SUCCESS(clear_queues_status);
    CHECK_SUCCESS(empty_pool_status);
    return HAILO_SUCCESS;
}

Expected<bool> AsyncPushQueueElement::can_push_buffer_downstream(uint32_t frames_count)
{
    return m_queue.size_approx() + frames_count < m_queue.max_capacity();
}

Expected<std::shared_ptr<PullQueueElement>> PullQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
    size_t queue_size, size_t frame_size, hailo_pipeline_elem_stats_flags_t flags, hailo_vstream_stats_flags_t vstream_stats_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
{
    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    auto queue = BaseQueueElement::create_queue(queue_size, shutdown_event);
    CHECK_EXPECTED(queue);

    auto activation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(activation_event);

    auto deactivation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(deactivation_event);

    // We do not measure duration for Q elements
    auto duration_collector = DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE);
    CHECK_EXPECTED(duration_collector);

    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto buffer_pool = BufferPool::create(frame_size, queue_size, shutdown_event, flags, vstream_stats_flags);
    CHECK_EXPECTED(buffer_pool);

    auto queue_ptr = make_shared_nothrow<PullQueueElement>(queue.release(), buffer_pool.release(), shutdown_event,
        name, timeout, duration_collector.release(), std::move(queue_size_accumulator), std::move(pipeline_status),
        activation_event.release(), deactivation_event.release());
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PullQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->description());

    return queue_ptr;
}
Expected<std::shared_ptr<PullQueueElement>> PullQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        size_t frame_size, std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
{
    return PullQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.queue_size, frame_size, vstream_params.pipeline_elements_stats_flags, vstream_params.vstream_stats_flags,
        pipeline_status);
}

PullQueueElement::PullQueueElement(SpscQueue<PipelineBuffer> &&queue, BufferPoolPtr buffer_pool, EventPtr shutdown_event,
    const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event) :
    BaseQueueElement(std::move(queue), buffer_pool, shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
        std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event), PipelineDirection::PULL, nullptr)
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
    // We do not measure duration for Q elements
    CHECK_AS_EXPECTED(!optional, HAILO_INVALID_ARGUMENT, "Optional buffer is not allowed in queue element!");

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
    if (HAILO_STREAM_ABORT == buffer.status()) {
        LOGGER__INFO("run_pull of queue element {} was aborted!", name());
        return HAILO_STREAM_ABORT;
    }
    if (HAILO_NETWORK_GROUP_NOT_ACTIVATED == buffer.status()) {
        LOGGER__INFO("run_pull of queue element {} was called before network_group is activated!", name());
        return HAILO_NETWORK_GROUP_NOT_ACTIVATED;
    }
    CHECK_EXPECTED_AS_STATUS(buffer);

    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_queue.size_approx()));
    }

    hailo_status status = m_queue.enqueue(buffer.release(), INIFINITE_TIMEOUT());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Shutdown event was signaled in enqueue of queue element {}!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<UserBufferQueueElement>> UserBufferQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
    hailo_pipeline_elem_stats_flags_t flags, hailo_vstream_stats_flags_t vstream_stats_flags, size_t frame_size,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
{
    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    const auto queue_size = 1;
    auto pending_buffer_queue = BaseQueueElement::create_queue(queue_size, shutdown_event);
    CHECK_EXPECTED(pending_buffer_queue);

    auto activation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(activation_event);

    auto deactivation_event = Event::create(Event::State::not_signalled);
    CHECK_EXPECTED(deactivation_event);

    // We do not measure duration for Q elements
    auto duration_collector = DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE);
    CHECK_EXPECTED(duration_collector);

    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto is_empty = true; // UserBufferQueue always holds user buffers, therefore its created empty
    auto is_dma_able = false;
    auto buffer_pool = BufferPool::create(frame_size, queue_size, shutdown_event, flags, vstream_stats_flags, is_empty, is_dma_able);
    CHECK_EXPECTED(buffer_pool);

    auto queue_ptr = make_shared_nothrow<UserBufferQueueElement>(pending_buffer_queue.release(),
        buffer_pool.release(), shutdown_event, name, timeout, duration_collector.release(),
        std::move(queue_size_accumulator), std::move(pipeline_status), activation_event.release(),
        deactivation_event.release());
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating UserBufferQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->description());

    return queue_ptr;
}

Expected<std::shared_ptr<UserBufferQueueElement>> UserBufferQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
    size_t frame_size, std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
{
    return UserBufferQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.pipeline_elements_stats_flags, vstream_params.vstream_stats_flags, frame_size, pipeline_status);
}

UserBufferQueueElement::UserBufferQueueElement(SpscQueue<PipelineBuffer> &&queue, BufferPoolPtr buffer_pool,
    EventPtr shutdown_event, const std::string &name, std::chrono::milliseconds timeout,
    DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    Event &&activation_event, Event &&deactivation_event) :
    PullQueueElement(std::move(queue), buffer_pool, shutdown_event, name, timeout, std::move(duration_collector),
        std::move(queue_size_accumulator), std::move(pipeline_status), std::move(activation_event),
        std::move(deactivation_event))
{}

Expected<PipelineBuffer> UserBufferQueueElement::run_pull(PipelineBuffer &&optional, const PipelinePad &/*source*/)
{
    CHECK_AS_EXPECTED(optional, HAILO_INVALID_ARGUMENT, "Optional buffer must be valid in {}!", name());

    hailo_status status = m_pool->enqueue_buffer(std::move(optional));
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Shutdown event was signaled in enqueue of queue element {}!", name());
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto output = m_queue.dequeue(m_timeout);
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

hailo_status UserBufferQueueElement::set_buffer_pool_buffer_size(uint32_t frame_size)
{
    return m_pool->set_buffer_size(frame_size);
}

hailo_status UserBufferQueueElement::run_in_thread()
{
    auto optional = m_pool->acquire_buffer(INIFINITE_TIMEOUT());
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
    if (HAILO_STREAM_ABORT == buffer.status()) {
        LOGGER__INFO("run_pull of {} was aborted!", name());

        return HAILO_STREAM_ABORT;
    }
    CHECK_EXPECTED_AS_STATUS(buffer);

    hailo_status status = m_queue.enqueue(buffer.release(), INIFINITE_TIMEOUT());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Shutdown event was signaled in enqueue of {}!", name());
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

std::vector<AccumulatorPtr> UserBufferQueueElement::get_queue_size_accumulators()
{
    return std::vector<AccumulatorPtr>(); // Since this element is sync, queue state will always be 0
}

hailo_status PullQueueElement::execute_abort()
{
    m_pipeline_status->store(HAILO_STREAM_ABORT);

    // Signal shutdown-event to make run_in_thread finish execution
    auto status = m_shutdown_event->signal();
    CHECK_SUCCESS(status);

    status = PipelineElementInternal::execute_abort();
    CHECK_SUCCESS(status);

    // Wait to confirm the thread running 'run_in_thread' finished execution and the abort flow finished
    status = m_deactivation_event.wait(std::chrono::milliseconds(HAILO_DEFAULT_VSTREAM_TIMEOUT_MS));
    CHECK_SUCCESS(status, "Failed to confirm abortion of {}", name());

    return HAILO_SUCCESS;
}

} /* namespace hailort */
