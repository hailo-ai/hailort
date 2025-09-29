/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline.hpp
 * @brief Hailo Infer Pipeline
 **/

#ifndef _HAILO_PIPELINE_HPP_
#define _HAILO_PIPELINE_HPP_

#include "hailo/buffer.hpp"
#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include "hailo/runtime_statistics.hpp"
#include "hailo/dma_mapped_buffer.hpp"
#include "net_flow/ops/nms_post_process.hpp"
#include "hailo/network_group.hpp"
#include "common/thread_safe_queue.hpp"

#include <memory>
#include <thread>
#include <sstream>
#include <functional>


namespace hailort
{

enum class PipelineDirection
{
    PULL,
    PUSH,
};

enum class BufferProtection
{
    NONE,
    READ,
    WRITE,
    READ_WRITE,
};


using TransferDoneCallbackAsyncInfer = std::function<void(hailo_status)>;

using PipelineTimePoint = std::chrono::steady_clock::time_point;
#define BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT (std::chrono::milliseconds(10000))
#define DEFAULT_NUM_FRAMES_BEFORE_COLLECTION_START (100)

class VDevice;

struct AdditionalData {
    virtual ~AdditionalData() = default;
    enum class Type {
        Base,
        IouPipelineData,
        PixBufferPipelineData,
        DmaBufferPipelineData
    };

    // Runtime type tag (avoids dynamic_cast). Derived types must override and return their static TYPE.
    virtual Type type() const noexcept { return Type::Base; }
};

struct IouPipelineData : AdditionalData
{
    static constexpr Type TYPE = Type::IouPipelineData;
    Type type() const noexcept override { return TYPE; }
    IouPipelineData(std::vector<net_flow::DetectionBbox> &&detections, std::vector<uint32_t> &&detections_classes_count)
        : m_detections(std::move(detections)),
          m_detections_classes_count(std::move(detections_classes_count)) {}
    std::vector<net_flow::DetectionBbox> m_detections;
    std::vector<uint32_t> m_detections_classes_count;

    bool operator==(const IouPipelineData &other) const {
        return m_detections == other.m_detections && m_detections_classes_count == other.m_detections_classes_count;
    }
};

struct PixBufferPipelineData : AdditionalData
{
    static constexpr Type TYPE = Type::PixBufferPipelineData;
    Type type() const noexcept override { return TYPE; }
    PixBufferPipelineData(const hailo_pix_buffer_t &buffer) : m_pix_buffer(buffer) {};
    hailo_pix_buffer_t m_pix_buffer;
};

struct DmaBufferPipelineData : AdditionalData
{
    static constexpr Type TYPE = Type::DmaBufferPipelineData;
    Type type() const noexcept override { return TYPE; }
    DmaBufferPipelineData(const hailo_dma_buffer_t &buffer) : m_dma_buffer(buffer) {};
    hailo_dma_buffer_t m_dma_buffer;
};

class PipelineBufferPool;
using PipelineBufferPoolPtr = std::shared_ptr<PipelineBufferPool>;
using PipelineBufferPoolWeakPtr = std::weak_ptr<PipelineBufferPool>;

class PipelineBuffer final
{
public:
    class Metadata final
    {
    public:
        explicit Metadata(PipelineTimePoint start_time);
        // Creates an empty metadata object
        Metadata();
        ~Metadata() = default;
        Metadata(const Metadata &) = default;
        Metadata &operator=(const Metadata &) = default;
        Metadata(Metadata &&other) = default;
        Metadata &operator=(Metadata &&other) = default;

        PipelineTimePoint get_start_time() const;

        void set_start_time(PipelineTimePoint val);

        void set_additional_data(std::shared_ptr<AdditionalData> data) { m_additional_data = data;}
        template <typename T>
        std::shared_ptr<T> get_additional_data() {
            static_assert(std::is_base_of<AdditionalData, T>::value, "T must derive from AdditionalData");
            if (!m_additional_data) {
                return nullptr;
            }
            if constexpr (std::is_same<T, AdditionalData>::value) {
                return std::static_pointer_cast<T>(m_additional_data);
            } else {
                if (m_additional_data->type() != T::TYPE) {
                    return nullptr;
                }
                return std::static_pointer_cast<T>(m_additional_data);
            }
        }

