/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "hailo/buffer.hpp"

#include <cmath>
#include <chrono>
#include <string>
#include <vector>


/** hailort namespace */
namespace hailort
{

#define RGB4_ALIGNMENT (4)

/*! Common utility functions and macros that help manage hailort.h structures */
class HAILORTAPI HailoRTCommon final
{
public:
    HailoRTCommon() = delete;
    
    static_assert(sizeof(hailo_bbox_t) / sizeof(uint16_t) == sizeof(hailo_bbox_float32_t) / sizeof(float32_t),
        "Mismatch bbox params size");
    static const uint32_t BBOX_PARAMS = sizeof(hailo_bbox_t) / sizeof(uint16_t);
    static const uint32_t DETECTION_BY_SCORE_SIZE = sizeof(hailo_detection_t);
    static const uint32_t DETECTION_WITH_BYTE_MASK_SIZE = sizeof(hailo_detection_with_byte_mask_t);
    static const uint32_t DETECTION_COUNT_SIZE = sizeof(uint16_t);
    static const uint32_t MAX_DEFUSED_LAYER_COUNT = 9;
    static const size_t HW_DATA_ALIGNMENT = 8;
    static const uint32_t MUX_INFO_COUNT = 32;
    static const uint32_t MAX_MUX_PREDECESSORS = 4;
    static const uint16_t ETH_INPUT_BASE_PORT = 32401;
    static const uint16_t ETH_OUTPUT_BASE_PORT = 32501;
    static const uint32_t MAX_NMS_BURST_SIZE = 65536;
    static const size_t DMA_ABLE_ALIGNMENT_WRITE_HW_LIMITATION = 64;
    static const size_t DMA_ABLE_ALIGNMENT_READ_HW_LIMITATION = 4096;

    /**
     * Deprecated: use get_nms_by_class_host_shape_size instead
     */
    static uint32_t get_nms_host_shape_size(const hailo_nms_info_t &nms_info);

    /**
     * Deprecated: use get_nms_by_class_host_shape_size instead
     */
    static uint32_t get_nms_host_shape_size(const hailo_nms_shape_t &nms_shape);

    /**
     * Gets the NMS host shape size (number of elements) from NMS info.
     *
     * @param[in] nms_info             The NMS info to get shape size from.
     * @return The host shape size (number of elements).
     * @note The size in bytes can be calculated using 
     *  get_nms_by_class_host_frame_size(const hailo_nms_info_t &nms_info, const hailo_format_t &format).
     */
    static constexpr uint32_t get_nms_by_class_host_shape_size(const hailo_nms_info_t &nms_info)
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
    static constexpr uint32_t get_nms_by_class_host_shape_size(const hailo_nms_shape_t &nms_shape)
    {
        const uint32_t max_bboxes_per_class = nms_shape.max_bboxes_per_class;
        // Counter + bboxes
        const uint32_t size_per_class = 1 + (BBOX_PARAMS * max_bboxes_per_class);
        return size_per_class * nms_shape.number_of_classes;
    }

    /**
     * Rounds an integer value up to the next multiple of a specified size.
     *
     * @param[in] num             Original number.
     * @param[in] alignment       Returned number should be aligned to this parameter.
     * @return aligned number
     */
    template<typename T, typename U>
    static constexpr T align_to(T num, U alignment) {
        auto remainder = num % alignment;
        return remainder == 0 ? num : num + (alignment - remainder);
    }

    static void *align_to(void *addr, size_t alignment)
    {
        return reinterpret_cast<void *>(align_to(reinterpret_cast<uintptr_t>(addr), alignment));
    }

    /**
     * Gets the shape size.
     *
     * @param[in] shape             The shape to get size from.
     * @param[in] row_alignment     The size the shape row is aligned to.
     * @return The shape size.
     */
    static constexpr uint32_t get_shape_size(const hailo_3d_image_shape_t &shape, uint32_t row_alignment = 1)
    {
        auto row_size = shape.width * shape.features;
        row_size = align_to(row_size, row_alignment);
        return shape.height * row_size;
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
        case HAILO_FORMAT_TYPE_AUTO:
            return "AUTO";
        default:
            return "Nan";
        }
    }

    /**
     * Gets a string reprenestation of the given power mode.
     *
     * @param[in] mode             A ::hailo_power_mode_t object.
     * @return The string representation of the power mode.
     */
    static std::string get_power_mode_str(const hailo_power_mode_t &mode)
    {
        switch (mode)
        {
        case HAILO_POWER_MODE_PERFORMANCE:
            return "PERFORMANCE";
        case HAILO_POWER_MODE_ULTRA_PERFORMANCE:
            return "ULTRA_PERFORMANCE";
        default:
            return "Nan";
        }
    }

