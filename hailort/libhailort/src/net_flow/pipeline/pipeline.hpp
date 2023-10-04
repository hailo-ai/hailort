/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#include "net_flow/ops/nms_post_process.hpp"

#include "utils/thread_safe_queue.hpp"

#include <memory>
#include <thread>
#include <sstream>
#include <functional>


namespace hailort
{
struct ElementBuildParams
{
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status;
    std::chrono::milliseconds timeout;
    EventPtr shutdown_event;
    size_t buffer_pool_size;
    hailo_pipeline_elem_stats_flags_t elem_stats_flags;
    hailo_vstream_stats_flags_t vstream_stats_flags;
};

enum class PipelineDirection
{
    PULL,
    PUSH,
};

// TODO: need to think about naming and the right place to declare the CompletionInfoAsyncInferInternal and TransferDoneCallbackAsyncInfer
struct CompletionInfoAsyncInferInternal
{
    hailo_status status;
};
using TransferDoneCallbackAsyncInfer = std::function<void(const CompletionInfoAsyncInferInternal &completion_info)>;;

using PipelineTimePoint = std::chrono::steady_clock::time_point;
#define BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT (std::chrono::milliseconds(10000))
#define DEFAULT_NUM_FRAMES_BEFORE_COLLECTION_START (100)

struct AdditionalData {};

struct IouPipelineData : AdditionalData
{
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
    PixBufferPipelineData(const hailo_pix_buffer_t &buffer) : m_pix_buffer(buffer) {};
    hailo_pix_buffer_t m_pix_buffer;
};

class BufferPool;
using BufferPoolPtr = std::shared_ptr<BufferPool>;

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
        Metadata &operator=(const Metadata &) = delete;
        Metadata(Metadata &&other) = default;
        Metadata &operator=(Metadata &&other) = default;

        PipelineTimePoint get_start_time() const;

        void set_start_time(PipelineTimePoint val);

        void set_additional_data(std::shared_ptr<AdditionalData> data) { m_additional_data = data;}
        template <typename T>
        std::shared_ptr<T> get_additional_data() {
            return std::static_pointer_cast<T>(m_additional_data);
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
    PipelineBuffer();
    PipelineBuffer(Type type);
    PipelineBuffer(hailo_status status);
    PipelineBuffer(MemoryView view, bool is_user_buffer = true, BufferPoolPtr pool = nullptr, bool should_measure = false, hailo_status status = HAILO_SUCCESS);
    PipelineBuffer(MemoryView view, const TransferDoneCallbackAsyncInfer &exec_done,
        bool is_user_buffer = true, BufferPoolPtr pool = nullptr, bool should_measure = false, hailo_status status = HAILO_SUCCESS);
    PipelineBuffer(hailo_pix_buffer_t buffer);
    ~PipelineBuffer();

    PipelineBuffer(const PipelineBuffer &) = delete;
    PipelineBuffer &operator=(const PipelineBuffer &) = delete;
    PipelineBuffer(PipelineBuffer &&other);
    PipelineBuffer &operator=(PipelineBuffer &&other);
    explicit operator bool() const;

    uint8_t* data();
    size_t size() const;
    MemoryView as_view();
    Expected<hailo_pix_buffer_t> as_hailo_pix_buffer(hailo_format_order_t order = HAILO_FORMAT_ORDER_AUTO);
    Type get_type() const;
    Metadata get_metadata() const;
    void set_metadata(Metadata &&val);
    void set_additional_data(std::shared_ptr<AdditionalData> data) { m_metadata.set_additional_data(data);}
    TransferDoneCallbackAsyncInfer get_exec_done_cb() const;
    hailo_status action_status();
    void set_action_status(hailo_status status);

private:
    Type m_type;
    BufferPoolPtr m_pool;
    MemoryView m_view;
    TransferDoneCallbackAsyncInfer m_exec_done;
    Metadata m_metadata;
    bool m_is_user_buffer;
    hailo_status m_action_status;

    static PipelineTimePoint add_timestamp(bool should_measure);
};

// The buffer pool has to be created as a shared pointer (via the create function) because we use shared_from_this(),
// which is only allowed if there is already a shared pointer pointing to "this"!
class BufferPool : public std::enable_shared_from_this<BufferPool>
{
public:
    static Expected<BufferPoolPtr> create(size_t buffer_size, size_t buffer_count, EventPtr shutdown_event,
        hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags, bool is_empty = false,
        bool dma_able = false);

