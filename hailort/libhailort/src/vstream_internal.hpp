/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream.hpp
 * @brief Virtual Stream.
 *        Hence, the hierarchy is as follows:
 *        ------------------------------------------------------------------------------------------------
 *        |                                         BaseVStream                                          |  (Internal "interface")
 *        |                   ___________________________|___________________________                    |
 *        |                  /                                                       \                   |
 *        |            InputVStreamInternal                                OutputVStreamInternal         |  (Base classes)
 *        |               /                  \                               /           \               |
 *        |  InputVStreamImpl           InputVStreamClient    OuputVStreamImpl     OutputVStreamClient   |  (Actual implementations)
 *        ------------------------------------------------------------------------------------------------
 *  -- InputVStream                                 (External 'interface')
 *     |
 *     |__ std::share_ptr<InputVStreamInternal>
 *
 *  -- OutputVStream                                (External 'interface')
 *     |
 *     |__ std::share_ptr<OutputVStreamInternal>
 **/

#ifndef _HAILO_VSTREAM_INTERNAL_HPP_
#define _HAILO_VSTREAM_INTERNAL_HPP_

#include "pipeline.hpp"
#include "hailo/transform.hpp"
#include "hailo/stream.hpp"
#include "hailo/network_group.hpp"

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "hailort_rpc_client.hpp"
#endif // HAILO_SUPPORT_MULTI_PROCESS

namespace hailort
{

/*! Virtual stream base class */
class BaseVStream
{
public:
    BaseVStream(BaseVStream &&other) noexcept;
    BaseVStream& operator=(BaseVStream &&other) noexcept;
    virtual ~BaseVStream() = default;

    virtual size_t get_frame_size() const;
    virtual const hailo_vstream_info_t &get_info() const;
    virtual const hailo_format_t &get_user_buffer_format() const;
    virtual std::string name() const;
    virtual std::string network_name() const;
    virtual const std::map<std::string, AccumulatorPtr> &get_fps_accumulators() const;
    virtual const std::map<std::string, AccumulatorPtr> &get_latency_accumulators() const;
    virtual const std::map<std::string, std::vector<AccumulatorPtr>> &get_queue_size_accumulators() const;
    virtual AccumulatorPtr get_pipeline_latency_accumulator() const;
    virtual const std::vector<std::shared_ptr<PipelineElement>> &get_pipeline() const;

    virtual hailo_status abort();
    virtual hailo_status resume();
    virtual hailo_status start_vstream();
    virtual hailo_status stop_vstream();
    virtual hailo_status stop_and_clear();

protected:
    BaseVStream(const hailo_vstream_info_t &vstream_info, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr &&network_group_activated_event, hailo_status &output_status);
    BaseVStream() = default;

    virtual std::string get_pipeline_description() const = 0;

    hailo_vstream_info_t m_vstream_info;
    hailo_vstream_params_t m_vstream_params;
    bool m_measure_pipeline_latency;
    std::shared_ptr<PipelineElement> m_entry_element;
    std::vector<std::shared_ptr<PipelineElement>> m_pipeline;
    volatile bool m_is_activated;
    volatile bool m_is_aborted;
    std::shared_ptr<std::atomic<hailo_status>> m_pipeline_status;
    EventPtr m_shutdown_event;
    EventPtr m_network_group_activated_event;
    std::map<std::string, AccumulatorPtr> m_fps_accumulators;
    std::map<std::string, AccumulatorPtr> m_latency_accumulators;
    std::map<std::string, std::vector<AccumulatorPtr>> m_queue_size_accumulators;
    AccumulatorPtr m_pipeline_latency_accumulator;
};

/*! Input virtual stream, used to stream data to device */
class InputVStreamInternal : public BaseVStream
{
public:
    static Expected<std::shared_ptr<InputVStreamInternal>> create(const hailo_vstream_info_t &vstream_info,
        const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
        std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, EventPtr network_group_activated_event,
        AccumulatorPtr pipeline_latency_accumulator);
    InputVStreamInternal(InputVStreamInternal &&other) noexcept = default;
    InputVStreamInternal &operator=(InputVStreamInternal &&other) noexcept = default;
    virtual ~InputVStreamInternal() = default;

    virtual hailo_status write(const MemoryView &buffer) = 0;
    virtual hailo_status flush() = 0;

