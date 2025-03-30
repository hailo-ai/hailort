/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file transform.cpp
 * @brief Implements transform module
 **/
#include "hailo/transform.hpp"
#include "hailo/hailort.h"
#include "hailo/stream.hpp"
#include "hailo/expected.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/quantization.hpp"
#include "hailo/hailort_defaults.hpp"
#include "net_flow/ops/nms_post_process.hpp"

#include "common/compiler_extensions_compat.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include "transform/transform_internal.hpp"

#include <type_traits>
#include <sstream>


namespace hailort
{

#define RGB_FEATURES (3)
#define F8CR_MIN_FEATURES_FOR_TRANSFORMATION (8)


Expected<bool> TransformContextUtils::should_quantize_by_type(const hailo_stream_direction_t stream_direction,
    const hailo_format_type_t &src_format_type, const hailo_format_type_t &dst_format_type)
{
    if (HAILO_H2D_STREAM == stream_direction) {
        CHECK_AS_EXPECTED(HAILO_FORMAT_TYPE_FLOAT32 != dst_format_type, HAILO_INVALID_ARGUMENT,
            "dst type cant be {} on input quantization", HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_FLOAT32));
        CHECK_AS_EXPECTED(!((HAILO_FORMAT_TYPE_UINT8 == dst_format_type) && (HAILO_FORMAT_TYPE_UINT16 == src_format_type)),
            HAILO_INVALID_ARGUMENT, "src type is {}, while the model compiled for type {}. Input quantization is impossible with this src type.",
            HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT16), HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT8));
        if ((src_format_type == HAILO_FORMAT_TYPE_UINT8) && (dst_format_type == HAILO_FORMAT_TYPE_UINT16)) {
            LOGGER__WARNING("src type is {}, while the model compiled for type {}. libhailort will type-cast every value which might reduce performance. Consider recompiling the model.",
                HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT8), HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT16));
            return true;
        }
        return ((src_format_type != HAILO_FORMAT_TYPE_AUTO) && (dst_format_type != src_format_type));
    } else {
        CHECK_AS_EXPECTED(HAILO_FORMAT_TYPE_FLOAT32 != src_format_type, HAILO_INVALID_ARGUMENT,
            "src type cant be {} on output de-quantization", HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_FLOAT32));
        CHECK_AS_EXPECTED(!((HAILO_FORMAT_TYPE_UINT8 == dst_format_type) && (HAILO_FORMAT_TYPE_UINT16 == src_format_type)),
            HAILO_INVALID_ARGUMENT, "The model compiled for type {}, while the dst type is {}. Output de-quantization is impossible to this dst type",
            HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT16), HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT8));
        if ((src_format_type == HAILO_FORMAT_TYPE_UINT8) && (dst_format_type == HAILO_FORMAT_TYPE_UINT16)) {
            LOGGER__WARNING("The model compiled for type {}, while the dst type is {}. libhailort will type-cast every value which might reduce performance. Consider recompiling the model.",
                HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT8), HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT16));
            return true;
        }
        return ((dst_format_type != HAILO_FORMAT_TYPE_AUTO) && (dst_format_type != src_format_type));
    }
}

Expected<bool> TransformContextUtils::should_quantize(const hailo_stream_direction_t stream_direction, 
    const hailo_format_t &src_format, const hailo_format_t &dst_format)
{
    return TransformContextUtils::should_quantize_by_type(stream_direction, src_format.type, dst_format.type);
}

bool TransformContextUtils::should_transpose(const hailo_format_flags_t &src_flags, const hailo_format_flags_t &dst_flags)
{
    return ((HAILO_FORMAT_FLAGS_TRANSPOSED & src_flags) != (HAILO_FORMAT_FLAGS_TRANSPOSED & dst_flags));
}

bool TransformContextUtils::should_reorder(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format)
{
    /* If the orders are NHCW <-> NHWC, but C == 1, no reordering is needed */
    if  ((src_image_shape.features          == dst_image_shape.features) &&
           (src_image_shape.features        == 1)                        &&
           (src_image_shape.height          == dst_image_shape.height)   &&
           (src_image_shape.width           == dst_image_shape.width)    &&
           (((src_format.order == HAILO_FORMAT_ORDER_NHCW) && ((dst_format.order == HAILO_FORMAT_ORDER_NHWC) || (dst_format.order == HAILO_FORMAT_ORDER_FCR))) ||
           (((src_format.order == HAILO_FORMAT_ORDER_NHWC) || src_format.order == HAILO_FORMAT_ORDER_FCR) && (dst_format.order == HAILO_FORMAT_ORDER_NHCW)))) {
        return false;
    }


    /* If not all shapes and formats are the same - reordering is needed */
    if  (!((src_image_shape.features        == dst_image_shape.features) &&
           (src_image_shape.height          == dst_image_shape.height)   &&
           (src_image_shape.width           == dst_image_shape.width)    &&
           (src_format.order                == dst_format.order))) {
        return true;
    }

    /* Some orders has to be reordered, even if shapes and types are the same 
    Note: In order to add new order to the list - add test to test_transform with all shapes and types same 
    pre and post transform */
    switch (src_format.order) {
        // Orders that are supported both on host and hw sides, and where transformation is still needed when shapes are equals
        case HAILO_FORMAT_ORDER_F8CR:
            // In F8CR - if amount of features is less (or equal) than F8CR_MIN_FEATURES_FOR_TRANSFORMATION (8) - dont transform
            if (F8CR_MIN_FEATURES_FOR_TRANSFORMATION >= src_image_shape.features) {
                return false;
            } else {
                return true;
            }
        case HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS:
        case HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE:
            return true;
        default:
            return false;
    }
}

bool TransformContextUtils::should_pad_periph(const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format)
{
    // Check if hw frame size is aligned to 8 for periph transfer
    const auto shape_size = dst_image_shape.height * dst_image_shape.width * dst_image_shape.features *
        HailoRTCommon::get_data_bytes(dst_format.type);
    return (0 != (shape_size % HailoRTCommon::HW_DATA_ALIGNMENT));
}

Expected<bool> TransformContextUtils::is_transformation_required(const hailo_stream_direction_t stream_direction,
    const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &quant_infos)
{
    if (quant_infos.size() == 1) {
        CHECK_AS_EXPECTED(Quantization::is_qp_valid(quant_infos.at(0)), HAILO_INVALID_ARGUMENT,
            "quant_info is invalid as the model was compiled with multiple quant_infos. Please compile again or provide a vector of quant_infos.");
    }
    /* This function should be called after auto expend function */
    assert((HAILO_FORMAT_ORDER_AUTO != src_format.order) && (HAILO_FORMAT_ORDER_AUTO != dst_format.order));
    assert((HAILO_FORMAT_TYPE_AUTO != src_format.type) && (HAILO_FORMAT_TYPE_AUTO != dst_format.type));

    auto should_quantize_exp = should_quantize(stream_direction, src_format, dst_format);
    CHECK_EXPECTED(should_quantize_exp);

    return (*should_quantize_exp || should_transpose(src_format.flags, dst_format.flags) ||
        should_reorder(src_image_shape, src_format, dst_image_shape, dst_format) ||
        should_pad_periph(dst_image_shape, dst_format));
}

std::string TransformContextUtils::make_quantization_description(hailo_format_type_t src_type,
    hailo_format_type_t dst_type, const std::vector<hailo_quant_info_t> &quant_infos)
{
    std::stringstream quant_description;
    quant_description << "Quantization - src_type: " << HailoRTCommon::get_format_type_str(src_type) <<
        ", dst_type " << HailoRTCommon::get_format_type_str(dst_type);

    quant_description <<", limvals_min: " << quant_infos[0].limvals_min <<
        ", limvals_max: " << quant_infos[0].limvals_max;

    return quant_description.str();
}

std::string TransformContextUtils::make_reorder_description(hailo_format_order_t src_order, hailo_3d_image_shape_t src_shape,
    hailo_format_order_t dst_order, hailo_3d_image_shape_t dst_shape)
{
    std::stringstream reorder_description;
    reorder_description << "Reorder - src_order: " << HailoRTCommon::get_format_order_str(src_order) << ", src_shape: (" <<
        src_shape.height << ", " << src_shape.width << ", " << src_shape.features << ")" <<
        ", dst_order: " << HailoRTCommon::get_format_order_str(dst_order) << ", dst_shape: (" <<
        dst_shape.height << ", " << dst_shape.width << ", " << dst_shape.features << ")";

    return reorder_description.str();
}

std::string TransformContextUtils::make_pad_periph_description(hailo_3d_image_shape_t src_shape, hailo_3d_image_shape_t dst_shape)
{
    std::stringstream reorder_description;
    reorder_description << "Padding Periph shape - src_shape: (" << src_shape.height << ", " << src_shape.width << ", "
        << src_shape.features << "), dst_shape: (" << dst_shape.height << ", " << dst_shape.width << ", "
        << dst_shape.features << ")";

    return reorder_description.str();
}

std::string TransformContextUtils::make_transpose_description(hailo_3d_image_shape_t src_shape, hailo_3d_image_shape_t transposed_shape)
{
    std::stringstream transpose_description;
    transpose_description << "Transpose - src_shape: (" <<
        src_shape.height << ", " << src_shape.width << ", " << src_shape.features << ")" <<
        ", dst_shape: (" << transposed_shape.height << ", " << transposed_shape.width << ", " << transposed_shape.features << ")";

    return transpose_description.str();
}

template<typename T, typename Q>
void cast_elements_inplace(T *dst_ptr, uint32_t frame_size)
{
    static_assert(sizeof(T) >= sizeof(Q), "cast_elements_inplace() cannot cast to smaller size");
    for (int32_t i = (int32_t)frame_size - 1; i >= 0; i--) {
        dst_ptr[i] = (T)(*((Q*)dst_ptr + i));
    }
}

template<typename T, typename Q>
void cast_elements(const Q *src_ptr, T *dst_ptr, uint32_t frame_size)
{
    static_assert(sizeof(T) >= sizeof(Q), "cast_elements() cannot cast to smaller size");
    for (uint32_t i = 0; i < frame_size; i++) {
        dst_ptr[i] = (T)(*((Q*)src_ptr + i));
    }
}

/* Transpose funcs */
static hailo_3d_image_shape_t transposed_shape(const hailo_3d_image_shape_t &shape)
{
    hailo_3d_image_shape_t transposed_shape = shape;
    std::swap(transposed_shape.height, transposed_shape.width);
    return transposed_shape;
}