    BufferPool(size_t buffer_size, bool is_holding_user_buffers, bool measure_vstream_latency, std::vector<Buffer> &&buffers, SpscQueue<MemoryView> &&free_mem_views,
        SpscQueue<TransferDoneCallbackAsyncInfer> &&done_cbs, AccumulatorPtr &&queue_size_accumulator, size_t max_buffer_count);
    virtual ~BufferPool() = default;

    size_t buffer_size();
    hailo_status enqueue_buffer(MemoryView mem_view);
    hailo_status enqueue_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done);
    hailo_status allocate_buffers(bool is_dma_able);
    Expected<PipelineBuffer> acquire_buffer(std::chrono::milliseconds timeout);
    Expected<std::shared_ptr<PipelineBuffer>> acquire_buffer_ptr(std::chrono::milliseconds timeout);
    AccumulatorPtr get_queue_size_accumulator();
    Expected<PipelineBuffer> get_available_buffer(PipelineBuffer &&optional, std::chrono::milliseconds timeout);
    bool is_full();

private:
    Expected<MemoryView> acquire_free_mem_view(std::chrono::milliseconds timeout);
    Expected<TransferDoneCallbackAsyncInfer> acquire_on_done_cb(std::chrono::milliseconds timeout);
    hailo_status release_buffer(MemoryView mem_view);

    const size_t m_buffer_size;
    bool m_is_holding_user_buffers;
    size_t m_max_buffer_count;
    const bool m_measure_vstream_latency;

    // BufferPool can hold allocated buffers (type of Buffer) and buffers that come from the user (type of MemoryView).
    // To be able to support both types, the queue of the pool holds MemoryViews and to hold the allocated buffers we use a vector.
    // So when the pool has allocated buffers, it will hold them in the vector and have pointers to them in the queue.
    // And when the pool holds user buffers, the vector will be empty and only the queue will hold the user's buffers.
    std::vector<Buffer> m_buffers;
    SpscQueue<MemoryView> m_free_mem_views;
    SpscQueue<TransferDoneCallbackAsyncInfer> m_done_cbs;
    AccumulatorPtr m_queue_size_accumulator;
    std::mutex m_release_buffer_mutex;

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
    PipelineObject(const std::string &name);
    virtual ~PipelineObject() = default;
    PipelineObject(PipelineObject &&) noexcept = default;
    PipelineObject& operator=(PipelineObject &&) noexcept = default;

    const std::string &name() const;

    static std::string create_element_name(const std::string &element_name, const std::string &stream_name, uint8_t stream_index);

private:
    std::string m_name;
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
    hailo_status wait_for_finish();
    hailo_status clear_abort();
    virtual hailo_status run_push(PipelineBuffer &&buffer);
    void run_push_async(PipelineBuffer &&buffer);
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional = PipelineBuffer());
    void set_push_complete_callback(PushCompleteCallback push_complete_callback);
    void set_pull_complete_callback(PullCompleteCallback pull_complete_callback);
    void set_next(PipelinePad *next);
    void set_prev(PipelinePad *prev);
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
    PipelineElement(const std::string &name, DurationCollector &&duration_collector,
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
    hailo_status clear_abort();
    hailo_status wait_for_finish();
    AccumulatorPtr get_fps_accumulator();
    AccumulatorPtr get_latency_accumulator();
    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators();
    std::vector<PipelinePad> &sinks();
    std::vector<PipelinePad> &sources();
    const std::vector<PipelinePad> &sinks() const;
    const std::vector<PipelinePad> &sources() const;
    virtual std::string description() const;
    virtual void set_on_cant_pull_callback(std::function<void()> callback);
    virtual void set_on_can_pull_callback(std::function<void()> callback);
    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name);
    hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done);
    virtual Expected<bool> are_buffer_pools_full();
    virtual hailo_status fill_buffer_pools(bool is_dma_able);
    void handle_non_recoverable_async_error(hailo_status error_status);

protected:
    DurationCollector m_duration_collector;
    std::shared_ptr<std::atomic<hailo_status>> m_pipeline_status;
    std::vector<PipelinePad> m_sinks;
    std::vector<PipelinePad> m_sources;
    PipelineDirection m_pipeline_direction;

    std::function<void()> m_cant_pull_callback;
    std::function<void()> m_can_pull_callback;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) = 0;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) = 0;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) = 0;
    virtual std::vector<PipelinePad*> execution_pads() = 0;
    virtual hailo_status execute_activate();
    virtual hailo_status execute_deactivate();
    virtual hailo_status execute_post_deactivate(bool should_clear_abort);
    virtual hailo_status execute_clear();
    virtual hailo_status execute_flush();
    virtual hailo_status execute_abort();
    virtual hailo_status execute_clear_abort();
    virtual hailo_status execute_wait_for_finish();

    virtual hailo_status execute(std::function<hailo_status(PipelinePad*)>);

    friend class PipelinePad;
};

