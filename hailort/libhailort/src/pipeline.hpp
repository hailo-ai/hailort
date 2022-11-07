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
#include "hailo/runtime_statistics.hpp"
#include "thread_safe_queue.hpp"

#include <memory>
#include <thread>
#include <sstream>
#include <functional>

namespace hailort
{

using PipelineTimePoint = std::chrono::steady_clock::time_point;
#define BUFFER_POOL_DEFAULT_QUEUE_TIMEOUT (std::chrono::milliseconds(10000))
#define DEFAULT_NUM_FRAMES_BEFORE_COLLECTION_START (100)

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

    private:
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
    PipelineBuffer(MemoryView view, bool should_measure = false);
    PipelineBuffer(Buffer &&buffer, BufferPoolPtr pool, bool should_measure = false);
    ~PipelineBuffer();
    
    PipelineBuffer(const PipelineBuffer &) = delete;
    PipelineBuffer &operator=(const PipelineBuffer &) = delete;
    PipelineBuffer(PipelineBuffer &&other);
    PipelineBuffer &operator=(PipelineBuffer &&other);
    explicit operator bool() const;

    uint8_t* data();
    size_t size() const;
    MemoryView as_view();
    Type get_type() const;
    Metadata get_metadata() const;
    void set_metadata(Metadata &&val);

private:
    Type m_type;
    Buffer m_buffer;
    bool m_should_release_buffer;
    BufferPoolPtr m_pool;
    MemoryView m_view;
    Metadata m_metadata;

    static PipelineTimePoint add_timestamp(bool should_measure);
};

// The buffer pool has to be created as a shared pointer (via the create function) because we use shared_from_this(),
// which is only allowed if there is already a shared pointer pointing to "this"!
class BufferPool : public std::enable_shared_from_this<BufferPool>
{
public:
    static Expected<BufferPoolPtr> create(size_t buffer_size, size_t buffer_count, EventPtr shutdown_event,
        hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags);
    BufferPool(size_t buffer_size, bool measure_vstream_latency, SpscQueue<Buffer> &&free_buffers, AccumulatorPtr &&queue_size_accumulator);
    virtual ~BufferPool() = default;

    size_t buffer_size();
    Expected<PipelineBuffer> acquire_buffer(std::chrono::milliseconds timeout);
    AccumulatorPtr get_queue_size_accumulator();
    Expected<PipelineBuffer> get_available_buffer(PipelineBuffer &&optional, std::chrono::milliseconds timeout);

private:
    hailo_status release_buffer(Buffer &&buffer);

    const size_t m_buffer_size;
    const bool m_measure_vstream_latency;
    SpscQueue<Buffer> m_free_buffers;
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
    hailo_status post_deactivate();
    hailo_status clear();
    hailo_status flush();
    hailo_status abort();
    hailo_status wait_for_finish();
    hailo_status resume();
    virtual hailo_status run_push(PipelineBuffer &&buffer);
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
                    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    virtual ~PipelineElement() = default;

    PipelineElement(PipelineElement &&other) = delete;
    PipelineElement(const PipelineElement &) = delete;
    PipelineElement &operator=(const PipelineElement &) = delete;
    PipelineElement &operator=(PipelineElement &&other) = delete;

    hailo_status activate();
    hailo_status deactivate();
    hailo_status post_deactivate();
    hailo_status clear();
    hailo_status flush();
    hailo_status abort();
    hailo_status resume();
    hailo_status wait_for_finish();
    virtual hailo_status run_push(PipelineBuffer &&buffer) = 0;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) = 0;
    AccumulatorPtr get_fps_accumulator();
    AccumulatorPtr get_latency_accumulator();
    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators();
    std::vector<PipelinePad> &sinks();
    std::vector<PipelinePad> &sources();
    const std::vector<PipelinePad> &sinks() const;
    const std::vector<PipelinePad> &sources() const;
    virtual std::string description() const;

    virtual void set_on_cant_pull_callback(std::function<void()> callback)
    {
        m_cant_pull_callback = callback;
    }

    virtual void set_on_can_pull_callback(std::function<void()> callback)
    {
        m_can_pull_callback = callback;
    }

protected:
    DurationCollector m_duration_collector;
    std::shared_ptr<std::atomic<hailo_status>> m_pipeline_status;
    std::vector<PipelinePad> m_sinks;
    std::vector<PipelinePad> m_sources;

    std::function<void()> m_cant_pull_callback;
    std::function<void()> m_can_pull_callback;

    virtual std::vector<PipelinePad*> execution_pads() = 0;
    virtual hailo_status execute_activate();
    virtual hailo_status execute_deactivate();
    virtual hailo_status execute_post_deactivate();
    virtual hailo_status execute_clear();
    virtual hailo_status execute_flush();
    virtual hailo_status execute_abort();
    virtual hailo_status execute_resume();
    virtual hailo_status execute_wait_for_finish();

    virtual hailo_status execute(std::function<hailo_status(PipelinePad*)>);
};

