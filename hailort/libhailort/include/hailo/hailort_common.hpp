/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_common.hpp
 * @brief Common utility functions/macros that help manage hailort.h structures
 **/

#ifndef _HAILO_HAILORT_COMMON_HPP_
#define _HAILO_HAILORT_COMMON_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace hailort
{

/*! Common utility functions and macros that help manage hailort.h structures */
class HAILORTAPI HailoRTCommon final
{
public:
    HailoRTCommon() = delete;
    
    static_assert(sizeof(hailo_bbox_t) / sizeof(uint16_t) == sizeof(hailo_bbox_float32_t) / sizeof(float32_t),
        "Mismatch bbox params size");
    static const uint32_t BBOX_PARAMS = sizeof(hailo_bbox_t) / sizeof(uint16_t);
    static const uint32_t MAX_DEFUSED_LAYER_COUNT = 9;
    static const size_t HW_DATA_ALIGNMENT = 8;
    static const uint64_t NMS_DELIMITER = 0xFFFFFFFFFFFFFFFF;
    static const uint64_t NMS_DUMMY_DELIMITER = 0xFFFFFFFFFFFFFFFE;
    static const uint32_t MUX_INFO_COUNT = 32;
    static const uint32_t MAX_MUX_PREDECESSORS = 4;
    static const uint16_t ETH_INPUT_BASE_PORT = 32401;
    static const uint16_t ETH_OUTPUT_BASE_PORT = 32501;

    /**
     * Gets the NMS host shape size (number of elements) from NMS info.
     *
     * @param[in] nms_info             The NMS info to get shape size from.
     * @return The host shape size (number of elements).
     * @note The size in bytes can be calculated using 
     *  get_nms_host_frame_size(const hailo_nms_info_t &nms_info, const hailo_format_t &format).
     */
    static constexpr uint32_t get_nms_host_shape_size(const hailo_nms_info_t &nms_info)
    {
        const uint32_t max_bboxes_per_class = nms_info.chunks_per_frame * nms_info.max_bboxes_per_class;
        // Counter + bboxes
        const uint32_t size_per_class = 1 + (BBOX_PARAMS * max_bboxes_per_class);
        return size_per_class * nms_info.number_of_classes;
    }

    /**
     * Gets the NMS host shape size (number of elements) from NMS shape.
     *
     * @param[in] nms_shape             The NMS shape to get size from.
     * @return The host shape size (number of elements).
     * @note The size in bytes can be calculated using 
     *  get_nms_host_frame_size(const hailo_nms_shape_t &nms_shape, const hailo_format_t &format).
     */
    static constexpr uint32_t get_nms_host_shape_size(const hailo_nms_shape_t &nms_shape)
    {
        const uint32_t max_bboxes_per_class = nms_shape.max_bboxes_per_class;
        // Counter + bboxes
        const uint32_t size_per_class = 1 + (BBOX_PARAMS * max_bboxes_per_class);
        return size_per_class * nms_shape.number_of_classes;
    }

    /**
     * Gets the shape size.
     *
     * @param[in] shape             The shape to get size from.
     * @return The shape size.
     */
    static constexpr uint32_t get_shape_size(const hailo_3d_image_shape_t &shape)
    {
        return shape.height * shape.width * shape.features; 
    }

    /**
     * Gets the size of each element in bytes from buffer's format type.
     *
     * @param[in] type             A ::hailo_format_type_t object.
     * @return The data bytes.
     */
    static constexpr uint8_t get_data_bytes(hailo_format_type_t type)
    {
        if (type == HAILO_FORMAT_TYPE_FLOAT32) {
            return 4;
        } else if (type == HAILO_FORMAT_TYPE_UINT16) {
            return 2;
        } else if (type == HAILO_FORMAT_TYPE_UINT8) {
            return 1;
        }

        return 1;
    }

    /**
     * Gets the format type of a stream by the hw data bytes parameter.
     *
     * @param[in] hw_data_bytes             The stream's info's hw_data_bytes parameter.
     * @return Upon success, returns Expected of ::hailo_format_type_t, The format type that the hw_data_type correlates to.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<hailo_format_type_t> get_format_type(uint32_t hw_data_bytes)
    {
        switch (hw_data_bytes) {
            case 1:
                return HAILO_FORMAT_TYPE_UINT8;
            case 2:
                return HAILO_FORMAT_TYPE_UINT16;
            default:
                return make_unexpected(HAILO_INVALID_ARGUMENT);
        }
    }

    /**
     * Gets a string reprenestation of the given format type.
     *
     * @param[in] type             A ::hailo_format_type_t object.
     * @return The string representation of the format type.
     */
    static std::string get_format_type_str(const hailo_format_type_t &type)
    {
        switch (type)
        {
        case HAILO_FORMAT_TYPE_UINT8:
            return "UINT8";
        case HAILO_FORMAT_TYPE_UINT16:
            return "UINT16";
        case HAILO_FORMAT_TYPE_FLOAT32:
            return "FLOAT32";
        default:
            return "Nan";
        }
    }

    /**
     * Gets a string reprenestation of the given format order.
     *
     * @param[in] order             A ::hailo_format_order_t object.
     * @return The string representation of the format order.
     */
    static std::string get_format_order_str(const hailo_format_order_t &order)
    {
        switch (order)
        {
        case HAILO_FORMAT_ORDER_NHWC:
            return "NHWC";
        case HAILO_FORMAT_ORDER_NHCW:
            return "NHCW";
        case HAILO_FORMAT_ORDER_FCR:
            return "FCR";
        case HAILO_FORMAT_ORDER_F8CR:
            return "F8CR";
        case HAILO_FORMAT_ORDER_NHW:
            return "NHW";
        case HAILO_FORMAT_ORDER_NC:
            return "NC";
        case HAILO_FORMAT_ORDER_BAYER_RGB:
            return "BAYER RGB";
        case HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB:
            return "12 BIT BAYER RGB";
        case HAILO_FORMAT_ORDER_HAILO_NMS:
            return "HAILO NMS";
        case HAILO_FORMAT_ORDER_RGB888:
            return "RGB 888";
        case HAILO_FORMAT_ORDER_NCHW:
            return "NCHW";
        case HAILO_FORMAT_ORDER_YUY2:
            return "YUY2";
        case HAILO_FORMAT_ORDER_NV12:
            return "NV12";
        case HAILO_FORMAT_ORDER_HAILO_YYUV:
            return "YYUV";
        case HAILO_FORMAT_ORDER_NV21:
            return "NV21";
        case HAILO_FORMAT_ORDER_HAILO_YYVU:
            return "YYVU";
        default:
            return "Nan";
        }
    }

    /**
     * Gets the size of each element in bytes from buffer's format.
     *
     * @param[in] format             A ::hailo_format_t object.
     * @return The format's data bytes.
     */
    static constexpr uint8_t get_format_data_bytes(const hailo_format_t &format)
    {
        return get_data_bytes(format.type);
    }

    /**
     * Gets NMS host frame size in bytes by nms info and buffer format.
     *
     * @param[in] nms_info           A ::hailo_nms_info_t object.
     * @param[in] format             A ::hailo_format_t object.
     * @return The NMS host frame size in bytes.
     */
    static constexpr uint32_t get_nms_host_frame_size(const hailo_nms_info_t &nms_info, const hailo_format_t &format)
    {
        return get_nms_host_shape_size(nms_info) * get_format_data_bytes(format);
    }

    /**
     * Gets NMS host frame size in bytes by nms shape and buffer format.
     *
     * @param[in] nms_shape         A ::hailo_nms_shape_t object.
     * @param[in] format            A ::hailo_format_t object.
     * @return The NMS host frame size in bytes.
     */
    static constexpr uint32_t get_nms_host_frame_size(const hailo_nms_shape_t &nms_shape, const hailo_format_t &format)
    {
        return get_nms_host_shape_size(nms_shape) * get_format_data_bytes(format);
    }

    /**
     * Gets NMS hw frame size in bytes by nms info.
     *
     * @param[in] nms_info          A ::hailo_nms_info_t object.
     * @return The NMS hw frame size in bytes.
     */
    static constexpr uint32_t get_nms_hw_frame_size(const hailo_nms_info_t &nms_info)
    {
        const uint32_t size_per_class = static_cast<uint32_t>(sizeof(nms_bbox_counter_t)) +
            nms_info.bbox_size * nms_info.max_bboxes_per_class;
        const uint32_t size_per_chunk = nms_info.number_of_classes * size_per_class;
        // 1 delimiter for an entire frame (since we are reading delimiters directly into the buffer and replacing them)
        return nms_info.bbox_size + (nms_info.chunks_per_frame * size_per_chunk);
    }

    /**
     * Gets frame size in bytes by image shape and format.
     *
     * @param[in] shape         A ::hailo_3d_image_shape_t object.
     * @param[in] format        A ::hailo_format_t object.
     * @return The frame's size in bytes.
     */
    static constexpr uint32_t get_frame_size(const hailo_3d_image_shape_t &shape, const hailo_format_t &format)
    {
        return get_shape_size(shape) * get_format_data_bytes(format);
    }

    /**
     * Gets frame size in bytes by stream info and transformation params.
     *
     * @param[in] stream_info         A ::hailo_stream_info_t object.
     * @param[in] trans_params        A ::hailo_transform_params_t object.
     * @return The frame's size in bytes.
     */
    static constexpr uint32_t get_frame_size(const hailo_stream_info_t &stream_info,
        hailo_transform_params_t trans_params)
    {
        if (HAILO_FORMAT_TYPE_AUTO == trans_params.user_buffer_format.type) {
            trans_params.user_buffer_format.type = stream_info.format.type;
        }

        if (HAILO_FORMAT_ORDER_HAILO_NMS == stream_info.format.order) {
            return get_nms_host_frame_size(stream_info.nms_info, trans_params.user_buffer_format);
        } else {
            auto shape = (HAILO_STREAM_NO_TRANSFORM == trans_params.transform_mode) ? stream_info.hw_shape :
                stream_info.shape;
            return get_frame_size(shape, trans_params.user_buffer_format);
        }
    }

    /**
     * Gets frame size in bytes by stream info and transformation params.
     *
     * @param[in] vstream_info         A ::hailo_vstream_info_t object.
     * @param[in] format               A ::hailo_format_t object.
     * @return The frame's size in bytes.
     */
    static constexpr uint32_t get_frame_size(const hailo_vstream_info_t &vstream_info,
        hailo_format_t format)
    {
        if (HAILO_FORMAT_TYPE_AUTO == format.type) {
            format.type = vstream_info.format.type;
        }

        if (HAILO_FORMAT_ORDER_HAILO_NMS == vstream_info.format.order) {
            return get_nms_host_frame_size(vstream_info.nms_shape, format);
        } else {
            return get_frame_size(vstream_info.shape, format);
        }
    }

    static constexpr bool is_vdma_stream_interface(hailo_stream_interface_t stream_interface)
    {
        return (HAILO_STREAM_INTERFACE_PCIE == stream_interface) || (HAILO_STREAM_INTERFACE_CORE == stream_interface);
    }

    static Expected<hailo_device_id_t> to_device_id(const std::string &device_id);
    static Expected<std::vector<hailo_device_id_t>> to_device_ids_vector(const std::vector<std::string> &device_ids_str);
};

#ifndef HAILO_EMULATOR
constexpr std::chrono::milliseconds DEFAULT_TRANSFER_TIMEOUT(std::chrono::seconds(10));
#else /* ifndef HAILO_EMULATOR */
constexpr std::chrono::milliseconds DEFAULT_TRANSFER_TIMEOUT(std::chrono::seconds(5000));
#endif /* ifndef HAILO_EMULATOR */

constexpr std::chrono::milliseconds HAILO_INFINITE_TIMEOUT(UINT32_MAX);

inline hailo_latency_measurement_flags_t operator|(hailo_latency_measurement_flags_t a,
    hailo_latency_measurement_flags_t b)
{
    return static_cast<hailo_latency_measurement_flags_t>(static_cast<int>(a) | static_cast<int>(b));
}

inline hailo_latency_measurement_flags_t& operator|=(hailo_latency_measurement_flags_t &a,
    hailo_latency_measurement_flags_t b)
{
    a = a | b;
    return a;
}

inline constexpr hailo_format_flags_t operator|(hailo_format_flags_t a, hailo_format_flags_t b)
{
    return static_cast<hailo_format_flags_t>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr hailo_format_flags_t& operator|=(hailo_format_flags_t &a, hailo_format_flags_t b)
{
    a = a | b;
    return a;
}

inline constexpr hailo_vstream_stats_flags_t operator|(hailo_vstream_stats_flags_t a, hailo_vstream_stats_flags_t b)
{
    return static_cast<hailo_vstream_stats_flags_t>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr hailo_vstream_stats_flags_t& operator|=(hailo_vstream_stats_flags_t &a, hailo_vstream_stats_flags_t b)
{
    a = a | b;
    return a;
}

inline constexpr hailo_pipeline_elem_stats_flags_t operator|(hailo_pipeline_elem_stats_flags_t a, hailo_pipeline_elem_stats_flags_t b)
{
    return static_cast<hailo_pipeline_elem_stats_flags_t>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr hailo_pipeline_elem_stats_flags_t& operator|=(hailo_pipeline_elem_stats_flags_t &a, hailo_pipeline_elem_stats_flags_t b)
{
    a = a | b;
    return a;
}

} /* namespace hailort */

#endif /* _HAILO_HAILORT_COMMON_HPP_ */
