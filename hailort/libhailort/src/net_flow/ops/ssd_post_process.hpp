/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file ssd_post_process.hpp
 * @brief SSD post process
 *
 * Reference code: https://github.com/winfredsu/ssd_postprocessing/blob/master/ssd_postprocessing.py
 **/

#ifndef _HAILO_SSD_POST_PROCESS_HPP_
#define _HAILO_SSD_POST_PROCESS_HPP_

#include "net_flow/ops/nms_post_process.hpp"

namespace hailort
{
namespace net_flow
{

struct SSDPostProcessConfig
{
    // The image height.
    float32_t image_height = 0;

    // The image width.
    float32_t image_width = 0;

    uint32_t centers_scale_factor = 0;

    uint32_t bbox_dimensions_scale_factor = 0;

    uint32_t ty_index = 0;
    uint32_t tx_index = 0;
    uint32_t th_index = 0;
    uint32_t tw_index = 0;

    std::map<std::string, std::string> reg_to_cls_inputs;

    // A vector of anchors, each element in the vector represents the anchors for a specific layer
    // Each layer anchors vector is structured as {w,h} pairs.
    // Each anchor is mapped by 2 keys:
    //     1. reg input
    //     2. cls input
    std::map<std::string, std::vector<float32_t>> anchors;

    // Indicates whether boxes should be normalized (and clipped)
    bool normalize_boxes = false;
};

class SSDPostProcessOp : public NmsPostProcessOp
{

public:
    static Expected<std::shared_ptr<Op>> create(const std::map<std::string, BufferMetaData> &inputs_metadata,
                                                const std::map<std::string, BufferMetaData> &outputs_metadata,
                                                const NmsPostProcessConfig &nms_post_process_config,
                                                const SSDPostProcessConfig &ssd_post_process_config);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;
    std::string get_op_description() override;
    hailo_status validate_metadata() override; // TODO: HRT-10676

    static const uint32_t DEFAULT_Y_OFFSET_IDX = 0;
    static const uint32_t DEFAULT_X_OFFSET_IDX = 1;
    static const uint32_t DEFAULT_H_OFFSET_IDX = 2;
    static const uint32_t DEFAULT_W_OFFSET_IDX = 3;

private:
    SSDPostProcessOp(const std::map<std::string, BufferMetaData> &inputs_metadata,
                     const std::map<std::string, BufferMetaData> &outputs_metadata,
                     const NmsPostProcessConfig &nms_post_process_config,
                     const SSDPostProcessConfig &ssd_post_process_config)
        : NmsPostProcessOp(inputs_metadata, outputs_metadata, nms_post_process_config, "SSD-Post-Process")
        , m_ssd_config(ssd_post_process_config)
    {}

    SSDPostProcessConfig m_ssd_config;

    template<typename HostType = float32_t, typename DeviceType>
    void extract_bbox_classes(const hailo_bbox_float32_t &dims_bbox, DeviceType *cls_data, const BufferMetaData &cls_metadata, uint32_t cls_index,
        std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count)
    {
        if (m_nms_config.cross_classes) {
            // Pre-NMS optimization. If NMS checks IOU over different classes, only the maximum class is relevant
            auto max_id_score_pair = get_max_class<HostType, DeviceType>(cls_data, cls_index, 0, 1,
                cls_metadata.quant_info, cls_metadata.padded_shape.width);
            auto bbox = dims_bbox;
            bbox.score = max_id_score_pair.second;
            if (max_id_score_pair.second >= m_nms_config.nms_score_th) {
                detections.emplace_back(DetectionBbox(bbox, max_id_score_pair.first));
                classes_detections_count[max_id_score_pair.first]++;
            }
        } else {
            for (uint32_t class_index = 0; class_index < m_nms_config.number_of_classes; class_index++) {
                auto class_id = class_index;
                if (m_nms_config.background_removal) {
                    if (m_nms_config.background_removal_index == class_index) {
                        // Ignore if class_index is background_removal_index
                        continue;
                    }
                    else if (0 == m_nms_config.background_removal_index) {
                        // background_removal_index will always be the first or last index.
                        // If it is the first one we need to reduce all classes id's in 1.
                        // If it is the last one we just ignore it in the previous if case.
                        class_id--;
                    }
                }

                auto class_entry_idx = cls_index + (class_index * cls_metadata.padded_shape.width);
                auto class_score = Quantization::dequantize_output<HostType, DeviceType>(cls_data[class_entry_idx],
                    cls_metadata.quant_info);
                if (class_score < m_nms_config.nms_score_th) {
                    continue;
                }
                auto bbox = dims_bbox;
                bbox.score = class_score;
                detections.emplace_back(bbox, class_id);
                classes_detections_count[class_id]++;
            }
        }
    }

