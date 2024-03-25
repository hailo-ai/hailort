/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
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

namespace hailort
{
namespace net_flow
{

struct NmsPostProcessConfig
{
    // User given confidence threshold for a bbox. A bbox will be consider as detection if the
    // (objectness * class_score) is higher then the confidence_threshold.
    double nms_score_th = 0;

    // User given IoU threshold (intersection over union). This threshold is for performing
    // Non-maximum suppression (Removing overlapping boxes).
    double nms_iou_th = 0;

    // Maximum amount of bboxes per nms class.
    uint32_t max_proposals_per_class = 0;

    // The model's number of classes. (This depends on the dataset that the model trained on).
    uint32_t number_of_classes = 0;

    // Toggle background class removal from results
    bool background_removal = false;

    // Index of background class for background removal
    uint32_t background_removal_index = 0;

    // Indicates whether or not NMS performs IoU over different classes for the same box.
    // If set to false - NMS won't intersect different classes, and a box could have multiple labels.
    bool cross_classes = false;

    // Indicates whether only the bbox decoding is being done
    bool bbox_only = false;
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
