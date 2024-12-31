/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file bindings_common.hpp
 * @brief Common funcs and defs for Python bindings
 **/

#ifndef _BINDINGS_COMMON_HPP_
#define _BINDINGS_COMMON_HPP_

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/network_group.hpp"

#include "utils.hpp"

#include <pybind11/numpy.h>


namespace hailort
{
class HailoRTBindingsCommon
{
public:
    static std::string convert_format_type_to_string(const hailo_format_type_t &type)
    {
        switch (type) {
        case HAILO_FORMAT_TYPE_UINT8:
            return "uint8";
        case HAILO_FORMAT_TYPE_UINT16:
            return "uint16";
        case HAILO_FORMAT_TYPE_FLOAT32:
            return "float32";
        default:
            throw HailoRTStatusException("Invalid format type.");
        }
    }

    static std::vector<size_t> get_pybind_shape(
        const hailo_3d_image_shape_t &shape,
        const hailo_nms_shape_t &nms_shape,
        const hailo_format_t &user_format)
    {
        switch (user_format.order)
        {
        case HAILO_FORMAT_ORDER_HAILO_NMS:
        case HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS:
            return { HailoRTCommon::get_nms_host_shape_size(nms_shape) };
        case HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE:
            throw HailoRTStatusException("Format order HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE is not supported in python API.");
        case HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK:
            return {HailoRTCommon::get_nms_host_frame_size(nms_shape, user_format) / HailoRTCommon::get_format_data_bytes(user_format)};
        case HAILO_FORMAT_ORDER_NC:
            return {shape.features};
        case HAILO_FORMAT_ORDER_NHW:
            return {shape.height, shape.width};
        default:
            return {shape.height, shape.width, shape.features};
        }
    }

    static py::dtype get_dtype(const hailo_format_type_t &type)
    {
        return py::dtype(HailoRTBindingsCommon::convert_format_type_to_string(type));
    }
};

} /* namespace hailort */

#endif /* _BINDINGS_COMMON_HPP_ */