    template<typename HostType = float32_t, typename DeviceType>
    hailo_status extract_bbox_detections(const std::string &reg_input_name, const std::string &cls_input_name,
        const MemoryView &reg_buffer, const MemoryView &cls_buffer,
        uint64_t x_index, uint64_t y_index, uint64_t w_index, uint64_t h_index,
        uint32_t cls_index, float32_t wa, float32_t ha, float32_t xcenter_a, float32_t ycenter_a,
        std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count)
    {
        const auto &shape = m_inputs_metadata[reg_input_name].shape;
        const auto &reg_quant_info = m_inputs_metadata[reg_input_name].quant_info;
        DeviceType *reg_data = (DeviceType*)reg_buffer.data();
        auto *cls_data = cls_buffer.data();
        auto tx = Quantization::dequantize_output<HostType, DeviceType>(reg_data[x_index], reg_quant_info);
        auto ty = Quantization::dequantize_output<HostType, DeviceType>(reg_data[y_index], reg_quant_info);
        auto tw = Quantization::dequantize_output<HostType, DeviceType>(reg_data[w_index], reg_quant_info);
        auto th = Quantization::dequantize_output<HostType, DeviceType>(reg_data[h_index], reg_quant_info);
        tx /= static_cast<float32_t>(m_ssd_config.centers_scale_factor);
        ty /= static_cast<float32_t>(m_ssd_config.centers_scale_factor);
        tw /= static_cast<float32_t>(m_ssd_config.bbox_dimensions_scale_factor);
        th /= static_cast<float32_t>(m_ssd_config.bbox_dimensions_scale_factor);
        auto w = exp(tw) * wa;
        auto h = exp(th) * ha;
        auto x_center = tx * wa + xcenter_a;
        auto y_center = ty * ha + ycenter_a;
        auto x_min = (x_center - (w / 2.0f));
        auto y_min = (y_center - (h / 2.0f));
        auto x_max = (x_center + (w / 2.0f));
        auto y_max = (y_center + (h / 2.0f));

        // TODO: HRT-10033 - Fix support for clip_boxes and normalize_output
        // Currently `normalize_boxes` is always false
        if (m_ssd_config.normalize_boxes) {
            x_min = Quantization::clip(x_min, 0, static_cast<float32_t>(shape.width-1));
            y_min = Quantization::clip(y_min, 0, static_cast<float32_t>(shape.height-1));
            x_max = Quantization::clip(x_max, 0, static_cast<float32_t>(shape.width-1));
            y_max = Quantization::clip(y_max, 0, static_cast<float32_t>(shape.height-1));
        }
        hailo_bbox_float32_t dims_bbox{y_min, x_min, y_max, x_max, 0};
        const auto &cls_metadata = m_inputs_metadata[cls_input_name];
        if (cls_metadata.format.type == HAILO_FORMAT_TYPE_UINT8) {
            extract_bbox_classes<HostType, uint8_t>(dims_bbox, (uint8_t*)cls_data, m_inputs_metadata[cls_input_name],
                cls_index, detections, classes_detections_count);
        } else if (cls_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) {
            extract_bbox_classes<HostType, uint16_t>(dims_bbox, (uint16_t*)cls_data, m_inputs_metadata[cls_input_name],
                cls_index, detections, classes_detections_count);
        } else if (cls_metadata.format.type == HAILO_FORMAT_TYPE_FLOAT32) {
            extract_bbox_classes<HostType, float32_t>(dims_bbox, (float32_t*)cls_data, m_inputs_metadata[cls_input_name],
                cls_index, detections, classes_detections_count);
        } else {
            CHECK_SUCCESS(HAILO_INVALID_ARGUMENT, "SSD post-process received invalid cls input type: {}",
                m_inputs_metadata[cls_input_name].format.type);
        }
        return HAILO_SUCCESS;
    }

    /**
     * Extract bboxes with confidence level higher then @a confidence_threshold from @a buffer and add them to @a detections.
     *
     * @param[in] reg_input_name                Name of the regression input
     * @param[in] cls_input_name                Name of the classes input
     * @param[in] reg_buffer                    Buffer containing the boxes data after inference
     * @param[in] cls_buffer                    Buffer containing the classes ids after inference.
     * @param[inout] detections                 A vector of ::DetectionBbox objects, to add the detected bboxes to.
     * @param[inout] classes_detections_count   A vector of uint32_t, to add count of detections count per class to.
     * 
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
    */
    hailo_status extract_detections(const std::string &reg_input_name, const std::string &cls_input_name,
        const MemoryView &reg_buffer, const MemoryView &cls_buffer,
        std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count);
};


}

}

#endif // _HAILO_SSD_POST_PROCESSING_HPP_