    private:
        std::shared_ptr<AdditionalData> m_additional_data;
        PipelineTimePoint m_start_time;
    };

    enum class Type {
        DATA = 0,
        FLUSH,
        DEACTIVATE
    };

    // Creates an empty PipelineBuffer (with no buffer/memory view)
    PipelineBuffer(Type type);
    // TODO HRT-12185: remove the option to pass a lambda as a parameter and save it as a member since it increases the memory consumption Significantly
    PipelineBuffer(hailo_status action_status = HAILO_SUCCESS, const TransferDoneCallbackAsyncInfer &exec_done = [](hailo_status){});
    PipelineBuffer(MemoryView view, const TransferDoneCallbackAsyncInfer &exec_done = [](hailo_status){}, hailo_status action_status = HAILO_SUCCESS,
        bool is_user_buffer = true, PipelineBufferPoolWeakPtr pool = PipelineBufferPoolWeakPtr(), bool should_measure = false);
    PipelineBuffer(hailo_pix_buffer_t buffer, const TransferDoneCallbackAsyncInfer &exec_done = [](hailo_status){});
    PipelineBuffer(hailo_dma_buffer_t dma_buffer, const TransferDoneCallbackAsyncInfer &exec_done = [](hailo_status){},
        hailo_status action_status = HAILO_SUCCESS, bool is_user_buffer = true, PipelineBufferPoolWeakPtr pool = PipelineBufferPoolWeakPtr(), bool should_measure = false);

    ~PipelineBuffer();

    PipelineBuffer(const PipelineBuffer &) = delete;
    PipelineBuffer &operator=(const PipelineBuffer &) = delete;
    PipelineBuffer(PipelineBuffer &&other);
    PipelineBuffer &operator=(PipelineBuffer &&other);
    explicit operator bool() const;

    uint8_t* data();
    size_t size() const;
    Expected<MemoryView> as_view(BufferProtection dma_buffer_protection);
    Expected<hailo_pix_buffer_t> as_hailo_pix_buffer(hailo_format_order_t order = HAILO_FORMAT_ORDER_AUTO);
    Type get_type() const;
    Metadata get_metadata() const;
    void set_metadata_start_time(PipelineTimePoint val);
    void set_additional_data(std::shared_ptr<AdditionalData> data) { m_metadata.set_additional_data(data);}
    hailo_status action_status();
    void set_action_status(hailo_status status);
    void call_exec_done();
    BufferType get_buffer_type() const;

private:
    Type m_type;
    PipelineBufferPoolWeakPtr m_pool;
    MemoryView m_view;
    std::mutex m_exec_done_mutex;
    TransferDoneCallbackAsyncInfer m_exec_done;
    Metadata m_metadata;
    bool m_is_user_buffer;
    bool m_should_call_exec_done;
    hailo_status m_action_status;
    BufferType m_buffer_type;

    static PipelineTimePoint add_timestamp(bool should_measure);
    static void return_buffer_to_pool(PipelineBufferPoolWeakPtr buffer_pool_weak_ptr, MemoryView mem_view, bool is_user_buffer);
    static void return_buffer_to_pool(PipelineBufferPoolWeakPtr buffer_pool_weak_ptr, hailo_dma_buffer_t dma_buffer, bool is_user_buffer);
    hailo_status set_dma_buf_as_memview(BufferProtection dma_buffer_protection);
};

class PipelineBufferPool
{
public:
    static Expected<PipelineBufferPoolPtr> create(size_t buffer_size, size_t buffer_count, EventPtr shutdown_event,
        hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags, bool is_empty = false,
        bool dma_able = false);

    PipelineBufferPool(size_t buffer_size, bool is_holding_user_buffers, bool measure_vstream_latency, std::vector<Buffer> &&buffers,
        SpscQueue<PipelineBuffer> &&pipeline_buffers_queue, AccumulatorPtr &&queue_size_accumulator, size_t max_buffer_count);
    virtual ~PipelineBufferPool() = default;

