/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream.cpp
 * @brief Implementation of the virtual stream
 **/

#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include "hailo/stream.hpp"
#include "hailo/vstream.hpp"
#include "hailo/hef.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/hailort_defaults.hpp"
#include "hailo/hailort_common.hpp"
#include "net_flow/pipeline/pipeline_internal.hpp"
#include "stream_common/stream_internal.hpp"
#include "net_flow/ops/nms_post_process.hpp"
#include "net_flow/ops/ssd_post_process.hpp"
#include "net_flow/ops/yolox_post_process.hpp"
#include "net_flow/ops/yolov8_post_process.hpp"
#include "net_flow/ops/yolov5_post_process.hpp"
#include "net_flow/ops/argmax_post_process.hpp"
#include "net_flow/ops/softmax_post_process.hpp"
#include "net_flow/ops/yolov5_seg_post_process.hpp"

#include "common/runtime_statistics_internal.hpp"

#include "net_flow/pipeline/vstream_internal.hpp"
#include <cstdint>
#include <math.h>
#include <memory>

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "rpc/rpc_definitions.hpp"
#include "service/rpc_client_utils.hpp"
#endif // HAILO_SUPPORT_MULTI_PROCESS

#include <unordered_set>


namespace hailort
{

static std::map<std::string, AccumulatorPtr> get_pipeline_accumulators_by_type(
    const std::vector<std::shared_ptr<PipelineElement>> &pipeline, AccumulatorType accumulator_type);

static std::map<std::string, std::vector<AccumulatorPtr>> get_pipeline_queue_size_accumulators(
    const std::vector<std::shared_ptr<PipelineElement>> &pipeline);

Expected<std::shared_ptr<PreInferElement>> PreInferElement::create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
    const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
    hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    PipelineDirection pipeline_direction, bool is_dma_able, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto transform_context = InputTransformContext::create(src_image_shape, src_format, dst_image_shape, dst_format,
        dst_quant_infos);
    CHECK_EXPECTED(transform_context, "Failed Creating InputTransformContext");

    bool is_empty = false;
    auto buffer_pool = BufferPool::create(transform_context.value()->get_dst_frame_size(), buffer_pool_size, shutdown_event, elem_flags,
        vstream_flags, is_empty, is_dma_able);
    CHECK_EXPECTED(buffer_pool, "Failed creating BufferPool for {}", name);

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto pre_infer_elem_ptr = make_shared_nothrow<PreInferElement>(transform_context.release(),
        buffer_pool.release(), name, timeout, duration_collector.release(), std::move(pipeline_status), pipeline_direction,
        async_pipeline);
    CHECK_AS_EXPECTED(nullptr != pre_infer_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", pre_infer_elem_ptr->name());

    return pre_infer_elem_ptr;
}

Expected<std::shared_ptr<PreInferElement>> PreInferElement::create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, const std::string &name,
        const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction, bool is_dma_able, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PreInferElement::create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_infos, name,
        std::chrono::milliseconds(vstream_params.timeout_ms), vstream_params.queue_size, vstream_params.pipeline_elements_stats_flags,
        vstream_params.vstream_stats_flags, shutdown_event, pipeline_status, pipeline_direction, is_dma_able, async_pipeline);
}

Expected<std::shared_ptr<PreInferElement>> PreInferElement::create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction, bool is_dma_able,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PreInferElement::create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_infos, name,
        build_params.timeout, build_params.buffer_pool_size_internal, build_params.elem_stats_flags, build_params.vstream_stats_flags,
        build_params.shutdown_event, build_params.pipeline_status, pipeline_direction, is_dma_able, async_pipeline);
}

PreInferElement::PreInferElement(std::unique_ptr<InputTransformContext> &&transform_context, BufferPoolPtr buffer_pool,
                                const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
                                std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
                                std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, buffer_pool, timeout, async_pipeline),
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

    auto transformed_buffer = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == transformed_buffer.status()) {
        return make_unexpected(transformed_buffer.status());
    }
    
    if (!transformed_buffer) {
        input.get_exec_done_cb()(transformed_buffer.status());
    }
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != transformed_buffer.status(), HAILO_TIMEOUT,
        "{} (H2D) failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_timeout.count());
    CHECK_EXPECTED(transformed_buffer);

    auto dst = transformed_buffer->as_view();
    m_duration_collector.start_measurement();
    const auto status = m_transform_context->transform(input.as_view(), dst);
    m_duration_collector.complete_measurement();

    auto exec_done_cb = input.get_exec_done_cb();
    exec_done_cb(status);
    transformed_buffer->set_action_status(status);

    auto metadata = input.get_metadata();

    CHECK_SUCCESS_AS_EXPECTED(status);

    // Note: The latency to be measured starts as the input buffer is sent to the InputVStream (via write())
    transformed_buffer->set_metadata(std::move(metadata));

    return transformed_buffer.release();
}

Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> ConvertNmsToDetectionsElement::create(
        const hailo_nms_info_t &nms_info, const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, size_t buffer_pool_size,
        PipelineDirection pipeline_direction, bool is_last_copy_element, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    // The actual data will be in the metadata
    auto frame_size = 0;
    auto buffer_pool_expected = BufferPool::create(frame_size, buffer_pool_size, shutdown_event, elem_flags, vstream_flags, is_last_copy_element);
    CHECK_EXPECTED(buffer_pool_expected, "Failed creating BufferPool for {}", name);
    auto buffer_pool = buffer_pool_expected.release();

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto convert_nms_to_detections_elem_ptr = make_shared_nothrow<ConvertNmsToDetectionsElement>(std::move(nms_info),
        name, duration_collector.release(), std::move(pipeline_status), buffer_pool, timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != convert_nms_to_detections_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", convert_nms_to_detections_elem_ptr->name());

    return convert_nms_to_detections_elem_ptr;
}

Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> ConvertNmsToDetectionsElement::create(
        const hailo_nms_info_t &nms_info, const std::string &name, const ElementBuildParams &build_params,
        PipelineDirection pipeline_direction, bool is_last_copy_element, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return ConvertNmsToDetectionsElement::create(nms_info, name, build_params.elem_stats_flags, build_params.pipeline_status,
        build_params.timeout, build_params.vstream_stats_flags, build_params.shutdown_event, build_params.buffer_pool_size_edges,
        pipeline_direction, is_last_copy_element, async_pipeline);
}

ConvertNmsToDetectionsElement::ConvertNmsToDetectionsElement(const hailo_nms_info_t &&nms_info, const std::string &name,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, BufferPoolPtr buffer_pool,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, buffer_pool, timeout, async_pipeline),
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

std::string ConvertNmsToDetectionsElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << ")";
    return element_description.str();
}

Expected<PipelineBuffer> ConvertNmsToDetectionsElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    auto buffer = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.get_exec_done_cb()(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status());

    buffer->set_metadata(input.get_metadata());

    m_duration_collector.start_measurement();

    auto detections_pair = net_flow::NmsPostProcessOp::transform__d2h_NMS_DETECTIONS(input.data(), m_nms_info);
    auto detections_pipeline_data = make_shared_nothrow<IouPipelineData>
        (std::move(detections_pair.first),std::move(detections_pair.second));
    buffer->set_additional_data(detections_pipeline_data);

    m_duration_collector.complete_measurement();

    auto exec_done_cb = input.get_exec_done_cb();
    exec_done_cb(HAILO_SUCCESS);

    return buffer.release();
}

Expected<std::shared_ptr<FillNmsFormatElement>> FillNmsFormatElement::create(const hailo_nms_info_t nms_info,
        const hailo_format_t &dst_format, const net_flow::NmsPostProcessConfig nms_config, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
        size_t buffer_pool_size, PipelineDirection pipeline_direction, bool is_last_copy_element,
        std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto frame_size = HailoRTCommon::get_nms_host_frame_size(nms_info, dst_format);
    auto buffer_pool_expected = BufferPool::create(frame_size, buffer_pool_size, shutdown_event, elem_flags, vstream_flags, is_last_copy_element);
    CHECK_EXPECTED(buffer_pool_expected, "Failed creating BufferPool for {}", name);
    auto buffer_pool = buffer_pool_expected.release();

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto fill_nms_format_element = make_shared_nothrow<FillNmsFormatElement>(std::move(nms_config),
        name, duration_collector.release(), std::move(pipeline_status), buffer_pool, timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != fill_nms_format_element, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", fill_nms_format_element->name());

    return fill_nms_format_element;
}

Expected<std::shared_ptr<FillNmsFormatElement>> FillNmsFormatElement::create(const hailo_nms_info_t nms_info,
        const hailo_format_t &dst_format, const net_flow::NmsPostProcessConfig nms_config, const std::string &name,
        const ElementBuildParams &build_params, PipelineDirection pipeline_direction, bool is_last_copy_element,
        std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return FillNmsFormatElement::create(nms_info, dst_format, nms_config, name, build_params.elem_stats_flags,
        build_params.pipeline_status, build_params.timeout, build_params.vstream_stats_flags,
        build_params.shutdown_event, build_params.buffer_pool_size_edges, pipeline_direction, is_last_copy_element,
        async_pipeline);
}

FillNmsFormatElement::FillNmsFormatElement(const net_flow::NmsPostProcessConfig &&nms_config, const std::string &name,
                                   DurationCollector &&duration_collector,
                                   std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   BufferPoolPtr buffer_pool, std::chrono::milliseconds timeout, PipelineDirection pipeline_direction,
                                   std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, buffer_pool, timeout, async_pipeline),
    m_nms_config(std::move(nms_config))
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

std::string FillNmsFormatElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << ")";
    return element_description.str();
}

Expected<PipelineBuffer> FillNmsFormatElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    auto buffer_expected = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer_expected.status()) {
        return make_unexpected(buffer_expected.status());
    }

    if (!buffer_expected) {
        input.get_exec_done_cb()(buffer_expected.status());
    }
    CHECK_EXPECTED(buffer_expected, "{} (D2H) failed with status={}", name(), buffer_expected.status());
    auto buffer = buffer_expected.release();

    buffer.set_metadata(input.get_metadata());

    m_duration_collector.start_measurement();

    auto detections = input.get_metadata().get_additional_data<IouPipelineData>();
    auto dst = buffer.as_view();
    net_flow::NmsPostProcessOp::fill_nms_format_buffer(dst, detections->m_detections, detections->m_detections_classes_count,
        m_nms_config);

    m_duration_collector.complete_measurement();

    auto exec_done_cb = input.get_exec_done_cb();
    exec_done_cb(HAILO_SUCCESS);

    return buffer;
}

Expected<std::shared_ptr<PostInferElement>> PostInferElement::create(const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
    const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info, const std::string &name,
    hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    std::chrono::milliseconds timeout, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, size_t buffer_pool_size,
    PipelineDirection pipeline_direction, bool is_last_copy_element, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto frame_size = (dst_format.order == HAILO_FORMAT_ORDER_HAILO_NMS) ? HailoRTCommon::get_nms_host_frame_size(nms_info, dst_format) : HailoRTCommon::get_frame_size(dst_image_shape, dst_format);
    auto buffer_pool_expected = BufferPool::create(frame_size, buffer_pool_size, shutdown_event, elem_flags, vstream_flags, is_last_copy_element);
    CHECK_EXPECTED(buffer_pool_expected, "Failed creating BufferPool for {}", name);

    auto transform_context = OutputTransformContext::create(src_image_shape, src_format, dst_image_shape, dst_format,
        dst_quant_infos, nms_info);
    CHECK_EXPECTED(transform_context, "Failed Creating OutputTransformContext");

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto post_infer_elem_ptr = make_shared_nothrow<PostInferElement>(transform_context.release(), name,
        duration_collector.release(), std::move(pipeline_status), buffer_pool_expected.release(), timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != post_infer_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", post_infer_elem_ptr->name());

    return post_infer_elem_ptr;
}

Expected<std::shared_ptr<PostInferElement>> PostInferElement::create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info,
        const std::string &name, const hailo_vstream_params_t &vstream_params, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        EventPtr shutdown_event, PipelineDirection pipeline_direction, bool is_last_copy_element, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PostInferElement::create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_infos, nms_info,
        name, vstream_params.pipeline_elements_stats_flags, pipeline_status, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.vstream_stats_flags, shutdown_event, vstream_params.queue_size, pipeline_direction, is_last_copy_element, async_pipeline);
}

Expected<std::shared_ptr<PostInferElement>> PostInferElement::create(const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
    const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info, const std::string &name,
    const ElementBuildParams &build_params, PipelineDirection pipeline_direction, bool is_last_copy_element,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return PostInferElement::create(src_image_shape, src_format, dst_image_shape, dst_format,
        dst_quant_infos, nms_info, name, build_params.elem_stats_flags, build_params.pipeline_status,
        build_params.timeout, build_params.vstream_stats_flags, build_params.shutdown_event, build_params.buffer_pool_size_edges,
        pipeline_direction, is_last_copy_element, async_pipeline);
}

PostInferElement::PostInferElement(std::unique_ptr<OutputTransformContext> &&transform_context, const std::string &name,
                                   DurationCollector &&duration_collector,
                                   std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   BufferPoolPtr buffer_pool, std::chrono::milliseconds timeout,
                                   PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, buffer_pool, timeout, async_pipeline),
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
    auto buffer = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.get_exec_done_cb()(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status());

    // Note: The latency to be measured starts as the buffer is read from the HW (it's 'input' in this case)
    buffer->set_metadata(input.get_metadata());

    auto dst = buffer->as_view();
    m_duration_collector.start_measurement();
    const auto status = m_transform_context->transform(input.as_view(), dst);
    m_duration_collector.complete_measurement();

    auto exec_done_cb = input.get_exec_done_cb();
    exec_done_cb(status);
    buffer->set_action_status(status);

    CHECK_SUCCESS_AS_EXPECTED(status);

    return buffer.release();
}

static hailo_nms_info_t fuse_nms_info(const std::vector<hailo_nms_info_t> &nms_infos)
{
    hailo_nms_info_t fused_info = nms_infos[0];
    fused_info.is_defused = false;
    fused_info.number_of_classes = 0;
    for (const auto &nms_info : nms_infos) {
        fused_info.number_of_classes += nms_info.number_of_classes;
        assert(nms_infos[0].max_bboxes_per_class == nms_info.max_bboxes_per_class);
        assert(nms_infos[0].bbox_size == nms_info.bbox_size);
        assert(nms_infos[0].chunks_per_frame == nms_info.chunks_per_frame);
        assert(nms_infos[0].burst_size == nms_info.burst_size);
        assert(nms_infos[0].burst_type == nms_info.burst_type);
    }
    return fused_info;
}

Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> RemoveOverlappingBboxesElement::create(
        const net_flow::NmsPostProcessConfig nms_config, const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout, hailo_vstream_stats_flags_t vstream_flags,
        EventPtr shutdown_event, size_t buffer_pool_size, PipelineDirection pipeline_direction, bool is_last_copy_element,
        std::shared_ptr<AsyncPipeline> async_pipeline)
{
    // The actual data will be in the metadata
    auto frame_size = 0;
    auto buffer_pool_expected = BufferPool::create(frame_size, buffer_pool_size, shutdown_event, elem_flags, vstream_flags, is_last_copy_element);
    CHECK_EXPECTED(buffer_pool_expected, "Failed creating BufferPool for {}", name);
    auto buffer_pool = buffer_pool_expected.release();

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto convert_nms_removed_overlapping_elem_ptr = make_shared_nothrow<RemoveOverlappingBboxesElement>(std::move(nms_config),
        name, duration_collector.release(), std::move(pipeline_status), buffer_pool, timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != convert_nms_removed_overlapping_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", convert_nms_removed_overlapping_elem_ptr->name());

    return convert_nms_removed_overlapping_elem_ptr;
}

Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> RemoveOverlappingBboxesElement::create(const net_flow::NmsPostProcessConfig nms_config,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction, bool is_last_copy_element,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return RemoveOverlappingBboxesElement::create(nms_config, name,
        build_params.elem_stats_flags, build_params.pipeline_status, build_params.timeout, build_params.vstream_stats_flags,
        build_params.shutdown_event, build_params.buffer_pool_size_edges, pipeline_direction, is_last_copy_element, async_pipeline);
}

RemoveOverlappingBboxesElement::RemoveOverlappingBboxesElement(const net_flow::NmsPostProcessConfig &&nms_config, const std::string &name,
                                   DurationCollector &&duration_collector,
                                   std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                   BufferPoolPtr buffer_pool, std::chrono::milliseconds timeout,
                                   PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, buffer_pool, timeout, async_pipeline),
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
    element_description << "(" << this->name() << ")";
    return element_description.str();
}

Expected<PipelineBuffer> RemoveOverlappingBboxesElement::action(PipelineBuffer &&input, PipelineBuffer &&optional)
{
    auto buffer = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.get_exec_done_cb()(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status());

    buffer->set_metadata(input.get_metadata());

    m_duration_collector.start_measurement();
    auto detections_pipeline_data = input.get_metadata().get_additional_data<IouPipelineData>();

    net_flow::NmsPostProcessOp::remove_overlapping_boxes(detections_pipeline_data->m_detections,
        detections_pipeline_data->m_detections_classes_count, m_nms_config.nms_iou_th);
    m_duration_collector.complete_measurement();

    auto exec_done_cb = input.get_exec_done_cb();
    exec_done_cb(HAILO_SUCCESS);

    return buffer.release();
}

