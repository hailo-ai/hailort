/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_pipeline_builder.cpp
 * @brief Async pipeline builder impl
 **/

#include "async_pipeline_builder.hpp"
#include "hailo/hailort.h"
#include "net_flow/ops/yolov5_seg_post_process.hpp"
#include "net_flow/ops/yolov5_bbox_only_post_process.hpp"
#include "net_flow/ops/yolov8_post_process.hpp"
#include "net_flow/ops/yolov8_bbox_only_post_process.hpp"
#include "net_flow/ops/argmax_post_process.hpp"
#include "net_flow/ops/softmax_post_process.hpp"
#include "net_flow/ops/yolox_post_process.hpp"
#include "net_flow/ops/ssd_post_process.hpp"
#include "net_flow/pipeline/vstream_builder.hpp"
#include <algorithm>

namespace hailort
{

Expected<std::unordered_map<std::string, hailo_format_t>> AsyncPipelineBuilder::expand_auto_input_formats(std::shared_ptr<ConfiguredNetworkGroup>net_group,
    const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    std::unordered_map<std::string, hailo_format_t> expanded_input_format;
    for (auto &input_format : inputs_formats) {
        TRY(const auto input_streams_names, net_group->get_stream_names_from_vstream_name(input_format.first));

        auto is_multi_planar = (input_streams_names.size() > 1);
        if(is_multi_planar) {
            TRY(const auto vstream_infos, net_group->get_input_vstream_infos());
            auto matching_vstream_info = std::find_if(vstream_infos.begin(), vstream_infos.end(), [&](const auto &item)
                { return item.name == input_format.first; } );
            CHECK_AS_EXPECTED(vstream_infos.end() != matching_vstream_info, HAILO_NOT_FOUND,
                "Could not find input layer with name '{}'", input_format.first);
            expanded_input_format[input_format.first] =
                VStreamsBuilderUtils::expand_user_buffer_format_autos_multi_planar(*matching_vstream_info, input_format.second);
        } else {
            const auto &stream_name = input_streams_names[0];
            CHECK_AS_EXPECTED(contains(named_stream_infos, stream_name), HAILO_INTERNAL_FAILURE);
            const auto &stream_info = named_stream_infos.at(stream_name);

            if (IS_PP_DISABLED()) {
                expanded_input_format[input_format.first] = stream_info.format;
            } else {
                expanded_input_format[input_format.first] = HailoRTDefaults::expand_auto_format(input_format.second,
                    stream_info.format);
            }
        }
    }
    return expanded_input_format;
}

Expected<std::unordered_map<std::string, hailo_format_t>> AsyncPipelineBuilder::expand_auto_output_formats(std::shared_ptr<ConfiguredNetworkGroup> net_group,
    const std::unordered_map<std::string, hailo_format_t> &outputs_formats, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    std::unordered_map<std::string, hailo_format_t> expanded_output_format;
    for (auto &output_format : outputs_formats) {
        TRY(const auto output_streams_names, net_group->get_stream_names_from_vstream_name(output_format.first));

        // TODO: Taking data from the first ll stream will not work in multi-planar work
        const auto &stream_name = output_streams_names[0];
        CHECK_AS_EXPECTED(contains(named_stream_infos, stream_name), HAILO_INTERNAL_FAILURE);
        const auto &stream_info = named_stream_infos.at(stream_name);

        if (IS_PP_DISABLED()) {
            expanded_output_format[output_format.first] = stream_info.format;
        } else {
            expanded_output_format[output_format.first] = HailoRTDefaults::expand_auto_format(output_format.second,
                stream_info.format);
        }
    }
    return expanded_output_format;
}

hailo_status AsyncPipelineBuilder::create_pre_async_hw_elements_per_input(std::shared_ptr<ConfiguredNetworkGroup> net_group,
    const std::vector<std::string> &stream_names, const std::unordered_map<std::string, hailo_format_t> &inputs_formats,
    const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    TRY(const auto vstream_names, net_group->get_vstream_names_from_stream_name(*stream_names.begin()));
    CHECK(vstream_names.size() == 1, HAILO_NOT_SUPPORTED, "low level stream must have exactly 1 user input");
    const auto &vstream_name = vstream_names[0];
    std::shared_ptr<PixBufferElement> multi_plane_splitter = nullptr;
    std::shared_ptr<PipelineElement> last_element_connected_to_pipeline = nullptr;

    auto is_empty = true;
    auto interacts_with_hw = true; // We want the entry queue size to be the size of queues interacts with HW
    auto is_entry = true;
    TRY(auto entry_queue_elem,
        add_push_queue_element(PipelineObject::create_element_name("EntryPushQEl", vstream_name, 0),
            async_pipeline, 0, is_empty, interacts_with_hw, nullptr, 0, is_entry));
    async_pipeline->add_entry_element(entry_queue_elem, vstream_name);
    last_element_connected_to_pipeline = entry_queue_elem;

    bool is_multi_planar = (stream_names.size() > 1);
    if (is_multi_planar) {
        async_pipeline->set_as_multi_planar();
        const auto &vstream_order = inputs_formats.at(vstream_name).order;

        TRY(multi_plane_splitter, create_multi_plane_splitter_element(vstream_name, vstream_order,
            async_pipeline->get_build_params().pipeline_status, async_pipeline));

        async_pipeline->add_element_to_pipeline(multi_plane_splitter);
        CHECK_SUCCESS(PipelinePad::link_pads(entry_queue_elem, multi_plane_splitter));
    }

    uint8_t plane_index = 0;
    for (const auto &stream_name : stream_names) {
        CHECK(contains(named_stream_infos, stream_name), HAILO_INTERNAL_FAILURE);
        const auto &input_stream_info = named_stream_infos.at(stream_name);
        auto src_format = inputs_formats.at(vstream_name);
        if (is_multi_planar) {
            /* In multi-planar case, the format order of each plane (stream) is determined by the ll-stream's order.
               Type and flags are determined by the vstream params */
            src_format.order = input_stream_info.format.order;
        }

        TRY(const auto sink_index, async_pipeline->get_async_hw_element()->get_sink_index_from_input_stream_name(stream_name));
        TRY(const auto should_transform, InputTransformContext::is_transformation_required(input_stream_info.shape,
            src_format, input_stream_info.hw_shape, input_stream_info.format,
            std::vector<hailo_quant_info_t>(1, input_stream_info.quant_info))); // Inputs always have single quant_info

        if(is_multi_planar) {
            is_empty = true; // pix buffer splitter doesnt do any copy, so no need to allocate buffers for its following queues
            interacts_with_hw = !should_transform; // If not doing transformations, this queue interacts with HW elem
            TRY(auto post_split_push_queue, add_push_queue_element(
                PipelineObject::create_element_name("PostSplitPushQEl", stream_name, sink_index),
                async_pipeline, 0, is_empty, interacts_with_hw, nullptr));
            CHECK_SUCCESS(PipelinePad::link_pads(multi_plane_splitter, post_split_push_queue, plane_index++));

            last_element_connected_to_pipeline = post_split_push_queue;
        }

        if (should_transform) {
            TRY(auto pre_infer_elem, PreInferElement::create(input_stream_info.shape, src_format,
                input_stream_info.hw_shape, input_stream_info.format, { input_stream_info.quant_info },
                PipelineObject::create_element_name("PreInferEl", stream_name, input_stream_info.index),
                async_pipeline->get_build_params(), PipelineDirection::PUSH, async_pipeline));
            async_pipeline->add_element_to_pipeline(pre_infer_elem);
            CHECK_SUCCESS(PipelinePad::link_pads(last_element_connected_to_pipeline, pre_infer_elem));

            is_empty = false;
            interacts_with_hw = true;
            TRY(auto queue_elem, add_push_queue_element(PipelineObject::create_element_name("PushQEl", stream_name, input_stream_info.index),
                async_pipeline, input_stream_info.hw_frame_size, is_empty, interacts_with_hw, pre_infer_elem));
            CHECK_SUCCESS(PipelinePad::link_pads(pre_infer_elem, queue_elem));
            CHECK_SUCCESS(PipelinePad::link_pads(queue_elem, async_pipeline->get_async_hw_element(), 0, sink_index));
        } else {
            CHECK_SUCCESS(PipelinePad::link_pads(last_element_connected_to_pipeline, async_pipeline->get_async_hw_element(), 0, sink_index));
        }
    }

    return HAILO_SUCCESS;
}

hailo_status AsyncPipelineBuilder::create_pre_async_hw_elements(std::shared_ptr<ConfiguredNetworkGroup> net_group,
    const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos,
    std::shared_ptr<AsyncPipeline> async_pipeline)
{
    for(const auto &input : inputs_formats) {
        TRY(const auto stream_names_under_vstream,
            net_group->get_stream_names_from_vstream_name(input.first));

        auto status = create_pre_async_hw_elements_per_input(net_group, stream_names_under_vstream, inputs_formats,
            named_stream_infos, async_pipeline);
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<PostInferElement>> AsyncPipelineBuilder::add_post_infer_element(const hailo_format_t &output_format,
    const hailo_nms_info_t &nms_info, std::shared_ptr<AsyncPipeline> async_pipeline, const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const std::vector<hailo_quant_info_t> &dst_quant_infos,
    std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index)
{
    auto pre_transform_frame_size = (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == src_format.order) ?
        HailoRTCommon::get_nms_hw_frame_size(nms_info) : HailoRTCommon::get_periph_frame_size(src_image_shape, src_format);
    auto is_empty = false;
    auto interacts_with_hw = true;
    TRY(auto queue_elem, add_push_queue_element(PipelineObject::create_element_name("PushQEl", final_elem->name(),
        static_cast<uint8_t>(final_elem_source_index)), async_pipeline, pre_transform_frame_size, is_empty, interacts_with_hw,
        final_elem, final_elem_source_index));

    TRY(auto post_infer_elem, PostInferElement::create(src_image_shape, src_format, dst_image_shape, output_format,
        dst_quant_infos, nms_info, PipelineObject::create_element_name("PostInferEl",
        final_elem->name(), static_cast<uint8_t>(final_elem_source_index)), async_pipeline->get_build_params(),
        PipelineDirection::PUSH, async_pipeline));

    async_pipeline->add_element_to_pipeline(post_infer_elem);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(queue_elem, post_infer_elem));
    return post_infer_elem;
}

Expected<std::shared_ptr<AsyncPushQueueElement>> AsyncPipelineBuilder::add_push_queue_element(const std::string &queue_name, std::shared_ptr<AsyncPipeline> async_pipeline,
    size_t frame_size, bool is_empty, bool interacts_with_hw, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index, bool is_entry)
{
    TRY(auto push_queue_elem, AsyncPushQueueElement::create(queue_name, async_pipeline->get_build_params(), frame_size,
        is_empty, interacts_with_hw, async_pipeline, is_entry));

    async_pipeline->add_element_to_pipeline(push_queue_elem);

    // final elem will be nullptr in case it's the first element in pipeline
    if (final_elem) {
        CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, push_queue_elem, final_elem_source_index, 0));
    }

