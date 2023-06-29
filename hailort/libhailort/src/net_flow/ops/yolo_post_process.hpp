/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file yolo_post_process.hpp
 * @brief YOLO post process
 *
 * https://learnopencv.com/object-detection-using-yolov5-and-opencv-dnn-in-c-and-python :
 * The headline '4.3.5 POST-PROCESSING YOLOv5 Prediction Output' contains explanations on the YOLOv5 post-processing.
 **/

#ifndef _HAILO_YOLO_POST_PROCESS_HPP_
#define _HAILO_YOLO_POST_PROCESS_HPP_

#include "net_flow/ops/nms_post_process.hpp"

namespace hailort
{
namespace net_flow
{

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


class YOLOPostProcessOp : public NmsPostProcessOp
{
public:
    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;
    std::string get_op_description() override;
    virtual hailo_status validate_metadata() = 0; // TODO: HRT-10676

protected:
    virtual hailo_bbox_float32_t decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
        int wa, int ha, uint32_t col, uint32_t row, uint32_t w_stride, uint32_t h_stride) const = 0;

    YOLOPostProcessOp(const std::map<std::string, BufferMetaData> &inputs_metadata,
                      const std::map<std::string, BufferMetaData> &outputs_metadata,
                      const NmsPostProcessConfig &nms_post_process_config,
                      const YoloPostProcessConfig &yolo_post_process_config,
                      const std::string &name)
        : NmsPostProcessOp(inputs_metadata, outputs_metadata, nms_post_process_config, name)
        , m_yolo_config(yolo_post_process_config)
    {}

