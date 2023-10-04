/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file yolov5_seg_post_process.cpp
 * @brief YOLOv5 Instance Segmentation post-process implementation
 **/

#include "yolov5_seg_post_process.hpp"
#include "hailo/hailort.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#else
#pragma GCC diagnostic pop
#endif

namespace hailort
{
namespace net_flow
{

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
        CHECK(!(HAILO_FORMAT_FLAGS_HOST_ARGMAX & output_metadata.second.format.flags), HAILO_INVALID_ARGUMENT,
            "Output {} is marked as argmax, which is not supported for this model.", output_metadata.first);
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
    auto vstream_info = NmsOpMetadata::get_output_vstream_info();
    CHECK_EXPECTED(vstream_info);

    vstream_info->nms_shape.max_mask_size = static_cast<uint32_t>(yolov5_config().image_height * yolov5_config().image_width);
    return vstream_info.release();
}

Expected<std::shared_ptr<Op>> Yolov5SegPostProcess::create(std::shared_ptr<Yolov5SegOpMetadata> metadata)
{
    auto status = metadata->validate_format_info();
    CHECK_SUCCESS_AS_EXPECTED(status);

    // Create help buffers
    assert(contains(metadata->inputs_metadata(), metadata->yolov5seg_config().proto_layer_name));
    auto proto_layer_metadata = metadata->inputs_metadata().at(metadata->yolov5seg_config().proto_layer_name);
    auto transformed_proto_layer_frame_size = HailoRTCommon::get_shape_size(proto_layer_metadata.shape) * sizeof(float32_t);
    auto transformed_proto_buffer = Buffer::create(transformed_proto_layer_frame_size);
    CHECK_EXPECTED(transformed_proto_buffer);
    auto dequantized_proto_buffer = Buffer::create(transformed_proto_layer_frame_size);
    CHECK_EXPECTED(dequantized_proto_buffer);
    auto mask_mult_result_buffer = Buffer::create(proto_layer_metadata.shape.height * proto_layer_metadata.shape.width * sizeof(float32_t));
    CHECK_EXPECTED(mask_mult_result_buffer);

    auto image_size = static_cast<uint32_t>(metadata->yolov5_config().image_width) * static_cast<uint32_t>(metadata->yolov5_config().image_height);
    auto resized_buffer = Buffer::create(image_size * sizeof(float32_t));
    CHECK_EXPECTED(resized_buffer);

    auto op = std::shared_ptr<Yolov5SegPostProcess>(new (std::nothrow) Yolov5SegPostProcess(std::move(metadata),
        mask_mult_result_buffer.release(), resized_buffer.release(), transformed_proto_buffer.release(), dequantized_proto_buffer.release()));
    CHECK_NOT_NULL_AS_EXPECTED(op, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

Yolov5SegPostProcess::Yolov5SegPostProcess(std::shared_ptr<Yolov5SegOpMetadata> metadata,
    Buffer &&mask_mult_result_buffer, Buffer &&resized_mask, Buffer &&transformed_proto_buffer, Buffer &&dequantized_proto_buffer)
    : YOLOv5PostProcessOp(static_cast<std::shared_ptr<Yolov5OpMetadata>>(metadata)), m_metadata(metadata),
    m_mask_mult_result_buffer(std::move(mask_mult_result_buffer)),
    m_resized_mask_to_image_dim(std::move(resized_mask)),
    m_transformed_proto_buffer(std::move(transformed_proto_buffer)),
    m_dequantized_proto_buffer(std::move(dequantized_proto_buffer))
{}

hailo_status Yolov5SegPostProcess::execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
{
    const auto &inputs_metadata = m_metadata->inputs_metadata();
    const auto &yolo_config = m_metadata->yolov5_config();
    const auto &yolov5seg_config = m_metadata->yolov5seg_config();
    const auto &nms_config = m_metadata->nms_config();

    std::vector<DetectionBbox> detections;
    std::vector<uint32_t> classes_detections_count(nms_config.number_of_classes, 0);
    detections.reserve(nms_config.max_proposals_per_class * nms_config.number_of_classes);
    for (const auto &name_to_input : inputs) {
        hailo_status status;
        auto &name = name_to_input.first;
        assert(contains(inputs_metadata, name));
        auto &input_metadata = inputs_metadata.at(name);

        CHECK(((input_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) || (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT8)),
            HAILO_INVALID_ARGUMENT, "YOLO post-process received invalid input type {}", input_metadata.format.type);

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
                input_metadata.padded_shape, yolo_config.anchors.at(name), detections, classes_detections_count);
        } else if (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) {
            status = extract_detections<float32_t, uint16_t>(name_to_input.second, input_metadata.quant_info, input_metadata.shape,
                input_metadata.padded_shape, yolo_config.anchors.at(name), detections, classes_detections_count);
        }
        CHECK_SUCCESS(status);
    }

    remove_overlapping_boxes(detections, classes_detections_count, m_metadata->nms_config().nms_iou_th);
    auto status = fill_nms_with_byte_mask_format(outputs.begin()->second, detections, classes_detections_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

uint32_t Yolov5SegPostProcess::get_entry_size()
{
    return (CLASSES_START_INDEX + m_metadata->nms_config().number_of_classes + MASK_COEFFICIENT_SIZE);
}

void Yolov5SegPostProcess::mult_mask_vector_and_proto_matrix(const DetectionBbox &detection)
{
    float32_t *proto_layer = (float32_t*)m_transformed_proto_buffer.data();
    float32_t *mult_result = (float32_t*)m_mask_mult_result_buffer.data();

    auto proto_layer_shape = get_proto_layer_shape();
    uint32_t mult_size = proto_layer_shape.height * proto_layer_shape.width;
    for (uint32_t i = 0; i < mult_size; i++) {
        float32_t sum = 0.0f;
        for (uint32_t j = 0; j < proto_layer_shape.features; j++) {
            sum += detection.m_mask[j] * proto_layer[j * mult_size + i];
        }
        mult_result[i] = sigmoid(sum);
    }
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

    auto x_min = static_cast<uint32_t>(std::round(detection.m_bbox.x_min * yolov5_config.image_width));
    auto x_max = static_cast<uint32_t>(std::round(detection.m_bbox.x_max * yolov5_config.image_width));
    auto y_min = static_cast<uint32_t>(std::round(detection.m_bbox.y_min * yolov5_config.image_height));
    auto y_max = static_cast<uint32_t>(std::round(detection.m_bbox.y_max * yolov5_config.image_height));
    auto box_width = detection.get_bbox_rounded_width(yolov5_config.image_width);

    float32_t *dst_mask = (float32_t*)(buffer.data() + buffer_offset);
    for (uint32_t i = y_min; i <= y_max; i++) {
        for (uint32_t j = x_min; j <= x_max; j++) {
            auto image_mask_idx = (i * static_cast<uint32_t>(yolov5_config.image_width)) + j;
            auto cropped_mask_idx = ((i-y_min) * box_width) + (j-x_min);

            if (resized_mask_to_image_dim_ptr[image_mask_idx] > mask_threshold) {
                dst_mask[cropped_mask_idx] = 1.0f;
            } else {
                dst_mask[cropped_mask_idx] = 0.0f;
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

uint32_t Yolov5SegPostProcess::get_mask_size(const DetectionBbox &detection)
{
    auto &yolov5_config = m_metadata->yolov5_config();
    auto box_height = detection.get_bbox_rounded_height(yolov5_config.image_height);
    auto box_width = detection.get_bbox_rounded_width(yolov5_config.image_width);
    auto mask_size = box_width * box_height;

    // Add padding if needed
    uint32_t remainder = mask_size % 8;
    uint32_t adjustment = (remainder != 0) ? (8 - remainder) : 0;
    uint32_t result = static_cast<uint32_t>(mask_size + adjustment);
    return result;
}

Expected<uint32_t> Yolov5SegPostProcess::copy_detection_to_result_buffer(MemoryView &buffer, const DetectionBbox &detection,
    uint32_t buffer_offset, std::vector<uint32_t> &classes_detections_count)
{
    auto detection_byte_size = 0;
    float32_t mask_size_bytes = static_cast<float32_t>(get_mask_size(detection)) * sizeof(float32_t);

    // Copy bbox
    uint32_t size_to_copy = sizeof(detection.m_bbox);
    assert((buffer_offset + size_to_copy) <= buffer.size());
    memcpy((hailo_bbox_float32_t*)(buffer.data() + buffer_offset), &detection.m_bbox, size_to_copy);
    buffer_offset += size_to_copy;
    detection_byte_size += size_to_copy;

    // Copy mask size
    size_to_copy = sizeof(mask_size_bytes);
    assert((buffer_offset + size_to_copy) <= buffer.size());
    memcpy((buffer.data() + buffer_offset), &mask_size_bytes, size_to_copy);
    buffer_offset += size_to_copy;
    detection_byte_size += size_to_copy;

    // Calc and copy mask
    auto status = calc_and_copy_mask(detection, buffer, buffer_offset);
    CHECK_SUCCESS_AS_EXPECTED(status);
    detection_byte_size += static_cast<uint32_t>(mask_size_bytes);

    classes_detections_count[detection.m_class_id]--;
    return detection_byte_size;
}

uint32_t Yolov5SegPostProcess::copy_bbox_count_to_result_buffer(MemoryView &buffer, uint32_t class_detection_count, uint32_t buffer_offset)
{
    float32_t bbox_count_casted = static_cast<float32_t>(class_detection_count);
    uint32_t size_to_copy = sizeof(bbox_count_casted);

    assert((buffer_offset + size_to_copy) <= buffer.size());
    memcpy((buffer.data() + buffer_offset), &bbox_count_casted, size_to_copy);
    return size_to_copy;
}

uint32_t Yolov5SegPostProcess::copy_zero_bbox_count(MemoryView &buffer, uint32_t classes_with_zero_detections_count, uint32_t buffer_offset)
{
    uint32_t size_to_copy = static_cast<uint32_t>(sizeof(float32_t)) * classes_with_zero_detections_count;

    assert((buffer_offset + size_to_copy) <= buffer.size());
    memset((buffer.data() + buffer_offset), 0, size_to_copy);
    return size_to_copy;
}

hailo_status Yolov5SegPostProcess::fill_nms_with_byte_mask_format(MemoryView &buffer, std::vector<DetectionBbox> &detections,
    std::vector<uint32_t> &classes_detections_count)
{
    // TODO: HRT-11734 - Improve performance by adding a new format that doesn't require the sort
    // Sort by class_id
    std::sort(detections.begin(), detections.end(),
        [](DetectionBbox a, DetectionBbox b)
        { return (a.m_class_id != b.m_class_id) ? (a.m_class_id < b.m_class_id) : (a.m_bbox.score > b.m_bbox.score); });

    const auto &nms_config = m_metadata->nms_config();
    uint32_t ignored_detections_count = 0;
    int curr_class_id = -1;
    uint32_t buffer_offset = 0;
    for (auto &detection : detections) {
        if (REMOVED_CLASS_SCORE == detection.m_bbox.score) {
            // Detection was removed in remove_overlapping_boxes()
            continue;
        }
        if (0 == classes_detections_count[detection.m_class_id]) {
            // This class' detections count is higher then m_nms_config.max_proposals_per_class.
            // This detection is ignored due to having lower score (detections vector is sorted by score).
            continue;
        }

        // If class's detections count is higher then max_proposals_per_class we set the detection count of that class to the max
        // and ignore the rest by reducing the classes_detections_count[detection.m_class_id] after copying the bbox to result buffer.
        if (nms_config.max_proposals_per_class < classes_detections_count[detection.m_class_id]) {
            ignored_detections_count += (classes_detections_count[detection.m_class_id] - nms_config.max_proposals_per_class);
            classes_detections_count[detection.m_class_id] = nms_config.max_proposals_per_class;
        }

        if (static_cast<int>(detection.m_class_id) == curr_class_id) {
            auto buffer_offset_expected = copy_detection_to_result_buffer(buffer, detection, buffer_offset, classes_detections_count);
            CHECK_EXPECTED_AS_STATUS(buffer_offset_expected);
            buffer_offset += buffer_offset_expected.value();
        }
        else if (static_cast<int>(detection.m_class_id) == (curr_class_id + 1)) {
            buffer_offset += copy_bbox_count_to_result_buffer(buffer, classes_detections_count[detection.m_class_id], buffer_offset);
            auto buffer_offset_expected = copy_detection_to_result_buffer(buffer, detection, buffer_offset, classes_detections_count);
            buffer_offset += buffer_offset_expected.value();
            curr_class_id = detection.m_class_id;
        }
        else {
            // no detections for classes between (curr_class_id, detection.m_class_id)
            auto zero_detections_classes_count = (detection.m_class_id - curr_class_id);
            buffer_offset += copy_zero_bbox_count(buffer, zero_detections_classes_count, buffer_offset);

            // Copy the new class box
            buffer_offset += copy_bbox_count_to_result_buffer(buffer, classes_detections_count[detection.m_class_id], buffer_offset);
            auto buffer_offset_expected = copy_detection_to_result_buffer(buffer, detection, buffer_offset, classes_detections_count);
            buffer_offset += buffer_offset_expected.value();
            curr_class_id = detection.m_class_id;
        }
    }

    if (0 != ignored_detections_count) {
        LOGGER__INFO("{} Detections were ignored, due to `max_bboxes_per_class` defined as {}.",
            ignored_detections_count, nms_config.max_proposals_per_class);
    }

    return HAILO_SUCCESS;
}

} /* namespace net_flow */
} /* namespace hailort */