    return push_queue_elem;
}

Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> AsyncPipelineBuilder::add_nms_to_detections_convert_element(std::shared_ptr<AsyncPipeline> async_pipeline,
    const std::string &output_stream_name, uint8_t stream_index, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_index)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    TRY(auto nms_to_detections_element, ConvertNmsToDetectionsElement::create(metadata->nms_info(),
        PipelineObject::create_element_name(element_name, output_stream_name, stream_index),
        async_pipeline->get_build_params(), PipelineDirection::PUSH, async_pipeline));

    async_pipeline->add_element_to_pipeline(nms_to_detections_element);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, nms_to_detections_element, final_elem_index, 0));
    return nms_to_detections_element;
}

Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> AsyncPipelineBuilder::add_remove_overlapping_bboxes_element(std::shared_ptr<AsyncPipeline> async_pipeline,
    const std::string &output_stream_name, uint8_t stream_index, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_index)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    TRY(auto remove_overlapping_bboxes_element, RemoveOverlappingBboxesElement::create(metadata->nms_config(),
        PipelineObject::create_element_name(element_name, output_stream_name, stream_index),
        async_pipeline->get_build_params(), PipelineDirection::PUSH, async_pipeline));

    async_pipeline->add_element_to_pipeline(remove_overlapping_bboxes_element);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, remove_overlapping_bboxes_element, final_elem_index, 0));
    return remove_overlapping_bboxes_element;
}

