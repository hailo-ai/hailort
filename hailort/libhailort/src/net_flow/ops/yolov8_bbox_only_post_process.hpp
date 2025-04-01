/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov8_bbox_only_post_process.hpp
 * @brief YOLOV8 bbox only post process
 * Output format of yolov8_bbox_only is NHWC - [1, total_proposals, 4 + number_of_classes]
 * 1 is the batch size and 4 is the number of coordinates for each proposal
 * The bboxes entry in the output of yolov8_bbox_only is a list of bboxes, such that each of them looks like this:
 * (y_min, x_min, y_max, x_max, score_per_class)
 **/

#ifndef _HAILO_YOLOV8_BBOX_ONLY_POST_PROCESS_HPP_
#define _HAILO_YOLOV8_BBOX_ONLY_POST_PROCESS_HPP_

#include "net_flow/ops/yolov8_post_process.hpp"
#include "net_flow/ops_metadata/yolov8_bbox_only_op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

class YOLOv8BboxOnlyPostProcessOp : public YOLOV8PostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<Yolov8BboxOnlyOpMetadata> metadata);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;

private:

    YOLOv8BboxOnlyPostProcessOp(std::shared_ptr<Yolov8BboxOnlyOpMetadata> metadata) :
        YOLOV8PostProcessOp(static_cast<std::shared_ptr<Yolov8OpMetadata>>(metadata))
    {}

    template<typename DstType = float32_t, typename SrcType>
    hailo_status add_bboxes(DstType *dst_ptr, size_t &next_bbox_output_offset, const Yolov8MatchingLayersNames &layers_names,
        const MemoryView &reg_buffer, const MemoryView &cls_buffer, uint32_t stride)
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

        for (uint32_t row = 0; row < cls_shape.height; row++) {
            for (uint32_t col = 0; col < cls_shape.width; col++) {
                auto cls_idx = (cls_row_size * row) + col;

                assert(contains(m_d_matrix, layers_names.reg));
                auto &d_matrix = m_d_matrix.at(layers_names.reg);
                auto bbox = get_bbox<DstType, SrcType>(row, col, stride, reg_padded_shape, reg_shape, reg_quant_info,
                                                        (SrcType*)reg_data, d_matrix); // we don't pass confidence here
                memcpy(&dst_ptr[next_bbox_output_offset], &bbox, sizeof(hailo_rectangle_t)); // copy y_min, x_min, y_max, x_max
                next_bbox_output_offset += sizeof(hailo_rectangle_t) / sizeof(float32_t);

                // copy class confidence for each of the classes
                for (uint32_t curr_class_idx = 0; curr_class_idx < nms_config.number_of_classes; curr_class_idx++) {
                    auto class_entry_idx = cls_idx + (curr_class_idx * cls_padded_shape.width);
                    auto class_confidence = Quantization::dequantize_output<DstType, SrcType>(
                        cls_data[class_entry_idx], cls_quant_info);
                    dst_ptr[next_bbox_output_offset++] = class_confidence;
                }
            }
        }
        return HAILO_SUCCESS;
    }
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_YOLOV5_BBOX_ONLY_POST_PROCESS_HPP_ */