    virtual std::string get_pipeline_description() const override;

protected:
    InputVStreamInternal(const hailo_vstream_info_t &vstream_info, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr &&network_group_activated_event, hailo_status &output_status);
    InputVStreamInternal() = default;
};

/*! Output virtual stream, used to read data from device */
class OutputVStreamInternal : public BaseVStream
{
public:
    static Expected<std::shared_ptr<OutputVStreamInternal>> create(
        const hailo_vstream_info_t &vstream_info, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
        EventPtr network_group_activated_event, AccumulatorPtr pipeline_latency_accumulator);
    OutputVStreamInternal(OutputVStreamInternal &&other) noexcept = default;
    OutputVStreamInternal &operator=(OutputVStreamInternal &&other) noexcept = default;
    virtual ~OutputVStreamInternal() = default;


    virtual hailo_status read(MemoryView buffer) = 0;
    virtual std::string get_pipeline_description() const override;

protected:
    OutputVStreamInternal(const hailo_vstream_info_t &vstream_info, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr network_group_activated_event, hailo_status &output_status);
    OutputVStreamInternal() = default;
};

class InputVStreamImpl : public InputVStreamInternal
{
public:
    static Expected<std::shared_ptr<InputVStreamImpl>> create(const hailo_vstream_info_t &vstream_info,
        const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
        std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, EventPtr network_group_activated_event,
        AccumulatorPtr pipeline_latency_accumulator);
    InputVStreamImpl(InputVStreamImpl &&) noexcept = default;
    InputVStreamImpl(const InputVStreamImpl &) = delete;
    InputVStreamImpl &operator=(InputVStreamImpl &&) noexcept = default;
    InputVStreamImpl &operator=(const InputVStreamImpl &) = delete;
    virtual ~InputVStreamImpl();

    virtual hailo_status write(const MemoryView &buffer) override;
    virtual hailo_status flush() override;
private:
    InputVStreamImpl(const hailo_vstream_info_t &vstream_info, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr network_group_activated_event, hailo_status &output_status);
};

class OutputVStreamImpl : public OutputVStreamInternal
{
public:
    static Expected<std::shared_ptr<OutputVStreamImpl>> create(
        const hailo_vstream_info_t &vstream_info, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
        EventPtr network_group_activated_event, AccumulatorPtr pipeline_latency_accumulator);
    OutputVStreamImpl(OutputVStreamImpl &&) noexcept = default;
    OutputVStreamImpl(const OutputVStreamImpl &) = delete;
    OutputVStreamImpl &operator=(OutputVStreamImpl &&) noexcept = default;
    OutputVStreamImpl &operator=(const OutputVStreamImpl &) = delete;
    virtual ~OutputVStreamImpl();

    virtual hailo_status read(MemoryView buffer);

    void set_on_vstream_cant_read_callback(std::function<void()> callback)
    {
        m_cant_read_callback = callback;
    }

    void set_on_vstream_can_read_callback(std::function<void()> callback)
    {
        m_can_read_callback = callback;
    }

private:
    OutputVStreamImpl(const hailo_vstream_info_t &vstream_info, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr network_group_activated_event, hailo_status &output_status);

    std::function<void()> m_cant_read_callback;
    std::function<void()> m_can_read_callback;
};

#ifdef HAILO_SUPPORT_MULTI_PROCESS
class InputVStreamClient : public InputVStreamInternal
{
public:
    static Expected<std::shared_ptr<InputVStreamClient>> create(uint32_t input_vstream_handle);
    InputVStreamClient(InputVStreamClient &&) noexcept = default;
    InputVStreamClient(const InputVStreamClient &) = delete;
    InputVStreamClient &operator=(InputVStreamClient &&) noexcept = default;
    InputVStreamClient &operator=(const InputVStreamClient &) = delete;
    virtual ~InputVStreamClient();

    virtual hailo_status write(const MemoryView &buffer) override;
    virtual hailo_status flush() override;

