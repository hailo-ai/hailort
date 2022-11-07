/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline.cpp
 * @brief Implemention of the pipeline
 **/

#include "pipeline.hpp"
#include "common/utils.hpp"
#include "common/runtime_statistics_internal.hpp"

namespace hailort
{

PipelineBuffer::Metadata::Metadata(PipelineTimePoint start_time) :
    m_start_time(start_time)
{}

PipelineBuffer::Metadata::Metadata() :
    Metadata(PipelineTimePoint{})
{}

PipelineTimePoint PipelineBuffer::Metadata::get_start_time() const
{
    return m_start_time;
}

void PipelineBuffer::Metadata::set_start_time(PipelineTimePoint val)
{
    m_start_time = val;
}

PipelineBuffer::PipelineBuffer() :
    PipelineBuffer(Type::DATA)
{}

PipelineBuffer::PipelineBuffer(Type type) :
    m_type(type),
    m_buffer(),
    m_should_release_buffer(false),
    m_pool(nullptr),
    m_view(),
    m_metadata()
{}

PipelineBuffer::PipelineBuffer(MemoryView view, bool should_measure) :
    m_type(Type::DATA),
    m_buffer(),
    m_should_release_buffer(false),
    m_pool(nullptr),
    m_view(view),
    m_metadata(Metadata(add_timestamp(should_measure)))
{}

PipelineBuffer::PipelineBuffer(Buffer &&buffer, BufferPoolPtr pool, bool should_measure) :
    m_type(Type::DATA),
    m_buffer(std::move(buffer)),
    m_should_release_buffer(true),
    m_pool(pool),
    m_view(m_buffer),
    m_metadata(Metadata(add_timestamp(should_measure)))
{}

PipelineBuffer::PipelineBuffer(PipelineBuffer &&other) :
    m_type(other.m_type),
    m_buffer(std::move(other.m_buffer)),
    m_should_release_buffer(std::exchange(other.m_should_release_buffer, false)),
    m_pool(std::move(other.m_pool)),
    m_view(std::move(other.m_view)),
    m_metadata(std::move(other.m_metadata))
{}

PipelineBuffer &PipelineBuffer::operator=(PipelineBuffer &&other)
{
    m_type = other.m_type,
    m_buffer = std::move(other.m_buffer);
    m_should_release_buffer = std::exchange(other.m_should_release_buffer, false);
    m_pool = std::move(other.m_pool);
    m_view = std::move(other.m_view);
    m_metadata = std::move(other.m_metadata);
    return *this;
}

PipelineBuffer::~PipelineBuffer()
{
    if (!m_should_release_buffer) {
        return;
    }

    hailo_status status = m_pool->release_buffer(std::move(m_buffer));
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Releasing buffer in buffer pool failed! status = {}", status);
    }
}

PipelineBuffer::operator bool() const
{
    return !m_view.empty();
}

uint8_t* PipelineBuffer::data()
{
    return m_view.data();
}

size_t PipelineBuffer::size() const
{
    return m_view.size();
}

MemoryView PipelineBuffer::as_view()
{
    return m_view;
}

PipelineBuffer::Type PipelineBuffer::get_type() const
{
    return m_type;
}

PipelineBuffer::Metadata PipelineBuffer::get_metadata() const
{
    return m_metadata;
}

void PipelineBuffer::set_metadata(Metadata &&val) 
{
    m_metadata = std::move(val);
}

PipelineTimePoint PipelineBuffer::add_timestamp(bool should_measure)
{
    return should_measure ? std::chrono::steady_clock::now() : PipelineTimePoint{};
}

