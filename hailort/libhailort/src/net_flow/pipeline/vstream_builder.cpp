/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream_builder.cpp
 * @brief Vstream builder impl
 **/

#include "vstream_builder.hpp"
#include "hailo/vstream.hpp"
#include "net_flow/ops/nms_post_process.hpp"
#include "net_flow/ops/ssd_post_process.hpp"
#include "net_flow/ops/yolox_post_process.hpp"
#include "net_flow/ops/yolov8_post_process.hpp"
#include "net_flow/ops/yolov8_bbox_only_post_process.hpp"
#include "net_flow/ops/yolov5_post_process.hpp"
#include "net_flow/ops/yolov5_bbox_only_post_process.hpp"
#include "net_flow/ops/argmax_post_process.hpp"
#include "net_flow/ops/softmax_post_process.hpp"
#include "net_flow/ops/yolov5_seg_post_process.hpp"
#include "common/runtime_statistics_internal.hpp"

namespace hailort
{
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

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstream_params);
    CHECK_EXPECTED(pipeline_latency_accumulator);

    auto user_timeout = std::chrono::milliseconds(vstream_params.timeout_ms);

    if (input_streams.size() > 1) {
        CHECK_SUCCESS_AS_EXPECTED(handle_pix_buffer_splitter_flow(input_streams, vstream_info,
            std::move(elements), vstreams, vstream_params, pipeline_status, core_op_activated_event,
            pipeline_latency_accumulator.value()));
    } else {
        auto hw_write_elem = HwWriteElement::create(input_stream,
            PipelineObject::create_element_name("HwWriteEl", input_stream->name(), input_stream->get_info().index),
            vstream_params.pipeline_elements_stats_flags, pipeline_status);
        CHECK_EXPECTED(hw_write_elem);
        elements.insert(elements.begin(), hw_write_elem.value());

        auto should_transform = InputTransformContext::is_transformation_required(input_stream->get_info().shape,
            vstream_params.user_buffer_format, input_stream->get_info().hw_shape, input_stream->get_info().format,
            input_stream->get_quant_infos());
        CHECK_EXPECTED(should_transform);

        if (should_transform.value()) {
            auto queue_elem = PushQueueElement::create(
                PipelineObject::create_element_name("PushQEl", input_stream->get_info().name, input_stream->get_info().index),
                vstream_params, input_stream->get_info().hw_frame_size, pipeline_status);
            CHECK_EXPECTED(queue_elem);
            elements.insert(elements.begin(), queue_elem.value());
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(queue_elem.value(), hw_write_elem.value()));

            auto pre_infer_elem = PreInferElement::create(input_stream->get_info().shape, vstream_params.user_buffer_format,
                input_stream->get_info().hw_shape, input_stream->get_info().format, input_stream->get_quant_infos(),
                PipelineObject::create_element_name("PreInferEl", input_stream->get_info().name, input_stream->get_info().index),
                vstream_params, pipeline_status);
            CHECK_EXPECTED(pre_infer_elem);
            elements.insert(elements.begin(), pre_infer_elem.value());
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_infer_elem.value(), queue_elem.value()));

            input_stream->set_timeout(user_timeout);
            auto vstream = InputVStream::create(vstream_info, input_stream->get_quant_infos(), vstream_params, pre_infer_elem.release(),
                hw_write_elem.release(), std::move(elements), std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED(vstream);
            vstreams.emplace_back(vstream.release());
        } else {
            input_stream->set_timeout(user_timeout);
            auto vstream = InputVStream::create(vstream_info, input_stream->get_quant_infos(), vstream_params, hw_write_elem.value(), hw_write_elem.value(),
                std::move(elements), std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED(vstream);
            vstreams.emplace_back(vstream.release());
        }
    }

    for (const auto &vstream : vstreams) {
       LOGGER__INFO("{}", vstream.get_pipeline_description());
    }

    return vstreams;
}

static hailo_vstream_params_t expand_vstream_params_autos(const hailo_stream_info_t &stream_info,
    const hailo_vstream_params_t &vstream_params)
{
    auto local_vstream_params = vstream_params;
    local_vstream_params.user_buffer_format = HailoRTDefaults::expand_auto_format(vstream_params.user_buffer_format,
        stream_info.format);
    return local_vstream_params;
}

