/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file edge_elements.hpp
 * @brief all edge elements in the pipeline (sinks and sources)
 **/

#ifndef _HAILO_EDGE_ELEMENTS_HPP_
#define _HAILO_EDGE_ELEMENTS_HPP_

namespace hailort
{
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

class HwWriteElement : public SinkElement
{
public:
    static Expected<std::shared_ptr<HwWriteElement>> create(std::shared_ptr<InputStreamBase> stream, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH);
    HwWriteElement(std::shared_ptr<InputStreamBase> stream, const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr got_flush_event, PipelineDirection pipeline_direction);
    virtual ~HwWriteElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_clear() override;
    virtual hailo_status execute_flush() override;
    virtual hailo_status execute_abort() override;
    virtual hailo_status execute_clear_abort() override;
    virtual std::string description() const override;

private:
    std::shared_ptr<InputStreamBase> m_stream;
    EventPtr m_got_flush_event;
};

class LastAsyncElement : public SinkElement
{
public:
    static Expected<std::shared_ptr<LastAsyncElement>> create(const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_stats_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, size_t queue_size, size_t frame_size,
        EventPtr shutdown_event, std::shared_ptr<AsyncPipeline> async_pipeline);
    static Expected<std::shared_ptr<LastAsyncElement>> create(const std::string &name, const ElementBuildParams &build_params,
        size_t frame_size, std::shared_ptr<AsyncPipeline> async_pipeline);
    LastAsyncElement(const std::string &name, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineBufferPoolPtr buffer_pool, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~LastAsyncElement() = default;

    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status execute_activate() override;

    virtual hailo_status enqueue_execution_buffer(PipelineBuffer &&pipeline_buffer) override;

    virtual Expected<bool> can_push_buffer_upstream(uint32_t frames_count) override;

    virtual hailo_status execute_post_deactivate(bool /*should_clear_abort*/) override { return HAILO_SUCCESS; };
    virtual hailo_status execute_deactivate() override { return HAILO_SUCCESS; };
    virtual hailo_status execute_dequeue_user_buffers(hailo_status error_status) override;

    virtual PipelineBufferPoolPtr get_buffer_pool(const std::string &/*pad_name*/) const override
    {
        return m_pool;
    }

private:
    PipelineBufferPoolPtr m_pool;
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

class HwReadElement : public SourceElement
{
public:
    static Expected<std::shared_ptr<HwReadElement>> create(std::shared_ptr<OutputStreamBase> stream, const std::string &name,
        const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL);
    HwReadElement(std::shared_ptr<OutputStreamBase> stream, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineDirection pipeline_direction);
    virtual ~HwReadElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_clear() override;
    virtual hailo_status execute_flush() override;
    virtual hailo_status execute_abort() override;
    virtual hailo_status execute_clear_abort() override;
    uint32_t get_invalid_frames_count();
    virtual std::string description() const override;

    PipelinePad &next_pad_downstream()
    {
        return *m_sources[0].next();
    }

private:
    std::shared_ptr<OutputStreamBase> m_stream;
    std::chrono::milliseconds m_timeout;
    EventPtr m_shutdown_event;
    WaitOrShutdown m_activation_wait_or_shutdown;
};


} /* namespace hailort */

#endif /* _HAILO_EDGE_ELEMENTS_HPP_ */
