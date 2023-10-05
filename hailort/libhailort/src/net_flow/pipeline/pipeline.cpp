/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline.cpp
 * @brief Implemention of the pipeline
 **/

#include "common/utils.hpp"
#include "common/runtime_statistics_internal.hpp"
#include "common/os_utils.hpp"

#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include "net_flow/pipeline/pipeline.hpp"
#include <cstdint>

namespace hailort
{

#define NUMBER_OF_PLANES_NV12_NV21 2
#define NUMBER_OF_PLANES_I420 3

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
    m_pool(nullptr),
    m_view(),
    m_exec_done([](CompletionInfoAsyncInferInternal /*completion_info*/) {}),
    m_metadata(),
    m_is_user_buffer(false),
    m_action_status(HAILO_SUCCESS)
{}

PipelineBuffer::PipelineBuffer(hailo_status action_status) :
    m_type(Type::DATA),
    m_pool(nullptr),
    m_view(),
    m_exec_done([](CompletionInfoAsyncInferInternal /*completion_info*/) {}),
    m_metadata(),
    m_is_user_buffer(false),
    m_action_status(action_status)
{}

PipelineBuffer::PipelineBuffer(MemoryView view, bool is_user_buffer, BufferPoolPtr pool, bool should_measure, hailo_status action_status) :
    m_type(Type::DATA),
    m_pool(pool),
    m_view(view),
    m_exec_done([](CompletionInfoAsyncInferInternal /*completion_info*/) {}),
    m_metadata(Metadata(add_timestamp(should_measure))),
    m_is_user_buffer(is_user_buffer),
    m_action_status(action_status)
{}

PipelineBuffer::PipelineBuffer(MemoryView view, const TransferDoneCallbackAsyncInfer &exec_done, bool is_user_buffer, BufferPoolPtr pool, bool should_measure,
    hailo_status action_status) :
    m_type(Type::DATA),
    m_pool(pool),
    m_view(view),
    m_exec_done(exec_done),
    m_metadata(Metadata(add_timestamp(should_measure))),
    m_is_user_buffer(is_user_buffer),
    m_action_status(action_status)
{}

PipelineBuffer::PipelineBuffer(hailo_pix_buffer_t buffer) :
    m_type(Type::DATA),
    m_pool(nullptr),
    m_view(),
    m_metadata(),
    m_is_user_buffer(false)
{
    set_additional_data(std::make_shared<PixBufferPipelineData>(buffer));
}

PipelineBuffer::PipelineBuffer(PipelineBuffer &&other) :
    m_type(other.m_type),
    m_pool(std::move(other.m_pool)),
    m_view(std::move(other.m_view)),
    m_exec_done(std::move(other.m_exec_done)),
    m_metadata(std::move(other.m_metadata)),
    m_is_user_buffer(std::move(other.m_is_user_buffer)),
    m_action_status(std::move(other.m_action_status))
{}

PipelineBuffer &PipelineBuffer::operator=(PipelineBuffer &&other)
{
    m_type = other.m_type;
    m_pool = std::move(other.m_pool);
    m_view = std::move(other.m_view);
    m_exec_done = std::move(other.m_exec_done);
    m_metadata = std::move(other.m_metadata);
    m_is_user_buffer = std::move(other.m_is_user_buffer);
    m_action_status = std::move(other.m_action_status);
    return *this;
}

PipelineBuffer::~PipelineBuffer()
{
    if ((nullptr != m_pool) && (!m_is_user_buffer)) {
        hailo_status status = m_pool->release_buffer(m_view);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Releasing buffer in buffer pool failed! status = {}", status);
        }
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

Expected<hailo_pix_buffer_t> PipelineBuffer::as_hailo_pix_buffer(hailo_format_order_t order)
{
    auto pix_buffer = get_metadata().get_additional_data<PixBufferPipelineData>();

    if (nullptr == pix_buffer) {
        switch(order){
            case HAILO_FORMAT_ORDER_NV12:
            case HAILO_FORMAT_ORDER_NV21: {
                CHECK_AS_EXPECTED(0 == (m_view.size() % 3), HAILO_INVALID_ARGUMENT, "buffer size must be divisible by 3");

                auto y_plane_size = m_view.size() * 2 / 3;
                auto uv_plane_size = m_view.size() * 1 / 3;

                auto uv_data_ptr = reinterpret_cast<uint8_t*>(m_view.data()) + y_plane_size;

                hailo_pix_buffer_plane_t y {uint32_t(y_plane_size), uint32_t(y_plane_size), m_view.data()};
                hailo_pix_buffer_plane_t uv {uint32_t(uv_plane_size), uint32_t(uv_plane_size), uv_data_ptr};
                hailo_pix_buffer_t buffer{0, {y, uv}, NUMBER_OF_PLANES_NV12_NV21};

                return buffer;
            }
            case HAILO_FORMAT_ORDER_I420: {
                CHECK_AS_EXPECTED(0 == (m_view.size() % 6), HAILO_INVALID_ARGUMENT, "buffer size must be divisible by 6");

                auto y_plane_size = m_view.size() * 2 / 3;
                auto u_plane_size = m_view.size() * 1 / 6;
                auto v_plane_size = m_view.size() * 1 / 6;

                auto u_data_ptr = (char*)m_view.data() + y_plane_size;
                auto v_data_ptr = u_data_ptr + u_plane_size;

                hailo_pix_buffer_plane_t y {uint32_t(y_plane_size), uint32_t(y_plane_size), m_view.data()};
                hailo_pix_buffer_plane_t u {uint32_t(u_plane_size), uint32_t(u_plane_size), u_data_ptr};
                hailo_pix_buffer_plane_t v {uint32_t(v_plane_size), uint32_t(v_plane_size), v_data_ptr};
                hailo_pix_buffer_t buffer{0, {y, u, v}, NUMBER_OF_PLANES_I420};

                return buffer;
            }
            default: {
                CHECK_AS_EXPECTED(false, HAILO_INTERNAL_FAILURE, "unsupported format order");
            }
        }
    } else {
        uint32_t expected_number_of_planes;
        switch(order){
            case HAILO_FORMAT_ORDER_NV12:
            case HAILO_FORMAT_ORDER_NV21: {
                expected_number_of_planes = NUMBER_OF_PLANES_NV12_NV21;
                break;
            }
            case HAILO_FORMAT_ORDER_I420: {
                expected_number_of_planes = NUMBER_OF_PLANES_I420;
                break;
            }
            default: {
                CHECK_AS_EXPECTED(false, HAILO_INTERNAL_FAILURE, "unsupported format order");
            }
        }
        CHECK_AS_EXPECTED(pix_buffer->m_pix_buffer.number_of_planes == expected_number_of_planes, HAILO_INVALID_ARGUMENT,
            "number of planes in the pix buffer ({}) doesn't match the order ({})",
            pix_buffer->m_pix_buffer.number_of_planes, expected_number_of_planes);

        return std::move(pix_buffer->m_pix_buffer);
    }
}

void PipelineBuffer::set_metadata(Metadata &&val)
{
    m_metadata = std::move(val);
}

TransferDoneCallbackAsyncInfer PipelineBuffer::get_exec_done_cb() const
{
    return m_exec_done;
}

PipelineTimePoint PipelineBuffer::add_timestamp(bool should_measure)
{
    return should_measure ? std::chrono::steady_clock::now() : PipelineTimePoint{};
}

hailo_status PipelineBuffer::action_status()
{
    return m_action_status;
}

void PipelineBuffer::set_action_status(hailo_status status)
{
    m_action_status = status;
}

Expected<BufferPoolPtr> BufferPool::create(size_t buffer_size, size_t buffer_count, EventPtr shutdown_event,
                                           hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags,
                                           bool is_empty, bool is_dma_able)
{
    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((elem_flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }
    const bool measure_vstream_latency = (vstream_flags & HAILO_VSTREAM_STATS_MEASURE_LATENCY) != 0;

    auto free_mem_views = SpscQueue<MemoryView>::create(buffer_count, shutdown_event, BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT);
    CHECK_EXPECTED(free_mem_views);

    auto done_cbs = SpscQueue<TransferDoneCallbackAsyncInfer>::create(buffer_count, shutdown_event, BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT);
    CHECK_EXPECTED(done_cbs);

    std::vector<Buffer> buffers;
    if (!is_empty) {
        buffers.reserve(buffer_count);
        for (size_t i = 0; i < buffer_count; i++) {
            BufferStorageParams buffer_storage_params;
            if (is_dma_able) {
                buffer_storage_params = BufferStorageParams::create_dma();
            }
            auto buffer = Buffer::create(buffer_size, buffer_storage_params);
            CHECK_EXPECTED(buffer);

            auto status = free_mem_views->enqueue(MemoryView(buffer.value()));
            CHECK_SUCCESS_AS_EXPECTED(status);

            buffers.emplace_back(buffer.release());
        }
    }

    auto buffer_pool_ptr = make_shared_nothrow<BufferPool>(buffer_size, is_empty, measure_vstream_latency, std::move(buffers),
        free_mem_views.release(), done_cbs.release(), std::move(queue_size_accumulator), buffer_count);
    CHECK_AS_EXPECTED(nullptr != buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return buffer_pool_ptr;
}

BufferPool::BufferPool(size_t buffer_size, bool is_holding_user_buffers, bool measure_vstream_latency, std::vector<Buffer> &&buffers,
        SpscQueue<MemoryView> &&free_mem_views, SpscQueue<TransferDoneCallbackAsyncInfer> &&done_cbs, AccumulatorPtr &&queue_size_accumulator,
        size_t max_buffer_count) :
    m_buffer_size(buffer_size),
    m_is_holding_user_buffers(is_holding_user_buffers),
    m_max_buffer_count(max_buffer_count),
    m_measure_vstream_latency(measure_vstream_latency),
    m_buffers(std::move(buffers)),
    m_free_mem_views(std::move(free_mem_views)),
    m_done_cbs(std::move(done_cbs)),
    m_queue_size_accumulator(std::move(queue_size_accumulator))
{}

size_t BufferPool::buffer_size()
{
    return m_buffer_size;
}

hailo_status BufferPool::enqueue_buffer(MemoryView mem_view)
{
    CHECK(mem_view.size() == m_buffer_size, HAILO_INTERNAL_FAILURE, "Buffer size is not the same as expected for pool! ({} != {})", mem_view.size(), m_buffer_size);

    auto status = m_free_mem_views.enqueue(mem_view);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status BufferPool::enqueue_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done)
{
    auto status = enqueue_buffer(mem_view);
    CHECK_SUCCESS(status);

    status = m_done_cbs.enqueue(exec_done);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

bool BufferPool::is_full() {
    return (m_max_buffer_count - m_buffers.size() == 0);
}

hailo_status BufferPool::allocate_buffers(bool is_dma_able)
{
    m_is_holding_user_buffers = false;
    size_t buffer_count = m_max_buffer_count - m_buffers.size();
    for (size_t i = 0; i < buffer_count; i++) {
        BufferStorageParams buffer_storage_params;
        if (is_dma_able) {
            buffer_storage_params = BufferStorageParams::create_dma();
        }
        auto buffer = Buffer::create(m_buffer_size, buffer_storage_params);
        CHECK_EXPECTED_AS_STATUS(buffer);

        auto status = m_free_mem_views.enqueue(MemoryView(buffer.value()));
        CHECK_SUCCESS(status);
        m_buffers.emplace_back(buffer.release());
    }
    return HAILO_SUCCESS;
}

Expected<PipelineBuffer> BufferPool::acquire_buffer(std::chrono::milliseconds timeout)
{
    auto mem_view = acquire_free_mem_view(timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == mem_view.status()) {
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }
    CHECK_EXPECTED(mem_view);

    if (m_is_holding_user_buffers) {
        auto done_cb = acquire_on_done_cb(timeout);
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == done_cb.status()) {
            return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
        }
        CHECK_EXPECTED(done_cb);

        return PipelineBuffer(mem_view.release(), done_cb.release(), m_is_holding_user_buffers, shared_from_this(), m_measure_vstream_latency);
    }

    return PipelineBuffer(mem_view.release(), m_is_holding_user_buffers, shared_from_this(), m_measure_vstream_latency);
}

Expected<std::shared_ptr<PipelineBuffer>> BufferPool::acquire_buffer_ptr(std::chrono::milliseconds timeout)
{
    auto mem_view = acquire_free_mem_view(timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == mem_view.status()) {
        return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
    }
    CHECK_EXPECTED(mem_view);

    std::shared_ptr<PipelineBuffer> ptr = nullptr;
    if (m_is_holding_user_buffers) {
        auto done_cb = acquire_on_done_cb(timeout);
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == done_cb.status()) {
            return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
        }
        CHECK_EXPECTED(done_cb);

        ptr = make_shared_nothrow<PipelineBuffer>(mem_view.release(), done_cb.release(), m_is_holding_user_buffers, shared_from_this(), m_measure_vstream_latency);
    } else {
        ptr = make_shared_nothrow<PipelineBuffer>(mem_view.release(), m_is_holding_user_buffers, shared_from_this(), m_measure_vstream_latency);
    }

    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);
    return ptr;
}

Expected<MemoryView> BufferPool::acquire_free_mem_view(std::chrono::milliseconds timeout)
{
    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_free_mem_views.size_approx()));
    }

    auto mem_view = m_free_mem_views.dequeue(timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == mem_view.status()) {
        return make_unexpected(mem_view.status());
    }
    else if (HAILO_TIMEOUT == mem_view.status()) {
        LOGGER__WARNING(
            "Failed to acquire buffer because the buffer pool is empty. This could be caused by uneven reading and writing speeds, with a short user-defined timeout. (timeout={}ms)",
            timeout.count());
        return make_unexpected(mem_view.status());
    }
    CHECK_EXPECTED(mem_view);

    return mem_view.release();
}