Expected<BufferPoolPtr> BufferPool::create(size_t buffer_size, size_t buffer_count, EventPtr shutdown_event,
                                           hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags)
{
    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((elem_flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }
    const bool measure_vstream_latency = (vstream_flags & HAILO_VSTREAM_STATS_MEASURE_LATENCY) != 0;

    auto free_buffers = SpscQueue<Buffer>::create(buffer_count, shutdown_event, BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT);
    CHECK_EXPECTED(free_buffers);

    for (size_t i = 0; i < buffer_count; i++) {
        auto buffer = Buffer::create(buffer_size);
        CHECK_EXPECTED(buffer);

        hailo_status status = free_buffers->enqueue(buffer.release());
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    auto buffer_pool_ptr = make_shared_nothrow<BufferPool>(buffer_size, measure_vstream_latency,
        free_buffers.release(), std::move(queue_size_accumulator));
    CHECK_AS_EXPECTED(nullptr != buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_pool_ptr;
}

BufferPool::BufferPool(size_t buffer_size, bool measure_vstream_latency, SpscQueue<Buffer> &&free_buffers, AccumulatorPtr &&queue_size_accumulator) :
    m_buffer_size(buffer_size),
    m_measure_vstream_latency(measure_vstream_latency),
    m_free_buffers(std::move(free_buffers)),
    m_queue_size_accumulator(std::move(queue_size_accumulator))
{}

size_t BufferPool::buffer_size()
{
    return m_buffer_size;
}

Expected<PipelineBuffer> BufferPool::acquire_buffer(std::chrono::milliseconds timeout)
{
    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_free_buffers.size_approx()));
    }
    auto buffer = m_free_buffers.dequeue(timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }
    else if (HAILO_TIMEOUT == buffer.status()) {
        LOGGER__WARNING("Failed to acquire buffer because the buffer pool is empty. This could be caused by uneven reading and writing speeds, with a short user-defined timeout.");
        return make_unexpected(buffer.status());
    }
    CHECK_EXPECTED(buffer);
    return PipelineBuffer(buffer.release(), shared_from_this(), m_measure_vstream_latency);
}

AccumulatorPtr BufferPool::get_queue_size_accumulator()
{
    return m_queue_size_accumulator;
}

Expected<PipelineBuffer> BufferPool::get_available_buffer(PipelineBuffer &&optional, std::chrono::milliseconds timeout)
{
    if (optional) {
        CHECK_AS_EXPECTED(optional.size() == buffer_size(), HAILO_INVALID_OPERATION,
            "Optional buffer size must be equal to pool buffer size. Optional buffer size = {}, buffer pool size = {}",
            optional.size(), buffer_size());
        return std::move(optional);
    }
    
    auto acquired_buffer = acquire_buffer(timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == acquired_buffer.status()) {
        return make_unexpected(acquired_buffer.status());
    }
    CHECK_EXPECTED(acquired_buffer, "Failed to acquire buffer");
    return acquired_buffer.release();
}

hailo_status BufferPool::release_buffer(Buffer &&buffer)
{
    std::unique_lock<std::mutex> lock(m_release_buffer_mutex);
    // This can be called after the shutdown event was signaled so we ignore it here
    return m_free_buffers.enqueue(std::move(buffer), true);
}

