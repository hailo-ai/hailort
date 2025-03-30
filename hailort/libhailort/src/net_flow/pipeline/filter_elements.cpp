/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file filter_elements.cpp
 * @brief Implementation of the filter elements
 **/

#include "net_flow/pipeline/vstream_internal.hpp"
#include "net_flow/pipeline/filter_elements.hpp"

namespace hailort
{

FilterElement::FilterElement(const std::string &name, DurationCollector &&duration_collector,
                             std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                             PipelineDirection pipeline_direction,
                             std::chrono::milliseconds timeout, std::shared_ptr<AsyncPipeline> async_pipeline) :
    IntermediateElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
    m_timeout(timeout)
{}

hailo_status FilterElement::run_push(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto output,
        action(std::move(buffer), PipelineBuffer()));

    hailo_status status = next_pad().run_push(std::move(output));
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("run_push of {} was shutdown!", name());
        return status;
    }
    if (HAILO_STREAM_ABORT == status) {
        LOGGER__INFO("run_push of {} was aborted!", name());
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

void FilterElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    assert(m_pipeline_direction == PipelineDirection::PUSH);
    if (HAILO_SUCCESS != buffer.action_status()) {
        auto pool = next_pad().get_buffer_pool();
        assert(pool);

        auto buffer_from_pool = pool->get_available_buffer(PipelineBuffer(), m_timeout);
        if (HAILO_SUCCESS != buffer_from_pool.status()) {
            handle_non_recoverable_async_error(buffer_from_pool.status());
        } else {
            buffer_from_pool->set_action_status(buffer.action_status());
            next_pad().run_push_async(buffer_from_pool.release());
        }
        return;
    }

    auto output = action(std::move(buffer), PipelineBuffer());
    if (HAILO_SUCCESS == output.status()) {
        next_pad().run_push_async(output.release());
    } else {
        next_pad().run_push_async(PipelineBuffer(output.status()));
    }
    return;
}

Expected<PipelineBuffer> FilterElement::run_pull(PipelineBuffer &&optional, const PipelinePad &/*source*/)
{
    TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto buffer,
        next_pad().run_pull());
    return action(std::move(buffer), std::move(optional));
}

PipelinePad &FilterElement::next_pad_downstream()
{
    return *m_sources[0].next();
}

PipelinePad &FilterElement::next_pad_upstream()
{
    return *m_sinks[0].prev();
}

Expected<std::shared_ptr<PreInferElement>> PreInferElement::create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
    const std::string &name, std::chrono::milliseconds timeout, hailo_pipeline_elem_stats_flags_t elem_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(auto transform_context,
        InputTransformContext::create(src_image_shape, src_format, dst_image_shape, dst_format,
            dst_quant_infos), "Failed Creating InputTransformContext");
    TRY(auto duration_collector, DurationCollector::create(elem_flags));

    auto pre_infer_elem_ptr = make_shared_nothrow<PreInferElement>(std::move(transform_context),
        name, timeout, std::move(duration_collector), std::move(pipeline_status), pipeline_direction,
        async_pipeline);
    CHECK_AS_EXPECTED(nullptr != pre_infer_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", pre_infer_elem_ptr->description());

    return pre_infer_elem_ptr;
}

Expected<std::shared_ptr<PreInferElement>> PreInferElement::create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
    const std::string &name, const hailo_vstream_params_t &vstream_params, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PreInferElement::create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_infos, name,
        std::chrono::milliseconds(vstream_params.timeout_ms), vstream_params.pipeline_elements_stats_flags,
        pipeline_status, pipeline_direction, async_pipeline);
}

Expected<std::shared_ptr<PreInferElement>> PreInferElement::create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PreInferElement::create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_infos, name,
        build_params.timeout, build_params.elem_stats_flags, build_params.pipeline_status, pipeline_direction, async_pipeline);
}

PreInferElement::PreInferElement(std::unique_ptr<InputTransformContext> &&transform_context, const std::string &name, std::chrono::milliseconds timeout,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, timeout, async_pipeline),
    m_transform_context(std::move(transform_context))
{}

Expected<PipelineBuffer> PreInferElement::run_pull(PipelineBuffer &&/*optional*/, const PipelinePad &/*source*/)
{
    LOGGER__ERROR("PreInferElement does not support run_pull operation");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

PipelinePad &PreInferElement::next_pad()
{
    // Note: The next elem to be run is downstream from this elem (i.e. buffers are pushed)
    return *m_sources[0].next();
}

std::string PreInferElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << " | " << m_transform_context->description() << ")";
    return element_description.str();
}

