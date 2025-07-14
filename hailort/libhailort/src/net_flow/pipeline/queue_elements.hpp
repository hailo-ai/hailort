/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file queue_elements.hpp
 * @brief all queue elements in the pipeline.
 **/

#ifndef _HAILO_QUEUE_ELEMENTS_HPP_
#define _HAILO_QUEUE_ELEMENTS_HPP_

#include "net_flow/pipeline/pipeline_internal.hpp"

namespace hailort
{

class BaseQueueElement : public IntermediateElement
{
public:
    virtual ~BaseQueueElement();

    hailo_status set_timeout(std::chrono::milliseconds timeout);
    virtual std::string description() const override;

    static constexpr auto INIFINITE_TIMEOUT() { return std::chrono::milliseconds(HAILO_INFINITE); }

    virtual PipelineBufferPoolPtr get_buffer_pool(const std::string &/*pad_name*/) const override
    {
        return m_pool;
    }

    virtual void add_element_to_stringstream(std::stringstream &stream, const PipelinePad &source) const override;

protected:
    static Expected<SpscQueue<PipelineBuffer>> create_queue(size_t queue_size, EventPtr shutdown_event);
    BaseQueueElement(SpscQueue<PipelineBuffer> &&queue, PipelineBufferPoolPtr buffer_pool,
        EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        Event &&activation_event, Event &&deactivation_event,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);

    hailo_status pipeline_status();

    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_clear() override;
    virtual hailo_status execute_clear_abort() override;

    /// Starts/stops the queue thread. This functions needs to be called on subclasses ctor and dtor
    /// accordingly because otherwise, if we will start/stop thread in this class we will face pure-call
    /// to `run_in_thread`.
    /// This functions don't return status because they are meant to be called on ctor and dtor 
    virtual void start_thread();
    virtual void stop_thread();
    void register_thread();

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

    virtual hailo_status run_in_thread() = 0;
    virtual std::string thread_name() = 0;

    hailo_status clear_queue();