static hailo_status transform__transpose_NHWC(const void *src_ptr, const hailo_3d_image_shape_t &shape,
    size_t feature_bytes_size, void *dst_ptr)
{
    // Flatten the features, look at the data as HW matrix
    const size_t element_size = shape.features * feature_bytes_size;
    const uint8_t *src_matrix = reinterpret_cast<const uint8_t*>(src_ptr);
    uint8_t *dst_matrix = reinterpret_cast<uint8_t*>(dst_ptr);
    for (size_t r = 0; r < shape.height; r++) {
        for (size_t c = 0; c < shape.width; c++) {
            // dest[c][r] = src[r][c]
            size_t src_offset = element_size * ((r * shape.width) + c);
            const uint8_t *src_pos = src_matrix + src_offset;

            size_t dst_offset = element_size * ((c * shape.height) + r);
            uint8_t *dst_pos = dst_matrix + dst_offset;

            memcpy(dst_pos, src_pos, element_size);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status transform__transpose_buffer(const void *src_ptr, const hailo_3d_image_shape_t &shape,
    const hailo_format_t &format, void *dst_ptr)
{
    switch (format.order)
    {
    case HAILO_FORMAT_ORDER_NHWC:
    case HAILO_FORMAT_ORDER_RGB4:
    case HAILO_FORMAT_ORDER_NHW:
    case HAILO_FORMAT_ORDER_BAYER_RGB:
    case HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB:
    case HAILO_FORMAT_ORDER_FCR:
    case HAILO_FORMAT_ORDER_F8CR:
        return transform__transpose_NHWC(src_ptr, shape, HailoRTCommon::get_format_data_bytes(format), dst_ptr);
    default:
        LOGGER__ERROR("Transpose is not supported for order {}", static_cast<int>(format.order));
        return HAILO_INVALID_OPERATION;
    }
}

hailo_status transpose_buffer(const MemoryView src, const hailo_3d_image_shape_t &shape,
    const hailo_format_t &format, MemoryView dst)
{
    if ((src.size() != dst.size()) || (src.size() != HailoRTCommon::get_frame_size(shape, format))) {
        LOGGER__ERROR("transpose NHWC invalid buffers size");
        return HAILO_INVALID_ARGUMENT;
    }

    return transform__transpose_buffer(src.data(), shape, format, dst.data());
}


/* Re-Ordering funcs */
template<typename T>
void transform__h2d_NHWC_to_NHWC(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    size_t src_offset = 0;
    size_t dst_offset = 0;
    uint32_t src_row_size = src_image_shape->width * src_image_shape->features;
    uint32_t pad_size = (dst_image_shape->width - src_image_shape->width) * dst_image_shape->features;

    /* copy src to dst, and pad width to 8 elements */
    for (uint32_t r = 0; r < src_image_shape->height ; r++) {
        src_offset = r * src_image_shape->width * src_image_shape->features;
        dst_offset = r * dst_image_shape->width * dst_image_shape->features;
        memcpy(dst_ptr + dst_offset, src_ptr + src_offset, src_row_size * sizeof(T));
        memset(dst_ptr + dst_offset + src_row_size, 0, pad_size * sizeof(T));
    }
}

template<typename T>
void transform__d2h_NHWC_to_NHWC(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    size_t src_offset = 0;
    size_t dst_offset = 0;

    // copy and removed padded features
    for (uint32_t r = 0; r < dst_image_shape->height ; r++) {
        for (uint32_t c = 0; c < dst_image_shape->width ; c++) {
            src_offset = r * src_image_shape->width * src_image_shape->features + c * src_image_shape->features;
            dst_offset = r * dst_image_shape->width * dst_image_shape->features + c * dst_image_shape->features;
            memcpy(dst_ptr + dst_offset, src_ptr + src_offset, dst_image_shape->features * sizeof(T));
        }
    }
}

template<typename T>
void transform__h2d_NV12_to_NV12(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape, T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);
    uint32_t rows_count = src_image_shape->height * src_image_shape->features;
    ASSERT(0 == fmod(rows_count, 1.5));
    ASSERT(0 == (src_image_shape->width % 2));

    auto row_leftover = dst_image_shape->width - src_image_shape->width;

    size_t src_offset_y = 0;
    size_t src_offset_uv = ((static_cast<uint32_t>(rows_count / 1.5)) * src_image_shape->width);
    size_t dst_offset = 0;

    for(uint32_t h = 0; h < (static_cast<uint32_t>(rows_count / 1.5)); h += 2) {
        /* Copy 2 rows of Y for each row of U,V */
        // Copy Y
        for (auto i = 0; i < 2; i++) {
            memcpy(dst_ptr + dst_offset, src_ptr + src_offset_y, (src_image_shape->width * sizeof(T)));
            src_offset_y += (src_image_shape->width);
            dst_offset += (src_image_shape->width);
            memset((dst_ptr + dst_offset), 0, (row_leftover * sizeof(T)));
            dst_offset += row_leftover;
        }

        // Copy U, V
        memcpy(dst_ptr + dst_offset, (src_ptr + src_offset_uv), (src_image_shape->width * sizeof(T)));
        src_offset_uv += src_image_shape->width;
        dst_offset += src_image_shape->width;
        memset((dst_ptr + dst_offset), 0, (row_leftover * sizeof(T)));
        dst_offset += row_leftover;
    }
}

template <typename T>
void transform__h2d_I420_to_YYYYUV(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape, T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);
    uint32_t rows_count = src_image_shape->height * src_image_shape->features;
    ASSERT(0 == (rows_count % 3));
    ASSERT(0 == (src_image_shape->width % 2));
    ASSERT(dst_image_shape->width >= src_image_shape->width);

    auto padding_size_y = (dst_image_shape->width - src_image_shape->width);
    auto padding_size_uv = (dst_image_shape->width / 2) - (src_image_shape->width / 2);

    uint32_t y_plane_rows_count = static_cast<uint32_t>(rows_count / 1.5);

    size_t src_offset_y = 0;
    size_t src_offset_u = (y_plane_rows_count * src_image_shape->width);
    size_t src_offset_v = src_offset_u + (static_cast<uint32_t>((y_plane_rows_count / 2) * (src_image_shape->width / 2)));
    size_t dst_offset = 0;

    for(uint32_t h = 0; h < y_plane_rows_count; h += 2) {
        // Copy Y
        for (auto j = 0; j < 2; j++) {
            memcpy(dst_ptr + dst_offset, src_ptr + src_offset_y, (src_image_shape->width * sizeof(T)));
            src_offset_y += (src_image_shape->width);
            dst_offset += (src_image_shape->width);
            // add padding
            memset((dst_ptr + dst_offset), 0, (padding_size_y * sizeof(T)));
            dst_offset += padding_size_y;
        }

        // Copy U/2
        memcpy(dst_ptr + dst_offset, (src_ptr + src_offset_u), ((src_image_shape->width / 2) * sizeof(T)));
        src_offset_u += (src_image_shape->width / 2);
        dst_offset += (src_image_shape->width / 2);
        // Add padding
        memset((dst_ptr + dst_offset), 0, (padding_size_uv * sizeof(T)));
        dst_offset += padding_size_uv;

        // Copy V/2
        memcpy(dst_ptr + dst_offset, (src_ptr + src_offset_v), ((src_image_shape->width / 2) * sizeof(T)));
        src_offset_v += (src_image_shape->width / 2);
        dst_offset += (src_image_shape->width / 2);
        // Add padding
        memset((dst_ptr + dst_offset), 0, (padding_size_uv * sizeof(T)));
        dst_offset += padding_size_uv;
    }
}

template<typename T>
void transform__h2d_NHWC_to_NHCW(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    uint32_t src_row_size = src_image_shape->width * src_image_shape->features;
    uint32_t dst_row_size = dst_image_shape->width * dst_image_shape->features;

    size_t src_offset = 0;
    size_t dst_offset = 0;
    uint32_t pad_size = dst_image_shape->width - src_image_shape->width;

    /* transpose - switch width and channels */
    for (uint32_t r = 0; r < src_image_shape->height ; r++) {
        for (uint32_t f = 0; f < src_image_shape->features; f++) {
            for (uint32_t c = 0; c < src_image_shape->width; c++) {
                src_offset = r * src_row_size + c * src_image_shape->features + f;
                dst_offset = r * dst_row_size + f * dst_image_shape->width + c;
                dst_ptr[dst_offset] = src_ptr[src_offset];
            }
            /* pad width to 8 elemnts */
            if (pad_size != 0) {
                dst_offset = r * dst_row_size + f * dst_image_shape->width + src_image_shape->width;
                memset(dst_ptr + dst_offset, 0, pad_size * sizeof(T));
            }
        }
    }
}

template<typename T>
void transform__h2d_NHCW_to_NHCW(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    size_t src_frame_offset = 0;
    size_t dst_frame_offset = 0;
    uint32_t pad_size = dst_image_shape->width - src_image_shape->width;

    /* Copy data while considering padding */
    for (uint32_t r = 0; r < src_image_shape->height; r++) {
        for (uint32_t f = 0; f < src_image_shape->features; f++) {
            for (uint32_t c = 0; c < src_image_shape->width; c++) {
                src_frame_offset = r * src_image_shape->width * src_image_shape->features + f * src_image_shape->width + c;
                dst_frame_offset = r * dst_image_shape->width * dst_image_shape->features + f * dst_image_shape->width + c;
                dst_ptr[dst_frame_offset] = src_ptr[src_frame_offset];
            }
            /* pad width to the specified width */
            if (pad_size > 0) {
                dst_frame_offset = r * dst_image_shape->width * dst_image_shape->features + f * dst_image_shape->width + src_image_shape->width;
                memset(dst_ptr + dst_frame_offset, 0, pad_size * sizeof(T));
            }
        }
    }
}

template<typename T>
void transform__d2h_NHCW_to_NHWC(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    /* transpose - switch channels and width, ignore padded elements */
    const auto row_size_src = src_image_shape->width * src_image_shape->features;
    const auto row_size_dest = dst_image_shape->width * dst_image_shape->features;
    for (uint32_t r = 0; r < dst_image_shape->height ; r++) {
        const auto row_offset_src = r * row_size_src;
        const auto row_offset_dest = r * row_size_dest;
        for (uint32_t c = 0; c < dst_image_shape->width; c++) {
            const auto src_offset = row_offset_src + c;
            const auto dest_offset = row_offset_dest + c * dst_image_shape->features;
            for (uint32_t f = 0; f < dst_image_shape->features; f++) {
                dst_ptr[dest_offset + f] = src_ptr[src_offset + f * src_image_shape->width];
            }
        }
    }
}

template<typename T>
void transform__d2h_NHW_to_NHW(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape, T *dst_ptr,
    hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    for (uint32_t row = 0; row < dst_image_shape->height; row++) {
        const T *src = src_ptr + (row * src_image_shape->width);
        T* dst = dst_ptr + row * dst_image_shape->width;
        std::copy_n(src, dst_image_shape->width, dst);
    }
}

template<typename T>
void transform__h2d_NC_to_NC(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    /* copy src to dst, and pad channels to 8 elements */
    memcpy(dst_ptr, src_ptr, src_image_shape->features * sizeof(T));
    memset(dst_ptr + src_image_shape->features, 0, (dst_image_shape->features - src_image_shape->features) * sizeof(T));
}

template<typename T>
void transform__d2h_NC_to_NC(const T *src_ptr, T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    memcpy(dst_ptr, src_ptr, dst_image_shape->features * sizeof(T));
}

void transform__d2h_NMS(const uint8_t *src_ptr, uint8_t *dst_ptr, const hailo_nms_info_t &nms_info, std::vector<size_t> &chunk_offsets)
{
    /* Validate arguments */
    assert(NULL != src_ptr);
    assert(NULL != dst_ptr);

    uint32_t num_of_classes = nms_info.number_of_classes;
    uint32_t bbox_size = nms_info.bbox_size;

    size_t bbox_index = 0;
    size_t src_offset = 0;
    size_t dst_offset = 0;

    nms_bbox_counter_t class_bboxes_count = 0;

    // For each class, we need to merge bboxes from all nms chunks. Therefore we use chunk_offsets - for
    // each nms chunk we store its offset, any time we finish parsing some class bboxes, we update the
    // offset

    // First, init the chunk_offset vector
    assert(chunk_offsets.size() == nms_info.chunks_per_frame);
    size_t current_offset = 0;
    chunk_offsets[0] = current_offset;
    for (size_t chunk_index = 1; chunk_index < nms_info.chunks_per_frame; chunk_index++) {
        // Skip all classes. Can be optimized if we store the size of each chunk in the begining of the buffer
        for (size_t class_index = 0; class_index < num_of_classes; class_index++) {
            class_bboxes_count = *(reinterpret_cast<const nms_bbox_counter_t*>(src_ptr + current_offset));
            current_offset += sizeof(nms_bbox_counter_t) + (class_bboxes_count * bbox_size); 
        }
        chunk_offsets[chunk_index] = current_offset;
    }

    // Now, the merge itself
    for (size_t class_index = 0; class_index < num_of_classes; class_index++) {
        nms_bbox_counter_t *dst_bbox_counter = reinterpret_cast<nms_bbox_counter_t*>(dst_ptr + dst_offset);
        *dst_bbox_counter = 0;

        dst_offset += sizeof(nms_bbox_counter_t);

        for (size_t chunk_index = 0; chunk_index < nms_info.chunks_per_frame; chunk_index++) {
            // Add bbox from all chunks of current class
            src_offset = chunk_offsets[chunk_index];
            class_bboxes_count = *((nms_bbox_counter_t*)((uint8_t*)src_ptr + src_offset));
            assert(class_bboxes_count <= nms_info.max_bboxes_per_class);
            *dst_bbox_counter = static_cast<nms_bbox_counter_t>(*dst_bbox_counter + class_bboxes_count);

            src_offset += sizeof(nms_bbox_counter_t);

            for (bbox_index = 0; bbox_index < class_bboxes_count; bbox_index++) {
                net_flow::NmsPostProcessOp::transform__parse_and_copy_bbox((hailo_bbox_t *)(dst_ptr + dst_offset), (uint64_t*)(src_ptr + src_offset));
                src_offset += bbox_size;
                dst_offset += sizeof(hailo_bbox_t);
            }

            chunk_offsets[chunk_index] = src_offset;
        }
    }
}

template<typename T>
void transform__h2d_FCR(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    size_t src_offset = 0;
    size_t dst_offset = 0;
    uint32_t src_row_size = src_image_shape->width * src_image_shape->features;
    uint32_t dst_row_size = dst_image_shape->width * dst_image_shape->features;
    uint32_t pad_size = dst_image_shape->features - src_image_shape->features;

    for (uint32_t r = 0; r < src_image_shape->height ; r++) {
        for (uint32_t c = 0; c < src_image_shape->width; c++) {
            src_offset = r * src_row_size + c * src_image_shape->features;
            dst_offset = r * dst_row_size + c * dst_image_shape->features;

            memcpy(dst_ptr + dst_offset, src_ptr + src_offset, src_image_shape->features * sizeof(T));
            dst_offset += src_image_shape->features;
            memset(dst_ptr + dst_offset, 0, pad_size * sizeof(T));
        }
    }
}

template<typename T>
void transform__h2d_F8CR(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);
    ASSERT(0 == (dst_image_shape->features % HailoRTCommon::HW_DATA_ALIGNMENT));

    uint32_t src_row_size = src_image_shape->width * src_image_shape->features;
    uint32_t dst_row_size = dst_image_shape->width * dst_image_shape->features;
    uint32_t src_features = src_image_shape->features;
    size_t src_offset = 0;
    size_t dst_offset = 0;

    /* copy src data to dst, 8channels * width at a time, pad features to 8 elemnts */
    for (uint32_t r = 0; r < src_image_shape->height ; r++) {
        for (uint32_t c = 0; c < src_image_shape->width; c++) {
            for (uint32_t f = 0; f < src_image_shape->features; f+=8) {
                src_offset = r * src_row_size + c * src_image_shape->features + f;
                dst_offset = r * dst_row_size + c * HailoRTCommon::HW_DATA_ALIGNMENT + f * dst_image_shape->width;
                if (f + HailoRTCommon::HW_DATA_ALIGNMENT <= src_image_shape->features) {
                    /* take 8 full features for each column and write them */
                    memcpy(dst_ptr + dst_offset, src_ptr + src_offset, HailoRTCommon::HW_DATA_ALIGNMENT * sizeof(T));
                }
                else {
                    /* take the last 8 or less features, pad features to 8 and write */
                    auto last_features = (src_features % HailoRTCommon::HW_DATA_ALIGNMENT);
                    auto remainder = (HailoRTCommon::HW_DATA_ALIGNMENT - last_features);
                    memcpy(dst_ptr + dst_offset, src_ptr + src_offset, last_features * sizeof(T));
                    dst_offset += last_features;
                    memset(dst_ptr + dst_offset, 0, remainder * sizeof(T));
                }
            }
        }
    }
}

