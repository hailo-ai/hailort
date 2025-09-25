/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file yolov5_seg_post_process.cpp
 * @brief YOLOv5 Instance Segmentation post-process implementation
 **/

#include "yolov5_seg_post_process.hpp"
#include "hailo/hailort.h"

#include "transform/eigen.hpp"

#ifndef _MSC_VER
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif // Not MSC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
#ifndef _MSC_VER
#pragma GCC diagnostic pop
#endif // Not MSC

namespace hailort
{
namespace net_flow
{

constexpr uint32_t VECTOR_DIM = 1;
using Eigen_Vector32f = Eigen::Matrix<float32_t, MASK_COEFFICIENT_SIZE, VECTOR_DIM>;

Expected<std::shared_ptr<OpMetadata>> Yolov5SegOpMetadata::create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
    const std::unordered_map<std::string, BufferMetaData> &outputs_metadata, const NmsPostProcessConfig &nms_post_process_config,
    const YoloPostProcessConfig &yolo_config, const YoloV5SegPostProcessConfig &yolo_seg_config,
    const std::string &network_name)
{
    auto op_metadata = std::shared_ptr<Yolov5SegOpMetadata>(new (std::nothrow) Yolov5SegOpMetadata(inputs_metadata, outputs_metadata,
        nms_post_process_config, yolo_config, yolo_seg_config, network_name));
    CHECK_AS_EXPECTED(op_metadata != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = op_metadata->validate_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return std::shared_ptr<OpMetadata>(std::move(op_metadata));
}

hailo_status Yolov5SegOpMetadata::validate_params()
{
    CHECK(!nms_config().bbox_only, HAILO_INVALID_ARGUMENT, "YOLOv5SegPostProcessOp: bbox_only is not supported for YOLOv5Seg model");

    return Yolov5OpMetadata::validate_params();
}

hailo_status Yolov5SegOpMetadata::validate_format_info()
{
    for (const auto& output_metadata : m_outputs_metadata) {
        CHECK(HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK == output_metadata.second.format.order, HAILO_INVALID_ARGUMENT,
            "The given output format order {} is not supported, should be `HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK`",
            HailoRTCommon::get_format_order_str(output_metadata.second.format.order));

        CHECK(HAILO_FORMAT_TYPE_FLOAT32 == output_metadata.second.format.type, HAILO_INVALID_ARGUMENT,
            "The given output format type {} is not supported, should be `HAILO_FORMAT_TYPE_FLOAT32`",
            HailoRTCommon::get_format_type_str(output_metadata.second.format.type));

        CHECK(!(HAILO_FORMAT_FLAGS_TRANSPOSED & output_metadata.second.format.flags), HAILO_INVALID_ARGUMENT,
            "Output {} is marked as transposed, which is not supported for this model.", output_metadata.first);
    }

    assert(1 <= m_inputs_metadata.size());
    for (const auto& input_metadata : m_inputs_metadata) {
        CHECK(HAILO_FORMAT_ORDER_NHCW == input_metadata.second.format.order, HAILO_INVALID_ARGUMENT,
            "The given input format order {} is not supported, should be `HAILO_FORMAT_ORDER_NHCW`",
            HailoRTCommon::get_format_order_str(input_metadata.second.format.order));

        CHECK((HAILO_FORMAT_TYPE_UINT8 == input_metadata.second.format.type) ||
            (HAILO_FORMAT_TYPE_UINT16 == input_metadata.second.format.type), HAILO_INVALID_ARGUMENT,
            "The given input format type {} is not supported, should be `HAILO_FORMAT_TYPE_UINT8` or `HAILO_FORMAT_TYPE_UINT16`",
            HailoRTCommon::get_format_type_str(input_metadata.second.format.type));
    }

    return HAILO_SUCCESS;
}

std::string Yolov5SegOpMetadata::get_op_description()
{
    auto yolo_config_info = Yolov5OpMetadata::get_op_description();
    auto config_info = fmt::format("{}, Mask threshold: {:.2f}",
                        yolo_config_info, m_yolo_seg_config.mask_threshold);
    return config_info;
}

Expected<hailo_vstream_info_t> Yolov5SegOpMetadata::get_output_vstream_info()
{
    TRY(auto vstream_info, NmsOpMetadata::get_output_vstream_info());
    vstream_info.nms_shape.max_accumulated_mask_size = m_yolo_seg_config.max_accumulated_mask_size;
    return vstream_info;
}

Expected<std::shared_ptr<Op>> Yolov5SegPostProcess::create(std::shared_ptr<Yolov5SegOpMetadata> metadata)
{
    auto status = metadata->validate_format_info();
    CHECK_SUCCESS_AS_EXPECTED(status);

    // Create help buffers
    assert(contains(metadata->inputs_metadata(), metadata->yolov5seg_config().proto_layer_name));
    auto proto_layer_metadata = metadata->inputs_metadata().at(metadata->yolov5seg_config().proto_layer_name);
    auto transformed_proto_layer_frame_size = HailoRTCommon::get_shape_size(proto_layer_metadata.shape) * sizeof(float32_t);
    TRY(auto transformed_proto_buffer,
        Buffer::create(transformed_proto_layer_frame_size));
    TRY(auto mask_mult_result_buffer,
        Buffer::create(proto_layer_metadata.shape.height * proto_layer_metadata.shape.width * sizeof(float32_t)));

    const auto image_size = static_cast<uint32_t>(metadata->yolov5_config().image_width) * static_cast<uint32_t>(metadata->yolov5_config().image_height);
    TRY(auto resized_buffer, Buffer::create(image_size * sizeof(float32_t)));

    auto op = std::shared_ptr<Yolov5SegPostProcess>(new (std::nothrow) Yolov5SegPostProcess(std::move(metadata),
        std::move(mask_mult_result_buffer), std::move(resized_buffer), std::move(transformed_proto_buffer)));
    CHECK_NOT_NULL_AS_EXPECTED(op, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

Yolov5SegPostProcess::Yolov5SegPostProcess(std::shared_ptr<Yolov5SegOpMetadata> metadata,
    Buffer &&mask_mult_result_buffer, Buffer &&resized_mask, Buffer &&transformed_proto_buffer)
    : YOLOv5PostProcessOp(static_cast<std::shared_ptr<Yolov5OpMetadata>>(metadata)), m_metadata(metadata),
    m_mask_mult_result_buffer(std::move(mask_mult_result_buffer)),
    m_resized_mask_to_image_dim(std::move(resized_mask)),
    m_transformed_proto_buffer(std::move(transformed_proto_buffer))
{}

hailo_status Yolov5SegPostProcess::execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
{
    const auto &inputs_metadata = m_metadata->inputs_metadata();
    const auto &yolo_config = m_metadata->yolov5_config();
    const auto &yolov5seg_config = m_metadata->yolov5seg_config();

    clear_before_frame();
    for (const auto &name_to_input : inputs) {
        hailo_status status = HAILO_UNINITIALIZED;
        auto &name = name_to_input.first;
        assert(contains(inputs_metadata, name));
        auto &input_metadata = inputs_metadata.at(name);

        CHECK(((input_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) || (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT8)),
            HAILO_INVALID_ARGUMENT, "YOLO post-process received invalid input type {}", static_cast<int>(input_metadata.format.type));

        // Prepare proto layer
        if (name == yolov5seg_config.proto_layer_name) {
            if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT8) {
                transform_proto_layer<float32_t, uint8_t>((uint8_t*)name_to_input.second.data(), input_metadata.quant_info);
            } else if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) {
                transform_proto_layer<float32_t, uint16_t>((uint16_t*)name_to_input.second.data(), input_metadata.quant_info);
            }
            // Skip bbox extraction if the input is proto layer (the mask layer)
            continue;
        }

        assert(contains(yolo_config.anchors, name));
        if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT8) {
            status = extract_detections<float32_t, uint8_t>(name_to_input.second, input_metadata.quant_info, input_metadata.shape,
                input_metadata.padded_shape, yolo_config.anchors.at(name));
        } else if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) {
            status = extract_detections<float32_t, uint16_t>(name_to_input.second, input_metadata.quant_info, input_metadata.shape,
                input_metadata.padded_shape, yolo_config.anchors.at(name));
        }
        CHECK_SUCCESS(status);
    }

