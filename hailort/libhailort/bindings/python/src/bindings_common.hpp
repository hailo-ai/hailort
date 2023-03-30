/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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

#include "common/logger_macros.hpp"

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

    static std::vector<size_t> get_pybind_shape(const hailo_vstream_info_t& vstream_info, const hailo_format_t &user_format)
    {
        // We are using user_format instead of hw format inside the vstream_info
        const auto shape = vstream_info.shape;
        // TODO: support no transformations (i.e. use stream_info.hw_shape) (SDK-16811)
        switch (user_format.order)
        {
        case HAILO_FORMAT_ORDER_HAILO_NMS:
            return { HailoRTCommon::get_nms_host_shape_size(vstream_info.nms_shape) };
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
