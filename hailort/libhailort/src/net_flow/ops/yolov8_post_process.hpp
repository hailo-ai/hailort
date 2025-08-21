/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov8_post_process.hpp
 * @brief YOLOV8 post process
 *
 **/

#ifndef _HAILO_YOLOV8_POST_PROCESS_HPP_
#define _HAILO_YOLOV8_POST_PROCESS_HPP_

#include "net_flow/ops/nms_post_process.hpp"
#include "net_flow/ops/softmax_post_process.hpp"
#include "net_flow/ops_metadata/yolov8_op_metadata.hpp"
#include "net_flow/ops/softmax_post_process.hpp"

namespace hailort
{
namespace net_flow
{

class YOLOV8PostProcessOp : public NmsPostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<Yolov8OpMetadata> metadata);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;

protected:
    std::shared_ptr<Yolov8OpMetadata> m_metadata;
    std::unordered_map<std::string, std::vector<std::vector<float32_t>>> m_d_matrix; // Holds the values from which we compute those distances
    YOLOV8PostProcessOp(std::shared_ptr<Yolov8OpMetadata> metadata)
        : NmsPostProcessOp(static_cast<std::shared_ptr<NmsOpMetadata>>(metadata))
        , m_metadata(metadata), m_d_values_matrix(NUM_OF_D_VALUES)
    {
        for (const auto &input_metadata : m_metadata->inputs_metadata()) {
            m_d_matrix[input_metadata.first] = std::vector<std::vector<float32_t>>(NUM_OF_D_VALUES,
                                                    std::vector<float32_t>(input_metadata.second.padded_shape.features / NUM_OF_D_VALUES));
        }
    }

    template<typename DstType = float32_t, typename SrcType>
    hailo_bbox_float32_t get_bbox(uint32_t row, uint32_t col, uint32_t stride, const hailo_3d_image_shape_t &reg_padded_shape,
        const hailo_3d_image_shape_t &reg_shape, const hailo_quant_info_t &reg_quant_info, SrcType *reg_data,
        std::vector<std::vector<DstType>> &d_matrix, DstType class_confidence = 0)
    {
        auto reg_row_size = reg_padded_shape.width * reg_padded_shape.features; // should be the padded values - we use it to get to the relevant row
        auto reg_feature_size = reg_padded_shape.width; // Also should be the padded value - we use it to get to the relevant feature
        auto reg_idx = (reg_row_size * row) + col;

        // For each HxW - reshape from features to 4 x (features/4) + dequantize
        // For example - reshape from 64 to 4X16 - 4 vectors of 16 values
        for (uint32_t feature = 0; feature < reg_shape.features; feature++) {
            auto &tmp_vector = d_matrix.at(feature / (reg_shape.features / NUM_OF_D_VALUES));
            tmp_vector[feature % (reg_shape.features / NUM_OF_D_VALUES)] = Quantization::dequantize_output<DstType, SrcType>(reg_data[reg_idx + feature*reg_feature_size], reg_quant_info);
        }

        // Performing softmax operation on each of the vectors
        for (uint32_t vector_index = 0; vector_index < d_matrix.size(); vector_index++) {
            auto &tmp_vector = d_matrix.at(vector_index);
            SoftmaxPostProcessOp::softmax(tmp_vector.data(), tmp_vector.data(), tmp_vector.size());
        }

        // Performing dot product on each vector
        // (A, B, C, ..., F, G) -> 0*A + 1*B + 2*C + ... + 14*F + 15*G
        for (uint32_t vector_index = 0; vector_index < NUM_OF_D_VALUES; vector_index++) {
            m_d_values_matrix[vector_index] = dot_product(d_matrix.at(vector_index));
        }

        // The decode function extract x_min, y_min, x_max, y_max from d1, d2, d3, d4
        const auto &d1 = m_d_values_matrix.at(0);
        const auto &d2 = m_d_values_matrix.at(1);
        const auto &d3 = m_d_values_matrix.at(2);
        const auto &d4 = m_d_values_matrix.at(3);
        auto bbox = decode(d1, d2, d3, d4, col, row, stride);
        bbox.score = class_confidence;
        return bbox;
    }