Expected<PipelineBuffer> PreInferElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    if (PipelineBuffer::Type::FLUSH == input.get_type()) {
        return std::move(input);
    }

    // Buffers are always taken from the next-pad-downstream
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    auto transformed_buffer = pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == transformed_buffer.status()) {
        return make_unexpected(transformed_buffer.status());
    }

    if (!transformed_buffer) {
        input.set_action_status(transformed_buffer.status());
    }
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != transformed_buffer.status(), HAILO_TIMEOUT,
        "{} (H2D) failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_timeout.count());
    CHECK_EXPECTED(transformed_buffer); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here

    TRY(auto dst, transformed_buffer->as_view(BufferProtection::WRITE));
    TRY(auto src, input.as_view(BufferProtection::READ));

    m_duration_collector.start_measurement();
    const auto status = m_transform_context->transform(src, dst);
    m_duration_collector.complete_measurement();

    input.set_action_status(status);
    transformed_buffer->set_action_status(status);

    auto metadata = input.get_metadata();

    CHECK_SUCCESS_AS_EXPECTED(status);

    // Note: The latency to be measured starts as the input buffer is sent to the InputVStream (via write())
    transformed_buffer->set_metadata_start_time(metadata.get_start_time());

    return transformed_buffer.release();
}

Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> ConvertNmsToDetectionsElement::create(
    const hailo_nms_info_t &nms_info, const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(auto duration_collector, DurationCollector::create(elem_flags));

    auto convert_nms_to_detections_elem_ptr = make_shared_nothrow<ConvertNmsToDetectionsElement>(std::move(nms_info),
        name, std::move(duration_collector), std::move(pipeline_status), timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != convert_nms_to_detections_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", convert_nms_to_detections_elem_ptr->description());

    return convert_nms_to_detections_elem_ptr;
}

Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> ConvertNmsToDetectionsElement::create(
    const hailo_nms_info_t &nms_info, const std::string &name, const ElementBuildParams &build_params,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return ConvertNmsToDetectionsElement::create(nms_info, name, build_params.elem_stats_flags, build_params.pipeline_status,
        build_params.timeout, pipeline_direction, async_pipeline);
}

ConvertNmsToDetectionsElement::ConvertNmsToDetectionsElement(const hailo_nms_info_t &&nms_info, const std::string &name,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, timeout, async_pipeline),
    m_nms_info(std::move(nms_info))
{}

hailo_status ConvertNmsToDetectionsElement::run_push(PipelineBuffer &&buffer, const PipelinePad &sink)
{
    CHECK(PipelineDirection::PUSH == m_pipeline_direction, HAILO_INVALID_OPERATION,
        "ConvertNmsToDetectionsElement {} does not support run_push operation", name());
    return FilterElement::run_push(std::move(buffer), sink);
}

PipelinePad &ConvertNmsToDetectionsElement::next_pad()
{
    if (PipelineDirection::PUSH == m_pipeline_direction){
        return *m_sources[0].next();
    }
    return *m_sinks[0].prev();
}

Expected<PipelineBuffer> ConvertNmsToDetectionsElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    // Buffers are always taken from the next-pad-downstream
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    auto buffer = pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.set_action_status(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status()); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here

    buffer->set_metadata_start_time(input.get_metadata().get_start_time());
    buffer->set_additional_data(input.get_metadata().get_additional_data<IouPipelineData>());

    m_duration_collector.start_measurement();

    auto detections_pair = net_flow::NmsPostProcessOp::transform__d2h_NMS_DETECTIONS(input.data(), m_nms_info);
    auto detections_pipeline_data = make_shared_nothrow<IouPipelineData>
        (std::move(detections_pair.first),std::move(detections_pair.second));
    buffer->set_additional_data(detections_pipeline_data);

    m_duration_collector.complete_measurement();

    return buffer.release();
}

