/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file softmax_post_process.cpp
 * @brief: Softmax op
 **/

#include "softmax_post_process.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/utils.hpp"

#include "transform/eigen.hpp"

#include <limits>

namespace hailort
{
namespace net_flow
{

// This function is for when trying to perform softmax op for unsupported formats
hailo_status SoftmaxPostProcessOp::execute_not_supported(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
    const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
    {
        (void)inputs;
        (void)outputs;
        LOGGER__ERROR("Softmax post-process not supported with params: input_order {}, input_type {}, output_type {}",
            HailoRTCommon::get_format_order_str(input_metadata.format.order),
            HailoRTCommon::get_format_type_str(input_metadata.format.type),
            HailoRTCommon::get_format_type_str(output_metadata.format.type));
        return HAILO_INVALID_ARGUMENT;
    }

hailo_status SoftmaxPostProcessOp::softmax(float32_t *src, float32_t *dst, size_t num_of_elements)
{
    // Create an Eigen Matrix view for src and dst
    Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, 1>> src_eigen(src, num_of_elements);
    Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, 1>> dst_eigen(dst, num_of_elements);

    src_eigen = src_eigen.array().exp(); // Compute exponentials in place

    assert(!src_eigen.hasNaN()); // Checks if any element in the array is NaN (Not a Number)

    float sum_exp = src_eigen.sum(); // Compute the sum of exponentials

    assert(0.0f != sum_exp); // Checks for division by zero
    dst_eigen = src_eigen / sum_exp; // Perform softmax operation

    return HAILO_SUCCESS;
}

SoftmaxFunction SoftmaxPostProcessOp::m_softmax_function_array[SOFTMAX_NUM_OF_POSSIBLE_FORMAT_ORDERS][SOFTMAX_NUM_OF_POSSIBLE_FORMAT_TYPES][SOFTMAX_NUM_OF_POSSIBLE_FORMAT_TYPES]
{
    // Currently supported on:
    // NC, float_32 to NC, float_32
    // NHWC, float_32 to NHWC, float_32
    {
        {
            // NHWC x AUTO
            // We don't support input_format_type to be auto
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported
        },
        {
            // NHWC x UINT8
            // We don't support input_format_type to be UINT8
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported
        },
        {
            // NHWC x UINT16
            // We don't support input_format_type to be UINT16
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported
        },
        {
            // NHWC x FLOAT32
            SoftmaxPostProcessOp::execute_not_supported, // We don't support output_format_type format of AUTO
            SoftmaxPostProcessOp::execute_not_supported, // We don't support output_format_type format of UINT8
            SoftmaxPostProcessOp::execute_not_supported, // We don't support output_format_type format of UINT16
            SoftmaxPostProcessOp::NHWC_to_NHWC_feature_axis<float32_t, float32_t>
        }
    },
    {
        {
            // NC x AUTO
            // We don't support input_format_type to be auto
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported
        },
        {
            // NC x UINT8
            // We don't support input_format_type to be UINT8
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
        },
        {
            // NC x UINT16
            // We don't support input_format_type to be UINT16
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
            SoftmaxPostProcessOp::execute_not_supported,
        },
        {
            // NC x FLOAT32
            SoftmaxPostProcessOp::execute_not_supported, // We don't support output_format_type format of AUTO
            SoftmaxPostProcessOp::execute_not_supported, // We don't support output_format_type format of UINT8
            SoftmaxPostProcessOp::execute_not_supported, // We don't support output_format_type format of UINT16
            SoftmaxPostProcessOp::NC_to_NC<float32_t, float32_t>,
        }
    }
};

hailo_status SoftmaxPostProcessOp::execute(const std::map<std::string, MemoryView> &inputs,
    std::map<std::string, MemoryView> &outputs)
{
    auto &input_name = inputs.begin()->first;
    auto &output_name = outputs.begin()->first;
    assert(contains(m_op_metadata->inputs_metadata(), input_name));
    auto &input_metadata = m_op_metadata->inputs_metadata().at(input_name);
    assert(contains(m_op_metadata->outputs_metadata(), output_name));
    auto &output_metadata = m_op_metadata->outputs_metadata().at(output_name);

    uint8_t format_index = UINT8_MAX;
    switch (input_metadata.format.order) {
        case HAILO_FORMAT_ORDER_NHWC:
            format_index = 0;
            break;
        case HAILO_FORMAT_ORDER_NC:
            format_index = 1;
            break;
        default:
            LOGGER__ERROR("Softmax post-process received invalid input order {}",
                HailoRTCommon::get_format_order_str(input_metadata.format.order));
            return HAILO_INVALID_ARGUMENT;
    }
    return SoftmaxPostProcessOp::m_softmax_function_array[format_index][input_metadata.format.type][output_metadata.format.type](input_metadata, output_metadata, inputs, outputs);
}

Expected<std::shared_ptr<OpMetadata>> SoftmaxOpMetadata::create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
    const std::unordered_map<std::string, BufferMetaData> &outputs_metadata, const std::string &network_name)
{
    auto op_metadata = std::shared_ptr<SoftmaxOpMetadata>(new (std::nothrow) SoftmaxOpMetadata(inputs_metadata, outputs_metadata, network_name));
    CHECK_AS_EXPECTED(op_metadata != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = op_metadata->validate_params();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return std::shared_ptr<OpMetadata>(std::move(op_metadata));
}

std::string SoftmaxOpMetadata::get_op_description()
{
    auto config_info = fmt::format("{} Op, Name: {}", OpMetadata::get_operation_type_str(m_type), m_name);
    return config_info;
}

hailo_status SoftmaxOpMetadata::validate_params()
{
    assert(m_inputs_metadata.size() == hailort::net_flow::SOFTMAX_NUMBER_OF_SRCS);
    assert(m_outputs_metadata.size() == hailort::net_flow::SOFTMAX_NUMBER_OF_DSTS);

    auto &input_metadata = m_inputs_metadata.begin()->second;
    auto &output_metadata = m_outputs_metadata.begin()->second;

    CHECK(input_metadata.shape.features == output_metadata.shape.features, HAILO_INVALID_OPERATION,
        "Softmax op is supported only when input num of features ({}) is equal to output num of features ({})",
        input_metadata.shape.features, output_metadata.shape.features);
    CHECK(input_metadata.shape.height == output_metadata.shape.height, HAILO_INVALID_OPERATION,
        "Softmax op is supported only when input height ({}) is equal to output height ({})",
        input_metadata.shape.height, output_metadata.shape.height);
    CHECK(input_metadata.shape.width == output_metadata.shape.width, HAILO_INVALID_OPERATION,
        "Softmax op is supported only when input width ({}) is equal to output width ({})",
        input_metadata.shape.width, output_metadata.shape.width);
    return HAILO_SUCCESS;
}

hailo_status SoftmaxOpMetadata::validate_format_info()
{
    auto &input_metadata = m_inputs_metadata.begin()->second;
    auto &output_metadata = m_outputs_metadata.begin()->second;

    CHECK(
        ((input_metadata.format.order == HAILO_FORMAT_ORDER_NHWC) &&  (output_metadata.format.order == HAILO_FORMAT_ORDER_NHWC)) ||
        ((input_metadata.format.order == HAILO_FORMAT_ORDER_NC) && (output_metadata.format.order == HAILO_FORMAT_ORDER_NC)),
        HAILO_INVALID_OPERATION, "Softmax op is not supported for input format order ({}) and output format order ({})",
        HailoRTCommon::get_format_order_str(input_metadata.format.order),
        HailoRTCommon::get_format_order_str(output_metadata.format.order));

    CHECK(input_metadata.format.type == HAILO_FORMAT_TYPE_FLOAT32,
        HAILO_INVALID_OPERATION, "The given input format type {} is not supported, should be {}",
        HailoRTCommon::get_format_type_str(input_metadata.format.type),
        HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_FLOAT32));
    CHECK(output_metadata.format.type == HAILO_FORMAT_TYPE_FLOAT32,
        HAILO_INVALID_OPERATION, "The given output format type {} is not valid, should be {}",
        HailoRTCommon::get_format_type_str(output_metadata.format.type),
        HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_FLOAT32));