// An element with one source pad only (generates data)
class SourceElement : public PipelineElement
{
public:
    SourceElement(const std::string &name, DurationCollector &&duration_collector,
                  std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                  PipelineDirection pipeline_direction);
    PipelinePad &source();

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
};

// An element with one sink pad only (consumes data)
class SinkElement : public PipelineElement
{
public:
    SinkElement(const std::string &name, DurationCollector &&duration_collector,
                std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                PipelineDirection pipeline_direction);
    PipelinePad &sink();

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
};

// Transfers data from one pad to another pad. Has one sink pad and one source pad.
class IntermediateElement : public PipelineElement
{
public:
    IntermediateElement(const std::string &name, DurationCollector &&duration_collector,
                        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                        PipelineDirection pipeline_direction);
    virtual PipelinePad &next_pad() = 0;

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
};

class FilterElement : public IntermediateElement
{
public:
    FilterElement(const std::string &name, DurationCollector &&duration_collector,
                  std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                  PipelineDirection pipeline_direction, BufferPoolPtr buffer_pool, std::chrono::milliseconds timeout);
    virtual ~FilterElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> are_buffer_pools_full() override;
    virtual hailo_status fill_buffer_pools(bool is_dma_able) override;
    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

protected:
    // The optional buffer functions as an output buffer that the user can write to instead of acquiring a new buffer
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) = 0;
    BufferPoolPtr m_pool;
    std::chrono::milliseconds m_timeout;
};

class BaseQueueElement : public IntermediateElement
{
public:
    virtual ~BaseQueueElement();

    hailo_status set_timeout(std::chrono::milliseconds timeout);
    virtual std::string description() const override;

    static constexpr auto INIFINITE_TIMEOUT() { return std::chrono::milliseconds(HAILO_INFINITE); }

protected:
    static Expected<SpscQueue<PipelineBuffer>> create_queue(size_t queue_size, EventPtr shutdown_event);
    BaseQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        Event &&activation_event, Event &&deactivation_event,
        PipelineDirection pipeline_direction);

    hailo_status pipeline_status();

    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_clear() override;
    virtual hailo_status execute_clear_abort() override;
    virtual hailo_status execute_wait_for_finish() override;

    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> are_buffer_pools_full() override;
    virtual hailo_status fill_buffer_pools(bool is_dma_able) override;

    /// Starts/stops the queue thread. This functions needs to be called on subclasses ctor and dtor
    /// accordingly because otherwise, if we will start/stop thread in this class we will face pure-call
    /// to `run_in_thread`.
    /// This functions don't return status because they are meant to be called on ctor and dtor 
    virtual void start_thread();
    virtual void stop_thread();

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

    virtual hailo_status run_in_thread() = 0;
    virtual std::string thread_name() = 0;

    SpscQueue<PipelineBuffer> m_queue;
    EventPtr m_shutdown_event;
    std::chrono::milliseconds m_timeout;
    std::thread m_thread;
    std::atomic_bool m_is_thread_running;
    Event m_activation_event;
    Event m_deactivation_event;
    AccumulatorPtr m_queue_size_accumulator;
    std::atomic_bool m_is_run_in_thread_running;
    std::condition_variable m_cv;
    std::mutex m_mutex;
};

class PushQueueElement : public BaseQueueElement
{
public:
    static Expected<std::shared_ptr<PushQueueElement>> create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction = PipelineDirection::PUSH);
    static Expected<std::shared_ptr<PushQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH);
    PushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
        PipelineDirection pipeline_direction, bool should_start_thread = true);
    virtual ~PushQueueElement();

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;

protected:
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status run_in_thread() override;
    virtual std::string thread_name() override { return "PUSH_QUEUE"; };
    virtual hailo_status execute_abort() override;
};

class AsyncPushQueueElement : public PushQueueElement
{
public:
    static Expected<std::shared_ptr<AsyncPushQueueElement>> create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction = PipelineDirection::PUSH);
    static Expected<std::shared_ptr<AsyncPushQueueElement>> create(const std::string &name, const ElementBuildParams &build_params,
        PipelineDirection pipeline_direction);
    AsyncPushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
        PipelineDirection pipeline_direction);

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;