    virtual hailo_status abort() override;
    virtual hailo_status resume() override;
    virtual size_t get_frame_size() const override;
    virtual const hailo_vstream_info_t &get_info() const override;
    virtual const hailo_format_t &get_user_buffer_format() const override;
    virtual std::string name() const override;
    virtual std::string network_name() const override;
    virtual const std::map<std::string, AccumulatorPtr> &get_fps_accumulators() const override;
    virtual const std::map<std::string, AccumulatorPtr> &get_latency_accumulators() const override;
    virtual const std::map<std::string, std::vector<AccumulatorPtr>> &get_queue_size_accumulators() const override;
    virtual AccumulatorPtr get_pipeline_latency_accumulator() const override;
    virtual const std::vector<std::shared_ptr<PipelineElement>> &get_pipeline() const override;

protected:
    virtual hailo_status start_vstream() override;
    virtual hailo_status stop_vstream() override;
    virtual hailo_status stop_and_clear() override;

private:
    InputVStreamClient(std::unique_ptr<HailoRtRpcClient> client, uint32_t input_vstream_handle);

    std::unique_ptr<HailoRtRpcClient> m_client;
    uint32_t m_handle;
};

class OutputVStreamClient : public OutputVStreamInternal
{
public:
    static Expected<std::shared_ptr<OutputVStreamClient>> create(uint32_t outputs_vstream_handle);
    OutputVStreamClient(OutputVStreamClient &&) noexcept = default;
    OutputVStreamClient(const OutputVStreamClient &) = delete;
    OutputVStreamClient &operator=(OutputVStreamClient &&) noexcept = default;
    OutputVStreamClient &operator=(const OutputVStreamClient &) = delete;
    virtual ~OutputVStreamClient();

    virtual hailo_status read(MemoryView buffer);

    virtual hailo_status abort() override;
    virtual hailo_status resume() override;
    virtual size_t get_frame_size() const override;
    virtual const hailo_vstream_info_t &get_info() const override;
    virtual const hailo_format_t &get_user_buffer_format() const override;
    virtual std::string name() const override;
    virtual std::string network_name() const override;
    virtual const std::map<std::string, AccumulatorPtr> &get_fps_accumulators() const override;
    virtual const std::map<std::string, AccumulatorPtr> &get_latency_accumulators() const override;
    virtual const std::map<std::string, std::vector<AccumulatorPtr>> &get_queue_size_accumulators() const override;
    virtual AccumulatorPtr get_pipeline_latency_accumulator() const override;
    virtual const std::vector<std::shared_ptr<PipelineElement>> &get_pipeline() const override;

protected:
    virtual hailo_status start_vstream() override;
    virtual hailo_status stop_vstream() override;
    virtual hailo_status stop_and_clear() override;

private:
    OutputVStreamClient(std::unique_ptr<HailoRtRpcClient> client, uint32_t outputs_vstream_handle);