Expected<DurationCollector> DurationCollector::create(hailo_pipeline_elem_stats_flags_t flags,
    uint32_t num_frames_before_collection_start)
{
    AccumulatorPtr latency_accumulator = nullptr;
    const auto measure_latency = should_measure_latency(flags);
    if (measure_latency) {
        latency_accumulator = make_shared_nothrow<FullAccumulator<double>>("latency");
        CHECK_AS_EXPECTED(nullptr != latency_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    AccumulatorPtr average_fps_accumulator = nullptr;
    const auto measure_average_fps = should_measure_average_fps(flags);
    if (measure_average_fps) {
        average_fps_accumulator = make_shared_nothrow<AverageFPSAccumulator<double>>("fps");
        CHECK_AS_EXPECTED(nullptr != average_fps_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    return DurationCollector(measure_latency, measure_average_fps, std::move(latency_accumulator),
        std::move(average_fps_accumulator), num_frames_before_collection_start);
}

DurationCollector::DurationCollector(bool measure_latency, bool measure_average_fps,
                                      AccumulatorPtr &&latency_accumulator, AccumulatorPtr &&average_fps_accumulator,
                                      uint32_t num_frames_before_collection_start) :
    m_measure_latency(measure_latency),
    m_measure_average_fps(measure_average_fps),
    m_measure(m_measure_latency || m_measure_average_fps),
    m_latency_accumulator(std::move(latency_accumulator)),
    m_average_fps_accumulator(std::move(average_fps_accumulator)),
    m_start(),
    m_count(0),
    m_num_frames_before_collection_start(num_frames_before_collection_start)
{}

void DurationCollector::start_measurement()
{
    if (!m_measure) {
        return;
    }

    m_count++;
    if (m_count < m_num_frames_before_collection_start) {
        return;
    }

    m_start = std::chrono::steady_clock::now();
}

void DurationCollector::complete_measurement()
{
    if ((!m_measure) || (m_count < m_num_frames_before_collection_start)) {
        return;
    }

    const auto duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(
        std::chrono::steady_clock::now() - m_start).count();
    if (m_measure_latency) {
        m_latency_accumulator->add_data_point(duration_sec);
    }

    if (m_measure_average_fps) {
        m_average_fps_accumulator->add_data_point(duration_sec);
    }
}

AccumulatorPtr DurationCollector::get_latency_accumulator()
{
    return m_latency_accumulator;
}

AccumulatorPtr DurationCollector::get_average_fps_accumulator()
{
    return m_average_fps_accumulator;
}

bool DurationCollector::should_measure_latency(hailo_pipeline_elem_stats_flags_t flags)
{
    return (flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_LATENCY) != 0;
}

bool DurationCollector::should_measure_average_fps(hailo_pipeline_elem_stats_flags_t flags)
{
    return (flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_FPS) != 0;
}

PipelineObject::PipelineObject(const std::string &name) : m_name(name)
{}

const std::string &PipelineObject::name() const
{
    return m_name;
}

std::string PipelineObject::create_element_name(const std::string &element_name, const std::string &stream_name, uint8_t stream_index)
{
    std::stringstream name;
    name << element_name << static_cast<uint32_t>(stream_index) << "_" << stream_name;
    return name.str();
}

hailo_status PipelinePad::link_pads(std::shared_ptr<PipelineElement> left, std::shared_ptr<PipelineElement> right,
    uint32_t left_source_index, uint32_t right_sink_index)
{
    CHECK_ARG_NOT_NULL(left);
    CHECK_ARG_NOT_NULL(right);
    return link_pads(*left, *right, left_source_index, right_sink_index);
}

hailo_status PipelinePad::link_pads(PipelineElement &left, PipelineElement &right, uint32_t left_source_index,
    uint32_t right_sink_index)
{
    CHECK(left_source_index < left.sources().size(), HAILO_INVALID_ARGUMENT,
        "Cannot link source pad #{} for PipelineElement '{}', it has only {} source pads.",
        left_source_index, left.name(), left.sources().size());
    CHECK(right_sink_index < right.sinks().size(), HAILO_INVALID_ARGUMENT,
        "Cannot link sink pad #{} for PipelineElement '{}', it has only {} sink pads.",
        right_sink_index, right.name(), right.sinks().size());
    auto &left_source_pad = left.sources()[left_source_index];
    auto &right_sink_pad = right.sinks()[right_sink_index];

    left_source_pad.set_next(&right_sink_pad);
    right_sink_pad.set_prev(&left_source_pad);

    return HAILO_SUCCESS;
}

// Initial value of the counter
uint32_t PipelinePad::index = 0;
std::string PipelinePad::create_pad_name(const std::string &element_name, Type pad_type)
{
    std::stringstream string_stream;
    const auto pad_type_name = (pad_type == Type::SINK) ? "sink" : "source";
    string_stream << element_name << "(" << pad_type_name << index++ << ")";
    return string_stream.str();
}

PipelinePad::PipelinePad(PipelineElement &element, const std::string &element_name, Type pad_type) :
    PipelineObject(create_pad_name(element_name, pad_type)),
    m_element(element),
    m_next(nullptr),
    m_prev(nullptr),
    m_push_complete_callback(nullptr),
    m_pull_complete_callback(nullptr)
{}

hailo_status PipelinePad::activate()
{
    return m_element.activate();
}

hailo_status PipelinePad::deactivate()
{
    return m_element.deactivate();
}

hailo_status PipelinePad::post_deactivate()
{
    return m_element.post_deactivate();
}

hailo_status PipelinePad::clear()
{
    return m_element.clear();
}

hailo_status PipelinePad::flush()
{
    return m_element.flush();
}

hailo_status PipelinePad::abort()
{
    return m_element.abort();
}

hailo_status PipelinePad::wait_for_finish()
{
    return m_element.wait_for_finish();
}

hailo_status PipelinePad::resume()
{
    return m_element.resume();
}

hailo_status PipelinePad::run_push(PipelineBuffer &&buffer)
{
    if (m_push_complete_callback) {
        auto metadata = buffer.get_metadata();
        const auto status = m_element.run_push(std::move(buffer));
        m_push_complete_callback(metadata);
        return status;
    }

    return m_element.run_push(std::move(buffer));
}

Expected<PipelineBuffer> PipelinePad::run_pull(PipelineBuffer &&optional)
{
    auto result = m_element.run_pull(std::move(optional), *this);
    if (m_pull_complete_callback && result) {
        m_pull_complete_callback(result->get_metadata());
    }

    return result;
}

void PipelinePad::set_push_complete_callback(PushCompleteCallback push_complete_callback)
{
    m_push_complete_callback = push_complete_callback;
}

void PipelinePad::set_pull_complete_callback(PullCompleteCallback pull_complete_callback)
{
    m_pull_complete_callback = pull_complete_callback;
}

void PipelinePad::set_next(PipelinePad *next)
{
    m_next = next;
}

void PipelinePad::set_prev(PipelinePad *prev)
{
    m_prev = prev;
}

PipelinePad *PipelinePad::next()
{
    return m_next;
}

PipelinePad *PipelinePad::prev()
{
    return m_prev;
}

PipelineElement &PipelinePad::element()
{
    return m_element;
}

const PipelinePad *PipelinePad::next() const
{
    return m_next;
}

const PipelinePad *PipelinePad::prev() const
{
    return m_prev;
}

const PipelineElement &PipelinePad::element() const
{
    return m_element;
}

SourceElement::SourceElement(const std::string &name, DurationCollector &&duration_collector,
                             std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status))
{
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
}

PipelinePad &SourceElement::source()
{
    return m_sources[0];
}

SinkElement::SinkElement(const std::string &name, DurationCollector &&duration_collector,
                         std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status))
{
    m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
}

PipelinePad &SinkElement::sink()
{
    return m_sinks[0];
}

IntermediateElement::IntermediateElement(const std::string &name, DurationCollector &&duration_collector,
                                         std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status))
{
    m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
}

std::vector<PipelinePad*> IntermediateElement::execution_pads()
{
    std::vector<PipelinePad*> result{&next_pad()};
    return result;
}

PipelineElement::PipelineElement(const std::string &name, DurationCollector &&duration_collector,
                                 std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status) :
    PipelineObject(name),
    m_duration_collector(std::move(duration_collector)),
    m_pipeline_status(std::move(pipeline_status)),
    m_sinks(),
    m_sources()
{}

AccumulatorPtr PipelineElement::get_fps_accumulator()
{
    return m_duration_collector.get_average_fps_accumulator();
}

AccumulatorPtr PipelineElement::get_latency_accumulator()
{
    return m_duration_collector.get_latency_accumulator();
}

std::vector<AccumulatorPtr> PipelineElement::get_queue_size_accumulators()
{
    return std::vector<AccumulatorPtr>();
}

std::vector<PipelinePad> &PipelineElement::sinks()
{
    return m_sinks;
}

std::vector<PipelinePad> &PipelineElement::sources()
{
    return m_sources;
}

const std::vector<PipelinePad> &PipelineElement::sinks() const
{
    return m_sinks;
}

const std::vector<PipelinePad> &PipelineElement::sources() const
{
    return m_sources;
}

std::string PipelineElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << ")";
    return element_description.str();
}

