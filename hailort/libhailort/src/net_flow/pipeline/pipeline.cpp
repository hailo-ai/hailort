/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline.cpp
 * @brief Implemention of the pipeline
 **/

#include "common/utils.hpp"
#include "common/runtime_statistics_internal.hpp"
#include "common/os_utils.hpp"
#include "utils/dma_buffer_utils.hpp"

#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/vdevice.hpp"
#include "net_flow/pipeline/pipeline.hpp"
#include "utils/buffer_storage.hpp"

#include <cstdint>

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

PipelineBuffer::PipelineBuffer(Type type) :
    m_type(type),
    m_view(),
    m_exec_done([](hailo_status){}),
    m_metadata(),
    m_is_user_buffer(false),
    m_should_call_exec_done(true),
    m_action_status(HAILO_SUCCESS),
    m_buffer_type(BufferType::UNINITIALIZED)
{
}

PipelineBuffer::PipelineBuffer(hailo_status action_status, const TransferDoneCallbackAsyncInfer &exec_done) :
    m_type(Type::DATA),
    m_view(),
    m_exec_done(exec_done),
    m_metadata(),
    m_is_user_buffer(false),
    m_should_call_exec_done(true),
    m_action_status(action_status),
    m_buffer_type(BufferType::UNINITIALIZED)
{
}

PipelineBuffer::PipelineBuffer(MemoryView view, const TransferDoneCallbackAsyncInfer &exec_done, hailo_status action_status,
    bool is_user_buffer, PipelineBufferPoolWeakPtr pool, bool should_measure) :
    m_type(Type::DATA),
    m_pool(pool),
    m_view(view),
    m_metadata(Metadata(add_timestamp(should_measure))),
    m_is_user_buffer(is_user_buffer),
    m_should_call_exec_done(true),
    m_action_status(action_status),
    m_buffer_type(BufferType::VIEW)
{
    std::unique_lock<std::mutex> lock(m_exec_done_mutex);
    m_exec_done = [pool = m_pool, mem_view = m_view, is_user_buffer = m_is_user_buffer, exec_done = exec_done](hailo_status status){
        exec_done(status);
        if (auto buffer_pool = pool.lock()) {
            return_buffer_to_pool(buffer_pool, mem_view, is_user_buffer);
        }
    };
}

PipelineBuffer::PipelineBuffer(hailo_pix_buffer_t buffer, const TransferDoneCallbackAsyncInfer &exec_done) :
    m_type(Type::DATA),
    m_view(),
    m_exec_done(exec_done),
    m_metadata(),
    m_is_user_buffer(false),
    m_should_call_exec_done(true),
    m_action_status(HAILO_SUCCESS),
    m_buffer_type(BufferType::PIX_BUFFER)
{
    set_additional_data(std::make_shared<PixBufferPipelineData>(buffer));
}

PipelineBuffer::PipelineBuffer(hailo_dma_buffer_t dma_buffer, const TransferDoneCallbackAsyncInfer &exec_done, hailo_status action_status,
    bool is_user_buffer, PipelineBufferPoolWeakPtr pool, bool should_measure) :
    m_type(Type::DATA),
    m_pool(pool),
    m_view(),
    m_metadata(Metadata(add_timestamp(should_measure))),
    m_is_user_buffer(is_user_buffer),
    m_should_call_exec_done(true),
    m_action_status(action_status),
    m_buffer_type(BufferType::DMA_BUFFER)
{
    std::unique_lock<std::mutex> lock(m_exec_done_mutex);
    set_additional_data(std::make_shared<DmaBufferPipelineData>(dma_buffer));
    m_exec_done = [pool = m_pool, dma_buffer = get_metadata().get_additional_data<DmaBufferPipelineData>(), is_user_buffer = m_is_user_buffer, exec_done = exec_done](hailo_status status){
        exec_done(status);
        if (auto buffer_pool = pool.lock()) {
            return_buffer_to_pool(buffer_pool, dma_buffer->m_dma_buffer, is_user_buffer);
        }
    };
}

