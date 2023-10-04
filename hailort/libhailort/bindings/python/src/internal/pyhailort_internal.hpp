/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pyhailort_internal.hpp
 * @brief Defines binding of internal functions over Python.
 **/

#ifndef _PYHAILORT_INTERNAL_
#define _PYHAILORT_INTERNAL_

#include "hef/hef_internal.hpp"

#include "hef_api.hpp"
#include "utils.hpp"
#include "utils.h"
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>
#include <vector>


namespace hailort
{

class PyhailortInternal {
public:
    static py::array get_yolov5_post_process_expected_buffer();
    static void demux_output_buffer(py::bytes src, const hailo_format_t &src_format, const hailo_3d_image_shape_t &src_shape,
        std::map<std::string, py::array> dst_buffers, const LayerInfo &mux_layer_info);
    static void transform_input_buffer(py::array src, const hailo_format_t &src_format, const hailo_3d_image_shape_t &src_shape,
        uintptr_t dst, size_t dst_size, const hailo_format_t &dst_format, const hailo_3d_image_shape_t &dst_shape,
        const std::vector<hailo_quant_info_t> &dst_quant_infos);
    static void transform_output_buffer(py::bytes src, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &src_shape, py::array dst, const hailo_format_t &dst_format,
        const hailo_3d_image_shape_t &dst_shape, const std::vector<hailo_quant_info_t> &dst_quant_infos);
    static void transform_output_buffer_nms(py::bytes src, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &src_shape, py::array dst, const hailo_format_t &dst_format,
        const hailo_3d_image_shape_t &dst_shape, const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info);
    static bool is_input_transformation_required(const hailo_3d_image_shape_t &src_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &quant_infos);
    static bool is_output_transformation_required(const hailo_3d_image_shape_t &src_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &quant_infos);
    static py::list get_all_layers_info(const HefWrapper &hef, const std::string &net_group_name);
};

} /* namespace hailort */

#endif /* _PYHAILORT_INTERNAL_ */