/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
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

    NmsPostProcessConfig(double nms_score_th = 0, double nms_iou_th = 0, uint32_t max_proposals = 0, uint32_t number_of_classes = 0, bool background_removal = false,
        uint32_t background_removal_index = 0, bool cross_classes = false, bool bbox_only = false,
        hailo_nms_result_order_type_t order_type = HAILO_NMS_RESULT_ORDER_BY_CLASS) :
            nms_score_th(nms_score_th),
            nms_iou_th(nms_iou_th),
            number_of_classes(number_of_classes),
            background_removal(background_removal),
            background_removal_index(background_removal_index),
            cross_classes(cross_classes),
            bbox_only(bbox_only),
            order_type(order_type)
    {
        if (HAILO_NMS_RESULT_ORDER_BY_SCORE == order_type) {
            max_proposals_total = max_proposals;
        } else {
            max_proposals_per_class = max_proposals;
        }
    }

    // User given confidence threshold for a bbox. A bbox will be consider as detection if the
    // (objectness * class_score) is higher then the confidence_threshold.
    double nms_score_th;

    // User given IoU threshold (intersection over union). This threshold is for performing
    // Non-maximum suppression (Removing overlapping boxes).
    double nms_iou_th;

    union
    {
        // Maximum amount of bboxes per nms class.
        uint32_t max_proposals_per_class;

        // Maximum amount of bboxes in total.
        uint32_t max_proposals_total;
    };

    // The model's number of classes. (This depends on the dataset that the model trained on).
    uint32_t number_of_classes;

    // Toggle background class removal from results
    bool background_removal;

    // Index of background class for background removal
    uint32_t background_removal_index;

    // Indicates whether or not NMS performs IoU over different classes for the same box.
    // If set to false - NMS won't intersect different classes, and a box could have multiple labels.
    bool cross_classes;

    // Indicates whether only the bbox decoding is being done
    bool bbox_only;

    // Order of NMS results
    hailo_nms_result_order_type_t order_type;
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
    {
        switch (outputs_metadata.begin()->second.format.order) {
            case HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE:
                m_nms_config.order_type = HAILO_NMS_RESULT_ORDER_BY_SCORE;
                m_nms_config.max_proposals_total = m_nms_config.max_proposals_per_class * m_nms_config.number_of_classes;
                break;
            case HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS:
                m_nms_config.order_type = HAILO_NMS_RESULT_ORDER_BY_CLASS;
                break;
            case HAILO_FORMAT_ORDER_NHWC:
                // In case of bbox only
                break;
            default:
                LOGGER__WARNING("Unsupported NMS format order type for NmsOpMetadata: {}",
                    HailoRTCommon::get_format_order_str(outputs_metadata.begin()->second.format.order));
        }
    }

    hailo_status validate_params() override;

private:
    NmsPostProcessConfig m_nms_config;
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_NET_FLOW_NMS_OP_METADATA_HPP_ */