Expected<std::shared_ptr<NmsPostProcessMuxElement>> NmsPostProcessMuxElement::create(std::shared_ptr<net_flow::Op> nms_op,
    const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size,
    hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction, bool is_last_copy_element,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    assert(nms_op->outputs_metadata().size() == 1);
    auto vstream_info = nms_op->metadata()->get_output_vstream_info();
    CHECK_EXPECTED(vstream_info);

    auto buffer_size = HailoRTCommon::get_nms_host_frame_size(nms_op->metadata()->get_output_vstream_info()->nms_shape,
        nms_op->outputs_metadata().begin()->second.format);

    auto buffer_pool = BufferPool::create(buffer_size, buffer_pool_size, shutdown_event, elem_flags, vstream_flags, is_last_copy_element);
    CHECK_EXPECTED(buffer_pool, "Failed creating BufferPool");

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto nms_elem_ptr = make_shared_nothrow<NmsPostProcessMuxElement>(nms_op, buffer_pool.release(),
        name, timeout, duration_collector.release(), std::move(pipeline_status), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != nms_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", nms_elem_ptr->name());
    return nms_elem_ptr;
}

Expected<std::shared_ptr<NmsPostProcessMuxElement>> NmsPostProcessMuxElement::create(std::shared_ptr<net_flow::Op> nms_op,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction, bool is_last_copy_element,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return NmsPostProcessMuxElement::create(nms_op, name, build_params.timeout,
        build_params.buffer_pool_size_edges, build_params.elem_stats_flags, build_params.vstream_stats_flags,
        build_params.shutdown_event, build_params.pipeline_status, pipeline_direction, is_last_copy_element, async_pipeline);
}

Expected<std::shared_ptr<NmsPostProcessMuxElement>> NmsPostProcessMuxElement::create(std::shared_ptr<net_flow::Op> nms_op,
       const std::string &name, const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event,
       std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction, bool is_last_copy_element,
       std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return NmsPostProcessMuxElement::create(nms_op, name, std::chrono::milliseconds(vstream_params.timeout_ms),
        vstream_params.queue_size, vstream_params.pipeline_elements_stats_flags, vstream_params.vstream_stats_flags, shutdown_event,
        pipeline_status, pipeline_direction, is_last_copy_element, async_pipeline);
}

NmsPostProcessMuxElement::NmsPostProcessMuxElement(std::shared_ptr<net_flow::Op> nms_op, BufferPoolPtr &&pool,
                                                   const std::string &name, std::chrono::milliseconds timeout,
                                                   DurationCollector &&duration_collector,
                                                   std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                                                   PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    BaseMuxElement(nms_op->inputs_metadata().size(), name, timeout, std::move(duration_collector), std::move(pipeline_status),
        std::move(pool), pipeline_direction, async_pipeline),
    m_nms_op(nms_op)
{}

std::vector<AccumulatorPtr> NmsPostProcessMuxElement::get_queue_size_accumulators()
{
    if (nullptr == m_pool->get_queue_size_accumulator()) {
        return std::vector<AccumulatorPtr>();
    }
    return {m_pool->get_queue_size_accumulator()};
}

Expected<PipelineBuffer> NmsPostProcessMuxElement::action(std::vector<PipelineBuffer> &&input_buffers, PipelineBuffer &&optional)
{
    std::map<std::string, MemoryView> inputs;
    std::map<std::string, MemoryView> outputs;
    for (size_t i = 0; i < input_buffers.size(); ++i) {
        inputs.insert({m_sinks_names[i], input_buffers[i].as_view()});
    }
    auto acquired_buffer = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == acquired_buffer.status()) {
        return make_unexpected(acquired_buffer.status());
    }

    if (!acquired_buffer) {
        for (auto &input : input_buffers) {
            auto exec_done_cb = input.get_exec_done_cb();
            exec_done_cb(acquired_buffer.status());
        }
    }
    CHECK_EXPECTED(acquired_buffer);
    outputs.insert({"", acquired_buffer->as_view()}); // TODO: fill with correct name
    m_duration_collector.start_measurement();

    auto post_process_result = m_nms_op->execute(inputs, outputs);
    m_duration_collector.complete_measurement();

    for (auto &input : input_buffers) {
        auto exec_done_cb = input.get_exec_done_cb();
        exec_done_cb(post_process_result);
    }
    acquired_buffer->set_action_status(post_process_result);

    CHECK_SUCCESS_AS_EXPECTED(post_process_result);
    return acquired_buffer;
}

Expected<std::shared_ptr<NmsMuxElement>> NmsMuxElement::create(const std::vector<hailo_nms_info_t> &nms_infos,
    const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size,
    hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction, bool is_last_copy_element,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    const auto &fused_info = fuse_nms_info(nms_infos);
    auto buffer_pool = BufferPool::create(HailoRTCommon::get_nms_hw_frame_size(fused_info),
        buffer_pool_size, shutdown_event, elem_flags, vstream_flags, is_last_copy_element);
    CHECK_EXPECTED(buffer_pool, "Failed creating BufferPool");

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto nms_elem_ptr = make_shared_nothrow<NmsMuxElement>(nms_infos, fused_info, buffer_pool.release(),
        name, timeout, duration_collector.release(), std::move(pipeline_status), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != nms_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", nms_elem_ptr->name());

    return nms_elem_ptr;
}

Expected<std::shared_ptr<NmsMuxElement>> NmsMuxElement::create(const std::vector<hailo_nms_info_t> &nms_infos, const std::string &name,
        const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction, bool is_last_copy_element, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return NmsMuxElement::create(nms_infos, name, std::chrono::milliseconds(vstream_params.timeout_ms), vstream_params.queue_size,
        vstream_params.pipeline_elements_stats_flags, vstream_params.vstream_stats_flags, shutdown_event, pipeline_status, pipeline_direction,
        is_last_copy_element, async_pipeline);
}

Expected<std::shared_ptr<NmsMuxElement>> NmsMuxElement::create(const std::vector<hailo_nms_info_t> &nms_infos,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction, bool is_last_copy_element,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return NmsMuxElement::create(nms_infos, name, build_params.timeout, build_params.buffer_pool_size_edges, build_params.elem_stats_flags,
        build_params.vstream_stats_flags, build_params.shutdown_event, build_params.pipeline_status, pipeline_direction, is_last_copy_element,
        async_pipeline);
}

NmsMuxElement::NmsMuxElement(const std::vector<hailo_nms_info_t> &nms_infos, const hailo_nms_info_t &fused_nms_info, BufferPoolPtr &&pool,
                             const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
                             std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
                             std::shared_ptr<AsyncPipeline> async_pipeline) :
    BaseMuxElement(nms_infos.size(), name, timeout, std::move(duration_collector), std::move(pipeline_status), std::move(pool), pipeline_direction, async_pipeline),
    m_nms_infos(nms_infos),
    m_fused_nms_info(fused_nms_info)
{}

const hailo_nms_info_t &NmsMuxElement::get_fused_nms_info() const
{
    return m_fused_nms_info;
}

std::vector<AccumulatorPtr> NmsMuxElement::get_queue_size_accumulators()
{
    if (nullptr == m_pool->get_queue_size_accumulator()) {
        return std::vector<AccumulatorPtr>();
    }
    return {m_pool->get_queue_size_accumulator()};
}

Expected<PipelineBuffer> NmsMuxElement::action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional)
{
    std::vector<MemoryView> input_views;

    input_views.reserve(inputs.size());
    for (auto &input_buf : inputs) {
        input_views.push_back(input_buf.as_view());
    }

    auto acquired_buffer = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == acquired_buffer.status()) {
        return make_unexpected(acquired_buffer.status());
    }

    if (!acquired_buffer) {
        for (auto &input : inputs) {
            auto exec_done_cb = input.get_exec_done_cb();
            exec_done_cb(acquired_buffer.status());
        }
    }    
    CHECK_AS_EXPECTED(HAILO_TIMEOUT != acquired_buffer.status(), HAILO_TIMEOUT,
        "{} failed with status={} (timeout={}ms)", name(), HAILO_TIMEOUT, m_timeout.count());
    CHECK_EXPECTED(acquired_buffer);

    m_duration_collector.start_measurement();
    const auto status = fuse_buffers(input_views, m_nms_infos, acquired_buffer.value().as_view());
    m_duration_collector.complete_measurement();

    for (auto &input : inputs) {
        auto exec_done_cb = input.get_exec_done_cb();
        exec_done_cb(status);
    }
    acquired_buffer->set_action_status(status);

    CHECK_SUCCESS_AS_EXPECTED(status);

    return acquired_buffer.release();
}

Expected<std::shared_ptr<TransformDemuxElement>> TransformDemuxElement::create(std::shared_ptr<OutputDemuxer> demuxer,
    const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
    hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    PipelineDirection pipeline_direction, bool is_last_copy_element, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    std::vector<BufferPoolPtr> pools;
    pools.reserve(demuxer->get_edges_stream_info().size());
    for (const auto& mux_edge : demuxer->get_edges_stream_info()) {
        auto buffer_pool = BufferPool::create(mux_edge.hw_frame_size, buffer_pool_size, shutdown_event, elem_flags, vstream_flags, is_last_copy_element);
        CHECK_EXPECTED(buffer_pool, "Failed creating BufferPool");
        pools.push_back(buffer_pool.release());
    }

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);


    auto demux_elem_ptr = make_shared_nothrow<TransformDemuxElement>(demuxer, std::move(pools), name, timeout,
        duration_collector.release(), std::move(pipeline_status), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != demux_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return demux_elem_ptr;
}

Expected<std::shared_ptr<TransformDemuxElement>> TransformDemuxElement::create(std::shared_ptr<OutputDemuxer> demuxer,
    const std::string &name, const ElementBuildParams &build_params,
    PipelineDirection pipeline_direction, bool is_last_copy_element, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return TransformDemuxElement::create(demuxer, name, build_params.timeout, build_params.buffer_pool_size_edges, build_params.elem_stats_flags,
        build_params.vstream_stats_flags, build_params.shutdown_event, build_params.pipeline_status, pipeline_direction, is_last_copy_element, async_pipeline);
}

TransformDemuxElement::TransformDemuxElement(std::shared_ptr<OutputDemuxer> demuxer, std::vector<BufferPoolPtr> &&pools,
    const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline) :
    BaseDemuxElement(demuxer->get_edges_stream_info().size(), name, timeout, std::move(duration_collector),
        std::move(pipeline_status), std::move(pools), pipeline_direction, async_pipeline),
    m_demuxer(demuxer) 
{}

std::vector<AccumulatorPtr> TransformDemuxElement::get_queue_size_accumulators()
{
    std::vector<AccumulatorPtr> result;
    for (const auto &pool : m_pools) {
        if (nullptr != pool->get_queue_size_accumulator()) {
            result.emplace_back(pool->get_queue_size_accumulator());
        }
    }
    return result;
}

Expected<std::vector<PipelineBuffer>> TransformDemuxElement::action(PipelineBuffer &&input)
{
    std::vector<PipelineBuffer> outputs;
    std::vector<MemoryView> raw_buffers;

    auto mux_edges = m_demuxer->get_edges_stream_info();
    outputs.reserve(mux_edges.size());
    raw_buffers.reserve(mux_edges.size());

    for (uint32_t i = 0; i < mux_edges.size(); i++) {
        auto acquired_buffer = m_pools[i]->acquire_buffer(m_timeout);
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == acquired_buffer.status()) {
            return make_unexpected(acquired_buffer.status());
        }

        if (!acquired_buffer) {
                input.get_exec_done_cb()(acquired_buffer.status());
        } 
        CHECK_EXPECTED(acquired_buffer, "Failed to acquire buffer");
        outputs.emplace_back(acquired_buffer.release());
        raw_buffers.push_back(outputs.back().as_view());
    }

    m_duration_collector.start_measurement();
    const auto status = m_demuxer->transform_demux(input.as_view(), raw_buffers);
    m_duration_collector.complete_measurement();

    auto exec_done_cb = input.get_exec_done_cb();
    exec_done_cb(status);
    for (auto &output : outputs) {
        output.set_action_status(status);
    }

    CHECK_SUCCESS_AS_EXPECTED(status);

    return outputs;
}

PixBufferElement::PixBufferElement(const std::string &name, std::chrono::milliseconds timeout,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    hailo_format_order_t order, std::shared_ptr<AsyncPipeline> async_pipeline) :
        BaseDemuxElement(((order == HAILO_FORMAT_ORDER_I420) ? NUMBER_OF_PLANES_I420 : NUMBER_OF_PLANES_NV12_NV21),
            name, timeout, std::move(duration_collector), std::move(pipeline_status),
            {}, PipelineDirection::PUSH, async_pipeline),
        m_order(order)
{}

Expected<bool> PixBufferElement::can_push_buffer_upstream(const std::string &pad_name)
{
    return m_sinks[0].prev()->element().can_push_buffer_upstream(pad_name);
}

Expected<std::shared_ptr<PixBufferElement>> PixBufferElement::create(const std::string &name,
    std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, hailo_format_order_t order,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto pix_buffer_splitter_elem_ptr = make_shared_nothrow<PixBufferElement>(name, timeout,
        std::move(duration_collector), std::move(pipeline_status), order, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != pix_buffer_splitter_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return pix_buffer_splitter_elem_ptr;
}

Expected<std::vector<PipelineBuffer>> PixBufferElement::action(PipelineBuffer &&input)
{
    // splits the planes into buffers
    m_duration_collector.start_measurement();
    std::vector<PipelineBuffer> outputs;

    auto input_pix_buffer_expected = input.as_hailo_pix_buffer(m_order);

    if (!input_pix_buffer_expected) {
        input.get_exec_done_cb()(input_pix_buffer_expected.status());
    }
    CHECK_EXPECTED(input_pix_buffer_expected);
    auto input_pix_buffer = input_pix_buffer_expected.release();

    if (PipelineBuffer::Type::FLUSH == input.get_type()) {
        for (uint32_t i = 0; i < input_pix_buffer.number_of_planes; i++) {
            outputs.emplace_back(PipelineBuffer(PipelineBuffer::Type::FLUSH));
        }
    } else {
        auto shared_counter = make_shared_nothrow<std::atomic_uint32_t>(input_pix_buffer.number_of_planes);
        if (!shared_counter) {
            input.get_exec_done_cb()(HAILO_OUT_OF_HOST_MEMORY);
        }
        CHECK_NOT_NULL_AS_EXPECTED(shared_counter, HAILO_OUT_OF_HOST_MEMORY);

        for (uint32_t i = 0; i < input_pix_buffer.number_of_planes; i++) {
            outputs.emplace_back(MemoryView(input_pix_buffer.planes[i].user_ptr, input_pix_buffer.planes[i].bytes_used),
                [shared_counter, input_cb = input.get_exec_done_cb()](hailo_status status)
                {
                    if (--*shared_counter == 0) {
                        input_cb(status);
                    }
                });
        }
    }

    m_duration_collector.complete_measurement();
    return outputs;
}

Expected<std::shared_ptr<ArgmaxPostProcessElement>> ArgmaxPostProcessElement::create(std::shared_ptr<net_flow::Op> argmax_op,
    const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    size_t buffer_pool_size, std::chrono::milliseconds timeout, hailo_vstream_stats_flags_t vstream_flags,
    EventPtr shutdown_event, PipelineDirection pipeline_direction, bool is_last_copy_element, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto out_metadata = argmax_op->outputs_metadata().begin()->second;
    auto buffer_size = HailoRTCommon::get_frame_size(out_metadata.shape, out_metadata.format);
    auto buffer_pool = BufferPool::create(buffer_size, buffer_pool_size, shutdown_event, elem_flags, vstream_flags, is_last_copy_element);
    CHECK_EXPECTED(buffer_pool, "Failed creating BufferPool for {}", name);

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);
    auto argmax_elem_ptr = make_shared_nothrow<ArgmaxPostProcessElement>(argmax_op,
        name, duration_collector.release(), std::move(pipeline_status), timeout, buffer_pool.release(), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != argmax_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);
    LOGGER__INFO("Created {}", argmax_elem_ptr->name());
    return argmax_elem_ptr;
}

Expected<std::shared_ptr<ArgmaxPostProcessElement>> ArgmaxPostProcessElement::create(std::shared_ptr<net_flow::Op> argmax_op,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction, bool is_last_copy_element,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return ArgmaxPostProcessElement::create(argmax_op, name,
        build_params.elem_stats_flags, build_params.pipeline_status, build_params.buffer_pool_size_edges, build_params.timeout,
        build_params.vstream_stats_flags, build_params.shutdown_event, pipeline_direction, is_last_copy_element, async_pipeline);
}

ArgmaxPostProcessElement::ArgmaxPostProcessElement(std::shared_ptr<net_flow::Op> argmax_op, const std::string &name,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    std::chrono::milliseconds timeout, BufferPoolPtr buffer_pool, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, buffer_pool, timeout, async_pipeline),
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
    auto buffer = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.get_exec_done_cb()(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status());

    std::map<std::string, MemoryView> inputs;
    std::map<std::string, MemoryView> outputs;
    auto &input_name = m_argmax_op->inputs_metadata().begin()->first;
    auto &output_name = m_argmax_op->outputs_metadata().begin()->first;
    inputs.insert({input_name, input.as_view()});
    outputs.insert({output_name, buffer->as_view()});
    m_duration_collector.start_measurement();
    auto post_process_result = m_argmax_op->execute(inputs, outputs);
    m_duration_collector.complete_measurement();

    auto exec_done_cb = input.get_exec_done_cb();
    exec_done_cb(post_process_result);
    buffer->set_action_status(post_process_result);

    CHECK_SUCCESS_AS_EXPECTED(post_process_result);

    return buffer.release();
}

Expected<std::shared_ptr<SoftmaxPostProcessElement>> SoftmaxPostProcessElement::create(std::shared_ptr<net_flow::Op> softmax_op,
    const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, size_t buffer_pool_size, std::chrono::milliseconds timeout,
    hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, PipelineDirection pipeline_direction, bool is_last_copy_element,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto out_metadata = softmax_op->outputs_metadata().begin()->second;
    auto buffer_size = HailoRTCommon::get_frame_size(out_metadata.shape, out_metadata.format);
    auto buffer_pool = BufferPool::create(buffer_size, buffer_pool_size, shutdown_event, elem_flags, vstream_flags, is_last_copy_element);
    CHECK_EXPECTED(buffer_pool, "Failed creating BufferPool for {}", name);

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);
    auto softmax_elem_ptr = make_shared_nothrow<SoftmaxPostProcessElement>(softmax_op,
        name, duration_collector.release(), std::move(pipeline_status), timeout, buffer_pool.release(), pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != softmax_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);
    LOGGER__INFO("Created {}", softmax_elem_ptr->name());
    return softmax_elem_ptr;
}

Expected<std::shared_ptr<SoftmaxPostProcessElement>> SoftmaxPostProcessElement::create(std::shared_ptr<net_flow::Op> softmax_op,
    const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction, bool is_last_copy_element,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    return SoftmaxPostProcessElement::create(softmax_op, name, build_params.elem_stats_flags, build_params.pipeline_status, build_params.buffer_pool_size_edges,
        build_params.timeout, build_params.vstream_stats_flags, build_params.shutdown_event, pipeline_direction, is_last_copy_element, async_pipeline);
}

SoftmaxPostProcessElement::SoftmaxPostProcessElement(std::shared_ptr<net_flow::Op> softmax_op, const std::string &name,
    DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    std::chrono::milliseconds timeout, BufferPoolPtr buffer_pool, PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, buffer_pool, timeout, async_pipeline),
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
    auto buffer = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }

    if (!buffer) {
        input.get_exec_done_cb()(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status());

    std::map<std::string, MemoryView> inputs;
    std::map<std::string, MemoryView> outputs;
    auto &input_name = m_softmax_op->inputs_metadata().begin()->first;
    auto &output_name = m_softmax_op->outputs_metadata().begin()->first;
    inputs.insert({input_name, input.as_view()});
    outputs.insert({output_name, buffer->as_view()});
    m_duration_collector.start_measurement();
    auto post_process_result = m_softmax_op->execute(inputs, outputs);
    m_duration_collector.complete_measurement();

    auto exec_done_cb = input.get_exec_done_cb();
    exec_done_cb(post_process_result);
    buffer->set_action_status(post_process_result);

    CHECK_SUCCESS_AS_EXPECTED(post_process_result);

    return buffer.release();
}

BaseVStream::BaseVStream(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
                         std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
                         std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                         EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator, EventPtr &&core_op_activated_event,
                         hailo_status &output_status) :
    m_vstream_info(vstream_info),
    m_quant_infos(quant_infos),
    m_vstream_params(vstream_params),
    m_measure_pipeline_latency((vstream_params.vstream_stats_flags & HAILO_VSTREAM_STATS_MEASURE_LATENCY) != 0),
    m_entry_element(pipeline_entry),
    m_pipeline(std::move(pipeline)),
    m_is_activated(false),
    m_is_aborted(false),
    m_pipeline_status(std::move(pipeline_status)),
    m_shutdown_event(shutdown_event),
    m_core_op_activated_event(std::move(core_op_activated_event)),
    m_fps_accumulators(get_pipeline_accumulators_by_type(m_pipeline, AccumulatorType::FPS)),
    m_latency_accumulators(get_pipeline_accumulators_by_type(m_pipeline, AccumulatorType::LATENCY)),
    m_queue_size_accumulators(get_pipeline_queue_size_accumulators(m_pipeline)),
    m_pipeline_latency_accumulator(pipeline_latency_accumulator)
{
    output_status = start_vstream();
}

BaseVStream::BaseVStream(BaseVStream &&other) noexcept :
    m_vstream_info(std::move(other.m_vstream_info)),
    m_vstream_params(std::move(other.m_vstream_params)),
    m_measure_pipeline_latency(std::move(other.m_measure_pipeline_latency)),
    m_entry_element(std::move(other.m_entry_element)),
    m_pipeline(std::move(other.m_pipeline)),
    m_is_activated(std::exchange(other.m_is_activated, false)),
    m_is_aborted(std::exchange(other.m_is_aborted, false)),
    m_pipeline_status(std::move(other.m_pipeline_status)),
    m_shutdown_event(std::move(other.m_shutdown_event)),
    m_core_op_activated_event(std::move(other.m_core_op_activated_event)),
    m_fps_accumulators(std::move(other.m_fps_accumulators)),
    m_latency_accumulators(std::move(other.m_latency_accumulators)),
    m_queue_size_accumulators(std::move(other.m_queue_size_accumulators)),
    m_pipeline_latency_accumulator(std::move(other.m_pipeline_latency_accumulator))
{}

BaseVStream& BaseVStream::operator=(BaseVStream &&other) noexcept
{
    if (this != &other) {
        // operator= is used only for vstream creation BEFORE activation. otherwise we should deactivate vstream here
        assert(!m_is_activated);
        m_vstream_info = std::move(other.m_vstream_info);
        m_quant_infos = std::move(other.m_quant_infos);
        m_vstream_params = std::move(other.m_vstream_params);
        m_measure_pipeline_latency = std::move(other.m_measure_pipeline_latency);
        m_entry_element = std::move(other.m_entry_element);
        m_pipeline = std::move(other.m_pipeline);
        m_is_activated = std::exchange(other.m_is_activated, false);
        m_is_aborted = std::exchange(other.m_is_aborted, false);
        m_pipeline_status = std::move(other.m_pipeline_status);
        m_shutdown_event = std::move(other.m_shutdown_event);
        m_core_op_activated_event = std::move(other.m_core_op_activated_event);
        m_fps_accumulators = std::move(other.m_fps_accumulators);
        m_latency_accumulators = std::move(other.m_latency_accumulators);
        m_queue_size_accumulators = std::move(other.m_queue_size_accumulators);
        m_pipeline_latency_accumulator = std::move(other.m_pipeline_latency_accumulator);
    }
    return *this;
}

hailo_status BaseVStream::start_vstream()
{
    auto status = m_shutdown_event->reset();
    CHECK_SUCCESS(status);

    status = resume();
    CHECK(((status == HAILO_SUCCESS) || (status == HAILO_STREAM_NOT_ACTIVATED)), status,
        "Failed to resume stream in {}", name());

    LOGGER__DEBUG("Activating {}...", name());
    status = m_entry_element->activate();
    CHECK_SUCCESS(status);

    m_is_activated = true;
    return HAILO_SUCCESS;
}

hailo_status BaseVStream::abort()
{
    auto status = m_entry_element->abort();
    CHECK_SUCCESS(status);
    m_is_aborted = true;

    return HAILO_SUCCESS;
}