template<typename T>
void transform__d2h_F8CR(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    uint32_t src_row_size = src_image_shape->width * src_image_shape->features;
    uint32_t dst_row_size = dst_image_shape->width * dst_image_shape->features;
    uint32_t dst_features = dst_image_shape->features;
    uint32_t src_offset = 0;
    uint32_t dst_offset = 0;

    for (uint32_t r = 0; r < dst_image_shape->height ; r++) {
        for (uint32_t c = 0; c < dst_image_shape->width; c++) {
            for (uint32_t f = 0; f < dst_image_shape->features; f+=8) {
                src_offset = r * src_row_size + c * static_cast<uint32_t>(HailoRTCommon::HW_DATA_ALIGNMENT) +
                    f * src_image_shape->width;
                dst_offset = r * dst_row_size + c * dst_image_shape->features + f;
                if (f + HailoRTCommon::HW_DATA_ALIGNMENT <= dst_image_shape->features) {
                    /* copy the first dst_image_features (which are aligned to 8)! */
                    memcpy(dst_ptr + dst_offset, src_ptr + src_offset, HailoRTCommon::HW_DATA_ALIGNMENT * sizeof(T));
                    }
                else {
                    /* copy the last 8 or less features, remove pad */
                    memcpy(dst_ptr + dst_offset, src_ptr + src_offset, (dst_features % HailoRTCommon::HW_DATA_ALIGNMENT) * sizeof(T));
                }
            }
        }
    }
}

template<typename T>
void transform__d2h_BAYER_RGB(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    uint32_t src_offset = 0;
    uint32_t dst_offset = 0;

    for (uint32_t r = 0; r < dst_image_shape->height ; r++) {
        src_offset = r * src_image_shape->width;
        dst_offset = r * dst_image_shape->width;
        memcpy(dst_ptr + dst_offset, src_ptr + src_offset, dst_image_shape->width * sizeof(T));
    }
}

template<typename T>
hailo_status transform__h2d_NHWC_to_RGB888(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    size_t src_offset = 0;
    size_t dst_offset = 0;
    uint32_t pad_size = (dst_image_shape->width - src_image_shape->width) * dst_image_shape->features;

    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    CHECK(((RGB_FEATURES == src_image_shape->features) && ((RGB_FEATURES + 1) == dst_image_shape->features)),
        HAILO_INVALID_ARGUMENT,
        "User features must be {}, received {}. HW features must be {}, received {}",
        RGB_FEATURES, src_image_shape->features, RGB_FEATURES + 1, dst_image_shape->features);

    for (uint32_t r = 0; r < src_image_shape->height ; r++) {
        for (uint32_t c = 0; c < src_image_shape->width; c++) {
            src_offset = r * src_image_shape->width * src_image_shape->features + c * src_image_shape->features;
            dst_offset = r * dst_image_shape->width * dst_image_shape->features + c * dst_image_shape->features;

            /* Copy while flipping the data feature-wise */
            for (uint32_t f = 0; f < src_image_shape->features; f++) {
                dst_ptr[dst_offset + f] = src_ptr[src_offset + src_image_shape->features - f - 1];
            }
            /* add another zero byte */
            dst_ptr[dst_offset + RGB_FEATURES] = 0;
        }
        /* move dst_offset 4 features (RGB + 1 zero byte) and pad width if needed */
        memset(dst_ptr + dst_offset + RGB_FEATURES + 1, 0, pad_size * sizeof(T));
    }

    return HAILO_SUCCESS;
}

template<typename T>
hailo_status transform__h2d_NCHW_to_NHCW(
    const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);
    CHECK(src_image_shape->features == dst_image_shape->features, HAILO_INVALID_ARGUMENT,
          "NCHW_to_NHCW Transform features src/dst should be the same");
    CHECK(src_image_shape->height == dst_image_shape->height, HAILO_INVALID_ARGUMENT,
          "NCHW_to_NHCW Transform height src/dst should be the same");
    CHECK(src_image_shape->width <= dst_image_shape->width, HAILO_INVALID_ARGUMENT,
          "NCHW_to_NHCW Transform src width should be smaller/equal than dst width");
    CHECK(((dst_image_shape->width * sizeof(T)) % HailoRTCommon::HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT,
          "NCHW_to_NHCW Transform dst width must be aligned to {}", HailoRTCommon::HW_DATA_ALIGNMENT);

    size_t width_size = src_image_shape->width;
    size_t pad_size = (dst_image_shape->width - src_image_shape->width);
    for (uint32_t c = 0; c < src_image_shape->features; c++) {
        for (uint32_t r = 0; r < src_image_shape->height; r++) {
            // Copy width
            const T *src = src_ptr +
                src_image_shape->width * src_image_shape->height * c +
                src_image_shape->width * r;
            T *dst = dst_ptr +
                dst_image_shape->features * dst_image_shape->width * r +
                dst_image_shape->width * c;

            std::copy_n(src, width_size, dst);
            if (pad_size != 0) {
                std::fill_n(dst + width_size, pad_size, static_cast<T>(0));
            }
        }
    }

    return HAILO_SUCCESS;
}

template<typename T>
hailo_status transform__h2d_YUY2_to_YUY2(const T *src_ptr, T *dst_ptr, uint32_t shape_size)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);
    
    auto shape_size_in_bytes = shape_size * sizeof(T);

    CHECK((shape_size_in_bytes % HailoRTCommon::HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT,
          "YUY2_to_YUY2 Transform shape_size must be aligned to {}", HailoRTCommon::HW_DATA_ALIGNMENT);

    std::copy_n(src_ptr, shape_size, dst_ptr);

    return HAILO_SUCCESS;
}

template<typename T>
hailo_status transform__h2d_RGB4_to_NHWC(const T *src_ptr, const hailo_3d_image_shape_t &src_image_shape, T *dst_ptr,
    const hailo_3d_image_shape_t &dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    const auto row_size = src_image_shape.width * src_image_shape.features;
    const auto src_row_size = HailoRTCommon::align_to(row_size, static_cast<uint32_t>(RGB4_ALIGNMENT));
    const auto dst_row_size = dst_image_shape.width * dst_image_shape.features;

    const auto pad_size = (dst_image_shape.width - src_image_shape.width) * dst_image_shape.features;

    uint32_t src_offset = 0;
    uint32_t dst_offset = 0;

    for (uint32_t r = 0; r < dst_image_shape.height; r++) {
        src_offset = r * src_row_size;
        dst_offset = r * dst_row_size;
        memcpy(dst_ptr + dst_offset, src_ptr + src_offset, src_row_size * sizeof(T));
        if (pad_size != 0) {
            std::fill_n(dst_ptr + dst_offset + src_row_size, pad_size, static_cast<T>(0));
        }
    }

    return HAILO_SUCCESS;
}

template<typename T>
hailo_status transform__h2d_RGB4_to_NHCW(const T *src_ptr, const hailo_3d_image_shape_t &src_image_shape, T *dst_ptr,
    const hailo_3d_image_shape_t &dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    const auto row_size = src_image_shape.width * src_image_shape.features;
    const auto src_row_size = HailoRTCommon::align_to(row_size, static_cast<uint32_t>(RGB4_ALIGNMENT));
    const auto dst_row_size = dst_image_shape.width * dst_image_shape.features;

    const auto pad_size = dst_image_shape.width - src_image_shape.width;

    uint32_t src_offset = 0;
    uint32_t dst_offset = 0;

    for (uint32_t r = 0; r < src_image_shape.height ; r++) {
        /* transpose - switch width and channels */
        for (uint32_t f = 0; f < src_image_shape.features; f++) {
            for (uint32_t c = 0; c < src_image_shape.width; c++) {
                src_offset = r * src_row_size + c * src_image_shape.features + f;
                dst_offset = r * dst_row_size + f * dst_image_shape.width + c;
                dst_ptr[dst_offset] = src_ptr[src_offset];
            }
            /* pad feature to 8 elements */
            if (pad_size != 0) {
                dst_offset = r * dst_row_size + f * dst_image_shape.width + src_image_shape.width;
                std::fill_n(dst_ptr + dst_offset, pad_size, static_cast<T>(0));
            }
        }
    }

    return HAILO_SUCCESS;
}

