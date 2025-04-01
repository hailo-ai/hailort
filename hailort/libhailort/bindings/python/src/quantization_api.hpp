/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file quantization_api.hpp
 * @brief Quantization python bindings functions
 **/

#ifndef _HAILO_QUANTIZATION_API_HPP_
#define _HAILO_QUANTIZATION_API_HPP_

#include "hailo/hailort.h"

#include "utils.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>


namespace hailort
{

class QuantizationBindings
{
public:
    static void quantize_input_buffer(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &src_dtype,
        const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info);
    static void dequantize_output_buffer_in_place(py::array dst_buffer, const hailo_format_type_t &src_dtype,
        const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info);
    static void dequantize_output_buffer(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &src_dtype,
        const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info);
    static bool is_qp_valid(const hailo_quant_info_t &quant_info);
private:
    static void dequantize_output_buffer_from_uint8(py::array src_buffer, py::array dst_buffer,
        const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info);
    static void dequantize_output_buffer_from_uint16(py::array src_buffer, py::array dst_buffer,
        const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info);
    static void dequantize_output_buffer_from_float32(py::array src_buffer, py::array dst_buffer,
        const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info);

    static void dequantize_output_buffer_from_uint8_in_place(py::array dst_buffer, const hailo_format_type_t &dst_dtype,
        uint32_t shape_size, const hailo_quant_info_t &quant_info);
    static void dequantize_output_buffer_from_uint16_in_place(py::array dst_buffer, const hailo_format_type_t &dst_dtype,
        uint32_t shape_size, const hailo_quant_info_t &quant_info);
    static void dequantize_output_buffer_from_float32_in_place(py::array dst_buffer, const hailo_format_type_t &dst_dtype,
        uint32_t shape_size, const hailo_quant_info_t &quant_info);

    static void quantize_input_buffer_from_uint8(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
        uint32_t shape_size, const hailo_quant_info_t &quant_info);
    static void quantize_input_buffer_from_uint16(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
        uint32_t shape_size, const hailo_quant_info_t &quant_info);
    static void quantize_input_buffer_from_float32(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
        uint32_t shape_size, const hailo_quant_info_t &quant_info);
};

} /* namespace hailort */

#endif /* _HAILO_QUANTIZATION_API_HPP_ */