hailo_status BaseVStream::resume()
{
    auto status = m_entry_element->clear_abort();
    CHECK_SUCCESS(status);
    m_is_aborted = false;

    if (m_is_activated) {
        status = m_entry_element->activate();
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

hailo_status BaseVStream::stop_vstream()
{
    hailo_status status = HAILO_SUCCESS;
    if (m_is_activated) {
        m_is_activated = false;
        status = m_entry_element->deactivate();
        if (HAILO_SUCCESS != status) {
            LOGGER__WARNING("Failed deactivate of vstream {} status {}", name(), status);
        }

        // If VStream was aborted, do not clear low-level stream abortion,
        // otherwise flush would be called on low-level stream d-tor when there is no receiver.
        auto should_clear_abort = (!m_is_aborted);
        status = m_entry_element->post_deactivate(should_clear_abort);
        if (HAILO_SUCCESS != status) {
            LOGGER__WARNING("Failed post deactivate of vstream {} status {}", name(), status);
        }
    }
    return status;
}

hailo_status BaseVStream::stop_and_clear()
{
    auto status = HAILO_SUCCESS;
    if (nullptr != m_core_op_activated_event) {
        status = m_core_op_activated_event->wait(std::chrono::milliseconds(0));
        CHECK(HAILO_TIMEOUT == status, HAILO_INVALID_OPERATION,
            "Trying to clear {} vstream before its network group is deactivated", name());
    }

    status = stop_vstream();
    CHECK_SUCCESS(status);

    status = m_entry_element->clear();
    CHECK_SUCCESS(status, "Failed clearing vstream {}", name());

    const auto curr_pipeline_status = m_pipeline_status->load();
    if (HAILO_SUCCESS != curr_pipeline_status) {
        LOGGER__TRACE("Overwritting current pipeline status {}", curr_pipeline_status);
        m_pipeline_status->store(HAILO_SUCCESS);
    }

    return status;
}

hailo_status BaseVStream::before_fork()
{
    return HAILO_SUCCESS;
}

hailo_status BaseVStream::after_fork_in_parent()
{
    return HAILO_SUCCESS;
}

hailo_status BaseVStream::after_fork_in_child()
{
    return HAILO_SUCCESS;
}

size_t BaseVStream::get_frame_size() const
{
    return HailoRTCommon::get_frame_size(m_vstream_info, m_vstream_params.user_buffer_format);
}

const hailo_vstream_info_t &BaseVStream::get_info() const
{
    return m_vstream_info;
}

const std::vector<hailo_quant_info_t> &BaseVStream::get_quant_infos() const
{
    return m_quant_infos;
}

const hailo_format_t &BaseVStream::get_user_buffer_format() const
{
    return m_vstream_params.user_buffer_format;
}

std::string BaseVStream::name() const
{
    return std::string(m_vstream_info.name);
}

std::string BaseVStream::network_name() const
{
    return std::string(m_vstream_info.network_name);
}

const std::map<std::string, AccumulatorPtr> &BaseVStream::get_fps_accumulators() const
{
    return m_fps_accumulators;
}

const std::map<std::string, AccumulatorPtr> &BaseVStream::get_latency_accumulators() const
{
    return m_latency_accumulators;
}

const std::map<std::string, std::vector<AccumulatorPtr>> &BaseVStream::get_queue_size_accumulators() const
{
    return m_queue_size_accumulators;
}

AccumulatorPtr BaseVStream::get_pipeline_latency_accumulator() const
{
    return m_pipeline_latency_accumulator;
}


const std::vector<std::shared_ptr<PipelineElement>> &BaseVStream::get_pipeline() const
{
    return m_pipeline;
}

Expected<InputVStream> InputVStream::create(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos,
        const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
        std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, EventPtr core_op_activated_event,
        AccumulatorPtr pipeline_latency_accumulator)
{
    auto vstream_internal = InputVStreamInternal::create(vstream_info, quant_infos, vstream_params, pipeline_entry, pipeline_exit,
        std::move(pipeline), std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator);
    CHECK_EXPECTED(vstream_internal);

    InputVStream vstream(vstream_internal.release());
    return vstream;
}

hailo_status InputVStream::write(const MemoryView &buffer)
{
    return m_vstream->write(std::move(buffer));
}

hailo_status InputVStream::write(const hailo_pix_buffer_t &buffer)
{
    // If only one plane is passed, address it as memview
    if (1 == buffer.number_of_planes) {
        return write(MemoryView(buffer.planes[0].user_ptr, buffer.planes[0].bytes_used));
    }

    // If model is multi planar, pass the pix buffer
    if (m_vstream->is_multi_planar()){
        return m_vstream->write(buffer);
    }

    // Other cases - allocate a contiguous buffer to hold all plains
    bool is_contiguous = true;
    uint32_t planes_total_size = 0;
    /* assuming contiguous memory. If not, this will be overriden by the coming loop */
    void *data_ptr = buffer.planes[0].user_ptr;

    /* calculate total data size by summing the planes' sizes and check if the planes are contiguous */
    for (uint32_t plane_index = 0; plane_index < buffer.number_of_planes; plane_index++){
        auto &plane = buffer.planes[plane_index];
        planes_total_size += plane.bytes_used;

        if (is_contiguous && (plane_index + 1 < buffer.number_of_planes)){
            auto &next_plane = buffer.planes[plane_index+1];
            if ((static_cast<uint8_t*>(plane.user_ptr) + plane.bytes_used) != next_plane.user_ptr){
                is_contiguous = false;
            }
        }
    }

    BufferPtr contiguous_buffer = nullptr;
    if (! is_contiguous) {
        /* copy to a contiguous buffer, and then pass it */
        auto expected_buffer = Buffer::create_shared(planes_total_size);
        CHECK_EXPECTED_AS_STATUS(expected_buffer);
        contiguous_buffer = expected_buffer.release();
        uint32_t copied_bytes = 0;

        for (uint32_t plane_index = 0; plane_index < buffer.number_of_planes; plane_index++){
            auto &plane = buffer.planes[plane_index];
            std::memcpy(contiguous_buffer->data() + copied_bytes, plane.user_ptr, plane.bytes_used);
            copied_bytes += plane.bytes_used;
        }

        data_ptr = contiguous_buffer->data();
    }

    return m_vstream->write(std::move(MemoryView(data_ptr, planes_total_size)));
}

hailo_status InputVStream::flush()
{
    return m_vstream->flush();
}

hailo_status InputVStream::clear(std::vector<InputVStream> &vstreams)
{
    for (auto &vstream : vstreams) {
        auto status = vstream.stop_and_clear();
        CHECK_SUCCESS(status);
    }
    for (auto &vstream : vstreams) {
        auto status = vstream.start_vstream();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status InputVStream::clear(std::vector<std::reference_wrapper<InputVStream>> &vstreams)
{
    for (auto &vstream : vstreams) {
        auto status = vstream.get().stop_and_clear();
        CHECK_SUCCESS(status);
    }
    for (auto &vstream : vstreams) {
        auto status = vstream.get().start_vstream();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status InputVStream::abort()
{
    return m_vstream->abort();
}

hailo_status InputVStream::resume()
{
    return m_vstream->resume();
}

size_t InputVStream::get_frame_size() const
{
    return m_vstream->get_frame_size();
}

const hailo_vstream_info_t &InputVStream::get_info() const
{
    return m_vstream->get_info();
}

const std::vector<hailo_quant_info_t> &InputVStream::get_quant_infos() const
{
    return m_vstream->get_quant_infos();
}

const hailo_format_t &InputVStream::get_user_buffer_format() const
{
    return m_vstream->get_user_buffer_format();
}

std::string InputVStream::name() const
{
    return m_vstream->name();
}

std::string InputVStream::network_name() const
{
    return m_vstream->network_name();
}

const std::map<std::string, AccumulatorPtr> &InputVStream::get_fps_accumulators() const
{
    return m_vstream->get_fps_accumulators();
}

const std::map<std::string, AccumulatorPtr> &InputVStream::get_latency_accumulators() const
{
    return m_vstream->get_latency_accumulators();
}

const std::map<std::string, std::vector<AccumulatorPtr>> &InputVStream::get_queue_size_accumulators() const
{
    return m_vstream->get_queue_size_accumulators();
}

AccumulatorPtr InputVStream::get_pipeline_latency_accumulator() const
{
    return m_vstream->get_pipeline_latency_accumulator();
}

const std::vector<std::shared_ptr<PipelineElement>> &InputVStream::get_pipeline() const
{
    return m_vstream->get_pipeline();
}

hailo_status InputVStream::start_vstream()
{
    return m_vstream->start_vstream();
}

hailo_status InputVStream::stop_vstream()
{
    return m_vstream->stop_vstream();
}

hailo_status InputVStream::stop_and_clear()
{
    return m_vstream->stop_and_clear();
}

std::string InputVStream::get_pipeline_description() const
{
    return m_vstream->get_pipeline_description();
}

bool InputVStream::is_aborted()
{
    return m_vstream->is_aborted();
}

bool InputVStream::is_multi_planar()
{
    return m_vstream->is_multi_planar();
}


hailo_status InputVStream::before_fork()
{
    return m_vstream->before_fork();
}

hailo_status InputVStream::after_fork_in_parent()
{
    return m_vstream->after_fork_in_parent();
}

hailo_status InputVStream::after_fork_in_child()
{
    return m_vstream->after_fork_in_child();
}

InputVStream::InputVStream(std::shared_ptr<InputVStreamInternal> vstream) : m_vstream(std::move(vstream)) {}

Expected<OutputVStream> OutputVStream::create(
        const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
        EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator)
{
    auto vstream_internal = OutputVStreamInternal::create(vstream_info, quant_infos, vstream_params, pipeline_entry,
        std::move(pipeline), std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator);
    CHECK_EXPECTED(vstream_internal);

    OutputVStream vstream(vstream_internal.release());
    return vstream;
}

hailo_status OutputVStream::read(MemoryView buffer)
{
    return m_vstream->read(std::move(buffer));
}

hailo_status OutputVStream::clear(std::vector<OutputVStream> &vstreams)
{
    for (auto &vstream : vstreams) {
        auto status = vstream.stop_and_clear();
        CHECK_SUCCESS(status);
    }
    for (auto &vstream : vstreams) {
        auto status = vstream.start_vstream();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status OutputVStream::abort()
{
    return m_vstream->abort();
}

hailo_status OutputVStream::resume()
{
    return m_vstream->resume();
}

hailo_status OutputVStream::clear(std::vector<std::reference_wrapper<OutputVStream>> &vstreams)
{
    for (auto &vstream : vstreams) {
        auto status = vstream.get().stop_and_clear();
        CHECK_SUCCESS(status);
    }
    for (auto &vstream : vstreams) {
        auto status = vstream.get().start_vstream();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

size_t OutputVStream::get_frame_size() const
{
    return m_vstream->get_frame_size();
}

const hailo_vstream_info_t &OutputVStream::get_info() const
{
    return m_vstream->get_info();
}

const std::vector<hailo_quant_info_t> &OutputVStream::get_quant_infos() const
{
    return m_vstream->get_quant_infos();
}

const hailo_format_t &OutputVStream::get_user_buffer_format() const
{
    return m_vstream->get_user_buffer_format();
}

std::string OutputVStream::name() const
{
    return m_vstream->name();
}

std::string OutputVStream::network_name() const
{
    return m_vstream->network_name();
}

const std::map<std::string, AccumulatorPtr> &OutputVStream::get_fps_accumulators() const
{
    return m_vstream->get_fps_accumulators();
}

const std::map<std::string, AccumulatorPtr> &OutputVStream::get_latency_accumulators() const
{
    return m_vstream->get_latency_accumulators();
}

const std::map<std::string, std::vector<AccumulatorPtr>> &OutputVStream::get_queue_size_accumulators() const
{
    return m_vstream->get_queue_size_accumulators();
}

AccumulatorPtr OutputVStream::get_pipeline_latency_accumulator() const
{
    return m_vstream->get_pipeline_latency_accumulator();
}

const std::vector<std::shared_ptr<PipelineElement>> &OutputVStream::get_pipeline() const
{
    return m_vstream->get_pipeline();
}

hailo_status OutputVStream::start_vstream()
{
    return m_vstream->start_vstream();
}

hailo_status OutputVStream::stop_vstream()
{
    return m_vstream->stop_vstream();
}

hailo_status OutputVStream::stop_and_clear()
{
    return m_vstream->stop_and_clear();
}

std::string OutputVStream::get_pipeline_description() const
{
    return m_vstream->get_pipeline_description();
}

bool OutputVStream::is_aborted()
{
    return m_vstream->is_aborted();
}

hailo_status OutputVStream::before_fork()
{
    return m_vstream->before_fork();
}

hailo_status OutputVStream::after_fork_in_parent()
{
    return m_vstream->after_fork_in_parent();
}

hailo_status OutputVStream::after_fork_in_child()
{
    return m_vstream->after_fork_in_child();
}

hailo_status OutputVStream::set_nms_score_threshold(float32_t threshold)
{
    return m_vstream->set_nms_score_threshold(threshold);
}

hailo_status OutputVStream::set_nms_iou_threshold(float32_t threshold)
{
    return m_vstream->set_nms_iou_threshold(threshold);
}

hailo_status OutputVStream::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    return m_vstream->set_nms_max_proposals_per_class(max_proposals_per_class);
}

OutputVStream::OutputVStream(std::shared_ptr<OutputVStreamInternal> vstream) : m_vstream(std::move(vstream)) {}

std::map<std::string, AccumulatorPtr> get_pipeline_accumulators_by_type(
    const std::vector<std::shared_ptr<PipelineElement>> &pipeline, AccumulatorType accumulator_type)
{
    std::map<std::string, AccumulatorPtr> result;
    for (const auto &elem : pipeline) {
        if (nullptr == elem) {
            continue;
        }

        AccumulatorPtr accumulator = nullptr;
        if (AccumulatorType::FPS == accumulator_type) {
            accumulator = elem->get_fps_accumulator();
        } else if (AccumulatorType::LATENCY == accumulator_type) {
            accumulator = elem->get_latency_accumulator();
        } else {
            continue;
        }

        if (nullptr != accumulator) {
            result.emplace(elem->name(), accumulator);
        }
    }

    return result;
}

std::map<std::string, std::vector<AccumulatorPtr>> get_pipeline_queue_size_accumulators(
    const std::vector<std::shared_ptr<PipelineElement>> &pipeline)
{
    std::map<std::string, std::vector<AccumulatorPtr>> result;
    for (const auto &elem : pipeline) {
        if (nullptr == elem) {
            continue;
        }

        const auto accumulators = elem->get_queue_size_accumulators();
        if (0 != accumulators.size()) {
            result.emplace(elem->name(), accumulators);
        }
    }

    return result;
}

Expected<std::shared_ptr<InputVStreamInternal>> InputVStreamInternal::create(const hailo_vstream_info_t &vstream_info,
    const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
    std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, EventPtr core_op_activated_event,
    AccumulatorPtr pipeline_latency_accumulator)
{
    auto vstream = InputVStreamImpl::create(vstream_info, quant_infos, vstream_params, pipeline_entry, pipeline_exit,
        std::move(pipeline), std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator);
    CHECK_EXPECTED(vstream);
    auto vstream_ptr = std::shared_ptr<InputVStreamInternal>(vstream.release());
    return vstream_ptr;
}

InputVStreamInternal::InputVStreamInternal(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
                         std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
                         std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                         EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator, EventPtr &&core_op_activated_event,
                         hailo_status &output_status) :
    BaseVStream(vstream_info, quant_infos, vstream_params, pipeline_entry, std::move(pipeline), std::move(pipeline_status),
                shutdown_event, pipeline_latency_accumulator, std::move(core_op_activated_event), output_status){}

Expected<std::shared_ptr<InputVStreamImpl>> InputVStreamImpl::create(const hailo_vstream_info_t &vstream_info,
    const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
    std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, EventPtr core_op_activated_event,
    AccumulatorPtr pipeline_latency_accumulator)
{
    hailo_status status = HAILO_UNINITIALIZED;

    if (nullptr != pipeline_latency_accumulator) {
        if (pipeline_exit) {
            pipeline_exit->sink().set_push_complete_callback([pipeline_latency_accumulator](const PipelineBuffer::Metadata& metadata) {
                    const auto duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(
                        std::chrono::steady_clock::now() - metadata.get_start_time()).count();
                    pipeline_latency_accumulator->add_data_point(duration_sec);
                });
        }
    }

    auto vstream_ptr = std::shared_ptr<InputVStreamImpl>(new InputVStreamImpl(vstream_info, quant_infos, vstream_params, std::move(pipeline_entry), std::move(pipeline),
        std::move(pipeline_status), shutdown_event, pipeline_latency_accumulator, std::move(core_op_activated_event), status));
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed to create virtual stream");

    return vstream_ptr;
}

InputVStreamImpl::InputVStreamImpl(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
    std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
    EventPtr core_op_activated_event, hailo_status &output_status) :
    InputVStreamInternal(vstream_info, quant_infos, vstream_params, pipeline_entry, std::move(pipeline), std::move(pipeline_status),
        shutdown_event, pipeline_latency_accumulator, std::move(core_op_activated_event), output_status)
{
    // TODO: propagate a flag instead of using dynamic_pointer_cast (will be disabled when we'll disable RTTI)
    m_is_multi_planar = (nullptr != std::dynamic_pointer_cast<PixBufferElement>(pipeline_entry));

    if (HAILO_SUCCESS != output_status) {
        return;
    }

    LOGGER__INFO("Creating {}...", name());
}

InputVStreamImpl::~InputVStreamImpl()
{
    (void)stop_vstream();
}

hailo_status InputVStreamImpl::write(const MemoryView &buffer)
{
    if (nullptr != m_core_op_activated_event) {
        CHECK(m_is_activated, HAILO_VSTREAM_PIPELINE_NOT_ACTIVATED, "Failed to write buffer! Virtual stream {} is not activated!", name());
        auto status = m_core_op_activated_event->wait(std::chrono::milliseconds(0));
        CHECK(HAILO_TIMEOUT != status, HAILO_NETWORK_GROUP_NOT_ACTIVATED,
            "Trying to write to vstream {} before its network group is activated", name());
    }

    assert(1 == m_entry_element->sinks().size());
    auto status = m_entry_element->sinks()[0].run_push(PipelineBuffer(buffer, false, nullptr, m_measure_pipeline_latency));
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Sending to VStream was shutdown!");
        status = m_pipeline_status->load();
    }
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Sending to VStream was aborted!");
        return HAILO_STREAM_ABORTED_BY_USER;
    }
    return status;
}

hailo_status InputVStreamImpl::write(const hailo_pix_buffer_t &buffer)
{
    if (nullptr != m_core_op_activated_event) {
        CHECK(m_is_activated, HAILO_VSTREAM_PIPELINE_NOT_ACTIVATED, "Failed to write buffer! Virtual stream {} is not activated!", name());
        auto status = m_core_op_activated_event->wait(std::chrono::milliseconds(0));
        CHECK(HAILO_TIMEOUT != status, HAILO_NETWORK_GROUP_NOT_ACTIVATED,
            "Trying to write to vstream {} before its network group is activated", name());
    }

    assert(1 == m_entry_element->sinks().size());
    auto status = m_entry_element->sinks()[0].run_push(PipelineBuffer(buffer));
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Sending to VStream was shutdown!");
        status = m_pipeline_status->load();
    }
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Sending to VStream was aborted!");
        return HAILO_STREAM_ABORTED_BY_USER;
    }
    return status;
}

hailo_status InputVStreamImpl::flush()
{
    assert(1 == m_entry_element->sinks().size());
    auto status =  m_entry_element->sinks()[0].run_push(PipelineBuffer(PipelineBuffer::Type::FLUSH));
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Sending to VStream was aborted!");
        return HAILO_STREAM_ABORTED_BY_USER;
    }
    CHECK_SUCCESS(status);

    status = m_entry_element->flush();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

bool InputVStreamImpl::is_multi_planar() const
{
    return m_is_multi_planar;
}

#ifdef HAILO_SUPPORT_MULTI_PROCESS
Expected<std::shared_ptr<InputVStreamClient>> InputVStreamClient::create(VStreamIdentifier &&identifier)
{
    grpc::ChannelArguments ch_args;
    ch_args.SetMaxReceiveMessageSize(-1);
    auto channel = grpc::CreateCustomChannel(hailort::HAILORT_SERVICE_ADDRESS, grpc::InsecureChannelCredentials(), ch_args);
    CHECK_AS_EXPECTED(channel != nullptr, HAILO_INTERNAL_FAILURE);

    auto client = make_unique_nothrow<HailoRtRpcClient>(channel);
    CHECK_AS_EXPECTED(client != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto user_buffer_format = client->InputVStream_get_user_buffer_format(identifier);
    CHECK_EXPECTED(user_buffer_format);

    auto vstream_info = client->InputVStream_get_info(identifier);
    CHECK_EXPECTED(vstream_info);

    return std::shared_ptr<InputVStreamClient>(new InputVStreamClient(std::move(client), std::move(identifier),
        user_buffer_format.release(), vstream_info.release()));
}

InputVStreamClient::InputVStreamClient(std::unique_ptr<HailoRtRpcClient> client, VStreamIdentifier &&identifier, hailo_format_t &&user_buffer_format,
    hailo_vstream_info_t &&info) :
        m_client(std::move(client)), m_identifier(std::move(identifier)), m_user_buffer_format(user_buffer_format), m_info(info) {}

InputVStreamClient::~InputVStreamClient()
{
    auto reply = m_client->InputVStream_release(m_identifier, OsUtils::get_curr_pid());
    if (reply != HAILO_SUCCESS) {
        LOGGER__CRITICAL("InputVStream_release failed!");
    }
}

hailo_status InputVStreamClient::write(const MemoryView &buffer)
{
    return m_client->InputVStream_write(m_identifier, buffer);
}

hailo_status InputVStreamClient::write(const hailo_pix_buffer_t &buffer)
{
    return m_client->InputVStream_write(m_identifier, buffer);
}

hailo_status InputVStreamClient::flush()
{
    return m_client->InputVStream_flush(m_identifier);
}

bool InputVStreamClient::is_multi_planar() const
{
    auto is_multi_planar_exp = m_client->InputVStream_is_multi_planar(m_identifier);
    if (!is_multi_planar_exp) {
        LOGGER__CRITICAL("InputVStream_is_multi_planar failed with status={}", is_multi_planar_exp.status());
        return true;
    }
    return is_multi_planar_exp.release();
}

hailo_status InputVStreamClient::abort()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto abort_client = expected_client.release();
    return abort_client->InputVStream_abort(m_identifier);
}

hailo_status InputVStreamClient::resume()
{
    return m_client->InputVStream_resume(m_identifier);
}

hailo_status InputVStreamClient::stop_and_clear()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto stop_and_clear_client = expected_client.release();

    return stop_and_clear_client->InputVStream_stop_and_clear(m_identifier);
}

hailo_status InputVStreamClient::start_vstream()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto start_vstream_client = expected_client.release();

    return start_vstream_client->InputVStream_start_vstream(m_identifier);
}

size_t InputVStreamClient::get_frame_size() const
{
    auto frame_size = m_client->InputVStream_get_frame_size(m_identifier);
    if (!frame_size) {
        LOGGER__CRITICAL("InputVStream_get_frame_size failed with status={}", frame_size.status());
        return 0;
    }
    return frame_size.release();
}

const hailo_vstream_info_t &InputVStreamClient::get_info() const
{
    return m_info;
}

const hailo_format_t &InputVStreamClient::get_user_buffer_format() const
{
    return m_user_buffer_format;
}

std::string InputVStreamClient::name() const
{
    auto expected_name = m_client->InputVStream_name(m_identifier);
    if (!expected_name) {
        LOGGER__CRITICAL("InputVStream_name failed with status={}", expected_name.status());
        return "";
    }
    return expected_name.release();
}

std::string InputVStreamClient::network_name() const
{
    auto expected_name = m_client->InputVStream_network_name(m_identifier);
    if (!expected_name) {
        LOGGER__CRITICAL("InputVStream_name failed with status={}", expected_name.status());
        return "";
    }
    return expected_name.release();
}

const std::map<std::string, AccumulatorPtr> &InputVStreamClient::get_fps_accumulators() const
{
    LOGGER__ERROR("InputVStream::get_fps_accumulators function is not supported when using multi-process service");
    return m_fps_accumulators;
}
const std::map<std::string, AccumulatorPtr> &InputVStreamClient::get_latency_accumulators() const
{
    LOGGER__ERROR("InputVStream::get_latency_accumulators function is not supported when using multi-process service");
    return m_latency_accumulators;
}

const std::map<std::string, std::vector<AccumulatorPtr>> &InputVStreamClient::get_queue_size_accumulators() const
{
    LOGGER__ERROR("InputVStream::get_queue_size_accumulators function is not supported when using multi-process service");
    return m_queue_size_accumulators;
}
AccumulatorPtr InputVStreamClient::get_pipeline_latency_accumulator() const
{
    LOGGER__ERROR("InputVStream::get_pipeline_latency_accumulator function is not supported when using multi-process service");
    return m_pipeline_latency_accumulator;
}
const std::vector<std::shared_ptr<PipelineElement>> &InputVStreamClient::get_pipeline() const
{
    LOGGER__ERROR("InputVStream::get_pipeline function is not supported when using multi-process service");
    return m_pipeline;
}

hailo_status InputVStreamClient::create_client()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    m_client = expected_client.release();
    return HAILO_SUCCESS;
}

hailo_status InputVStreamClient::before_fork()
{
    m_client.reset();
    return HAILO_SUCCESS;
}

hailo_status InputVStreamClient::after_fork_in_parent()
{
    return create_client();
}

hailo_status InputVStreamClient::after_fork_in_child()
{
    return create_client();
}

bool InputVStreamClient::is_aborted()
{
    auto is_aborted_exp = m_client->InputVStream_is_aborted(m_identifier);
    if (!is_aborted_exp) {
        LOGGER__CRITICAL("InputVStream_is_aborted failed with status={}", is_aborted_exp.status());
        return true;
    }
    return is_aborted_exp.release();
}

#endif // HAILO_SUPPORT_MULTI_PROCESS

std::string InputVStreamInternal::get_pipeline_description() const
{
    std::stringstream pipeline_str;
    pipeline_str << "Input pipeline '" << name() << "': ";
    for (const auto &element : m_pipeline) {
        pipeline_str << element->description() << " >> ";
    }
    pipeline_str << "HW";
    return pipeline_str.str();
}

Expected<std::shared_ptr<OutputVStreamInternal>> OutputVStreamInternal::create(
        const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
        EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator)
{
    auto vstream = OutputVStreamImpl::create(vstream_info, quant_infos, vstream_params, pipeline_entry,
        std::move(pipeline), std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator);
    CHECK_EXPECTED(vstream);
    auto vstream_ptr = std::shared_ptr<OutputVStreamInternal>(vstream.release());
    return vstream_ptr;
}

OutputVStreamInternal::OutputVStreamInternal(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
                                             std::shared_ptr<PipelineElement> pipeline_entry,
                                             std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
                                             std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
                                             AccumulatorPtr pipeline_latency_accumulator,
                                             EventPtr core_op_activated_event, hailo_status &output_status) :
    BaseVStream(vstream_info, quant_infos, vstream_params, pipeline_entry, std::move(pipeline), std::move(pipeline_status),
                shutdown_event, pipeline_latency_accumulator, std::move(core_op_activated_event), output_status)
{
    // Reversing the order of pipeline-elements, for the destruction flow to work in the right order (from user-side to hw-side)
    std::reverse(m_pipeline.begin(), m_pipeline.end());
}

Expected<std::shared_ptr<OutputVStreamImpl>> OutputVStreamImpl::create(const hailo_vstream_info_t &vstream_info,
    const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
    std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
    EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator)
{
    hailo_status status = HAILO_UNINITIALIZED;

    CHECK_AS_EXPECTED(1 == pipeline_entry->sources().size(), HAILO_INVALID_ARGUMENT,
        "OutputVStream's entry element is expected to have one source");

    if (nullptr != pipeline_latency_accumulator) {
        pipeline_entry->sources()[0].set_pull_complete_callback([pipeline_latency_accumulator](const PipelineBuffer::Metadata& metadata) {
                const auto duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(
                    std::chrono::steady_clock::now() - metadata.get_start_time()).count();
                pipeline_latency_accumulator->add_data_point(duration_sec);
            });
    }

    auto vstream_ptr = std::shared_ptr<OutputVStreamImpl>(new OutputVStreamImpl(vstream_info, quant_infos, vstream_params, std::move(pipeline_entry), std::move(pipeline),
        std::move(pipeline_status), shutdown_event, pipeline_latency_accumulator, std::move(core_op_activated_event), status));
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed to create virtual stream");

    return vstream_ptr;
}

std::string OutputVStreamInternal::get_pipeline_description() const
{
    std::stringstream pipeline_str;
    pipeline_str << "Output pipeline '" << name() << "': HW";
    for (const auto &element : m_pipeline) {
        pipeline_str << " >> " << element->description();
    }
    return pipeline_str.str();
}

OutputVStreamImpl::OutputVStreamImpl(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos,
                                     const hailo_vstream_params_t &vstream_params,
                                     std::shared_ptr<PipelineElement> pipeline_entry,
                                     std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
                                     std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
                                     AccumulatorPtr pipeline_latency_accumulator,
                                     EventPtr core_op_activated_event, hailo_status &output_status) :
    OutputVStreamInternal(vstream_info, quant_infos, vstream_params, pipeline_entry, std::move(pipeline), std::move(pipeline_status),
                shutdown_event, pipeline_latency_accumulator, std::move(core_op_activated_event), output_status)
{
    if (HAILO_SUCCESS != output_status) {
        return;
    }

    LOGGER__INFO("Creating {}...", name());
}

OutputVStreamImpl::~OutputVStreamImpl()
{
    (void)stop_vstream();
}

hailo_status OutputVStreamImpl::read(MemoryView buffer)
{
    if (nullptr != m_core_op_activated_event) {
        CHECK(m_is_activated, HAILO_VSTREAM_PIPELINE_NOT_ACTIVATED, "read() failed! Virtual stream {} is not activated!", name());
        auto status = m_core_op_activated_event->wait(std::chrono::milliseconds(0));
        if (HAILO_TIMEOUT == status) {
            LOGGER__INFO("Trying to read from vstream {} before its network_group is activated", name());
            return HAILO_NETWORK_GROUP_NOT_ACTIVATED;
        }
        CHECK_SUCCESS(status);
    }

    assert(1 == m_entry_element->sources().size());
    auto recv_buffer = m_entry_element->sources()[0].run_pull(PipelineBuffer(buffer, false, nullptr, m_measure_pipeline_latency));
    auto status = recv_buffer.status();
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
        LOGGER__INFO("Receiving to VStream was shutdown!");
        status = m_pipeline_status->load();
    }
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Receiving to VStream was aborted!");
        m_entry_element->wait_for_finish();
        return HAILO_STREAM_ABORTED_BY_USER;
    }
    return status;
}

