/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file nms_op_metadata.hpp
 * @brief NMS op metadata
 *
 **/

#ifndef _HAILO_NET_FLOW_NMS_OP_METADATA_HPP_
#define _HAILO_NET_FLOW_NMS_OP_METADATA_HPP_

#include "net_flow/ops_metadata/op_metadata.hpp"
#include "hailo/hailort_common.hpp"

namespace hailort
{
namespace net_flow
{

struct NmsPostProcessConfig
{

    NmsPostProcessConfig(double nms_score_th = 0, double nms_iou_th = 0, uint32_t max_proposals_per_class = 0, uint32_t max_proposals_total = 0,
        uint32_t number_of_classes = 0, bool background_removal = false, uint32_t background_removal_index = 0, bool bbox_only = false) :
            nms_score_th(nms_score_th),
            nms_iou_th(nms_iou_th),
            max_proposals_per_class(max_proposals_per_class),
            max_proposals_total(max_proposals_total),
            number_of_classes(number_of_classes),
            background_removal(background_removal),
            background_removal_index(background_removal_index),
            bbox_only(bbox_only)
    {}

    // User given confidence threshold for a bbox. A bbox will be consider as detection if the
    // (objectness * class_score) is higher then the confidence_threshold.
    double nms_score_th;

    // User given IoU threshold (intersection over union). This threshold is for performing
    // Non-maximum suppression (Removing overlapping boxes).
    double nms_iou_th;

    // Maximum amount of bboxes per nms class.
    uint32_t max_proposals_per_class;

    // Maximum amount of bboxes in total.
    uint32_t max_proposals_total;

    // The model's number of classes. (This depends on the dataset that the model trained on).
    uint32_t number_of_classes;

    // Toggle background class removal from results
    bool background_removal;

    // Index of background class for background removal
    uint32_t background_removal_index;

    // Indicates whether only the bbox decoding is being done
    bool bbox_only;
};

static const float32_t REMOVED_CLASS_SCORE = 0.0f;

class NmsOpMetadata : public OpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                    const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                    const NmsPostProcessConfig &nms_post_process_config,
                                                    const std::string &network_name,
                                                    const OperationType type,
                                                    const std::string &name);
    virtual ~NmsOpMetadata() = default;
    std::string get_nms_config_description();
    hailo_status validate_format_info() override;
    hailo_status validate_format_type(const hailo_format_t &format);
    NmsPostProcessConfig &nms_config() { return m_nms_config;};
    hailo_nms_info_t nms_info();
    std::string get_op_description() override;
    static hailo_format_t expand_output_format_autos_by_op_type(const hailo_format_t &output_format, OperationType type, bool bbox_only);

    virtual Expected<hailo_vstream_info_t> get_output_vstream_info() override;

protected:
    NmsOpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                    const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                    const NmsPostProcessConfig &nms_post_process_config,
                    const std::string &name,
                    const std::string &network_name,
                    const OperationType type)
        : OpMetadata(inputs_metadata, outputs_metadata, name, network_name, type),
            m_nms_config(nms_post_process_config)
    {}

    hailo_status validate_params() override;

private:
    NmsPostProcessConfig m_nms_config;
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_NET_FLOW_NMS_OP_METADATA_HPP_ */