PipelineBuffer::PipelineBuffer(PipelineBuffer &&other) :
    m_type(other.m_type),
    m_pool(std::move(other.m_pool)),
    m_view(std::move(other.m_view)),
    m_exec_done(std::move(other.m_exec_done)),
    m_metadata(std::move(other.m_metadata)),
    m_is_user_buffer(std::move(other.m_is_user_buffer)),
    m_should_call_exec_done(std::exchange(other.m_should_call_exec_done, false)),
    m_action_status(std::move(other.m_action_status)),
    m_buffer_type(other.m_buffer_type)
{}

PipelineBuffer &PipelineBuffer::operator=(PipelineBuffer &&other)
{
    m_type = other.m_type;
    m_pool = std::move(other.m_pool);
    m_view = std::move(other.m_view);
    m_exec_done = std::move(other.m_exec_done);
    m_metadata = std::move(other.m_metadata);
    m_is_user_buffer = std::move(other.m_is_user_buffer);
    m_should_call_exec_done = std::exchange(other.m_should_call_exec_done, false);
    m_action_status = std::move(other.m_action_status);
    m_buffer_type = std::move(other.m_buffer_type);
    return *this;
}

PipelineBuffer::~PipelineBuffer()
{
    call_exec_done();
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
    if (BufferType::DMA_BUFFER == m_buffer_type) {
        auto dma_buffer = get_metadata().get_additional_data<DmaBufferPipelineData>();
        return dma_buffer->m_dma_buffer.size;
    } else if (BufferType::PIX_BUFFER == m_buffer_type) {
        auto pix_buffer = get_metadata().get_additional_data<PixBufferPipelineData>();
        size_t size = 0;
        for (uint32_t i = 0; i < pix_buffer->m_pix_buffer.number_of_planes; i++) {
            size += pix_buffer->m_pix_buffer.planes[i].plane_size;
        }
        return size;
    } else {
        return m_view.size();
    }
}