    size_t buffer_size();
    hailo_status enqueue_buffer(PipelineBuffer &&pipeline_buffer);
    Expected<PipelineBuffer> acquire_buffer(std::chrono::milliseconds timeout, bool ignore_shutdown_event = false);
    AccumulatorPtr get_queue_size_accumulator();
    Expected<PipelineBuffer> get_available_buffer(PipelineBuffer &&optional, std::chrono::milliseconds timeout);
    bool should_measure_vstream_latency();
    bool is_full();
    size_t max_capacity();
    size_t num_of_buffers_in_pool();
    bool is_holding_user_buffers();

    hailo_status map_to_vdevice(VDevice &vdevice, hailo_dma_buffer_direction_t direction);
    hailo_status set_buffer_size(uint32_t buffer_size);
private:
    hailo_status return_buffer_to_pool(PipelineBuffer &&pipeline_buffer);

    std::atomic<size_t> m_buffer_size;
    bool m_is_holding_user_buffers;
    size_t m_max_buffer_count;
    const bool m_measure_vstream_latency;

    // PipelineBufferPool can hold allocated buffers (type of Buffer) and buffers that come from the user (type of MemoryView).
    // To be able to support both types, the queue of the pool holds MemoryViews and to hold the allocated buffers we use a vector.
    // So when the pool has allocated buffers, it will hold them in the vector and have pointers to them in the queue.
    // And when the pool holds user buffers, the vector will be empty and only the queue will hold the user's buffers.
    std::vector<Buffer> m_buffers;

    // When m_buffers is not empty, and we need to pre-map the buffers to the vdevice, this vector will hold reference
    // to the mapping objects.
    std::vector<hailort::DmaMappedBuffer> m_dma_mapped_buffers;

    SpscQueue<PipelineBuffer> m_pipeline_buffers_queue;
    AccumulatorPtr m_queue_size_accumulator;
    // we have enqueue and dequeue mutex to allow mpmc
    std::mutex m_enqueue_mutex;
    std::mutex m_dequeue_mutex;
    std::mutex m_buffer_size_mutex;

    std::atomic<bool> m_is_already_running;

    friend class PipelineBuffer;
};

class DurationCollector final
{
public:
    // TODO: HRT-4258
    // Note: We start measuring the FPS/latency after num_frames_before_collection_start calls to start_measurement + 
    //       complete_measurement. This is to allow the vstream pipeline to stabilize. Thus we ignore invalid
    //       measurements that are due to buffering that occours when the pipeline starts.
    static Expected<DurationCollector> create(hailo_pipeline_elem_stats_flags_t flags,
        uint32_t num_frames_before_collection_start = DEFAULT_NUM_FRAMES_BEFORE_COLLECTION_START);
    DurationCollector(const DurationCollector &) = delete;
    DurationCollector(DurationCollector &&other) = default;
    DurationCollector &operator=(const DurationCollector &) = delete;
    DurationCollector &operator=(DurationCollector &&other) = delete;
    ~DurationCollector() = default;
    
    void start_measurement();
    void complete_measurement();

    // latency_accumulator will measure latency in seconds
    AccumulatorPtr get_latency_accumulator();
    // average_fps_accumulator will measure fps in seconds^-1
    AccumulatorPtr get_average_fps_accumulator();

private:
    DurationCollector(bool measure_latency, bool measure_average_fps,
                      AccumulatorPtr &&latency_accumulator, AccumulatorPtr &&average_fps_accumulator,
                      uint32_t num_frames_before_collection_start);
    static bool should_measure_latency(hailo_pipeline_elem_stats_flags_t flags);
    static bool should_measure_average_fps(hailo_pipeline_elem_stats_flags_t flags);