    std::unique_ptr<HailoRtRpcClient> m_client;
    uint32_t m_handle;
};
#endif // HAILO_SUPPORT_MULTI_PROCESS

class PreInferElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<PreInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info,
        const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    static Expected<std::shared_ptr<PreInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, const std::string &name,
        const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    PreInferElement(std::unique_ptr<InputTransformContext> &&transform_context, BufferPoolPtr buffer_pool,
        const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    virtual ~PreInferElement() = default;

    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::unique_ptr<InputTransformContext> m_transform_context;
    BufferPoolPtr m_pool;
    std::chrono::milliseconds m_timeout;
};

class PostInferElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<PostInferElement>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
        const hailo_quant_info_t &dst_quant_info, const hailo_nms_info_t &nms_info, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    static Expected<std::shared_ptr<PostInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, const hailo_nms_info_t &nms_info,
        const std::string &name, const hailo_vstream_params_t &vstream_params, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    PostInferElement(std::unique_ptr<OutputTransformContext> &&transform_context, const std::string &name,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    virtual ~PostInferElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::unique_ptr<OutputTransformContext> m_transform_context;
};

class NmsMuxElement : public BaseMuxElement
{
public:
    static Expected<std::shared_ptr<NmsMuxElement>> create(const std::vector<hailo_nms_info_t> &nms_infos,
        const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    static Expected<std::shared_ptr<NmsMuxElement>> create(const std::vector<hailo_nms_info_t> &nms_infos, const std::string &name,
        const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    NmsMuxElement(const std::vector<hailo_nms_info_t> &nms_infos, const hailo_nms_info_t &fused_nms_info, BufferPoolPtr &&pool, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    const hailo_nms_info_t &get_fused_nms_info() const;

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

protected:
    virtual Expected<PipelineBuffer> action(std::vector<PipelineBuffer>  &&inputs, PipelineBuffer &&optional) override;

private:
    std::vector<hailo_nms_info_t> m_nms_infos;
    hailo_nms_info_t m_fused_nms_info;
    BufferPoolPtr m_pool;
};

class TransformDemuxElement : public BaseDemuxElement
{
public:
    static Expected<std::shared_ptr<TransformDemuxElement>> create(std::shared_ptr<OutputDemuxer> demuxer,
        const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    TransformDemuxElement(std::shared_ptr<OutputDemuxer> demuxer, std::vector<BufferPoolPtr> &&pools, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

protected:
    virtual Expected<std::vector<PipelineBuffer>> action(PipelineBuffer &&input) override;

private:
    std::shared_ptr<OutputDemuxer> m_demuxer;
    std::vector<BufferPoolPtr> m_pools;
};

class HwReadElement : public SourceElement
{
public:
    static Expected<std::shared_ptr<HwReadElement>> create(OutputStream &stream, const std::string &name, std::chrono::milliseconds timeout,
        size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    HwReadElement(OutputStream &stream, BufferPoolPtr buffer_pool, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status);
    virtual ~HwReadElement() = default;

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

    virtual hailo_status run_push(PipelineBuffer &&buffer) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status activate() override;
    virtual hailo_status deactivate() override;
    virtual hailo_status post_deactivate() override;
    virtual hailo_status clear() override;
    virtual hailo_status flush() override;
    virtual hailo_status abort() override;
    virtual void wait_for_finish() override;
    virtual hailo_status resume() override;
    uint32_t get_invalid_frames_count();
    virtual std::string description() const override;

private:
    OutputStream &m_stream;
    BufferPoolPtr m_pool;
    std::chrono::milliseconds m_timeout;
    EventPtr m_shutdown_event;
    WaitOrShutdown m_activation_wait_or_shutdown;
};

class HwWriteElement : public SinkElement
{
public:
    static Expected<std::shared_ptr<HwWriteElement>> create(InputStream &stream, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    HwWriteElement(InputStream &stream, const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr got_flush_event);
    virtual ~HwWriteElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status activate() override;
    virtual hailo_status deactivate() override;
    virtual hailo_status post_deactivate() override;
    virtual hailo_status clear() override;
    virtual hailo_status flush() override;
    virtual hailo_status abort() override;
    virtual void wait_for_finish() override;
    virtual hailo_status resume() override;
    virtual std::string description() const override;

private:
    InputStream &m_stream;
    EventPtr m_got_flush_event;
};

class CopyBufferElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<CopyBufferElement>> create(const std::string &name, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    CopyBufferElement(const std::string &name, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);
    virtual ~CopyBufferElement() = default;
    virtual PipelinePad &next_pad() override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;
};

class VStreamsBuilderUtils
{
public:
    static Expected<std::vector<InputVStream>> create_inputs(InputStream &input_stream, const hailo_vstream_info_t &input_vstream_infos,
        const hailo_vstream_params_t &vstreams_params);
    static Expected<std::vector<OutputVStream>> create_outputs(OutputStream &output_stream,
        NameToVStreamParamsMap &vstreams_params_map, const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);
    static InputVStream create_input(std::shared_ptr<InputVStreamInternal> input_vstream);
    static OutputVStream create_output(std::shared_ptr<OutputVStreamInternal> output_vstream);
    static Expected<std::vector<OutputVStream>> create_output_nms(OutputStreamRefVector &output_streams,
        hailo_vstream_params_t vstreams_params,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);
    static hailo_status add_demux(OutputStream &output_stream, NameToVStreamParamsMap &vstreams_params_map,
        std::vector<std::shared_ptr<PipelineElement>> &&elements, std::vector<OutputVStream> &vstreams,
        std::shared_ptr<HwReadElement> hw_read_elem, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);
    static hailo_status add_nms_fuse(OutputStreamRefVector &output_streams, hailo_vstream_params_t &vstreams_params,
        std::vector<std::shared_ptr<PipelineElement>> &elements, std::vector<OutputVStream> &vstreams,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);
    static Expected<AccumulatorPtr> create_pipeline_latency_accumulator(const hailo_vstream_params_t &vstreams_params);
};

} /* namespace hailort */

#endif /* _HAILO_VSTREAM_INTERNAL_HPP_ */
