/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file softmax_post_process.cpp
 * @brief: Softmax op
 **/

#include "softmax_post_process.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "common/utils.hpp"

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
    auto &input_metadata = m_inputs_metadata[input_name];
    auto &output_metadata = m_outputs_metadata[output_name];

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

std::string SoftmaxPostProcessOp::get_op_description()
{
    auto config_info = fmt::format("SoftmaxPostProcess Op, Name: {}", m_name);
    return config_info;
}

hailo_status SoftmaxPostProcessOp::validate_metadata()
{
    assert(m_inputs_metadata.size() == hailort::net_flow::SOFTMAX_NUMBER_OF_SRCS);
    assert(m_outputs_metadata.size() == hailort::net_flow::SOFTMAX_NUMBER_OF_DSTS);

    auto &input_metadata = m_inputs_metadata.begin()->second;
    auto &output_metadata = m_outputs_metadata.begin()->second;

    CHECK(
        ((input_metadata.format.flags & HAILO_FORMAT_FLAGS_QUANTIZED) == 0) && ((output_metadata.format.flags & HAILO_FORMAT_FLAGS_QUANTIZED) == 0),
        HAILO_INVALID_OPERATION, "Softmax op is supported only on dequantized data");

    CHECK(
        ((input_metadata.format.order == HAILO_FORMAT_ORDER_NHWC) &&  (output_metadata.format.order == HAILO_FORMAT_ORDER_NHWC)) ||
        ((input_metadata.format.order == HAILO_FORMAT_ORDER_NC) && (output_metadata.format.order == HAILO_FORMAT_ORDER_NC)),
        HAILO_INVALID_OPERATION, "Softmax op is not supported for src format order ({}) and dst format order ({})",
        HailoRTCommon::get_format_order_str(input_metadata.format.order),
        HailoRTCommon::get_format_order_str(output_metadata.format.order));

    CHECK(input_metadata.shape.features == output_metadata.shape.features, HAILO_INVALID_OPERATION,
        "Softmax op is supported only when src num of features ({}) is equal to dst num of features ({})",
        input_metadata.shape.features, output_metadata.shape.features);
    CHECK(input_metadata.shape.height == output_metadata.shape.height, HAILO_INVALID_OPERATION,
        "Softmax op is supported only when src height ({}) is equal to dst height ({})",
        input_metadata.shape.height, output_metadata.shape.height);
    CHECK(input_metadata.shape.width == output_metadata.shape.width, HAILO_INVALID_OPERATION,
        "Softmax op is supported only when src width ({}) is equal to dst width ({})",
        input_metadata.shape.width, output_metadata.shape.width);
    CHECK(input_metadata.format.type == HAILO_FORMAT_TYPE_FLOAT32,
        HAILO_INVALID_OPERATION, "Src format type {} is not valid. Must be {}",
        HailoRTCommon::get_format_type_str(input_metadata.format.type),
        HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_FLOAT32));
    CHECK(output_metadata.format.type == HAILO_FORMAT_TYPE_FLOAT32,
        HAILO_INVALID_OPERATION, "Dst format type {} is not valid. Must be {}",
        HailoRTCommon::get_format_type_str(output_metadata.format.type),
        HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_FLOAT32));
    CHECK(!(HAILO_FORMAT_FLAGS_HOST_ARGMAX & output_metadata.format.flags), HAILO_INVALID_ARGUMENT, "Output {} is marked as argmax, which is not supported for this model.",
        m_outputs_metadata.begin()->first);
    CHECK(!(HAILO_FORMAT_FLAGS_QUANTIZED & output_metadata.format.flags), HAILO_INVALID_ARGUMENT, "Output {} is marked as quantized, which is not supported for this model.",
        m_outputs_metadata.begin()->first);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<Op>> SoftmaxPostProcessOp::create(const std::map<std::string, BufferMetaData> &inputs_metadata,
    std::map<std::string, BufferMetaData> &outputs_metadata)
{
    auto op = std::shared_ptr<SoftmaxPostProcessOp>(new (std::nothrow) SoftmaxPostProcessOp(inputs_metadata, outputs_metadata));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

} /* namespace net_flow */
} /* namespace hailort */