hailo_status OutputVStreamImpl::set_nms_score_threshold(float32_t threshold)
{
    auto status = HAILO_INVALID_OPERATION; // Assuming there is no valid element
    for (auto &elem : m_pipeline) {
        auto elem_status = elem->set_nms_score_threshold(threshold);
        if (HAILO_SUCCESS == elem_status) {
            status = elem_status; // 1 element is enough to call this setter successful
        }
    }
    CHECK_SUCCESS(status, "Unable to set NMS score threshold in {}", name());

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamImpl::set_nms_iou_threshold(float32_t threshold)
{
    auto status = HAILO_INVALID_OPERATION; // Assuming there is no valid element
    for (auto &elem : m_pipeline) {
        auto elem_status = elem->set_nms_iou_threshold(threshold);
        if (HAILO_SUCCESS == elem_status) {
            status = elem_status; // 1 element is enough to call this setter successful
        }
    }
    CHECK_SUCCESS(status, "Unable to set NMS IoU threshold in {}", name());

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamImpl::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    auto status = HAILO_INVALID_OPERATION; // Assuming there is no valid element
    for (auto &elem : m_pipeline) {
        auto elem_status = elem->set_nms_max_proposals_per_class(max_proposals_per_class);
        if (HAILO_SUCCESS == elem_status) {
            status = elem_status; // 1 element is enough to call this setter successful
        }
    }
    CHECK_SUCCESS(status, "Unable to set NMS max proposals per class in {}", name());

    // Update vstream info
    m_vstream_info.nms_shape.max_bboxes_per_class = max_proposals_per_class;

    return HAILO_SUCCESS;
}

#ifdef HAILO_SUPPORT_MULTI_PROCESS
Expected<std::shared_ptr<OutputVStreamClient>> OutputVStreamClient::create(const VStreamIdentifier &&identifier)
{
    grpc::ChannelArguments ch_args;
    ch_args.SetMaxReceiveMessageSize(-1);
    auto channel = grpc::CreateCustomChannel(hailort::HAILORT_SERVICE_ADDRESS, grpc::InsecureChannelCredentials(), ch_args);
    CHECK_AS_EXPECTED(channel != nullptr, HAILO_INTERNAL_FAILURE);

    auto client = make_unique_nothrow<HailoRtRpcClient>(channel);
    CHECK_AS_EXPECTED(client != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto user_buffer_format = client->OutputVStream_get_user_buffer_format(identifier);
    CHECK_EXPECTED(user_buffer_format);

    auto info = client->OutputVStream_get_info(identifier);
    CHECK_EXPECTED(info);

    return std::shared_ptr<OutputVStreamClient>(new OutputVStreamClient(std::move(client), std::move(identifier),
        user_buffer_format.release(), info.release()));
}

OutputVStreamClient::OutputVStreamClient(std::unique_ptr<HailoRtRpcClient> client, const VStreamIdentifier &&identifier, hailo_format_t &&user_buffer_format,
    hailo_vstream_info_t &&info) :
        m_client(std::move(client)), m_identifier(std::move(identifier)), m_user_buffer_format(user_buffer_format), m_info(info) {}

OutputVStreamClient::~OutputVStreamClient()
{
    auto reply = m_client->OutputVStream_release(m_identifier, OsUtils::get_curr_pid());
    if (reply != HAILO_SUCCESS) {
        LOGGER__CRITICAL("OutputVStream_release failed!");
    }
}

hailo_status OutputVStreamClient::read(MemoryView buffer)
{
    return m_client->OutputVStream_read(m_identifier, buffer);
}

hailo_status OutputVStreamClient::abort()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto abort_client = expected_client.release();
    return abort_client->OutputVStream_abort(m_identifier);
}

hailo_status OutputVStreamClient::resume()
{
    return m_client->OutputVStream_resume(m_identifier);
}

hailo_status OutputVStreamClient::stop_and_clear()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto stop_and_clear_client = expected_client.release();

    return stop_and_clear_client->OutputVStream_stop_and_clear(m_identifier);
}

hailo_status OutputVStreamClient::start_vstream()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto start_vstream_client = expected_client.release();

    return start_vstream_client->OutputVStream_start_vstream(m_identifier);
}

size_t OutputVStreamClient::get_frame_size() const
{
    auto frame_size =  m_client->OutputVStream_get_frame_size(m_identifier);
    if (!frame_size) {
        LOGGER__CRITICAL("OutputVStream_get_frame_size failed with status={}", frame_size.status());
        return 0;
    }
    return frame_size.release();
}

const hailo_vstream_info_t &OutputVStreamClient::get_info() const
{
    return m_info;
}

const hailo_format_t &OutputVStreamClient::get_user_buffer_format() const
{
    return m_user_buffer_format;
}

std::string OutputVStreamClient::name() const
{
    auto expected_name = m_client->OutputVStream_name(m_identifier);
    if (!expected_name) {
        LOGGER__CRITICAL("OutputVStream_name failed with status={}", expected_name.status());
        return "";
    }
    return expected_name.release();
}

std::string OutputVStreamClient::network_name() const
{
    auto expected_name = m_client->OutputVStream_network_name(m_identifier);
    if (!expected_name) {
        LOGGER__CRITICAL("OutputVStream_name failed with status={}", expected_name.status());
        return "";
    }
    return expected_name.release();
}

const std::map<std::string, AccumulatorPtr> &OutputVStreamClient::get_fps_accumulators() const
{
    LOGGER__ERROR("OutputVStream::get_fps_accumulators function is not supported when using multi-process service");
    return m_fps_accumulators;
}
const std::map<std::string, AccumulatorPtr> &OutputVStreamClient::get_latency_accumulators() const
{
    LOGGER__ERROR("OutputVStream::get_latency_accumulators functoin is not supported when using multi-process service");
    return m_latency_accumulators;
}

const std::map<std::string, std::vector<AccumulatorPtr>> &OutputVStreamClient::get_queue_size_accumulators() const
{
    LOGGER__ERROR("OutputVStream::get_queue_size_accumulators function is not supported when using multi-process service");
    return m_queue_size_accumulators;
}
AccumulatorPtr OutputVStreamClient::get_pipeline_latency_accumulator() const
{
    LOGGER__ERROR("OutputVStream::get_pipeline_latency_accumulator function is not supported when using multi-process service");
    return m_pipeline_latency_accumulator;
}
const std::vector<std::shared_ptr<PipelineElement>> &OutputVStreamClient::get_pipeline() const
{
    LOGGER__ERROR("OutputVStream::get_pipeline function is not supported when using multi-process service");
    return m_pipeline;
}

hailo_status OutputVStreamClient::create_client()
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    m_client = expected_client.release();
    return HAILO_SUCCESS;
}

hailo_status OutputVStreamClient::before_fork()
{
    m_client.reset();
    return HAILO_SUCCESS;
}

hailo_status OutputVStreamClient::after_fork_in_parent()
{
    return create_client();
}

hailo_status OutputVStreamClient::after_fork_in_child()
{
    return create_client();
}

bool OutputVStreamClient::is_aborted()
{
    auto is_aborted_exp = m_client->OutputVStream_is_aborted(m_identifier);
    if (!is_aborted_exp) {
        LOGGER__CRITICAL("OutputVStream_is_aborted failed with status={}", is_aborted_exp.status());
        return true;
    }
    return is_aborted_exp.release();
}

hailo_status OutputVStreamClient::set_nms_score_threshold(float32_t threshold)
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto vstream_client = expected_client.release();

    CHECK_SUCCESS(vstream_client->OutputVStream_set_nms_score_threshold(m_identifier, threshold));

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamClient::set_nms_iou_threshold(float32_t threshold)
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto vstream_client = expected_client.release();

    CHECK_SUCCESS(vstream_client->OutputVStream_set_nms_iou_threshold(m_identifier, threshold));

    return HAILO_SUCCESS;
}

hailo_status OutputVStreamClient::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    auto expected_client = HailoRtRpcClientUtils::create_client();
    CHECK_EXPECTED_AS_STATUS(expected_client);
    auto vstream_client = expected_client.release();

    CHECK_SUCCESS(vstream_client->OutputVStream_set_nms_max_proposals_per_class(m_identifier, max_proposals_per_class));
    m_info.nms_shape.max_bboxes_per_class = max_proposals_per_class;

    return HAILO_SUCCESS;
}

#endif // HAILO_SUPPORT_MULTI_PROCESS

Expected<std::shared_ptr<HwReadElement>> HwReadElement::create(std::shared_ptr<OutputStreamBase> stream, const std::string &name, std::chrono::milliseconds timeout,
    size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction)
{
    auto buffer_pool = BufferPool::create(stream->get_frame_size(), buffer_pool_size, shutdown_event, elem_flags, vstream_flags);
    CHECK_EXPECTED(buffer_pool, "Failed creating BufferPool for {}", name);

    // On HwReadElement the stream always owns the buffer, hence, we set the mode explicitly.
    auto status = stream->set_buffer_mode(StreamBufferMode::OWNING);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto hw_read_elem_ptr = make_shared_nothrow<HwReadElement>(stream, buffer_pool.release(), name, timeout,
        duration_collector.release(), shutdown_event, std::move(pipeline_status), pipeline_direction);
    CHECK_AS_EXPECTED(nullptr != hw_read_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", hw_read_elem_ptr->name());

    return hw_read_elem_ptr;
}

HwReadElement::HwReadElement(std::shared_ptr<OutputStreamBase> stream, BufferPoolPtr buffer_pool, const std::string &name,
                             std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
                             EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                             PipelineDirection pipeline_direction) :
    SourceElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, nullptr),
    m_stream(stream),
    m_pool(buffer_pool),
    m_timeout(timeout),
    m_shutdown_event(shutdown_event),
    m_activation_wait_or_shutdown(stream->get_core_op_activated_event(), shutdown_event)
{}

uint32_t HwReadElement::get_invalid_frames_count()
{
    return m_stream->get_invalid_frames_count();
}

std::string HwReadElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << " | hw_frame_size: " << m_stream->get_info().hw_frame_size << ")";   

    return element_description.str();
}

hailo_status HwReadElement::execute_post_deactivate(bool should_clear_abort)
{
    if (should_clear_abort) {
        auto status = execute_clear_abort();
        CHECK(((HAILO_SUCCESS == status) || (HAILO_STREAM_NOT_ACTIVATED == status)), status,
            "Failed to clear abort stream in {}", name());
    }
    return HAILO_SUCCESS;
}

hailo_status HwReadElement::execute_clear()
{
    return HAILO_SUCCESS;
}

hailo_status HwReadElement::execute_flush()
{
    return HAILO_INVALID_OPERATION;
}

hailo_status HwReadElement::execute_abort()
{
    return m_stream->abort_impl();
}

hailo_status HwReadElement::execute_clear_abort()
{
    return m_stream->clear_abort_impl();
}

hailo_status HwReadElement::execute_wait_for_finish()
{
    return HAILO_SUCCESS;
}

std::vector<AccumulatorPtr> HwReadElement::get_queue_size_accumulators()
{
    if (nullptr == m_pool->get_queue_size_accumulator()) {
        return std::vector<AccumulatorPtr>();
    }
    return {m_pool->get_queue_size_accumulator()};
}

void HwReadElement::run_push_async(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    LOGGER__ERROR("run_push_async is not supported for {}", name());
    assert(false);
}

