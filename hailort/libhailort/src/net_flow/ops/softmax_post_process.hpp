/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file softmax_post_process.hpp
 * @brief: Softmax op perform softmax op as described: https://www.tensorflow.org/api_docs/python/tf/nn/softmax
 * A few notes:
 *  - Support only on features axis 
 *  - Support only on NHWC and NC input data order
 **/

#ifndef _HAILO_SOFTMAX_POST_PROCESS_HPP_
#define _HAILO_SOFTMAX_POST_PROCESS_HPP_

#include "hailo/hailort.h"
#include "net_flow/ops/op.hpp"
#include "common/utils.hpp"
#include "hailo/quantization.hpp"

#include <iostream>

namespace hailort
{
namespace net_flow
{

#define SOFTMAX_NUM_OF_POSSIBLE_FORMAT_ORDERS (2) // NHWC, NC
#define SOFTMAX_NUM_OF_POSSIBLE_FORMAT_TYPES (4) // Auto, UINT8, UINT16, FLOAT32

constexpr std::size_t SOFTMAX_NUMBER_OF_SRCS {1};
constexpr std::size_t SOFTMAX_NUMBER_OF_DSTS {1};

typedef hailo_status (*SoftmaxFunction)(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
    const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs);

class SoftmaxPostProcessOp : public Op
{

private:
    SoftmaxPostProcessOp(const std::map<std::string, BufferMetaData> &inputs_metadata,
                         const std::map<std::string, BufferMetaData> &outputs_metadata)
        : Op(inputs_metadata, outputs_metadata, "Softmax-Post-Process")
    {}

    template<typename src_type = float32_t, typename dst_type = float32_t>
    static hailo_status NHWC_to_NHWC_feature_axis(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
        const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
    {
        auto src_ptr = (dst_type*)inputs.begin()->second.data();
        auto dst_ptr = (src_type*)outputs.begin()->second.data();
        const auto src_row_size = input_metadata.shape.width * input_metadata.shape.features;
        const auto dst_row_size = output_metadata.shape.width * output_metadata.shape.features;
        const auto src_width_size = input_metadata.shape.features;
        const auto dst_width_size = output_metadata.shape.features;

        for (uint32_t r = 0; r < input_metadata.shape.height; r++) { // H axis - rows
            dst_type *src_row = src_ptr + (r * src_row_size);
            src_type *dst_row = dst_ptr + (r * dst_row_size);
            for (uint32_t w = 0; w < input_metadata.shape.width; w++) { // W axis - coloums
                dst_type *src_col = src_row + (w * src_width_size); 
                src_type *dst_col = dst_row + (w * dst_width_size);
                // In order to avoid overflows, we will perform the following:
                // For each HW, we will find the maximal c value and then we will substract this value from
                // all of the values in this HW. This will preserve the original softmax values + prevent overflows
                src_type max_val = std::numeric_limits<float>::min();
                for (uint32_t c = 0; c < input_metadata.shape.features; c++) {
                    auto &current_value = *(src_col + c);
                    if (current_value > max_val)
                        max_val = current_value;
                }
                dst_type sum_exp = 0; // denominator
                for (uint32_t c = 0; c < input_metadata.shape.features; c++) { // C axis - features
                    auto &current_value = *(src_col + c);
                    current_value -= max_val; // This step preserves the original softmax values + prevent overflows
                    current_value = std::exp(static_cast<float32_t>(current_value)); // Set src_ptr[c] to e^(src_ptr[c]) so that we only calculate it once
                    sum_exp += current_value;
                }
                for (uint32_t c = 0; c < input_metadata.shape.features; c++) {
                    const auto &current_value = *(src_col + c);
                    dst_col[c] = static_cast<dst_type>(current_value / sum_exp);
                }
            }
        }
        return HAILO_SUCCESS;
    }

    template<typename src_type = float32_t, typename dst_type = float32_t>
    static hailo_status NC_to_NC(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
        const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
    {
        (void) output_metadata;
        auto src_ptr = (src_type*)inputs.begin()->second.data();
        auto dst_ptr = (dst_type*)outputs.begin()->second.data();
        // In order to avoid overflows, we will perform the following:
        // For each HW, we will find the maximal c value and then we will substract this value from
        // all of the values in this HW. This will preserve the original softmax values + prevent overflows
        src_type max_val = std::numeric_limits<float>::min();
        for (uint32_t c = 0; c < input_metadata.shape.features; c++) {
            auto &current_value = *(src_ptr + c);
            if (current_value > max_val)
                max_val = current_value;
        }
        dst_type sum_exp = 0;
        for (uint32_t c = 0; c < input_metadata.shape.features; c++) {
            auto &current_value = *(src_ptr + c);
            current_value -= max_val; // This step preserves the original softmax values + prevent overflows
            current_value = std::exp(static_cast<dst_type>(current_value)); // Set src_ptr[c] to e^(src_ptr[c])
            sum_exp += current_value;
        }
        for (uint32_t c = 0; c < input_metadata.shape.features; c++) {
            dst_ptr[c] = static_cast<dst_type>(src_ptr[c] / sum_exp);
        }
        return HAILO_SUCCESS;
    }
    
    static hailo_status execute_not_supported(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
        const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs);

    public:
        static Expected<std::shared_ptr<Op>> create(const std::map<std::string, BufferMetaData> &inputs_metadata,
                                                    std::map<std::string, BufferMetaData> &outputs_metadata);
        virtual hailo_status execute(const std::map<std::string, MemoryView> &inputs,
            std::map<std::string, MemoryView> &outputs) override;
        virtual std::string get_op_description() override;
        hailo_status validate_metadata() override;

        // A 3D array of softmax functions to call:
        // 1st dim represent the data format order (NHWC and NC are supported)
        // 2nd dim represent the input data type (only float_32 is supported)
        // 3rd dim represent the output data type (only float_32 is supported)
        static SoftmaxFunction m_softmax_function_array[SOFTMAX_NUM_OF_POSSIBLE_FORMAT_ORDERS][SOFTMAX_NUM_OF_POSSIBLE_FORMAT_TYPES][SOFTMAX_NUM_OF_POSSIBLE_FORMAT_TYPES];

};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_SOFTMAX_POST_PROCESS_HPP_ */