hailo_status PipelineElement::activate()
{
    return execute_activate();
}

hailo_status PipelineElement::deactivate()
{
    return execute_deactivate();
}

hailo_status PipelineElement::post_deactivate()
{
    return execute_post_deactivate();
}

hailo_status PipelineElement::clear()
{
    return execute_clear();
}

hailo_status PipelineElement::flush()
{
    return execute_flush();
}

hailo_status PipelineElement::abort()
{
    return execute_abort();
}

hailo_status PipelineElement::resume()
{
    return execute_resume();
}

hailo_status PipelineElement::wait_for_finish()
{
    return execute_wait_for_finish();
}

hailo_status PipelineElement::execute_activate()
{
    return execute([&](auto *pad){ return pad->activate(); });
}

hailo_status PipelineElement::execute_deactivate()
{
    return execute([&](auto *pad){ return pad->deactivate(); });
}

hailo_status PipelineElement::execute_post_deactivate()
{
    return execute([&](auto *pad){ return pad->post_deactivate(); });
}

hailo_status PipelineElement::execute_clear()
{
    return execute([&](auto *pad){ return pad->clear(); });
}

hailo_status PipelineElement::execute_flush()
{
    return execute([&](auto *pad){ return pad->flush(); });
}

hailo_status PipelineElement::execute_abort()
{
    return execute([&](auto *pad){ return pad->abort(); });
}