Expected<std::shared_ptr<FillNmsFormatElement>> AsyncPipelineBuilder::add_fill_nms_format_element(std::shared_ptr<AsyncPipeline> async_pipeline,
    const std::string &output_stream_name, uint8_t stream_index, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    std::shared_ptr<PipelineElement> final_elem, const hailo_format_order_t &dst_format_order, const uint32_t final_elem_index)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    TRY(auto fill_nms_format_element, FillNmsFormatElement::create(metadata->nms_config(),
        PipelineObject::create_element_name(element_name, output_stream_name, stream_index),
        async_pipeline->get_build_params(), dst_format_order, PipelineDirection::PUSH, async_pipeline));

    async_pipeline->add_element_to_pipeline(fill_nms_format_element);

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, fill_nms_format_element, final_elem_index, 0));
    return fill_nms_format_element;
}

Expected<std::shared_ptr<LastAsyncElement>> AsyncPipelineBuilder::add_last_async_element(std::shared_ptr<AsyncPipeline> async_pipeline,
    const std::string &output_format_name, size_t frame_size, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index)
{
    TRY(auto last_async_element, LastAsyncElement::create(PipelineObject::create_element_name("LastAsyncEl",
        final_elem->name(), static_cast<uint8_t>(final_elem_source_index)), async_pipeline->get_build_params(), frame_size, async_pipeline));

    async_pipeline->add_element_to_pipeline(last_async_element);
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, last_async_element, final_elem_source_index, 0));

    async_pipeline->add_last_element(last_async_element, output_format_name);

    return last_async_element;
}

