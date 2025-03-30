/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file quantization_api.cpp
 * @brief Quantization python bindings functions
 **/

#include "hailo/quantization.hpp"

#include "quantization_api.hpp"
#include "bindings_common.hpp"

#include <iostream>

namespace hailort
{

void QuantizationBindings::dequantize_output_buffer_from_uint8(py::array src_buffer, py::array dst_buffer,
    const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::dequantize_output_buffer<uint8_t, uint8_t>(static_cast<uint8_t*>(src_buffer.mutable_data()),
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::dequantize_output_buffer<uint16_t, uint8_t>(static_cast<uint8_t*>(src_buffer.mutable_data()),
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer<float32_t, uint8_t>(static_cast<uint8_t*>(src_buffer.mutable_data()),
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            std::cerr << "Output quantization isn't supported from src format type uint8 to dst format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::dequantize_output_buffer_from_uint16(py::array src_buffer, py::array dst_buffer,
    const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::dequantize_output_buffer<uint16_t, uint16_t>(static_cast<uint16_t*>(src_buffer.mutable_data()),
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer<float32_t, uint16_t>(static_cast<uint16_t*>(src_buffer.mutable_data()),
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            std::cerr << "Output quantization isn't supported from src dormat type uint16 to dst format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::dequantize_output_buffer_from_float32(py::array src_buffer, py::array dst_buffer,
    const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer<float32_t, float32_t>(static_cast<float32_t*>(src_buffer.mutable_data()),
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            std::cerr << "Output quantization isn't supported from src format type float32 to dst format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype); 
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::dequantize_output_buffer_from_uint8_in_place(py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::dequantize_output_buffer_in_place<uint8_t, uint8_t>(
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::dequantize_output_buffer_in_place<uint16_t, uint8_t>(
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer_in_place<float32_t, uint8_t>(
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            std::cerr << "Output quantization isn't supported from src format type uint8 to dst format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::dequantize_output_buffer_from_uint16_in_place(py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::dequantize_output_buffer_in_place<uint16_t, uint16_t>(
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer_in_place<float32_t, uint16_t>(
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            std::cerr << "Output quantization isn't supported from src dormat type uint16 to dst format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::dequantize_output_buffer_from_float32_in_place(py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer_in_place<float32_t, float32_t>(
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            std::cerr << "Output quantization isn't supported from src format type float32 to dst format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::dequantize_output_buffer_in_place(py::array dst_buffer, const hailo_format_type_t &src_dtype,
    const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (src_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            QuantizationBindings::dequantize_output_buffer_from_uint8_in_place(dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            QuantizationBindings::dequantize_output_buffer_from_uint16_in_place(dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            QuantizationBindings::dequantize_output_buffer_from_float32_in_place(dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        default:
            std::cerr << "Unsupported src format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::dequantize_output_buffer(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &src_dtype,
    const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (src_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            QuantizationBindings::dequantize_output_buffer_from_uint8(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            QuantizationBindings::dequantize_output_buffer_from_uint16(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            QuantizationBindings::dequantize_output_buffer_from_float32(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        default:
            std::cerr << "Unsupported src format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::quantize_input_buffer_from_uint8(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::quantize_input_buffer<uint8_t, uint8_t>(static_cast<uint8_t*>(src_buffer.mutable_data()),
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            std::cerr << "Input quantization isn't supported from src format type uint8 to dst format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::quantize_input_buffer_from_uint16(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::quantize_input_buffer<uint16_t, uint8_t>(static_cast<uint16_t*>(src_buffer.mutable_data()),
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::quantize_input_buffer<uint16_t, uint16_t>(static_cast<uint16_t*>(src_buffer.mutable_data()),
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            std::cerr << "Input quantization isn't supported from src format type uint16 to dst format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::quantize_input_buffer_from_float32(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::quantize_input_buffer<float32_t, uint8_t>(static_cast<float32_t*>(src_buffer.mutable_data()),
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::quantize_input_buffer<float32_t, uint16_t>(static_cast<float32_t*>(src_buffer.mutable_data()),
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            std::cerr << "Input quantization isn't supported from src format type float32 to dst format type = " <<
                HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void QuantizationBindings::quantize_input_buffer(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &src_dtype,
    const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (src_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            QuantizationBindings::quantize_input_buffer_from_uint8(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            QuantizationBindings::quantize_input_buffer_from_uint16(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            QuantizationBindings::quantize_input_buffer_from_float32(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        default:
            std::cerr << "Input quantization isn't supported for src format type = " << HailoRTBindingsCommon::convert_format_type_to_string(dst_dtype);
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

bool QuantizationBindings::is_qp_valid(const hailo_quant_info_t &quant_info)
{
    return Quantization::is_qp_valid(quant_info);
}

} /* namespace hailort */