Expected<std::shared_ptr<FillNmsFormatElement>> FillNmsFormatElement::create(const net_flow::NmsPostProcessConfig nms_config,
    const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    std::chrono::milliseconds timeout, const hailo_format_order_t format_order, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(auto duration_collector, DurationCollector::create(elem_flags));

    auto fill_nms_format_element = make_shared_nothrow<FillNmsFormatElement>(std::move(nms_config),
        name, std::move(duration_collector), std::move(pipeline_status), timeout, pipeline_direction, async_pipeline, format_order);
    CHECK_AS_EXPECTED(nullptr != fill_nms_format_element, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", fill_nms_format_element->description());

    return fill_nms_format_element;
}

Expected<std::shared_ptr<FillNmsFormatElement>> FillNmsFormatElement::create(const net_flow::NmsPostProcessConfig nms_config,
    const std::string &name, const ElementBuildParams &build_params, const hailo_format_order_t format_order,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return FillNmsFormatElement::create(nms_config, name, build_params.elem_stats_flags,
        build_params.pipeline_status, build_params.timeout, format_order, pipeline_direction, async_pipeline);
}

FillNmsFormatElement::FillNmsFormatElement(const net_flow::NmsPostProcessConfig &&nms_config, const std::string &name,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline,
    const hailo_format_order_t format_order) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, timeout, async_pipeline),
    m_nms_config(std::move(nms_config)),
    m_format_order(format_order)
{}

hailo_status FillNmsFormatElement::run_push(PipelineBuffer &&buffer, const PipelinePad &sink)
{
    CHECK(PipelineDirection::PUSH == m_pipeline_direction, HAILO_INVALID_OPERATION,
        "FillNmsFormatElement {} does not support run_push operation", name());
    return FilterElement::run_push(std::move(buffer), sink);
}

PipelinePad &FillNmsFormatElement::next_pad()
{
    if (PipelineDirection::PUSH == m_pipeline_direction){
        return *m_sources[0].next();
    }
    return *m_sinks[0].prev();
}

Expected<PipelineBuffer> FillNmsFormatElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    // Buffers are always taken from the next-pad-downstream
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    auto buffer_expected = pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer_expected.status()) {
        return make_unexpected(buffer_expected.status());
    }
    if (!buffer_expected) {
        input.set_action_status(buffer_expected.status());
    }
    CHECK_EXPECTED(buffer_expected,
        "{} (D2H) failed with status={}", name(),buffer_expected.status()); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here
    auto buffer = buffer_expected.release();

    buffer.set_metadata_start_time(input.get_metadata().get_start_time());
    buffer.set_additional_data(input.get_metadata().get_additional_data<IouPipelineData>());

    m_duration_collector.start_measurement();

    auto detections = input.get_metadata().get_additional_data<IouPipelineData>();
    TRY(auto dst, buffer.as_view(BufferProtection::WRITE));

    if (HailoRTCommon::is_nms_by_class(m_format_order)) {
        net_flow::NmsPostProcessOp::fill_nms_by_class_format_buffer(dst, detections->m_detections, detections->m_detections_classes_count,
            m_nms_config);
    } else if (HailoRTCommon::is_nms_by_score(m_format_order)) {
        net_flow::NmsPostProcessOp::fill_nms_by_score_format_buffer(dst, detections->m_detections, m_nms_config, true);
    } else {
        LOGGER__ERROR("Unsupported output format order for NmsPostProcessOp: {}",
                HailoRTCommon::get_format_order_str(m_format_order));
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    m_duration_collector.complete_measurement();

    return buffer;
}

Expected<std::shared_ptr<PostInferElement>> PostInferElement::create(const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
    const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info, const std::string &name,
    hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(auto transform_context, OutputTransformContext::create(src_image_shape, src_format, dst_image_shape, dst_format,
        dst_quant_infos, nms_info), "Failed creating OutputTransformContext");
    TRY(auto duration_collector, DurationCollector::create(elem_flags));

    auto post_infer_elem_ptr = make_shared_nothrow<PostInferElement>(std::move(transform_context), name,
        std::move(duration_collector), std::move(pipeline_status), timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != post_infer_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", post_infer_elem_ptr->description());

    return post_infer_elem_ptr;
}

Expected<std::shared_ptr<PostInferElement>> PostInferElement::create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info,
        const std::string &name, const hailo_vstream_params_t &vstream_params, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PostInferElement::create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_infos, nms_info,
        name, vstream_params.pipeline_elements_stats_flags, pipeline_status, std::chrono::milliseconds(vstream_params.timeout_ms),
        pipeline_direction, async_pipeline);
}

