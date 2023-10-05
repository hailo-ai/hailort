/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file yolov5_seg_post_process.hpp
 * @brief YOLOv5 Instance Segmentation Post-Process
 **/

#ifndef _HAILO_YOLOV5_SEG_POST_PROCESS_HPP_
#define _HAILO_YOLOV5_SEG_POST_PROCESS_HPP_

#include "hailo/hailort.h"
#include "net_flow/ops/yolov5_post_process.hpp"
#include "transform/transform_internal.hpp"

namespace hailort
{
namespace net_flow
{

struct YoloV5SegPostProcessConfig
{
    // User given mask threshold. A pixel will consider part of the mask if it's value is higher then the mask_threshold.
    double mask_threshold;
    std::string proto_layer_name;
};

class Yolov5SegOpMetadata : public Yolov5OpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const NmsPostProcessConfig &nms_post_process_config,
                                                        const YoloPostProcessConfig &yolov5_config,
                                                        const YoloV5SegPostProcessConfig &yolov5_seg_config,
                                                        const std::string &network_name);
    hailo_status validate_format_info() override;
    std::string get_op_description() override;
    YoloV5SegPostProcessConfig &yolov5seg_config() { return m_yolo_seg_config;};
    virtual Expected<hailo_vstream_info_t> get_output_vstream_info() override;

private:
    Yolov5SegOpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                       const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                       const NmsPostProcessConfig &nms_post_process_config,
                       const YoloPostProcessConfig &yolo_config,
                       const YoloV5SegPostProcessConfig &yolo_seg_config,
                       const std::string &network_name)
        : Yolov5OpMetadata(inputs_metadata, outputs_metadata, nms_post_process_config, "YOLOv5Seg-Post-Process",
            network_name, yolo_config, OperationType::YOLOV5SEG),
        m_yolo_seg_config(yolo_seg_config)
    {}

    YoloV5SegPostProcessConfig m_yolo_seg_config;
};

class Yolov5SegPostProcess : public YOLOv5PostProcessOp
{
public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<Yolov5SegOpMetadata> metadata);

    hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) override;

    uint32_t get_entry_size() override;

    virtual bool should_sigmoid()
    {
        return true;
    };

    virtual bool should_add_mask()
    {
        return true;
    };

    const hailo_3d_image_shape_t &get_proto_layer_shape() const
    {
        assert(contains(m_metadata->inputs_metadata(), m_metadata->yolov5seg_config().proto_layer_name));
        return m_metadata->inputs_metadata().at(m_metadata->yolov5seg_config().proto_layer_name).shape;
    };

    // Transform proto layer - To multiply between the box mask coefficients (of shape (1, 32)), in the proto layer,
    // we change the proto layer shape to be (features=32, height * width)
    template<typename DstType = float32_t, typename SrcType>
    void transform_proto_layer(SrcType *src_buffer, const hailo_quant_info_t &quant_info)
    {
        hailo_3d_image_shape_t shape = get_proto_layer_shape();

         // TODO: HRT-11734 Improve performance - Make both funcs in one run?
        Quantization::dequantize_output_buffer<float32_t, SrcType>(src_buffer, (float32_t*)m_dequantized_proto_buffer.data(),
            HailoRTCommon::get_shape_size(shape), quant_info);
        TransformContextUtils::transform__d2h_NHCW_to_NCHW<float32_t>((float32_t*)m_dequantized_proto_buffer.data(), &shape,
            (float32_t*)m_transformed_proto_buffer.data(), &shape);
    }

private:
    Yolov5SegPostProcess(std::shared_ptr<Yolov5SegOpMetadata> metadata, Buffer &&mask_mult_result_buffer,
        Buffer &&resized_mask, Buffer &&transformed_proto_buffer, Buffer &&dequantized_proto_buffer);

    hailo_status fill_nms_with_byte_mask_format(MemoryView &buffer, std::vector<DetectionBbox> &detections,
        std::vector<uint32_t> &classes_detections_count);
    void mult_mask_vector_and_proto_matrix(const DetectionBbox &detection);
    uint32_t get_mask_size(const DetectionBbox &detection);

    hailo_status calc_and_copy_mask(const DetectionBbox &detection, MemoryView &buffer, uint32_t buffer_offset);
    hailo_status crop_and_copy_mask(const DetectionBbox &detection, MemoryView &buffer, uint32_t buffer_offset);
    uint32_t copy_zero_bbox_count(MemoryView &buffer, uint32_t classes_with_zero_detections_count, uint32_t buffer_offset);
    uint32_t copy_bbox_count_to_result_buffer(MemoryView &buffer, uint32_t class_detection_count, uint32_t buffer_offset);
    Expected<uint32_t> copy_detection_to_result_buffer(MemoryView &buffer, const DetectionBbox &detection, uint32_t buffer_offset,
        std::vector<uint32_t> &classes_detections_count);

    std::shared_ptr<Yolov5SegOpMetadata> m_metadata;
    Buffer m_mask_mult_result_buffer;
    Buffer m_resized_mask_to_image_dim;

    // TODO: HRT-11734 - Try use one buffer for both actions
    Buffer m_transformed_proto_buffer;
    Buffer m_dequantized_proto_buffer;
};

} /* namespace hailort */
} /* namespace net_flow */

#endif /* _HAILO_YOLOV5_SEG_POST_PROCESS_HPP_ */
