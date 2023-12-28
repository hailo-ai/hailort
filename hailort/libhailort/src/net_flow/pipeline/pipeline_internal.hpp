/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline_internal.hpp
 * @brief Hailo Infer Pipeline elements
 **/

#ifndef _HAILO_PIPELINE_ELEMENTS_HPP_
#define _HAILO_PIPELINE_ELEMENTS_HPP_

#include "net_flow/pipeline/pipeline.hpp"

namespace hailort
{

class AsyncPipeline; // Forward declaration

struct ElementBuildParams
{
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status;
    std::chrono::milliseconds timeout;
    EventPtr shutdown_event;
    size_t buffer_pool_size_internal;
    size_t buffer_pool_size_edges;
    hailo_pipeline_elem_stats_flags_t elem_stats_flags;
    hailo_vstream_stats_flags_t vstream_stats_flags;
};

class PipelineElementInternal : public PipelineElement
{
public:
    PipelineElementInternal(const std::string &name, DurationCollector &&duration_collector,
                    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    PipelineElementInternal &operator=(PipelineElementInternal &&other) = delete;

protected:
    void handle_non_recoverable_async_error(hailo_status error_status);
    std::weak_ptr<AsyncPipeline> m_async_pipeline;

    friend class PipelinePad;
};


// An element with one source pad only (generates data)
class SourceElement : public PipelineElementInternal
{
public:
    SourceElement(const std::string &name, DurationCollector &&duration_collector,
                  std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                  PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    PipelinePad &source();

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
};

// An element with one sink pad only (consumes data)
class SinkElement : public PipelineElementInternal
{
public:
    SinkElement(const std::string &name, DurationCollector &&duration_collector,
                std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    PipelinePad &sink();

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
    virtual hailo_status execute_terminate(hailo_status error_status) override;
    virtual hailo_status execute_dequeue_user_buffers(hailo_status error_status) override;
};

// Transfers data from one pad to another pad. Has one sink pad and one source pad.
class IntermediateElement : public PipelineElementInternal
{
public:
    IntermediateElement(const std::string &name, DurationCollector &&duration_collector,
                        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual PipelinePad &next_pad() = 0;

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
};

class FilterElement : public IntermediateElement
{
public:
    FilterElement(const std::string &name, DurationCollector &&duration_collector,
                  std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                  PipelineDirection pipeline_direction, BufferPoolPtr buffer_pool, std::chrono::milliseconds timeout,
                  std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~FilterElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> can_push_buffer_upstream(const uint32_t source_index) override;
    virtual hailo_status fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t source_index) override;
    virtual Expected<bool> can_push_buffer_upstream(const std::string &source_name) override;
    virtual hailo_status fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &source_name) override;

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

protected:
    // The optional buffer functions as an output buffer that the user can write to instead of acquiring a new buffer
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) = 0;
    virtual hailo_status execute_dequeue_user_buffers(hailo_status error_status) override;

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
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);

    hailo_status pipeline_status();

    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_clear() override;
    virtual hailo_status execute_clear_abort() override;
    virtual hailo_status execute_wait_for_finish() override;

    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> can_push_buffer_upstream(const uint32_t source_index) override;
    virtual Expected<bool> can_push_buffer_downstream(const uint32_t source_index) override;
    virtual hailo_status fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t source_index) override;
    virtual Expected<bool> can_push_buffer_upstream(const std::string &source_name) override;
    virtual Expected<bool> can_push_buffer_downstream(const std::string &source_name) override;
    virtual hailo_status fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &source_name) override;

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
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction = PipelineDirection::PUSH,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<PushQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH, std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    PushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline, bool should_start_thread = true);
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
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::shared_ptr<AsyncPipeline> async_pipeline,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH);
    static Expected<std::shared_ptr<AsyncPushQueueElement>> create(const std::string &name, const ElementBuildParams &build_params,
        std::shared_ptr<AsyncPipeline> async_pipeline, PipelineDirection pipeline_direction);
    AsyncPushQueueElement(SpscQueue<PipelineBuffer> &&queue, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual hailo_status execute_dequeue_user_buffers(hailo_status error_status) override;

protected:
    virtual hailo_status run_in_thread() override;
    virtual std::string thread_name() override { return "ASYNC_PUSH_Q"; };
    virtual void start_thread() override;
    virtual hailo_status execute_terminate(hailo_status error_status);
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_deactivate() override;
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

protected:
    virtual hailo_status execute_clear() override;
    virtual hailo_status run_in_thread() override;

private:
    SpscQueue<PipelineBuffer> m_full_buffer_queue;
};

class BaseMuxElement : public PipelineElementInternal
{
public:
    virtual ~BaseMuxElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> can_push_buffer_upstream(const uint32_t source_index) override;
    virtual hailo_status fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t source_index) override;
    virtual Expected<bool> can_push_buffer_upstream(const std::string &source_name) override;
    virtual hailo_status fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &source_name) override;

protected:
    BaseMuxElement(size_t sink_count, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        BufferPoolPtr buffer_pool, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual hailo_status execute_terminate(hailo_status error_status) override;
    virtual Expected<PipelineBuffer> action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional) = 0;
    virtual std::vector<PipelinePad*> execution_pads() override;
    virtual hailo_status execute_dequeue_user_buffers(hailo_status error_status) override;

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

class BaseDemuxElement : public PipelineElementInternal
{
public:
    virtual ~BaseDemuxElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    hailo_status set_timeout(std::chrono::milliseconds timeout);

    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> can_push_buffer_upstream(const uint32_t source_index) override;
    virtual hailo_status fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t source_index) override;
    virtual Expected<bool> can_push_buffer_upstream(const std::string &source_name) override;
    virtual hailo_status fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &source_name) override;

    virtual Expected<uint32_t> get_source_index_from_source_name(const std::string &source_name) override;

protected:
    BaseDemuxElement(size_t source_count, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        std::vector<BufferPoolPtr> pools, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_abort() override;
    virtual Expected<std::vector<PipelineBuffer>> action(PipelineBuffer &&input) = 0;
    virtual std::vector<PipelinePad*> execution_pads() override;
    virtual hailo_status execute_dequeue_user_buffers(hailo_status error_status) override;

    std::chrono::milliseconds m_timeout;
    std::vector<BufferPoolPtr> m_pools;

private:
    bool were_all_srcs_arrived();

    std::atomic_bool m_is_activated;
    std::atomic_bool m_was_stream_aborted;
    std::unordered_map<std::string, uint32_t> m_source_name_to_index;
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

#endif /* _HAILO_PIPELINE_ELEMENTS_HPP_ */