    const bool m_measure_latency;
    const bool m_measure_average_fps;
    const bool m_measure;
    AccumulatorPtr m_latency_accumulator;
    AccumulatorPtr m_average_fps_accumulator;
    PipelineTimePoint m_start;
    size_t m_count;
    const size_t m_num_frames_before_collection_start;
};

class PipelineObject
{
public:
    PipelineObject(const std::string &name, uint64_t pipeline_unique_id);
    virtual ~PipelineObject() = default;
    PipelineObject(PipelineObject &&) noexcept = default;
    PipelineObject& operator=(PipelineObject &&) noexcept = default;

    const std::string& name() const;
    const uint64_t& pipeline_unique_id() const;

    static std::string create_element_name(const std::string &element_name, const std::string &stream_name, uint8_t stream_index);

private:
    std::string m_name;
    uint64_t m_pipeline_unique_id;
};

class PipelineElement;
using PushCompleteCallback = std::function<void(const PipelineBuffer::Metadata&)>;
using PullCompleteCallback = std::function<void(const PipelineBuffer::Metadata&)>;

class PipelinePad final : public PipelineObject
{
public:
    enum class Type
    {
        SOURCE,
        SINK
    };

    // Link left's source pad (left->sources()[left_source_index]) with right's sink pad (right->right()[right_sink_index])
    static hailo_status link_pads(std::shared_ptr<PipelineElement> left, std::shared_ptr<PipelineElement> right,
        uint32_t left_source_index = 0, uint32_t right_sink_index = 0);
    // Link left's source pad (left.sources()[left_source_index]) with right's sink pad (right.right()[right_sink_index])
    static hailo_status link_pads(PipelineElement &left, PipelineElement &right, uint32_t left_source_index = 0,
        uint32_t right_sink_index = 0);
    static std::string create_pad_name(const std::string &element_name, Type pad_type);

    PipelinePad(PipelineElement &element, const std::string &element_name, Type pad_type);
    PipelinePad(const PipelinePad &) = delete;
    PipelinePad(PipelinePad &&other) = default;
    PipelinePad &operator=(const PipelinePad &) = delete;
    PipelinePad &operator=(PipelinePad &&other) = delete;
    ~PipelinePad() = default;

    hailo_status activate();
    hailo_status deactivate();
    hailo_status post_deactivate(bool should_clear_abort);
    hailo_status clear();
    hailo_status flush();
    hailo_status abort();
    hailo_status terminate(hailo_status error_status);
    hailo_status dequeue_user_buffers(hailo_status error_status);
    hailo_status clear_abort();
    virtual hailo_status run_push(PipelineBuffer &&buffer);
    void run_push_async(PipelineBuffer &&buffer);
    virtual void run_push_async_multi(std::vector<PipelineBuffer> &&buffers);
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional = PipelineBuffer());
    void set_push_complete_callback(PushCompleteCallback push_complete_callback);
    void set_pull_complete_callback(PullCompleteCallback pull_complete_callback);
    void set_next(PipelinePad *next);
    void set_prev(PipelinePad *prev);
    const PipelineBufferPoolPtr get_buffer_pool() const;
    PipelinePad *next();
    PipelinePad *prev();
    PipelineElement &element();
    const PipelinePad *next() const;
    const PipelinePad *prev() const;
    const PipelineElement &element() const;

protected:
    PipelineElement &m_element;
    PipelinePad *m_next;
    PipelinePad *m_prev;
    PushCompleteCallback m_push_complete_callback;
    PullCompleteCallback m_pull_complete_callback;

private:
    // Automatic naming isn't thread safe
    static uint32_t index;
};

// Note: PipelinePads accept 'PipelineElement &' in their ctor. PipelineElements can pass "*this" to their
//       PipelinePads (sources/sinks) in the PipelineElement ctor. This is OK because the ctor of PipelinePad
//       does nothing with the element reference other than setting it as class member.
class PipelineElement : public PipelineObject
{
public:
    PipelineElement(const std::string &name, uint64_t pipeline_unique_id, DurationCollector &&duration_collector,
                    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                    PipelineDirection pipeline_direction);
    virtual ~PipelineElement() = default;