Expected<std::vector<OutputVStream>> VStreamsBuilderUtils::create_outputs(std::shared_ptr<OutputStreamBase> output_stream,
    NameToVStreamParamsMap &vstreams_params_map, const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos)
{
    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<OutputVStream> vstreams;

    EventPtr core_op_activated_event = nullptr;
    if (!output_stream->is_scheduled()) {
        core_op_activated_event = output_stream->get_core_op_activated_event();
    }

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

	ElementBuildParams build_params{};
    build_params.elem_stats_flags = hw_read_element_stats_flags;
    build_params.pipeline_status = pipeline_status;
    build_params.timeout = std::chrono::milliseconds(HAILO_INFINITE);
    build_params.shutdown_event = nullptr;
    build_params.vstream_stats_flags = hw_read_stream_stats_flags;
    build_params.buffer_pool_size_edges = buffer_pool_size;

    auto hw_read_element = add_hw_read_element(output_stream, elements, "HwReadEl", build_params);
    CHECK_EXPECTED(hw_read_element);

    if (output_stream->get_info().is_mux) {
        hailo_status status = add_demux(output_stream, vstreams_params_map, std::move(elements), vstreams, hw_read_element.value(),
            pipeline_status, output_vstream_infos);
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
            auto pull_queue = PullQueueElement::create(
                PipelineObject::create_element_name("PullQEl_hw_read", output_stream->name(), output_stream->get_info().index),
                build_params.timeout, buffer_pool_size, output_stream->get_frame_size(),
                hw_read_element_stats_flags, hw_read_stream_stats_flags, pipeline_status);
            CHECK_EXPECTED(pull_queue);
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_element.value(), pull_queue.value()));
            elements.push_back(pull_queue.value());

            auto post_infer_element = add_post_infer_element(output_stream, pipeline_status, elements,
                "PostInferEl", vstream_params);
            CHECK_EXPECTED(post_infer_element);
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pull_queue.value(), post_infer_element.value()));

            auto post_transform_frame_size = HailoRTCommon::get_frame_size(vstream_info->second, vstream_params.user_buffer_format);
            auto user_buffer_queue_element = add_user_buffer_queue_element(output_stream, pipeline_status, elements,
                "UserBuffQEl", vstream_params, post_transform_frame_size);
            CHECK_EXPECTED(user_buffer_queue_element);
	        CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(post_infer_element.value(), user_buffer_queue_element.value()));

            output_stream->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
            pull_queue->get()->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
            auto vstream = OutputVStream::create(vstream_info->second, output_stream->get_quant_infos(), vstream_params, user_buffer_queue_element.release(), std::move(elements),
                std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED(vstream);
            vstreams.emplace_back(vstream.release());
        } else {
            auto post_transform_frame_size = HailoRTCommon::get_frame_size(vstream_info->second, vstream_params.user_buffer_format);
            auto user_buffer_queue_element = add_user_buffer_queue_element(output_stream, pipeline_status, elements,
                "UserBuffQEl", vstream_params, post_transform_frame_size);
            CHECK_EXPECTED(user_buffer_queue_element);
            CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_element.value(), user_buffer_queue_element.value()));

            output_stream->set_timeout(std::chrono::milliseconds(vstream_params.timeout_ms));
            auto vstream = OutputVStream::create(vstream_info->second, output_stream->get_quant_infos(), vstream_params, user_buffer_queue_element.release(), std::move(elements),
                std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator.release());
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

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    auto nms_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(iou_op_metadata);
    assert(nullptr != nms_metadata);

    vstream_params.user_buffer_format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(vstream_params.user_buffer_format,
        iou_op_metadata->type(), nms_metadata->nms_config().bbox_only);

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstream_params);
    CHECK_EXPECTED(pipeline_latency_accumulator);

	ElementBuildParams build_params{};
    build_params.elem_stats_flags = vstream_params.pipeline_elements_stats_flags;
    build_params.pipeline_status = pipeline_status;
    build_params.timeout = std::chrono::milliseconds(HAILO_INFINITE);
    build_params.shutdown_event = nullptr;
    build_params.vstream_stats_flags = vstream_params.vstream_stats_flags;
    build_params.buffer_pool_size_edges = vstream_params.queue_size;

    auto hw_read_element = add_hw_read_element(output_stream, elements, "HwReadEl", build_params);
    CHECK_EXPECTED(hw_read_element);

    auto hw_read_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQEl_hw_read",
        vstream_params, output_stream->get_frame_size());
    CHECK_EXPECTED(hw_read_queue_element);
    hw_read_queue_element->get()->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_element.value(), hw_read_queue_element.value()));

    auto post_infer_element = add_post_infer_element(output_stream, pipeline_status, elements,
        "PostInferEl", vstream_params);
    CHECK_EXPECTED(post_infer_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_queue_element.value(), post_infer_element.value()));

    auto post_transform_frame_size = HailoRTCommon::get_nms_by_class_host_frame_size(output_stream->get_info().nms_info, vstream_params.user_buffer_format);
    auto pre_nms_convert_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQEl_pre_nms_convert",
        vstream_params, post_transform_frame_size);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(post_infer_element.value(), pre_nms_convert_queue_element.value()));

    auto nms_to_detections_element = add_nms_to_detections_convert_element(output_stream, elements, "NmsFormatToDetectionsEl",
        iou_op_metadata, build_params);
    CHECK_EXPECTED(nms_to_detections_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_nms_convert_queue_element.value(), nms_to_detections_element.value()));

    auto pre_remove_overlapping_bboxes_element_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQEl_pre_bboxes_removing",
        vstream_params, 0);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(nms_to_detections_element.value(), pre_remove_overlapping_bboxes_element_queue_element.value()));

    auto remove_overlapping_bboxes_element = add_remove_overlapping_bboxes_element(output_stream, elements, "RemoveOverlappingBboxesEl",
        iou_op_metadata, build_params);
    CHECK_EXPECTED(remove_overlapping_bboxes_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_remove_overlapping_bboxes_element_queue_element.value(), remove_overlapping_bboxes_element.value()));

    auto pre_fill_nms_format_element_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQElt_pre_fill_nms_format",
        vstream_params, 0);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(remove_overlapping_bboxes_element.value(), pre_fill_nms_format_element_queue_element.value()));

    auto fill_nms_format_element = add_fill_nms_format_element(output_stream, elements, "FillNmsFormatEl",
        iou_op_metadata, build_params, vstream_params.user_buffer_format.order);
    CHECK_EXPECTED(fill_nms_format_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_fill_nms_format_element_queue_element.value(), fill_nms_format_element.value()));

    auto output_vstream_info = iou_op_metadata->get_output_vstream_info();
    CHECK_EXPECTED(output_vstream_info);
    const auto final_frame_size = HailoRTCommon::get_frame_size(*output_vstream_info, vstream_params.user_buffer_format);

    auto user_buffer_queue_element = add_user_buffer_queue_element(output_stream, pipeline_status, elements,
        "UserBuffQEl", vstream_params, final_frame_size);
    CHECK_EXPECTED(user_buffer_queue_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(fill_nms_format_element.value(), user_buffer_queue_element.value()));
    output_stream->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));

    auto vstream = OutputVStream::create(output_vstream_info.value(), output_stream->get_quant_infos(), vstream_params, user_buffer_queue_element.release(),
        std::move(elements), std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator.release());
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

	ElementBuildParams build_params{};
    build_params.elem_stats_flags = hw_read_element_stats_flags;
    build_params.pipeline_status = pipeline_status;
    build_params.timeout = std::chrono::milliseconds(HAILO_INFINITE);
    build_params.vstream_stats_flags = hw_read_stream_stats_flags;
    build_params.shutdown_event = nullptr;
    build_params.buffer_pool_size_edges = buffer_pool_size;

    auto hw_read_element = add_hw_read_element(output_stream, elements, "HwReadEl", build_params);
    CHECK_EXPECTED(hw_read_element);

    auto hw_read_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQEl_hw_read",
        vstream_params, output_stream->get_frame_size());
    CHECK_EXPECTED(hw_read_queue_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_element.value(), hw_read_queue_element.value()));

    auto post_infer_element = add_post_infer_element(output_stream, pipeline_status, elements,
        "PostInferEl", vstream_params);
    CHECK_EXPECTED(post_infer_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_queue_element.value(), post_infer_element.value()));

    auto post_transform_frame_size = HailoRTCommon::get_frame_size(output_vstream_info, vstream_params.user_buffer_format);

    auto pre_softmax_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQEl_pre_softmax",
        vstream_params, post_transform_frame_size);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(post_infer_element.value(), pre_softmax_queue_element.value()));

    auto softmax_element = add_softmax_element(output_stream, elements, "SoftmaxPPEl", vstream_params, softmax_op_metadata, build_params);
    CHECK_EXPECTED(softmax_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(pre_softmax_queue_element.value(), softmax_element.value()));

    auto user_buffer_queue_element = add_user_buffer_queue_element(output_stream, pipeline_status, elements,
        "UserBuffQEl", vstream_params, post_transform_frame_size);
    CHECK_EXPECTED(user_buffer_queue_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(softmax_element.value(), user_buffer_queue_element.value()));

    output_stream->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
    hw_read_queue_element->get()->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));

    auto vstream = OutputVStream::create(output_vstream_info, output_stream->get_quant_infos(), vstream_params, user_buffer_queue_element.release(), std::move(elements),
        std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator.release());
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
    if ((first_stream_info.format.order == HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP) && (first_stream_info.nms_info.is_defused)) {
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
            auto nms_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
            assert(nullptr != nms_metadata);
            updated_outputs_metadata.begin()->second.format =
                net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(vstream_params.user_buffer_format, op_metadata->type(),
                nms_metadata->nms_config().bbox_only);

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
                if (metadata->nms_config().bbox_only) {
                    auto bbox_only_metadata = std::dynamic_pointer_cast<net_flow::Yolov8BboxOnlyOpMetadata>(op_metadata);
                    assert(nullptr != bbox_only_metadata);
                    TRY(op, net_flow::YOLOv8BboxOnlyPostProcessOp::create(bbox_only_metadata));
                    break;
                } else {
                    TRY(op, net_flow::YOLOV8PostProcessOp::create(metadata));
                    break;
                }
            }
            case (net_flow::OperationType::YOLOV5):
            {
                auto metadata = std::dynamic_pointer_cast<net_flow::Yolov5OpMetadata>(op_metadata);
                assert(nullptr != metadata);
                if (metadata->nms_config().bbox_only) {
                    auto bbox_only_metadata = std::dynamic_pointer_cast<net_flow::Yolov5BboxOnlyOpMetadata>(op_metadata);
                    assert(nullptr != bbox_only_metadata);
                    auto op_expected = net_flow::YOLOv5BboxOnlyPostProcessOp::create(bbox_only_metadata);
                    CHECK_EXPECTED(op_expected);
                    op = op_expected.release();
                    break;
                } else {
                    auto op_expected = net_flow::YOLOv5PostProcessOp::create(metadata);
                    CHECK_EXPECTED(op_expected);
                    op = op_expected.release();
                    break;
                }
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
                if (strncmp(output_stream.first->get_info().name, output_streams[0]->get_info().name, HAILO_MAX_STREAM_NAME_SIZE) == 0) {
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
                if (strncmp(output_stream.first->get_info().name, output_streams[0]->get_info().name, HAILO_MAX_STREAM_NAME_SIZE) == 0) {
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
            if (strncmp(output_stream.first->get_info().name, output_streams[0]->get_info().name, HAILO_MAX_STREAM_NAME_SIZE) == 0) {
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

    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<OutputVStream> vstreams;

    hailo_status status = add_nms_fuse(output_streams, vstreams_params, elements, vstreams,
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
    auto pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_AS_EXPECTED(nullptr != pipeline_status, HAILO_OUT_OF_HOST_MEMORY);

    std::vector<std::shared_ptr<PipelineElement>> elements;
    std::vector<OutputVStream> vstreams;

    hailo_status status = add_nms_post_process(output_streams, vstreams_params, elements, vstreams,
        pipeline_status, output_vstream_infos, nms_op);
    CHECK_SUCCESS_AS_EXPECTED(status);

    for (const auto &vstream : vstreams) {
        LOGGER__INFO("{}", vstream.get_pipeline_description());
    }

    return vstreams;
}

Expected<std::shared_ptr<HwReadElement>> VStreamsBuilderUtils::add_hw_read_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, const ElementBuildParams &build_params)
{
    auto hw_read_elem = HwReadElement::create(output_stream,
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index), 
        build_params);
    CHECK_EXPECTED(hw_read_elem);
    elements.push_back(hw_read_elem.value());
    return hw_read_elem;
}

Expected<std::shared_ptr<PullQueueElement>> VStreamsBuilderUtils::add_pull_queue_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, const hailo_vstream_params_t &vstream_params, size_t frame_size)
{
    auto pull_queue_elem = PullQueueElement::create(
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params, frame_size, pipeline_status);
    CHECK_EXPECTED(pull_queue_elem);
    elements.push_back(pull_queue_elem.value());
    return pull_queue_elem;
}

Expected<std::shared_ptr<ArgmaxPostProcessElement>> VStreamsBuilderUtils::add_argmax_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, hailo_vstream_params_t &vstream_params, 
    const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata, const ElementBuildParams &build_params)
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
        build_params);
    CHECK_EXPECTED(argmax_element);
    elements.push_back(argmax_element.value());
    return argmax_element;
}

