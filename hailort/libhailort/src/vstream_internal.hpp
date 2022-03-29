/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream.hpp
 * @brief Virtual Stream
 **/

#ifndef _HAILO_VSTREAM_INTERNAL_HPP_
#define _HAILO_VSTREAM_INTERNAL_HPP_

#include "pipeline.hpp"
#include "hailo/transform.hpp"
#include "hailo/stream.hpp"
#include "hailo/network_group.hpp"

namespace hailort
{

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
