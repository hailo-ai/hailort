/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
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
#include "net_flow/ops/op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

#define MASK_COEFFICIENT_SIZE (32)

struct YoloPostProcessConfig
{
    // The image height.
    float32_t image_height = 0;

    // The image width.
    float32_t image_width = 0;

    // A vector of anchors, each element in the vector represents the anchors for a specific layer
    // Each layer anchors vector is structured as {w,h} pairs.
    std::map<std::string, std::vector<int>> anchors;
};

class Yolov5OpMetadata : public NmsOpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const YoloPostProcessConfig &yolov5_post_process_config,
                                                        const std::string &network_name);
    std::string get_op_description() override;
    hailo_status validate_format_info() override;
    YoloPostProcessConfig &yolov5_config() { return m_yolov5_config;};

protected:
    Yolov5OpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const std::string &name,
                       const std::string &network_name,
                       const YoloPostProcessConfig &yolov5_post_process_config,
                       const OperationType op_type)
        : NmsOpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, name, network_name, op_type)
        , m_yolov5_config(yolov5_post_process_config)
    {}

    hailo_status validate_params() override;

private:
    YoloPostProcessConfig m_yolov5_config;

};

class YOLOv5PostProcessOp : public NmsPostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<Yolov5OpMetadata> metadata);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;