Expected<std::shared_ptr<SoftmaxPostProcessElement>> VStreamsBuilderUtils::add_softmax_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, hailo_vstream_params_t &vstream_params,
    const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata, const ElementBuildParams &build_params)
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
        build_params.elem_stats_flags, build_params.pipeline_status, build_params.timeout);
    CHECK_EXPECTED(softmax_element);
    elements.push_back(softmax_element.value());
    return softmax_element;
}

Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> VStreamsBuilderUtils::add_nms_to_detections_convert_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    const ElementBuildParams &build_params)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    auto nms_to_detections_element = ConvertNmsToDetectionsElement::create(metadata->nms_info(),
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        build_params);
    CHECK_EXPECTED(nms_to_detections_element);
    elements.push_back(nms_to_detections_element.value());
    return nms_to_detections_element;
}

Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> VStreamsBuilderUtils::add_remove_overlapping_bboxes_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    const ElementBuildParams &build_params)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    auto remove_overlapping_bboxes_element = RemoveOverlappingBboxesElement::create(metadata->nms_config(),
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        build_params);
    CHECK_EXPECTED(remove_overlapping_bboxes_element);
    elements.push_back(remove_overlapping_bboxes_element.value());
    return remove_overlapping_bboxes_element;
}

Expected<std::shared_ptr<FillNmsFormatElement>> VStreamsBuilderUtils::add_fill_nms_format_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
        const ElementBuildParams &build_params, const hailo_format_order_t &dst_format_order)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    auto fill_nms_format_element = FillNmsFormatElement::create(metadata->nms_config(),
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        build_params, dst_format_order);
    CHECK_EXPECTED(fill_nms_format_element);
    elements.push_back(fill_nms_format_element.value());
    return fill_nms_format_element;
}