    /**
     * Gets a string reprenestation of the given latency measurement flags.
     *
     * @param[in] flags            A ::hailo_latency_measurement_flags_t object.
     * @return The string representation of the latency measurement flags.
     */
    static std::string get_latency_measurement_str(const hailo_latency_measurement_flags_t &flags)
    {
        switch (flags)
        {
        case HAILO_LATENCY_NONE:
            return "NONE";
        case HAILO_LATENCY_MEASURE:
            return "MEASURE";
        case HAILO_LATENCY_CLEAR_AFTER_GET:
            return "CLEAR_AFTER_GET";
        default:
            return "Nan";
        }
    }

    /**
     * Gets a string reprenestation of the given scheduling algorithm.
     *
     * @param[in] scheduling_algo  A ::hailo_scheduling_algorithm_t object.
     * @return The string representation of the scheduling algorithm.
     */
    static std::string get_scheduling_algorithm_str(const hailo_scheduling_algorithm_t &scheduling_algo)
    {
        switch (scheduling_algo)
        {
        case HAILO_SCHEDULING_ALGORITHM_NONE:
            return "NONE";
        case HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN:
            return "ROUND_ROBIN";
        default:
            return "Nan";
        }
    }

    /**
     * Gets a string reprenestation of the given device architecture.
     *
     * @param[in] arch    A ::hailo_device_architecture_t object.
     * @return The string representation of the device architecture.
     */
    static std::string get_device_arch_str(const hailo_device_architecture_t &arch)
    {
        switch (arch)
        {
        case HAILO_ARCH_HAILO8_A0:
            return "HAILO8_A0";
        case HAILO_ARCH_HAILO8:
            return "HAILO8";
        case HAILO_ARCH_HAILO8L:
            return "HAILO8L";
        case HAILO_ARCH_HAILO15H:
            return "HAILO15H";
        case HAILO_ARCH_HAILO15L:
            return "HAILO15L";
        case HAILO_ARCH_HAILO15M:
            return "HAILO15M";
        case HAILO_ARCH_HAILO10H:
            return "HAILO10H";
        case HAILO_ARCH_MARS:
            return "MARS";
        default:
            return "UNKNOWN ARCHITECTURE";
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
        case HAILO_FORMAT_ORDER_RGB4:
            return "RGB4";
        case HAILO_FORMAT_ORDER_I420:
            return "I420";
        case HAILO_FORMAT_ORDER_HAILO_YYYYUV:
            return "YYYYUV";
        case HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK:
            return "HAILO NMS WITH BYTE MASK";
        case HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP:
            return "HAILO NMS ON CHIP";
        case HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS:
            return "HAILO NMS BY CLASS";
        case HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE:
            return "HAILO NMS BY SCORE";
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
    static constexpr uint32_t get_nms_by_class_host_frame_size(const hailo_nms_info_t &nms_info, const hailo_format_t &format)
    {
        return get_nms_by_class_host_shape_size(nms_info) * get_format_data_bytes(format);
    }

    /**
     * Gets NMS host frame size in bytes by nms shape and buffer format.
     *
     * @param[in] nms_shape         A ::hailo_nms_shape_t object.
     * @param[in] format            A ::hailo_format_t object.
     * @return The NMS host frame size in bytes.
     */
    static uint32_t get_nms_host_frame_size(const hailo_nms_shape_t &nms_shape, const hailo_format_t &format);

    /**
     * Gets `HAILO_NMS_WITH_BYTE_MASK` host frame size in bytes by nms_shape.
     *
     * @param[in] nms_shape             The NMS shape to get size from.
     * @return The HAILO_NMS_WITH_BYTE_MASK host frame size.
     */
    static constexpr uint32_t get_nms_with_byte_mask_host_frame_size(const hailo_nms_shape_t &nms_shape)
    {
        auto max_detections_size = nms_shape.max_bboxes_total * DETECTION_WITH_BYTE_MASK_SIZE;
        return (DETECTION_COUNT_SIZE + max_detections_size + nms_shape.max_accumulated_mask_size);
    }

    /**
     * Gets `HAILO_NMS_BY_SCORE` host frame size in bytes by nms_shape.
     *
     * @param[in] nms_shape             The NMS shape to get size from.
     * @return The HAILO_NMS_BY_SCORE host frame size.
     */
    static constexpr uint32_t get_nms_by_score_host_frame_size(const hailo_nms_shape_t &nms_shape)
    {
        return (DETECTION_COUNT_SIZE + nms_shape.max_bboxes_total * DETECTION_BY_SCORE_SIZE);
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
            nms_info.bbox_size * std::max(nms_info.burst_size, nms_info.max_bboxes_per_class);
        const uint32_t size_per_chunk = nms_info.number_of_classes * size_per_class;
        // Extra Burst size for frame (since may be reading bursts directly into the buffer and replacing them)
        const uint32_t size_for_extra_burst = nms_info.bbox_size * nms_info.burst_size;
        return (nms_info.chunks_per_frame * size_per_chunk) + size_for_extra_burst;
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
        uint32_t row_alignment = 1;
        if (format.order == HAILO_FORMAT_ORDER_RGB4) {
            row_alignment = RGB4_ALIGNMENT;
        }
        return get_shape_size(shape, row_alignment) * get_format_data_bytes(format);
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

        if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == stream_info.format.order) {
            return get_nms_by_class_host_frame_size(stream_info.nms_info, trans_params.user_buffer_format);
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
    static constexpr uint32_t get_frame_size(const hailo_vstream_info_t &vstream_info, hailo_format_t format)
    {
        if (HAILO_FORMAT_TYPE_AUTO == format.type) {
            format.type = vstream_info.format.type;
        }

        if (HAILO_FORMAT_ORDER_AUTO == format.order) {
            format.order = vstream_info.format.order;
        }

        if (HailoRTCommon::is_nms(vstream_info)) {
            return get_nms_host_frame_size(vstream_info.nms_shape, format);
        } else {
            return get_frame_size(vstream_info.shape, format);
        }
    }

    /**
     * Gets periph frame size in bytes by image shape and format - periph frame size is amount of bytes transferred
     * through peripherals which must be aligned to HW_DATA_ALIGNMENT (8). Note: this function always aligns to next largest HW_DATA_ALIGNMENT 
     *
     * @param[in] shape         A ::hailo_3d_image_shape_t object.
     * @param[in] format        A ::hailo_format_t object.
     * @return The periph frame's size in bytes.
     */
    static constexpr uint32_t get_periph_frame_size(const hailo_3d_image_shape_t &shape, const hailo_format_t &format)
    {
        return align_to(get_frame_size(shape, format), static_cast<uint32_t>(HW_DATA_ALIGNMENT));
    }

    static constexpr bool is_vdma_stream_interface(hailo_stream_interface_t stream_interface)
    {
        return (HAILO_STREAM_INTERFACE_PCIE == stream_interface) || (HAILO_STREAM_INTERFACE_INTEGRATED == stream_interface);
    }

    static constexpr bool is_nms(const hailo_vstream_info_t &vstream_info)
    {
        return is_nms(vstream_info.format.order);
    }

    static constexpr bool is_nms(const hailo_stream_info_t &stream_info)
    {
        return is_nms(stream_info.format.order);
    }

    static constexpr bool is_nms(const hailo_format_order_t &order)
    {
        return (is_nms_by_class(order) || is_nms_by_score(order));
    }

    static constexpr bool is_nms_by_class(const hailo_format_order_t &order)
    {
        return (HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS == order);
    }

    static constexpr bool is_nms_by_score(const hailo_format_order_t &order)
    {
        return ((HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK == order) || (HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE == order));
    }

    // TODO HRT-10073: change to supported features list
    static bool is_hailo1x_device_type(const hailo_device_architecture_t dev_arch)
    {
        // Compare with HAILO1X device archs
        return (HAILO_ARCH_HAILO15H == dev_arch) || (HAILO_ARCH_HAILO15M == dev_arch) || (HAILO_ARCH_HAILO15L == dev_arch) ||
            (HAILO_ARCH_HAILO10H == dev_arch);
    }

    static Expected<hailo_device_id_t> to_device_id(const std::string &device_id);
    static Expected<std::vector<hailo_device_id_t>> to_device_ids_vector(const std::vector<std::string> &device_ids_str);
    static Expected<hailo_pix_buffer_t> as_hailo_pix_buffer(MemoryView memory_view, hailo_format_order_t order);
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

inline constexpr hailo_format_flags_t operator&(hailo_format_flags_t a, hailo_format_flags_t b)
{
    return static_cast<hailo_format_flags_t>(static_cast<int>(a) & static_cast<int>(b));
}

inline constexpr hailo_format_flags_t& operator&=(hailo_format_flags_t &a, hailo_format_flags_t b)
{
    a = a & b;
    return a;
}

inline constexpr hailo_format_flags_t operator~(hailo_format_flags_t a)
{
    return static_cast<hailo_format_flags_t>(~(static_cast<int>(a)));
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

inline constexpr hailo_stream_flags_t operator|(hailo_stream_flags_t a, hailo_stream_flags_t b)
{
    return static_cast<hailo_stream_flags_t>(static_cast<int>(a) | static_cast<int>(b));
}

inline constexpr hailo_stream_flags_t& operator|=(hailo_stream_flags_t &a, hailo_stream_flags_t b)
{
    a = a | b;
    return a;
}

inline bool is_bit_set(uint32_t num, uint8_t i)
{
    return (1 == ((num >> i) & 1));
}

} /* namespace hailort */

#endif /* _HAILO_HAILORT_COMMON_HPP_ */