    PipelineElement(PipelineElement &&other) = delete;
    PipelineElement(const PipelineElement &) = delete;
    PipelineElement &operator=(const PipelineElement &) = delete;
    PipelineElement &operator=(PipelineElement &&other) = delete;

    hailo_status activate();
    hailo_status deactivate();
    hailo_status post_deactivate(bool should_clear_abort);
    hailo_status clear();
    hailo_status flush();
    hailo_status abort();
    hailo_status terminate(hailo_status error_status);
    hailo_status dequeue_user_buffers(hailo_status error_status);
    hailo_status clear_abort();
    AccumulatorPtr get_fps_accumulator();
    AccumulatorPtr get_latency_accumulator();
    bool is_terminating_element();
    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators();
    std::vector<PipelinePad> &sinks();
    std::vector<PipelinePad> &sources();
    const std::vector<PipelinePad> &sinks() const;
    const std::vector<PipelinePad> &sources() const;
    virtual std::string description() const;
    virtual void add_element_to_stringstream(std::stringstream &stream, const PipelinePad &source) const;
    std::string links_description() const;
    void print_deep_description(std::vector<std::string> &visited_elements);

    virtual hailo_status enqueue_execution_buffer(PipelineBuffer &&pipeline_buffer);
    hailo_status empty_buffer_pool(PipelineBufferPoolPtr pool, hailo_status error_status, std::chrono::milliseconds timeout);
    virtual Expected<bool> can_push_buffer_upstream(uint32_t frames_count);
    virtual Expected<bool> can_push_buffer_downstream(uint32_t frames_count);

    virtual Expected<uint32_t> get_source_index_from_source_name(const std::string &/*source_name*/) {
        // This function is overriden in multi-srcs elements
        return 0;
    }

    virtual hailo_status set_nms_score_threshold(float32_t /*threshold*/) {
        return HAILO_INVALID_OPERATION;
    }

    virtual hailo_status set_nms_iou_threshold(float32_t /*threshold*/) {
        return HAILO_INVALID_OPERATION;
    }

    virtual hailo_status set_nms_max_proposals_per_class(uint32_t /*max_proposals_per_class*/) {
        return HAILO_INVALID_OPERATION;
    }

    virtual hailo_status set_nms_max_proposals_total(uint32_t /*max_proposals_total*/) {
        return HAILO_INVALID_OPERATION;
    }

    virtual hailo_status set_nms_max_accumulated_mask_size(uint32_t /*max_accumulated_mask_size*/) {
        return HAILO_INVALID_OPERATION;
    }

    virtual PipelineBufferPoolPtr get_buffer_pool(const std::string &/*pad_name*/) const
    {
        // This method should be overriden by element with local pools
        return nullptr;
    }

protected:
    DurationCollector m_duration_collector;
    std::shared_ptr<std::atomic<hailo_status>> m_pipeline_status;
    std::vector<PipelinePad> m_sinks;
    std::vector<PipelinePad> m_sources;
    PipelineDirection m_pipeline_direction;
    bool m_is_terminating_element;
    bool m_is_terminated;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) = 0;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) = 0;
    virtual void run_push_async_multi(std::vector<PipelineBuffer> &&/*buffers*/) {
        // This method should be overriden by element which supports multiple buffers (e.g. mux element)
        LOGGER__CRITICAL("run_push_async_multi is not implemented for element {}", name());
        assert(false);
    };
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) = 0;
    virtual std::vector<PipelinePad*> execution_pads() = 0;
    virtual hailo_status execute_activate();
    virtual hailo_status execute_deactivate();
    virtual hailo_status execute_post_deactivate(bool should_clear_abort);
    virtual hailo_status execute_clear();
    virtual hailo_status execute_flush();
    virtual hailo_status execute_abort();
    virtual hailo_status execute_terminate(hailo_status error_status);
    virtual hailo_status execute_dequeue_user_buffers(hailo_status error_status);
    virtual hailo_status execute_clear_abort();

    virtual hailo_status execute(std::function<hailo_status(PipelinePad*)>);

    friend class PipelinePad;
};

} /* namespace hailort */

#endif /* _HAILO_PIPELINE_HPP_ */