Expected<TransferDoneCallbackAsyncInfer> BufferPool::acquire_on_done_cb(std::chrono::milliseconds timeout)
{
    auto done_cb = m_done_cbs.dequeue(timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == done_cb.status()) {
        return make_unexpected(done_cb.status());
    }
    else if (HAILO_TIMEOUT == done_cb.status()) {
        LOGGER__WARNING(
            "Failed to acquire buffer because the buffer pool is empty. This could be caused by uneven reading and writing speeds, with a short user-defined timeout. (timeout={}ms)",
            timeout.count());
        return make_unexpected(done_cb.status());
    }
    CHECK_EXPECTED(done_cb);

    return done_cb.release();
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
    CHECK_EXPECTED(acquired_buffer, "Failed to acquire buffer with status={}", acquired_buffer.status());
    return acquired_buffer.release();
}

hailo_status BufferPool::release_buffer(MemoryView mem_view)
{
    std::unique_lock<std::mutex> lock(m_release_buffer_mutex);
    // This can be called after the shutdown event was signaled so we ignore it here
    return m_free_mem_views.enqueue(std::move(mem_view), true);
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

hailo_status PipelinePad::post_deactivate(bool should_clear_abort)
{
    return m_element.post_deactivate(should_clear_abort);
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

hailo_status PipelinePad::clear_abort()
{
    return m_element.clear_abort();
}

hailo_status PipelinePad::run_push(PipelineBuffer &&buffer)
{
    if (m_push_complete_callback) {
        auto metadata = buffer.get_metadata();
        const auto status = m_element.run_push(std::move(buffer), *this);
        m_push_complete_callback(metadata);
        return status;
    }

    return m_element.run_push(std::move(buffer), *this);
}

void PipelinePad::run_push_async(PipelineBuffer &&buffer)
{
    if (m_push_complete_callback) {
        auto metadata = buffer.get_metadata();
        m_element.run_push_async(std::move(buffer), *this);
        m_push_complete_callback(metadata);
        return;
    }

    return m_element.run_push_async(std::move(buffer), *this);
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
                             std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                             PipelineDirection pipeline_direction) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction)
{
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
}

PipelinePad &SourceElement::source()
{
    return m_sources[0];
}

SinkElement::SinkElement(const std::string &name, DurationCollector &&duration_collector,
                         std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                         PipelineDirection pipeline_direction) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction)
{
    m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
}