hailo_status InputTransformContext::quantize_stream(const void *src_ptr, void *quant_buffer)
{
    auto shape_size = HailoRTCommon::get_shape_size(m_src_image_shape);

    switch (m_src_format.type) {
        case HAILO_FORMAT_TYPE_UINT8:
            if (HAILO_FORMAT_TYPE_UINT16 == m_dst_format.type) {
                cast_elements<uint16_t, uint8_t>(static_cast<const uint8_t*>(src_ptr), static_cast<uint16_t*>(quant_buffer), shape_size);
            }
            else {
                return HAILO_INVALID_OPERATION;
            }
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            if (HAILO_FORMAT_TYPE_UINT8 == m_dst_format.type) {
                Quantization::quantize_input_buffer<float32_t, uint8_t>((float32_t*)src_ptr, (uint8_t*)quant_buffer, shape_size, m_dst_quant_infos[0]);
            }
            else if (HAILO_FORMAT_TYPE_UINT16 == m_dst_format.type) {
                Quantization::quantize_input_buffer<float32_t, uint16_t>((float32_t*)src_ptr, (uint16_t*)quant_buffer, shape_size, m_dst_quant_infos[0]);
            }
            else {
                return HAILO_INVALID_OPERATION;
            }
            break;
        default:
            LOGGER__ERROR("Invalid src-buffer's type format");
            return HAILO_INVALID_ARGUMENT;
    }
    return HAILO_SUCCESS;
}

hailo_status FrameOutputTransformContext::quantize_stream(const void *dst_ptr)
{
    auto shape_size = HailoRTCommon::get_shape_size(m_dst_image_shape);

    switch (m_dst_format.type) {
        case HAILO_FORMAT_TYPE_UINT16:
            if (HAILO_FORMAT_TYPE_UINT8 == m_src_format.type) {
                cast_elements_inplace<uint16_t, uint8_t>((uint16_t*)dst_ptr, shape_size);
            } else {
                return HAILO_INVALID_OPERATION;
            }
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            if (HAILO_FORMAT_ORDER_NHW != m_dst_format.order) {
                if (HAILO_FORMAT_TYPE_UINT8 == m_src_format.type) {
                    if (m_are_all_qps_the_same) {
                        Quantization::dequantize_output_buffer_in_place<float32_t, uint8_t>((float32_t*)dst_ptr, shape_size, m_dst_quant_infos[0]);
                    } else {
                        dequantize_output_by_feature<float32_t, uint8_t>((float32_t*)dst_ptr, shape_size, m_quant_info_per_feature, m_quant_infos_rep_count);
                    }
                }
                else if (HAILO_FORMAT_TYPE_UINT16 == m_src_format.type) {
                    if (m_are_all_qps_the_same) {
                        Quantization::dequantize_output_buffer_in_place<float32_t, uint16_t>((float32_t*)dst_ptr, shape_size, m_dst_quant_infos[0]);
                    } else {
                        dequantize_output_by_feature<float32_t, uint16_t>((float32_t*)dst_ptr, shape_size, m_quant_info_per_feature, m_quant_infos_rep_count);
                    }
                }
                else {
                    return HAILO_INVALID_OPERATION;
                }
            } else {
                if (HAILO_FORMAT_TYPE_UINT8 == m_src_format.type) {
                    cast_elements_inplace<float32_t, uint8_t>((float32_t*)dst_ptr, shape_size);
                }
                else if (HAILO_FORMAT_TYPE_UINT16 == m_src_format.type) {
                    cast_elements_inplace<float32_t, uint16_t>((float32_t*)dst_ptr, shape_size);
                }
                else {
                    return HAILO_INVALID_OPERATION;
                }
            }
            break;
        default:
            LOGGER__ERROR("Invalid dst-buffer's type format");
            return HAILO_INVALID_ARGUMENT;
    }

    return HAILO_SUCCESS;
}

