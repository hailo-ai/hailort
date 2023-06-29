/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file yolox_post_process.hpp
 * @brief YOLOX post process
 *
 **/

#ifndef _HAILO_YOLOX_POST_PROCESS_HPP_
#define _HAILO_YOLOX_POST_PROCESS_HPP_

#include "net_flow/ops/nms_post_process.hpp"

namespace hailort
{
namespace net_flow
{

struct MatchingLayersNames
{
    // Regression layer
    std::string reg;

    // Objectness layer
    std::string obj;

    // Classifications layer
    std::string cls;
};

struct YoloxPostProcessConfig
{
    // The image height.
    float32_t image_height = 0;

    // The image width.
    float32_t image_width = 0;

    // A vector off three strings that represents the relations between the outputs names.
    std::vector<MatchingLayersNames> input_names;
};

class YOLOXPostProcessOp : public NmsPostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(const std::map<std::string, BufferMetaData> &inputs_metadata,
                                                const std::map<std::string, BufferMetaData> &outputs_metadata,
                                                const NmsPostProcessConfig &nms_post_process_config,
                                                const YoloxPostProcessConfig &yolo_post_process_config);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;
    std::string get_op_description() override;
    hailo_status validate_metadata() override;// TODO: HRT-10676

private:
    template<typename HostType = float32_t, typename DeviceType>
    hailo_status extract_detections(const MatchingLayersNames &layers_names, const MemoryView &reg_buffer, const MemoryView &cls_buffer,
        const MemoryView &obj_buffer, std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count)
    {
        const auto &reg_shape = m_inputs_metadata[layers_names.reg].shape;
        const auto &reg_padded_shape = m_inputs_metadata[layers_names.reg].padded_shape;
        const auto &cls_padded_shape = m_inputs_metadata[layers_names.cls].padded_shape;
        const auto &obj_padded_shape = m_inputs_metadata[layers_names.obj].padded_shape;
        const auto &reg_quant_info = m_inputs_metadata[layers_names.reg].quant_info;
        const auto &cls_quant_info = m_inputs_metadata[layers_names.cls].quant_info;
        const auto &obj_quant_info = m_inputs_metadata[layers_names.obj].quant_info;

        static const uint32_t X_INDEX = 0;
        static const uint32_t Y_INDEX = 1;
        static const uint32_t W_INDEX = 2;
        static const uint32_t H_INDEX = 3;

        const uint32_t X_OFFSET = X_INDEX * reg_padded_shape.width;
        const uint32_t Y_OFFSET = Y_INDEX * reg_padded_shape.width;
        const uint32_t W_OFFSET = W_INDEX * reg_padded_shape.width;
        const uint32_t H_OFFSET = H_INDEX * reg_padded_shape.width;

        static const uint32_t CLASSES_START_INDEX = 0;

        // Validate regression buffer size
        static const uint32_t reg_entry_size = 4;
        auto number_of_entries = reg_padded_shape.height * reg_padded_shape.width;
        auto buffer_size = number_of_entries * reg_entry_size * sizeof(DeviceType);
        CHECK(buffer_size == reg_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, reg {} buffer_size should be {}, but is {}", layers_names.reg, buffer_size, reg_buffer.size());

        // Validate classes buffer size
        const uint32_t cls_entry_size = m_nms_config.number_of_classes;
        number_of_entries = cls_padded_shape.height * cls_padded_shape.width;
        buffer_size = number_of_entries * cls_entry_size * sizeof(DeviceType);
        CHECK(buffer_size == cls_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, cls {} buffer_size should be {}, but is {}", layers_names.cls, buffer_size, cls_buffer.size());

        // Validate objectness buffer size
        static const uint32_t obj_entry_size = 1;
        number_of_entries = obj_padded_shape.height * obj_padded_shape.width;
        buffer_size = number_of_entries * obj_entry_size * sizeof(DeviceType);
        CHECK(buffer_size == obj_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, obj {} buffer_size should be {}, but is {}", layers_names.obj, buffer_size, obj_buffer.size());

        auto reg_row_size = reg_padded_shape.width * reg_padded_shape.features;
        auto cls_row_size = cls_padded_shape.width * cls_padded_shape.features;
        auto obj_row_size = obj_padded_shape.width * obj_padded_shape.features;

        DeviceType *reg_data = (DeviceType*)reg_buffer.data();
        DeviceType *obj_data = (DeviceType*)obj_buffer.data();
        DeviceType *cls_data = (DeviceType*)cls_buffer.data();

        for (uint32_t row = 0; row < reg_shape.height; row++) {
            for (uint32_t col = 0; col < reg_shape.width; col++) {
                auto obj_idx = (obj_row_size * row) + col;
                auto objectness = Quantization::dequantize_output<HostType, DeviceType>(obj_data[obj_idx], obj_quant_info);

                if (objectness < m_nms_config.nms_score_th) {
                    continue;
                }

                auto reg_idx = (reg_row_size * row) + col;
                auto cls_idx = (cls_row_size * row) + col;

                auto tx = Quantization::dequantize_output<HostType, DeviceType>(reg_data[reg_idx + X_OFFSET], reg_quant_info);
                auto ty = Quantization::dequantize_output<HostType, DeviceType>(reg_data[reg_idx + Y_OFFSET], reg_quant_info);
                auto tw = Quantization::dequantize_output<HostType, DeviceType>(reg_data[reg_idx + W_OFFSET], reg_quant_info);
                auto th = Quantization::dequantize_output<HostType, DeviceType>(reg_data[reg_idx + H_OFFSET], reg_quant_info);
                auto bbox = decode(tx, ty, tw, th, col, row, static_cast<float32_t>(reg_shape.width), static_cast<float32_t>(reg_shape.height));

                if (m_nms_config.cross_classes) {
                    // Pre-NMS optimization. If NMS checks IOU over different classes, only the maximum class is relevant
                    auto max_id_score_pair = get_max_class<HostType, DeviceType>(cls_data, cls_idx, CLASSES_START_INDEX, objectness, cls_quant_info, cls_padded_shape.width);
                    bbox.score = max_id_score_pair.second;
                    if (max_id_score_pair.second >= m_nms_config.nms_score_th) {
                        detections.emplace_back(DetectionBbox(bbox, max_id_score_pair.first));
                        classes_detections_count[max_id_score_pair.first]++;
                    }
                }
                else {
                    for (uint32_t curr_class_idx = 0; curr_class_idx < m_nms_config.number_of_classes; curr_class_idx++) {
                        auto class_entry_idx = cls_idx + (curr_class_idx * cls_padded_shape.width);
                        auto class_confidence = Quantization::dequantize_output<HostType, DeviceType>(
                            cls_data[class_entry_idx], cls_quant_info);
                        auto class_score = class_confidence * objectness;
                        if (class_score >= m_nms_config.nms_score_th) {
                            bbox.score = class_score;
                            detections.emplace_back(DetectionBbox(bbox, curr_class_idx));
                            classes_detections_count[curr_class_idx]++;
                        }
                    }
                }
            }
        }

        return HAILO_SUCCESS;
    }

    virtual hailo_bbox_float32_t decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
        uint32_t col, uint32_t row, float32_t w_stride, float32_t h_stride) const;

    YoloxPostProcessConfig m_yolox_config;

    YOLOXPostProcessOp(const std::map<std::string, BufferMetaData> &inputs_metadata,
                       const std::map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const YoloxPostProcessConfig &yolo_post_process_config)
        : NmsPostProcessOp(inputs_metadata, outputs_metadata, nms_post_process_config, "YOLOX-Post-Process")
        , m_yolox_config(yolo_post_process_config)
    {}

};

} // namespace net_flow
} // namespace hailort

#endif // _HAILO_YOLOX_POST_PROCESS_HPP_