Expected<std::shared_ptr<PostInferElement>> PostInferElement::create(const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
    const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info, const std::string &name,
    const ElementBuildParams &build_params, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PostInferElement::create(src_image_shape, src_format, dst_image_shape, dst_format,
        dst_quant_infos, nms_info, name, build_params.elem_stats_flags, build_params.pipeline_status,
        build_params.timeout, pipeline_direction, async_pipeline);
}

PostInferElement::PostInferElement(std::unique_ptr<OutputTransformContext> &&transform_context, const std::string &name,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, timeout, async_pipeline),
    m_transform_context(std::move(transform_context))
{}

Expected<PipelineBuffer> PostInferElement::run_pull(PipelineBuffer &&optional, const PipelinePad &source)
{
    CHECK_AS_EXPECTED(m_pipeline_direction == PipelineDirection::PULL, HAILO_INVALID_OPERATION,
        "PostInferElement {} does not support run_pull operation", name()
    );
    return FilterElement::run_pull(std::move(optional), source);
}

hailo_status PostInferElement::run_push(PipelineBuffer &&buffer, const PipelinePad &sink)
{
    CHECK(PipelineDirection::PUSH == m_pipeline_direction, HAILO_INVALID_OPERATION,
        "PostInferElement {} does not support run_push operation", name());
    return FilterElement::run_push(std::move(buffer), sink);
}

PipelinePad &PostInferElement::next_pad()
{
    if (PipelineDirection::PUSH == m_pipeline_direction){
        return *m_sources[0].next();
    }
    return *m_sinks[0].prev();
}

std::string PostInferElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << " | " << m_transform_context->description() << ")";
    return element_description.str();
}

Expected<PipelineBuffer> PostInferElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    // Buffers are always taken from the next-pad-downstream
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    auto buffer = pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.set_action_status(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status()); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here

    // Note: The latency to be measured starts as the buffer is read from the HW (it's 'input' in this case)
    buffer->set_metadata_start_time(input.get_metadata().get_start_time());

    TRY(auto src, input.as_view(BufferProtection::READ));
    TRY(auto dst, buffer->as_view(BufferProtection::WRITE));

    m_duration_collector.start_measurement();
    const auto status = m_transform_context->transform(src, dst);
    m_duration_collector.complete_measurement();

    input.set_action_status(status);
    buffer->set_action_status(status);

    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer.release();
}

Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> RemoveOverlappingBboxesElement::create(
    const net_flow::NmsPostProcessConfig nms_config, const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(auto duration_collector, DurationCollector::create(elem_flags));

    auto convert_nms_removed_overlapping_elem_ptr = make_shared_nothrow<RemoveOverlappingBboxesElement>(std::move(nms_config),
        name, std::move(duration_collector), std::move(pipeline_status), timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != convert_nms_removed_overlapping_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", convert_nms_removed_overlapping_elem_ptr->description());

    return convert_nms_removed_overlapping_elem_ptr;
}

Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> RemoveOverlappingBboxesElement::create(const net_flow::NmsPostProcessConfig nms_config,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return RemoveOverlappingBboxesElement::create(nms_config, name,
        build_params.elem_stats_flags, build_params.pipeline_status, build_params.timeout, pipeline_direction, async_pipeline);
}

RemoveOverlappingBboxesElement::RemoveOverlappingBboxesElement(const net_flow::NmsPostProcessConfig &&nms_config, const std::string &name,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, timeout, async_pipeline),
    m_nms_config(std::move(nms_config))
{}

hailo_status RemoveOverlappingBboxesElement::run_push(PipelineBuffer &&buffer, const PipelinePad &sink)
{
    CHECK(PipelineDirection::PUSH == m_pipeline_direction, HAILO_INVALID_OPERATION,
        "RemoveOverlappingBboxesElement {} does not support run_push operation", name());
    return FilterElement::run_push(std::move(buffer), sink);
}

PipelinePad &RemoveOverlappingBboxesElement::next_pad()
{
    if (PipelineDirection::PUSH == m_pipeline_direction){
        return *m_sources[0].next();
    }
    return *m_sinks[0].prev();
}

std::string RemoveOverlappingBboxesElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name();
    element_description << " | " << "IoU Threshold: " << this->m_nms_config.nms_iou_th << ")";
    return element_description.str();
}