PipelinePad &SinkElement::sink()
{
    return m_sinks[0];
}

IntermediateElement::IntermediateElement(const std::string &name, DurationCollector &&duration_collector,
                                         std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                         PipelineDirection pipeline_direction) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction)
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
                                 std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                 PipelineDirection pipeline_direction) :
    PipelineObject(name),
    m_duration_collector(std::move(duration_collector)),
    m_pipeline_status(std::move(pipeline_status)),
    m_sinks(),
    m_sources(),
    m_pipeline_direction(pipeline_direction)
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

void PipelineElement::set_on_cant_pull_callback(std::function<void()> callback)
{
    m_cant_pull_callback = callback;
}

void PipelineElement::set_on_can_pull_callback(std::function<void()> callback)
{
    m_can_pull_callback = callback;
}

hailo_status PipelineElement::enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name)
{
    (void)mem_view;
    (void)exec_done;
    (void)source_name;
    LOGGER__ERROR("enqueue_execution_buffer is not implemented for {}!", name());
    return HAILO_NOT_IMPLEMENTED;
};

hailo_status PipelineElement::enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done)
{
    return enqueue_execution_buffer(mem_view, exec_done, "");
};

hailo_status PipelineElement::fill_buffer_pools(bool is_dma_able)
{
    (void)is_dma_able;
    return HAILO_NOT_IMPLEMENTED;
}