    std::mutex m_dequeue_mutex;
    SpscQueue<PipelineBuffer> m_queue;
    EventPtr m_shutdown_event;
    std::chrono::milliseconds m_timeout;
    std::thread m_thread;
    uint32_t m_thread_id;
    std::condition_variable m_thread_ready_cv;
    std::mutex m_thread_ready_mutex;
    bool m_thread_ready;
    std::atomic_bool m_is_thread_running;
    Event m_activation_event;
    Event m_deactivation_event;
    AccumulatorPtr m_queue_size_accumulator;
    PipelineBufferPoolPtr m_pool;
};

class PushQueueElement : public BaseQueueElement
{
public:
    static Expected<std::shared_ptr<PushQueueElement>> create(const std::string &name, std::chrono::milliseconds timeout,
        size_t queue_size, size_t frame_size, hailo_pipeline_elem_stats_flags_t flags, hailo_vstream_stats_flags_t vs_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<PushQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        size_t frame_size, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    PushQueueElement(SpscQueue<PipelineBuffer> &&queue, PipelineBufferPoolPtr buffer_pool, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
        std::shared_ptr<AsyncPipeline> async_pipeline, bool should_start_thread);
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
        size_t queue_size, size_t frame_size, bool is_empty, bool interacts_with_hw, hailo_pipeline_elem_stats_flags_t flags,
        hailo_vstream_stats_flags_t vstream_stats_flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::shared_ptr<AsyncPipeline> async_pipeline, bool is_entry = false);
    static Expected<std::shared_ptr<AsyncPushQueueElement>> create(const std::string &name, const ElementBuildParams &build_params,
        size_t frame_size, bool is_empty, bool interacts_with_hw, std::shared_ptr<AsyncPipeline> async_pipeline, bool is_entry = false);
    AsyncPushQueueElement(SpscQueue<PipelineBuffer> &&queue, PipelineBufferPoolPtr buffer_pool, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event,
        std::shared_ptr<AsyncPipeline> async_pipeline);

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual hailo_status execute_dequeue_user_buffers(hailo_status error_status) override;
    virtual Expected<bool> can_push_buffer_downstream(uint32_t frames_count) override;

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
        size_t queue_size, size_t frame_size, hailo_pipeline_elem_stats_flags_t flags, hailo_vstream_stats_flags_t vstream_stats_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    static Expected<std::shared_ptr<PullQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        size_t frame_size, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    PullQueueElement(SpscQueue<PipelineBuffer> &&queue, PipelineBufferPoolPtr buffer_pool, EventPtr shutdown_event, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, AccumulatorPtr &&queue_size_accumulator,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event, Event &&deactivation_event);
    virtual ~PullQueueElement();

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;

protected:
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status run_in_thread() override;
    virtual std::string thread_name() override { return "PULL_QUEUE"; };
    virtual hailo_status execute_abort() override;

};

class UserBufferQueueElement : public PullQueueElement
{
public:
    static Expected<std::shared_ptr<UserBufferQueueElement>> create(const std::string &name, std::chrono::milliseconds timeout,
        hailo_pipeline_elem_stats_flags_t flags, hailo_vstream_stats_flags_t vstream_stats_flags,
        size_t frame_size, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    static Expected<std::shared_ptr<UserBufferQueueElement>> create(const std::string &name, const hailo_vstream_params_t &vstream_params,
        size_t frame_size, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    UserBufferQueueElement(SpscQueue<PipelineBuffer> &&queue, PipelineBufferPoolPtr buffer_pool,
        EventPtr shutdown_event, const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        AccumulatorPtr &&queue_size_accumulator, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, Event &&activation_event,
        Event &&deactivation_event);

    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    hailo_status set_buffer_pool_buffer_size(uint32_t frame_size);

virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

protected:
    virtual hailo_status run_in_thread() override;
};

class MultiPushQueue : public PipelineElementInternal
{
public:
    static Expected<std::shared_ptr<MultiPushQueue>> create(const std::string &name,
        const ElementBuildParams &build_params, std::vector<size_t> frame_sizes, bool is_empty,
        bool interacts_with_hw, std::shared_ptr<AsyncPipeline> async_pipeline, bool is_entry = false);
    MultiPushQueue(std::vector<SpscQueue<PipelineBuffer>> &&queues, size_t sink_count, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline, EventPtr shutdown_event,
        std::vector<PipelineBufferPoolPtr> &&buffer_pools);

    virtual ~MultiPushQueue();

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

    static constexpr auto INIFINITE_TIMEOUT() { return std::chrono::milliseconds(HAILO_INFINITE); }
    PipelinePad &next_pad();

    virtual PipelineBufferPoolPtr get_buffer_pool(const std::string &pad_name) const override
    {
        return m_buffer_pools.at(pad_name);
    }
    virtual std::string description() const override;
    virtual void add_element_to_stringstream(std::stringstream &stream, const PipelinePad &source) const override;

protected:
    static Expected<SpscQueue<PipelineBuffer>> create_queue(size_t queue_size, EventPtr shutdown_event);
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status execute_terminate(hailo_status error_status) override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_dequeue_user_buffers(hailo_status error_status) override;
    virtual std::vector<PipelinePad*> execution_pads() override;
    hailo_status clear_queues();
    PipelinePad &next_pad_downstream()
    {
        return *m_sources[0].next();
    }

private:
    std::string thread_name() { return "MULT_PUSH_QUEUE"; };
    void register_thread();
    void start_thread();
    void stop_thread();
    hailo_status run_in_thread();

    std::unordered_map<std::string, uint32_t> m_sink_name_to_index;
    std::unordered_map<std::string, SpscQueue<PipelineBuffer>> m_queues;
    std::chrono::milliseconds m_timeout;
    uint32_t m_thread_id;
    bool m_thread_ready;
    std::atomic_bool m_is_thread_running;
    std::thread m_thread;
    std::mutex m_thread_ready_mutex;
    std::mutex m_queues_operations_mutex;
    std::condition_variable m_thread_ready_cv;
    std::condition_variable m_all_queues_have_buffer_cv;

    EventPtr m_shutdown_event;
    std::unordered_map<std::string, PipelineBufferPoolPtr> m_buffer_pools;
};

} /* namespace hailort */

#endif /* _HAILO_QUEUE_ELEMENTS_HPP_ */