Expected<std::shared_ptr<UserBufferQueueElement>> VStreamsBuilderUtils::add_user_buffer_queue_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, const hailo_vstream_params_t &vstream_params, size_t frame_size)
{
    auto post_argmax_queue_element = UserBufferQueueElement::create(
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params, frame_size, pipeline_status);
    CHECK_EXPECTED(post_argmax_queue_element);
    elements.push_back(post_argmax_queue_element.value());
    return post_argmax_queue_element;
}

Expected<std::shared_ptr<PostInferElement>> VStreamsBuilderUtils::add_post_infer_element(std::shared_ptr<OutputStreamBase> &output_stream,
    std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
    const std::string &element_name, const hailo_vstream_params_t &vstream_params)
{
    auto post_infer_element = PostInferElement::create(output_stream->get_info().hw_shape, output_stream->get_info().format,
        output_stream->get_info().shape, vstream_params.user_buffer_format, output_stream->get_quant_infos(), output_stream->get_info().nms_info,
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        vstream_params, pipeline_status);
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

    ElementBuildParams build_params{};
    build_params.elem_stats_flags = hw_read_element_stats_flags;
    build_params.pipeline_status = pipeline_status;
    build_params.timeout = std::chrono::milliseconds(HAILO_INFINITE);
    build_params.vstream_stats_flags = hw_read_stream_stats_flags;
    build_params.shutdown_event = nullptr;
    build_params.buffer_pool_size_edges = buffer_pool_size;

    auto hw_read_element = add_hw_read_element(output_stream, elements, "HwReadEl", build_params);
    CHECK_EXPECTED(hw_read_element);

    assert(1 == vstreams_params_map.size());
    auto op_input_format = argmax_op_metadata->inputs_metadata().begin()->second.format;
    auto vstream_params = vstreams_params_map.begin()->second;
    vstream_params.user_buffer_format = net_flow::ArgmaxOpMetadata::expand_output_format_autos(vstream_params.user_buffer_format, op_input_format);

    auto hw_read_queue_element = add_pull_queue_element(output_stream, pipeline_status, elements, "PullQEl_hw_read",
        vstream_params, output_stream->get_frame_size());
    CHECK_EXPECTED(hw_read_queue_element);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_element.value(), hw_read_queue_element.value()));

    auto argmax_element = add_argmax_element(output_stream, elements, "ArgmaxPPEl",
        vstream_params, argmax_op_metadata, build_params);
    CHECK_EXPECTED(argmax_element);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(hw_read_queue_element.value(), argmax_element.value()));

    const auto final_frame_size = HailoRTCommon::get_frame_size(output_vstream_info,
        vstream_params.user_buffer_format);

    auto post_argmax_queue_element = add_user_buffer_queue_element(output_stream, pipeline_status, elements,
        "UserBuffQEl_post_argmax", vstream_params, final_frame_size);
    CHECK_EXPECTED(post_argmax_queue_element);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(argmax_element.value(), post_argmax_queue_element.value()));

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstream_params);
    CHECK_EXPECTED(pipeline_latency_accumulator);

    output_stream->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
    hw_read_queue_element->get()->set_timeout(std::chrono::milliseconds(HAILO_INFINITE));
    auto vstream = OutputVStream::create(output_vstream_info, output_stream->get_quant_infos(), vstream_params, post_argmax_queue_element.release(), std::move(elements),
        std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator.release());
    CHECK_EXPECTED(vstream);
    vstreams.emplace_back(vstream.release());

    for (const auto &current_vstream : vstreams) {
        LOGGER__INFO("{}", current_vstream.get_pipeline_description());
    }

    return vstreams;
}