    remove_overlapping_boxes(m_detections, m_classes_detections_count, m_metadata->nms_config().nms_iou_th);
    auto status = fill_nms_with_byte_mask_format(outputs.begin()->second);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

uint32_t Yolov5SegPostProcess::get_entry_size()
{
    return (CLASSES_START_INDEX + m_metadata->nms_config().number_of_classes + MASK_COEFFICIENT_SIZE);
}

void Yolov5SegPostProcess::mult_mask_vector_and_proto_matrix(const DetectionBbox &detection)
{
    static auto shape = get_proto_layer_shape();
    static uint32_t proto_mat_cols = shape.height * shape.width;

    Eigen::Map<Eigen::Matrix<float, MASK_COEFFICIENT_SIZE, Eigen::Dynamic, Eigen::RowMajor>> proto_layer(
        (float32_t*)m_transformed_proto_buffer.data(), MASK_COEFFICIENT_SIZE, proto_mat_cols);

    Eigen_Vector32f coefficients(detection.m_coefficients.data());
    auto mult_result = (coefficients.transpose() * proto_layer);

    Eigen::Map<Eigen::Matrix<float, VECTOR_DIM, Eigen::Dynamic, Eigen::RowMajor>> result(
        (float32_t*)m_mask_mult_result_buffer.data(), VECTOR_DIM, proto_mat_cols);
    result = 1.0f / (1.0f + (-1*mult_result).array().exp());
}

hailo_status Yolov5SegPostProcess::crop_and_copy_mask(const DetectionBbox &detection, MemoryView &buffer, uint32_t buffer_offset)
{
    auto &yolov5_config = m_metadata->yolov5_config();
    auto mask_threshold = m_metadata->yolov5seg_config().mask_threshold;

    // Based on Bilinear interpolation algorithm
    // TODO: HRT-11734 - Improve performance by resizing only the mask part if possible
    auto proto_layer_shape = get_proto_layer_shape();
    float32_t* resized_mask_to_image_dim_ptr = (float32_t*)m_resized_mask_to_image_dim.data();
    stbir_resize_float_generic((float32_t*)m_mask_mult_result_buffer.data(), proto_layer_shape.width,
        proto_layer_shape.height, 0, resized_mask_to_image_dim_ptr, static_cast<uint32_t>(yolov5_config.image_width),
        static_cast<uint32_t>(yolov5_config.image_height), 0, 1, STBIR_ALPHA_CHANNEL_NONE, 0,
        STBIR_EDGE_CLAMP, STBIR_FILTER_TRIANGLE, STBIR_COLORSPACE_LINEAR, NULL);

    auto x_min = static_cast<uint32_t>(MAX(std::ceil(detection.m_bbox.x_min * yolov5_config.image_width), 0.0f));
    auto x_max = static_cast<uint32_t>(MIN(std::ceil(detection.m_bbox.x_max * yolov5_config.image_width), yolov5_config.image_width));
    auto y_min = static_cast<uint32_t>(MAX(std::ceil(detection.m_bbox.y_min * yolov5_config.image_height), 0.0f));
    auto y_max = static_cast<uint32_t>(MIN(std::ceil(detection.m_bbox.y_max * yolov5_config.image_height), yolov5_config.image_height));
    auto box_width = detection.get_bbox_width(yolov5_config.image_width);

    uint8_t *dst_mask = (uint8_t*)(buffer.data() + buffer_offset);
    for (uint32_t i = y_min; i <= y_max; i++) {
        for (uint32_t j = x_min; j <= x_max; j++) {
            auto image_mask_idx = (i * static_cast<uint32_t>(yolov5_config.image_width)) + j;
            auto cropped_mask_idx = ((i-y_min) * box_width) + (j-x_min);

            if (resized_mask_to_image_dim_ptr[image_mask_idx] > mask_threshold) {
                dst_mask[cropped_mask_idx] = 1;
            } else {
                dst_mask[cropped_mask_idx] = 0;
            }
        }
    }

    return HAILO_SUCCESS;
}

hailo_status Yolov5SegPostProcess::calc_and_copy_mask(const DetectionBbox &detection, MemoryView &buffer, uint32_t buffer_offset)
{
    mult_mask_vector_and_proto_matrix(detection);
    auto status = crop_and_copy_mask(detection, buffer, buffer_offset);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<uint32_t> Yolov5SegPostProcess::copy_detection_to_result_buffer(MemoryView &buffer, DetectionBbox &detection,
    uint32_t buffer_offset)
{
    uint32_t detection_size = sizeof(detection.m_bbox_with_mask);
    uint32_t mask_size = static_cast<uint32_t>(detection.m_bbox_with_mask.mask_size);
    CHECK((buffer_offset + detection_size + mask_size) < buffer.size(), HAILO_INSUFFICIENT_BUFFER,
        "The given buffer is too small to contain all detections." \
        " The output buffer will contain the highest scored detections that could be filled." \
        " One can use `set_nms_max_accumulated_mask_size` to change the output buffer size.");

    // Copy bbox
    uint32_t copied_bytes_amount = 0;
    detection.m_bbox_with_mask.mask = (buffer.data() + buffer_offset + detection_size);

    *(hailo_detection_with_byte_mask_t*)(buffer.data() + buffer_offset) =
        *(hailo_detection_with_byte_mask_t*)&(detection.m_bbox_with_mask);
    buffer_offset += detection_size;
    copied_bytes_amount += detection_size;

    // Calc and copy mask
    auto status = calc_and_copy_mask(detection, buffer, buffer_offset);
    CHECK_SUCCESS_AS_EXPECTED(status);
    copied_bytes_amount += mask_size;

    m_classes_detections_count[detection.m_class_id]--;
    return copied_bytes_amount;
}

hailo_status Yolov5SegPostProcess::fill_nms_with_byte_mask_format(MemoryView &buffer)
{
    auto status = HAILO_SUCCESS;
    const auto &nms_config = m_metadata->nms_config();
    uint16_t detections_count = 0;
    // The beginning of the output buffer will contain the detections_count first, here we save space for it.
    uint32_t buffer_offset = sizeof(detections_count);
    // Note: Assuming the m_detections is sorted by score (it's done in remove_overlapping_boxes())
    for (auto &detection : m_detections) {
        if (REMOVED_CLASS_SCORE == detection.m_bbox.score) {
            // Detection overlapped with a higher score detection and removed in remove_overlapping_boxes()
            continue;
        }

        detections_count++;
        uint32_t max_proposals_total = nms_config.max_proposals_total;
        // TODO: HRT-15885 remove support for max_proposals_per_class in YOLOv5Seg
        if (HailoRTCommon::is_nms_by_class(m_metadata->outputs_metadata().begin()->second.format.order)) {
            max_proposals_total = nms_config.max_proposals_per_class * nms_config.number_of_classes;
        }
        if (detections_count > max_proposals_total) {
            LOGGER__INFO("{} detections were ignored, due to `max_bboxes_total` defined as {}.",
                detections_count - max_proposals_total, max_proposals_total);
            break;
        }

        auto copied_bytes_amount = copy_detection_to_result_buffer(buffer, detection, buffer_offset);
        if (HAILO_INSUFFICIENT_BUFFER == copied_bytes_amount.status()) {
            status = copied_bytes_amount.status();
            break;
        }
        CHECK_EXPECTED_AS_STATUS(copied_bytes_amount); // TODO (HRT-13278): Figure out how to remove CHECK_EXPECTED here
        buffer_offset += copied_bytes_amount.release();
    }

    // Copy detections count to the beginning of the buffer
    *(uint16_t*)buffer.data() = detections_count;

    return status;
}

} /* namespace net_flow */
} /* namespace hailort */