Expected<PipelineBuffer> RemoveOverlappingBboxesElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    // Buffers are always taken from the next-pad-downstream
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    auto buffer = pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.set_action_status(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status()); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here

    buffer->set_metadata_start_time(input.get_metadata().get_start_time());
    buffer->set_additional_data(input.get_metadata().get_additional_data<IouPipelineData>());

    m_duration_collector.start_measurement();
    auto detections_pipeline_data = input.get_metadata().get_additional_data<IouPipelineData>();

    net_flow::NmsPostProcessOp::remove_overlapping_boxes(detections_pipeline_data->m_detections,
        detections_pipeline_data->m_detections_classes_count, m_nms_config.nms_iou_th);
    m_duration_collector.complete_measurement();

    return buffer.release();
}

Expected<std::shared_ptr<ArgmaxPostProcessElement>> ArgmaxPostProcessElement::create(std::shared_ptr<net_flow::Op> argmax_op,
    const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(auto duration_collector, DurationCollector::create(elem_flags));
    auto argmax_elem_ptr = make_shared_nothrow<ArgmaxPostProcessElement>(argmax_op,
        name, std::move(duration_collector), std::move(pipeline_status), timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != argmax_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);
    LOGGER__INFO("Created {}", argmax_elem_ptr->description());
    return argmax_elem_ptr;
}

Expected<std::shared_ptr<ArgmaxPostProcessElement>> ArgmaxPostProcessElement::create(std::shared_ptr<net_flow::Op> argmax_op,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return ArgmaxPostProcessElement::create(argmax_op, name,
        build_params.elem_stats_flags, build_params.pipeline_status, build_params.timeout,
        pipeline_direction, async_pipeline);
}

ArgmaxPostProcessElement::ArgmaxPostProcessElement(std::shared_ptr<net_flow::Op> argmax_op, const std::string &name,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    std::chrono::milliseconds timeout, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, timeout, async_pipeline),
    m_argmax_op(argmax_op)
{}

Expected<PipelineBuffer> ArgmaxPostProcessElement::run_pull(PipelineBuffer &&optional, const PipelinePad &source)
{
    CHECK_AS_EXPECTED(m_pipeline_direction == PipelineDirection::PULL, HAILO_INVALID_OPERATION,
        "ArgmaxPostProcessElement {} does not support run_pull operation", name());
    return FilterElement::run_pull(std::move(optional), source);
}

hailo_status ArgmaxPostProcessElement::run_push(PipelineBuffer &&buffer, const PipelinePad &sink)
{
    CHECK(PipelineDirection::PUSH == m_pipeline_direction, HAILO_INVALID_OPERATION,
        "ArgmaxPostProcessElement {} does not support run_push operation", name());
    return FilterElement::run_push(std::move(buffer), sink);
}

PipelinePad &ArgmaxPostProcessElement::next_pad()
{
    if (PipelineDirection::PUSH == m_pipeline_direction){
        return *m_sources[0].next();
    }
    return *m_sinks[0].prev();
}

std::string ArgmaxPostProcessElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << " | " << m_argmax_op->metadata()->get_op_description() << ")";
    return element_description.str();
}

Expected<PipelineBuffer> ArgmaxPostProcessElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    // Buffers are always taken from the next-pad-downstream
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    auto buffer = pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.set_action_status(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status()); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here

    std::map<std::string, MemoryView> inputs;
    std::map<std::string, MemoryView> outputs;
    auto &input_name = m_argmax_op->inputs_metadata().begin()->first;
    auto &output_name = m_argmax_op->outputs_metadata().begin()->first;

    TRY(auto src, input.as_view(BufferProtection::READ));
    TRY(auto dst, buffer->as_view(BufferProtection::WRITE));

    inputs.insert({input_name, src});
    outputs.insert({output_name, dst});
    m_duration_collector.start_measurement();
    auto post_process_result = m_argmax_op->execute(inputs, outputs);
    m_duration_collector.complete_measurement();

    input.set_action_status(post_process_result);
    buffer->set_action_status(post_process_result);

    CHECK_SUCCESS_AS_EXPECTED(post_process_result);

    return buffer.release();
}

Expected<std::shared_ptr<SoftmaxPostProcessElement>> SoftmaxPostProcessElement::create(std::shared_ptr<net_flow::Op> softmax_op,
    const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(auto duration_collector, DurationCollector::create(elem_flags));
    auto softmax_elem_ptr = make_shared_nothrow<SoftmaxPostProcessElement>(softmax_op,
        name, std::move(duration_collector), std::move(pipeline_status), timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != softmax_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);
    LOGGER__INFO("Created {}", softmax_elem_ptr->description());
    return softmax_elem_ptr;
}