    template<typename SrcType>
    hailo_status validate_regression_buffer_size(const hailo_3d_image_shape_t &reg_padded_shape, const MemoryView &reg_buffer,
        const Yolov8MatchingLayersNames &layers_names)
    {
        auto number_of_entries = reg_padded_shape.height * reg_padded_shape.width;
        auto buffer_size = number_of_entries * reg_padded_shape.features * sizeof(SrcType);
        CHECK(buffer_size == reg_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, reg {} buffer_size should be {}, but is {}", layers_names.reg, buffer_size, reg_buffer.size());
        return HAILO_SUCCESS;
    }

    template<typename SrcType>
    hailo_status validate_classes_buffer_size(const hailo_3d_image_shape_t &cls_padded_shape, const MemoryView &cls_buffer,
        const Yolov8MatchingLayersNames &layers_names, const NmsPostProcessConfig &nms_config)
    {
        const uint32_t cls_entry_size = nms_config.number_of_classes;
        auto number_of_entries = cls_padded_shape.height * cls_padded_shape.width;
        auto buffer_size = number_of_entries * cls_entry_size * sizeof(SrcType);
        CHECK(buffer_size == cls_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, cls {} buffer_size should be {}, but is {}", layers_names.cls, buffer_size, cls_buffer.size());
        return HAILO_SUCCESS;
    }

private:
    std::vector<float32_t> m_d_values_matrix;  // Holds the values of the bbox boundaries distances from the stride's center

    static const uint32_t CLASSES_START_INDEX = 0;
    static const uint32_t NO_OBJECTNESS = 1;
    static const uint32_t NUM_OF_D_VALUES = 4;

    template<typename DstType = float32_t, typename SrcType>
    hailo_status extract_detections(const Yolov8MatchingLayersNames &layers_names, const MemoryView &reg_buffer, const MemoryView &cls_buffer,
        uint32_t stride)
    {
        const auto &inputs_metadata = m_metadata->inputs_metadata();
        const auto &nms_config = m_metadata->nms_config();

        assert(contains(inputs_metadata, layers_names.reg));
        assert(contains(inputs_metadata, layers_names.cls));
        const auto &reg_shape = inputs_metadata.at(layers_names.reg).shape;
        const auto &cls_shape = inputs_metadata.at(layers_names.cls).shape;
        const auto &reg_padded_shape = inputs_metadata.at(layers_names.reg).padded_shape;
        const auto &cls_padded_shape = inputs_metadata.at(layers_names.cls).padded_shape;
        const auto &reg_quant_info = inputs_metadata.at(layers_names.reg).quant_info;
        const auto &cls_quant_info = inputs_metadata.at(layers_names.cls).quant_info;

        CHECK_SUCCESS(validate_regression_buffer_size<SrcType>(reg_padded_shape, reg_buffer, layers_names));
        CHECK_SUCCESS(validate_classes_buffer_size<SrcType>(cls_padded_shape, cls_buffer, layers_names, nms_config));

        // Format is NHCW -> each row size is (padded C size) * (padded W size)
        auto cls_row_size = cls_padded_shape.features * cls_padded_shape.width;

        SrcType *reg_data = (SrcType*)reg_buffer.data();
        SrcType *cls_data = (SrcType*)cls_buffer.data();

        // Quantize the threshold once instead of dequantizing each data point
        SrcType quantized_threshold = Quantization::quantize_input<float32_t, SrcType>(static_cast<float32_t>(nms_config.nms_score_th), cls_quant_info);

        // Optimized: incrementing pointers and offsets instead of calculating offsets with multiplication
        uint32_t row_offset = 0;
        for (uint32_t row = 0; row < cls_shape.height; row++) {
            uint32_t offset = row_offset;
            for (uint32_t curr_class_idx = 0; curr_class_idx < nms_config.number_of_classes; curr_class_idx++) {
                uint32_t end_of_feature = offset + cls_shape.width; // We want to iterate over the entire feature, without the padding
                for (; offset < end_of_feature; offset++) {
                    if (cls_data[offset] >= quantized_threshold) { // First - compare quantized values
                        // Only dequantize when we know the quantized value passes the quantized threshold
                        auto class_confidence = Quantization::dequantize_output<DstType, SrcType>(
                            cls_data[offset], cls_quant_info);
                        if (class_confidence >= nms_config.nms_score_th) {
                            // If passes threshold - get the relevant bbox and add this detection
                            assert(contains(m_d_matrix, layers_names.reg));
                            auto &d_matrix = m_d_matrix.at(layers_names.reg);
                            auto col = offset % cls_padded_shape.width;
                            auto bbox = get_bbox<DstType, SrcType>(row, col, stride, reg_padded_shape, reg_shape, reg_quant_info,
                                                                    (SrcType*)reg_data, d_matrix, class_confidence);
                            m_detections.emplace_back(DetectionBbox(bbox, curr_class_idx));
                            m_classes_detections_count[curr_class_idx]++;
                        }
                    }
                }
                offset += cls_padded_shape.width - cls_shape.width; // When finish with the current feature - go to the next one by skipping the padding
            }
            row_offset += cls_row_size;
        }
        return HAILO_SUCCESS;
    }

    virtual hailo_bbox_float32_t decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
        uint32_t col, uint32_t row, uint32_t stride) const;

    static float32_t dot_product(std::vector<float> &values);

};

} // namespace net_flow
} // namespace hailort

#endif // _HAILO_YOLOV8_POST_PROCESS_HPP_