hailo_status VStreamsBuilderUtils::handle_pix_buffer_splitter_flow(std::vector<std::shared_ptr<InputStreamBase>> streams,
    const hailo_vstream_info_t &vstream_info, std::vector<std::shared_ptr<PipelineElement>> &&base_elements,
    std::vector<InputVStream> &vstreams, const hailo_vstream_params_t &vstream_params,
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

    auto planes_splitter = PixBufferElement::create(PipelineObject::create_element_name("PixBufferEl",
        vstream_info.name, 0), std::chrono::milliseconds(HAILO_INFINITE), duration_collector_expected.release(),
        pipeline_status, vstream_info.format.order);
    CHECK_EXPECTED_AS_STATUS(planes_splitter);
    base_elements.push_back(planes_splitter.value());

    uint32_t stream_number = 0;

    for (const auto &stream : streams){
         auto hw_write_elem = HwWriteElement::create(stream,
            PipelineObject::create_element_name("HwWriteEl", stream->name(), stream->get_info().index),
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
                dst_image_shape, dst_format, quant_infos, PipelineObject::create_element_name("PreInferEl",
                stream->get_info().name, stream->get_info().index), vstream_params, pipeline_status);

            CHECK_EXPECTED_AS_STATUS(pre_infer_elem);
            base_elements.push_back(pre_infer_elem.value());

            auto queue_elem = PushQueueElement::create(
                PipelineObject::create_element_name("PushQEl", stream_info.name, stream_info.index),
                vstream_params, stream_info.hw_frame_size, pipeline_status);

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
        nullptr, std::move(base_elements), std::move(pipeline_status), core_op_activated_event, accumalator);
    CHECK_EXPECTED_AS_STATUS(vstream);
    vstreams.emplace_back(vstream.release());

    return HAILO_SUCCESS;
}