hailo_status PipelineElement::execute_resume()
{
    return execute([&](auto *pad){ return pad->resume(); });
}

hailo_status PipelineElement::execute_wait_for_finish()
{
    return execute([&](auto *pad){ return pad->wait_for_finish(); });
}

hailo_status PipelineElement::execute(std::function<hailo_status(PipelinePad*)> func)
{
    for (auto pad : execution_pads()) {
        auto status = func(pad);
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

std::vector<PipelinePad*> SourceElement::execution_pads()
{
    std::vector<PipelinePad*> result{&source()};
    return result;
}

std::vector<PipelinePad*> SinkElement::execution_pads()
{
    std::vector<PipelinePad*> result{&sink()};
    return result;
}

FilterElement::FilterElement(const std::string &name, DurationCollector &&duration_collector,
                             std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status) :
    IntermediateElement(name, std::move(duration_collector), std::move(pipeline_status))
{}

hailo_status FilterElement::run_push(PipelineBuffer &&buffer)
{
    auto output = action(std::move(buffer), PipelineBuffer());
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == output.status()) {
        return output.status();
    }
    CHECK_EXPECTED_AS_STATUS(output);

    hailo_status status = next_pad().run_push(output.release());
    if (status == HAILO_SHUTDOWN_EVENT_SIGNALED) {
        LOGGER__INFO("run_push of {} was shutdown!", name());
        return status;
    }
    if (status == HAILO_STREAM_INTERNAL_ABORT) {
        LOGGER__INFO("run_push of {} was aborted!", name());
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
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

Expected<SpscQueue<PipelineBuffer>> BaseQueueElement::create_queue(size_t queue_size, EventPtr shutdown_event)
{
    auto queue = SpscQueue<PipelineBuffer>::create(queue_size, shutdown_event);
    CHECK_EXPECTED(queue);

    return queue.release();
}

BaseQueueElement::BaseQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
                                   std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
                                   AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   Event &&activation_event, Event &&deactivation_event) :
    IntermediateElement(name, std::move(duration_collector), std::move(pipeline_status)),
    m_queue(std::move(queue)),
    m_shutdown_event(shutdown_event),
    m_timeout(timeout),
    m_is_thread_running(true),
    m_activation_event(std::move(activation_event)),
    m_deactivation_event(std::move(deactivation_event)),
    m_queue_size_accumulator(std::move(queue_size_accumulator)),
    m_is_run_in_thread_running(false)
{}

void BaseQueueElement::start_thread()
{
    m_thread = std::thread([this] () {
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
                    // We do not want to log error for HAILO_STREAM_INTERNAL_ABORT
                    if (HAILO_STREAM_INTERNAL_ABORT != status) {
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
    hailo_status status = PipelineElement::execute_activate();
    CHECK_SUCCESS(status);

    status = m_activation_event.signal();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status BaseQueueElement::execute_post_deactivate()
{
    hailo_status status = m_deactivation_event.wait(INIFINITE_TIMEOUT());
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to post_deactivate() in {} with status {}", name(), status);
    }

    status = m_deactivation_event.reset();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to reset of deactivation event in {} with status {}", name(), status);
    }

    return PipelineElement::execute_post_deactivate();
}

hailo_status BaseQueueElement::execute_clear()
{
    auto status = PipelineElement::execute_clear();
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

hailo_status PushQueueElement::execute_abort()
{
    auto status = m_shutdown_event->reset();
    CHECK_SUCCESS(status);
    m_pipeline_status->store(HAILO_STREAM_INTERNAL_ABORT);
    status = PipelineElement::execute_abort();
    CHECK_SUCCESS(status);
    return m_activation_event.signal();
}

hailo_status BaseQueueElement::execute_resume()
{
    auto status = m_shutdown_event->reset();
    CHECK_SUCCESS(status);
    m_pipeline_status->store(HAILO_SUCCESS);
    status = PipelineElement::execute_resume();
    CHECK_SUCCESS(status);
    return m_activation_event.signal();
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

    // We treat HAILO_STREAM_INTERNAL_ABORT as success because it is caused by user action (aborting streams)
    if (HAILO_STREAM_INTERNAL_ABORT == status) {
        return HAILO_SUCCESS;
    }
    return status;
}

Expected<std::shared_ptr<PushQueueElement>> PushQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
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
        activation_event.release(), deactivation_event.release());
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PushQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->name());

    return queue_ptr;
}

Expected<std::shared_ptr<PushQueueElement>> PushQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
{
    return PushQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.queue_size, vstream_params.pipeline_elements_stats_flags, shutdown_event, pipeline_status);
}

PushQueueElement::PushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
                                   std::chrono::milliseconds timeout, DurationCollector &&duration_collector, 
                                   AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   Event &&activation_event, Event &&deactivation_event) :
    BaseQueueElement(std::move(queue), shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
                     std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event))
{
    start_thread();
}