protected:
    virtual hailo_status run_in_thread() override;
    virtual std::string thread_name() override { return "ASYNC_PUSH_Q"; };
    virtual void start_thread() override;
};

class PullQueueElement : public BaseQueueElement
{
public:
    static Expected<std::shared_ptr<PullQueueElement>> create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction = PipelineDirection::PULL);
    static Expected<std::shared_ptr<PullQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL);
    PullQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
        PipelineDirection pipeline_direction);
    virtual ~PullQueueElement();

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;

    virtual void set_on_cant_pull_callback(std::function<void()> callback) override
    {
        m_cant_pull_callback = callback;
        m_queue.set_on_cant_enqueue_callback([this] () {
            m_cant_pull_callback();
        });
    }

    virtual void set_on_can_pull_callback(std::function<void()> callback) override
    {
        m_can_pull_callback = callback;
        m_queue.set_on_can_enqueue_callback([this] () {
            m_can_pull_callback();
        });
    }

protected:
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status run_in_thread() override;
    virtual std::string thread_name() override { return "PULL_QUEUE"; };
};

class UserBufferQueueElement : public PullQueueElement
{
public:
    static Expected<std::shared_ptr<UserBufferQueueElement>> create(const std::string &name, std::chrono::milliseconds timeout,
        hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL);
    static Expected<std::shared_ptr<UserBufferQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL);
    UserBufferQueueElement(SpscQueue<PipelineBuffer> &&queue, SpscQueue<PipelineBuffer> &&full_buffer_queue, EventPtr shutdown_event,
        const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
        PipelineDirection pipeline_direction);

    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

    virtual void set_on_cant_pull_callback(std::function<void()> callback) override
    {
        m_cant_pull_callback = callback;
    }

    virtual void set_on_can_pull_callback(std::function<void()> callback) override
    {
        m_can_pull_callback = callback;
    }

protected:
    virtual hailo_status execute_clear() override;
    virtual hailo_status run_in_thread() override;

private:
    SpscQueue<PipelineBuffer> m_full_buffer_queue;
};

class BaseMuxElement : public PipelineElement
{
public:
    BaseMuxElement(size_t sink_count, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        BufferPoolPtr buffer_pool, PipelineDirection pipeline_direction = PipelineDirection::PULL);
    virtual ~BaseMuxElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> are_buffer_pools_full() override;
    virtual hailo_status fill_buffer_pools(bool is_dma_able) override;

protected:
    virtual Expected<PipelineBuffer> action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional) = 0;
    virtual std::vector<PipelinePad*> execution_pads() override;

    std::chrono::milliseconds m_timeout;
    BufferPoolPtr m_pool;

private:
    bool has_all_sinks_arrived();
    std::unordered_map<std::string, bool> m_sink_has_arrived;
    std::mutex m_mutex;
    std::unordered_map<std::string, uint32_t> m_index_of_sink;
    std::unordered_map<std::string, PipelineBuffer> m_input_buffers;
    std::vector<PipelinePad*> m_next_pads;
    std::condition_variable m_cv;
};

class BaseDemuxElement : public PipelineElement
{
public:
    BaseDemuxElement(size_t source_count, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        std::vector<BufferPoolPtr> pools, PipelineDirection pipeline_direction);
    virtual ~BaseDemuxElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    hailo_status set_timeout(std::chrono::milliseconds timeout);
    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> are_buffer_pools_full() override;
    virtual hailo_status fill_buffer_pools(bool is_dma_able) override;
    hailo_status fill_buffer_pool(bool is_dma_able, size_t pool_id);

protected:
    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_abort() override;
    virtual Expected<std::vector<PipelineBuffer>> action(PipelineBuffer &&input) = 0;
    virtual std::vector<PipelinePad*> execution_pads() override;

    std::chrono::milliseconds m_timeout;
    std::vector<BufferPoolPtr> m_pools;

private:
    bool were_all_srcs_arrived();

    std::atomic_bool m_is_activated;
    std::atomic_bool m_was_stream_aborted;
    std::unordered_map<std::string, uint32_t> m_index_of_source;
    std::vector<bool> m_was_source_called;
    std::vector<PipelineBuffer> m_buffers_for_action;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<PipelinePad*> m_next_pads;
};

enum class AccumulatorType
{
    FPS,
    LATENCY,
    QUEUE_SIZE
};

} /* namespace hailort */

#endif /* _HAILO_PIPELINE_HPP_ */