Expected<bool> PipelineElement::are_buffer_pools_full()
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status PipelineElement::activate()
{
    return execute_activate();
}

hailo_status PipelineElement::deactivate()
{
    return execute_deactivate();
}

hailo_status PipelineElement::post_deactivate(bool should_clear_abort)
{
    return execute_post_deactivate(should_clear_abort);
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

hailo_status PipelineElement::clear_abort()
{
    return execute_clear_abort();
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

hailo_status PipelineElement::execute_post_deactivate(bool should_clear_abort)
{
    return execute([&](auto *pad){ return pad->post_deactivate(should_clear_abort); });
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

hailo_status PipelineElement::execute_clear_abort()
{
    return execute([&](auto *pad){ return pad->clear_abort(); });
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

void PipelineElement::handle_non_recoverable_async_error(hailo_status error_status)
{
    if (HAILO_SUCCESS != m_pipeline_status->load()){
        LOGGER__ERROR("Non-recoverable Async Infer Pipeline error. status error code: {}", error_status);
        m_pipeline_status->store(error_status);
    }
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
                             std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                             PipelineDirection pipeline_direction, BufferPoolPtr buffer_pool,
                             std::chrono::milliseconds timeout) :
    IntermediateElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction),
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
            next_pad().run_push_async(PipelineBuffer(buffer_from_pool.status()));
        } else {
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

Expected<bool> FilterElement::are_buffer_pools_full()
{
    return m_pool->is_full();
}

hailo_status FilterElement::fill_buffer_pools(bool is_dma_able)
{
    auto status = m_pool->allocate_buffers(is_dma_able);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
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
                                   Event &&activation_event, Event &&deactivation_event, PipelineDirection pipeline_direction) :
    IntermediateElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction),
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
    hailo_status status = PipelineElement::execute_activate();
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

    return PipelineElement::execute_post_deactivate(should_clear_abort);
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

hailo_status BaseQueueElement::enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name)
{
    (void)source_name;
    return m_sinks[0].prev()->element().enqueue_execution_buffer(mem_view, exec_done, m_sinks[0].prev()->name());
}

Expected<bool> BaseQueueElement::are_buffer_pools_full()
{
    return m_sinks[0].prev()->element().are_buffer_pools_full();
}

hailo_status BaseQueueElement::fill_buffer_pools(bool is_dma_able)
{
    return m_sinks[0].prev()->element().fill_buffer_pools(is_dma_able);
}

hailo_status PushQueueElement::execute_abort()
{
    auto status = m_shutdown_event->reset();
    CHECK_SUCCESS(status);
    m_pipeline_status->store(HAILO_STREAM_ABORTED_BY_USER);
    status = PipelineElement::execute_abort();
    CHECK_SUCCESS(status);
    return m_activation_event.signal();
}

hailo_status BaseQueueElement::execute_clear_abort()
{
    auto status = m_shutdown_event->reset();
    CHECK_SUCCESS(status);
    m_pipeline_status->store(HAILO_SUCCESS);
    return PipelineElement::execute_clear_abort();
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

    auto queue_ptr = make_shared_nothrow<PushQueueElement>(queue.release(), shutdown_event, name, timeout,
        duration_collector.release(), std::move(queue_size_accumulator), std::move(pipeline_status),
        activation_event.release(), deactivation_event.release(), pipeline_direction);
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PushQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->name());

    return queue_ptr;
}