hailo_status HwReadElement::run_push(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

Expected<PipelineBuffer> HwReadElement::run_pull(PipelineBuffer &&optional, const PipelinePad &/*source*/)
{
    auto buffer = m_pool->get_available_buffer(std::move(optional), m_timeout);
    if (HAILO_SHUTDOWN_EVENT_SIGNALED == buffer.status()) {
        return make_unexpected(buffer.status());
    }
    CHECK_EXPECTED(buffer, "{} (D2H) failed with status={}", name(), buffer.status());

    while (true) {
        if (!m_stream->is_scheduled()) {
            auto status = m_activation_wait_or_shutdown.wait(m_timeout);
            if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
                return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
            }
            if (HAILO_TIMEOUT == status) {
                return make_unexpected(HAILO_NETWORK_GROUP_NOT_ACTIVATED);
            }
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else {
            auto status = m_activation_wait_or_shutdown.wait(std::chrono::milliseconds(0));
            if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
                return make_unexpected(HAILO_SHUTDOWN_EVENT_SIGNALED);
            }
        }

        MemoryView buffer_view(buffer.value().as_view());
        m_duration_collector.start_measurement();
        auto status = m_stream->read(buffer_view);
        if (HAILO_INVALID_FRAME == status) {
            m_stream->increase_invalid_frames_count(1);
            status = HAILO_SUCCESS;
        }
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            // Try again
            continue;
        }
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            LOGGER__INFO("Reading from stream was aborted!");
            return make_unexpected(HAILO_STREAM_ABORTED_BY_USER);
        }
        CHECK_SUCCESS_AS_EXPECTED(status, "{} (D2H) failed with status={}", name(), status);
        m_duration_collector.complete_measurement();

        return buffer.release();
    }
}

hailo_status HwReadElement::execute_activate()
{
    return HAILO_SUCCESS;
}

hailo_status HwReadElement::execute_deactivate()
{
    auto signal_shutdown_status = m_shutdown_event->signal();
    if (HAILO_SUCCESS != signal_shutdown_status) {
        LOGGER__ERROR("Signaling {} shutdown event failed with {}", name(), signal_shutdown_status);
    }

    auto abort_status = execute_abort();
    if ((HAILO_SUCCESS != abort_status) && (HAILO_STREAM_NOT_ACTIVATED != abort_status)) {
        LOGGER__ERROR("Abort {} failed with {}", name(), abort_status);
        return abort_status;
    }

    return signal_shutdown_status;
}

Expected<std::shared_ptr<HwWriteElement>> HwWriteElement::create(std::shared_ptr<InputStreamBase> stream, const std::string &name,
    hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    PipelineDirection pipeline_direction)
{
    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto got_flush_event = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(got_flush_event);

    // On HwWriteElement the stream always owns the buffer, hence, we set the mode explicitly.
    auto status = stream->set_buffer_mode(StreamBufferMode::OWNING);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto hw_write_elem_ptr = make_shared_nothrow<HwWriteElement>(stream, name,
        duration_collector.release(), std::move(pipeline_status), got_flush_event.release(), pipeline_direction);
    CHECK_AS_EXPECTED(nullptr != hw_write_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", hw_write_elem_ptr->name());

    return hw_write_elem_ptr;
}

HwWriteElement::HwWriteElement(std::shared_ptr<InputStreamBase> stream, const std::string &name, DurationCollector &&duration_collector,
                               std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr got_flush_event, PipelineDirection pipeline_direction) :
    SinkElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, nullptr),
    m_stream(stream), m_got_flush_event(got_flush_event)
{}

Expected<PipelineBuffer> HwWriteElement::run_pull(PipelineBuffer &&/*optional*/, const PipelinePad &/*source*/)
{
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status HwWriteElement::run_push(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    if (PipelineBuffer::Type::FLUSH == buffer.get_type()) {
        hailo_status flush_status = m_stream->flush();
        if (HAILO_STREAM_ABORTED_BY_USER == flush_status) {
            LOGGER__INFO("Failed flushing input stream {} because stream was aborted", m_stream->to_string());
        } else if (HAILO_SUCCESS != flush_status) {
            LOGGER__ERROR("flush has failed in {} with status {}", name(), flush_status);
        }
        hailo_status status = m_got_flush_event->signal();
        CHECK_SUCCESS(status);
        return HAILO_SUCCESS;
    }

    m_duration_collector.start_measurement();
    const auto status = m_stream->write(MemoryView(buffer.data(), buffer.size()));
    m_duration_collector.complete_measurement();

    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("Failed to send on input stream {} because stream was aborted", m_stream->to_string());
        return HAILO_STREAM_ABORTED_BY_USER;
    }
    CHECK_SUCCESS(status, "{} (H2D) failed with status={}", name(), status);

    return HAILO_SUCCESS;
}

void HwWriteElement::run_push_async(PipelineBuffer &&/*buffer*/, const PipelinePad &/*sink*/)
{
    LOGGER__ERROR("run_push_async is not supported for {}", name());
    assert(false);
}

hailo_status HwWriteElement::execute_activate()
{
    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_deactivate()
{
    // The flush operation will block until all buffers currently in the pipeline will be processed.
    // We assume that no buffers are sent after the call for deactivate.
    hailo_status flush_status = m_stream->flush();
    if (HAILO_STREAM_ABORTED_BY_USER == flush_status) {
        LOGGER__INFO("Failed flushing input stream {} because stream was aborted", m_stream->to_string());
        return HAILO_SUCCESS;
    } else if (HAILO_STREAM_NOT_ACTIVATED == flush_status) {
        LOGGER__INFO("Failed flushing input stream {} because stream is not activated", m_stream->to_string());
        return HAILO_SUCCESS;
    } else if (HAILO_SUCCESS != flush_status) {
        LOGGER__ERROR("flush has failed in {} with status {}", name(), flush_status);
    }

    auto abort_status = execute_abort();
    CHECK(((abort_status == HAILO_SUCCESS) || (abort_status == HAILO_STREAM_NOT_ACTIVATED)), abort_status,
        "Failed to abort stream in {}", name());
    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_post_deactivate(bool should_clear_abort)
{
    if (should_clear_abort) {
        auto status = execute_clear_abort();
        CHECK(((status == HAILO_SUCCESS) || (status == HAILO_STREAM_NOT_ACTIVATED)), status,
            "Failed to clear abort stream in {}", name());
    }
    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_clear()
{
    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_flush()
{
    hailo_status status = m_got_flush_event->wait(m_stream->get_timeout());
    CHECK_SUCCESS(status);

    status = m_got_flush_event->reset();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status HwWriteElement::execute_abort()
{
    return m_stream->abort_impl();
}

hailo_status HwWriteElement::execute_clear_abort()
{
    return m_stream->clear_abort_impl();
}

hailo_status HwWriteElement::execute_wait_for_finish()
{
    return HAILO_SUCCESS;
}

std::string HwWriteElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << " | hw_frame_size: " << m_stream->get_info().hw_frame_size << ")";   

    return element_description.str();
}

Expected<std::shared_ptr<LastAsyncElement>> LastAsyncElement::create(const std::string &name,
    hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    std::shared_ptr<AsyncPipeline> async_pipeline, PipelineDirection pipeline_direction)
{
    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto last_async_elem_ptr = make_shared_nothrow<LastAsyncElement>(name,
        duration_collector.release(), std::move(pipeline_status), pipeline_direction, async_pipeline);
    CHECK_NOT_NULL_AS_EXPECTED(last_async_elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", last_async_elem_ptr->name());

    return last_async_elem_ptr;
}

Expected<std::shared_ptr<LastAsyncElement>> LastAsyncElement::create(const std::string &name,
    const ElementBuildParams &build_params, std::shared_ptr<AsyncPipeline> async_pipeline, PipelineDirection pipeline_direction)
{
    return LastAsyncElement::create(name, build_params.elem_stats_flags,
        build_params.pipeline_status, async_pipeline, pipeline_direction);
}

LastAsyncElement::LastAsyncElement(const std::string &name, DurationCollector &&duration_collector,
                               std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                               PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline):
    SinkElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline)
{}

Expected<PipelineBuffer> LastAsyncElement::run_pull(PipelineBuffer &&/*optional*/, const PipelinePad &/*source*/)
{
    return make_unexpected(HAILO_INVALID_OPERATION);
}

hailo_status LastAsyncElement::run_push(PipelineBuffer &&/*optional*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

void LastAsyncElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &/*sink*/)
{
    auto exec_done_cb = buffer.get_exec_done_cb();
    exec_done_cb(buffer.action_status());
}

std::string LastAsyncElement::description() const
{
    std::stringstream element_description;
    element_description << "(" << this->name() << ")";

    return element_description.str();
}

hailo_status LastAsyncElement::execute_activate()
{
    return HAILO_SUCCESS;
}

hailo_status LastAsyncElement::execute_wait_for_finish()
{
    return HAILO_SUCCESS;
}

hailo_status LastAsyncElement::enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name)
{
    (void)source_name;
    return m_sinks[0].prev()->element().enqueue_execution_buffer(mem_view, exec_done, m_sinks[0].prev()->name());
}

Expected<bool> LastAsyncElement::can_push_buffer_upstream(const uint32_t /*source_index*/)
{
    auto source_index = m_sinks[0].prev()->element().get_source_index_from_source_name(m_sinks[0].prev()->name());
    CHECK_EXPECTED(source_index);
    return m_sinks[0].prev()->element().can_push_buffer_upstream(*source_index);
}

hailo_status LastAsyncElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t /*source_index*/)
{
    auto source_index = m_sinks[0].prev()->element().get_source_index_from_source_name(m_sinks[0].prev()->name());
    CHECK_EXPECTED_AS_STATUS(source_index);
    return m_sinks[0].prev()->element().fill_buffer_pool(is_dma_able, num_of_buffers, *source_index);
}

Expected<bool> LastAsyncElement::can_push_buffer_upstream(const std::string &/*source_name*/)
{
    return m_sinks[0].prev()->element().can_push_buffer_upstream(m_sinks[0].prev()->name());
}

hailo_status LastAsyncElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &/*source_name*/)
{
    return m_sinks[0].prev()->element().fill_buffer_pool(is_dma_able, num_of_buffers, m_sinks[0].prev()->name());
}

Expected<std::shared_ptr<AsyncHwElement>> AsyncHwElement::create(const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos,
    std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
    hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, const std::string &name,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::shared_ptr<ConfiguredNetworkGroup> net_group,
    PipelineDirection pipeline_direction, bool is_last_copy_element, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    std::vector<BufferPoolPtr> output_streams_pools;
    for (const auto &stream_info_pair : named_stream_infos) {
        if (HAILO_D2H_STREAM == stream_info_pair.second.direction) {
            auto buffer_pool = BufferPool::create(stream_info_pair.second.hw_frame_size, buffer_pool_size, shutdown_event, elem_flags, vstream_flags,
                is_last_copy_element);
            CHECK_EXPECTED(buffer_pool);
            output_streams_pools.emplace_back(buffer_pool.release());
        }
    }

    auto duration_collector = DurationCollector::create(elem_flags);
    CHECK_EXPECTED(duration_collector);

    auto min_buffer_pool_size = net_group->get_min_buffer_pool_size();
    CHECK_EXPECTED(min_buffer_pool_size);

    auto elem_ptr = make_shared_nothrow<AsyncHwElement>(named_stream_infos, timeout, std::move(output_streams_pools), name,
        duration_collector.release(), std::move(pipeline_status), pipeline_direction, async_pipeline, net_group,
        min_buffer_pool_size.release());
    CHECK_AS_EXPECTED(nullptr != elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", elem_ptr->name());

    return elem_ptr;
}

AsyncHwElement::AsyncHwElement(const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos, std::chrono::milliseconds timeout,
    std::vector<BufferPoolPtr> &&output_streams_pools, const std::string &name, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline, std::shared_ptr<ConfiguredNetworkGroup> net_group,
    const size_t max_ongoing_transfers) :
        PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline),
        m_timeout(timeout),
        m_pools(std::move(output_streams_pools)),
        m_net_group(net_group),
        m_max_ongoing_transfers(max_ongoing_transfers)
{
    uint32_t sinks_count = 0;
    uint32_t sources_count = 0;
    for (const auto &stream_info_pair : named_stream_infos) {
        if (HAILO_D2H_STREAM == stream_info_pair.second.direction) {
            m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
            const auto &source_name = m_sources[sources_count++].name();
            m_source_name_to_stream_name[source_name] = stream_info_pair.first;

            m_source_name_to_index[source_name] = static_cast<uint32_t>(m_sources.size() - 1);
        } else {
            m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
            const auto &sink_name = m_sinks[sinks_count++].name();
            m_sink_name_to_stream_name[sink_name] = stream_info_pair.first;
            m_sink_name_to_index[sink_name] = static_cast<uint32_t>(m_sinks.size() - 1);
            m_sink_has_arrived[sink_name] = false;
        }
    }
}

bool AsyncHwElement::has_all_sinks_arrived()
{
    for (const auto &current_sink : m_sink_has_arrived) {
        if (!current_sink.second) {
            return false;
        }
    }
    return true;
}

// This func overides the regular dataflow of this element and calls all next elements run_push_async directly
// (normally, the run_push_async of the next elements will be called by the LL async read_done)
void AsyncHwElement::handle_error_in_hw_async_elem(hailo_status error_status)
{
    for (auto &name_output_stream_pair : m_source_name_to_index) {
        auto source_index = name_output_stream_pair.second;
        assert(source_index < m_pools.size());
        assert(source_index < m_sources.size());
        auto expected_buffer = m_pools[source_index]->acquire_buffer_ptr(m_timeout);
        if (HAILO_SUCCESS == expected_buffer.status()) {
            expected_buffer->get()->set_action_status(error_status);
            m_sources[source_index].next()->run_push_async(std::move(*expected_buffer.value()));
        } else {
            m_sources[source_index].next()->run_push_async(PipelineBuffer(error_status));
        }
    }

    for (const auto &sink : m_sinks) {
        m_sink_has_arrived[sink.name()] = false;
    }
    m_input_buffers.clear();

    return;
}

void AsyncHwElement::run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink)
{
    assert(contains(m_sink_name_to_stream_name, sink.name()));

    std::unique_lock<std::mutex> lock(m_mutex);
    m_sink_has_arrived[sink.name()] = true;
    m_input_buffers[sink.name()] = std::move(buffer);

    if (has_all_sinks_arrived()) {
        hailo_status all_buffers_status = HAILO_SUCCESS;
        for (auto &input_buffer : m_input_buffers) {
            if (HAILO_SUCCESS != input_buffer.second.action_status()) {
                all_buffers_status = input_buffer.second.action_status();
                break;  // error from one buffer is enough
            }
        }

        if (HAILO_SUCCESS != all_buffers_status) {
            handle_error_in_hw_async_elem(all_buffers_status);
            // Manual unlocking is done before notifying, to avoid waking up the waiting thread only to block again
            lock.unlock();
            m_cv.notify_all();
        } else {
            std::unordered_map<std::string, std::shared_ptr<PipelineBuffer>> source_name_to_output_buffer;
            for (auto &name_to_index_pair : m_source_name_to_index) {
                auto expected_buffer = m_pools[name_to_index_pair.second]->acquire_buffer_ptr(m_timeout);
                if (HAILO_SUCCESS != expected_buffer.status()) {
                    handle_non_recoverable_async_error(expected_buffer.status());
                    m_input_buffers.clear();
                    // Manual unlocking is done before notifying, to avoid waking up the waiting thread only to block again
                    lock.unlock();
                    m_cv.notify_all();
                    return;
                }
                source_name_to_output_buffer[name_to_index_pair.first] = expected_buffer.release();
            }

            NamedBuffersCallbacks named_buffers_callbacks;

            for (auto &input_buffer : m_input_buffers) {
                const auto &stream_name = m_sink_name_to_stream_name.at(input_buffer.first);
                named_buffers_callbacks.emplace(stream_name, std::make_pair(input_buffer.second.as_view(), input_buffer.second.get_exec_done_cb()));
            }

            for (auto &output_buffer : source_name_to_output_buffer) {
                const auto &stream_name = m_source_name_to_stream_name.at(output_buffer.first);
                named_buffers_callbacks.emplace(stream_name, std::make_pair(output_buffer.second->as_view(),
                    [this, buffer = output_buffer.second, source_name = output_buffer.first](hailo_status status){
                        buffer->set_action_status(status);
                        if (HAILO_SUCCESS == m_pipeline_status->load()) {
                            assert(contains(m_source_name_to_index, source_name));
                            // If pipeline_status is not success, someone already handled this error and no reason for this buffer to be pushed
                            assert(contains(m_source_name_to_index, source_name));
                            m_sources[m_source_name_to_index[source_name]].next()->run_push_async(std::move(*buffer));
                        }
                }));
            }

            auto done_cb = [](hailo_status){};
            auto status = m_net_group->wait_for_callbacks_to_maintain_below_threshold(m_max_ongoing_transfers);
            if (HAILO_SUCCESS != status ) {
                handle_non_recoverable_async_error(status);
            }

            status = m_net_group->infer_async(named_buffers_callbacks, done_cb);
            if (HAILO_SUCCESS != status ) {
                handle_non_recoverable_async_error(status);
            }

            for (const auto &curr_sink : m_sinks) {
                m_sink_has_arrived[curr_sink.name()] = false;
            }
            m_input_buffers.clear();

            // Manual unlocking is done before notifying, to avoid waking up the waiting thread only to block again
            lock.unlock();
            m_cv.notify_all();
        }
    } else {
        bool done = m_cv.wait_for(lock, m_timeout, [&](){
            if (m_pipeline_status->load() != HAILO_SUCCESS) {
                return true; // so we can exit this flow
            }
            return !m_sink_has_arrived[sink.name()];
        });

        if (!done) {
            LOGGER__ERROR("Waiting for other threads in AsyncHwElement {} has reached a timeout (timeout={}ms)", name(), m_timeout.count());
            handle_non_recoverable_async_error(HAILO_TIMEOUT);
        }

        if (m_pipeline_status->load() == HAILO_STREAM_ABORTED_BY_USER) {
            lock.unlock();
            m_cv.notify_all();
        }
    }
}

hailo_status AsyncHwElement::run_push(PipelineBuffer &&/*optional*/, const PipelinePad &/*sink*/)
{
    return HAILO_INVALID_OPERATION;
}

hailo_status AsyncHwElement::enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name)
{
    CHECK(contains(m_source_name_to_index, source_name), HAILO_INTERNAL_FAILURE);
    auto source_index = m_source_name_to_index[source_name];

    auto status = m_pools[source_index]->enqueue_buffer(mem_view, exec_done);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status AsyncHwElement::execute_dequeue_user_buffers(hailo_status error_status)
{
    for (auto pool : m_pools) {
        auto status = empty_buffer_pool(pool, error_status, m_timeout);
        CHECK_SUCCESS(status);
    }
    return PipelineElement::execute_dequeue_user_buffers(error_status);
}

Expected<bool> AsyncHwElement::can_push_buffer_upstream(const uint32_t source_index)
{
    CHECK_AS_EXPECTED(source_index < m_pools.size(), HAILO_NOT_FOUND);
    return !m_pools[source_index]->is_full();
}

hailo_status AsyncHwElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const uint32_t source_index)
{
    CHECK(source_index < m_pools.size(), HAILO_NOT_FOUND);
    CHECK_SUCCESS(m_pools[source_index]->allocate_buffers(is_dma_able, num_of_buffers));

    return HAILO_SUCCESS;
}

Expected<bool> AsyncHwElement::can_push_buffer_upstream(const std::string &source_name)
{
    auto source_index = get_source_index_from_source_name(source_name);
    CHECK_EXPECTED(source_index);
    return can_push_buffer_upstream(*source_index);
}

hailo_status AsyncHwElement::fill_buffer_pool(bool is_dma_able, size_t num_of_buffers, const std::string &source_name)
{
    auto source_index = get_source_index_from_source_name(source_name);
    CHECK_EXPECTED_AS_STATUS(source_index);
    return fill_buffer_pool(is_dma_able, num_of_buffers, *source_index);
}

