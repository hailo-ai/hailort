/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#include "hailort_defaults.hpp"
#include "common/compiler_extensions_compat.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "transform_internal.hpp"
#include "microprofile.h"

#include <type_traits>
#include <sstream>

namespace hailort
{

#define HW_DATA_ALIGNMENT (8)
#define RGB_FEATURES (3)


bool TransformContextUtils::should_quantize(const hailo_stream_direction_t stream_direction, 
    const hailo_format_t &src_format, const hailo_format_t &dst_format, const hailo_quant_info_t &quant_info)
{
    if (HAILO_H2D_STREAM == stream_direction) {
        return (!(HAILO_FORMAT_FLAGS_QUANTIZED & src_format.flags) &&
            (HAILO_FORMAT_FLAGS_QUANTIZED & dst_format.flags) &&
            !((Quantization::is_identity_qp(quant_info)) && (src_format.type == dst_format.type)));
    } else {
        return (HAILO_FORMAT_FLAGS_QUANTIZED & src_format.flags) && 
            !(HAILO_FORMAT_FLAGS_QUANTIZED & dst_format.flags);
    }
}

bool TransformContextUtils::should_transpose(const hailo_format_flags_t &src_flags, const hailo_format_flags_t &dst_flags)
{
    return ((HAILO_FORMAT_FLAGS_TRANSPOSED & src_flags) != (HAILO_FORMAT_FLAGS_TRANSPOSED & dst_flags));
}

bool TransformContextUtils::should_reorder(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format)
{

    /* If shapes and format are different - need to use transform_context */
    if  (!((src_image_shape.features    == dst_image_shape.features)     && 
            (src_image_shape.height         == dst_image_shape.height)   && 
            (src_image_shape.width          == dst_image_shape.width)    &&
            (src_format.order               == dst_format.order)         &&
            (src_format.type                == dst_format.type))) {
        return true;
    }

    /* Some orders has to be reordered, even if shapes and types are the same 
    Note: In order to add new order to the list - add test to test_transform with all shapes and types same 
    pre and post transform */
    switch (src_format.order) {
        case HAILO_FORMAT_ORDER_NHWC:
        case HAILO_FORMAT_ORDER_NHCW:
        case HAILO_FORMAT_ORDER_NC:
        case HAILO_FORMAT_ORDER_NHW:
        case HAILO_FORMAT_ORDER_FCR:
        case HAILO_FORMAT_ORDER_BAYER_RGB:
        case HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB:
        case HAILO_FORMAT_ORDER_YUY2:
            return false;
        case HAILO_FORMAT_ORDER_F8CR:
        case HAILO_FORMAT_ORDER_HAILO_NMS:
        case HAILO_FORMAT_ORDER_RGB888:
        case HAILO_FORMAT_ORDER_NCHW:
        case HAILO_FORMAT_ORDER_NV12:
        case HAILO_FORMAT_ORDER_NV21:
            return true;
        default:
            LOGGER__WARN("Hailo Internal warning - Unrecognised order. Transformation optimization would not be activated");
            /* In case user asks to add new order - please add this order to one of the true or false lists */
            assert(false);
            return true;
    }
}

bool TransformContextUtils::is_transformation_required(const hailo_stream_direction_t stream_direction,
    const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const hailo_quant_info_t &quant_info)
{
    /* This function should be called after auto expend function */
    assert((HAILO_FORMAT_ORDER_AUTO != src_format.order) && (HAILO_FORMAT_ORDER_AUTO != dst_format.order));
    assert((HAILO_FORMAT_TYPE_AUTO != src_format.type) && (HAILO_FORMAT_TYPE_AUTO != dst_format.type));

    return (should_quantize(stream_direction, src_format, dst_format, quant_info) ||
        should_transpose(src_format.flags, dst_format.flags) ||
        should_reorder(src_image_shape, src_format, dst_image_shape, dst_format));
}

std::string TransformContextUtils::make_quantization_description(hailo_format_type_t src_type,
    hailo_format_type_t dst_type, hailo_quant_info_t quant_info)
{
    std::stringstream quant_description;
    quant_description << "Quantization - src_type: " << HailoRTCommon::get_format_type_str(src_type) <<
        ", dst_type " << HailoRTCommon::get_format_type_str(dst_type) <<
        ", qp_scale: " << quant_info.qp_scale <<
        ", qp_zp: " << quant_info.qp_zp <<
        ", limvals_min: " << quant_info.limvals_min <<
        ", limvals_max: " << quant_info.limvals_max;

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
    case HAILO_FORMAT_ORDER_NHW:
    case HAILO_FORMAT_ORDER_BAYER_RGB:
    case HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB:
    case HAILO_FORMAT_ORDER_FCR:
    case HAILO_FORMAT_ORDER_F8CR:
        return transform__transpose_NHWC(src_ptr, shape, HailoRTCommon::get_format_data_bytes(format), dst_ptr);
    default:
        LOGGER__ERROR("Transpose is not supported for order {}", format.order);
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
template <typename T>
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

template <typename T>
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

template <typename T>
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
                memset(dst_ptr + dst_offset, 0, pad_size);
            }
        }
    }
}