Expected<std::shared_ptr<PushQueueElement>> PushQueueElement::create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction)
{
    return PushQueueElement::create(name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.queue_size, vstream_params.pipeline_elements_stats_flags, shutdown_event, pipeline_status, pipeline_direction);
}

PushQueueElement::PushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
                                   std::chrono::milliseconds timeout, DurationCollector &&duration_collector, 
                                   AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   Event &&activation_event, Event &&deactivation_event, PipelineDirection pipeline_direction, bool should_start_thread) :
    BaseQueueElement(std::move(queue), shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
                     std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event), pipeline_direction)
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
        auto deactivation_status = PipelineElement::execute_deactivate();
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

    auto queue_ptr = make_shared_nothrow<AsyncPushQueueElement>(queue.release(), shutdown_event, name, timeout,
        duration_collector.release(), std::move(queue_size_accumulator), std::move(pipeline_status),
        activation_event.release(), deactivation_event.release(), pipeline_direction);
    CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY, "Creating PushQueueElement {} failed!", name);

    LOGGER__INFO("Created {}", queue_ptr->name());

    return queue_ptr;
}

Expected<std::shared_ptr<AsyncPushQueueElement>> AsyncPushQueueElement::create(const std::string &name, const ElementBuildParams &build_params,
    PipelineDirection pipeline_direction)
{
    return AsyncPushQueueElement::create(name, build_params.timeout, build_params.buffer_pool_size,
            build_params.elem_stats_flags, build_params.shutdown_event, build_params.pipeline_status, pipeline_direction);
}