Expected<uint32_t> AsyncHwElement::get_source_index_from_output_stream_name(const std::string &output_stream_name)
{
    for (const auto &name_pair : m_source_name_to_stream_name) {
        if (name_pair.second == output_stream_name) {
            assert(contains(m_source_name_to_index, name_pair.first));
            uint32_t ret_val = m_source_name_to_index.at(name_pair.first);
            return ret_val;
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

Expected<uint32_t> AsyncHwElement::get_source_index_from_source_name(const std::string &source_name)
{
    CHECK_AS_EXPECTED(contains(m_source_name_to_index, source_name), HAILO_NOT_FOUND, "couldnt find src '{}'", source_name);
    auto ret_val = m_source_name_to_index.at(source_name);
    return ret_val;
}

Expected<uint32_t> AsyncHwElement::get_sink_index_from_input_stream_name(const std::string &input_stream_name)
{
    for (const auto &name_pair : m_sink_name_to_stream_name) {
        if (name_pair.second == input_stream_name) {
            return Expected<uint32_t>(m_sink_name_to_index.at(name_pair.first));
        }
    }
    return make_unexpected(HAILO_INVALID_ARGUMENT);
}

Expected<PipelineBuffer> AsyncHwElement::run_pull(PipelineBuffer &&/*optional*/, const PipelinePad &/*source*/)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

std::vector<PipelinePad*> AsyncHwElement::execution_pads()
{
    std::vector<PipelinePad*> result;
    result.reserve(m_sources.size());
    for (auto& pad : m_sources) {
        result.push_back(pad.next());
    }
    return result;
}

hailo_status AsyncHwElement::execute_terminate(hailo_status error_status)
{
    if (m_is_terminated) {
        return HAILO_SUCCESS;
    }

    if (!m_is_terminating_element) {
        {
            // There is a case where the other thread is halted (via context switch) before the wait_for() function,
            // then we call notify_all() here, and then the wait_for() is called - resulting in a timeout.
            // notify_all() only works on threads which are already waiting, so that's why we acquire the lock here.
            std::unique_lock<std::mutex> lock(m_mutex);
        }
        m_cv.notify_all();
    }

    // Checking success of shutdown is best effort (terminate should be called even if shutdown fails)
    auto shutdown_status = m_net_group->shutdown();
    auto wait_for_callbacks_finish_status = m_net_group->wait_for_callbacks_finish();
    auto terminate_status = PipelineElement::execute_terminate(error_status);
    CHECK_SUCCESS(shutdown_status);
    CHECK_SUCCESS(wait_for_callbacks_finish_status);
    CHECK_SUCCESS(terminate_status);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<CopyBufferElement>> CopyBufferElement::create(const std::string &name,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    auto duration_collector = DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE);
    CHECK_EXPECTED(duration_collector);
    auto elem_ptr = make_shared_nothrow<CopyBufferElement>(name, duration_collector.release(), std::move(pipeline_status),
        timeout, pipeline_direction, async_pipeline);
    CHECK_AS_EXPECTED(nullptr != elem_ptr, HAILO_OUT_OF_HOST_MEMORY);

    LOGGER__INFO("Created {}", elem_ptr->name());

    return elem_ptr;
}

CopyBufferElement::CopyBufferElement(const std::string &name, DurationCollector &&duration_collector, 
                                     std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::chrono::milliseconds timeout,
                                     PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    FilterElement(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, nullptr, timeout, async_pipeline)
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

Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> VStreamsBuilder::create_vstreams(
    ConfiguredNetworkGroup &net_group, bool /*unused*/, hailo_format_type_t format_type,
    const std::string &network_name)
{
    const auto params = HailoRTDefaults::get_vstreams_params({}, format_type);
    return create_vstreams(net_group, params, network_name);
}

Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> VStreamsBuilder::create_vstreams(
    ConfiguredNetworkGroup &net_group, const hailo_vstream_params_t &vstreams_params,
    const std::string &network_name)
{
    std::map<std::string, hailo_vstream_params_t> vstreams_params_by_input_stream_name;
    auto input_vstream_params = net_group.make_input_vstream_params(true, HAILO_FORMAT_TYPE_AUTO, 
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, network_name);
    CHECK_EXPECTED(input_vstream_params);

    for (auto params_pair : input_vstream_params.release()) {
        vstreams_params_by_input_stream_name.emplace(std::make_pair(params_pair.first, vstreams_params));
    }

    auto expected_all_inputs = create_input_vstreams(net_group, vstreams_params_by_input_stream_name);
    CHECK_EXPECTED(expected_all_inputs);

    std::map<std::string, hailo_vstream_params_t> vstreams_params_by_output_stream_name;
    auto output_vstream_params = net_group.make_output_vstream_params(true, HAILO_FORMAT_TYPE_AUTO, 
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE, network_name);
    CHECK_EXPECTED(output_vstream_params);

    for (auto params_pair : output_vstream_params.release()) {
        vstreams_params_by_output_stream_name.emplace(std::make_pair(params_pair.first, vstreams_params));
    }

    auto expected_all_outputs = create_output_vstreams(net_group, vstreams_params_by_output_stream_name);
    CHECK_EXPECTED(expected_all_outputs);

    return std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>(
            expected_all_inputs.release(), expected_all_outputs.release());
}

static hailo_vstream_params_t expand_vstream_params_autos(const hailo_stream_info_t &stream_info,
    const hailo_vstream_params_t &vstream_params)
{
    auto local_vstream_params = vstream_params;
    local_vstream_params.user_buffer_format = HailoRTDefaults::expand_auto_format(vstream_params.user_buffer_format,
        stream_info.format);
    return local_vstream_params;
}

Expected<std::vector<InputVStream>> VStreamsBuilder::create_input_vstreams(ConfiguredNetworkGroup &net_group,
    const std::map<std::string, hailo_vstream_params_t> &inputs_params)
{
    return net_group.create_input_vstreams(inputs_params);
}

Expected<std::vector<OutputVStream>> VStreamsBuilder::create_output_vstreams(ConfiguredNetworkGroup &net_group,
    const std::map<std::string, hailo_vstream_params_t> &outputs_params)
{
    return net_group.create_output_vstreams(outputs_params);
}

Expected<std::vector<InputVStream>> VStreamsBuilderUtils::create_inputs(
    std::vector<std::shared_ptr<InputStreamBase>> input_streams, const hailo_vstream_info_t &vstream_info,
    const hailo_vstream_params_t &vstream_params)
{
    CHECK_AS_EXPECTED(!input_streams.empty(), HAILO_INVALID_ARGUMENT, "input streams can't be empty");
    // if input streams has more than 1 value, it will be handled by handle_pix_buffer_splitter_flow. For all other purposes,
    // assuming there is only 1 stream is valid
    std::shared_ptr<InputStreamBase> input_stream = input_streams.front();

    // TODO (HRT-4522): Support this measurement
    CHECK_AS_EXPECTED(!(vstream_params.vstream_stats_flags & HAILO_VSTREAM_STATS_MEASURE_FPS), HAILO_NOT_IMPLEMENTED,
        "Pipeline FPS statistics measurement is not implemented");

    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<InputVStream> vstreams;

    EventPtr core_op_activated_event = nullptr;
    if (!input_stream->is_scheduled()) {
        core_op_activated_event = input_stream->get_core_op_activated_event();
    }

    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstream_params);
    CHECK_EXPECTED(pipeline_latency_accumulator);

    auto user_timeout = std::chrono::milliseconds(vstream_params.timeout_ms);

    if (input_streams.size() > 1) {
        CHECK_SUCCESS_AS_EXPECTED(handle_pix_buffer_splitter_flow(input_streams, vstream_info,
            std::move(elements), vstreams, vstream_params, shutdown_event, pipeline_status, core_op_activated_event,
            pipeline_latency_accumulator.value()));
    } else {
        auto hw_write_elem = HwWriteElement::create(input_stream,
            PipelineObject::create_element_name("HwWriteElement", input_stream->name(), input_stream->get_info().index),
            vstream_params.pipeline_elements_stats_flags, pipeline_status);
        CHECK_EXPECTED(hw_write_elem);
        elements.insert(elements.begin(), hw_write_elem.value());

        auto should_transform = InputTransformContext::is_transformation_required(input_stream->get_info().shape,
            vstream_params.user_buffer_format, input_stream->get_info().hw_shape, input_stream->get_info().format,
            input_stream->get_quant_infos());
        CHECK_EXPECTED(should_transform);

        if (should_transform.value()) {
            std::shared_ptr<SinkElement> elem_after_post_infer = hw_write_elem.value();
            auto queue_elem = PushQueueElement::create(
                PipelineObject::create_element_name("PushQueueElement", input_stream->get_info().name, input_stream->get_info().index),
                vstream_params, shutdown_event, pipeline_status);
            CHECK_EXPECTED(queue_elem);
            elements.insert(elements.begin(), queue_elem.value());
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(queue_elem.value(), hw_write_elem.value()));

            auto pre_infer_elem = PreInferElement::create(input_stream->get_info().shape, vstream_params.user_buffer_format,
                input_stream->get_info().hw_shape, input_stream->get_info().format, input_stream->get_quant_infos(),
                PipelineObject::create_element_name("PreInferElement", input_stream->get_info().name, input_stream->get_info().index),
                vstream_params, shutdown_event, pipeline_status);
            CHECK_EXPECTED(pre_infer_elem);
            elements.insert(elements.begin(), pre_infer_elem.value());
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_infer_elem.value(), queue_elem.value()));

            input_stream->set_timeout(user_timeout);
            auto vstream = InputVStream::create(vstream_info, input_stream->get_quant_infos(), vstream_params, pre_infer_elem.release(), hw_write_elem.release(), std::move(elements),
                std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED(vstream);
            vstreams.emplace_back(vstream.release());
        } else {
            input_stream->set_timeout(user_timeout);
            auto vstream = InputVStream::create(vstream_info, input_stream->get_quant_infos(), vstream_params, hw_write_elem.value(), hw_write_elem.value(), std::move(elements),
                std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED(vstream);
            vstreams.emplace_back(vstream.release());
        }
    }

    for (const auto &vstream : vstreams) {
       LOGGER__INFO("{}", vstream.get_pipeline_description());
    }

    return vstreams;
}

Expected<std::vector<OutputVStream>> VStreamsBuilderUtils::create_outputs(std::shared_ptr<OutputStreamBase> output_stream,
    NameToVStreamParamsMap &vstreams_params_map, const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos)
{
    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<OutputVStream> vstreams;

    if (0 != (HAILO_FORMAT_FLAGS_HOST_ARGMAX & output_stream->get_info().format.flags))
    {
        LOGGER__WARNING("Using legacy implementation of Argmax in host. Please re-compile your model with latest DFC version");
    }

    EventPtr core_op_activated_event = nullptr;
    if (!output_stream->is_scheduled()) {
        core_op_activated_event = output_stream->get_core_op_activated_event();
    }

    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    assert(!vstreams_params_map.empty());

    // Note: In case of multiple values in vstreams_params_map (e.g. in the case of demux), we'll set the
    //       pipeline_elements_stats_flags for the hw_read_element as bitwise or of all the flags.
    hailo_pipeline_elem_stats_flags_t hw_read_element_stats_flags = HAILO_PIPELINE_ELEM_STATS_NONE;
    hailo_vstream_stats_flags_t hw_read_stream_stats_flags = HAILO_VSTREAM_STATS_NONE;
    size_t buffer_pool_size = 0;
    for (const auto &elem_name_params : vstreams_params_map) {
        hw_read_element_stats_flags |= elem_name_params.second.pipeline_elements_stats_flags;
        hw_read_stream_stats_flags |= elem_name_params.second.vstream_stats_flags;
        buffer_pool_size += elem_name_params.second.queue_size;
    }

    // TODO (HRT-4522): Support this measurement
    CHECK_AS_EXPECTED(!(hw_read_stream_stats_flags & HAILO_VSTREAM_STATS_MEASURE_FPS), HAILO_NOT_IMPLEMENTED,
        "Pipeline FPS statistics measurement is not implemented");

    auto hw_read_element = add_hw_read_element(output_stream, pipeline_status, elements, "HwReadElement", shutdown_event,
        buffer_pool_size, hw_read_element_stats_flags, hw_read_stream_stats_flags);
    CHECK_EXPECTED(hw_read_element);

    if (output_stream->get_info().is_mux) {
        hailo_status status = add_demux(output_stream, vstreams_params_map, std::move(elements), vstreams, hw_read_element.value(),
            shutdown_event, pipeline_status, output_vstream_infos);
        CHECK_SUCCESS_AS_EXPECTED(status);
    } else {
        auto vstream_info = output_vstream_infos.find(output_stream->name());
        CHECK_AS_EXPECTED(vstream_info != output_vstream_infos.end(), HAILO_NOT_FOUND,
            "Failed to find vstream info of {}", output_stream->name());
        assert(1 == vstreams_params_map.size());
        auto vstream_params = expand_vstream_params_autos(output_stream->get_info(), vstreams_params_map.begin()->second);

        auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstream_params);
        CHECK_EXPECTED(pipeline_latency_accumulator);

        auto should_transform = OutputTransformContext::is_transformation_required(output_stream->get_info().hw_shape, 
            output_stream->get_info().format, output_stream->get_info().shape, 
            vstream_params.user_buffer_format, output_stream->get_quant_infos());
        CHECK_EXPECTED(should_transform);

        if (should_transform.value()) {
            auto hw_read_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQueueElement_hw_read",
                shutdown_event, vstream_params);
            CHECK_EXPECTED(hw_read_queue_element);
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_element.value(), hw_read_queue_element.value()));

            auto post_infer_element = add_post_infer_element(output_stream, pipeline_status, elements,
                "PostInferElement", vstream_params, shutdown_event);
            CHECK_EXPECTED(post_infer_element);
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_queue_element.value(), post_infer_element.value()));
            auto user_buffer_queue_element = add_user_buffer_queue_element(output_stream, pipeline_status, elements,
                "UserBufferQueueElement", shutdown_event, vstream_params);
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(post_infer_element.value(), user_buffer_queue_element.value()));
            output_stream->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
            hw_read_queue_element->get()->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
            auto vstream = OutputVStream::create(vstream_info->second, output_stream->get_quant_infos(), vstream_params, user_buffer_queue_element.release(), std::move(elements),
                std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED(vstream);
            vstreams.emplace_back(vstream.release());
        } else {
            output_stream->set_timeout(std::chrono::milliseconds(vstream_params.timeout_ms));
            auto vstream = OutputVStream::create(vstream_info->second, output_stream->get_quant_infos(), vstream_params, hw_read_element.release(), std::move(elements),
                std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED(vstream);
            vstreams.emplace_back(vstream.release());
        }
    }

    for (const auto &vstream : vstreams) {
        LOGGER__INFO("{}", vstream.get_pipeline_description());
    }

    return vstreams;
}

Expected<std::vector<OutputVStream>> VStreamsBuilderUtils::create_output_post_process_iou(std::shared_ptr<OutputStreamBase> output_stream,
    hailo_vstream_params_t vstream_params, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata)
{
    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<OutputVStream> vstreams;

    EventPtr core_op_activated_event = nullptr;
    if (!output_stream->is_scheduled()) {
        core_op_activated_event = output_stream->get_core_op_activated_event();
    }

    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_AS_EXPECTED(shutdown_event_exp, HAILO_OUT_OF_HOST_MEMORY);
    auto shutdown_event = shutdown_event_exp.release();

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    vstream_params.user_buffer_format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(vstream_params.user_buffer_format,
        iou_op_metadata->type());

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstream_params);
    CHECK_EXPECTED(pipeline_latency_accumulator);

    auto hw_read_element = add_hw_read_element(output_stream, pipeline_status, elements, "HwReadElement", shutdown_event,
        vstream_params.queue_size, vstream_params.pipeline_elements_stats_flags, vstream_params.vstream_stats_flags);
    CHECK_EXPECTED(hw_read_element);

    auto hw_read_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQueueElement_hw_read",
        shutdown_event, vstream_params);
    CHECK_EXPECTED(hw_read_queue_element);
    hw_read_queue_element->get()->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_element.value(), hw_read_queue_element.value()));

    auto post_infer_element = add_post_infer_element(output_stream, pipeline_status, elements,
        "PostInferElement", vstream_params, shutdown_event);
    CHECK_EXPECTED(post_infer_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_queue_element.value(), post_infer_element.value()));

    auto pre_nms_convert_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQueueElement_pre_nms_convert",
        shutdown_event, vstream_params);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(post_infer_element.value(), pre_nms_convert_queue_element.value()));

    auto nms_to_detections_element = add_nms_to_detections_convert_element(output_stream, pipeline_status, elements, "NmsFormatToDetectionsElement",
        vstream_params, iou_op_metadata, vstream_params.queue_size, std::chrono::milliseconds(HAILO_INFINITE), vstream_params.vstream_stats_flags, shutdown_event);
    CHECK_EXPECTED(nms_to_detections_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_nms_convert_queue_element.value(), nms_to_detections_element.value()));

    auto pre_remove_overlapping_bboxes_element_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQueueElement_pre_bboxes_removing",
        shutdown_event, vstream_params);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(nms_to_detections_element.value(), pre_remove_overlapping_bboxes_element_queue_element.value()));

    auto remove_overlapping_bboxes_element = add_remove_overlapping_bboxes_element(output_stream, pipeline_status, elements, "RemoveOverlappingBboxesElement",
        vstream_params, iou_op_metadata, vstream_params.queue_size, std::chrono::milliseconds(HAILO_INFINITE), vstream_params.vstream_stats_flags, shutdown_event);
    CHECK_EXPECTED(remove_overlapping_bboxes_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_remove_overlapping_bboxes_element_queue_element.value(), remove_overlapping_bboxes_element.value()));

    auto pre_fill_nms_format_element_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQueueElement_pre_fill_nms_format",
        shutdown_event, vstream_params);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(remove_overlapping_bboxes_element.value(), pre_fill_nms_format_element_queue_element.value()));

    auto fill_nms_format_element = add_fill_nms_format_element(output_stream, pipeline_status, elements, "FillNmsFormatElement",
        vstream_params, iou_op_metadata, vstream_params.queue_size, std::chrono::milliseconds(HAILO_INFINITE), vstream_params.vstream_stats_flags, shutdown_event);
    CHECK_EXPECTED(fill_nms_format_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_fill_nms_format_element_queue_element.value(), fill_nms_format_element.value()));

    auto user_buffer_queue_element = add_user_buffer_queue_element(output_stream, pipeline_status, elements,
        "UserBufferQueueElement", shutdown_event, vstream_params);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(fill_nms_format_element.value(), user_buffer_queue_element.value()));
    output_stream->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));

    auto output_vstream_info = iou_op_metadata->get_output_vstream_info();
    CHECK_EXPECTED(output_vstream_info);

    auto vstream = OutputVStream::create(output_vstream_info.value(), output_stream->get_quant_infos(), vstream_params, user_buffer_queue_element.release(), std::move(elements),
        std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
    CHECK_EXPECTED(vstream);
    vstreams.emplace_back(vstream.release());

    for (const auto &curr_vstream : vstreams) {
        LOGGER__INFO("{}", curr_vstream.get_pipeline_description());
    }

    return vstreams;
}

Expected<std::vector<OutputVStream>> VStreamsBuilderUtils::create_output_post_process_softmax(std::shared_ptr<OutputStreamBase> output_stream,
    const NameToVStreamParamsMap &vstreams_params_map, const hailo_vstream_info_t &output_vstream_info,
    const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata)
{
    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<OutputVStream> vstreams;

    EventPtr core_op_activated_event = nullptr;
    if (!output_stream->is_scheduled()) {
        core_op_activated_event = output_stream->get_core_op_activated_event();
    }

    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    assert(!vstreams_params_map.empty());

    // Note: In case of multiple values in vstreams_params_map (e.g. in the case of demux), we'll set the
    //       pipeline_elements_stats_flags for the hw_read_element as bitwise or of all the flags.
    hailo_pipeline_elem_stats_flags_t hw_read_element_stats_flags = HAILO_PIPELINE_ELEM_STATS_NONE;
    hailo_vstream_stats_flags_t hw_read_stream_stats_flags = HAILO_VSTREAM_STATS_NONE;
    size_t buffer_pool_size = 0;
    for (const auto &elem_name_params : vstreams_params_map) {
        hw_read_element_stats_flags |= elem_name_params.second.pipeline_elements_stats_flags;
        hw_read_stream_stats_flags |= elem_name_params.second.vstream_stats_flags;
        buffer_pool_size += elem_name_params.second.queue_size;
    }

    // TODO (HRT-4522): Support this measurement
    CHECK_AS_EXPECTED(!(hw_read_stream_stats_flags & HAILO_VSTREAM_STATS_MEASURE_FPS), HAILO_NOT_IMPLEMENTED,
        "Pipeline FPS statistics measurement is not implemented");

    assert(1 == vstreams_params_map.size());
    auto op_input_format = softmax_op_metadata->inputs_metadata().begin()->second.format;
    auto vstream_params = vstreams_params_map.begin()->second;
    vstream_params.user_buffer_format = net_flow::SoftmaxOpMetadata::expand_output_format_autos(vstream_params.user_buffer_format, op_input_format);

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstream_params);
    CHECK_EXPECTED(pipeline_latency_accumulator);

    auto hw_read_element = add_hw_read_element(output_stream, pipeline_status, elements, "HwReadElement", shutdown_event,
        buffer_pool_size, hw_read_element_stats_flags, hw_read_stream_stats_flags);
    CHECK_EXPECTED(hw_read_element);

    auto hw_read_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQueueElement_hw_read",
        shutdown_event, vstream_params);
    CHECK_EXPECTED(hw_read_queue_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_element.value(), hw_read_queue_element.value()));

    auto post_infer_element = add_post_infer_element(output_stream, pipeline_status, elements,
        "PostInferElement", vstream_params, shutdown_event);
    CHECK_EXPECTED(post_infer_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_queue_element.value(), post_infer_element.value()));

    auto pre_softmax_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQueueElement_pre_softmax",
        shutdown_event, vstream_params);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(post_infer_element.value(), pre_softmax_queue_element.value()));

    auto softmax_element = add_softmax_element(output_stream, pipeline_status, elements, "SoftmaxPostProcessElement",
        vstream_params, softmax_op_metadata, buffer_pool_size, std::chrono::milliseconds(HAILO_INFINITE), hw_read_stream_stats_flags, shutdown_event);
    CHECK_EXPECTED(softmax_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_softmax_queue_element.value(), softmax_element.value()));
    auto user_buffer_queue_element = add_user_buffer_queue_element(output_stream, pipeline_status, elements,
        "UserBufferQueueElement", shutdown_event, vstream_params);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(softmax_element.value(), user_buffer_queue_element.value()));
    output_stream->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
    hw_read_queue_element->get()->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));

    auto vstream = OutputVStream::create(output_vstream_info, output_stream->get_quant_infos(), vstream_params, user_buffer_queue_element.release(), std::move(elements),
        std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
    CHECK_EXPECTED(vstream);
    vstreams.emplace_back(vstream.release());

    for (const auto &curr_vstream : vstreams) {
        LOGGER__INFO("{}", curr_vstream.get_pipeline_description());
    }

    return vstreams;
}