// An element with one source pad only (generates data)
class SourceElement : public PipelineElement
{
public:
    SourceElement(const std::string &name, DurationCollector &&duration_collector,
                  std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    PipelinePad &source();

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
};

// An element with one sink pad only (consumes data)
class SinkElement : public PipelineElement
{
public:
    SinkElement(const std::string &name, DurationCollector &&duration_collector,
                std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    PipelinePad &sink();

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
};

// Transfers data from one pad to another pad. Has one sink pad and one source pad.
class IntermediateElement : public PipelineElement
{
public:
    IntermediateElement(const std::string &name, DurationCollector &&duration_collector,
                        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    virtual PipelinePad &next_pad() = 0;

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
};

class FilterElement : public IntermediateElement
{
public:
    FilterElement(const std::string &name, DurationCollector &&duration_collector,
                  std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    virtual ~FilterElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

protected:
    // The optional buffer functions as an output buffer that the user can write to instead of acquiring a new buffer
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) = 0;
};

class BaseQueueElement : public IntermediateElement
{
public:
    virtual ~BaseQueueElement() = default;

    hailo_status set_timeout(std::chrono::milliseconds timeout);
    virtual std::string description() const override;

    static constexpr auto INIFINITE_TIMEOUT() { return std::chrono::milliseconds(HAILO_INFINITE); }

protected:
    static Expected<SpscQueue<PipelineBuffer>> create_queue(size_t queue_size, EventPtr shutdown_event);
    BaseQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        Event &&activation_event, Event &&deactivation_event);

    hailo_status pipeline_status();

    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_post_deactivate() override;
    virtual hailo_status execute_clear() override;
    virtual hailo_status execute_resume() override;
    virtual hailo_status execute_wait_for_finish() override;

    /// Starts/stops the queue thread. This functions needs to be called on subclasses ctor and dtor
    /// accordingly because otherwise, if we will start/stop thread in this class we will face pure-call
    /// to `run_in_thread`.
    /// This functions don't return status because they are meant to be called on ctor and dtor 
    void start_thread();
    void stop_thread();

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

    virtual hailo_status run_in_thread() = 0;

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
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    static Expected<std::shared_ptr<PushQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    PushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event);
    virtual ~PushQueueElement();

    virtual hailo_status run_push(PipelineBuffer &&buffer) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;

protected:
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status run_in_thread() override;
    virtual hailo_status execute_abort() override;
};

class PullQueueElement : public BaseQueueElement
{
public:
    static Expected<std::shared_ptr<PullQueueElement>> create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    static Expected<std::shared_ptr<PullQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    PullQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event);
    virtual ~PullQueueElement();

    virtual hailo_status run_push(PipelineBuffer &&buffer) override;
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
};

class UserBufferQueueElement : public PullQueueElement
{
public:
    static Expected<std::shared_ptr<UserBufferQueueElement>> create(const std::string &name, std::chrono::milliseconds timeout,
        hailo_pipeline_elem_stats_flags_t flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    static Expected<std::shared_ptr<UserBufferQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    UserBufferQueueElement(SpscQueue<PipelineBuffer> &&queue, SpscQueue<PipelineBuffer> &&full_buffer_queue, EventPtr shutdown_event,
        const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event);

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
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    virtual ~BaseMuxElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

protected:
    virtual Expected<PipelineBuffer> action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional) = 0;
    virtual std::vector<PipelinePad*> execution_pads() override;

    std::chrono::milliseconds m_timeout;
};

class BaseDemuxElement : public PipelineElement
{
public:
    BaseDemuxElement(size_t source_count, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    virtual ~BaseDemuxElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    hailo_status set_timeout(std::chrono::milliseconds timeout);

protected:
    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status execute_post_deactivate() override;
    virtual hailo_status execute_abort() override;
    virtual Expected<std::vector<PipelineBuffer>> action(PipelineBuffer &&input) = 0;
    virtual std::vector<PipelinePad*> execution_pads() override;

    std::chrono::milliseconds m_timeout;

private:
    bool were_all_sinks_called();
    PipelinePad &next_pad();

    std::atomic_bool m_is_activated;
    std::atomic_bool m_was_stream_aborted;
    std::unordered_map<const PipelinePad*, uint32_t> m_index_of_source;
    std::vector<bool> m_was_source_called;
    std::vector<PipelineBuffer> m_buffers_for_action;
    std::mutex m_mutex;
    std::condition_variable m_cv;
};

enum class AccumulatorType
{
    FPS,
    LATENCY,
    QUEUE_SIZE
};

} /* namespace hailort */

#endif /* _HAILO_PIPELINE_HPP_ */