template <typename T>
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

template <typename T>
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

template <typename T>
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

template <typename T>
void transform__d2h_NC_to_NC(const T *src_ptr, T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    memcpy(dst_ptr, src_ptr, dst_image_shape->features * sizeof(T));
}

static inline void transform__parse_and_copy_bbox (hailo_bbox_t *dst, uint64_t* proposal)
{
    dst->y_min = (uint16_t)((*((uint64_t*)proposal) & 0xfff000000000) >> 36);
    dst->x_min = (uint16_t)((*((uint64_t*)proposal) & 0xfff000000) >> 24);
    dst->y_max = (uint16_t)((*((uint64_t*)proposal) & 0xfff000) >> 12);
    dst->x_max = (uint16_t)((*((uint64_t*)proposal) & 0xfff));
    dst->score = (uint16_t)((*((uint64_t*)proposal) & 0xffff000000000000) >> 48);
}

void transform__d2h_NMS(const uint8_t *src_ptr, uint8_t *dst_ptr, const hailo_nms_info_t &nms_info, std::vector<size_t> &chunk_offsets)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

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
            *dst_bbox_counter = static_cast<nms_bbox_counter_t>(*dst_bbox_counter + class_bboxes_count);

            src_offset += sizeof(nms_bbox_counter_t);

            for (bbox_index = 0; bbox_index < class_bboxes_count; bbox_index++) {
                transform__parse_and_copy_bbox((hailo_bbox_t *)(dst_ptr + dst_offset), (uint64_t*)(src_ptr + src_offset));
                src_offset += bbox_size;
                dst_offset += sizeof(hailo_bbox_t);
            }

            chunk_offsets[chunk_index] = src_offset;
        }
    }
}

template <typename T>
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

template <typename T>
void transform__h2d_F8CR(const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
    T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

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
                dst_offset = r * dst_row_size + c * HW_DATA_ALIGNMENT + f * dst_image_shape->width;
                if (f + HW_DATA_ALIGNMENT <= src_image_shape->features) {
                    /* take 8 full features for each column and write them */
                    memcpy(dst_ptr + dst_offset, src_ptr + src_offset, HW_DATA_ALIGNMENT * sizeof(T));
                }
                else {
                    /* take the last 8 or less features, pad features to 8 and write */
                    auto last_features = (src_features % HW_DATA_ALIGNMENT);
                    auto remainder = (HW_DATA_ALIGNMENT - last_features);
                    memcpy(dst_ptr + dst_offset, src_ptr + src_offset, last_features * sizeof(T));
                    dst_offset += last_features;
                    memset(dst_ptr + dst_offset, 0, remainder * sizeof(T));
                }
            }
        }
    }
}

template <typename T>
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
                src_offset = r * src_row_size + c * HW_DATA_ALIGNMENT + f * src_image_shape->width;
                dst_offset = r * dst_row_size + c * dst_image_shape->features + f;
                if (f + HW_DATA_ALIGNMENT <= dst_image_shape->features) {
                    /* copy the first dst_image_features (which are aligned to 8)! */
                    memcpy(dst_ptr + dst_offset, src_ptr + src_offset, HW_DATA_ALIGNMENT * sizeof(T));
                    }
                else {
                    /* copy the last 8 or less features, remove pad */
                    memcpy(dst_ptr + dst_offset, src_ptr + src_offset, (dst_features % HW_DATA_ALIGNMENT) * sizeof(T));
                }
            }
        }
    }
}

template <typename T>
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