InputVStream VStreamsBuilderUtils::create_input(std::shared_ptr<InputVStreamInternal> input_vstream)
{
    return InputVStream(std::move(input_vstream));
}

OutputVStream VStreamsBuilderUtils::create_output(std::shared_ptr<OutputVStreamInternal> output_vstream)
{
    return OutputVStream(std::move(output_vstream));
}

static bool are_formats_equal(const hailo_format_t &format1, const hailo_format_t &format2) {
    return ((format1.order == format2.order) && (format1.flags == format2.flags) && (format1.type == format2.type));
}

Expected<std::vector<OutputVStream>> VStreamsBuilderUtils::create_output_vstreams_from_streams(const OutputStreamWithParamsVector &all_output_streams,
    OutputStreamPtrVector &output_streams, const hailo_vstream_params_t &vstream_params,
    const std::unordered_map<std::string, net_flow::PostProcessOpMetadataPtr> &post_process_ops_metadata,
    const std::unordered_map<stream_name_t, op_name_t> &op_inputs_to_op_name, const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos_map)
{
    auto first_stream_info = output_streams[0]->get_info();
    if ((HailoRTCommon::is_nms(first_stream_info)) && (first_stream_info.nms_info.is_defused)) {
        // Case defuse NMS
        return create_output_nms(output_streams, vstream_params, output_vstream_infos_map);
    } else if (contains(op_inputs_to_op_name, static_cast<stream_name_t>(first_stream_info.name))) {
        // Case post-process on host
        auto &op_name = op_inputs_to_op_name.at(first_stream_info.name);
        auto &op_metadata = post_process_ops_metadata.at(op_name);
        switch (op_metadata->type()) {
        case net_flow::OperationType::YOLOX:
        case net_flow::OperationType::YOLOV8:
        case net_flow::OperationType::SSD:
        case net_flow::OperationType::YOLOV5:
        case net_flow::OperationType::YOLOV5SEG:
        case net_flow::OperationType::IOU:
        {
            assert(1 <= op_metadata->outputs_metadata().size());
            auto updated_outputs_metadata = op_metadata->outputs_metadata();
            updated_outputs_metadata.begin()->second.format =
                net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(vstream_params.user_buffer_format, op_metadata->type());
            op_metadata->set_outputs_metadata(updated_outputs_metadata);
            CHECK_SUCCESS_AS_EXPECTED(op_metadata->validate_format_info());

            std::shared_ptr<hailort::net_flow::Op> op;
            switch (op_metadata->type()) {
            case (net_flow::OperationType::YOLOX):
            {
                auto metadata = std::dynamic_pointer_cast<net_flow::YoloxOpMetadata>(op_metadata);
                assert(nullptr != metadata);
                auto op_expected = net_flow::YOLOXPostProcessOp::create(metadata);
                CHECK_EXPECTED(op_expected);
                op = op_expected.release();
                break;
            }
            case (net_flow::OperationType::YOLOV8):
            {
                auto metadata = std::dynamic_pointer_cast<net_flow::Yolov8OpMetadata>(op_metadata);
                assert(nullptr != metadata);
                auto op_expected = net_flow::YOLOV8PostProcessOp::create(metadata);
                CHECK_EXPECTED(op_expected);
                op = op_expected.release();
                break;
            }
            case (net_flow::OperationType::YOLOV5):
            {
                auto metadata = std::dynamic_pointer_cast<net_flow::Yolov5OpMetadata>(op_metadata);
                assert(nullptr != metadata);
                auto op_expected = net_flow::YOLOv5PostProcessOp::create(metadata);
                CHECK_EXPECTED(op_expected);
                op = op_expected.release();
                break;
            }
            case (net_flow::OperationType::YOLOV5SEG):
            {
                auto metadata = std::dynamic_pointer_cast<net_flow::Yolov5SegOpMetadata>(op_metadata);
                assert(nullptr != metadata);
                auto op_expected = net_flow::Yolov5SegPostProcess::create(metadata);
                CHECK_EXPECTED(op_expected);
                op = op_expected.release();
                break;
            }
            case (net_flow::OperationType::SSD):
            {
                auto metadata = std::dynamic_pointer_cast<net_flow::SSDOpMetadata>(op_metadata);
                assert(nullptr != metadata);
                auto op_expected = net_flow::SSDPostProcessOp::create(metadata);
                CHECK_EXPECTED(op_expected);
                op = op_expected.release();
                break;
            }
            case (net_flow::OperationType::IOU):
            {
                return create_output_post_process_iou(output_streams[0], vstream_params, op_metadata);
            }
            default:
                break;
            }

            return create_output_post_process_nms(output_streams, vstream_params, output_vstream_infos_map, op);
        }

        case net_flow::OperationType::ARGMAX:
        {
            assert(output_streams.size() == 1);
            NameToVStreamParamsMap name_to_vstream_params_map;
            for (auto &output_stream : all_output_streams) {
                if (output_stream.first->get_info().name == output_streams[0]->get_info().name) {
                    for (auto &vstream : output_stream.second) {
                        name_to_vstream_params_map.insert(vstream);
                    }
                }
            }
            auto output_vstream_info = op_metadata->get_output_vstream_info();
            CHECK_EXPECTED(output_vstream_info);
            return create_output_post_process_argmax(output_streams[0], name_to_vstream_params_map, output_vstream_info.release(), op_metadata);
        }

        case net_flow::OperationType::SOFTMAX:
        {
            assert(output_streams.size() == 1);
            NameToVStreamParamsMap name_to_vstream_params_map;
            for (auto &output_stream : all_output_streams) {
                if (output_stream.first->get_info().name == output_streams[0]->get_info().name) {
                    for (auto &vstream : output_stream.second) {
                        name_to_vstream_params_map.insert(vstream);
                    }
                }
            }
            auto output_vstream_info = op_metadata->get_output_vstream_info();
            CHECK_EXPECTED(output_vstream_info);
            return create_output_post_process_softmax(output_streams[0], name_to_vstream_params_map, output_vstream_info.release(), op_metadata);
            }

        default:
            LOGGER__ERROR("op type {} of op {} is not in any of the supported post process OP types", net_flow::OpMetadata::get_operation_type_str(op_metadata->type()), op_name);
            return make_unexpected(HAILO_INVALID_OPERATION);
        }
    } else {
        // All other cases
        assert(output_streams.size() == 1);
        NameToVStreamParamsMap name_to_vstream_params_map;
        for (auto &output_stream : all_output_streams) {
            if (output_stream.first->get_info().name == output_streams[0]->get_info().name) {
                for (auto &vstream : output_stream.second) {
                    name_to_vstream_params_map.insert(vstream);
                }
            }
        }
        return create_outputs(output_streams[0], name_to_vstream_params_map, output_vstream_infos_map);
    }
}

Expected<std::vector<OutputVStream>> VStreamsBuilderUtils::create_output_nms(OutputStreamPtrVector &output_streams,
    hailo_vstream_params_t vstreams_params,
    const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos)
{
    for (const auto &out_stream : output_streams) {
        CHECK_AS_EXPECTED(are_formats_equal(output_streams[0]->get_info().format, out_stream->get_info().format),
            HAILO_INVALID_ARGUMENT, "All nms streams of the same virtual output must have the same format");
    }

    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<OutputVStream> vstreams;

    hailo_status status = add_nms_fuse(output_streams, vstreams_params, elements, vstreams, shutdown_event,
        pipeline_status, output_vstream_infos);
    CHECK_SUCCESS_AS_EXPECTED(status);

    for (const auto &vstream : vstreams) {
        LOGGER__INFO("{}", vstream.get_pipeline_description());
    }

    return vstreams;
}

Expected<std::vector<OutputVStream>> VStreamsBuilderUtils::create_output_post_process_nms(OutputStreamPtrVector &output_streams,
    hailo_vstream_params_t vstreams_params,
    const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos,
    const std::shared_ptr<hailort::net_flow::Op> &nms_op)
{
    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<OutputVStream> vstreams;

    hailo_status status = add_nms_post_process(output_streams, vstreams_params, elements, vstreams, shutdown_event,
        pipeline_status, output_vstream_infos, nms_op);
    CHECK_SUCCESS_AS_EXPECTED(status);

    for (const auto &vstream : vstreams) {
        LOGGER__INFO("{}", vstream.get_pipeline_description());
    }

    return vstreams;
}

Expected<std::shared_ptr<HwReadElement>> VStreamsBuilderUtils::add_hw_read_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, EventPtr &shutdown_event, size_t buffer_pool_size,
        const hailo_pipeline_elem_stats_flags_t &hw_read_element_stats_flags, const hailo_vstream_stats_flags_t &hw_read_stream_stats_flags)
{
    auto hw_read_elem = HwReadElement::create(output_stream,
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        HAILO_INFINITE_TIMEOUT, buffer_pool_size, hw_read_element_stats_flags, hw_read_stream_stats_flags, shutdown_event, pipeline_status);
    CHECK_EXPECTED(hw_read_elem);
    elements.push_back(hw_read_elem.value());
    return hw_read_elem;
}

Expected<std::shared_ptr<PullQueueElement>> VStreamsBuilderUtils::add_pull_queue_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, EventPtr &shutdown_event, const hailo_vstream_params_t &vstream_params)
{
    auto pull_queue_elem = PullQueueElement::create(
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params, shutdown_event, pipeline_status);
    CHECK_EXPECTED(pull_queue_elem);
    elements.push_back(pull_queue_elem.value());
    return pull_queue_elem;
}

Expected<std::shared_ptr<ArgmaxPostProcessElement>> VStreamsBuilderUtils::add_argmax_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata,
    size_t buffer_pool_size, std::chrono::milliseconds timeout, const hailo_vstream_stats_flags_t &vstream_flags, EventPtr &shutdown_event)
{
    // Updating metadata according to user request. TODO: HRT-9737
    auto updated_outputs_metadata = argmax_op_metadata.get()->outputs_metadata();
    updated_outputs_metadata.begin()->second.format = vstream_params.user_buffer_format;
    auto metadata = std::dynamic_pointer_cast<net_flow::ArgmaxOpMetadata>(argmax_op_metadata);
    assert(nullptr != metadata);
    metadata->set_outputs_metadata(updated_outputs_metadata);
    CHECK_SUCCESS_AS_EXPECTED(metadata->validate_format_info());
    // Updating metadata according to use request. TODO: HRT-9737 - End

    auto op_expected = net_flow::ArgmaxPostProcessOp::create(metadata);
    CHECK_EXPECTED(op_expected);
    auto argmax_op = op_expected.release();

    auto argmax_element = ArgmaxPostProcessElement::create(argmax_op,
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params.pipeline_elements_stats_flags, pipeline_status, buffer_pool_size, timeout, vstream_flags, shutdown_event);
    CHECK_EXPECTED(argmax_element);
    elements.push_back(argmax_element.value());
    return argmax_element;
}

Expected<std::shared_ptr<SoftmaxPostProcessElement>> VStreamsBuilderUtils::add_softmax_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata,
    size_t buffer_pool_size, std::chrono::milliseconds timeout, const hailo_vstream_stats_flags_t &vstream_flags, EventPtr &shutdown_event)
{
    // Updating metadata according to user request. TODO: HRT-9737
    // Currently softmax only supports inputs to be float32 and order NHWC or NC
    auto updated_inputs_metadata = softmax_op_metadata.get()->inputs_metadata();
    updated_inputs_metadata.begin()->second.format = vstream_params.user_buffer_format;
    auto updated_outputs_metadata = softmax_op_metadata.get()->outputs_metadata();
    updated_outputs_metadata.begin()->second.format = vstream_params.user_buffer_format;
    auto metadata = std::dynamic_pointer_cast<net_flow::SoftmaxOpMetadata>(softmax_op_metadata);
    assert(nullptr != metadata);
    metadata->set_outputs_metadata(updated_outputs_metadata);
    metadata->set_inputs_metadata(updated_inputs_metadata);
    CHECK_SUCCESS_AS_EXPECTED(metadata->validate_format_info());
    // Updating metadata according to use request. TODO: HRT-9737 - End

    auto op_expected = net_flow::SoftmaxPostProcessOp::create(metadata);
    CHECK_EXPECTED(op_expected);
    auto softmax_op = op_expected.release();
    auto softmax_element = SoftmaxPostProcessElement::create(softmax_op,
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params.pipeline_elements_stats_flags, pipeline_status, buffer_pool_size, timeout, vstream_flags, shutdown_event);
    CHECK_EXPECTED(softmax_element);
    elements.push_back(softmax_element.value());
    return softmax_element;
}

Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> VStreamsBuilderUtils::add_nms_to_detections_convert_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    size_t buffer_pool_size, std::chrono::milliseconds timeout, const hailo_vstream_stats_flags_t &vstream_flags, EventPtr &shutdown_event)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    auto nms_to_detections_element = ConvertNmsToDetectionsElement::create(metadata->nms_info(),
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params.pipeline_elements_stats_flags, pipeline_status, timeout, vstream_flags, shutdown_event, buffer_pool_size);
    CHECK_EXPECTED(nms_to_detections_element);
    elements.push_back(nms_to_detections_element.value());
    return nms_to_detections_element;
}

Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> VStreamsBuilderUtils::add_remove_overlapping_bboxes_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    size_t buffer_pool_size, std::chrono::milliseconds timeout, const hailo_vstream_stats_flags_t &vstream_flags, EventPtr &shutdown_event)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    auto remove_overlapping_bboxes_element = RemoveOverlappingBboxesElement::create(metadata->nms_config(),
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params.pipeline_elements_stats_flags, pipeline_status, timeout, vstream_flags, shutdown_event, buffer_pool_size);
    CHECK_EXPECTED(remove_overlapping_bboxes_element);
    elements.push_back(remove_overlapping_bboxes_element.value());
    return remove_overlapping_bboxes_element;
}

Expected<std::shared_ptr<FillNmsFormatElement>> VStreamsBuilderUtils::add_fill_nms_format_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    size_t buffer_pool_size, std::chrono::milliseconds timeout, const hailo_vstream_stats_flags_t &vstream_flags, EventPtr &shutdown_event)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    auto fill_nms_format_element = FillNmsFormatElement::create(metadata->nms_info(), vstream_params.user_buffer_format, metadata->nms_config(),
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params.pipeline_elements_stats_flags, pipeline_status, timeout, vstream_flags, shutdown_event, buffer_pool_size);
    CHECK_EXPECTED(fill_nms_format_element);
    elements.push_back(fill_nms_format_element.value());
    return fill_nms_format_element;
}

Expected<std::shared_ptr<UserBufferQueueElement>> VStreamsBuilderUtils::add_user_buffer_queue_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, EventPtr &shutdown_event, const hailo_vstream_params_t &vstream_params)
{
    auto post_argmax_queue_element = UserBufferQueueElement::create(
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params, shutdown_event, pipeline_status);
    CHECK_EXPECTED(post_argmax_queue_element);
    elements.push_back(post_argmax_queue_element.value());
    return post_argmax_queue_element;
}

Expected<std::shared_ptr<PostInferElement>> VStreamsBuilderUtils::add_post_infer_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event)
{
    auto post_infer_element = PostInferElement::create(output_stream->get_info().hw_shape, output_stream->get_info().format,
        output_stream->get_info().shape, vstream_params.user_buffer_format, output_stream->get_quant_infos(), output_stream->get_info().nms_info,
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params, pipeline_status, shutdown_event);
    CHECK_EXPECTED(post_infer_element);
    elements.push_back(post_infer_element.value());
    return post_infer_element;
}

Expected<std::vector<OutputVStream>> VStreamsBuilderUtils::create_output_post_process_argmax(std::shared_ptr<OutputStreamBase> output_stream,
    const NameToVStreamParamsMap &vstreams_params_map, const hailo_vstream_info_t &output_vstream_info,
    const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata)
{
    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<OutputVStream> vstreams;

    EventPtr core_op_activated_event = nullptr;
    if (!output_stream->is_scheduled()) {
        core_op_activated_event = output_stream->get_core_op_activated_event();
    }

    auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_exp);
    auto shutdown_event = shutdown_event_exp.release();

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    assert(!vstreams_params_map.empty());

    // Note: In case of multiple values in vstreams_params_map (e.g. in the case of demux), we'll set the
    //       pipeline_elements_stats_flags for the hw_read_element as bitwise or of all the flags.
    hailo_pipeline_elem_stats_flags_t hw_read_element_stats_flags = HAILO_PIPELINE_ELEM_STATS_NONE;
    hailo_vstream_stats_flags_t hw_read_stream_stats_flags = HAILO_VSTREAM_STATS_NONE;
    size_t buffer_pool_size = 0;
    for (const auto &elem_name_params : vstreams_params_map) {
        hw_read_element_stats_flags |= elem_name_params.second.pipeline_elements_stats_flags;
        hw_read_stream_stats_flags |= elem_name_params.second.vstream_stats_flags;
        buffer_pool_size += elem_name_params.second.queue_size;
    }

    // TODO (HRT-4522): Support this measurement
    CHECK_AS_EXPECTED(!(hw_read_stream_stats_flags & HAILO_VSTREAM_STATS_MEASURE_FPS), HAILO_NOT_IMPLEMENTED,
        "Pipeline FPS statistics measurement is not implemented");

    auto hw_read_element = add_hw_read_element(output_stream, pipeline_status, elements, "HwReadElement", shutdown_event,
        buffer_pool_size, hw_read_element_stats_flags, hw_read_stream_stats_flags);
    CHECK_EXPECTED(hw_read_element);

    assert(1 == vstreams_params_map.size());
    auto op_input_format = argmax_op_metadata->inputs_metadata().begin()->second.format;
    auto vstream_params = vstreams_params_map.begin()->second;
    vstream_params.user_buffer_format = net_flow::ArgmaxOpMetadata::expand_output_format_autos(vstream_params.user_buffer_format, op_input_format);

    auto hw_read_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQueueElement_hw_read",
        shutdown_event, vstream_params);
    CHECK_EXPECTED(hw_read_queue_element);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_element.value(), hw_read_queue_element.value()));

    auto argmax_element = add_argmax_element(output_stream, pipeline_status, elements, "ArgmaxPostProcessElement",
        vstream_params, argmax_op_metadata, buffer_pool_size, std::chrono::milliseconds(HAILO_INFINITE), hw_read_stream_stats_flags, shutdown_event);
    CHECK_EXPECTED(argmax_element);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_queue_element.value(), argmax_element.value()));

    auto post_argmax_queue_element = add_user_buffer_queue_element(output_stream, pipeline_status, elements,
        "UserBufferQueueElement_post_argmax", shutdown_event, vstream_params);
    CHECK_EXPECTED(post_argmax_queue_element);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(argmax_element.value(), post_argmax_queue_element.value()));

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstream_params);
    CHECK_EXPECTED(pipeline_latency_accumulator);

    output_stream->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
    hw_read_queue_element->get()->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
    auto vstream = OutputVStream::create(output_vstream_info, output_stream->get_quant_infos(), vstream_params, post_argmax_queue_element.release(), std::move(elements),
        std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
    CHECK_EXPECTED(vstream);
    vstreams.emplace_back(vstream.release());

    for (const auto &current_vstream : vstreams) {
        LOGGER__INFO("{}", current_vstream.get_pipeline_description());
    }

    return vstreams;
}