Expected<std::pair<std::string, hailo_format_t>> AsyncPipelineBuilder::get_output_format_from_edge_info_name(const std::string &edge_info_name,
    const std::unordered_map<std::string, hailo_format_t> &outputs_formats)
{
    for (auto &output_format : outputs_formats) {
        if (output_format.first == edge_info_name) {
            return std::pair<std::string, hailo_format_t>(output_format);
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

hailo_status AsyncPipelineBuilder::add_output_demux_flow(const std::string &output_stream_name, std::shared_ptr<AsyncPipeline> async_pipeline,
    const std::unordered_map<std::string, hailo_format_t> &outputs_formats, std::shared_ptr<ConfiguredNetworkGroup> net_group,
    const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    CHECK(contains(named_stream_infos, output_stream_name), HAILO_INTERNAL_FAILURE);
    const auto &stream_info = named_stream_infos.at(output_stream_name);

    TRY(const auto source_index,
        async_pipeline->get_async_hw_element()->get_source_index_from_output_stream_name(output_stream_name));

    auto is_empty = false;
    auto interacts_with_hw = true;
    TRY_V(auto hw_queue_elem,
            add_push_queue_element(PipelineObject::create_element_name("PushQueueElement_post_hw", stream_info.name, stream_info.index),
            async_pipeline, stream_info.hw_frame_size, is_empty, interacts_with_hw, async_pipeline->get_async_hw_element(), source_index));

    TRY(const auto layer_info, net_group->get_layer_info(output_stream_name));

    TRY(auto demuxer, OutputDemuxerBase::create(stream_info.hw_frame_size, *layer_info));

    auto demuxer_ptr = make_shared_nothrow<OutputDemuxerBase>(std::move(demuxer));
    CHECK_ARG_NOT_NULL(demuxer_ptr);

    TRY(auto demux_elem, TransformDemuxElement::create(demuxer_ptr,
        PipelineObject::create_element_name("TransformDemuxEl", output_stream_name, stream_info.index),
        async_pipeline->get_build_params(), PipelineDirection::PUSH, async_pipeline));
    async_pipeline->add_element_to_pipeline(demux_elem);

    CHECK_SUCCESS(PipelinePad::link_pads(hw_queue_elem, demux_elem));

    uint8_t i = 0;
    for (auto &edge_info : demuxer_ptr->get_edges_stream_info()) {
        TRY(const auto output_format, get_output_format_from_edge_info_name(edge_info.name, outputs_formats));

        TRY(const auto should_transform, OutputTransformContext::is_transformation_required(edge_info.hw_shape, 
            edge_info.format, edge_info.shape, output_format.second, std::vector<hailo_quant_info_t>{edge_info.quant_info})); // TODO: Get quant vector (HRT-11077)

        if (should_transform) {
            is_empty = false;
            interacts_with_hw = false;
            TRY(auto demux_queue_elem, add_push_queue_element(PipelineObject::create_element_name("PushQEl_demux", edge_info.name, i), async_pipeline,
                edge_info.hw_frame_size, is_empty, interacts_with_hw, demux_elem, i));

            TRY(auto post_infer_elem, add_post_infer_element(output_format.second, edge_info.nms_info,
                async_pipeline, edge_info.hw_shape, edge_info.format, edge_info.shape, {edge_info.quant_info}, demux_queue_elem));

            auto post_transform_frame_size = (HailoRTCommon::is_nms(edge_info.format.order)) ?
                HailoRTCommon::get_nms_by_class_host_frame_size(edge_info.nms_info, output_format.second) :
                HailoRTCommon::get_frame_size(edge_info.shape, output_format.second);

            TRY(auto last_async_element, add_last_async_element(async_pipeline, output_format.first, post_transform_frame_size,
                post_infer_elem));
        } else {
            TRY(auto last_async_element, add_last_async_element(async_pipeline, output_format.first, edge_info.hw_frame_size,
                demux_elem, i));
        }
        i++;
    }
    return HAILO_SUCCESS;
}

Expected<bool> AsyncPipelineBuilder::should_transform(const hailo_stream_info_t &stream_info, const std::vector<hailo_quant_info_t> &stream_quant_infos, 
    const hailo_format_t &output_format)
{
    return OutputTransformContext::is_transformation_required(stream_info.hw_shape,
        stream_info.format, stream_info.shape, output_format, stream_quant_infos);
}

hailo_status AsyncPipelineBuilder::add_nms_fuse_flow(const std::vector<std::string> &output_streams_names,
    const std::pair<std::string, hailo_format_t> &output_format, std::shared_ptr<AsyncPipeline> async_pipeline,
    const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    std::vector<hailo_nms_info_t> nms_infos;
    nms_infos.reserve(output_streams_names.size());
    hailo_stream_info_t first_defused_stream_info = {};
    for (const auto &stream_name : output_streams_names) {
        CHECK(contains(named_stream_infos, stream_name), HAILO_INTERNAL_FAILURE);
        const auto &curr_stream_info = named_stream_infos.at(stream_name);

        CHECK(curr_stream_info.nms_info.defuse_info.class_group_index <= output_streams_names.size(),
            HAILO_INVALID_ARGUMENT, "Not all defused nms outputs were grouped correctly!");
        nms_infos.emplace_back(curr_stream_info.nms_info);
        first_defused_stream_info = curr_stream_info;
    }

    // To get the fused layer name and src stream format, we use the stream info of one of the defuses
    auto fused_layer_name = first_defused_stream_info.nms_info.defuse_info.original_name;

    TRY(auto nms_elem, NmsMuxElement::create(nms_infos, PipelineObject::create_element_name("NmsMuxEl", fused_layer_name, 0),
        async_pipeline->get_build_params(), PipelineDirection::PUSH, async_pipeline));
    async_pipeline->add_element_to_pipeline(nms_elem);

    auto is_empty = false;
    auto interacts_with_hw = true;
    std::vector<size_t> frame_sizes(output_streams_names.size());
    for (const auto &stream_name : output_streams_names) {
        CHECK(contains(named_stream_infos, stream_name), HAILO_INTERNAL_FAILURE);
        const auto &curr_stream_info = named_stream_infos.at(stream_name);

        TRY(const auto output_index, async_pipeline->get_async_hw_element()->get_source_index_from_output_stream_name(stream_name));
        frame_sizes[output_index] = curr_stream_info.hw_frame_size;
    }

    TRY(auto multi_queue_elem, MultiPushQueue::create(PipelineObject::create_element_name("MultiPushQEl", fused_layer_name, 0),
        async_pipeline->get_build_params(), frame_sizes, is_empty, interacts_with_hw, async_pipeline));
    async_pipeline->add_element_to_pipeline(multi_queue_elem);

    CHECK_SUCCESS(PipelinePad::link_pads(multi_queue_elem, nms_elem));

    for (uint32_t i = 0; i < output_streams_names.size(); i++) {
        TRY(const auto output_index, async_pipeline->get_async_hw_element()->get_source_index_from_output_stream_name(output_streams_names[i]));
        CHECK_SUCCESS(PipelinePad::link_pads(async_pipeline->get_async_hw_element(), multi_queue_elem, i, output_index));
    }

    // TODO(HRT-11078): Fix multi qp for fused NMS
    auto stream_quant_infos = std::vector<hailo_quant_info_t>(1, first_defused_stream_info.quant_info);

    // On NMS models we always need tp post-infer
    const auto &fused_layer_nms_info = nms_elem->get_fused_nms_info();

    TRY(auto post_infer_elem, add_post_infer_element(output_format.second, fused_layer_nms_info, async_pipeline,
        first_defused_stream_info.hw_shape, first_defused_stream_info.format, first_defused_stream_info.shape, stream_quant_infos, nms_elem));

    const auto post_transform_frame_size = HailoRTCommon::get_nms_by_class_host_frame_size(fused_layer_nms_info, output_format.second);

    TRY(auto last_async_element, add_last_async_element(async_pipeline, output_format.first, post_transform_frame_size,
        post_infer_elem));

    return HAILO_SUCCESS;
}

hailo_status AsyncPipelineBuilder::add_softmax_flow(std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
    const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata,
    const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    assert(output_streams_names.size() == 1);
    const auto &stream_name = *output_streams_names.begin();

    CHECK(contains(named_stream_infos, stream_name), HAILO_INTERNAL_FAILURE);
    const auto &stream_info = named_stream_infos.at(stream_name);

    auto updated_output_format = output_format;

    TRY(const auto hw_async_elem_index, async_pipeline->get_async_hw_element()->get_source_index_from_output_stream_name(stream_name));

    const auto op_input_format = softmax_op_metadata->inputs_metadata().begin()->second.format;
    const auto output_format_expanded = net_flow::SoftmaxOpMetadata::expand_output_format_autos(updated_output_format.second, op_input_format);

    // TODO (HRT-11078): Fix multi qp for PP
    auto stream_quant_infos = std::vector<hailo_quant_info_t>(1, stream_info.quant_info);

    TRY(auto post_infer_elem, add_post_infer_element(output_format_expanded, {}, async_pipeline, stream_info.hw_shape, stream_info.format,
        stream_info.shape, stream_quant_infos, async_pipeline->get_async_hw_element(), hw_async_elem_index));

    auto is_empty = false;
    auto interacts_with_hw = false;
    const auto post_transform_frame_size = HailoRTCommon::get_frame_size(stream_info.shape, output_format_expanded);
    TRY(auto queue_elem, add_push_queue_element(PipelineObject::create_element_name("PushQEl_softmax", async_pipeline->get_async_hw_element()->name(),
        static_cast<uint8_t>(hw_async_elem_index)), async_pipeline, post_transform_frame_size, is_empty, interacts_with_hw, post_infer_elem));

    // Updating metadata according to user request
    // Currently softmax only supports inputs to be float32 and order NHWC or NC
    auto updated_inputs_metadata = softmax_op_metadata.get()->inputs_metadata();
    updated_inputs_metadata.begin()->second.format = output_format_expanded;
    auto updated_outputs_metadata = softmax_op_metadata.get()->outputs_metadata();
    updated_outputs_metadata.begin()->second.format = output_format_expanded;
    auto metadata = std::dynamic_pointer_cast<net_flow::SoftmaxOpMetadata>(softmax_op_metadata);
    assert(nullptr != metadata);
    metadata->set_outputs_metadata(updated_outputs_metadata);
    metadata->set_inputs_metadata(updated_inputs_metadata);
    CHECK_SUCCESS(metadata->validate_format_info());

    TRY(auto softmax_op, net_flow::SoftmaxPostProcessOp::create(metadata));
    TRY(auto softmax_element, SoftmaxPostProcessElement::create(softmax_op,
        PipelineObject::create_element_name("SoftmaxPPEl", stream_name, stream_info.index),
        async_pipeline->get_build_params(), PipelineDirection::PUSH, async_pipeline));

    async_pipeline->add_element_to_pipeline(softmax_element);
    CHECK_SUCCESS(PipelinePad::link_pads(queue_elem, softmax_element));

    TRY(auto last_async_element, add_last_async_element(async_pipeline, updated_output_format.first, post_transform_frame_size,
        softmax_element));

    return HAILO_SUCCESS;
}

hailo_status AsyncPipelineBuilder::add_argmax_flow(std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
    const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata,
    const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    assert(output_streams_names.size() == 1);
    const auto &stream_name = *output_streams_names.begin();

    CHECK(contains(named_stream_infos, stream_name), HAILO_INTERNAL_FAILURE);
    const auto &stream_info = named_stream_infos.at(stream_name);

    TRY(const auto hw_async_elem_index, async_pipeline->get_async_hw_element()->get_source_index_from_output_stream_name(stream_name));

    auto is_empty = false;
    auto interacts_with_hw = true;
    TRY(auto queue_elem, add_push_queue_element(PipelineObject::create_element_name("PushQEl_argmax", async_pipeline->get_async_hw_element()->name(),
        static_cast<uint8_t>(hw_async_elem_index)), async_pipeline, stream_info.hw_frame_size, is_empty, interacts_with_hw,
        async_pipeline->get_async_hw_element(), hw_async_elem_index));

    // Updating metadata according to user request
    const auto op_input_format = argmax_op_metadata->inputs_metadata().begin()->second.format;
    auto updated_outputs_metadata = argmax_op_metadata.get()->outputs_metadata();
    updated_outputs_metadata.begin()->second.format = net_flow::ArgmaxOpMetadata::expand_output_format_autos(output_format.second, op_input_format);
    auto metadata = std::dynamic_pointer_cast<net_flow::ArgmaxOpMetadata>(argmax_op_metadata);
    assert(nullptr != metadata);
    metadata->set_outputs_metadata(updated_outputs_metadata);
    CHECK_SUCCESS(metadata->validate_format_info());

    TRY(auto argmax_op, net_flow::ArgmaxPostProcessOp::create(metadata));
    TRY(auto argmax_element, ArgmaxPostProcessElement::create(argmax_op,
        PipelineObject::create_element_name("ArgmaxPPEl", stream_name, stream_info.index),
        async_pipeline->get_build_params(), PipelineDirection::PUSH, async_pipeline));

    async_pipeline->add_element_to_pipeline(argmax_element);
    CHECK_SUCCESS(PipelinePad::link_pads(queue_elem, argmax_element));

    const auto post_transform_frame_size = HailoRTCommon::get_frame_size(updated_outputs_metadata.begin()->second.shape,
        updated_outputs_metadata.begin()->second.format);

    TRY(auto last_async_element, add_last_async_element(async_pipeline, output_format.first, post_transform_frame_size,
        argmax_element));

    return HAILO_SUCCESS;
}

hailo_status AsyncPipelineBuilder::add_nms_flow(std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
    const std::pair<std::string, hailo_format_t> &output_format, const std::shared_ptr<hailort::net_flow::Op> &nms_op,
    const hailo_vstream_info_t &vstream_info, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    const auto &first_stream_name = *output_streams_names.begin();
    CHECK(contains(named_stream_infos, first_stream_name), HAILO_INTERNAL_FAILURE);
    const auto &first_stream_info = named_stream_infos.at(first_stream_name);

    auto nms_op_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(nms_op->metadata());
    assert(nullptr != nms_op_metadata);

    CHECK_SUCCESS(nms_op_metadata->validate_format_type(output_format.second));

    if(!nms_op_metadata->nms_config().bbox_only){
        CHECK(HailoRTCommon::is_nms(output_format.second.order), HAILO_INVALID_ARGUMENT,
            "NMS output format order must be an NMS format order");
    }

    std::unordered_map<std::string, net_flow::BufferMetaData> inputs_metadata;
    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    for (uint32_t i = 0; i < output_streams_names.size(); ++i) {
        const auto &curr_stream_name = output_streams_names[i];
        CHECK(contains(named_stream_infos, curr_stream_name), HAILO_INTERNAL_FAILURE);
        const auto &curr_stream_info = named_stream_infos.at(curr_stream_name);

        net_flow::BufferMetaData input_metadata = {
            curr_stream_info.shape,
            curr_stream_info.hw_shape,
            curr_stream_info.format,
            curr_stream_info.quant_info
        };
        inputs_metadata.insert({curr_stream_info.name, input_metadata});
    }

    assert(nms_op->outputs_metadata().size() == 1);

    net_flow::BufferMetaData output_metadata = {
        vstream_info.shape,
        vstream_info.shape,
        vstream_info.format,
        vstream_info.quant_info
    };
    outputs_metadata.insert({nms_op->outputs_metadata().begin()->first, output_metadata});

    TRY(auto nms_elem,
        NmsPostProcessMuxElement::create(nms_op, PipelineObject::create_element_name("NmsPPMuxEl", nms_op->get_name(), 0),
            async_pipeline->get_build_params(), PipelineDirection::PUSH, async_pipeline));

    async_pipeline->add_element_to_pipeline(nms_elem);

    hailo_format_t nms_src_format = {};
    nms_src_format.flags = HAILO_FORMAT_FLAGS_NONE;
    nms_src_format.order = HAILO_FORMAT_ORDER_NHCW;
    nms_src_format.type = first_stream_info.format.type;
    std::vector<size_t> frame_sizes(output_streams_names.size());
    for (uint32_t i = 0; i < output_streams_names.size(); ++i) {
        const auto &curr_stream_name = output_streams_names[i];
        CHECK(contains(named_stream_infos, curr_stream_name), HAILO_INTERNAL_FAILURE);
        const auto &curr_stream_info = named_stream_infos.at(curr_stream_name);

        // TODO (HRT-11052): Fix multi qp for NMS
        auto stream_quant_infos = std::vector<hailo_quant_info_t>(1, curr_stream_info.quant_info); //output_stream_base->get_quant_infos();

        TRY(const auto should_transform,
            OutputTransformContext::is_transformation_required(curr_stream_info.hw_shape, curr_stream_info.format,
                curr_stream_info.hw_shape, nms_src_format, stream_quant_infos));

        CHECK(!(should_transform), HAILO_INVALID_ARGUMENT, "Unexpected transformation required for {}", curr_stream_name);

        TRY(const auto output_index, async_pipeline->get_async_hw_element()->get_source_index_from_output_stream_name(curr_stream_name));
        frame_sizes[output_index] = curr_stream_info.hw_frame_size;

        nms_elem->add_sink_name(curr_stream_name, output_index);
    }

    auto is_empty = false;
    auto interacts_with_hw = true;
    TRY(auto multi_queue_elem, MultiPushQueue::create(PipelineObject::create_element_name("MultiPushQEl", nms_op->get_name(), 0),
        async_pipeline->get_build_params(), frame_sizes, is_empty, interacts_with_hw, async_pipeline));
    async_pipeline->add_element_to_pipeline(multi_queue_elem);

    CHECK_SUCCESS(PipelinePad::link_pads(multi_queue_elem, nms_elem));

    for (uint32_t i = 0; i < output_streams_names.size(); i++) {
        CHECK_SUCCESS(PipelinePad::link_pads(async_pipeline->get_async_hw_element(), multi_queue_elem, i, i));
    }

    uint32_t post_transform_frame_size;
    if(nms_op_metadata->nms_config().bbox_only){
        post_transform_frame_size = HailoRTCommon::get_frame_size(vstream_info, output_format.second);
    } else {
        post_transform_frame_size = HailoRTCommon::get_nms_host_frame_size(vstream_info.nms_shape, output_format.second);
    }
    TRY(auto last_async_element,
        add_last_async_element(async_pipeline, output_format.first, post_transform_frame_size, nms_elem));

    return HAILO_SUCCESS;
}

hailo_status AsyncPipelineBuilder::add_iou_flow( std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
    const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata,
    const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    assert(output_streams_names.size() == 1);
    auto output_stream_name = output_streams_names[0];
    CHECK(contains(named_stream_infos, output_stream_name), HAILO_INTERNAL_FAILURE);
    const auto &output_stream_info = named_stream_infos.at(output_stream_name);

    // TODO (HRT-11078): Fix multi qp for PP
    auto stream_quant_infos = std::vector<hailo_quant_info_t>(1, output_stream_info.quant_info); //output_stream_base->get_quant_infos();

    TRY(auto post_infer_element, add_post_infer_element(output_format.second, output_stream_info.nms_info,
        async_pipeline, output_stream_info.hw_shape, output_stream_info.format, output_stream_info.shape, stream_quant_infos,
        async_pipeline->get_async_hw_element()));

    auto is_empty = false;
    auto interacts_with_hw = false;
    const auto post_transform_frame_size = HailoRTCommon::get_nms_by_class_host_frame_size(output_stream_info.nms_info, output_format.second);
    TRY(auto pre_nms_convert_queue_element,
        add_push_queue_element(PipelineObject::create_element_name("PushQEl_pre_nms_convert", output_stream_name,
            output_stream_info.index), async_pipeline, post_transform_frame_size, is_empty, interacts_with_hw, post_infer_element));

    TRY(auto nms_to_detections_element,
        add_nms_to_detections_convert_element(async_pipeline, output_stream_name, output_stream_info.index,
            "NmsFormatToDetectionsEl", iou_op_metadata, pre_nms_convert_queue_element));

    TRY(auto pre_remove_overlapping_bboxes_element_queue_element,
        add_push_queue_element(PipelineObject::create_element_name("PushQEl_pre_bboxes_removing",
            output_stream_name, output_stream_info.index), async_pipeline, 0, is_empty, interacts_with_hw, nms_to_detections_element));
    
    TRY(auto remove_overlapping_bboxes_element,
        add_remove_overlapping_bboxes_element(async_pipeline, output_stream_name, output_stream_info.index,
            "RemoveOverlappingBboxesEl", iou_op_metadata, pre_remove_overlapping_bboxes_element_queue_element));

    TRY(auto pre_fill_nms_format_element_queue_element,
        add_push_queue_element(PipelineObject::create_element_name("PushQEl_pre_fill_nms_format",
            output_stream_name, output_stream_info.index), async_pipeline, 0, is_empty, interacts_with_hw, remove_overlapping_bboxes_element));

    TRY(auto fill_nms_format_element,
        add_fill_nms_format_element(async_pipeline, output_stream_name, output_stream_info.index,
            "FillNmsFormatEl", iou_op_metadata, pre_fill_nms_format_element_queue_element, output_format.second.order));

    TRY(const auto output_vstream_info, iou_op_metadata->get_output_vstream_info());
    const auto final_frame_size = HailoRTCommon::get_frame_size(output_vstream_info, output_format.second);

    TRY(auto last_async_element,
        add_last_async_element(async_pipeline, output_format.first, final_frame_size, fill_nms_format_element));

    return HAILO_SUCCESS;
}

hailo_status AsyncPipelineBuilder::add_nms_flows(std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
    const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    const std::vector<hailo_vstream_info_t> &vstreams_infos, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    assert(1 <= op_metadata->outputs_metadata().size());
    auto updated_outputs_metadata = op_metadata->outputs_metadata();
    auto nms_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != nms_metadata);
    std::pair<std::string, hailo_format_t> expanded_output_format = {output_format.first,
        net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(output_format.second, op_metadata->type(), 
        nms_metadata->nms_config().bbox_only)};
    updated_outputs_metadata.begin()->second.format = expanded_output_format.second;

    op_metadata->set_outputs_metadata(updated_outputs_metadata);
    CHECK_SUCCESS(op_metadata->validate_format_info());
    std::shared_ptr<hailort::net_flow::Op> op;

    switch (op_metadata->type()) {
    case net_flow::OperationType::IOU:
        return add_iou_flow(async_pipeline, output_streams_names, expanded_output_format, op_metadata, named_stream_infos);

    case net_flow::OperationType::YOLOX:
    {
        auto metadata = std::dynamic_pointer_cast<net_flow::YoloxOpMetadata>(op_metadata);
        assert(nullptr != metadata);
        TRY(op, net_flow::YOLOXPostProcessOp::create(metadata));
        break;
    }
    case net_flow::OperationType::YOLOV8:
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
    case net_flow::OperationType::YOLOV5:
    {
        auto metadata = std::dynamic_pointer_cast<net_flow::Yolov5OpMetadata>(op_metadata);
        assert(nullptr != metadata);
        if (metadata->nms_config().bbox_only) {
            auto bbox_only_metadata = std::dynamic_pointer_cast<net_flow::Yolov5BboxOnlyOpMetadata>(op_metadata);
            assert(nullptr != bbox_only_metadata);
            TRY(op, net_flow::YOLOv5BboxOnlyPostProcessOp::create(bbox_only_metadata));
            break;
        } else {
            TRY(op, net_flow::YOLOv5PostProcessOp::create(metadata));
            break;
        }
    }
    case (net_flow::OperationType::YOLOV5SEG):
    {
        auto metadata = std::dynamic_pointer_cast<net_flow::Yolov5SegOpMetadata>(op_metadata);
        assert(nullptr != metadata);
        TRY(op, net_flow::Yolov5SegPostProcess::create(metadata));
        break;
    }
    case net_flow::OperationType::SSD:
    {
        auto metadata = std::dynamic_pointer_cast<net_flow::SSDOpMetadata>(op_metadata);
        assert(nullptr != metadata);
        TRY(op, net_flow::SSDPostProcessOp::create(metadata));
        break;
    }
    default:
        break;
    }
    hailo_vstream_info_t output_vstream_info;
    for (auto &current_output_vstream_info : vstreams_infos) {
        if (current_output_vstream_info.name == op->outputs_metadata().begin()->first) {
            output_vstream_info = current_output_vstream_info;
        }
    }
    return add_nms_flow(async_pipeline, output_streams_names, expanded_output_format, op, output_vstream_info, named_stream_infos);
}

hailo_status AsyncPipelineBuilder::add_ops_flows(std::shared_ptr<AsyncPipeline> async_pipeline,
    const std::pair<std::string, hailo_format_t> &output_format, net_flow::PostProcessOpMetadataPtr &op_metadata,
    const std::vector<std::string> &output_streams_names, const std::vector<hailo_vstream_info_t> &vstreams_infos,
    const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos)
{
    switch (op_metadata->type()) {
    case net_flow::OperationType::YOLOX:
    case net_flow::OperationType::YOLOV8:
    case net_flow::OperationType::SSD:
    case net_flow::OperationType::YOLOV5:
    case net_flow::OperationType::YOLOV5SEG:
    case net_flow::OperationType::IOU:
        return add_nms_flows(async_pipeline, output_streams_names, output_format, op_metadata, vstreams_infos, named_stream_infos);

    case net_flow::OperationType::ARGMAX:
        return add_argmax_flow(async_pipeline, output_streams_names, output_format, op_metadata, named_stream_infos);

    case net_flow::OperationType::SOFTMAX:
        return add_softmax_flow(async_pipeline, output_streams_names, output_format, op_metadata, named_stream_infos);

    default:
        LOGGER__ERROR("op type {} of op {} is not in any of the supported post process OP types", net_flow::OpMetadata::get_operation_type_str(op_metadata->type()), op_metadata->get_name());
        return HAILO_INVALID_OPERATION;
    }
}

hailo_status AsyncPipelineBuilder::create_post_async_hw_elements(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::unordered_map<std::string, hailo_format_t> &expanded_outputs_formats, std::unordered_map<std::string, hailo_format_t> &original_outputs_formats,
        const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    // streams_added is a vector which holds all stream names which vstreams connected to them were already added (for demux cases)
    std::vector<std::string> streams_added;

    // Building DBs that connect output_vstreams, output_streams and ops.
    // Note: Assuming each post process op has a unique output streams.
    //       In other words, not possible for an output stream to be connected to more than one op
    std::unordered_map<std::string, net_flow::PostProcessOpMetadataPtr> post_process_metadata;
    std::unordered_map<stream_name_t, op_name_t> op_inputs_to_op_name;
    for (auto &metadata : net_group->get_ops_metadata().release()) {
        post_process_metadata.insert({metadata->get_name(), metadata});
        for (const auto &input_name : metadata->get_input_names()) {
            op_inputs_to_op_name.insert({input_name, metadata->get_name()});
        }
    }

    for (auto &output_format : expanded_outputs_formats) {
        TRY(const auto stream_names, net_group->get_stream_names_from_vstream_name(output_format.first));

        if (contains(streams_added, *stream_names.begin())) {
            continue;
        }
        for (auto &output_name : stream_names) {
            streams_added.push_back(output_name);
        }

        CHECK(contains(named_stream_infos, *stream_names.begin()), HAILO_INTERNAL_FAILURE);
        const auto &first_stream_info = named_stream_infos.at(*stream_names.begin());

        if (contains(op_inputs_to_op_name, *stream_names.begin())) {
            const auto &op_name = op_inputs_to_op_name.at(*stream_names.begin());
            auto &op_metadata = post_process_metadata.at(op_name);

            TRY(const auto output_vstreams_infos, net_group->get_output_vstream_infos());

            std::pair<std::string, hailo_format_t> original_output_format = {output_format.first, original_outputs_formats.at(output_format.first)};

            hailo_status status = add_ops_flows(async_pipeline, original_output_format,
                op_metadata, stream_names, output_vstreams_infos, named_stream_infos);
            CHECK_SUCCESS(status);

        } else if ((HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == first_stream_info.format.order) &&
            (first_stream_info.nms_info.is_defused)) {
            // Case defuse NMS
            hailo_status status = add_nms_fuse_flow(stream_names, output_format, async_pipeline, named_stream_infos);
            CHECK_SUCCESS(status);
        } else if (first_stream_info.is_mux) {
            // case demux in output from NN core (only one output stream is currently supported)
            hailo_status status = add_output_demux_flow(*stream_names.begin(), async_pipeline, expanded_outputs_formats, net_group, named_stream_infos);
            CHECK_SUCCESS(status);
        } else {
            // case simple and single output from NN core to user (and transformation at best)
            TRY(const auto final_elem_source_index,
                async_pipeline->get_async_hw_element()->get_source_index_from_output_stream_name(*stream_names.begin()));

            TRY(const auto layer_info, net_group->get_layer_info(first_stream_info.name));
            auto stream_quant_infos = layer_info->quant_infos;

            TRY(auto should_transform,
                should_transform(first_stream_info, stream_quant_infos, output_format.second));

            if (should_transform) {
                TRY(auto post_infer_elem,
                    add_post_infer_element(output_format.second, first_stream_info.nms_info, async_pipeline, first_stream_info.hw_shape,
                        first_stream_info.format, first_stream_info.shape, stream_quant_infos, async_pipeline->get_async_hw_element(),
                        final_elem_source_index));

                const auto post_transform_frame_size = (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == first_stream_info.format.order) ?
                    HailoRTCommon::get_nms_by_class_host_frame_size(first_stream_info.nms_info, output_format.second) :
                    HailoRTCommon::get_frame_size(first_stream_info.shape, output_format.second);

                TRY(auto last_async_element, add_last_async_element(async_pipeline, output_format.first, post_transform_frame_size,
                    post_infer_elem));
            } else {
                TRY(auto last_async_element, add_last_async_element(async_pipeline, output_format.first, first_stream_info.hw_frame_size,
                    async_pipeline->get_async_hw_element(), final_elem_source_index));
            }
        }
    }
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<AsyncPipeline>> AsyncPipelineBuilder::create_pipeline(std::shared_ptr<ConfiguredNetworkGroup> net_group,
    const std::unordered_map<std::string, hailo_format_t> &inputs_formats,
    const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
    const uint32_t timeout, std::shared_ptr<std::atomic<hailo_status>> pipeline_status)
{
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> entry_elements;
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> last_elements;

    ElementBuildParams build_params {};

    // Buffer pool sizes for pipeline elements should be:
    // * The minimum of the maximum queue size of all LL streams (input and output) - for edge elements
    // * HAILO_DEFAULT_ASYNC_INFER_QUEUE_SIZE - for internal elements
    TRY(build_params.buffer_pool_size_edges, net_group->infer_queue_size());
    build_params.buffer_pool_size_internal = std::min(static_cast<uint32_t>(build_params.buffer_pool_size_edges),
        static_cast<uint32_t>(HAILO_DEFAULT_ASYNC_INFER_QUEUE_SIZE));
    build_params.elem_stats_flags = HAILO_PIPELINE_ELEM_STATS_NONE;
    build_params.vstream_stats_flags = HAILO_VSTREAM_STATS_NONE;

    TRY(auto async_pipeline, AsyncPipeline::create_shared(net_group->name()));
    TRY(const auto all_stream_infos, net_group->get_all_stream_infos());

    std::unordered_map<std::string, hailo_stream_info_t> named_stream_infos;
    for (const auto &info : all_stream_infos) {
        named_stream_infos.emplace(info.name, info);
    }

    TRY(const auto input_expanded_format, expand_auto_input_formats(net_group, inputs_formats, named_stream_infos));
    TRY(const auto output_expanded_format, expand_auto_output_formats(net_group, outputs_formats, named_stream_infos));
    auto outputs_original_formats = outputs_formats;  // The original formats is needed for specific format expanding (required for PP OPs, like argmax)

    TRY(build_params.shutdown_event, Event::create_shared(Event::State::not_signalled));
    build_params.pipeline_status = pipeline_status;
    build_params.timeout = std::chrono::milliseconds(timeout);

    async_pipeline->set_build_params(build_params);

    TRY(auto async_hw_elem, AsyncHwElement::create(named_stream_infos, build_params.timeout,
        build_params.elem_stats_flags, "AsyncHwEl", build_params.pipeline_status, net_group,
        PipelineDirection::PUSH, async_pipeline));
    async_pipeline->add_element_to_pipeline(async_hw_elem);
    async_pipeline->set_async_hw_element(async_hw_elem);

    hailo_status status = create_pre_async_hw_elements(net_group, input_expanded_format, named_stream_infos,
        async_pipeline);
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = create_post_async_hw_elements(net_group, output_expanded_format, outputs_original_formats, named_stream_infos,
        async_pipeline);
    CHECK_SUCCESS_AS_EXPECTED(status);

    print_pipeline_elements_info(async_pipeline);

    return async_pipeline;
}

void AsyncPipelineBuilder::print_pipeline_elements_info(std::shared_ptr<hailort::AsyncPipeline> async_pipeline)
{
    auto async_entry_elements = async_pipeline->get_entry_elements();
    std::vector<std::string> visited_elements;
    visited_elements.reserve(async_pipeline->get_pipeline().size());

    for (auto &element : async_entry_elements) {
        element.second->print_deep_description(visited_elements);
    }
}

Expected<std::shared_ptr<PixBufferElement>> AsyncPipelineBuilder::create_multi_plane_splitter_element(const std::string &input_name,
    hailo_format_order_t order, std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::shared_ptr<AsyncPipeline> async_pipeline)
{
    CHECK_AS_EXPECTED((HAILO_FORMAT_ORDER_NV12 == order) || (HAILO_FORMAT_ORDER_NV21 == order) || (HAILO_FORMAT_ORDER_I420 == order),
        HAILO_INVALID_ARGUMENT, "The given order ({}) is not a multi-planar order", HailoRTCommon::get_format_order_str(order));

    // TODO: Support fps/latency collection for queue elems (HRT-7711)
    TRY(auto duration_collector, DurationCollector::create(HAILO_PIPELINE_ELEM_STATS_NONE));

    TRY(auto planes_splitter, PixBufferElement::create(PipelineObject::create_element_name("PixBufEl",
        input_name, 0), std::chrono::milliseconds(HAILO_INFINITE), std::move(duration_collector), pipeline_status, order,
        async_pipeline));

    return planes_splitter;
}

} /* namespace hailort */