template <typename T>
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
    CHECK((dst_image_shape->width % HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT,
          "NCHW_to_NHCW Transform dst width must be aligned to {}", HW_DATA_ALIGNMENT);

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
hailo_status transform__d2h_NHCW_to_NCHW(
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
    CHECK(dst_image_shape->width <= src_image_shape->width, HAILO_INVALID_ARGUMENT,
          "NCHW_to_NHCW Transform dst width should be smaller/equal than src width");
    CHECK((src_image_shape->width % HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT,
          "NCHW_to_NHCW Transform src width must be aligned to {}", HW_DATA_ALIGNMENT);

    size_t width_size = dst_image_shape->width;
    for (uint32_t r = 0; r < src_image_shape->height; r++) {
        for (uint32_t c = 0; c < src_image_shape->features; c++) {
            // Copy width
            T *dst = dst_ptr +
                dst_image_shape->width * dst_image_shape->height * c +
                dst_image_shape->width * r;
            const T *src = src_ptr +
                src_image_shape->features * src_image_shape->width * r +
                src_image_shape->width * c;

            std::copy_n(src, width_size, dst);
        }
    }

    return HAILO_SUCCESS;
}

template<typename T>
hailo_status transform__d2h_argmax_NHCW_to_NHW(const T *src_ptr, const hailo_3d_image_shape_t &src_image_shape,
    T *dst_ptr, const hailo_3d_image_shape_t &dst_image_shape)
{
    assert(nullptr != src_ptr);
    assert(nullptr != dst_ptr);

    CHECK(src_image_shape.height == dst_image_shape.height, HAILO_INVALID_OPERATION,
        "NHCW_to_NHW argmax Transform is supported only when src height ({}) is equal to dst height ({})",
        src_image_shape.height, dst_image_shape.height);
    CHECK(src_image_shape.width >= dst_image_shape.width, HAILO_INVALID_OPERATION,
        "NHCW_to_NHW argmax Transform is supported only when src width ({}) is equal/larger than dst width ({})",
        src_image_shape.width, dst_image_shape.width);
    CHECK(dst_image_shape.features == 1, HAILO_INVALID_OPERATION,
        "NHCW_to_NHW argmax Transform is supported only when dst features ({}) is 1",
        dst_image_shape.features);
    CHECK(src_image_shape.features < std::numeric_limits<T>::max(), HAILO_INVALID_OPERATION,
        "NHCW_to_NHW argmax Transform is supported only when src features ({}) is smaller than {}",
        src_image_shape.features, std::numeric_limits<T>::max());

    const auto src_row_size = src_image_shape.width * src_image_shape.features;
    const auto dst_row_size = dst_image_shape.width;
    for (uint32_t r = 0; r < src_image_shape.height; r++) {
        // For each row, we iterate on all columns, and find the max feature. It can be implemented better by iteratre
        // over all features, and on each iteration save the max value for each column.
        const T *src_row = src_ptr + (r * src_row_size);
        T *dst_row = dst_ptr + (r * dst_row_size);
        for (uint32_t w = 0; w < dst_image_shape.width; w++) {
            const T *offset_in_row = src_row + w;
            T max_index = 0;
            T max_value = *offset_in_row;

            for (uint32_t c = 1; c < src_image_shape.features; c++) {
                offset_in_row += src_image_shape.width;
                const auto &current_value = *offset_in_row;
                if (current_value > max_value) {
                    max_index = static_cast<T>(c);
                    max_value = current_value;
                }
            }

            dst_row[w] = max_index;
        }
    }

    return HAILO_SUCCESS;
}


template <typename T>
hailo_status transform__h2d_YUY2_to_YUY2(const T *src_ptr, T *dst_ptr, uint32_t shape_size)
{
    /* Validate arguments */
    ASSERT(NULL != src_ptr);
    ASSERT(NULL != dst_ptr);

    CHECK((shape_size % HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT,
          "YUY2_to_YUY2 Transform shape_size must be aligned to {}", HW_DATA_ALIGNMENT);

    std::copy_n(src_ptr, shape_size, dst_ptr);

    return HAILO_SUCCESS;
}

hailo_status InputTransformContext::quantize_stream(const void *src_ptr, void *quant_buffer)
{
    auto shape_size = HailoRTCommon::get_shape_size(m_src_image_shape);

    switch (m_src_format.type) {
        case HAILO_FORMAT_TYPE_UINT8:
            if (m_dst_format.type == HAILO_FORMAT_TYPE_UINT8) {
                Quantization::quantize_input_buffer<uint8_t, uint8_t>((uint8_t*)src_ptr, (uint8_t*)quant_buffer, shape_size, m_dst_quant_info);
            }
            else {
                return HAILO_INVALID_OPERATION;
            }
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            if (m_dst_format.type == HAILO_FORMAT_TYPE_UINT16) {
                Quantization::quantize_input_buffer<uint16_t, uint16_t>((uint16_t*)src_ptr, (uint16_t *)quant_buffer, shape_size, m_dst_quant_info);
            }
            else {
                return HAILO_INVALID_OPERATION;
            }
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            if (m_dst_format.type == HAILO_FORMAT_TYPE_UINT8) {
                Quantization::quantize_input_buffer<float32_t, uint8_t>((float32_t*)src_ptr, (uint8_t*)quant_buffer, shape_size, m_dst_quant_info);
            }
            else if (m_dst_format.type == HAILO_FORMAT_TYPE_UINT16) {
                Quantization::quantize_input_buffer<float32_t, uint16_t>((float32_t*)src_ptr, (uint16_t*)quant_buffer, shape_size, m_dst_quant_info);
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
        case HAILO_FORMAT_TYPE_UINT8:
            if (HAILO_FORMAT_TYPE_UINT8 == m_src_format.type) {
                Quantization::dequantize_output_buffer_in_place<uint8_t, uint8_t>((uint8_t*)dst_ptr, shape_size, m_dst_quant_info);
            }
            else {
                return HAILO_INVALID_OPERATION;
            }
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            if (HAILO_FORMAT_TYPE_UINT16 == m_src_format.type) {
                Quantization::dequantize_output_buffer_in_place<uint16_t, uint16_t>((uint16_t*)dst_ptr, shape_size, m_dst_quant_info);
            }
            else {
                return HAILO_INVALID_OPERATION;
            }
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            /* if output layer is argmax - do not rescale */
            if (HAILO_FORMAT_ORDER_NHW != m_dst_format.order) {
                if (m_src_format.type == HAILO_FORMAT_TYPE_UINT8) {
                    Quantization::dequantize_output_buffer_in_place<float32_t, uint8_t>((float32_t*)dst_ptr, shape_size, m_dst_quant_info);
                }
                else if (m_src_format.type == HAILO_FORMAT_TYPE_UINT16) {
                    Quantization::dequantize_output_buffer_in_place<float32_t, uint16_t>((float32_t*)dst_ptr, shape_size, m_dst_quant_info);
                }
                else {
                    return HAILO_INVALID_OPERATION;
                }
            } else {
                if (m_src_format.type == HAILO_FORMAT_TYPE_UINT8) {
                    cast_elements_inplace<float32_t, uint8_t>((float32_t*)dst_ptr, shape_size);
                }
                else if (m_src_format.type == HAILO_FORMAT_TYPE_UINT16) {
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
        assert(0 == (dst_image_shape.features % 8));
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
                    LOGGER__ERROR("Invalid src-buffer's type format {}", src_format.type);
                    return HAILO_INVALID_ARGUMENT;
            }
            return HAILO_SUCCESS;
    }

    LOGGER__ERROR("Unsupported input stream transformation from hailo_format_order_t "
                "{} to hailo_format_order_t {}", src_format.order, dst_format.order);
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
                    transform__d2h_NHCW_to_NCHW<uint8_t>((uint8_t*)src_ptr, &src_image_shape, (uint8_t*)dst_ptr, &dst_image_shape);
                    break;
                case HAILO_FORMAT_TYPE_UINT16:
                    transform__d2h_NHCW_to_NCHW<uint16_t>((uint16_t*)src_ptr, &src_image_shape, (uint16_t*)dst_ptr, &dst_image_shape);
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
    } else if ((HAILO_FORMAT_ORDER_NHCW == src_format.order) &&
               (HAILO_FORMAT_ORDER_NHW == dst_format.order)  &&
               (0 != (HAILO_FORMAT_FLAGS_HOST_ARGMAX & src_format.flags)))  {
            switch (src_format.type) {
            case HAILO_FORMAT_TYPE_UINT8:
                return transform__d2h_argmax_NHCW_to_NHW<uint8_t>((uint8_t*)src_ptr, src_image_shape, (uint8_t*)dst_ptr, dst_image_shape);
            case HAILO_FORMAT_TYPE_UINT16:
                return transform__d2h_argmax_NHCW_to_NHW<uint16_t>((uint16_t*)src_ptr, src_image_shape, (uint16_t*)dst_ptr, dst_image_shape);
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
                    LOGGER__ERROR("Invalid src-buffer's type format {}", src_format.type);
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

    if (!(m_should_quantize || m_should_transpose || m_should_reorder)) {
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

    if (!(m_should_quantize || m_should_transpose || m_should_reorder)) {
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

    if (m_should_quantize) {
        auto status = quantize_stream(dst_ptr);
        CHECK_SUCCESS(status);
    }
    
    if (!(m_should_transpose || m_should_reorder)) {
        /* If quantize is the only step - need to copy src buffer to dst buffer */
        auto frame_size = HailoRTCommon::get_frame_size(m_dst_image_shape, m_dst_format);
        memcpy(dst_ptr, src_ptr, frame_size);
    }

    return HAILO_SUCCESS;
}


hailo_status transform_demux_raw_frame(const void *src, uint32_t offset,
    hailo_mux_info_t *mux_info, uint32_t mux_row_count)
{
    MICROPROFILE_SCOPEI("Transformations", "Demux", 0);
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
    hailo_3d_image_shape_t dst_image_shape, hailo_format_t dst_format)
{
    /* Check quantize flags - where quantize is no needed */
    if ((HAILO_FORMAT_FLAGS_QUANTIZED & src_format.flags) && !(HAILO_FORMAT_FLAGS_QUANTIZED & dst_format.flags)) {
        LOGGER__ERROR("Cannot dequantize input data");
        return HAILO_INVALID_ARGUMENT;
    }

    if ((HAILO_FORMAT_FLAGS_QUANTIZED & src_format.flags) && (HAILO_FORMAT_TYPE_FLOAT32 == src_format.type)) {
        LOGGER__ERROR("float32 data isn't quantized");
        return HAILO_INVALID_ARGUMENT;
    }

    /* Check for overscale transformation*/
    CHECK((hailo_format_type_t::HAILO_FORMAT_TYPE_AUTO == src_format.type) || (src_format.type >= dst_format.type),
        HAILO_INVALID_ARGUMENT, "Overscale transformation is not supported");

    /* Check device type */
    if (!((HAILO_FORMAT_TYPE_UINT16 == dst_format.type) || (HAILO_FORMAT_TYPE_UINT8 == dst_format.type))) {
        LOGGER__ERROR("unsupported device type {}", dst_format.type);
        return HAILO_INVALID_ARGUMENT;
    }

    /* Check reorder flags - where no reorder is needed */
    if ((HAILO_FORMAT_ORDER_FCR == src_format.order) &&
        (HAILO_FORMAT_ORDER_FCR == dst_format.order)) {
        if (0 != (dst_image_shape.features % 8)) {
            LOGGER__ERROR("HW features must be aligned to {}. passed hw features - {}",
                HW_DATA_ALIGNMENT, dst_image_shape.features);
            return HAILO_INVALID_ARGUMENT;
        }
    } else if ((HAILO_FORMAT_ORDER_BAYER_RGB == src_format.order) &&
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
    } else if ((HAILO_FORMAT_ORDER_YUY2 == src_format.order) &&
        (HAILO_FORMAT_ORDER_YUY2 == dst_format.order)) {
        auto shape_size = HailoRTCommon::get_shape_size(src_image_shape);
        CHECK((shape_size % HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT,
          "YUY2_to_YUY2 Transform shape_size must be aligned to {}", HW_DATA_ALIGNMENT);
    }

    return HAILO_SUCCESS;
}

hailo_status validate_output_transform_params(hailo_3d_image_shape_t src_image_shape, hailo_format_t src_format,
    hailo_3d_image_shape_t dst_image_shape, hailo_format_t dst_format)
{
    /* Check quantize flags - where quantize is no needed */
    if (!(HAILO_FORMAT_FLAGS_QUANTIZED & src_format.flags) && (HAILO_FORMAT_FLAGS_QUANTIZED & dst_format.flags)) {
        LOGGER__ERROR("Cannot quantize output data");
        return HAILO_INVALID_ARGUMENT;
    }

    /* Check device type */
    if (!((HAILO_FORMAT_TYPE_UINT16 == src_format.type) || (HAILO_FORMAT_TYPE_UINT8 == src_format.type))) {
        LOGGER__ERROR("unsupported device type {}", dst_format.type);
        return HAILO_INVALID_ARGUMENT;
    }

    /* Check for underscale transformation*/
    CHECK((hailo_format_type_t::HAILO_FORMAT_TYPE_AUTO == dst_format.type) || (src_format.type <= dst_format.type),
        HAILO_INVALID_ARGUMENT, "Underscale transformation is not supported");

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

bool InputTransformContext::is_transformation_required(
    const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format, 
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, 
    const hailo_quant_info_t &quant_info)
{
    auto host_format = HailoRTDefaults::expand_auto_format(src_format, dst_format);
    return TransformContextUtils::is_transformation_required(HAILO_H2D_STREAM, src_image_shape, host_format,
        dst_image_shape, dst_format, quant_info);
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
        transform_description << TransformContextUtils::make_quantization_description(m_src_format.type, m_dst_format.type, m_dst_quant_info);
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

    return transform_description.str();
}

Expected<std::unique_ptr<InputTransformContext>> InputTransformContext::create(const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
    const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info)
{
    auto status = validate_input_transform_params(src_image_shape, src_format, dst_image_shape, dst_format);
    CHECK_SUCCESS_AS_EXPECTED(status);

    const auto internal_src_format = HailoRTDefaults::expand_auto_format(src_format, dst_format);

    const auto src_frame_size = HailoRTCommon::get_frame_size(src_image_shape, internal_src_format);
    const auto dst_frame_size = HailoRTCommon::get_frame_size(dst_image_shape, dst_format);

    Buffer quant_buffer;
    bool should_quantize = TransformContextUtils::should_quantize(HAILO_H2D_STREAM, src_format, dst_format, 
        dst_quant_info);
    if (should_quantize) {
        auto expected_quant_buffer = Buffer::create(src_frame_size, 0);
        CHECK_EXPECTED(expected_quant_buffer);
        quant_buffer = expected_quant_buffer.release();
    }

    Buffer transpose_buffer;
    bool should_transpose = TransformContextUtils::should_transpose(src_format.flags, dst_format.flags);
    if (should_transpose) {
        auto expected_transpose_buffer = Buffer::create(get_transpose_buffer_size(src_image_shape,
            dst_format.type));
        CHECK_EXPECTED(expected_transpose_buffer);
        transpose_buffer = expected_transpose_buffer.release();
    }

    auto should_reorder = TransformContextUtils::should_reorder(src_image_shape, src_format, dst_image_shape, dst_format);

    std::unique_ptr<InputTransformContext> transform_context(new (std::nothrow) InputTransformContext(src_frame_size, src_image_shape,
        internal_src_format, dst_frame_size, dst_image_shape, dst_format, dst_quant_info, std::move(quant_buffer),
        std::move(transpose_buffer), should_quantize, should_transpose, should_reorder));
    CHECK_AS_EXPECTED(nullptr != transform_context, HAILO_OUT_OF_HOST_MEMORY);

    return transform_context;
}

Expected<std::unique_ptr<InputTransformContext>> InputTransformContext::create(const hailo_stream_info_t &stream_info,
    const hailo_transform_params_t &transform_params)
{
    return create(stream_info.shape, transform_params.user_buffer_format, stream_info.hw_shape, stream_info.format,
        stream_info.quant_info);
}

Expected<std::unique_ptr<InputTransformContext>> InputTransformContext::create(const hailo_stream_info_t &stream_info, bool quantized,
    hailo_format_type_t format_type)
{
    return create(stream_info, HailoRTDefaults::get_transform_params(quantized, format_type));
}

InputTransformContext::InputTransformContext(size_t src_frame_size, const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, size_t dst_frame_size, const hailo_3d_image_shape_t &dst_image_shape,
    const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, Buffer &&quant_buffer,
    Buffer &&transpose_buffer,const bool should_quantize, const bool should_transpose, const bool should_reorder) :
        m_src_frame_size(src_frame_size),
        m_src_image_shape(src_image_shape),
        m_src_format(src_format),
        m_dst_frame_size(dst_frame_size),
        m_dst_image_shape(dst_image_shape),
        m_dst_format(dst_format),
        m_dst_quant_info(dst_quant_info),
        m_should_quantize(should_quantize),
        m_should_transpose(should_transpose),
        m_should_reorder(should_reorder),
        m_quant_buffer(std::move(quant_buffer)),
        m_transpose_buffer(std::move(transpose_buffer))
{}

hailo_status InputTransformContext::transform(const MemoryView src, MemoryView dst)
{
    MICROPROFILE_SCOPEI("Transformations", "H2D transform", 0);
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

bool OutputTransformContext::is_transformation_required(
    const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format, 
    const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, 
    const hailo_quant_info_t &quant_info)
{
    auto host_format = HailoRTDefaults::expand_auto_format(dst_format, src_format);
    return TransformContextUtils::is_transformation_required(HAILO_D2H_STREAM, src_image_shape, src_format, 
        dst_image_shape, host_format, quant_info);
}

Expected<std::unique_ptr<OutputTransformContext>> OutputTransformContext::create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, const hailo_nms_info_t &nms_info)
{
    auto status = validate_output_transform_params(src_image_shape, src_format, dst_image_shape, dst_format);
    CHECK_SUCCESS_AS_EXPECTED(status);
    
    if (HAILO_FORMAT_ORDER_HAILO_NMS == src_format.order) {
        return NMSOutputTransformContext::create(src_format, dst_format, dst_quant_info, nms_info);
    }

    return FrameOutputTransformContext::create(src_image_shape, src_format, dst_image_shape, dst_format, dst_quant_info);
}

Expected<std::unique_ptr<OutputTransformContext>> OutputTransformContext::create(const hailo_stream_info_t &stream_info,
    const hailo_transform_params_t &transform_params)
{
    return create(stream_info.hw_shape, stream_info.format, stream_info.shape,
        transform_params.user_buffer_format, stream_info.quant_info, stream_info.nms_info);
}

Expected<std::unique_ptr<OutputTransformContext>> OutputTransformContext::create(const hailo_stream_info_t &stream_info, bool quantized,
    hailo_format_type_t format_type)
{
    return create(stream_info, HailoRTDefaults::get_transform_params(quantized, format_type));
}

OutputTransformContext::OutputTransformContext(size_t src_frame_size, const hailo_format_t &src_format, size_t dst_frame_size,
    const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, const bool should_quantize, 
    const bool should_transpose, const bool should_reorder) :
        m_src_frame_size(src_frame_size),
        m_src_format(src_format),
        m_dst_frame_size(dst_frame_size),
        m_dst_format(dst_format),
        m_dst_quant_info(dst_quant_info),
        m_should_quantize(should_quantize),
        m_should_transpose(should_transpose),
        m_should_reorder(should_reorder)
{}

FrameOutputTransformContext::FrameOutputTransformContext(size_t src_frame_size, const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, size_t dst_frame_size, const hailo_3d_image_shape_t &dst_image_shape,
    const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, Buffer&& transpose_buffer,
    const bool should_quantize, const bool should_transpose, const bool should_reorder) :
        OutputTransformContext(src_frame_size, src_format, dst_frame_size, dst_format, dst_quant_info, should_quantize, 
            should_transpose, should_reorder), m_src_image_shape(src_image_shape), m_dst_image_shape(dst_image_shape), 
            m_transpose_buffer(std::move(transpose_buffer))
{}

Expected<std::unique_ptr<OutputTransformContext>> FrameOutputTransformContext::create(const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
    const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info)
{
    const auto internal_dst_format = HailoRTDefaults::expand_auto_format(dst_format, src_format);

    const auto src_frame_size = HailoRTCommon::get_frame_size(src_image_shape, src_format);
    const auto dst_frame_size = HailoRTCommon::get_frame_size(dst_image_shape, internal_dst_format);

    auto should_quantize = TransformContextUtils::should_quantize(HAILO_D2H_STREAM, src_format, dst_format, 
        dst_quant_info);

    Buffer transpose_buffer;
    auto should_transpose = TransformContextUtils::should_transpose(src_format.flags, dst_format.flags);
    if (should_transpose) {
        auto expected_transpose_buffer = Buffer::create(get_transpose_buffer_size(dst_image_shape, src_format.type));
        CHECK_EXPECTED(expected_transpose_buffer);
        transpose_buffer = expected_transpose_buffer.release();
    }

    auto should_reorder = TransformContextUtils::should_reorder(src_image_shape, src_format, dst_image_shape, dst_format);

    std::unique_ptr<OutputTransformContext> frame_transform_context = std::make_unique<FrameOutputTransformContext>(src_frame_size,
        src_image_shape, src_format, dst_frame_size, dst_image_shape, internal_dst_format, dst_quant_info, std::move(transpose_buffer),
        should_quantize, should_transpose, should_reorder);

    CHECK_AS_EXPECTED(nullptr != frame_transform_context, HAILO_OUT_OF_HOST_MEMORY);

    return frame_transform_context;
}

NMSOutputTransformContext::NMSOutputTransformContext(size_t src_frame_size, const hailo_format_t &src_format, 
    size_t dst_frame_size, const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info,
    const hailo_nms_info_t &nms_info, Buffer &&quant_buffer, const bool should_quantize, const bool should_transpose) :
        OutputTransformContext(src_frame_size, src_format, dst_frame_size, dst_format, dst_quant_info, should_quantize ,should_transpose, 
        true), m_nms_info(nms_info), m_chunk_offsets(nms_info.chunks_per_frame, 0), m_quant_buffer(std::move(quant_buffer))
{}

Expected<std::unique_ptr<OutputTransformContext>> NMSOutputTransformContext::create(const hailo_format_t &src_format,
    const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, const hailo_nms_info_t &nms_info)
{
    // Validate params
    CHECK_AS_EXPECTED(HAILO_FORMAT_ORDER_HAILO_NMS == src_format.order, HAILO_INVALID_ARGUMENT,
        "Format order should be HAILO_FORMAT_ORDER_HAILO_NMS");

    const auto internal_dst_format = HailoRTDefaults::expand_auto_format(dst_format, src_format);

    CHECK_AS_EXPECTED(HAILO_FORMAT_ORDER_HAILO_NMS == internal_dst_format.order, HAILO_INVALID_ARGUMENT,
        "Format order should be HAILO_FORMAT_ORDER_HAILO_NMS");

    if (internal_dst_format.flags & HAILO_FORMAT_FLAGS_QUANTIZED) {
        CHECK_AS_EXPECTED(HAILO_FORMAT_TYPE_UINT16 == internal_dst_format.type, HAILO_INVALID_ARGUMENT,
            "Format order HAILO_FORMAT_ORDER_HAILO_NMS without quantization is allowed only with type HAILO_FORMAT_TYPE_UINT16");
    }
    else {
        CHECK_AS_EXPECTED((HAILO_FORMAT_TYPE_UINT16 == internal_dst_format.type) || (HAILO_FORMAT_TYPE_FLOAT32 == internal_dst_format.type),
            HAILO_INVALID_ARGUMENT,
            "Format order HAILO_FORMAT_ORDER_HAILO_NMS with quantization is allowed only with type HAILO_FORMAT_TYPE_UINT16 or HAILO_FORMAT_TYPE_FLOAT32");
    }

    const auto src_frame_size = HailoRTCommon::get_nms_hw_frame_size(nms_info);
    auto dst_frame_size = HailoRTCommon::get_nms_host_frame_size(nms_info, internal_dst_format);

    Buffer quant_buffer;
    const bool should_quantize = (src_format.flags & HAILO_FORMAT_FLAGS_QUANTIZED) &&
        !(internal_dst_format.flags & HAILO_FORMAT_FLAGS_QUANTIZED);
    if (should_quantize) {
        dst_frame_size = HailoRTCommon::get_nms_host_frame_size(nms_info, internal_dst_format);
        auto expected_nms_quant_buffer = Buffer::create(dst_frame_size, 0);
        CHECK_EXPECTED(expected_nms_quant_buffer);
        quant_buffer = expected_nms_quant_buffer.release();
    }

    auto should_transpose = TransformContextUtils::should_transpose(src_format.flags, dst_format.flags);

    std::unique_ptr<OutputTransformContext> nms_transform_context = std::make_unique<NMSOutputTransformContext>(src_frame_size,
        src_format, dst_frame_size, internal_dst_format, dst_quant_info, nms_info, std::move(quant_buffer),
        should_quantize, should_transpose);
    CHECK_AS_EXPECTED(nullptr != nms_transform_context, HAILO_OUT_OF_HOST_MEMORY);

    return nms_transform_context;
}

hailo_status FrameOutputTransformContext::transform(const MemoryView src, MemoryView dst)
{
    MICROPROFILE_SCOPEI("Transformations", "D2H transform", 0);
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
    MICROPROFILE_SCOPEI("Transformations", "D2H NMS transform", 0);
    /* Check sizes */
    CHECK(src.size() == m_src_frame_size, HAILO_INVALID_ARGUMENT,
        "src size must be {}. passed size - {}", m_src_frame_size, src.size());
    CHECK(dst.size() == m_dst_frame_size, HAILO_INVALID_ARGUMENT,
        "dst_size must be {}. passed size - {}", m_dst_frame_size, dst.size());

    assert((HAILO_FORMAT_ORDER_HAILO_NMS == m_src_format.order) && (HAILO_FORMAT_ORDER_HAILO_NMS == m_dst_format.order));

    auto shape_size = HailoRTCommon::get_nms_host_shape_size(m_nms_info);

    if (!(HAILO_FORMAT_FLAGS_QUANTIZED & m_src_format.flags) && (HAILO_FORMAT_FLAGS_QUANTIZED & m_dst_format.flags)) {
        LOGGER__ERROR("Cannot quantize output data");
        return HAILO_INVALID_OPERATION;
    }

    if ((HAILO_FORMAT_FLAGS_TRANSPOSED & m_src_format.flags) || (HAILO_FORMAT_FLAGS_TRANSPOSED & m_dst_format.flags)) {
        LOGGER__ERROR("NMS doesn't support transposed format currently");
        return HAILO_INVALID_OPERATION;
    }

    if (!((HAILO_FORMAT_FLAGS_QUANTIZED & m_src_format.flags) &&
        !(HAILO_FORMAT_FLAGS_QUANTIZED & m_dst_format.flags))) {
            transform__d2h_NMS((uint8_t*)src.data(), (uint8_t*)dst.data(), m_nms_info, m_chunk_offsets);
    } 
    else {
        transform__d2h_NMS((uint8_t*)src.data(), m_quant_buffer.data(), m_nms_info, m_chunk_offsets);
    }

    if ((HAILO_FORMAT_FLAGS_QUANTIZED & m_src_format.flags) && !(HAILO_FORMAT_FLAGS_QUANTIZED & m_dst_format.flags)) {
        // NMS has to be uint16 or float32
        switch (m_dst_format.type) {
            case HAILO_FORMAT_TYPE_UINT16:
                if (m_src_format.type == HAILO_FORMAT_TYPE_UINT16) {
                    Quantization::dequantize_output_buffer_nms<uint16_t, uint16_t>((uint16_t*)m_quant_buffer.data(),
                        (uint16_t*)dst.data(), shape_size, m_dst_quant_info, m_nms_info.number_of_classes);
                } 
                else {
                    return HAILO_INVALID_OPERATION;
                }
                break;
            case HAILO_FORMAT_TYPE_FLOAT32:
                if (m_src_format.type == HAILO_FORMAT_TYPE_UINT16) {
                    Quantization::dequantize_output_buffer_nms<float32_t, uint16_t>((uint16_t*)m_quant_buffer.data(),
                        (float32_t*)dst.data(), shape_size, m_dst_quant_info, m_nms_info.number_of_classes);
                }
                else {
                    return HAILO_INVALID_OPERATION;
                }
                break;
            default:
                LOGGER__ERROR("Invalid dst-buffer's type format");
                return HAILO_INVALID_ARGUMENT;
        }
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
        transform_description << TransformContextUtils::make_quantization_description(m_src_format.type, m_dst_format.type, m_dst_quant_info);
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

    return transform_description.str();
}

std::string NMSOutputTransformContext::description() const
{
    std::stringstream transform_description;

    transform_description << "number_of_classes: " << m_nms_info.number_of_classes <<
        ", max_bboxes_per_class: " << m_nms_info.max_bboxes_per_class;

    if (m_should_quantize) {
        transform_description << " | " <<
            TransformContextUtils::make_quantization_description(m_src_format.type, m_dst_format.type, m_dst_quant_info);
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
    auto obj = OutputDemuxerBase::create(output_stream.get_frame_size(), output_stream.get_layer_info());
    CHECK_EXPECTED(obj);

    auto obj_ptr = make_unique_nothrow<OutputDemuxerBase>(obj.release());
    CHECK_AS_EXPECTED(nullptr != obj_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return Expected<std::unique_ptr<OutputDemuxer>>(std::move(obj_ptr));
}

Expected<OutputDemuxerBase> OutputDemuxerBase::create(size_t src_frame_size, const LayerInfo &layer_info)
{
    // Validate params
    CHECK_AS_EXPECTED((HAILO_FORMAT_ORDER_HAILO_NMS != layer_info.format.order), HAILO_INVALID_OPERATION,
        "NMS layer does not support mux.");

    auto mux_infos = get_mux_infos_from_layer_info(layer_info);
    CHECK_EXPECTED(mux_infos);

    return OutputDemuxerBase(src_frame_size, mux_infos.release());
}

hailo_status OutputDemuxerBase::get_mux_info_from_layer_info_impl(hailo_mux_info_t &mux_info, const LayerInfo &layer_info,
    uint32_t &offset, uint32_t height_ratio, std::vector<hailo_mux_info_t> &res, size_t &number_of_mux_infos)
{
    // This is a recursive function with a maximum depth of HailoRTCommon::MUX_INFO_COUNT. 
    mux_info.info = LayerInfoUtils::get_stream_info_from_layer_info(layer_info);

    mux_info.row_size = height_ratio * layer_info.hw_shape.width * layer_info.hw_shape.features;
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
    MICROPROFILE_SCOPEI("Transformations", "Fuse NMS", 0);
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
        total_size_of_buffers += buffer.size();
        CHECK(buffer.size() == HailoRTCommon::get_nms_hw_frame_size(info), HAILO_INVALID_ARGUMENT,
            "Source buffer size is not same as NMS HW frame size! ({} != {})", buffer.size(),
            HailoRTCommon::get_nms_hw_frame_size(info));
    }

    // Each frame contributes 1 extra bbox_size at the end of it which acts as a delimiter, but we don't copy those to the fused buffer.
    // We keep the size of the dst buffer 1 bbox_size too big to stay in the format of not defused nms frames.
    total_size_of_buffers -= (frames.size() - 1) * frames[0].first->bbox_size;

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
