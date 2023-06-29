/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file argmax_post_process.cpp
 * @brief: Argsmax op
 **/

#include "argmax_post_process.hpp"
#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "common/utils.hpp"

#include <limits>


namespace hailort
{
namespace net_flow
{

// Source https://stackoverflow.com/questions/3793838/which-is-the-first-integer-that-an-ieee-754-float-is-incapable-of-representing-e
#define FLOAT_LAST_CONSECUTIVE_REPRESENTABLE_INT (1 << std::numeric_limits<float32_t>::digits)

hailo_status ArgmaxPostProcessOp::execute_not_supported(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
    const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
    {
        (void)inputs;
        (void)outputs;
        LOGGER__ERROR("Argmax post-process not supported with params: input_order {}, input_type {}, output_type {}",
            HailoRTCommon::get_format_order_str(input_metadata.format.order),
            HailoRTCommon::get_format_type_str(input_metadata.format.type),
            HailoRTCommon::get_format_type_str(output_metadata.format.type));
        return HAILO_INVALID_ARGUMENT;
    }

ArgmaxFunction ArgmaxPostProcessOp::m_argmax_function_array[ARGMAX_NUM_OF_POSSIBLE_FORMAT_ORDERS][ARGMAX_NUM_OF_POSSIBLE_FORMAT_TYPES][ARGMAX_NUM_OF_POSSIBLE_FORMAT_TYPES]
{
    {
        {
            // NHCW x AUTO
            // We don't support input_format_type to be auto
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported
        },
        {
            // NHCW x UINT8
            ArgmaxPostProcessOp::execute_not_supported, // We don't support output_format_type to be auto
            ArgmaxPostProcessOp::NHCW_to_NHW_feature_axis<uint8_t, uint8_t>,
            ArgmaxPostProcessOp::NHCW_to_NHW_feature_axis<uint8_t, uint16_t>,
            ArgmaxPostProcessOp::NHCW_to_NHW_feature_axis<uint8_t, float32_t>
        },
        {
            // NHCW x UINT16
            ArgmaxPostProcessOp::execute_not_supported, // We don't support output_format_type to be auto
            ArgmaxPostProcessOp::NHCW_to_NHW_feature_axis<uint16_t, uint8_t>,
            ArgmaxPostProcessOp::NHCW_to_NHW_feature_axis<uint16_t, uint16_t>,
            ArgmaxPostProcessOp::NHCW_to_NHW_feature_axis<uint16_t, float32_t>
        },
        {
            // NHCW x FLOAT32
            // We don't support input_format_type to be float32
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported
        }
    },
    {
        {
            // NHWC x AUTO
            // We don't support input_format_type to be auto
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported
        },
        {
            // NHWC x UINT8
            ArgmaxPostProcessOp::execute_not_supported, // We don't support output_format_type to be auto
            ArgmaxPostProcessOp::NHWC_to_NHW_feature_axis<uint8_t, uint8_t>,
            ArgmaxPostProcessOp::NHWC_to_NHW_feature_axis<uint8_t, uint16_t>,
            ArgmaxPostProcessOp::NHWC_to_NHW_feature_axis<uint8_t, float32_t>
        },
        {
            // NHWC x UINT16
            ArgmaxPostProcessOp::execute_not_supported, // We don't support output_format_type to be auto
            ArgmaxPostProcessOp::NHWC_to_NHW_feature_axis<uint16_t, uint8_t>,
            ArgmaxPostProcessOp::NHWC_to_NHW_feature_axis<uint16_t, uint16_t>,
            ArgmaxPostProcessOp::NHWC_to_NHW_feature_axis<uint16_t, float32_t>,
        },
        {
            // NHWC x FLOAT32
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported
        }
    },
    {
        {
            // NC x AUTO
            // We don't support input_format_type to be auto
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported
        },
        {
            // NC x UINT8
            ArgmaxPostProcessOp::execute_not_supported, // We don't support output_format_type to be auto
            ArgmaxPostProcessOp::NC_to_N<uint8_t, uint8_t>,
            ArgmaxPostProcessOp::NC_to_N<uint8_t, uint16_t>,
            ArgmaxPostProcessOp::NC_to_N<uint8_t, float32_t>,
        },
        {
            // NC x UINT16
            ArgmaxPostProcessOp::execute_not_supported, // We don't support output_format_type to be auto
            ArgmaxPostProcessOp::NC_to_N<uint16_t, uint8_t>,
            ArgmaxPostProcessOp::NC_to_N<uint16_t, uint16_t>,
            ArgmaxPostProcessOp::NC_to_N<uint16_t, float32_t>,
        },
        {
            // NC x FLOAT32
            // We don't support input_format_type to be float32
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported,
            ArgmaxPostProcessOp::execute_not_supported
        }
    }
};

hailo_status ArgmaxPostProcessOp::execute(const std::map<std::string, MemoryView> &inputs,
    std::map<std::string, MemoryView> &outputs)
{
    auto &input_name = inputs.begin()->first;
    auto &output_name = outputs.begin()->first;
    auto &input_metadata = m_inputs_metadata[input_name];
    auto &output_metadata = m_outputs_metadata[output_name];

    uint8_t format_index = UINT8_MAX;
    switch (input_metadata.format.order) {
        case HAILO_FORMAT_ORDER_NHCW:
            format_index = 0;
            break;
        case HAILO_FORMAT_ORDER_NHWC:
            format_index = 1;
            break;
        case HAILO_FORMAT_ORDER_NC:
            format_index = 2;
            break;
        default:
            LOGGER__ERROR("Argmax post-process received invalid input order {}",
                HailoRTCommon::get_format_order_str(input_metadata.format.order));
            return HAILO_INVALID_ARGUMENT;
    }
    return ArgmaxPostProcessOp::m_argmax_function_array[format_index][input_metadata.format.type][output_metadata.format.type](input_metadata, output_metadata, inputs, outputs);
}

std::string ArgmaxPostProcessOp::get_op_description()
{
    auto config_info = fmt::format("ArgmaxPostProcess Op, Name: {}", m_name);
    return config_info;
}

hailo_status ArgmaxPostProcessOp::validate_metadata()
{
    assert(m_inputs_metadata.size() == hailort::net_flow::ARGMAX_NUMBER_OF_SRCS);
    assert(m_outputs_metadata.size() == hailort::net_flow::ARGMAX_NUMBER_OF_DSTS);

    auto &input_metadata = m_inputs_metadata.begin()->second;
    auto &output_metadata = m_outputs_metadata.begin()->second;

    CHECK((
        ((output_metadata.format.type == HAILO_FORMAT_TYPE_UINT8) && (input_metadata.shape.features <= std::numeric_limits<uint8_t>::max())) ||
        ((output_metadata.format.type == HAILO_FORMAT_TYPE_UINT16) && (input_metadata.shape.features <= std::numeric_limits<uint16_t>::max())) ||
        ((output_metadata.format.type == HAILO_FORMAT_TYPE_FLOAT32) && (input_metadata.shape.features <= FLOAT_LAST_CONSECUTIVE_REPRESENTABLE_INT))),
        HAILO_INVALID_OPERATION, "Dst format type {} can't represent possible range {} for Argmax op",
        HailoRTCommon::get_format_type_str(output_metadata.format.type), input_metadata.shape.features);
    CHECK(
        ((input_metadata.format.order == HAILO_FORMAT_ORDER_NHCW) &&  (output_metadata.format.order == HAILO_FORMAT_ORDER_NHW)) ||
        ((input_metadata.format.order == HAILO_FORMAT_ORDER_NHWC) && (output_metadata.format.order == HAILO_FORMAT_ORDER_NHW)) ||
        ((input_metadata.format.order == HAILO_FORMAT_ORDER_NC) && (output_metadata.format.order == HAILO_FORMAT_ORDER_NC)),
        HAILO_INVALID_OPERATION, "Argmax op is not supported for src format order ({}) and dst format order ({})",
        HailoRTCommon::get_format_order_str(input_metadata.format.order),
        HailoRTCommon::get_format_order_str(output_metadata.format.order));

    CHECK(output_metadata.shape.features == hailort::net_flow::ARGMAX_OUTPUT_FEATURES_SIZE, HAILO_INVALID_OPERATION,
        "Dst features ({}) must be 1 on Argmax op", output_metadata.shape.features);
    CHECK(input_metadata.shape.height == output_metadata.shape.height, HAILO_INVALID_OPERATION,
        "Argmax op is supported only when src height ({}) is equal to dst height ({})",
        input_metadata.shape.height, output_metadata.shape.height);
    CHECK(input_metadata.shape.width == output_metadata.shape.width, HAILO_INVALID_OPERATION,
        "Argmax op is supported only when src width ({}) is equal to dst width ({})",
        input_metadata.shape.width, output_metadata.shape.width);
    CHECK((
        (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT8) || (input_metadata.format.type == HAILO_FORMAT_TYPE_UINT16)),
        HAILO_INVALID_OPERATION, "Src format type {} is not valid. Must be either {} or {}",
        HailoRTCommon::get_format_type_str(input_metadata.format.type), HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT8),
        HailoRTCommon::get_format_type_str(HAILO_FORMAT_TYPE_UINT16));

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<Op>> ArgmaxPostProcessOp::create(const std::map<std::string, BufferMetaData> &inputs_metadata,
    std::map<std::string, BufferMetaData> &outputs_metadata)
{
    auto op = std::shared_ptr<ArgmaxPostProcessOp>(new (std::nothrow) ArgmaxPostProcessOp(inputs_metadata, outputs_metadata));
    CHECK_AS_EXPECTED(op != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::shared_ptr<Op>(std::move(op));
}

} /* namespace net_flow */
} /* namespace hailort */
