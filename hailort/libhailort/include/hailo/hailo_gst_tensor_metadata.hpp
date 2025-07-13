/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailo_gst_tensor_metadata.hpp
 * @brief Definitions for hailo tensor metadata
 **/

#ifndef _HAILO_GST_TENSOR_METADATA_HPP_
#define _HAILO_GST_TENSOR_METADATA_HPP_

#include "hailo/platform.h"
#include <limits.h>


#define HAILO_MAX_ENUM (INT_MAX)
#define HAILO_MAX_NAME_SIZE (128)
#define HAILO_MAX_STREAM_NAME_SIZE (HAILO_MAX_NAME_SIZE)

typedef float float32_t;

enum class HailoTensorFormatType {
    /** Chosen automatically to match the format expected by the device, usually UINT8 */
    HAILO_FORMAT_TYPE_AUTO                  = 0,

    /** Data format type uint8_t - 1 byte per item, host/device side */
    HAILO_FORMAT_TYPE_UINT8                 = 1,

    /** Data format type uint16_t - 2 bytes per item, host/device side */
    HAILO_FORMAT_TYPE_UINT16                = 2,

    /** Data format type float32_t - used only on host side (Translated in the quantization process) */
    HAILO_FORMAT_TYPE_FLOAT32               = 3,

    /** Max enum value to maintain ABI Integrity */
    HAILO_FORMAT_TYPE_MAX_ENUM              = HAILO_MAX_ENUM
};

/** Hailo data format */
struct hailo_tensor_format_t {
    HailoTensorFormatType type;
    bool is_nms;
};

struct hailo_tensor_3d_image_shape_t {
    uint32_t height;
    uint32_t width;
    uint32_t features;
};

struct hailo_tensor_nms_shape_t {
    /** Amount of NMS classes */
    uint32_t number_of_classes;
    /** Maximum amount of bboxes per nms class */
    uint32_t max_bboxes_per_class;
    /** Maximum amount of total bboxes */
    uint32_t max_bboxes_total;
    /** Maximum accumulated mask size for all of the detections in a frame.
     *  Used only with 'HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK' format order.
     *  The default value is (`input_image_size` * 2)
     */
    uint32_t max_accumulated_mask_size;
};

struct hailo_tensor_quant_info_t {
    /** zero_point */
    float32_t qp_zp;

    /** scale */
    float32_t qp_scale;

    /** min limit value */
    float32_t limvals_min;

    /** max limit value */
    float32_t limvals_max;
};

struct HAILORTAPI hailo_tensor_metadata_t {
    char name[HAILO_MAX_STREAM_NAME_SIZE];
    hailo_tensor_format_t format;

    union
    {
        /* Frame shape */
        hailo_tensor_3d_image_shape_t shape;
        /* NMS shape, only valid if format.is_nms is true */
        hailo_tensor_nms_shape_t nms_shape;
    };

    hailo_tensor_quant_info_t quant_info;

    hailo_tensor_metadata_t() = default;
};

#endif // _HAILO_GST_TENSOR_METADATA_HPP_