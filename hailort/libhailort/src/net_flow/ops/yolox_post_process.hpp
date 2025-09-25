/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "net_flow/ops_metadata/yolox_op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

class YOLOXPostProcessOp : public NmsPostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<YoloxOpMetadata> metadata);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;

private:
    std::shared_ptr<YoloxOpMetadata> m_metadata;

    YOLOXPostProcessOp(std::shared_ptr<YoloxOpMetadata> metadata)
        : NmsPostProcessOp(static_cast<std::shared_ptr<NmsOpMetadata>>(metadata))
        , m_metadata(metadata)
    {}

    template<typename DstType = float32_t, typename SrcType>
    hailo_status extract_detections(const YoloxMatchingLayersNames &layers_names, const MemoryView &reg_buffer, const MemoryView &cls_buffer,
        const MemoryView &obj_buffer)
    {
        const auto &inputs_metadata = m_metadata->inputs_metadata();
        const auto &nms_config = m_metadata->nms_config();

        assert(contains(inputs_metadata, layers_names.reg));
        assert(contains(inputs_metadata, layers_names.cls));
        assert(contains(inputs_metadata, layers_names.obj));
        const auto &reg_shape = inputs_metadata.at(layers_names.reg).shape;
        const auto &reg_padded_shape = inputs_metadata.at(layers_names.reg).padded_shape;
        const auto &cls_padded_shape = inputs_metadata.at(layers_names.cls).padded_shape;
        const auto &obj_padded_shape = inputs_metadata.at(layers_names.obj).padded_shape;
        const auto &reg_quant_info = inputs_metadata.at(layers_names.reg).quant_info;
        const auto &cls_quant_info = inputs_metadata.at(layers_names.cls).quant_info;
        const auto &obj_quant_info = inputs_metadata.at(layers_names.obj).quant_info;

        static const uint32_t X_INDEX = 0;
        static const uint32_t Y_INDEX = 1;
        static const uint32_t W_INDEX = 2;
        static const uint32_t H_INDEX = 3;

        const uint32_t X_OFFSET = X_INDEX * reg_padded_shape.width;
        const uint32_t Y_OFFSET = Y_INDEX * reg_padded_shape.width;
        const uint32_t W_OFFSET = W_INDEX * reg_padded_shape.width;
        const uint32_t H_OFFSET = H_INDEX * reg_padded_shape.width;

        // Validate regression buffer size
        static const uint32_t reg_entry_size = 4;
        auto number_of_entries = reg_padded_shape.height * reg_padded_shape.width;
        auto buffer_size = number_of_entries * reg_entry_size * sizeof(SrcType);
        CHECK(buffer_size == reg_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, reg {} buffer_size should be {}, but is {}", layers_names.reg, buffer_size, reg_buffer.size());

        // Validate classes buffer size
        const uint32_t cls_entry_size = nms_config.number_of_classes;
        number_of_entries = cls_padded_shape.height * cls_padded_shape.width;
        buffer_size = number_of_entries * cls_entry_size * sizeof(SrcType);
        CHECK(buffer_size == cls_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, cls {} buffer_size should be {}, but is {}", layers_names.cls, buffer_size, cls_buffer.size());

        // Validate objectness buffer size
        static const uint32_t obj_entry_size = 1;
        number_of_entries = obj_padded_shape.height * obj_padded_shape.width;
        buffer_size = number_of_entries * obj_entry_size * sizeof(SrcType);
        CHECK(buffer_size == obj_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, obj {} buffer_size should be {}, but is {}", layers_names.obj, buffer_size, obj_buffer.size());

        auto reg_row_size = reg_padded_shape.width * reg_padded_shape.features;
        auto cls_row_size = cls_padded_shape.width * cls_padded_shape.features;
        auto obj_row_size = obj_padded_shape.width * obj_padded_shape.features;

        SrcType *reg_data = (SrcType*)reg_buffer.data();
        SrcType *obj_data = (SrcType*)obj_buffer.data();
        SrcType *cls_data = (SrcType*)cls_buffer.data();


        for (uint32_t row = 0; row < reg_shape.height; row++) {
            for (uint32_t col = 0; col < reg_shape.width; col++) {
                auto obj_idx = (obj_row_size * row) + col;
                auto objectness = Quantization::dequantize_output<DstType, SrcType>(obj_data[obj_idx], obj_quant_info);

                if (objectness < nms_config.nms_score_th) {
                    continue;
                }

                auto reg_idx = (reg_row_size * row) + col;
                auto cls_idx = (cls_row_size * row) + col;

                auto tx = Quantization::dequantize_output<DstType, SrcType>(reg_data[reg_idx + X_OFFSET], reg_quant_info);
                auto ty = Quantization::dequantize_output<DstType, SrcType>(reg_data[reg_idx + Y_OFFSET], reg_quant_info);
                auto tw = Quantization::dequantize_output<DstType, SrcType>(reg_data[reg_idx + W_OFFSET], reg_quant_info);
                auto th = Quantization::dequantize_output<DstType, SrcType>(reg_data[reg_idx + H_OFFSET], reg_quant_info);
                auto bbox = decode(tx, ty, tw, th, col, row, static_cast<float32_t>(reg_shape.width), static_cast<float32_t>(reg_shape.height));

                for (uint32_t curr_class_idx = 0; curr_class_idx < nms_config.number_of_classes; curr_class_idx++) {
                    auto class_entry_idx = cls_idx + (curr_class_idx * cls_padded_shape.width);
                    auto class_confidence = Quantization::dequantize_output<DstType, SrcType>(
                        cls_data[class_entry_idx], cls_quant_info);
                    auto class_score = class_confidence * objectness;
                    if (class_score >= nms_config.nms_score_th) {
                        bbox.score = class_score;
                        m_detections.emplace_back(DetectionBbox(bbox, curr_class_idx));
                        m_classes_detections_count[curr_class_idx]++;
                    }
                }
            }
        }

        return HAILO_SUCCESS;
    }

    virtual hailo_bbox_float32_t decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
        uint32_t col, uint32_t row, float32_t w_stride, float32_t h_stride) const;

};

} // namespace net_flow
} // namespace hailort

#endif // _HAILO_YOLOX_POST_PROCESS_HPP_