hailo_status VStreamsBuilderUtils::add_demux(std::shared_ptr<OutputStreamBase> output_stream, NameToVStreamParamsMap &vstreams_params_map,
    std::vector<std::shared_ptr<PipelineElement>> &&base_elements, std::vector<OutputVStream> &vstreams,
    std::shared_ptr<PipelineElement> last_elem, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
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

    auto pull_queue_elem = PullQueueElement::create("PreDemuxPullQEl", HAILO_INFINITE_TIMEOUT,
        buffer_pool_size, output_stream->get_frame_size(), demux_elem_stats_flags, demux_vstream_stats_flags,
        pipeline_status);
    CHECK_EXPECTED_AS_STATUS(pull_queue_elem);
    base_elements.push_back(pull_queue_elem.value());
    CHECK_SUCCESS(PipelinePad::link_pads(last_elem, pull_queue_elem.value()));
    last_elem = pull_queue_elem.release();

    auto demux_elem = TransformDemuxElement::create(demuxer_ptr,
        PipelineObject::create_element_name("TransformDemuxEl", output_stream->name(), output_stream->get_info().index),
        std::chrono::milliseconds(HAILO_INFINITE), demux_elem_stats_flags, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(demux_elem);
    base_elements.push_back(demux_elem.value());
    CHECK_SUCCESS(PipelinePad::link_pads(last_elem, demux_elem.value()));

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
            PipelineObject::create_element_name("PullQueueEl_demux", edge_info.name, edge_info.index),
            vstream_params, edge_info.hw_frame_size, pipeline_status);
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
                PipelineObject::create_element_name("PostInferEl", edge_info.name, edge_info.index),
                vstream_params, pipeline_status);
            CHECK_EXPECTED_AS_STATUS(post_infer_elem);
            current_vstream_elements.push_back(post_infer_elem.value());
            CHECK_SUCCESS(PipelinePad::link_pads(demux_queue_elem.value(), post_infer_elem.value()));

            auto post_transform_frame_size = HailoRTCommon::get_frame_size(edge_info.shape, vstream_params.user_buffer_format);
            auto post_infer_queue_elem = UserBufferQueueElement::create(
                PipelineObject::create_element_name("UserBuffQEl_post_infer", edge_info.name, edge_info.index),
                vstream_params, post_transform_frame_size, pipeline_status);
            CHECK_EXPECTED_AS_STATUS(post_infer_queue_elem);
            current_vstream_elements.push_back(post_infer_queue_elem.value());
            CHECK_SUCCESS(PipelinePad::link_pads(post_infer_elem.value(), post_infer_queue_elem.value()));

            // TODO: Replace output_stream->get_quant_infos() with mux quant info
            auto vstream = OutputVStream::create(vstream_info->second, output_stream->get_quant_infos(), vstream_params, post_infer_queue_elem.release(), std::move(current_vstream_elements), // TODO: Get quant vector (HRT-11077)
                std::move(pipeline_status_copy), core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED_AS_STATUS(vstream);
            vstreams.emplace_back(vstream.release());
        } else {
            // TODO: HRT-4179
            auto user_copy_elem = CopyBufferElement::create(
                PipelineObject::create_element_name("CopyBufferEl", edge_info.name, edge_info.index),
                pipeline_status, std::chrono::milliseconds(vstream_params.timeout_ms));
            CHECK_EXPECTED_AS_STATUS(user_copy_elem);
            current_vstream_elements.push_back(user_copy_elem.value());
            CHECK_SUCCESS(PipelinePad::link_pads(demux_queue_elem.value(), user_copy_elem.value()));

            // TODO: Replace output_stream->get_quant_infos() with mux quant info
            auto vstream = OutputVStream::create(vstream_info->second, { edge_info.quant_info }, vstream_params, user_copy_elem.release(), std::move(current_vstream_elements), // TODO: Get quant vector (HRT-11077)
                std::move(pipeline_status_copy), core_op_activated_event, pipeline_latency_accumulator.release());
            CHECK_EXPECTED_AS_STATUS(vstream);
            vstreams.emplace_back(vstream.release());
        }
        i++;
    }
    return HAILO_SUCCESS;
}