Expected<std::shared_ptr<SoftmaxPostProcessElement>> SoftmaxPostProcessElement::create(std::shared_ptr<net_flow::Op> softmax_op,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return SoftmaxPostProcessElement::create(softmax_op, name, build_params.elem_stats_flags, build_params.pipeline_status,
        build_params.timeout, pipeline_direction, async_pipeline);
}

SoftmaxPostProcessElement::SoftmaxPostProcessElement(std::shared_ptr<net_flow::Op> softmax_op, const std::string &name,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, timeout, async_pipeline),
    m_softmax_op(softmax_op)
{}

Expected<PipelineBuffer> SoftmaxPostProcessElement::run_pull(PipelineBuffer &&optional, const PipelinePad &source)
{
    CHECK_AS_EXPECTED(m_pipeline_direction == PipelineDirection::PULL, HAILO_INVALID_OPERATION,
        "SoftmaxPostProcessElement {} does not support run_pull operation", name());
    return FilterElement::run_pull(std::move(optional), source);
}

hailo_status SoftmaxPostProcessElement::run_push(PipelineBuffer &&buffer, const PipelinePad &sink)
{
    CHECK(PipelineDirection::PUSH == m_pipeline_direction, HAILO_INVALID_OPERATION,
        "SoftmaxPostProcessElement {} does not support run_push operation", name());
    return FilterElement::run_push(std::move(buffer), sink);
}

PipelinePad &SoftmaxPostProcessElement::next_pad()
{
    if (PipelineDirection::PUSH == m_pipeline_direction){
        return *m_sources[0].next();
    }
    return *m_sinks[0].prev();
}

std::string SoftmaxPostProcessElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << " | " << m_softmax_op->metadata()->get_op_description() << ")";
    return element_description.str();
}

Expected<PipelineBuffer> SoftmaxPostProcessElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    // Buffers are always taken from the next-pad-downstream
    auto pool = next_pad_downstream().get_buffer_pool();
    assert(pool);

    auto buffer = pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.set_action_status(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status()); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here

    std::map<std::string, MemoryView> inputs;
    std::map<std::string, MemoryView> outputs;
    auto &input_name = m_softmax_op->inputs_metadata().begin()->first;
    auto &output_name = m_softmax_op->outputs_metadata().begin()->first;

    TRY(auto src, input.as_view(BufferProtection::READ));
    TRY(auto dst, buffer->as_view(BufferProtection::WRITE));

    inputs.insert({input_name, src});
    outputs.insert({output_name, dst});
    m_duration_collector.start_measurement();
    auto post_process_result = m_softmax_op->execute(inputs, outputs);
    m_duration_collector.complete_measurement();

    input.set_action_status(post_process_result);
    buffer->set_action_status(post_process_result);

    CHECK_SUCCESS_AS_EXPECTED(post_process_result);

    return buffer.release();
}

Expected<std::shared_ptr<CopyBufferElement>> CopyBufferElement::create(const std::string &name,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(auto duration_collector, DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE));
    auto elem_ptr = make_shared_nothrow<CopyBufferElement>(name, std::move(duration_collector), std::move(pipeline_status),
        timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", elem_ptr->description());

    return elem_ptr;
}

CopyBufferElement::CopyBufferElement(const std::string &name, DurationCollector &&duration_collector, 
                                     std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout,
                                     PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, timeout, async_pipeline)
{}

PipelinePad &CopyBufferElement::next_pad()
{
    if (PipelineDirection::PUSH == m_pipeline_direction){
        return *m_sources[0].next();
    }
    return *m_sinks[0].prev();
}

Expected<PipelineBuffer> CopyBufferElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    CHECK_AS_EXPECTED(optional, HAILO_INVALID_ARGUMENT, "Optional buffer must be passed to CopyBufferElement!");

    CHECK_AS_EXPECTED(optional.size() == input.size(), HAILO_INVALID_ARGUMENT, "Optional buffer size does not equal to the input buffer size!");
    memcpy(optional.data(), input.data(), optional.size());

    return std::move(optional);
}

} /* namespace hailort */