    return HAILO_SUCCESS;
}

hailo_format_t SoftmaxOpMetadata::expand_output_format_autos(const hailo_format_t &output_format, const hailo_format_t &input_format)
{
    auto format = output_format;

    // Type should be float32, after de-quantization, and order NHWC or NC in softmax
    if (format.type == HAILO_FORMAT_TYPE_AUTO) {
        format.type = HAILO_FORMAT_TYPE_FLOAT32;
    }
    if (format.order == HAILO_FORMAT_ORDER_AUTO) {
        format.order = HailoRTDefaults::get_default_host_format_order(input_format);
    }
    return format;
}

Expected<hailo_vstream_info_t> SoftmaxOpMetadata::get_output_vstream_info()
{
    CHECK_AS_EXPECTED((m_outputs_metadata.size() == 1), HAILO_INVALID_OPERATION, "{} has more than 1 output", m_name);

    hailo_vstream_info_t vstream_info{};
    strncpy(vstream_info.name, m_outputs_metadata.begin()->first.c_str(), m_outputs_metadata.begin()->first.length() + 1);
    strncpy(vstream_info.network_name, m_network_name.c_str(), m_network_name.length() + 1);
    vstream_info.direction = HAILO_D2H_STREAM;
    vstream_info.format.order = m_outputs_metadata.begin()->second.format.order;
    vstream_info.format.type = m_outputs_metadata.begin()->second.format.type;
    vstream_info.format.flags = HAILO_FORMAT_FLAGS_NONE;

    assert(m_inputs_metadata.size() == 1);
    vstream_info.format = SoftmaxOpMetadata::expand_output_format_autos(vstream_info.format, m_inputs_metadata.begin()->second.format);
    vstream_info.shape = m_outputs_metadata.begin()->second.shape;

    vstream_info.quant_info = m_inputs_metadata.begin()->second.quant_info;

    return vstream_info;
}

Expected<std::shared_ptr<Op>> SoftmaxPostProcessOp::create(std::shared_ptr<SoftmaxOpMetadata> metadata)
{
    auto status = metadata->validate_format_info();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto op = std::shared_ptr<SoftmaxPostProcessOp>(new (std::nothrow) SoftmaxPostProcessOp(metadata));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

} /* namespace net_flow */
} /* namespace hailort */