AsyncPushQueueElement::AsyncPushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
                                   std::chrono::milliseconds timeout, DurationCollector &&duration_collector, 
                                   AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   Event &&activation_event, Event &&deactivation_event, PipelineDirection pipeline_direction) :
    PushQueueElement(std::move(queue), shutdown_event, name, timeout, std::move(duration_collector), std::move(queue_size_accumulator),
                     std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event), pipeline_direction, false)
{
    start_thread();
}

void AsyncPushQueueElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    if (HAILO_SUCCESS != buffer.action_status()) {
        auto status = m_queue.enqueue(std::move(buffer), m_timeout);
        if (HAILO_SUCCESS != status) {
            handle_non_recoverable_async_error(status);
        }
        return;
    }
    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_queue.size_approx()));
    }

    auto status = m_queue.enqueue(std::move(buffer), m_timeout);
    if (HAILO_SUCCESS != status && HAILO_SHUTDOWN_EVENT_SIGNALED != status) {
        handle_non_recoverable_async_error(status);
    }
}

void AsyncPushQueueElement::start_thread()
{
    m_thread = std::thread([this] () {
        OsUtils::set_current_thread_name(thread_name());
        while (m_is_thread_running.load()) {
            auto status = m_activation_event.wait(INIFINITE_TIMEOUT());
            if (HAILO_SUCCESS != status) {
                handle_non_recoverable_async_error(status);
            }

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
                if (HAILO_SUCCESS != status) {
                    handle_non_recoverable_async_error(status);
                }

                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_is_run_in_thread_running = false;
                }
                m_cv.notify_all();
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
    switch (buffer.status()) {
    case HAILO_SHUTDOWN_EVENT_SIGNALED:
        break;
    
    case HAILO_SUCCESS:
        next_pad().run_push_async(buffer.release());
        break;

    default:
        next_pad().run_push_async(PipelineBuffer(buffer.status()));
    }
    return buffer.status();
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
                     std::move(pipeline_status), std::move(activation_event), std::move(deactivation_event), pipeline_direction)
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
                               BufferPoolPtr buffer_pool, PipelineDirection pipeline_direction) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction),
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
        for (auto &input_buffer : m_input_buffers) {
            if (HAILO_SUCCESS != input_buffer.second.action_status()) {
                auto acquired_buffer = m_pool->get_available_buffer(PipelineBuffer(), m_timeout);
                if (HAILO_SUCCESS == acquired_buffer.status()) {
                    acquired_buffer->set_action_status(input_buffer.second.action_status());
                    m_next_pads[0]->run_push_async(acquired_buffer.release());
                } else {
                    handle_non_recoverable_async_error(acquired_buffer.status());
                }
                return;
            }
        }
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

        for (const auto &curr_sink : m_sinks) {
            m_sink_has_arrived[curr_sink.name()] = false;
        }
        m_input_buffers.clear();

        // Manual unlocking is done before notifying, to avoid waking up the waiting thread only to block again
        lock.unlock();
        m_cv.notify_all();
    } else {
        auto cv_status = m_cv.wait_for(lock, m_timeout);
        if (std::cv_status::timeout == cv_status) {
            LOGGER__ERROR("Waiting for other threads in BaseMuxElement {} has reached a timeout (timeout={}ms)", name(), m_timeout.count());
            handle_non_recoverable_async_error(HAILO_TIMEOUT);
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

Expected<bool> BaseMuxElement::are_buffer_pools_full()
{
    return m_pool->is_full();
}

hailo_status BaseMuxElement::fill_buffer_pools(bool is_dma_able)
{
    auto status = m_pool->allocate_buffers(is_dma_able);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

BaseDemuxElement::BaseDemuxElement(size_t source_count, const std::string &name, std::chrono::milliseconds timeout,
                                   DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   std::vector<BufferPoolPtr> pools, PipelineDirection pipeline_direction) :
    PipelineElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction),
    m_timeout(timeout),
    m_pools(pools),
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
        m_index_of_source[m_sources[i].name()] = i;
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

    for (const auto pad : execution_pads()) {
        assert(m_index_of_source.count(pad->prev()->name()) > 0);
        auto source_index = m_index_of_source[pad->prev()->name()];

        hailo_status status = pad->run_push(std::move(outputs.value()[source_index]));
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
        for (const auto pad : execution_pads()) {
            auto source_index = m_index_of_source[pad->prev()->name()];
            auto acquired_buffer = m_pools[source_index]->acquire_buffer(m_timeout);
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

    for (const auto pad : execution_pads()) {
        assert(m_index_of_source.count(pad->prev()->name()) > 0);
        auto source_index = m_index_of_source[pad->prev()->name()];
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

    m_was_source_called[m_index_of_source[source.name()]] = true;

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
            return !m_was_source_called[m_index_of_source[source.name()]] || m_was_stream_aborted || !m_is_activated;
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

    assert(m_index_of_source[source.name()] < m_buffers_for_action.size());
    return std::move(m_buffers_for_action[m_index_of_source[source.name()]]);
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

hailo_status BaseDemuxElement::execute_post_deactivate(bool should_clear_abort)
{
    for (uint32_t i = 0; i < m_was_source_called.size(); i++) {
        m_was_source_called[i] = false;
    }
    return PipelineElement::execute_post_deactivate(should_clear_abort);
}

hailo_status BaseDemuxElement::execute_abort()
{
    auto status = PipelineElement::execute_abort();
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
    auto pool_id = m_index_of_source.at(source_name);
    auto status = m_pools[pool_id]->enqueue_buffer(mem_view, exec_done);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<bool> BaseDemuxElement::are_buffer_pools_full()
{
    for (const auto &pool : m_pools) {
        if (pool->is_full()) {
            return true;
        }
    }
    return false;
}

hailo_status BaseDemuxElement::fill_buffer_pool(bool is_dma_able, size_t pool_id) {
    auto status = m_pools[pool_id]->allocate_buffers(is_dma_able);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status BaseDemuxElement::fill_buffer_pools(bool is_dma_able) {
    for (auto &pool : m_pools) {
        auto status = pool->allocate_buffers(is_dma_able);
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
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