hailo_status VStreamsBuilderUtils::handle_pix_buffer_splitter_flow(std::vector<std::shared_ptr<InputStreamBase>> streams,
    const hailo_vstream_info_t &vstream_info, std::vector<std::shared_ptr<PipelineElement>> &&base_elements,
    std::vector<InputVStream> &vstreams, const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, EventPtr &core_op_activated_event,
    AccumulatorPtr accumalator)
{
    // sorting the streams based on their plane index -> we count on order to know which plane belongs to which stream
    auto compartor = [](std::shared_ptr<InputStreamBase> a, std::shared_ptr<InputStreamBase> b) {
        return a->get_layer_info().plane_index < b->get_layer_info().plane_index;
    };
    std::sort(streams.begin(), streams.end(), compartor);

    auto duration_collector_expected = DurationCollector::create(vstream_params.pipeline_elements_stats_flags);
    CHECK_EXPECTED_AS_STATUS(duration_collector_expected);

    auto planes_splitter = PixBufferElement::create(PipelineObject::create_element_name("PixBufferElement",
        vstream_info.name, 0), std::chrono::milliseconds(HAILO_INFINITE), duration_collector_expected.release(),
        pipeline_status, vstream_info.format.order);
    CHECK_EXPECTED_AS_STATUS(planes_splitter);
    base_elements.push_back(planes_splitter.value());

    uint32_t stream_number = 0;

    for (const auto &stream : streams){
         auto hw_write_elem = HwWriteElement::create(stream,
            PipelineObject::create_element_name("HwWriteElement", stream->name(), stream->get_info().index),
            vstream_params.pipeline_elements_stats_flags, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(hw_write_elem);
        base_elements.insert(base_elements.begin(), hw_write_elem.value());

        auto &stream_info = stream->get_info();
        auto &src_image_shape = stream_info.shape;
        auto &dst_image_shape = stream_info.hw_shape;
        auto &dst_format = stream_info.format;
        auto src_format = vstream_params.user_buffer_format;
        /* the format order of each plane (stream) is determined by the stream's order.
            type and flags are determined by the vstream params */
        src_format.order = dst_format.order;
        auto quant_infos = std::vector<hailo_quant_info_t>{stream_info.quant_info};

        auto should_transform_expected = InputTransformContext::is_transformation_required(src_image_shape, src_format,
            dst_image_shape, dst_format, quant_infos);
        CHECK_EXPECTED_AS_STATUS(should_transform_expected);

        if(should_transform_expected.value()){
            auto pre_infer_elem = PreInferElement::create(src_image_shape, src_format,
                dst_image_shape, dst_format, quant_infos, PipelineObject::create_element_name( "PreInferElement",
                stream->get_info().name, stream->get_info().index), vstream_params, shutdown_event, pipeline_status);

            CHECK_EXPECTED_AS_STATUS(pre_infer_elem);
            base_elements.push_back(pre_infer_elem.value());

            auto queue_elem = PushQueueElement::create(
                PipelineObject::create_element_name("PushQueueElement", stream_info.name, stream_info.index),
                vstream_params, shutdown_event, pipeline_status);

            CHECK_EXPECTED_AS_STATUS(queue_elem);
            base_elements.push_back((queue_elem.value()));

            CHECK_SUCCESS(PipelinePad::link_pads(planes_splitter.value(), pre_infer_elem.value(), stream_number, 0));
            CHECK_SUCCESS(PipelinePad::link_pads(pre_infer_elem.value(), queue_elem.value()));
            CHECK_SUCCESS(PipelinePad::link_pads(queue_elem.value(), *hw_write_elem));
        } else {
            CHECK_SUCCESS(PipelinePad::link_pads(planes_splitter.value(), *hw_write_elem, stream_number, 0));

        }
        stream_number++;
    }

    auto vstream = InputVStream::create(vstream_info, { vstream_info.quant_info }, vstream_params, planes_splitter.value(),
        nullptr, std::move(base_elements), std::move(pipeline_status), shutdown_event,
        core_op_activated_event, accumalator);
    CHECK_EXPECTED_AS_STATUS(vstream);
    vstreams.emplace_back(vstream.release());

    return HAILO_SUCCESS;
}

hailo_status VStreamsBuilderUtils::add_demux(std::shared_ptr<OutputStreamBase> output_stream, NameToVStreamParamsMap &vstreams_params_map,
    std::vector<std::shared_ptr<PipelineElement>> &&base_elements, std::vector<OutputVStream> &vstreams,
    std::shared_ptr<HwReadElement> hw_read_elem, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos)
{
    auto expected_demuxer = OutputDemuxer::create(*output_stream);
    CHECK_EXPECTED_AS_STATUS(expected_demuxer);

    std::shared_ptr<OutputDemuxer> demuxer_ptr = expected_demuxer.release();
    CHECK(nullptr != demuxer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = output_stream->set_timeout(HAILO_INFINITE_TIMEOUT);
    CHECK_SUCCESS(status);

    // Note: In case of multiple values in vstreams_params_map (e.g. in the case of demux), we'll set the
    //       pipeline_elements_stats_flags for the demux_elem as bitwise or of all the flags.
    hailo_pipeline_elem_stats_flags_t demux_elem_stats_flags = HAILO_PIPELINE_ELEM_STATS_NONE;
    hailo_vstream_stats_flags_t demux_vstream_stats_flags = HAILO_VSTREAM_STATS_NONE;
    size_t buffer_pool_size = 0;
    for (const auto &elem_name_params : vstreams_params_map) {
        demux_elem_stats_flags |= elem_name_params.second.pipeline_elements_stats_flags;
        demux_vstream_stats_flags |= elem_name_params.second.vstream_stats_flags;
        buffer_pool_size += elem_name_params.second.queue_size;
    }

    auto demux_elem = TransformDemuxElement::create(demuxer_ptr,
        PipelineObject::create_element_name("TransformDemuxElement", output_stream->name(), output_stream->get_info().index),
        std::chrono::milliseconds(HAILO_INFINITE), buffer_pool_size, demux_elem_stats_flags, demux_vstream_stats_flags, shutdown_event, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(demux_elem);
    base_elements.push_back(demux_elem.value());
    CHECK_SUCCESS(PipelinePad::link_pads(hw_read_elem, demux_elem.value()));

    EventPtr core_op_activated_event = nullptr;
    if (!output_stream->is_scheduled()) {
        core_op_activated_event = output_stream->get_core_op_activated_event();
    }

    uint32_t i = 0;
    for (auto &edge_info : demuxer_ptr->get_edges_stream_info()) {
        auto name_params_pair = vstreams_params_map.find(edge_info.name);
        CHECK(name_params_pair != vstreams_params_map.end(), HAILO_NOT_FOUND,
            "Failed to find vstreams params of edge {}", edge_info.name);

        const auto vstream_info = output_vstream_infos.find(edge_info.name);
        CHECK(vstream_info != output_vstream_infos.end(), HAILO_NOT_FOUND,
            "Failed to find vstream info of {}", edge_info.name);

        const auto vstream_params = expand_vstream_params_autos(output_stream->get_info(), name_params_pair->second);

        // For each mux vstream, we create a copy of the previous elements
        auto current_vstream_elements = base_elements;

        // For muxed VStreams we use the same pipeline_status for all
        auto pipeline_status_copy = pipeline_status;
        auto demux_queue_elem = PullQueueElement::create(
            PipelineObject::create_element_name("PullQueueElement_demux", edge_info.name, edge_info.index),
            vstream_params, shutdown_event, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(demux_queue_elem);
        current_vstream_elements.push_back(demux_queue_elem.value());
        CHECK_SUCCESS(PipelinePad::link_pads(demux_elem.value(), demux_queue_elem.value(), i, 0));

        CHECK_SUCCESS(demux_queue_elem.value()->set_timeout(HAILO_INFINITE_TIMEOUT));

        auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstream_params);
        CHECK_EXPECTED_AS_STATUS(pipeline_latency_accumulator);
        auto should_transform = OutputTransformContext::is_transformation_required(edge_info.hw_shape, 
            edge_info.format, edge_info.shape, vstream_params.user_buffer_format, std::vector<hailo_quant_info_t>{edge_info.quant_info}); // TODO: Get quant vector (HRT-11077)
        CHECK_EXPECTED_AS_STATUS(should_transform);

        if (should_transform.value()) {
            auto post_infer_elem = PostInferElement::create(edge_info.hw_shape, edge_info.format, 
                edge_info.shape, vstream_params.user_buffer_format, { edge_info.quant_info }, edge_info.nms_info, // TODO: Get quant vector (HRT-11077)
                PipelineObject::create_element_name("PostInferElement", edge_info.name, edge_info.index),
                vstream_params, pipeline_status, shutdown_event);
            CHECK_EXPECTED_AS_STATUS(post_infer_elem);
            current_vstream_elements.push_back(post_infer_elem.value());
            CHECK_SUCCESS(PipelinePad::link_pads(demux_queue_elem.value(), post_infer_elem.value()));

            auto post_infer_queue_elem = UserBufferQueueElement::create(
                PipelineObject::create_element_name("UserBufferQueueElement_post_infer", edge_info.name, edge_info.index),
                vstream_params, shutdown_event, pipeline_status);
            CHECK_EXPECTED_AS_STATUS(post_infer_queue_elem);
            current_vstream_elements.push_back(post_infer_queue_elem.value());
            CHECK_SUCCESS(PipelinePad::link_pads(post_infer_elem.value(), post_infer_queue_elem.value()));

            // TODO: Replace output_stream->get_quant_infos() with mux quant info
            auto vstream = OutputVStream::create(vstream_info->second, output_stream->get_quant_infos(), vstream_params, post_infer_queue_elem.release(), std::move(current_vstream_elements), // TODO: Get quant vector (HRT-11077)
                std::move(pipeline_status_copy), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED_AS_STATUS(vstream);
            vstreams.emplace_back(vstream.release());
        } else {
            // TODO: HRT-4179
            auto user_copy_elem = CopyBufferElement::create(
                PipelineObject::create_element_name("CopyBufferElement", edge_info.name, edge_info.index),
                pipeline_status, std::chrono::milliseconds(vstream_params.timeout_ms));
            CHECK_EXPECTED_AS_STATUS(user_copy_elem);
            current_vstream_elements.push_back(user_copy_elem.value());
            CHECK_SUCCESS(PipelinePad::link_pads(demux_queue_elem.value(), user_copy_elem.value()));

            // TODO: Replace output_stream->get_quant_infos() with mux quant info
            auto vstream = OutputVStream::create(vstream_info->second, { edge_info.quant_info }, vstream_params, user_copy_elem.release(), std::move(current_vstream_elements), // TODO: Get quant vector (HRT-11077)
                std::move(pipeline_status_copy), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED_AS_STATUS(vstream);
            vstreams.emplace_back(vstream.release());
        }
        i++;
    }
    return HAILO_SUCCESS;
}

hailo_status VStreamsBuilderUtils::add_nms_fuse(OutputStreamPtrVector &output_streams, hailo_vstream_params_t &vstreams_params,
    std::vector<std::shared_ptr<PipelineElement>> &elements, std::vector<OutputVStream> &vstreams,
    EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos)
{
    std::vector<hailo_nms_info_t> nms_infos;
    nms_infos.reserve(output_streams.size());
    for (const auto &out_stream : output_streams) {
        CHECK(out_stream->get_info().nms_info.defuse_info.class_group_index <= output_streams.size(),
            HAILO_INVALID_ARGUMENT, "Not all defused nms outputs were grouped correctly!");
        nms_infos.emplace_back(out_stream->get_info().nms_info);
    }

    // To get the fused layer name and src stream format, we use the stream info of one of the defuses
    auto first_defused_stream_info = output_streams[0]->get_info();
    auto fused_layer_name = first_defused_stream_info.nms_info.defuse_info.original_name;
    auto src_stream_format = first_defused_stream_info.format;

    auto vstream_info = output_vstream_infos.find(fused_layer_name);
    CHECK(vstream_info != output_vstream_infos.end(), HAILO_NOT_FOUND,
        "Failed to find vstream info of {}. Could be due to use of old HEF. Try to re-compile network with newer Dataflow Compiler version", fused_layer_name);

    vstreams_params = expand_vstream_params_autos(first_defused_stream_info, vstreams_params);
    auto nms_elem = NmsMuxElement::create(nms_infos,
        PipelineObject::create_element_name("NmsMuxElement", fused_layer_name, 0),
        vstreams_params, shutdown_event, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(nms_elem);
    auto fused_layer_nms_info = nms_elem.value()->get_fused_nms_info();

    for (uint32_t i = 0; i < output_streams.size(); ++i) {
        const auto &curr_stream_info = output_streams[i]->get_info();
        output_streams[i]->set_timeout(HAILO_INFINITE_TIMEOUT);

        auto hw_read_elem = HwReadElement::create(output_streams[i],
            PipelineObject::create_element_name("HwReadElement", curr_stream_info.name, curr_stream_info.index),
            HAILO_INFINITE_TIMEOUT, vstreams_params.queue_size, vstreams_params.pipeline_elements_stats_flags,
            vstreams_params.vstream_stats_flags, shutdown_event, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(hw_read_elem);
        elements.push_back(hw_read_elem.value());

        auto nms_source_queue_elem = PullQueueElement::create(
            PipelineObject::create_element_name("PullQueueElement_nms_source", curr_stream_info.name, curr_stream_info.index),
            vstreams_params, shutdown_event, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(nms_source_queue_elem);
        elements.push_back(nms_source_queue_elem.value());
        nms_source_queue_elem.value()->set_timeout(HAILO_INFINITE_TIMEOUT);
        CHECK_SUCCESS(PipelinePad::link_pads(hw_read_elem.value(), nms_source_queue_elem.value()));
        CHECK_SUCCESS(PipelinePad::link_pads(nms_source_queue_elem.value(), nms_elem.value(), 0, i));
    }
    elements.push_back(nms_elem.value());

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstreams_params);
    CHECK_EXPECTED_AS_STATUS(pipeline_latency_accumulator);

    auto should_transform = OutputTransformContext::is_transformation_required({}, src_stream_format, {},
        vstreams_params.user_buffer_format, std::vector<hailo_quant_info_t>{vstream_info->second.quant_info}); // TODO: Get quant vector (HRT-11078)
    CHECK_EXPECTED_AS_STATUS(should_transform);

    EventPtr core_op_activated_event = nullptr;
    if (!output_streams[0]->is_scheduled()) {
        core_op_activated_event = output_streams[0]->get_core_op_activated_event();
    }

    if (should_transform.value()) {
        auto nms_queue_elem = PullQueueElement::create(
            PipelineObject::create_element_name("PullQueueElement_nms", fused_layer_name, 0),
            vstreams_params, shutdown_event, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(nms_queue_elem);
        nms_queue_elem.value()->set_timeout(HAILO_INFINITE_TIMEOUT);
        elements.push_back(nms_queue_elem.value());
        CHECK_SUCCESS(PipelinePad::link_pads(nms_elem.value(), nms_queue_elem.value()));

        auto post_infer_elem = PostInferElement::create({}, src_stream_format,
            {}, vstreams_params.user_buffer_format, { vstream_info->second.quant_info }, fused_layer_nms_info, // TODO: Get quant vector (HRT-11078)
            PipelineObject::create_element_name("PostInferElement", fused_layer_name, 0), vstreams_params, pipeline_status,
            shutdown_event);
        CHECK_EXPECTED_AS_STATUS(post_infer_elem);

        elements.push_back(post_infer_elem.value());
        CHECK_SUCCESS(PipelinePad::link_pads(nms_queue_elem.value(), post_infer_elem.value()));

        auto post_infer_queue_elem = UserBufferQueueElement::create(
            PipelineObject::create_element_name("UserBufferQueueElement_post_infer", fused_layer_name, 0),
            vstreams_params, shutdown_event, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(post_infer_queue_elem);
        elements.push_back(post_infer_queue_elem.value());
        CHECK_SUCCESS(PipelinePad::link_pads(post_infer_elem.value(), post_infer_queue_elem.value()));

        // TODO: Check with SDK where should we take the quant infos from (output_streams[0]->get_quant_infos() might be good) (HRT-11078)
        auto vstream = OutputVStream::create(vstream_info->second, output_streams[0]->get_quant_infos(), vstreams_params, post_infer_queue_elem.release(), std::move(elements), // TODO: Get quant vector (HRT-11078)
            std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
        CHECK_EXPECTED_AS_STATUS(vstream);
        vstreams.emplace_back(vstream.release());
    } else {
        // TODO: Check with SDK where should we take the quant infos from (output_streams[0]->get_quant_infos() might be good) (HRT-11078)
        auto vstream = OutputVStream::create(vstream_info->second, output_streams[0]->get_quant_infos(), vstreams_params, nms_elem.release(), std::move(elements), // TODO: Get quant vector (HRT-11078)
            std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
        CHECK_EXPECTED_AS_STATUS(vstream);
        vstreams.emplace_back(vstream.release());
    }

    return HAILO_SUCCESS;
}

hailo_status VStreamsBuilderUtils::add_nms_post_process(OutputStreamPtrVector &output_streams, hailo_vstream_params_t &vstreams_params,
    std::vector<std::shared_ptr<PipelineElement>> &elements, std::vector<OutputVStream> &vstreams,
    EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
    const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos,
    const std::shared_ptr<hailort::net_flow::Op> &nms_op)
{
    auto first_stream_info = output_streams[0]->get_info();
    vstreams_params.user_buffer_format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(
        vstreams_params.user_buffer_format, nms_op->metadata()->type());
    CHECK(vstreams_params.user_buffer_format.type == HAILO_FORMAT_TYPE_FLOAT32, HAILO_INVALID_ARGUMENT,
        "NMS output format type must be HAILO_FORMAT_TYPE_FLOAT32");
    CHECK(HailoRTCommon::is_nms(vstreams_params.user_buffer_format.order), HAILO_INVALID_ARGUMENT,
        "NMS output format order must be HAILO_FORMAT_ORDER_HAILO_NMS or HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK");

    std::unordered_map<std::string, net_flow::BufferMetaData> inputs_metadata;
    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    for (uint32_t i = 0; i < output_streams.size(); ++i) {
        const auto &curr_stream_info = output_streams[i]->get_info();
        net_flow::BufferMetaData input_metadata = {
            curr_stream_info.shape,
            curr_stream_info.hw_shape,
            curr_stream_info.format,
            curr_stream_info.quant_info
        };
        inputs_metadata.insert({curr_stream_info.name, input_metadata});
    }

    const auto &output_pads = nms_op->outputs_metadata();
    assert(output_pads.size() == 1);
    auto vstream_info = output_vstream_infos.find(output_pads.begin()->first);
    CHECK(vstream_info != output_vstream_infos.end(), HAILO_NOT_FOUND,
        "Failed to find vstream info of {}", nms_op->metadata()->get_name());
    net_flow::BufferMetaData output_metadata = {
        vstream_info->second.shape,
        vstream_info->second.shape,
        vstream_info->second.format,
        vstream_info->second.quant_info
    };
    outputs_metadata.insert({vstream_info->first, output_metadata});

    auto op_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(nms_op->metadata());
    assert(nullptr != op_metadata);
    auto nms_elem = NmsPostProcessMuxElement::create(nms_op,
        PipelineObject::create_element_name("NmsPostProcessMuxElement", nms_op->get_name(), 0),
        vstreams_params, shutdown_event, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(nms_elem);

    hailo_format_t nms_src_format;
    nms_src_format.flags = HAILO_FORMAT_FLAGS_NONE;
    nms_src_format.order = HAILO_FORMAT_ORDER_NHCW;
    nms_src_format.type = first_stream_info.format.type;

    for (uint32_t i = 0; i < output_streams.size(); ++i) {
        const auto &curr_stream_info = output_streams[i]->get_info();
        output_streams[i]->set_timeout(HAILO_INFINITE_TIMEOUT);

        auto should_transform = OutputTransformContext::is_transformation_required(curr_stream_info.hw_shape, curr_stream_info.format,
            curr_stream_info.hw_shape, nms_src_format, output_streams[i]->get_quant_infos());
        CHECK_EXPECTED_AS_STATUS(should_transform);

        CHECK(!(should_transform.value()), HAILO_INVALID_ARGUMENT, "Unexpected transformation required for {}", curr_stream_info.name);

        auto hw_read_elem = HwReadElement::create(output_streams[i],
            PipelineObject::create_element_name("HwReadElement", curr_stream_info.name, curr_stream_info.index),
            HAILO_INFINITE_TIMEOUT, vstreams_params.queue_size, vstreams_params.pipeline_elements_stats_flags,
            vstreams_params.vstream_stats_flags, shutdown_event, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(hw_read_elem);
        elements.push_back(hw_read_elem.value());

        auto nms_source_queue_elem = PullQueueElement::create(
            PipelineObject::create_element_name("PullQueueElement_nms_source", curr_stream_info.name, curr_stream_info.index),
            vstreams_params, shutdown_event, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(nms_source_queue_elem);
        nms_source_queue_elem.value()->set_timeout(HAILO_INFINITE_TIMEOUT);
        elements.push_back(nms_source_queue_elem.value());
        CHECK_SUCCESS(PipelinePad::link_pads(hw_read_elem.value(), nms_source_queue_elem.value()));
        CHECK_SUCCESS(PipelinePad::link_pads(nms_source_queue_elem.value(), nms_elem.value(), 0, i));
        nms_elem.value()->add_sink_name(curr_stream_info.name);
    }
    elements.push_back(nms_elem.value());

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstreams_params);
    CHECK_EXPECTED_AS_STATUS(pipeline_latency_accumulator);

    EventPtr core_op_activated_event = nullptr;
    if (!output_streams[0]->is_scheduled()) {
        core_op_activated_event = output_streams[0]->get_core_op_activated_event();
    }

    // If user uses HailoRT++ we can assume he won't use Output Scale by Feature
    auto vstream = OutputVStream::create(vstream_info->second, output_streams[0]->get_quant_infos(), vstreams_params, nms_elem.release(), std::move(elements),
        std::move(pipeline_status), shutdown_event, core_op_activated_event, pipeline_latency_accumulator.release());
    CHECK_EXPECTED_AS_STATUS(vstream);
    vstreams.emplace_back(vstream.release());

    return HAILO_SUCCESS;
}

Expected<AccumulatorPtr> VStreamsBuilderUtils::create_pipeline_latency_accumulator(const hailo_vstream_params_t &vstreams_params)
{
    AccumulatorPtr pipeline_latency_accumulator = nullptr;
    const auto measure_latency = ((vstreams_params.vstream_stats_flags & HAILO_VSTREAM_STATS_MEASURE_LATENCY) != 0);
    if (measure_latency) {
        pipeline_latency_accumulator = make_shared_nothrow<FullAccumulator<double>>("latency");
        CHECK_AS_EXPECTED(nullptr != pipeline_latency_accumulator, HAILO_OUT_OF_HOST_MEMORY);
    }

    return pipeline_latency_accumulator;
}

} /* namespace hailort */