PushQueueElement::~PushQueueElement()
{
    stop_thread();
}

hailo_status PushQueueElement::run_push(PipelineBuffer &&buffer)
{
    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_queue.size_approx()));
    }
    auto status = m_pipeline_status->load();
    if (status == HAILO_STREAM_INTERNAL_ABORT) {
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
        auto deactivation_status = PipelineElement::execute_deactivate();
        CHECK_SUCCESS(deactivation_status);
        if ((HAILO_STREAM_INTERNAL_ABORT == status) || (HAILO_SHUTDOWN_EVENT_SIGNALED == status)) {
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
    if (HAILO_STREAM_INTERNAL_ABORT == status) {
        LOGGER__INFO("run_push of {} was aborted!", name());
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<PullQueueElement>> PullQueueElement::create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
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
        activation_event.release(), deactivation_event.release());
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PullQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->name());

    return queue_ptr;
}
Expected<std::shared_ptr<PullQueueElement>> PullQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
{
    return PullQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.queue_size, vstream_params.pipeline_elements_stats_flags, shutdown_event, pipeline_status);
}

PullQueueElement::PullQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
                                   std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
                                   AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   Event &&activation_event, Event &&deactivation_event) :
    BaseQueueElement(std::move(queue), shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
                     std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event))
{
    start_thread();
}

PullQueueElement::~PullQueueElement()
{
    stop_thread();
}

hailo_status PullQueueElement::run_push(PipelineBuffer &&/*buffer*/)
{
    return HAILO_INVALID_OPERATION;
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
    hailo_status status = PipelineElement::execute_deactivate();
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
    if (HAILO_STREAM_INTERNAL_ABORT == buffer.status()) {
        LOGGER__INFO("run_pull of queue element {} was aborted!", name());
        return HAILO_STREAM_INTERNAL_ABORT;
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
    hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
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
        deactivation_event.release());
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating UserBufferQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->name());

    return queue_ptr;
}

Expected<std::shared_ptr<UserBufferQueueElement>> UserBufferQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
{
    return UserBufferQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.pipeline_elements_stats_flags, shutdown_event, pipeline_status);
}

UserBufferQueueElement::UserBufferQueueElement(SpscQueue<PipelineBuffer> &&queue, SpscQueue<PipelineBuffer> &&full_buffer_queue,
                                               EventPtr shutdown_event, const std::string &name, std::chrono::milliseconds timeout,
                                               DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
                                               std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                               Event &&activation_event, Event &&deactivation_event) :
    PullQueueElement(std::move(queue), shutdown_event, name, timeout, std::move(duration_collector),
                     std::move(queue_size_accumulator), std::move(pipeline_status), std::move(activation_event),
                     std::move(deactivation_event)),
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
    CHECK_EXPECTED(output);

    CHECK_AS_EXPECTED(output->data() == optional.data(), HAILO_INTERNAL_FAILURE, "The buffer received in {} was not the same as the user buffer!", name());
    return output;
}

