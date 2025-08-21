/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov5_bbox_only_post_process.hpp
 * @brief YOLOV5 bbox only post process
 * Output format of yolov5_bbox_only is NHWC - [1, total_proposals, 5 + number_of_classes]
 * The bboxes entry in the output of yolov5_bbox_only is a list of bboxes, such that each of them looks like this:
 * (y_min, x_min, y_max, x_max, objectness, score_per_class)
 *
 **/

#ifndef _HAILO_YOLOV5_BBOX_ONLY_POST_PROCESS_HPP_
#define _HAILO_YOLOV5_BBOX_ONLY_POST_PROCESS_HPP_

#include "net_flow/ops/yolov5_post_process.hpp"
#include "net_flow/ops_metadata/yolov5_bbox_only_op_metadata.hpp"

namespace hailort
{

static const uint32_t YOLOV5_BBOX_NUM_OF_VALUES = 5;
namespace net_flow
{

class YOLOv5BboxOnlyPostProcessOp : public YOLOv5PostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<Yolov5BboxOnlyOpMetadata> metadata);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;

private:

    YOLOv5BboxOnlyPostProcessOp(std::shared_ptr<Yolov5BboxOnlyOpMetadata> metadata) :
        YOLOv5PostProcessOp(static_cast<std::shared_ptr<Yolov5OpMetadata>>(metadata))
    {}

    static const uint32_t YOLOV5_BBOX_ONLY_BBOXES_INDEX = 0;

    template<typename DstType = float32_t, typename SrcType>
    void add_classes_scores(hailo_quant_info_t &quant_info, DstType* dst_data, size_t &next_bbox_output_offset,
        SrcType* src_data, uint32_t entry_idx, uint32_t class_start_idx, uint32_t padded_width)
    {
        const auto &nms_config = m_metadata->nms_config();

        for (uint32_t class_index = 0; class_index < nms_config.number_of_classes; class_index++) {
            auto class_entry_idx = entry_idx + ((class_start_idx + class_index) * padded_width);
            auto class_confidence = dequantize_and_sigmoid<DstType, SrcType>(
            src_data[class_entry_idx], quant_info);
            dst_data[next_bbox_output_offset++] = class_confidence;
        }
    }

    template<typename DstType = float32_t, typename SrcType>
    hailo_status add_bboxes(DstType *dst_ptr, size_t &next_bbox_output_offset,
        const MemoryView &input_buffer, hailo_quant_info_t quant_info, hailo_3d_image_shape_t shape,
        hailo_3d_image_shape_t padded_shape, const std::vector<int> &layer_anchors)
    {
        const uint32_t X_OFFSET = X_INDEX * padded_shape.width;
        const uint32_t Y_OFFSET = Y_INDEX * padded_shape.width;
        const uint32_t W_OFFSET = W_INDEX * padded_shape.width;
        const uint32_t H_OFFSET = H_INDEX * padded_shape.width;
        const uint32_t OBJECTNESS_OFFSET = OBJECTNESS_INDEX * padded_shape.width;

        auto num_of_anchors = get_num_of_anchors(layer_anchors);

        uint32_t entry_size = get_entry_size();
        auto number_of_entries = padded_shape.height * padded_shape.width * num_of_anchors;

        auto buffer_size = number_of_entries * entry_size * sizeof(SrcType);
        CHECK(buffer_size == input_buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract proposals, buffer_size should be {}, but is {}", buffer_size, input_buffer.size());

        const auto input_row_size = padded_shape.width * padded_shape.features;
        const auto anchor_size = entry_size * padded_shape.width;

        // Optimized: incrementing pointers and offsets instead of calculating offsets with multiplication
        SrcType *input_data = (SrcType*)input_buffer.data();
        auto row_offset = 0;
        for (uint32_t row = 0; row < shape.height; row++) {
            for (uint32_t col = 0; col < shape.width; col++) {
                auto entry_idx = row_offset + col;
                for (uint32_t anchor = 0; anchor < num_of_anchors; anchor++) {
                    auto objectness = dequantize_and_sigmoid<DstType, SrcType>(input_data[entry_idx + OBJECTNESS_OFFSET], quant_info);
                    auto bbox = decode_bbox(input_data, entry_idx, X_OFFSET, Y_OFFSET, W_OFFSET, H_OFFSET, 
                        quant_info, anchor, layer_anchors, col, row, shape);
                    memcpy(&dst_ptr[next_bbox_output_offset], &bbox, sizeof(hailo_bbox_float32_t) - sizeof(DstType)); // copy y_min, x_min, y_max, x_max
                    next_bbox_output_offset += (sizeof(hailo_bbox_float32_t) / sizeof(float32_t)) - 1;
                    dst_ptr[next_bbox_output_offset++] = objectness;

                    add_classes_scores(quant_info, dst_ptr, next_bbox_output_offset, input_data, entry_idx,
                        CLASSES_START_INDEX, padded_shape.width);

                    entry_idx += anchor_size;
                }
            }
            row_offset += input_row_size;
        }
        return HAILO_SUCCESS;
    }
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_YOLOV5_BBOX_ONLY_POST_PROCESS_HPP_ */