Expected<MemoryView> PipelineBuffer::as_view(BufferProtection dma_buffer_protection)
{
    if (BufferType::DMA_BUFFER == m_buffer_type) {
        assert(BufferProtection::NONE != dma_buffer_protection);
        auto status = set_dma_buf_as_memview(dma_buffer_protection);
        CHECK_SUCCESS_AS_EXPECTED(status);
    } else if (BufferType::PIX_BUFFER == m_buffer_type) {
        LOGGER__ERROR("Can't call as_view for buffer of type pix_buffer.");
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    auto mem_view = m_view;

    return mem_view;
}

PipelineBuffer::Type PipelineBuffer::get_type() const
{
    return m_type;
}

PipelineBuffer::Metadata PipelineBuffer::get_metadata() const
{
    return m_metadata;
}

BufferType PipelineBuffer::get_buffer_type() const
{
    return m_buffer_type;
}

Expected<hailo_pix_buffer_t> PipelineBuffer::as_hailo_pix_buffer(hailo_format_order_t order)
{
    auto pix_buffer = get_metadata().get_additional_data<PixBufferPipelineData>();

    if (nullptr == pix_buffer) {
        TRY(auto mem_view, as_view(BufferProtection::READ));
        return HailoRTCommon::as_hailo_pix_buffer(mem_view, order);
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

void PipelineBuffer::set_metadata_start_time(PipelineTimePoint val)
{
    m_metadata.set_start_time(val);
}

PipelineTimePoint PipelineBuffer::add_timestamp(bool should_measure)
{
    return should_measure ? std::chrono::steady_clock::now() : PipelineTimePoint{};
}

void PipelineBuffer::return_buffer_to_pool(PipelineBufferPoolWeakPtr buffer_pool_weak_ptr, MemoryView mem_view, bool is_user_buffer)
{
    if (is_user_buffer) {
        return;
    }

    if (auto buffer_pool_ptr = buffer_pool_weak_ptr.lock() ) {
        auto pipeline_buffer = PipelineBuffer(mem_view, [](hailo_status){}, HAILO_SUCCESS, false, buffer_pool_ptr, buffer_pool_ptr->should_measure_vstream_latency());
        hailo_status status = buffer_pool_ptr->return_buffer_to_pool(std::move(pipeline_buffer));
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Releasing buffer in buffer pool failed! status = {}", status);
        }
    }
}

void PipelineBuffer::return_buffer_to_pool(PipelineBufferPoolWeakPtr buffer_pool_weak_ptr, hailo_dma_buffer_t dma_buffer, bool is_user_buffer)
{
    if (is_user_buffer) {
        return;
    }

    if (auto buffer_pool_ptr = buffer_pool_weak_ptr.lock() ) {
        auto pipeline_buffer = PipelineBuffer(dma_buffer, [](hailo_status){}, HAILO_SUCCESS, false, buffer_pool_ptr, buffer_pool_ptr->should_measure_vstream_latency());
        hailo_status status = buffer_pool_ptr->return_buffer_to_pool(std::move(pipeline_buffer));
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Releasing buffer in buffer pool failed! status = {}", status);
        }
    }
}

hailo_status PipelineBuffer::action_status()
{
    return m_action_status;
}

void PipelineBuffer::set_action_status(hailo_status status)
{
    m_action_status = status;
}

hailo_status PipelineBuffer::set_dma_buf_as_memview(BufferProtection dma_buffer_protection)
{
    auto dma_buffer = get_metadata().get_additional_data<DmaBufferPipelineData>();

    std::unique_lock<std::mutex> lock(m_exec_done_mutex);
    TRY(m_view, DmaBufferUtils::mmap_dma_buffer(dma_buffer->m_dma_buffer, dma_buffer_protection));

    m_exec_done = [mem_view=m_view, exec_done=m_exec_done, dma_buffer, dma_buffer_protection](hailo_status status) {
        auto mumap_status = DmaBufferUtils::munmap_dma_buffer(dma_buffer->m_dma_buffer, mem_view, dma_buffer_protection);
        if (HAILO_SUCCESS != mumap_status) {
            LOGGER__ERROR("Failed to unmap dma buffer");
            status = HAILO_FILE_OPERATION_FAILURE;
        }
        exec_done(status);
    };

    m_buffer_type = BufferType::VIEW;
    return HAILO_SUCCESS;
}

void PipelineBuffer::call_exec_done()
{
    if (m_should_call_exec_done) {
        std::unique_lock<std::mutex> lock(m_exec_done_mutex);
        m_exec_done(action_status());
        m_should_call_exec_done = false;
    }
}

Expected<PipelineBufferPoolPtr> PipelineBufferPool::create(size_t buffer_size, size_t buffer_count, EventPtr shutdown_event,
                                           hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags,
                                           bool is_empty, bool is_dma_able)
{
    AccumulatorPtr queue_size_accumulator = nullptr;
    if ((elem_flags & HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE) != 0) {
        queue_size_accumulator = make_shared_nothrow<FullAccumulator<double>>("queue_size");
        CHECK_AS_EXPECTED(nullptr != queue_size_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }
    const bool measure_vstream_latency = (vstream_flags & HAILO_VSTREAM_STATS_MEASURE_LATENCY) != 0;

    TRY(auto pipeline_buffers_queue, SpscQueue<PipelineBuffer>::create(buffer_count, shutdown_event, BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT));

    std::vector<Buffer> buffers;
    buffers.reserve(buffer_count);

    auto buffer_pool_ptr = make_shared_nothrow<PipelineBufferPool>(buffer_size, is_empty, measure_vstream_latency, std::move(buffers),
        std::move(pipeline_buffers_queue), std::move(queue_size_accumulator), buffer_count);
    CHECK_AS_EXPECTED(nullptr != buffer_pool_ptr, HAILO_OUT_OF_HOST_MEMORY);

    if (!is_empty) {
        for (size_t i = 0; i < buffer_count; i++) {
            BufferStorageParams buffer_storage_params;
            if (is_dma_able) {
                buffer_storage_params = BufferStorageParams::create_dma();
            }
            auto buffer = Buffer::create(buffer_size, buffer_storage_params);
            CHECK_EXPECTED(buffer);

            auto pipeline_buffer = PipelineBuffer(MemoryView(buffer.value()), [](hailo_status){}, HAILO_SUCCESS, false, buffer_pool_ptr,
                buffer_pool_ptr->m_measure_vstream_latency);

            auto status = buffer_pool_ptr->enqueue_buffer(std::move(pipeline_buffer));
            CHECK_SUCCESS_AS_EXPECTED(status);

            buffer_pool_ptr->m_buffers.emplace_back(buffer.release());
        }
    }

    return buffer_pool_ptr;
}

PipelineBufferPool::PipelineBufferPool(size_t buffer_size, bool is_holding_user_buffers, bool measure_vstream_latency, std::vector<Buffer> &&buffers,
        SpscQueue<PipelineBuffer> &&pipeline_buffers_queue, AccumulatorPtr &&queue_size_accumulator,
        size_t max_buffer_count) :
    m_buffer_size(buffer_size),
    m_is_holding_user_buffers(is_holding_user_buffers),
    m_max_buffer_count(max_buffer_count),
    m_measure_vstream_latency(measure_vstream_latency),
    m_buffers(std::move(buffers)),
    m_pipeline_buffers_queue(std::move(pipeline_buffers_queue)),
    m_queue_size_accumulator(std::move(queue_size_accumulator)),
    m_is_already_running(false)
{
}

size_t PipelineBufferPool::buffer_size()
{
    std::unique_lock<std::mutex> lock(m_buffer_size_mutex);
    return m_buffer_size.load();
}

hailo_status PipelineBufferPool::enqueue_buffer(PipelineBuffer &&pipeline_buffer)
{
    // TODO: add support for pix_buffer here (currently enqueue_buffer is called only to add the output_buffers which are never pix_buffers)
    m_is_already_running = true;
    auto pool_buffer_size = buffer_size();
    CHECK(pipeline_buffer.size() == pool_buffer_size, HAILO_INTERNAL_FAILURE,
        "Buffer size is not the same as expected for pool! ({} != {})", pipeline_buffer.size(), pool_buffer_size);

    std::unique_lock<std::mutex> lock(m_enqueue_mutex);
    auto status = m_pipeline_buffers_queue.enqueue(std::move(pipeline_buffer));
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        return HAILO_SHUTDOWN_EVENT_SIGNALED;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}
bool PipelineBufferPool::should_measure_vstream_latency()
{
    return m_measure_vstream_latency;
}

bool PipelineBufferPool::is_full()
{
    return (m_max_buffer_count - num_of_buffers_in_pool() == 0);
}

size_t PipelineBufferPool::max_capacity()
{
    return m_max_buffer_count;
}

size_t PipelineBufferPool::num_of_buffers_in_pool()
{
    return m_pipeline_buffers_queue.size_approx();
}

bool PipelineBufferPool::is_holding_user_buffers()
{
    return m_is_holding_user_buffers;
}

Expected<PipelineBuffer> PipelineBufferPool::acquire_buffer(std::chrono::milliseconds timeout,
    bool ignore_shutdown_event)
{
    m_is_already_running = true;
    std::unique_lock<std::mutex> lock(m_dequeue_mutex);

    if (nullptr != m_queue_size_accumulator) {
        m_queue_size_accumulator->add_data_point(static_cast<double>(m_pipeline_buffers_queue.size_approx()));
    }

    auto pipeline_buffer = m_pipeline_buffers_queue.dequeue(timeout, ignore_shutdown_event);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == pipeline_buffer.status()) {
        return make_unexpected(pipeline_buffer.status());
    }
    else if (HAILO_TIMEOUT == pipeline_buffer.status()) {
        LOGGER__WARNING(
            "Failed to acquire buffer because the buffer pool is empty. This could be caused by uneven reading and writing speeds, with a short user-defined timeout. (timeout={}ms)",
            timeout.count());
        return make_unexpected(pipeline_buffer.status());
    }
    CHECK_EXPECTED(pipeline_buffer);

    return pipeline_buffer.release();
}

AccumulatorPtr PipelineBufferPool::get_queue_size_accumulator()
{
    return m_queue_size_accumulator;
}

Expected<PipelineBuffer> PipelineBufferPool::get_available_buffer(PipelineBuffer &&optional, std::chrono::milliseconds timeout)
{
    m_is_already_running = true;

    if (optional) {
        auto pool_buffer_size = buffer_size();
        CHECK_AS_EXPECTED(optional.size() == pool_buffer_size, HAILO_INVALID_OPERATION,
            "Optional buffer size must be equal to pool buffer size. Optional buffer size = {}, buffer pool size = {}",
            optional.size(), pool_buffer_size);
        return std::move(optional);
    }

    auto acquired_buffer = acquire_buffer(timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == acquired_buffer.status()) {
        return make_unexpected(acquired_buffer.status());
    }
    CHECK_EXPECTED(acquired_buffer, "Failed to acquire buffer with status={}", acquired_buffer.status());
    return acquired_buffer.release();
}

hailo_status PipelineBufferPool::return_buffer_to_pool(PipelineBuffer &&pipeline_buffer)
{
    std::unique_lock<std::mutex> lock(m_enqueue_mutex);
    // This can be called after the shutdown event was signaled so we ignore it here
    return m_pipeline_buffers_queue.enqueue(std::move(pipeline_buffer), true);
}

hailo_status PipelineBufferPool::map_to_vdevice(VDevice &vdevice, hailo_dma_buffer_direction_t direction)
{
    for (auto &buff : m_buffers) {
        auto dma_mapped_buffer = DmaMappedBuffer::create(vdevice, buff.data(), buff.size(), direction);
        CHECK_EXPECTED(dma_mapped_buffer);
        m_dma_mapped_buffers.emplace_back(dma_mapped_buffer.release());
    }
    return HAILO_SUCCESS;
}

hailo_status PipelineBufferPool::set_buffer_size(uint32_t buffer_size)
{
    std::unique_lock<std::mutex> lock(m_buffer_size_mutex);
    CHECK(!m_is_already_running, HAILO_INVALID_OPERATION,
        "Setting buffer size of pool size after starting inference in not allowed");

    m_buffer_size = buffer_size;
    return HAILO_SUCCESS;
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

PipelineObject::PipelineObject(const std::string &name, uint64_t pipeline_unique_id) : m_name(name), m_pipeline_unique_id(pipeline_unique_id)
{}

const std::string &PipelineObject::name() const
{
    return m_name;
}

const uint64_t &PipelineObject::pipeline_unique_id() const
{
    return m_pipeline_unique_id;
}

std::string PipelineObject::create_element_name(const std::string &element_name, const std::string &stream_name, uint8_t stream_index)
{
    std::stringstream name;
    name << element_name << static_cast<uint32_t>(stream_index) << stream_name;
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
    PipelineObject(create_pad_name(element_name, pad_type), element.pipeline_unique_id()),
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

hailo_status PipelinePad::terminate(hailo_status error_status)
{
    return m_element.terminate(error_status);
}

hailo_status PipelinePad::dequeue_user_buffers(hailo_status error_status)
{
    return m_element.dequeue_user_buffers(error_status);
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

void PipelinePad::run_push_async_multi(std::vector<PipelineBuffer> &&buffers)
{
    m_element.run_push_async_multi(std::move(buffers));
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

const PipelineBufferPoolPtr PipelinePad::get_buffer_pool() const
{
    return m_element.get_buffer_pool(name());
}

PipelineElement::PipelineElement(const std::string &name, uint64_t pipeline_unique_id, DurationCollector &&duration_collector,
                                 std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                 PipelineDirection pipeline_direction) :
    PipelineObject(name, pipeline_unique_id),
    m_duration_collector(std::move(duration_collector)),
    m_pipeline_status(std::move(pipeline_status)),
    m_sinks(),
    m_sources(),
    m_pipeline_direction(pipeline_direction),
    m_is_terminating_element(false),
    m_is_terminated(false)
{}

AccumulatorPtr PipelineElement::get_fps_accumulator()
{
    return m_duration_collector.get_average_fps_accumulator();
}

AccumulatorPtr PipelineElement::get_latency_accumulator()
{
    return m_duration_collector.get_latency_accumulator();
}

bool PipelineElement::is_terminating_element()
{
    return m_is_terminating_element;
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

void PipelineElement::add_element_to_stringstream(std::stringstream &stream, const PipelinePad &source) const
{
    stream << " " << source.next()->element().name();
}

std::string PipelineElement::links_description() const
{
    std::stringstream element_base_description;

    element_base_description << "| inputs:";
    if ((!sinks().empty()) && (nullptr != sinks()[0].prev())) {
        size_t i = 0;
        for(const auto &sink : sinks()) {
            if (sink.prev()) {
                element_base_description << " " << sink.prev()->element().name() << "[" << i << "]";
            }
            i++;
        }
    } else {
        element_base_description << " user";
    }

    element_base_description << " | outputs:";
    if ((!sources().empty()) && (nullptr != sources()[0].next())) {
        for(const auto &source : sources()) {
            if (source.next()) {
                add_element_to_stringstream(element_base_description, source);
            }
        }
    } else {
        element_base_description << " user";
    }

    return element_base_description.str();
}

void PipelineElement::print_deep_description(std::vector<std::string> &visited_elements)
{
    auto visited_node = find(visited_elements.begin(), visited_elements.end(), this->name());
    if (visited_elements.end() != visited_node) {
        return;
    }

    LOGGER__INFO("{} {}", this->name().c_str(), this->links_description().c_str());
    visited_elements.emplace_back(this->name());

    for (auto &source : sources()) {
        source.next()->element().print_deep_description(visited_elements);
    }
}

hailo_status PipelineElement::enqueue_execution_buffer(PipelineBuffer &&pipeline_buffer)
{
    (void)pipeline_buffer;
    LOGGER__ERROR("enqueue_execution_buffer is not implemented for {}!", name());
    return HAILO_NOT_IMPLEMENTED;
};

hailo_status PipelineElement::empty_buffer_pool(PipelineBufferPoolPtr pool, hailo_status error_status, std::chrono::milliseconds timeout)
{
    if (!pool) {
        return HAILO_SUCCESS;
    }

    if (!pool->is_holding_user_buffers()) {
        return HAILO_SUCCESS;
    }

    while (pool->num_of_buffers_in_pool() > 0) {
        auto acquired_buffer = pool->acquire_buffer(timeout, true);

        if (HAILO_SUCCESS != acquired_buffer.status()) {
            LOGGER__CRITICAL("Failed to acquire from pool in {} element!", name());
            return acquired_buffer.status();
        }

        acquired_buffer->set_action_status(error_status);
    }
    return HAILO_SUCCESS;
}

Expected<bool> PipelineElement::can_push_buffer_upstream(uint32_t /*frames_count*/)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<bool> PipelineElement::can_push_buffer_downstream(uint32_t /*frames_count*/)
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

hailo_status PipelineElement::terminate(hailo_status error_status)
{
    return execute_terminate(error_status);
}

hailo_status PipelineElement::dequeue_user_buffers(hailo_status error_status)
{
    return execute_dequeue_user_buffers(error_status);
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

hailo_status PipelineElement::execute_terminate(hailo_status error_status)
{
    m_is_terminated = true;
    return execute([&](auto *pad){ return pad->terminate(error_status); });
}

hailo_status PipelineElement::execute_dequeue_user_buffers(hailo_status error_status)
{
    return execute([&](auto *pad){ return pad->dequeue_user_buffers(error_status); });
}

hailo_status PipelineElement::execute(std::function<hailo_status(PipelinePad*)> func)
{
    for (auto pad : execution_pads()) {
        auto status = func(pad);
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

} /* namespace hailort */