    YoloPostProcessConfig m_yolo_config;

private:
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
    template<typename HostType = float32_t, typename DeviceType>
    hailo_status extract_detections(const MemoryView &buffer, hailo_quant_info_t quant_info,
        hailo_3d_image_shape_t shape, hailo_3d_image_shape_t padded_shape,
        const std::vector<int> &layer_anchors, std::vector<DetectionBbox> &detections, std::vector<uint32_t> &classes_detections_count)
    {
        static const uint32_t X_INDEX = 0;
        static const uint32_t Y_INDEX = 1;
        static const uint32_t W_INDEX = 2;
        static const uint32_t H_INDEX = 3;
        static const uint32_t OBJECTNESS_INDEX = 4;
        static const uint32_t CLASSES_START_INDEX = 5;

        const uint32_t X_OFFSET = X_INDEX * padded_shape.width;
        const uint32_t Y_OFFSET = Y_INDEX * padded_shape.width;
        const uint32_t W_OFFSET = W_INDEX * padded_shape.width;
        const uint32_t H_OFFSET = H_INDEX * padded_shape.width;
        const uint32_t OBJECTNESS_OFFSET = OBJECTNESS_INDEX * padded_shape.width;

        // Each layer anchors vector is structured as {w,h} pairs.
        // For example, if we have a vector of size 6 (default YOLOv5 vector) then we have 3 anchors for this layer.
        assert(layer_anchors.size() % 2 == 0);
        const size_t num_of_anchors = (layer_anchors.size() / 2);

        uint32_t entry_size = (uint32_t)(CLASSES_START_INDEX + m_nms_config.number_of_classes);
        auto number_of_entries = padded_shape.height * padded_shape.width * num_of_anchors;
        // TODO: this can also be part of the Op configuration
        auto buffer_size = number_of_entries * entry_size * sizeof(DeviceType);
        CHECK(buffer_size == buffer.size(), HAILO_INVALID_ARGUMENT,
            "Failed to extract_detections, buffer_size should be {}, but is {}", buffer_size, buffer.size());

        auto row_size = padded_shape.width * padded_shape.features;
        DeviceType *data = (DeviceType*)buffer.data();
        for (uint32_t row = 0; row < shape.height; row++) {
            for (uint32_t col = 0; col < shape.width; col++) {
                for (uint32_t anchor = 0; anchor < num_of_anchors; anchor++) {
                    auto entry_idx = (row_size * row) + col + ((anchor * entry_size) * padded_shape.width);
                    auto objectness = Quantization::dequantize_output<HostType, DeviceType>(data[entry_idx + OBJECTNESS_OFFSET], quant_info);
                    if (objectness < m_nms_config.nms_score_th) {
                        continue;
                    }
                    
                    auto tx = Quantization::dequantize_output<HostType, DeviceType>(data[entry_idx + X_OFFSET], quant_info);
                    auto ty = Quantization::dequantize_output<HostType, DeviceType>(data[entry_idx + Y_OFFSET], quant_info);
                    auto tw = Quantization::dequantize_output<HostType, DeviceType>(data[entry_idx + W_OFFSET], quant_info);
                    auto th = Quantization::dequantize_output<HostType, DeviceType>(data[entry_idx + H_OFFSET], quant_info);
                    auto bbox = decode(tx, ty, tw, th, layer_anchors[anchor * 2], layer_anchors[anchor * 2 + 1], col, row,
                        shape.width, shape.height);

                    // Source for the calculations - https://github.com/ultralytics/yolov5/blob/HEAD/models/yolo.py
                    // Explanations for the calculations - https://github.com/ultralytics/yolov5/issues/471
                    if (m_nms_config.cross_classes) {
                        // Pre-NMS optimization. If NMS checks IOU over different classes, only the maximum class is relevant
                        auto max_id_score_pair = get_max_class<HostType, DeviceType>(data, entry_idx, CLASSES_START_INDEX, objectness, quant_info, padded_shape.width);
                        bbox.score = max_id_score_pair.second;
                        if (max_id_score_pair.second >= m_nms_config.nms_score_th) {
                            detections.emplace_back(DetectionBbox(bbox, max_id_score_pair.first));
                            classes_detections_count[max_id_score_pair.first]++;
                        }
                    }
                    else {
                        for (uint32_t class_index = 0; class_index < m_nms_config.number_of_classes; class_index++) {
                            auto class_entry_idx = entry_idx + ((CLASSES_START_INDEX + class_index) * padded_shape.width);
                            auto class_confidence = Quantization::dequantize_output<HostType, DeviceType>(
                                data[class_entry_idx], quant_info);
                            auto class_score = class_confidence * objectness;
                            if (class_score >= m_nms_config.nms_score_th) {
                                bbox.score = class_score;
                                detections.emplace_back(DetectionBbox(bbox, class_index));
                                classes_detections_count[class_index]++;
                            }
                        }
                    }
                }
            }
        }
        
        return HAILO_SUCCESS;
    }
};

class YOLOv5PostProcessOp : public YOLOPostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(const std::map<std::string, BufferMetaData> &inputs_metadata,
                                                const std::map<std::string, BufferMetaData> &outputs_metadata,
                                                const NmsPostProcessConfig &nms_post_process_config,
                                                const YoloPostProcessConfig &yolo_post_process_config);
    hailo_status validate_metadata() override; // TODO: HRT-10676

protected:
    virtual hailo_bbox_float32_t decode(float32_t tx, float32_t ty, float32_t tw, float32_t th,
        int wa, int ha, uint32_t col, uint32_t row, uint32_t w_stride, uint32_t h_stride) const override;

private:
    YOLOv5PostProcessOp(const std::map<std::string, BufferMetaData> &inputs_metadata,
                        const std::map<std::string, BufferMetaData> &outputs_metadata,
                        const NmsPostProcessConfig &nms_post_process_config,
                        const YoloPostProcessConfig &yolo_post_process_config)
        : YOLOPostProcessOp(inputs_metadata, outputs_metadata, nms_post_process_config, yolo_post_process_config, "YOLOv5-Post-Process")
    {}
};

} // namespace net_flow
} // namespace hailort

#endif // _HAILO_YOLO_POST_PROCESS_HPP_