protected:
    hailo_bbox_float32_t decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
        int wa, int ha, uint32_t col, uint32_t row, uint32_t w_stride, uint32_t h_stride) const;

    virtual uint32_t get_entry_size();

    YOLOv5PostProcessOp(std::shared_ptr<Yolov5OpMetadata> metadata) :
        NmsPostProcessOp(static_cast<std::shared_ptr<NmsOpMetadata>>(metadata)),
        m_metadata(metadata)
    {}

    static const uint32_t X_INDEX = 0;
    static const uint32_t Y_INDEX = 1;
    static const uint32_t W_INDEX = 2;
    static const uint32_t H_INDEX = 3;
    static const uint32_t OBJECTNESS_INDEX = 4;
    static const uint32_t CLASSES_START_INDEX = 5;



    template<typename DstType = float32_t, typename SrcType>
    void check_threshold_and_add_detection(std::vector<DetectionBbox> &detections,
        std::vector<uint32_t> &classes_detections_count, hailo_bbox_float32_t bbox, hailo_quant_info_t &quant_info,
        uint32_t class_index, SrcType* data, uint32_t entry_idx, uint32_t padded_width, DstType objectness)
    {
        const auto &nms_config = m_metadata->nms_config();
        if (bbox.score >= nms_config.nms_score_th) {
            if (should_add_mask()) {
                // We will not preform the sigmoid on the mask at this point -
                // It should happen on the result of the vector mask multiplication with the proto_mask layer.
                uint32_t mask_index_start_index = CLASSES_START_INDEX + nms_config.number_of_classes;
                std::vector<float32_t> mask(MASK_COEFFICIENT_SIZE, 0.0f);
                for (size_t i = 0; i < MASK_COEFFICIENT_SIZE; i++) {
                    auto mask_offset = entry_idx + (mask_index_start_index + i) * padded_width;
                    mask[i] = (Quantization::dequantize_output<DstType, SrcType>(data[mask_offset], quant_info) * objectness);
                }
                detections.emplace_back(DetectionBbox(bbox, class_index, std::move(mask)));
            } else {
                detections.emplace_back(DetectionBbox(bbox, class_index));
            }
            classes_detections_count[class_index]++;
        }
    }

    template<typename DstType = float32_t, typename SrcType>
    void decode_classes_scores(std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count,
        hailo_bbox_float32_t &bbox, hailo_quant_info_t &quant_info, SrcType* data, uint32_t entry_idx, uint32_t class_start_idx,
        DstType objectness, uint32_t padded_width)
    {
        const auto &nms_config = m_metadata->nms_config();

        if (nms_config.cross_classes) {
            // Pre-NMS optimization. If NMS checks IoU over different classes, only the maximum class is relevant
            auto max_id_score_pair = get_max_class<DstType, SrcType>(data, entry_idx, class_start_idx, objectness, quant_info, padded_width);
            bbox.score = max_id_score_pair.second;
            check_threshold_and_add_detection(detections, classes_detections_count, bbox, quant_info, max_id_score_pair.first,
                data, entry_idx, padded_width, objectness);
        }
        else {
            for (uint32_t class_index = 0; class_index < nms_config.number_of_classes; class_index++) {
                auto class_entry_idx = entry_idx + ((class_start_idx + class_index) * padded_width);
                auto class_confidence = dequantize_and_sigmoid<DstType, SrcType>(
                    data[class_entry_idx], quant_info);
                bbox.score = class_confidence * objectness;
                check_threshold_and_add_detection(detections, classes_detections_count, bbox, quant_info, class_index,
                    data, entry_idx, padded_width, objectness);
            }
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
     * @param[inout] detections                 A vector of ::DetectionBbox objects, to add the detected bboxes to.
     * @param[inout] classes_detections_count   A vector of uint32_t, to add count of detections count per class to.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
    */
    template<typename DstType = float32_t, typename SrcType>
    hailo_status extract_detections(const MemoryView &buffer, hailo_quant_info_t quant_info,
        hailo_3d_image_shape_t shape, hailo_3d_image_shape_t padded_shape,
        const std::vector<int> &layer_anchors, std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count)
    {
        const uint32_t X_OFFSET = X_INDEX * padded_shape.width;
        const uint32_t Y_OFFSET = Y_INDEX * padded_shape.width;
        const uint32_t W_OFFSET = W_INDEX * padded_shape.width;
        const uint32_t H_OFFSET = H_INDEX * padded_shape.width;
        const uint32_t OBJECTNESS_OFFSET = OBJECTNESS_INDEX * padded_shape.width;

        const auto &nms_config = m_metadata->nms_config();

        // Each layer anchors vector is structured as {w,h} pairs.
        // For example, if we have a vector of size 6 (default YOLOv5 vector) then we have 3 anchors for this layer.
        assert(layer_anchors.size() % 2 == 0);
        const size_t num_of_anchors = (layer_anchors.size() / 2);

        uint32_t entry_size = get_entry_size();
        auto number_of_entries = padded_shape.height * padded_shape.width * num_of_anchors;

        auto buffer_size = number_of_entries * entry_size * sizeof(SrcType);
        CHECK(buffer_size == buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, buffer_size should be {}, but is {}", buffer_size, buffer.size());

        auto row_size = padded_shape.width * padded_shape.features;
        SrcType *data = (SrcType*)buffer.data();
        for (uint32_t row = 0; row < shape.height; row++) {
            for (uint32_t col = 0; col < shape.width; col++) {
                for (uint32_t anchor = 0; anchor < num_of_anchors; anchor++) {
                    auto entry_idx = (row_size * row) + col + ((anchor * entry_size) * padded_shape.width);
                    auto objectness = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + OBJECTNESS_OFFSET], quant_info);
                    if (objectness < nms_config.nms_score_th) {
                        continue;
                    }

                    auto tx = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + X_OFFSET], quant_info);
                    auto ty = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + Y_OFFSET], quant_info);
                    auto tw = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + W_OFFSET], quant_info);
                    auto th = dequantize_and_sigmoid<DstType, SrcType>(data[entry_idx + H_OFFSET], quant_info);
                    auto bbox = decode(tx, ty, tw, th, layer_anchors[anchor * 2], layer_anchors[anchor * 2 + 1], col, row,
                        shape.width, shape.height);

                    decode_classes_scores(detections, classes_detections_count, bbox, quant_info, data, entry_idx,
                        CLASSES_START_INDEX, objectness, padded_shape.width);
                }
            }
        }

        return HAILO_SUCCESS;
    }
private:
   std::shared_ptr<Yolov5OpMetadata> m_metadata;

};

} // namespace net_flow
} // namespace hailort

#endif // _HAILO_YOLO_POST_PROCESS_HPP_