hailo_status reorder_input_stream(const void *src_ptr, hailo_3d_image_shape_t src_image_shape, hailo_format_t src_format, 
    void *dst_ptr, hailo_3d_image_shape_t dst_image_shape, hailo_format_t dst_format)
{
    if ((HAILO_FORMAT_ORDER_NHWC == src_format.order) &&
        (HAILO_FORMAT_ORDER_NHCW == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_NHWC_to_NHCW<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_NHWC_to_NHCW<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if ((HAILO_FORMAT_ORDER_NHCW == src_format.order) &&
        (HAILO_FORMAT_ORDER_NHCW == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_NHCW_to_NHCW<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_NHCW_to_NHCW<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if ((HAILO_FORMAT_ORDER_NHWC == src_format.order) &&
        (HAILO_FORMAT_ORDER_NHWC == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_NHWC_to_NHWC<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_NHWC_to_NHWC<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if ((HAILO_FORMAT_ORDER_NC == src_format.order) &&
        (HAILO_FORMAT_ORDER_NC == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_NC_to_NC<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_NC_to_NC<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if (((HAILO_FORMAT_ORDER_FCR == src_format.order) || (HAILO_FORMAT_ORDER_NHWC == src_format.order)) &&
        (HAILO_FORMAT_ORDER_FCR == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_FCR<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_FCR<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if (((HAILO_FORMAT_ORDER_F8CR == src_format.order) || (HAILO_FORMAT_ORDER_NHWC == src_format.order)) &&
        (HAILO_FORMAT_ORDER_F8CR == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_F8CR<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_F8CR<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if ((HAILO_FORMAT_ORDER_BAYER_RGB == src_format.order) &&
        (HAILO_FORMAT_ORDER_BAYER_RGB == dst_format.order)) {
        assert(1 == src_image_shape.features);
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_NHWC_to_NHWC<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_NHWC_to_NHWC<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if ((HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB == src_format.order) &&
        (HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB == dst_format.order)) {
        assert(1 == src_image_shape.features);
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_NHWC_to_NHWC<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_NHWC_to_NHWC<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if ((HAILO_FORMAT_ORDER_NHWC == src_format.order) &&
        (HAILO_FORMAT_ORDER_RGB888 == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                return transform__h2d_NHWC_to_RGB888<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                return transform__h2d_NHWC_to_RGB888<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
    }

    if ((HAILO_FORMAT_ORDER_NCHW == src_format.order) &&
        (HAILO_FORMAT_ORDER_NHCW == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                return transform__h2d_NCHW_to_NHCW<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                return transform__h2d_NCHW_to_NHCW<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
    }

    if ((HAILO_FORMAT_ORDER_YUY2 == src_format.order) &&
        (HAILO_FORMAT_ORDER_YUY2 == dst_format.order)) {
        auto shape_size = HailoRTCommon::get_shape_size(src_image_shape);
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                return transform__h2d_YUY2_to_YUY2<uint8_t>((uint8_t*)src_ptr, (uint8_t*)dst_ptr, shape_size);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                return transform__h2d_YUY2_to_YUY2<uint16_t>((uint16_t*)src_ptr, (uint16_t*)dst_ptr, shape_size);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
    }

    if (((HAILO_FORMAT_ORDER_NV12 == src_format.order) &&
               (HAILO_FORMAT_ORDER_HAILO_YYUV) == dst_format.order) ||
               ((HAILO_FORMAT_ORDER_NV21 == src_format.order) &&
               (HAILO_FORMAT_ORDER_HAILO_YYVU) == dst_format.order)) {
        switch (src_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_NV12_to_NV12<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_NV12_to_NV12<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format {}", static_cast<int>(src_format.type));
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if (((HAILO_FORMAT_ORDER_I420 == src_format.order) &&
               (HAILO_FORMAT_ORDER_HAILO_YYYYUV) == dst_format.order)) {
        switch (src_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_I420_to_YYYYUV<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_I420_to_YYYYUV<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format {}", static_cast<int>(src_format.type));
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if ((HAILO_FORMAT_ORDER_RGB4 == src_format.order) &&
        (HAILO_FORMAT_ORDER_NHWC == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_RGB4_to_NHWC<uint8_t>((uint8_t*)src_ptr, src_image_shape, (uint8_t*)dst_ptr, dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_RGB4_to_NHWC<uint16_t>((uint16_t*)src_ptr, src_image_shape, (uint16_t*)dst_ptr, dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    if ((HAILO_FORMAT_ORDER_RGB4 == src_format.order) &&
        (HAILO_FORMAT_ORDER_NHCW == dst_format.order)) {
        switch (dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__h2d_RGB4_to_NHCW<uint8_t>((uint8_t*)src_ptr, src_image_shape, (uint8_t*)dst_ptr, dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__h2d_RGB4_to_NHCW<uint16_t>((uint16_t*)src_ptr, src_image_shape, (uint16_t*)dst_ptr, dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
        return HAILO_SUCCESS;
    }

    LOGGER__ERROR("Unsupported input stream transformation from hailo_format_order_t "
        "{} to hailo_format_order_t {}", HailoRTCommon::get_format_order_str(src_format.order),
        HailoRTCommon::get_format_order_str(dst_format.order));
    return HAILO_INVALID_OPERATION;
}

hailo_status reorder_output_stream(const void *src_ptr, hailo_3d_image_shape_t src_image_shape, hailo_format_t src_format, 
    void *dst_ptr, hailo_3d_image_shape_t dst_image_shape, hailo_format_t dst_format)
{
    if ((HAILO_FORMAT_ORDER_NHCW == src_format.order) &&
        (HAILO_FORMAT_ORDER_NHWC == dst_format.order)) {
        switch (src_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                transform__d2h_NHCW_to_NHWC<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                break;
            case HAILO_FORMAT_TYPE_UINT16:
                transform__d2h_NHCW_to_NHWC<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                break;
            default:
                LOGGER__ERROR("Invalid src-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
    }
    else if ((HAILO_FORMAT_ORDER_NC == src_format.order) &&
            (HAILO_FORMAT_ORDER_NC == dst_format.order)) {
            switch (src_format.type) {
                case HAILO_FORMAT_TYPE_UINT8:
                    transform__d2h_NC_to_NC<uint8_t>((uint8_t*)src_ptr, (uint8_t*)dst_ptr, &dst_image_shape);
                    break;
                case HAILO_FORMAT_TYPE_UINT16:
                    transform__d2h_NC_to_NC<uint16_t>((uint16_t*)src_ptr, (uint16_t*)dst_ptr, &dst_image_shape);
                    break;
                default:
                    LOGGER__ERROR("Invalid src-buffer's type format");
                    return HAILO_INVALID_ARGUMENT;
            }
    }
    else if ((HAILO_FORMAT_ORDER_NHW == src_format.order) &&
            (HAILO_FORMAT_ORDER_NHW == dst_format.order)) {
            switch (src_format.type) {
                case HAILO_FORMAT_TYPE_UINT8:
                    transform__d2h_NHW_to_NHW<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                    break;
                case HAILO_FORMAT_TYPE_UINT16:
                    transform__d2h_NHW_to_NHW<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                    break;
                default:
                    LOGGER__ERROR("Invalid src-buffer's type format");
                    return HAILO_INVALID_ARGUMENT;
            }
    }
    else if ((HAILO_FORMAT_ORDER_FCR == src_format.order) &&
        ((HAILO_FORMAT_ORDER_FCR == dst_format.order) || (HAILO_FORMAT_ORDER_NHWC == dst_format.order))) {
            switch (src_format.type) {
                case HAILO_FORMAT_TYPE_UINT8:
                    transform__d2h_NHWC_to_NHWC<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                    break;
                case HAILO_FORMAT_TYPE_UINT16:
                    transform__d2h_NHWC_to_NHWC<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                    break;
                default:
                    LOGGER__ERROR("Invalid src-buffer's type format");
                    return HAILO_INVALID_ARGUMENT;
            }
    }
    else if ((HAILO_FORMAT_ORDER_F8CR == src_format.order) &&
        ((HAILO_FORMAT_ORDER_F8CR == dst_format.order) || (HAILO_FORMAT_ORDER_NHWC == dst_format.order))) {
            switch (src_format.type) {
                case HAILO_FORMAT_TYPE_UINT8:
                    transform__d2h_F8CR<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                    break;
                case HAILO_FORMAT_TYPE_UINT16:
                    transform__d2h_F8CR<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                    break;
                default:
                    LOGGER__ERROR("Invalid src-buffer's type format");
                    return HAILO_INVALID_ARGUMENT;
            }
    }
    else if ((HAILO_FORMAT_ORDER_BAYER_RGB == src_format.order) &&
        (HAILO_FORMAT_ORDER_BAYER_RGB == dst_format.order)) {
            assert((1 == src_image_shape.features) && (1 == dst_image_shape.features));
            switch (src_format.type) {
                case HAILO_FORMAT_TYPE_UINT8:
                    transform__d2h_BAYER_RGB<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                    break;
                case HAILO_FORMAT_TYPE_UINT16:
                    transform__d2h_BAYER_RGB<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                    break;
                default:
                    LOGGER__ERROR("Invalid src-buffer's type format");
                    return HAILO_INVALID_ARGUMENT;
            }
    } else if ((HAILO_FORMAT_ORDER_NHCW == src_format.order) &&
               (HAILO_FORMAT_ORDER_NCHW) == dst_format.order) {
            switch (src_format.type) {
                case HAILO_FORMAT_TYPE_UINT8:
                    TransformContextUtils::transform__d2h_NHCW_to_NCHW<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                    break;
                case HAILO_FORMAT_TYPE_UINT16:
                    TransformContextUtils::transform__d2h_NHCW_to_NCHW<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                    break;
                default:
                    LOGGER__ERROR("Invalid src-buffer's type format");
                    return HAILO_INVALID_ARGUMENT;
            }
    } else if ((HAILO_FORMAT_ORDER_NHW == src_format.order) &&
               (HAILO_FORMAT_ORDER_NCHW) == dst_format.order) {

            CHECK((src_image_shape.features == 1) && (dst_image_shape.features == 1), HAILO_INVALID_ARGUMENT,
                "Invalid number of features. Expected 1, received hw: {}, user: {}",
                    src_image_shape.features, dst_image_shape.features);
            switch (src_format.type) {
                // We call for transform__d2h_NHW_to_NHW function since NCHW is the same as NHW when the the image's features = 1.
                case HAILO_FORMAT_TYPE_UINT8:
                    transform__d2h_NHW_to_NHW<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                    break;
                case HAILO_FORMAT_TYPE_UINT16:
                    transform__d2h_NHW_to_NHW<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                    break;
                default:
                    LOGGER__ERROR("Invalid src-buffer's type format");
                    return HAILO_INVALID_ARGUMENT;
            }
    } else if ((HAILO_FORMAT_ORDER_NHWC == src_format.order) &&
               (HAILO_FORMAT_ORDER_NHWC) == dst_format.order) {
            switch (src_format.type) {
                case HAILO_FORMAT_TYPE_UINT8:
                    transform__d2h_NHWC_to_NHWC<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                    break;
                case HAILO_FORMAT_TYPE_UINT16:
                    transform__d2h_NHWC_to_NHWC<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
                    break;
                default:
                    LOGGER__ERROR("Invalid src-buffer's type format {}", static_cast<int>(src_format.type));
                    return HAILO_INVALID_ARGUMENT;
            }
    } else {
        LOGGER__ERROR("Unsupported output stream transformation from hailo_format_order_t "
            "{} to hailo_format_order_t {}", HailoRTCommon::get_format_order_str(src_format.order),
                HailoRTCommon::get_format_order_str(dst_format.order));
        return HAILO_INVALID_OPERATION;
    }

    return HAILO_SUCCESS;
}

/* Public funcs */
hailo_status InputTransformContext::transform_inner(const void *src_ptr, void *quant_buffer, void *dst_ptr, 
    MemoryView transpose_buffer)
{
    void *orig_dst_ptr = nullptr;
    hailo_3d_image_shape_t transposed_image_shape = m_src_image_shape;
    hailo_format_t quantized_src_format = m_src_format;

    if (!(m_should_quantize || m_should_transpose || m_should_reorder || m_should_pad_periph)) {
        /* If transform was created without any actual use - just copy src_ptr to dst_ptr */
        LOGGER__WARN("Transformer was created, but not needed and can be removed. copies src buffer to dst buffer");
        auto frame_size = HailoRTCommon::get_frame_size(m_dst_image_shape, m_dst_format);
        memcpy(dst_ptr, src_ptr, frame_size);
        return HAILO_SUCCESS;
    }

    if (m_should_quantize) {
        /* If final step - output of this quant func is the dst_ptr */
        orig_dst_ptr = (m_should_transpose || m_should_reorder) ? quant_buffer : dst_ptr;
        auto status = quantize_stream(src_ptr, orig_dst_ptr);
        CHECK_SUCCESS(status);
        src_ptr = orig_dst_ptr;
        quantized_src_format.type = m_dst_format.type;
    }

    if (!(m_should_transpose || m_should_reorder)) {
        /* If quantize is the only step - need to copy src buffer to dst buffer */
        auto frame_size = HailoRTCommon::get_frame_size(m_dst_image_shape, m_dst_format);
        memcpy(dst_ptr, src_ptr, frame_size);
        return HAILO_SUCCESS;
    }

    if (m_should_transpose) {
        if (transpose_buffer.empty()) {
            LOGGER__ERROR("Transpose buffer not given");
            return HAILO_INVALID_ARGUMENT;
        }

        if (transpose_buffer.size() != HailoRTCommon::get_frame_size(m_src_image_shape, quantized_src_format)) {
            LOGGER__ERROR("Transpose buffer size mismatch (expected {}, actual {})",
                HailoRTCommon::get_frame_size(m_src_image_shape, quantized_src_format), transpose_buffer.size());
            return HAILO_INVALID_ARGUMENT;
        }

        /* If final step - output of this quant func is the dst_ptr */
        orig_dst_ptr = (m_should_reorder) ? transpose_buffer.data() : dst_ptr;
        auto status = transform__transpose_buffer(src_ptr, m_src_image_shape, quantized_src_format, orig_dst_ptr);
        CHECK_SUCCESS(status);

        src_ptr = transpose_buffer.data();
        transposed_image_shape = transposed_shape(m_src_image_shape);
    }

    if (m_should_reorder){
        auto status = reorder_input_stream(src_ptr, transposed_image_shape, quantized_src_format, dst_ptr, 
            m_dst_image_shape, m_dst_format);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status FrameOutputTransformContext::transform_inner(const void *src_ptr, void *dst_ptr, MemoryView transpose_buffer)
{
    hailo_format_t transposed_format = m_dst_format;
    hailo_3d_image_shape_t transposed_image_shape = m_dst_image_shape;
    transposed_format.type = m_src_format.type;

    void *orig_dst_ptr = nullptr;
    void *orig_src_ptr = nullptr;

    if (!(m_should_quantize || m_should_transpose || m_should_reorder || m_should_pad_periph)) {
        /* If transform context was created without any actual use - just copy src_ptr to dst_ptr */
        LOGGER__WARN("Transform context was created, but not needed and can be removed. copies src buffer to dst buffer");
        auto frame_size = HailoRTCommon::get_frame_size(m_dst_image_shape, m_dst_format);
        memcpy(dst_ptr, src_ptr, frame_size);
        return HAILO_SUCCESS;
    }

    if (m_should_reorder) {
        if (m_should_transpose) {
            /* If user needs to reorder and transform - the output of the reorder is the transform buffer*/
            if (transpose_buffer.empty()) {
                LOGGER__ERROR("Transpose buffer not given");
                return HAILO_INVALID_ARGUMENT;
            }

            if (transpose_buffer.size() != HailoRTCommon::get_frame_size(m_dst_image_shape, transposed_format)) {
                LOGGER__ERROR("Transpose buffer size mismatch (expected {}, actual {})",
                    HailoRTCommon::get_frame_size(m_dst_image_shape, transposed_format), transpose_buffer.size());
                return HAILO_INVALID_ARGUMENT;
            }

            // Prepare transpose - the order transformation will be applied to the transpose buffer, later we will transpose
            // from dst_ptr (transpose_buffer) to orig_dst_ptr (user buffer)
            orig_dst_ptr = transpose_buffer.data();
            transposed_image_shape = transposed_shape(m_dst_image_shape);
        } else {
            orig_dst_ptr = dst_ptr;
        }
        auto status = reorder_output_stream(src_ptr, m_src_image_shape, m_src_format, orig_dst_ptr, transposed_image_shape, 
            m_dst_format);
        CHECK_SUCCESS(status);
    }

    if (m_should_transpose) {
        orig_src_ptr = (m_should_reorder) ? orig_dst_ptr : const_cast<void *>(src_ptr);
        auto status = transform__transpose_buffer(orig_src_ptr, transposed_image_shape, transposed_format, dst_ptr);
        CHECK_SUCCESS(status);

        transposed_image_shape = transposed_shape(transposed_image_shape);
    }
    
    if (!(m_should_transpose || m_should_reorder)) {
        /* If quantize is the only step - need to copy src buffer to dst buffer */
        auto frame_size = HailoRTCommon::get_frame_size(m_src_image_shape, m_src_format);
        memcpy(dst_ptr, src_ptr, frame_size);
    }

    if (m_should_quantize) {
        auto status = quantize_stream(dst_ptr);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}


hailo_status transform_demux_raw_frame(const void *src, uint32_t offset,
    hailo_mux_info_t *mux_info, uint32_t mux_row_count)
{
    // This is a recursive function with a maximum depth of HailoRTCommon::MUX_INFO_COUNT.
    hailo_status status = HAILO_UNINITIALIZED;
    struct hailo_mux_info_t *predecessor = NULL;
    uint32_t row_size = 0;

    CHECK_ARG_NOT_NULL(src);

    for (uint32_t i = 0; i < mux_row_count; i++) {
        for (uint32_t j = 0; j < mux_info->successors_count; j++) {
            predecessor = mux_info->successors[j];
            row_size = predecessor->row_size;

            if ((predecessor->info.is_mux) && (i < predecessor->rows_gcd)) {
                status = transform_demux_raw_frame(src, offset, predecessor, predecessor->info.hw_shape.height / mux_info->rows_gcd);
                CHECK_SUCCESS(status);                
            }

            if (!(predecessor->info.is_mux)) {
                if (predecessor->row_counter < predecessor->info.shape.height) {
                    memcpy((uint8_t*)predecessor->buffer + predecessor->current_offset, (uint8_t*)src + offset, row_size);
                    predecessor->current_offset += row_size;
                }
                
                predecessor->row_counter++;
                if (predecessor->row_counter == (predecessor->info.hw_shape.height + 1)) {
                    predecessor->row_counter = 0;
                }
            }
            
            offset += row_size;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status validate_input_transform_params(hailo_3d_image_shape_t src_image_shape, hailo_format_t src_format,
    hailo_format_t dst_format)
{
    /* Check device type */
    if (!((HAILO_FORMAT_TYPE_UINT16 == dst_format.type) || (HAILO_FORMAT_TYPE_UINT8 == dst_format.type))) {
        LOGGER__ERROR("Unsupported device-side format_type {}", HailoRTCommon::get_format_type_str(dst_format.type));
        return HAILO_INVALID_ARGUMENT;
    }

    /* Check reorder flags - where no reorder is needed */
    if ((HAILO_FORMAT_ORDER_BAYER_RGB == src_format.order) &&
        (HAILO_FORMAT_ORDER_BAYER_RGB == dst_format.order)) {
        if (src_image_shape.features != 1) {
            LOGGER__ERROR("Invalid Bayer user features. Expected 1, received {}", src_image_shape.features);
            return HAILO_INVALID_ARGUMENT;
        }
    } else if ((HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB == src_format.order) &&
        (HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB == dst_format.order)) {
        if (src_image_shape.features != 1) {
            LOGGER__ERROR("Invalid Bayer user features. Expected 1, received {}", src_image_shape.features);
            return HAILO_INVALID_ARGUMENT;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status validate_output_transform_params(hailo_3d_image_shape_t src_image_shape, hailo_format_t src_format,
    hailo_3d_image_shape_t dst_image_shape, hailo_format_t dst_format)
{
    /* Check device type */
    if (!((HAILO_FORMAT_TYPE_UINT16 == src_format.type) || (HAILO_FORMAT_TYPE_UINT8 == src_format.type))) {
        LOGGER__ERROR("Unsupported device-side format_type {}", HailoRTCommon::get_format_type_str(src_format.type));
        return HAILO_INVALID_ARGUMENT;
    }

    /* Check reorder flags - where no reorder is needed */
    if ((HAILO_FORMAT_ORDER_BAYER_RGB == src_format.order) &&
        (HAILO_FORMAT_ORDER_BAYER_RGB == dst_format.order)) {
        if ((src_image_shape.features != 1) || (dst_image_shape.features != 1)) {
            LOGGER__ERROR("Invalid Bayer user or hw features. Expected 1, received user: {}, hw: {}",
                src_image_shape.features, dst_image_shape.features);
            return HAILO_INVALID_ARGUMENT;
        }
    }

    return HAILO_SUCCESS;
}

Expected<bool> InputTransformContext::is_transformation_required(
    const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format, 
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, 
    const std::vector<hailo_quant_info_t> &quant_infos)
{
    auto host_format = HailoRTDefaults::expand_auto_format(src_format, dst_format);
    auto val = TransformContextUtils::is_transformation_required(HAILO_H2D_STREAM, src_image_shape, host_format,
        dst_image_shape, dst_format, quant_infos);
    return val;
}

bool InputTransformContext::is_transformation_required(
    const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format, 
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, 
    const hailo_quant_info_t &quant_info)
{
    LOGGER__WARNING("Using a deprecated function. Use is_transformation_required that recieves a vector of hailo_quant_info_t instead");
    std::vector<hailo_quant_info_t> quant_infos = { quant_info };
    auto expected_is_transformation_required = is_transformation_required(src_image_shape, src_format, dst_image_shape, dst_format, quant_infos);
    if (!expected_is_transformation_required) {
        return true;
    }
    return expected_is_transformation_required.release();
}

std::string InputTransformContext::description() const
{
    std::stringstream transform_description;
    bool first = true;

    if (m_should_quantize) {
        if (!first) {
            transform_description << " | ";
        } else {
            first = false;
        }
        transform_description << TransformContextUtils::make_quantization_description(m_src_format.type, m_dst_format.type, m_dst_quant_infos);
    }

    if (m_should_transpose) {
        if (!first) {
            transform_description << " | ";
        } else {
            first = false;
        }
        transform_description << TransformContextUtils::make_transpose_description(m_src_image_shape, transposed_shape(m_src_image_shape));
    }

    if (m_should_reorder) {
        if (!first) {
            transform_description << " | ";
        } else {
            first = false;
        }
        transform_description << TransformContextUtils::make_reorder_description(m_src_format.order, m_src_image_shape, m_dst_format.order, m_dst_image_shape);
    }

    if (m_should_pad_periph) {
        if (!first) {
            transform_description << " | ";
        } else {
            first = false;
        }
        transform_description << TransformContextUtils::make_pad_periph_description(m_src_image_shape, m_dst_image_shape);
    }

    return transform_description.str();
}

Expected<std::unique_ptr<InputTransformContext>> InputTransformContext::create(const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
    const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos)
{
    auto status = validate_input_transform_params(src_image_shape, src_format, dst_format);
    CHECK_SUCCESS_AS_EXPECTED(status);

    const auto internal_src_format = HailoRTDefaults::expand_auto_format(src_format, dst_format);

    const auto src_frame_size = HailoRTCommon::get_frame_size(src_image_shape, internal_src_format);
    const auto dst_frame_size = HailoRTCommon::get_periph_frame_size(dst_image_shape, dst_format);

    Buffer quant_buffer;
    auto should_quantize = TransformContextUtils::should_quantize(HAILO_H2D_STREAM, internal_src_format, dst_format);
    CHECK_EXPECTED(should_quantize);
    if (should_quantize.value()) {
        auto expected_quant_buffer = Buffer::create(src_frame_size, 0);
        CHECK_EXPECTED(expected_quant_buffer);
        quant_buffer = expected_quant_buffer.release();
    }

    Buffer transpose_buffer;
    bool should_transpose = TransformContextUtils::should_transpose(internal_src_format.flags, dst_format.flags);
    if (should_transpose) {
        auto expected_transpose_buffer = Buffer::create(get_transpose_buffer_size(src_image_shape,
            dst_format.type));
        CHECK_EXPECTED(expected_transpose_buffer);
        transpose_buffer = expected_transpose_buffer.release();
    }

    auto should_reorder = TransformContextUtils::should_reorder(src_image_shape, internal_src_format, dst_image_shape, dst_format);
    auto should_pad_periph = TransformContextUtils::should_pad_periph(dst_image_shape, dst_format);

    std::unique_ptr<InputTransformContext> transform_context(new (std::nothrow) InputTransformContext(src_frame_size, src_image_shape,
        internal_src_format, dst_frame_size, dst_image_shape, dst_format, dst_quant_infos, std::move(quant_buffer),
        std::move(transpose_buffer), *should_quantize, should_transpose, should_reorder, should_pad_periph));
    CHECK_AS_EXPECTED(nullptr != transform_context, HAILO_OUT_OF_HOST_MEMORY);

    return transform_context;
}

Expected<std::unique_ptr<InputTransformContext>> InputTransformContext::create(const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
    const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info)
{
    CHECK_AS_EXPECTED(Quantization::is_qp_valid(dst_quant_info), HAILO_INVALID_ARGUMENT,
        "quant_info is invalid as the model was compiled with multiple quant_infos. Please compile again or provide a list of quant_infos.");

    std::vector<hailo_quant_info_t> dst_quant_infos = { dst_quant_info };
    return create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_infos);
}

Expected<std::unique_ptr<InputTransformContext>> InputTransformContext::create(const hailo_stream_info_t &stream_info,
    const hailo_transform_params_t &transform_params)
{
    return create(stream_info.shape, transform_params.user_buffer_format, stream_info.hw_shape, stream_info.format,
        std::vector<hailo_quant_info_t>{stream_info.quant_info});
}

Expected<std::unique_ptr<InputTransformContext>> InputTransformContext::create(const hailo_stream_info_t &stream_info, bool quantized,
    hailo_format_type_t format_type)
{
    return create(stream_info, HailoRTDefaults::get_transform_params(quantized, format_type));
}

Expected<std::unique_ptr<InputTransformContext>> InputTransformContext::create(InputStream &input_stream,
    const hailo_transform_params_t &transform_params)
{
    auto stream_info = input_stream.get_info();
    auto src_quant_infos = input_stream.get_quant_infos();

    return create(stream_info.shape, transform_params.user_buffer_format, stream_info.hw_shape,
        stream_info.format, src_quant_infos);
}

InputTransformContext::InputTransformContext(size_t src_frame_size, const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, size_t dst_frame_size, const hailo_3d_image_shape_t &dst_image_shape,
    const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, Buffer &&quant_buffer,
    Buffer &&transpose_buffer,const bool should_quantize, const bool should_transpose, const bool should_reorder,
    const bool should_pad_periph) :
        m_src_frame_size(src_frame_size),
        m_src_image_shape(src_image_shape),
        m_src_format(src_format),
        m_dst_frame_size(dst_frame_size),
        m_dst_image_shape(dst_image_shape),
        m_dst_format(dst_format),
        m_dst_quant_infos(dst_quant_infos),
        m_should_quantize(should_quantize),
        m_should_transpose(should_transpose),
        m_should_reorder(should_reorder),
        m_should_pad_periph(should_pad_periph),
        m_quant_buffer(std::move(quant_buffer)),
        m_transpose_buffer(std::move(transpose_buffer))
{}

hailo_status InputTransformContext::transform(const MemoryView src, MemoryView dst)
{
    /* Check sizes */
    CHECK(src.size() == m_src_frame_size, HAILO_INVALID_ARGUMENT,
        "src size must be {}. passed size - {}", m_src_frame_size, src.size());
    CHECK(dst.size() == m_dst_frame_size, HAILO_INVALID_ARGUMENT,
        "dst_size must be {}. passed size - {}", m_dst_frame_size, dst.size());

    hailo_status status = transform_inner(src.data(),
        quant_buffer().data(), dst.data(), transpose_buffer());
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

size_t InputTransformContext::get_src_frame_size() const
{
    return m_src_frame_size;
}

size_t InputTransformContext::get_dst_frame_size() const
{
    return m_dst_frame_size;
}

Expected<bool> OutputTransformContext::is_transformation_required(
    const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
    const std::vector<hailo_quant_info_t> &quant_infos)
{
    auto host_format = HailoRTDefaults::expand_auto_format(dst_format, src_format);
    auto val = TransformContextUtils::is_transformation_required(HAILO_D2H_STREAM, src_image_shape, src_format, 
        dst_image_shape, host_format, quant_infos);
    return val;
}

bool OutputTransformContext::is_transformation_required(
    const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
    const hailo_quant_info_t &quant_info)
{
    LOGGER__WARNING("Using a deprecated function. Use is_transformation_required that recieves a vector of hailo_quant_info_t instead");
    if (Quantization::is_qp_valid(quant_info)) {
        LOGGER__ERROR("quant_info is invalid as the model was compiled with multiple quant_infos. Please compile again or provide a vector of quant_infos.");
        return true;
    }
    std::vector<hailo_quant_info_t> quant_infos = { quant_info };
    auto expected_is_transformation_required = is_transformation_required(src_image_shape, src_format, dst_image_shape, dst_format, quant_infos);
    if(!expected_is_transformation_required) {
        return true;
    }
    return expected_is_transformation_required.release();
}

Expected<std::unique_ptr<OutputTransformContext>> OutputTransformContext::create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info)
{
    auto status = validate_output_transform_params(src_image_shape, src_format, dst_image_shape, dst_format);
    CHECK_SUCCESS_AS_EXPECTED(status);

    if (dst_quant_infos.size() == 1) {
        CHECK_AS_EXPECTED(Quantization::is_qp_valid(dst_quant_infos.at(0)), HAILO_INVALID_ARGUMENT,
            "quant_info is invalid as the model was compiled with multiple quant_infos. Please compile again or provide a vector of quant_infos.");
    }

    if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == src_format.order) {
        return NMSOutputTransformContext::create(src_format, dst_format, dst_quant_infos, nms_info);
    }

    return FrameOutputTransformContext::create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_infos);
}

Expected<std::unique_ptr<OutputTransformContext>> OutputTransformContext::create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, const hailo_nms_info_t &nms_info)
{
    std::vector<hailo_quant_info_t> dst_quant_infos = { dst_quant_info };
    return create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_infos, nms_info);
}

Expected<std::unique_ptr<OutputTransformContext>> OutputTransformContext::create(const hailo_stream_info_t &stream_info,
    const hailo_transform_params_t &transform_params)
{
    std::vector<hailo_quant_info_t> quant_infos = { stream_info.quant_info };
    return create(stream_info.hw_shape, stream_info.format, stream_info.shape,
        transform_params.user_buffer_format, quant_infos, stream_info.nms_info);
}

Expected<std::unique_ptr<OutputTransformContext>> OutputTransformContext::create(const hailo_stream_info_t &stream_info, bool quantized,
    hailo_format_type_t format_type)
{
    std::vector<hailo_quant_info_t> quant_infos = { stream_info.quant_info };
    auto transform_params = HailoRTDefaults::get_transform_params(quantized, format_type);
    return create(stream_info.hw_shape, stream_info.format, stream_info.shape,
        transform_params.user_buffer_format, quant_infos, stream_info.nms_info);
}

Expected<std::unique_ptr<OutputTransformContext>> OutputTransformContext::create(OutputStream &output_stream,
    const hailo_transform_params_t &transform_params)
{
    auto stream_info = output_stream.get_info();
    auto dst_quant_infos = output_stream.get_quant_infos();
    return create(stream_info.hw_shape, stream_info.format, stream_info.shape,
        transform_params.user_buffer_format, dst_quant_infos, stream_info.nms_info);
}

OutputTransformContext::OutputTransformContext(size_t src_frame_size, const hailo_format_t &src_format, size_t dst_frame_size,
    const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, const bool should_quantize, 
    const bool should_transpose, const bool should_reorder, const bool should_pad_periph) :
        m_src_frame_size(src_frame_size),
        m_src_format(src_format),
        m_dst_frame_size(dst_frame_size),
        m_dst_format(dst_format),
        m_dst_quant_infos(dst_quant_infos),
        m_should_quantize(should_quantize),
        m_should_transpose(should_transpose),
        m_should_reorder(should_reorder),
        m_should_pad_periph(should_pad_periph)
{}

FrameOutputTransformContext::FrameOutputTransformContext(size_t src_frame_size, const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, size_t dst_frame_size, const hailo_3d_image_shape_t &dst_image_shape,
    const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, Buffer&& transpose_buffer,
    const bool should_quantize, const bool should_transpose, const bool should_reorder, const bool should_pad_periph) :
        OutputTransformContext(src_frame_size, src_format, dst_frame_size, dst_format, dst_quant_infos, should_quantize, 
            should_transpose, should_reorder, should_pad_periph), m_src_image_shape(src_image_shape), m_dst_image_shape(dst_image_shape), 
            m_transpose_buffer(std::move(transpose_buffer))
{
    // TODO: Add verification that quant infos size equals to features count (HRT-11052)

    bool are_all_qps_the_same = true;
    if (dst_quant_infos.size() > 1) {
        for (const auto &quant_info : dst_quant_infos) {
            if (0 != memcmp(&quant_info, &dst_quant_infos[0], sizeof(quant_info))) {
                are_all_qps_the_same = false;
                break;
            }
        }
    }
    m_are_all_qps_the_same = are_all_qps_the_same;

    switch (dst_format.order) {
    case HAILO_FORMAT_ORDER_NHW:
    case HAILO_FORMAT_ORDER_BAYER_RGB:
    case HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB:
    case HAILO_FORMAT_ORDER_NCHW:
        for (const auto &quant_info : dst_quant_infos) {
            m_quant_info_per_feature.emplace_back(quant_info.qp_zp, quant_info.qp_scale);
        }
        m_quant_infos_rep_count = static_cast<uint32_t>(dst_frame_size);
        break;
    case HAILO_FORMAT_ORDER_NHWC:
    case HAILO_FORMAT_ORDER_FCR:
    case HAILO_FORMAT_ORDER_F8CR:
    case HAILO_FORMAT_ORDER_NC:
    case HAILO_FORMAT_ORDER_RGB4:
        for (const auto &quant_info : dst_quant_infos) {
            m_quant_info_per_feature.emplace_back(quant_info.qp_zp, quant_info.qp_scale);
        }
        m_quant_infos_rep_count = 1;
        break;
    case HAILO_FORMAT_ORDER_NHCW:
        for (const auto &quant_info : dst_quant_infos) {
            m_quant_info_per_feature.emplace_back(quant_info.qp_zp, quant_info.qp_scale);
        }
        m_quant_infos_rep_count = dst_image_shape.width;
        break;
    default:
        LOGGER__CRITICAL("Got unknown format order = {}", HailoRTCommon::get_format_order_str(dst_format.order));
        break;
    }
}

Expected<std::unique_ptr<OutputTransformContext>> FrameOutputTransformContext::create(const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
    const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos)
{
    const auto internal_dst_format = HailoRTDefaults::expand_auto_format(dst_format, src_format);

    const auto src_frame_size = HailoRTCommon::get_periph_frame_size(src_image_shape, src_format);
    const auto dst_frame_size = HailoRTCommon::get_frame_size(dst_image_shape, internal_dst_format);

    auto should_quantize = TransformContextUtils::should_quantize(HAILO_D2H_STREAM, src_format, internal_dst_format);
    CHECK_EXPECTED(should_quantize);

    Buffer transpose_buffer;
    auto should_transpose = TransformContextUtils::should_transpose(src_format.flags, internal_dst_format.flags);
    if (should_transpose) {
        auto expected_transpose_buffer = Buffer::create(get_transpose_buffer_size(dst_image_shape, src_format.type));
        CHECK_EXPECTED(expected_transpose_buffer);
        transpose_buffer = expected_transpose_buffer.release();
    }

    auto should_reorder = TransformContextUtils::should_reorder(src_image_shape, src_format, dst_image_shape, internal_dst_format);
    auto should_pad_periph = TransformContextUtils::should_pad_periph(dst_image_shape, internal_dst_format);

    std::unique_ptr<OutputTransformContext> frame_transform_context = std::make_unique<FrameOutputTransformContext>(src_frame_size,
        src_image_shape, src_format, dst_frame_size, dst_image_shape, internal_dst_format, dst_quant_infos, std::move(transpose_buffer),
        *should_quantize, should_transpose, should_reorder, should_pad_periph);

    CHECK_AS_EXPECTED(nullptr != frame_transform_context, HAILO_OUT_OF_HOST_MEMORY);

    return frame_transform_context;
}

NMSOutputTransformContext::NMSOutputTransformContext(size_t src_frame_size, const hailo_format_t &src_format, 
    size_t dst_frame_size, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
    const hailo_nms_info_t &nms_info, Buffer &&quant_buffer, const bool should_quantize, const bool should_transpose) :
        OutputTransformContext(src_frame_size, src_format, dst_frame_size, dst_format, dst_quant_infos, should_quantize ,should_transpose, 
        true, false), m_nms_info(nms_info), m_chunk_offsets(nms_info.chunks_per_frame, 0), m_quant_buffer(std::move(quant_buffer))
{}

Expected<std::unique_ptr<OutputTransformContext>> NMSOutputTransformContext::create(const hailo_format_t &src_format,
    const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info)
{
    // Validate params
    CHECK_AS_EXPECTED(HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == src_format.order, HAILO_INVALID_ARGUMENT,
        "Format order should be HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP");

    const auto internal_dst_format = HailoRTDefaults::expand_auto_format(dst_format, src_format);

    CHECK_AS_EXPECTED(HailoRTCommon::is_nms_by_class(internal_dst_format.order),
        HAILO_INVALID_ARGUMENT, "Format order should be HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS");

    CHECK_AS_EXPECTED(HAILO_FORMAT_TYPE_FLOAT32 == internal_dst_format.type, HAILO_INVALID_ARGUMENT,
        "Format type of HAILO_FORMAT_TYPE_FLOAT32");

    const auto src_frame_size = HailoRTCommon::get_nms_hw_frame_size(nms_info);
    auto dst_frame_size = HailoRTCommon::get_nms_by_class_host_frame_size(nms_info, internal_dst_format);

    Buffer quant_buffer;
    auto should_quantize = TransformContextUtils::should_quantize(HAILO_D2H_STREAM, src_format, internal_dst_format);
    CHECK_EXPECTED(should_quantize);
    if (*should_quantize) {
        dst_frame_size = HailoRTCommon::get_nms_by_class_host_frame_size(nms_info, internal_dst_format);
        auto expected_nms_quant_buffer = Buffer::create(dst_frame_size, 0);
        CHECK_EXPECTED(expected_nms_quant_buffer);
        quant_buffer = expected_nms_quant_buffer.release();
    }

    auto should_transpose = TransformContextUtils::should_transpose(src_format.flags, internal_dst_format.flags);

    std::unique_ptr<OutputTransformContext> nms_transform_context = std::make_unique<NMSOutputTransformContext>(src_frame_size,
        src_format, dst_frame_size, internal_dst_format, dst_quant_infos, nms_info, std::move(quant_buffer),
        *should_quantize, should_transpose);
    CHECK_AS_EXPECTED(nullptr != nms_transform_context, HAILO_OUT_OF_HOST_MEMORY);

    return nms_transform_context;
}

hailo_status FrameOutputTransformContext::transform(const MemoryView src, MemoryView dst)
{
    /* Check sizes */
    CHECK(src.size() == m_src_frame_size, HAILO_INVALID_ARGUMENT,
        "src size must be {}. passed size - {}", m_src_frame_size, src.size());
    CHECK(dst.size() == m_dst_frame_size, HAILO_INVALID_ARGUMENT,
        "dst_size must be {}. passed size - {}", m_dst_frame_size, dst.size());

    auto status = transform_inner(src.data(), dst.data(), MemoryView(m_transpose_buffer));
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status NMSOutputTransformContext::transform(const MemoryView src, MemoryView dst)
{
    /* Check sizes */
    CHECK(src.size() == m_src_frame_size, HAILO_INVALID_ARGUMENT,
        "src size must be {}. passed size - {}", m_src_frame_size, src.size());
    CHECK(dst.size() == m_dst_frame_size, HAILO_INVALID_ARGUMENT,
        "dst_size must be {}. passed size - {}", m_dst_frame_size, dst.size());

    CHECK(HailoRTCommon::is_nms_by_class(m_dst_format.order),
        HAILO_INVALID_ARGUMENT, "Wrong format order {}", HailoRTCommon::get_format_order_str(m_dst_format.order));

    assert(HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == m_src_format.order);

    auto shape_size = HailoRTCommon::get_nms_by_class_host_shape_size(m_nms_info);

    if ((HAILO_FORMAT_FLAGS_TRANSPOSED & m_src_format.flags) || (HAILO_FORMAT_FLAGS_TRANSPOSED & m_dst_format.flags)) {
        LOGGER__ERROR("NMS doesn't support transposed format");
        return HAILO_INVALID_OPERATION;
    }

    auto dst_buffer = m_should_quantize ? m_quant_buffer.data() : dst.data();
    transform__d2h_NMS(src.data(), dst_buffer, m_nms_info, m_chunk_offsets);

    if (m_should_quantize) {
        CHECK((HAILO_FORMAT_TYPE_FLOAT32 == m_dst_format.type) && (HAILO_FORMAT_TYPE_UINT16 == m_src_format.type), HAILO_INTERNAL_FAILURE);
        Quantization::dequantize_output_buffer_nms<float32_t, uint16_t>((uint16_t*)m_quant_buffer.data(),
            (float32_t*)dst.data(), shape_size, m_dst_quant_infos[0], m_nms_info.number_of_classes); // TODO: Support NMS scale by feature (HRT-11052)
    }

    return HAILO_SUCCESS;
}

std::string FrameOutputTransformContext::description() const
{
    std::stringstream transform_description;
    bool first = true;

    if (m_should_quantize) {
        if (!first) {
            transform_description << " | ";
        } else {
            first = false;
        }
        transform_description << TransformContextUtils::make_quantization_description(m_src_format.type, m_dst_format.type, m_dst_quant_infos);
    }

    if (m_should_transpose) {
        if (!first) {
            transform_description << " | ";
        } else {
            first = false;
        }
        transform_description << TransformContextUtils::make_transpose_description(m_src_image_shape, transposed_shape(m_src_image_shape));
    }

    if (m_should_reorder) {
        if (!first) {
            transform_description << " | ";
        } else {
            first = false;
        }
        transform_description << TransformContextUtils::make_reorder_description(m_src_format.order, m_src_image_shape, m_dst_format.order, m_dst_image_shape);
    }

    if (m_should_pad_periph) {
        if (!first) {
            transform_description << " | ";
        } else {
            first = false;
        }
        transform_description << TransformContextUtils::make_pad_periph_description(m_src_image_shape, m_dst_image_shape);
    }

    return transform_description.str();
}

std::string NMSOutputTransformContext::description() const
{
    std::stringstream transform_description;

    transform_description << "number_of_classes: " << m_nms_info.number_of_classes <<
        ", max_bboxes_per_class: " << m_nms_info.max_bboxes_per_class;

    if (m_should_quantize) {
        transform_description << " | " <<
            TransformContextUtils::make_quantization_description(m_src_format.type, m_dst_format.type, m_dst_quant_infos);
    }

    return transform_description.str();
}

size_t OutputTransformContext::get_src_frame_size() const
{
    return m_src_frame_size;
}

size_t OutputTransformContext::get_dst_frame_size() const
{
    return m_dst_frame_size;
}

Expected<std::unique_ptr<OutputDemuxer>> OutputDemuxer::create(OutputStream &output_stream)
{
    auto &stream_base = static_cast<OutputStreamBase&>(output_stream);
    auto obj = OutputDemuxerBase::create(stream_base.get_frame_size(), stream_base.get_layer_info());
    CHECK_EXPECTED(obj);

    auto obj_ptr = make_unique_nothrow<OutputDemuxerBase>(obj.release());
    CHECK_AS_EXPECTED(nullptr != obj_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return Expected<std::unique_ptr<OutputDemuxer>>(std::move(obj_ptr));
}

Expected<OutputDemuxerBase> OutputDemuxerBase::create(size_t src_frame_size, const LayerInfo &layer_info)
{
    // Validate params
    CHECK_AS_EXPECTED((HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP != layer_info.format.order), HAILO_INVALID_OPERATION,
        "NMS layer does not support mux.");

    auto mux_infos = get_mux_infos_from_layer_info(layer_info);
    CHECK_EXPECTED(mux_infos);

    return OutputDemuxerBase(src_frame_size, mux_infos.release());
}

hailo_status OutputDemuxerBase::get_mux_info_from_layer_info_impl(hailo_mux_info_t &mux_info, const LayerInfo &layer_info,
    uint32_t &offset, uint32_t height_ratio, std::vector<hailo_mux_info_t> &res, size_t &number_of_mux_infos)
{
    // This is a recursive function with a maximum depth of HailoRTCommon::MUX_INFO_COUNT. 
    const auto &stream_infos = LayerInfoUtils::get_stream_infos_from_layer_info(layer_info);
    assert(1 == stream_infos.size());
    mux_info.info = stream_infos[0];

    mux_info.row_size = height_ratio * layer_info.hw_shape.width * layer_info.hw_shape.features * layer_info.hw_data_bytes;
    mux_info.row_counter = 0;

    if (mux_info.info.is_mux) {
        int i = 0;
        CHECK(layer_info.predecessor.size() <= HailoRTCommon::MUX_INFO_COUNT, HAILO_INTERNAL_FAILURE, "Too many mux edges");
        for (auto &pred : layer_info.predecessor) {
            hailo_mux_info_t successor = {};
            auto status = get_mux_info_from_layer_info_impl(successor,
                pred, offset, layer_info.height_ratios[i], res, number_of_mux_infos);
            CHECK_SUCCESS(status);
            res.push_back(successor);
            mux_info.successors[i] = &(res.back());
            i++;
            number_of_mux_infos++;
        }
        mux_info.successors_count = static_cast<uint32_t>(layer_info.predecessor.size());
        mux_info.rows_gcd = layer_info.height_gcd;
    } else {
        mux_info.offset = offset;
        offset += mux_info.info.hw_frame_size;
    }

    return HAILO_SUCCESS;
}

hailo_status fuse_buffers(const std::vector<MemoryView> &buffers,
    const std::vector<hailo_nms_info_t> &infos_of_buffers, MemoryView dst)
{
    CHECK_ARG_NOT_NULL(dst.data());
    CHECK(buffers.size() == infos_of_buffers.size(), HAILO_INVALID_ARGUMENT,
        "Vectors of buffers and NMS infos does not match!");
    CHECK(HailoRTCommon::MAX_DEFUSED_LAYER_COUNT >= buffers.size(), HAILO_INVALID_ARGUMENT,
        "Buffers count is bigger than allowed! ({} > {})", buffers.size(), HailoRTCommon::MAX_DEFUSED_LAYER_COUNT);

    // Order the buffers by their class group index, which specifies in what order they should me fused.
    auto frames = std::vector<std::pair<const hailo_nms_info_t*, const MemoryView*>>(buffers.size());
    for (uint32_t i = 0; i < infos_of_buffers.size(); ++i) {
        frames[infos_of_buffers[i].defuse_info.class_group_index].first = &infos_of_buffers[i];
        frames[infos_of_buffers[i].defuse_info.class_group_index].second = &buffers[i];
    }

    uint32_t total_num_of_classes = 0;
    size_t total_size_of_buffers = 0;
    for (const auto &frame_pair : frames) {
        auto &info = *frame_pair.first;
        auto &buffer = *frame_pair.second;
        total_num_of_classes += info.number_of_classes * info.chunks_per_frame;
        // Remove extra burst at the end of every nms buffer
        total_size_of_buffers += buffer.size() - (info.bbox_size * info.burst_size);
        CHECK(buffer.size() == HailoRTCommon::get_nms_hw_frame_size(info), HAILO_INVALID_ARGUMENT,
            "Source buffer size is not same as NMS HW frame size! ({} != {})", buffer.size(),
            HailoRTCommon::get_nms_hw_frame_size(info));
    }

    // We keep the size of the dst buffer 1 burst_size too big to stay in the format of not defused nms frames.
    const auto burst_size = (frames[0].first->bbox_size * frames[0].first->burst_size);
    total_size_of_buffers += burst_size;

    CHECK(dst.size() == total_size_of_buffers, HAILO_INVALID_ARGUMENT,
        "Size of destination buffer is not same as the expected size of the fused frame! (size: {}, expected: {})",
        dst.size(), total_size_of_buffers);

    uint32_t offsets[HailoRTCommon::MAX_DEFUSED_LAYER_COUNT] = {0};
    uint32_t dst_offset = 0;
    for (uint32_t i = 0; i < total_num_of_classes; i++) {
        size_t buff_index = (i % frames.size());
        auto &info = *frames[buff_index].first;
        auto &buffer = *frames[buff_index].second;

        const uint8_t *src_ptr = buffer.data();
        // TODO: Maybe change asserts to checks
        assert(offsets[buff_index] + sizeof(nms_bbox_counter_t) <= buffer.size());
        nms_bbox_counter_t bbox_count = *reinterpret_cast<const nms_bbox_counter_t*>(src_ptr + offsets[buff_index]);
        uint32_t copy_size = static_cast<uint32_t>(sizeof(bbox_count) + bbox_count * info.bbox_size);
        assert(offsets[buff_index] + copy_size <= buffer.size());
        assert(dst_offset + copy_size <= dst.size());
        std::copy_n(src_ptr + offsets[buff_index], copy_size, dst.data() + dst_offset);
        offsets[buff_index] += copy_size;
        dst_offset += copy_size;
    }

    return HAILO_SUCCESS;
}

Expected<std::vector<hailo_mux_info_t>> OutputDemuxerBase::get_mux_infos_from_layer_info(const LayerInfo &layer_info)
{
    // Setting the first mux
    std::vector<hailo_mux_info_t> res;
    res.reserve(HailoRTCommon::MUX_INFO_COUNT);
    res.push_back({});
    uint32_t offset = 0;
    uint32_t height_ratio = 0;
    size_t number_of_mux_infos = 1;

    auto status = get_mux_info_from_layer_info_impl(res[0], layer_info, offset, height_ratio, res, number_of_mux_infos);
    CHECK_SUCCESS_AS_EXPECTED(status);
    res.resize(number_of_mux_infos);

    return res;
}

OutputDemuxerBase::OutputDemuxerBase(size_t src_frame_size, std::vector<hailo_mux_info_t> &&mux_infos) :
        OutputDemuxer(src_frame_size),
        m_mux_infos(std::move(mux_infos)) {}

hailo_status OutputDemuxerBase::transform_demux(const MemoryView src, std::vector<MemoryView> &raw_buffers)
{
    size_t raw_buffer_index = 0;
    size_t total_mux_sizes = 0;

    CHECK(raw_buffers.size() == get_edges_stream_info().size(), HAILO_INVALID_ARGUMENT,
        "There is a missmatch between mux edges counts ({}) and raw_buffers_size ({})", get_edges_stream_info().size(),
        raw_buffers.size());

    // Reset the runtime offset
    for (auto &mux_edge : m_mux_infos) {
        if (!mux_edge.info.is_mux) {
            mux_edge.buffer = (void*)((uintptr_t)raw_buffers[raw_buffer_index].data());
            mux_edge.current_offset = 0;
            mux_edge.row_counter = 0;
            CHECK((mux_edge.info.hw_frame_size == raw_buffers[raw_buffer_index].size()), HAILO_INVALID_ARGUMENT,
                "Expected buffer size of {}, got {}", mux_edge.info.hw_frame_size, raw_buffers[raw_buffer_index].size());
            total_mux_sizes += mux_edge.info.hw_frame_size;
            raw_buffer_index++;
        }
    }
    CHECK(total_mux_sizes == src.size(), HAILO_INVALID_ARGUMENT,
        "src_size must be: {}, passed_size: {}", total_mux_sizes, src.size());

    // TODO: Optimization - Read directly to user raw buffers (in case of NO_TRANSFORM, INPLACE_TRANSFORM)

    auto first_mux_info = m_mux_infos[0];
    return transform_demux_raw_frame(src.data(), 0, &first_mux_info, first_mux_info.rows_gcd);
}

hailo_status OutputDemuxerBase::transform_demux(const MemoryView src, const std::map<std::string, MemoryView> &dst_ptrs)
{
    size_t total_mux_sizes = 0;
    // Reset the runtime offset
    for (auto &mux_edge : m_mux_infos) {
        if (!mux_edge.info.is_mux) {
            auto name = std::string(mux_edge.info.name);
            CHECK(contains(dst_ptrs, name), HAILO_INVALID_ARGUMENT, "edge name {} is not in dst_ptrs", name);
            mux_edge.buffer = const_cast<void*>(reinterpret_cast<const void*>((dst_ptrs.at(name)).data()));
            mux_edge.current_offset = 0;
            mux_edge.row_counter = 0;
            CHECK((mux_edge.info.hw_frame_size == (dst_ptrs.at(name)).size()), HAILO_INVALID_ARGUMENT,
                "Expected buffer size of {}, got {}", mux_edge.info.hw_frame_size, (dst_ptrs.at(name)).size());
            total_mux_sizes += mux_edge.info.hw_frame_size;
        }
    }
    CHECK(total_mux_sizes == src.size(), HAILO_INVALID_ARGUMENT, "src_size must be: {}, passed_size: {}",
        total_mux_sizes, src.size());

    auto first_mux_info = m_mux_infos[0];
    return transform_demux_raw_frame(src.data(), 0, &first_mux_info, first_mux_info.rows_gcd);
}

} /* namespace hailort */
