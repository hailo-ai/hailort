/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov5_post_process.hpp
 * @brief YOLO post process
 *
 * https://learnopencv.com/object-detection-using-yolov5-and-opencv-dnn-in-c-and-python :
 * The headline '4.3.5 POST-PROCESSING YOLOv5 Prediction Output' contains explanations on the YOLOv5 post-processing.
 **/

#ifndef _HAILO_YOLO_POST_PROCESS_HPP_
#define _HAILO_YOLO_POST_PROCESS_HPP_

#include "net_flow/ops/nms_post_process.hpp"
#include "net_flow/ops_metadata/yolov5_op_metadata.hpp"

#include "common/internal_env_vars.hpp"


namespace hailort
{
namespace net_flow
{

#define MASK_COEFFICIENT_SIZE (32)

class YOLOv5PostProcessOp : public NmsPostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<Yolov5OpMetadata> metadata);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;
    static size_t get_num_of_anchors(const std::vector<int> &layer_anchors);

protected:
    hailo_bbox_float32_t decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
        int wa, int ha, uint32_t col, uint32_t row, uint32_t w_stride, uint32_t h_stride) const;

    virtual uint32_t get_entry_size();

    YOLOv5PostProcessOp(std::shared_ptr<Yolov5OpMetadata> metadata) :
        NmsPostProcessOp(static_cast<std::shared_ptr<NmsOpMetadata>>(metadata)),
        m_metadata(metadata),
        m_is_crop_optimization_on(is_env_variable_on(HAILORT_YOLOV5_SEG_PP_CROP_OPT_ENV_VAR)),
        m_is_nn_resize_optimization_on(is_env_variable_on(HAILORT_YOLOV5_SEG_NN_RESIZE_ENV_VAR))
    {}

    static const uint32_t X_INDEX = 0;
    static const uint32_t Y_INDEX = 1;
    static const uint32_t W_INDEX = 2;
    static const uint32_t H_INDEX = 3;
    static const uint32_t OBJECTNESS_INDEX = 4;
    static const uint32_t CLASSES_START_INDEX = 5;

    /**
     * Quantize threshold for comparison optimization.
     * If sigmoid is applied after dequantization, we need to compare sigmoid(dequantize(x)) >= threshold
     * This is equivalent to: x >= quantize(logit(threshold)) - logit is the inverse of sigmoid
     * Otherwise: x >= quantize(threshold)
     *
     * @param[in] threshold             The threshold value to quantize.
     * @param[in] quant_info            Quantization info.
     *
     * @return Returns the quantized threshold for comparison.
     */
    template<typename SrcType>
    SrcType quantize_threshold_for_comparison(float32_t threshold, hailo_quant_info_t quant_info)
    {
        if (should_sigmoid()) {
            auto logit_threshold = logit(threshold);
            return Quantization::quantize_input<float32_t, SrcType>(logit_threshold, quant_info);
        } else {
            return Quantization::quantize_input<float32_t, SrcType>(threshold, quant_info);
        }
    }

    template<typename DstType = float32_t, typename SrcType>
    hailo_bbox_float32_t decode_bbox(SrcType* data, uint32_t entry_idx, const uint32_t X_OFFSET, const uint32_t Y_OFFSET,
        const uint32_t W_OFFSET, const uint32_t H_OFFSET, hailo_quant_info_t quant_info, uint32_t anchor,
        const std::vector<int> &layer_anchors, uint32_t col, uint32_t row, hailo_3d_image_shape_t shape)
    {
        auto tx = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + X_OFFSET], quant_info);
        auto ty = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + Y_OFFSET], quant_info);
        auto tw = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + W_OFFSET], quant_info);
        auto th = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + H_OFFSET], quant_info);
        return decode(tx, ty, tw, th, layer_anchors[anchor * 2], layer_anchors[anchor * 2 + 1], col, row,
            shape.width, shape.height);
    }

    template<typename DstType = float32_t, typename SrcType>
    void check_threshold_and_add_detection(hailo_bbox_float32_t bbox, hailo_quant_info_t &quant_info,
        uint32_t class_index, SrcType* data, uint32_t entry_idx, uint32_t padded_width, DstType objectness)
    {
        const auto &nms_config = m_metadata->nms_config();
        const auto &yolov5_config = m_metadata->yolov5_config();
        if (bbox.score >= nms_config.nms_score_th) {
            if (should_add_mask()) {
                // We will not preform the sigmoid on the mask at this point -
                // It should happen on the result of the vector mask multiplication with the proto_mask layer.
                uint32_t mask_index_start_index = CLASSES_START_INDEX + nms_config.number_of_classes;
                std::vector<float32_t> mask_coefficients(MASK_COEFFICIENT_SIZE, 0.0f);
                for (size_t i = 0; i < MASK_COEFFICIENT_SIZE; i++) {
                    auto coeffs_offset = entry_idx + (mask_index_start_index + i) * padded_width;
                    mask_coefficients[i] = (Quantization::dequantize_output<DstType, SrcType>(
                        data[coeffs_offset], quant_info) * objectness);
                }
                m_detections.emplace_back(DetectionBbox(bbox, static_cast<uint16_t>(class_index), std::move(mask_coefficients),
                    yolov5_config.image_height, yolov5_config.image_width, m_is_crop_optimization_on));
            } else {
                m_detections.emplace_back(DetectionBbox(bbox, class_index));
            }
            m_classes_detections_count[class_index]++;
        }
    }

    template<typename DstType = float32_t, typename SrcType>
    void decode_classes_scores(hailo_bbox_float32_t &bbox,
        hailo_quant_info_t &quant_info, SrcType* data, uint32_t entry_idx, uint32_t class_start_idx,
        DstType objectness, uint32_t padded_width)
    {
        const auto &nms_config = m_metadata->nms_config();

        // Quantize the class confidence threshold once instead of dequantizing each data point
        SrcType quantized_class_threshold = quantize_threshold_for_comparison<SrcType>(static_cast<float32_t>(nms_config.nms_score_th), quant_info);

        // Optimized: incrementing pointers instead of calculating offsets with multiplication
        auto class_entry_idx = entry_idx + (class_start_idx * padded_width);
        for (uint32_t class_index = 0; class_index < nms_config.number_of_classes; class_index++) {
            if (data[class_entry_idx] < quantized_class_threshold) { // First - compare quantized values
                class_entry_idx += padded_width;
                continue;
            }
            // Only dequantize and apply sigmoid when we know the quantized value passes the quantized threshold
            auto class_confidence = dequantize_and_sigmoid<DstType, SrcType>(
                data[class_entry_idx], quant_info);
            bbox.score = class_confidence * objectness;
            check_threshold_and_add_detection(bbox, quant_info, class_index,
                data, entry_idx, padded_width, objectness);
            class_entry_idx += padded_width;
        }
    }

    /**
     * Extract bboxes with confidence level higher then @a confidence_threshold from @a buffer and add them to @a detections.
     *
     * @param[in] buffer                        Buffer containing data after inference
     * @param[in] quant_info                    Quantization info corresponding to the @a buffer layer.
     * @param[in] shape                         Shape corresponding to the @a buffer layer.
     * @param[in] layer_anchors                 The layer anchors corresponding to layer receiving the @a buffer.
     *                                          Each anchor is structured as {width, height} pairs.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
    */
    template<typename DstType = float32_t, typename SrcType>
    hailo_status extract_detections(const MemoryView &buffer, hailo_quant_info_t quant_info,
        hailo_3d_image_shape_t shape, hailo_3d_image_shape_t padded_shape,
        const std::vector<int> &layer_anchors)
    {
        const uint32_t X_OFFSET = X_INDEX * padded_shape.width;
        const uint32_t Y_OFFSET = Y_INDEX * padded_shape.width;
        const uint32_t W_OFFSET = W_INDEX * padded_shape.width;
        const uint32_t H_OFFSET = H_INDEX * padded_shape.width;
        const uint32_t OBJECTNESS_OFFSET = OBJECTNESS_INDEX * padded_shape.width;

        const auto &nms_config = m_metadata->nms_config();

        auto num_of_anchors = get_num_of_anchors(layer_anchors);

        uint32_t entry_size = get_entry_size();
        auto number_of_entries = padded_shape.height * padded_shape.width * num_of_anchors;

        auto buffer_size = number_of_entries * entry_size * sizeof(SrcType);
        CHECK(buffer_size == buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, buffer_size should be {}, but is {}", buffer_size, buffer.size());

        const auto row_size = padded_shape.width * padded_shape.features;
        const auto anchor_size = entry_size * padded_shape.width;

        // Quantize the objectness threshold once instead of dequantizing each data point
        SrcType quantized_objectness_threshold = quantize_threshold_for_comparison<SrcType>(static_cast<float32_t>(nms_config.nms_score_th), quant_info);

        // Optimized: incrementing pointers and offsets instead of calculating offsets with multiplication
        // Loop order optimized for NHCW layout: row, anchor, col
        SrcType *data = (SrcType*)buffer.data();
        uint32_t row_offset = 0;
        for (uint32_t row = 0; row < shape.height; row++) {
            uint32_t anchor_offset = row_offset;
            for (uint32_t anchor = 0; anchor < num_of_anchors; anchor++) {
                for (uint32_t col = 0; col < shape.width; col++) {
                    uint32_t entry_idx = anchor_offset + col;
                    if (data[entry_idx + OBJECTNESS_OFFSET] < quantized_objectness_threshold) { // First - compare quantized values
                        continue;
                    }
                    // Only dequantize and apply sigmoid when we know the quantized value passes the quantized threshold
                    auto objectness = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + OBJECTNESS_OFFSET], quant_info);
                    if (objectness < nms_config.nms_score_th) { // Double check: compare dequantized value to real threshold
                        continue;
                    }

                    auto bbox = decode_bbox(data, entry_idx, X_OFFSET, Y_OFFSET, W_OFFSET, H_OFFSET,
                        quant_info, anchor, layer_anchors, col, row, shape);

                    decode_classes_scores(bbox, quant_info, data, entry_idx,
                        CLASSES_START_INDEX, objectness, padded_shape.width);
                }
                anchor_offset += anchor_size;
            }
            row_offset += row_size;
        }

        return HAILO_SUCCESS;
    }

    std::shared_ptr<Yolov5OpMetadata> m_metadata;
    bool m_is_crop_optimization_on;
    bool m_is_nn_resize_optimization_on;

};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_YOLO_POST_PROCESS_HPP_ */