hailo_status VStreamsBuilderUtils::add_nms_fuse(OutputStreamPtrVector &output_streams, hailo_vstream_params_t &vstreams_params,
    std::vector<std::shared_ptr<PipelineElement>> &elements, std::vector<OutputVStream> &vstreams,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
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
        PipelineObject::create_element_name("NmsMuxEl", fused_layer_name, 0),
        vstreams_params, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(nms_elem);
    auto fused_layer_nms_info = nms_elem.value()->get_fused_nms_info();

    for (uint32_t i = 0; i < output_streams.size(); ++i) {

	    ElementBuildParams build_params{};
        build_params.elem_stats_flags = vstreams_params.pipeline_elements_stats_flags;
        build_params.pipeline_status = pipeline_status;
        build_params.timeout = std::chrono::milliseconds(HAILO_INFINITE);
        build_params.vstream_stats_flags = vstreams_params.vstream_stats_flags;
        build_params.shutdown_event = nullptr;
        build_params.buffer_pool_size_edges = vstreams_params.queue_size;

        const auto &curr_stream_info = output_streams[i]->get_info();
        output_streams[i]->set_timeout(HAILO_INFINITE_TIMEOUT);

        auto hw_read_elem = HwReadElement::create(output_streams[i],
            PipelineObject::create_element_name("HwReadEl", curr_stream_info.name, curr_stream_info.index),
            build_params);
        CHECK_EXPECTED_AS_STATUS(hw_read_elem);
        elements.push_back(hw_read_elem.value());

        auto nms_source_queue_elem = PullQueueElement::create(
            PipelineObject::create_element_name("PullQueueEl_nms_source", curr_stream_info.name, curr_stream_info.index),
            vstreams_params, curr_stream_info.hw_frame_size, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(nms_source_queue_elem);
        elements.push_back(nms_source_queue_elem.value());
        nms_source_queue_elem.value()->set_timeout(HAILO_INFINITE_TIMEOUT);
        CHECK_SUCCESS(PipelinePad::link_pads(hw_read_elem.value(), nms_source_queue_elem.value()));
        CHECK_SUCCESS(PipelinePad::link_pads(nms_source_queue_elem.value(), nms_elem.value(), 0, i));
    }
    elements.push_back(nms_elem.value());

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstreams_params);
    CHECK_EXPECTED_AS_STATUS(pipeline_latency_accumulator);

    EventPtr core_op_activated_event = nullptr;
    if (!output_streams[0]->is_scheduled()) {
        core_op_activated_event = output_streams[0]->get_core_op_activated_event();
    }

    auto pre_transform_frame_size = HailoRTCommon::get_nms_hw_frame_size(fused_layer_nms_info);

    auto nms_queue_elem = PullQueueElement::create(
        PipelineObject::create_element_name("PullQEl_post_infer", fused_layer_name, 0),
        vstreams_params, pre_transform_frame_size, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(nms_queue_elem);
    nms_queue_elem.value()->set_timeout(HAILO_INFINITE_TIMEOUT);
    elements.push_back(nms_queue_elem.value());
    CHECK_SUCCESS(PipelinePad::link_pads(nms_elem.value(), nms_queue_elem.value()));

    auto post_infer_elem = PostInferElement::create({}, src_stream_format,
        {}, vstreams_params.user_buffer_format, { vstream_info->second.quant_info }, fused_layer_nms_info, // TODO: Get quant vector (HRT-11078)
        PipelineObject::create_element_name("PostInferEl", fused_layer_name, 0), vstreams_params, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(post_infer_elem);

    elements.push_back(post_infer_elem.value());
    CHECK_SUCCESS(PipelinePad::link_pads(nms_queue_elem.value(), post_infer_elem.value()));

    auto post_transform_frame_size = HailoRTCommon::get_nms_by_class_host_frame_size(fused_layer_nms_info, vstreams_params.user_buffer_format);
    auto post_infer_queue_elem = UserBufferQueueElement::create(
        PipelineObject::create_element_name("UserBufQEl_post_infer", fused_layer_name, 0),
        vstreams_params, post_transform_frame_size, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(post_infer_queue_elem);
    elements.push_back(post_infer_queue_elem.value());
    CHECK_SUCCESS(PipelinePad::link_pads(post_infer_elem.value(), post_infer_queue_elem.value()));

    // TODO: Check with SDK where should we take the quant infos from (output_streams[0]->get_quant_infos() might be good) (HRT-11078)
    auto vstream = OutputVStream::create(vstream_info->second, output_streams[0]->get_quant_infos(), vstreams_params, post_infer_queue_elem.release(), std::move(elements), // TODO: Get quant vector (HRT-11078)
        std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator.release());
    CHECK_EXPECTED_AS_STATUS(vstream);
    vstreams.emplace_back(vstream.release());

    return HAILO_SUCCESS;
}

hailo_status VStreamsBuilderUtils::add_nms_post_process(OutputStreamPtrVector &output_streams, hailo_vstream_params_t &vstreams_params,
    std::vector<std::shared_ptr<PipelineElement>> &elements, std::vector<OutputVStream> &vstreams,
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status, const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos,
    const std::shared_ptr<hailort::net_flow::Op> &nms_op)
{
    auto first_stream_info = output_streams[0]->get_info();
    auto op_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(nms_op->metadata());
    assert(nullptr != op_metadata);
    vstreams_params.user_buffer_format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(
        vstreams_params.user_buffer_format, nms_op->metadata()->type(), op_metadata->nms_config().bbox_only);
    CHECK_SUCCESS(op_metadata->validate_format_type(vstreams_params.user_buffer_format));

    if (!op_metadata->nms_config().bbox_only) {
        CHECK(HailoRTCommon::is_nms(vstreams_params.user_buffer_format.order), HAILO_INVALID_ARGUMENT,
            "NMS output format order must be one of NMS format orders");
    }

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

    auto nms_elem = NmsPostProcessMuxElement::create(nms_op,
        PipelineObject::create_element_name("NmsPPMuxEl", nms_op->get_name(), 0),
        vstreams_params, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(nms_elem);

    hailo_format_t nms_src_format;
    nms_src_format.flags = HAILO_FORMAT_FLAGS_NONE;
    nms_src_format.order = HAILO_FORMAT_ORDER_NHCW;
    nms_src_format.type = first_stream_info.format.type;

    for (uint32_t i = 0; i < output_streams.size(); ++i) {

        ElementBuildParams build_params{};
        build_params.elem_stats_flags = vstreams_params.pipeline_elements_stats_flags;
        build_params.pipeline_status = pipeline_status;
        build_params.timeout = std::chrono::milliseconds(HAILO_INFINITE);
        build_params.vstream_stats_flags = vstreams_params.vstream_stats_flags;
        build_params.shutdown_event = nullptr;
        build_params.buffer_pool_size_edges = vstreams_params.queue_size;

        const auto &curr_stream_info = output_streams[i]->get_info();
        output_streams[i]->set_timeout(HAILO_INFINITE_TIMEOUT);

        auto should_transform = OutputTransformContext::is_transformation_required(curr_stream_info.hw_shape, curr_stream_info.format,
            curr_stream_info.hw_shape, nms_src_format, output_streams[i]->get_quant_infos());
        CHECK_EXPECTED_AS_STATUS(should_transform);

        CHECK(!(should_transform.value()), HAILO_INVALID_ARGUMENT, "Unexpected transformation required for {}", curr_stream_info.name);

        auto hw_read_elem = HwReadElement::create(output_streams[i],
            PipelineObject::create_element_name("HwReadEl", curr_stream_info.name, curr_stream_info.index),
            build_params);
        CHECK_EXPECTED_AS_STATUS(hw_read_elem);
        elements.push_back(hw_read_elem.value());

        auto nms_source_queue_elem = PullQueueElement::create(
            PipelineObject::create_element_name("PullQEl_nms", curr_stream_info.name, curr_stream_info.index),
            vstreams_params, curr_stream_info.hw_frame_size, pipeline_status);
        CHECK_EXPECTED_AS_STATUS(nms_source_queue_elem);
        nms_source_queue_elem.value()->set_timeout(HAILO_INFINITE_TIMEOUT);
        elements.push_back(nms_source_queue_elem.value());
        CHECK_SUCCESS(PipelinePad::link_pads(hw_read_elem.value(), nms_source_queue_elem.value()));
        CHECK_SUCCESS(PipelinePad::link_pads(nms_source_queue_elem.value(), nms_elem.value(), 0, i));
        nms_elem.value()->add_sink_name(curr_stream_info.name, i);
    }
    elements.push_back(nms_elem.value());

    uint32_t post_transform_frame_size;
    if (op_metadata->nms_config().bbox_only) {
        post_transform_frame_size = HailoRTCommon::get_frame_size(vstream_info->second.shape, vstream_info->second.format);
    } else {
        post_transform_frame_size = HailoRTCommon::get_nms_host_frame_size(vstream_info->second.nms_shape, vstreams_params.user_buffer_format);
    }
    auto user_buffer_elem = UserBufferQueueElement::create(
        PipelineObject::create_element_name("UserBufQEl_post_infer", vstream_info->first, 0),
        vstreams_params, post_transform_frame_size, pipeline_status);
    CHECK_EXPECTED_AS_STATUS(user_buffer_elem);
    elements.push_back(user_buffer_elem.value());
    CHECK_SUCCESS(PipelinePad::link_pads(nms_elem.value(), user_buffer_elem.value()));

    auto pipeline_latency_accumulator = create_pipeline_latency_accumulator(vstreams_params);
    CHECK_EXPECTED_AS_STATUS(pipeline_latency_accumulator);

    EventPtr core_op_activated_event = nullptr;
    if (!output_streams[0]->is_scheduled()) {
        core_op_activated_event = output_streams[0]->get_core_op_activated_event();
    }

    // If user uses HailoRT++ we can assume he won't use Output Scale by Feature
    auto vstream = OutputVStream::create(vstream_info->second, output_streams[0]->get_quant_infos(), vstreams_params, nms_elem.release(), std::move(elements),
        std::move(pipeline_status), core_op_activated_event, pipeline_latency_accumulator.release());
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