hailo_status UserBufferQueueElement::execute_clear()
{
    auto status = PipelineElement::execute_clear();
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
    if (HAILO_STREAM_INTERNAL_ABORT == buffer.status()) {
        LOGGER__INFO("run_pull of {} was aborted!", name());
        return HAILO_STREAM_INTERNAL_ABORT;
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
                               DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status)),
    m_timeout(timeout)
{
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
    m_sinks.reserve(sink_count);
    for (uint32_t i = 0; i < sink_count; ++i) {
        m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
    }
}

std::vector<PipelinePad*> BaseMuxElement::execution_pads()
{
    std::vector<PipelinePad*> result;
    result.reserve(m_sinks.size());
    for (auto& pad : m_sinks) {
        result.push_back(pad.prev());
    }
    return result;
}

hailo_status BaseMuxElement::run_push(PipelineBuffer &&/*buffer*/)
{
    return HAILO_NOT_IMPLEMENTED;
}

Expected<PipelineBuffer> BaseMuxElement::run_pull(PipelineBuffer &&optional, const PipelinePad &/*source*/)
{
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

BaseDemuxElement::BaseDemuxElement(size_t source_count, const std::string &name, std::chrono::milliseconds timeout,
                                   DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status)),
    m_timeout(timeout),
    m_is_activated(false),
    m_was_stream_aborted(false),
    m_index_of_source(),
    m_was_source_called(source_count, false),
    m_buffers_for_action()
{
    m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
    m_sources.reserve(source_count);
    for (uint32_t i = 0; i < source_count; i++) {
        m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
        m_index_of_source[&m_sources[i]] = i;
    }
}

hailo_status BaseDemuxElement::run_push(PipelineBuffer &&/*buffer*/)
{
    return HAILO_NOT_IMPLEMENTED;
}

Expected<PipelineBuffer> BaseDemuxElement::run_pull(PipelineBuffer &&optional, const PipelinePad &source)
{
    CHECK_AS_EXPECTED(!optional, HAILO_INVALID_ARGUMENT, "Optional buffer is not allowed in demux element!");

    // TODO: should we lock here? or only right before wait_for?
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_is_activated) {
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }

    m_was_source_called[m_index_of_source[&source]] = true;
    if (were_all_sinks_called()) {
        auto input = next_pad().run_pull();
        if (HAILO_STREAM_INTERNAL_ABORT == input.status()) {
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
        auto cv_status = m_cv.wait_for(lock, m_timeout);
        CHECK_AS_EXPECTED(std::cv_status::timeout != cv_status, HAILO_TIMEOUT, "Waiting for other threads in demux {} has reached a timeout!", name());

        if (m_was_stream_aborted) {
            return make_unexpected(HAILO_STREAM_INTERNAL_ABORT);
        }

        // We check if the element is not activated in case notify_all() was called from deactivate()
        if (!m_is_activated) {
            return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
        }
    }

    assert(m_index_of_source[&source] < m_buffers_for_action.size());
    return std::move(m_buffers_for_action[m_index_of_source[&source]]);
}

bool BaseDemuxElement::were_all_sinks_called()
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
    return PipelineElement::execute_activate();
}

hailo_status BaseDemuxElement::execute_deactivate()
{
    if (!m_is_activated) {
        return HAILO_SUCCESS;
    }
    m_is_activated = false;

    // deactivate should be called before mutex acquire and notify_all because it is possible that all queues are waiting on
    // the run_pull of the source (HwRead) and the mutex is already acquired so this would prevent a timeout error
    hailo_status status = PipelineElement::execute_deactivate();

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

hailo_status BaseDemuxElement::execute_post_deactivate()
{
    for (uint32_t i = 0; i < m_was_source_called.size(); i++) {
        m_was_source_called[i] = false;
    }
    return PipelineElement::execute_post_deactivate();
}

hailo_status BaseDemuxElement::execute_abort()
{
    m_was_stream_aborted = true;
    return PipelineElement::execute_abort();
}

PipelinePad &BaseDemuxElement::next_pad()
{
    // Note: The next elem to be run is upstream from this elem (i.e. buffers are pulled)
    return *m_sinks[0].prev();
}

hailo_status BaseDemuxElement::set_timeout(std::chrono::milliseconds timeout)
{
    m_timeout = timeout;
    return HAILO_SUCCESS;
}

std::vector<PipelinePad*> BaseDemuxElement::execution_pads()
{
    std::vector<PipelinePad*> result{&next_pad()};
    return result;
}


} /* namespace hailort */
