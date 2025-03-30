/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filter_elements.hpp
 * @brief all filter elements in the pipeline (single input, single output).
 **/

#ifndef _HAILO_FILTER_ELEMENTS_HPP_
#define _HAILO_FILTER_ELEMENTS_HPP_

#include "net_flow/pipeline/pipeline_internal.hpp"

namespace hailort
{

class FilterElement : public IntermediateElement
{
public:
    FilterElement(const std::string &name, DurationCollector &&duration_collector,
                  std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                  PipelineDirection pipeline_direction, std::chrono::milliseconds timeout,
                  std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~FilterElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

protected:
    // The optional buffer functions as an output buffer that the user can write to instead of acquiring a new buffer
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) = 0;

    PipelinePad &next_pad_downstream();
    PipelinePad &next_pad_upstream();

    std::chrono::milliseconds m_timeout;
};

class PreInferElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<PreInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
        const std::string &name, std::chrono::milliseconds timeout, hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction = PipelineDirection::PUSH,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<PreInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
        const std::string &name, const hailo_vstream_params_t &vstream_params, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH, std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<PreInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PUSH,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    PreInferElement(std::unique_ptr<InputTransformContext> &&transform_context, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
        std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~PreInferElement() = default;

    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::unique_ptr<InputTransformContext> m_transform_context;
};

class RemoveOverlappingBboxesElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> create(
        const net_flow::NmsPostProcessConfig nms_config, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> create(const net_flow::NmsPostProcessConfig nms_config,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL, 
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    RemoveOverlappingBboxesElement(const net_flow::NmsPostProcessConfig &&nms_config, const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, std::chrono::milliseconds timeout,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~RemoveOverlappingBboxesElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

    virtual hailo_status set_nms_iou_threshold(float32_t threshold)
    {
        m_nms_config.nms_iou_th = threshold;
        return HAILO_SUCCESS;
    }

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    net_flow::NmsPostProcessConfig m_nms_config;
};

class PostInferElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<PostInferElement>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
        const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<PostInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
        const std::vector<hailo_quant_info_t> &dst_quant_info, const hailo_nms_info_t &nms_info,
        const std::string &name, const hailo_vstream_params_t &vstream_params, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<PostInferElement>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
        const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info, const std::string &name,
        const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    PostInferElement(std::unique_ptr<OutputTransformContext> &&transform_context, const std::string &name,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~PostInferElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::unique_ptr<OutputTransformContext> m_transform_context;
};

class ConvertNmsToDetectionsElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> create(const hailo_nms_info_t &nms_info, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> create(
        const hailo_nms_info_t &nms_info, const std::string &name, const ElementBuildParams &build_params,
        PipelineDirection pipeline_direction = PipelineDirection::PULL, std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    ConvertNmsToDetectionsElement(const hailo_nms_info_t &&nms_info, const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, std::chrono::milliseconds timeout,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~ConvertNmsToDetectionsElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual PipelinePad &next_pad() override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    hailo_nms_info_t m_nms_info;
};

class FillNmsFormatElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<FillNmsFormatElement>> create(const net_flow::NmsPostProcessConfig nms_config, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, const hailo_format_order_t format_order, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<FillNmsFormatElement>> create(const net_flow::NmsPostProcessConfig nms_config, const std::string &name,
        const ElementBuildParams &build_params, const hailo_format_order_t format_order, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    FillNmsFormatElement(const net_flow::NmsPostProcessConfig &&nms_config, const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, std::chrono::milliseconds timeout,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline, const hailo_format_order_t format_order);
    virtual ~FillNmsFormatElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual PipelinePad &next_pad() override;

    virtual hailo_status set_nms_max_proposals_per_class(uint32_t max_proposals_per_class) override
    {
        m_nms_config.max_proposals_per_class = max_proposals_per_class;
        return HAILO_SUCCESS;
    }

    virtual hailo_status set_nms_max_proposals_total(uint32_t max_proposals_total) override
    {
        m_nms_config.max_proposals_total = max_proposals_total;
        return HAILO_SUCCESS;
    }

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    net_flow::NmsPostProcessConfig m_nms_config;
    hailo_format_order_t m_format_order;
};

class ArgmaxPostProcessElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<ArgmaxPostProcessElement>> create(std::shared_ptr<net_flow::Op> argmax_op,
        const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout,
        PipelineDirection pipeline_direction = PipelineDirection::PULL, std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<ArgmaxPostProcessElement>> create(std::shared_ptr<net_flow::Op> argmax_op,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    ArgmaxPostProcessElement(std::shared_ptr<net_flow::Op> argmax_op, const std::string &name,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction,
        std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~ArgmaxPostProcessElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;
    
protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::shared_ptr<net_flow::Op> m_argmax_op;
};

class SoftmaxPostProcessElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<SoftmaxPostProcessElement>> create(std::shared_ptr<net_flow::Op> softmax_op,
        const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout,
        PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<SoftmaxPostProcessElement>> create(std::shared_ptr<net_flow::Op> softmax_op,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    SoftmaxPostProcessElement(std::shared_ptr<net_flow::Op> softmax_op, const std::string &name,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction,
        std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~SoftmaxPostProcessElement() = default;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::shared_ptr<net_flow::Op> m_softmax_op;
};

class CopyBufferElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<CopyBufferElement>> create(const std::string &name, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction = PipelineDirection::PULL, std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    CopyBufferElement(const std::string &name, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual ~CopyBufferElement() = default;
    virtual PipelinePad &next_pad() override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;
};



} /* namespace hailort */

#endif /* _HAILO_FILTER_ELEMENTS_HPP_ */
