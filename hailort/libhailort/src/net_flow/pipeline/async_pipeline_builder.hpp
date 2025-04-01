/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_pipeline_builder.hpp
 * @brief Async Pipeline Builder
 **/

#ifndef _HAILO_ASYNC_PIPELINE_BUILDER_HPP_
#define _HAILO_ASYNC_PIPELINE_BUILDER_HPP_

#include "hailo/hailort.h"
#include "network_group/network_group_internal.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"
#include "net_flow/pipeline/async_infer_runner.hpp"
#include "net_flow/ops/op.hpp"

namespace hailort
{


class AsyncPipelineBuilder final
{
public:
    AsyncPipelineBuilder() = delete;

    static Expected<std::shared_ptr<AsyncPipeline>> create_pipeline(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats,
        const std::unordered_map<std::string, hailo_format_t> &outputs_formats, const uint32_t timeout,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status);

    static Expected<std::unordered_map<std::string, hailo_format_t>> expand_auto_input_formats(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);
    static Expected<std::unordered_map<std::string, hailo_format_t>> expand_auto_output_formats(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::unordered_map<std::string, hailo_format_t> &outputs_formats, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);
    static Expected<std::pair<std::string, hailo_format_t>> get_output_format_from_edge_info_name(const std::string &edge_info_name,
        const std::unordered_map<std::string, hailo_format_t> &outputs_formats);

    static hailo_status create_pre_async_hw_elements(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos,
        std::shared_ptr<AsyncPipeline> async_pipeline);
    static hailo_status create_pre_async_hw_elements_per_input(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::vector<std::string> &stream_names, const std::unordered_map<std::string, hailo_format_t> &inputs_formats,
        const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos, std::shared_ptr<AsyncPipeline> async_pipeline);
    static hailo_status create_post_async_hw_elements(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::unordered_map<std::string, hailo_format_t> &expanded_outputs_formats, std::unordered_map<std::string, hailo_format_t> &original_outputs_formats,
        const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos, std::shared_ptr<AsyncPipeline> async_pipeline);

    static hailo_status add_argmax_flow(std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
        const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata,
        const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);
    static hailo_status add_softmax_flow(std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
        const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata,
        const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);
    static hailo_status add_ops_flows(std::shared_ptr<AsyncPipeline> async_pipeline, const std::pair<std::string, hailo_format_t> &output_format,
        net_flow::PostProcessOpMetadataPtr &op_metadata, const std::vector<std::string> &output_streams_names,
        const std::vector<hailo_vstream_info_t> &vstreams_infos, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);
    static hailo_status add_output_demux_flow(const std::string &output_stream_name,
        std::shared_ptr<AsyncPipeline> async_pipeline, const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
        std::shared_ptr<ConfiguredNetworkGroup> net_group, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);
    static hailo_status add_nms_fuse_flow(const std::vector<std::string> &output_streams_names, const std::pair<std::string, hailo_format_t> &output_format,
        std::shared_ptr<AsyncPipeline> async_pipeline, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);
    static hailo_status add_nms_flow(std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
        const std::pair<std::string, hailo_format_t> &output_format, const std::shared_ptr<hailort::net_flow::Op> &nms_op,
        const hailo_vstream_info_t &vstream_info, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);
    static hailo_status add_iou_flow(std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
        const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata,
        const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);
    static hailo_status add_nms_flows(std::shared_ptr<AsyncPipeline> async_pipeline, const std::vector<std::string> &output_streams_names,
        const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &op_metadata,
        const std::vector<hailo_vstream_info_t> &vstreams_infos, const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos);


    static Expected<std::shared_ptr<PostInferElement>> add_post_infer_element(const hailo_format_t &output_format, const hailo_nms_info_t &nms_info,
        std::shared_ptr<AsyncPipeline> async_pipeline, const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const std::vector<hailo_quant_info_t> &dst_quant_infos,
        std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<LastAsyncElement>> add_last_async_element(std::shared_ptr<AsyncPipeline> async_pipeline,
        const std::string &output_format_name, size_t frame_size, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<AsyncPushQueueElement>> add_push_queue_element(const std::string &queue_name, std::shared_ptr<AsyncPipeline> async_pipeline,
        size_t frame_size, bool is_empty, bool interacts_with_hw, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0,
        bool is_entry = false);
    static Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> add_nms_to_detections_convert_element(std::shared_ptr<AsyncPipeline> async_pipeline,
        const std::string &output_stream_name, uint8_t stream_index, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
        std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> add_remove_overlapping_bboxes_element(std::shared_ptr<AsyncPipeline> async_pipeline,
        const std::string &output_stream_name, uint8_t stream_index, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
        std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<FillNmsFormatElement>> add_fill_nms_format_element(std::shared_ptr<AsyncPipeline> async_pipeline,
        const std::string &output_stream_name, uint8_t stream_index, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
        std::shared_ptr<PipelineElement> final_elem, const hailo_format_order_t &dst_format_order, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<PixBufferElement>> create_multi_plane_splitter_element(const std::string &input_name,
        hailo_format_order_t order, std::shared_ptr<std::atomic<hailo_status>> pipeline_status, std::shared_ptr<AsyncPipeline> async_pipeline);

    static Expected<bool> should_transform(const hailo_stream_info_t &stream_info, const std::vector<hailo_quant_info_t> &stream_quant_infos, 
        const hailo_format_t &output_format);

    static void print_pipeline_elements_info(std::shared_ptr<hailort::AsyncPipeline> async_pipeline);
};

} /* namespace hailort */

#endif /* _HAILO_ASYNC_PIPELINE_BUILDER_HPP_ */
