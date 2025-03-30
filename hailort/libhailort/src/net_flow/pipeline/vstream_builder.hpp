/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream_builder.hpp
 * @brief Vstream Builder
 **/

#ifndef _HAILO_VSTREAM_BUILDER_HPP_
#define _HAILO_VSTREAM_BUILDER_HPP_

#include "net_flow/pipeline/vstream_internal.hpp"

namespace hailort
{

class VStreamsBuilderUtils
{
public:
    static Expected<std::vector<InputVStream>> create_inputs(std::vector<std::shared_ptr<InputStreamBase>> input_streams, const hailo_vstream_info_t &input_vstream_infos,
        const hailo_vstream_params_t &vstreams_params);
    static Expected<std::vector<OutputVStream>> create_outputs(std::shared_ptr<OutputStreamBase> output_stream,
        NameToVStreamParamsMap &vstreams_params_map, const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);
    static InputVStream create_input(std::shared_ptr<InputVStreamInternal> input_vstream);
    static OutputVStream create_output(std::shared_ptr<OutputVStreamInternal> output_vstream);
    static Expected<std::vector<OutputVStream>> create_output_nms(OutputStreamPtrVector &output_streams,
        hailo_vstream_params_t vstreams_params,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);
    static Expected<std::vector<OutputVStream>> create_output_vstreams_from_streams(const OutputStreamWithParamsVector &all_output_streams,
        OutputStreamPtrVector &output_streams, const hailo_vstream_params_t &vstream_params,
        const std::unordered_map<std::string, net_flow::PostProcessOpMetadataPtr> &post_process_ops,
        const std::unordered_map<std::string, std::string> &op_inputs_to_op_name, const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos_map);
    static Expected<std::vector<OutputVStream>> create_output_post_process_nms(OutputStreamPtrVector &output_streams,
        hailo_vstream_params_t vstreams_params,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos,
        const std::shared_ptr<hailort::net_flow::Op> &nms_op);
    static Expected<std::shared_ptr<HwReadElement>> add_hw_read_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, const ElementBuildParams &build_params);

    static Expected<std::shared_ptr<PullQueueElement>> add_pull_queue_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, const hailo_vstream_params_t &vstream_params, size_t frame_size);

    // Move all post-processes related elements to a dedicated model - HRT-11512
    static Expected<std::shared_ptr<ArgmaxPostProcessElement>> add_argmax_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, hailo_vstream_params_t &vstream_params,
        const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata, const ElementBuildParams &build_params);

    static Expected<std::shared_ptr<SoftmaxPostProcessElement>> add_softmax_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, hailo_vstream_params_t &vstream_params,
        const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata, const ElementBuildParams &build_params);

    static Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> add_nms_to_detections_convert_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata, 
        const ElementBuildParams &build_params);

    static Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> add_remove_overlapping_bboxes_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata,
        const ElementBuildParams &build_params);

    static Expected<std::shared_ptr<FillNmsFormatElement>> add_fill_nms_format_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata,
        const ElementBuildParams &build_params, const hailo_format_order_t &dst_format_order);

    static Expected<std::shared_ptr<UserBufferQueueElement>> add_user_buffer_queue_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, const hailo_vstream_params_t &vstream_params, size_t frame_size);

    static Expected<std::shared_ptr<PostInferElement>> add_post_infer_element(std::shared_ptr<OutputStreamBase> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, const hailo_vstream_params_t &vstream_params);

    static hailo_status add_demux(std::shared_ptr<OutputStreamBase> output_stream, NameToVStreamParamsMap &vstreams_params_map,
        std::vector<std::shared_ptr<PipelineElement>> &&elements, std::vector<OutputVStream> &vstreams,
        std::shared_ptr<PipelineElement> last_elem, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);

    static hailo_status handle_pix_buffer_splitter_flow(std::vector<std::shared_ptr<InputStreamBase>> streams,
        const hailo_vstream_info_t &vstream_info, std::vector<std::shared_ptr<PipelineElement>> &&base_elements,
        std::vector<InputVStream> &vstreams, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, EventPtr &core_op_activated_event,
        AccumulatorPtr accumaltor);

    static hailo_status add_nms_fuse(OutputStreamPtrVector &output_streams, hailo_vstream_params_t &vstreams_params,
        std::vector<std::shared_ptr<PipelineElement>> &elements, std::vector<OutputVStream> &vstreams,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);

    static hailo_status add_nms_post_process(OutputStreamPtrVector &output_streams, hailo_vstream_params_t &vstreams_params,
        std::vector<std::shared_ptr<PipelineElement>> &elements, std::vector<OutputVStream> &vstreams,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos,
        const std::shared_ptr<hailort::net_flow::Op> &nms_op);

    static Expected<AccumulatorPtr> create_pipeline_latency_accumulator(const hailo_vstream_params_t &vstreams_params);

    static hailo_format_t expand_user_buffer_format_autos_multi_planar(const hailo_vstream_info_t &vstream_info,
        const hailo_format_t &user_buffer_format)
    {
        /* In multi planar case we compare to vstream_info instead of stream_info,
            as the ll-streams formats doesnt indicate the format of the vstreams */
        auto expanded_user_buffer_format = user_buffer_format;
        if (HAILO_FORMAT_TYPE_AUTO == expanded_user_buffer_format.type) {
            expanded_user_buffer_format.type = vstream_info.format.type;
        }
        if (HAILO_FORMAT_ORDER_AUTO == expanded_user_buffer_format.order) {
            expanded_user_buffer_format.order = vstream_info.format.order;
        }

        return expanded_user_buffer_format;
    }

private:
    static Expected<std::vector<OutputVStream>> create_output_post_process_argmax(std::shared_ptr<OutputStreamBase> output_stream,
        const NameToVStreamParamsMap &vstreams_params_map, const hailo_vstream_info_t &output_vstream_info,
        const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata);
    static Expected<std::vector<OutputVStream>> create_output_post_process_softmax(std::shared_ptr<OutputStreamBase> output_stream,
        const NameToVStreamParamsMap &vstreams_params_map, const hailo_vstream_info_t &output_vstream_info,
        const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata);
    static Expected<std::vector<OutputVStream>> create_output_post_process_iou(std::shared_ptr<OutputStreamBase> output_stream,
        hailo_vstream_params_t vstream_params, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata);
};

} /* namespace hailort */

#endif /* _HAILO_VSTREAM_BUILDER_HPP_ */
