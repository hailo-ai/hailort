/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "net_flow/ops_metadata/yolov5_seg_op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

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

    template<typename DstType = float32_t, typename SrcType>
    static void transform__d2h_NHCW_to_NCHW_with_dequantize(SrcType *src_ptr, hailo_3d_image_shape_t shape,
        DstType *dst_ptr, hailo_quant_info_t quant_info)
    {
        assert(nullptr != src_ptr);
        assert(nullptr != dst_ptr);

        uint32_t width_size = shape.width;
        for (uint32_t r = 0; r < shape.height; r++) {
            for (uint32_t c = 0; c < shape.features; c++) {
                SrcType *src = src_ptr + shape.features * shape.width * r + shape.width * c;
                DstType *dst = dst_ptr + shape.width * shape.height * c + shape.width * r;
                Quantization::dequantize_output_buffer<DstType, SrcType>(src, dst, width_size, quant_info);
            }
        }
    }

    // Transform proto layer - To multiply between the box mask coefficients (of shape (1, 32)), in the proto layer,
    // we change the proto layer shape to be (features=32, height * width)
    template<typename DstType = float32_t, typename SrcType>
    void transform_proto_layer(SrcType *src_buffer, const hailo_quant_info_t &quant_info)
    {
        hailo_3d_image_shape_t shape = get_proto_layer_shape();
        transform__d2h_NHCW_to_NCHW_with_dequantize<DstType, SrcType>(src_buffer, shape,
            (float32_t*)m_transformed_proto_buffer.data(), quant_info);
    }

private:
    Yolov5SegPostProcess(std::shared_ptr<Yolov5SegOpMetadata> metadata, Buffer &&mask_mult_result_buffer,
        Buffer &&resized_mask, Buffer &&transformed_proto_buffer);

    hailo_status fill_nms_with_byte_mask_format(MemoryView &buffer);
    void mult_mask_vector_and_proto_matrix(const DetectionBbox &detection);

    hailo_status calc_and_copy_mask(const DetectionBbox &detection, MemoryView &buffer, uint32_t buffer_offset);
    hailo_status crop_and_copy_mask(const DetectionBbox &detection, MemoryView &buffer, uint32_t buffer_offset);

    // Returns the number of copied bytes
    Expected<uint32_t> copy_detection_to_result_buffer(MemoryView &buffer, DetectionBbox &detection, uint32_t buffer_offset);

    std::shared_ptr<Yolov5SegOpMetadata> m_metadata;
    Buffer m_mask_mult_result_buffer;
    Buffer m_resized_mask_to_image_dim;

    Buffer m_transformed_proto_buffer;
};

} /* namespace hailort */
} /* namespace net_flow */

#endif /* _HAILO_YOLOV5_SEG_POST_PROCESS_HPP_ */
