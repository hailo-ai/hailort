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
#include "net_flow/ops/op_metadata.hpp"

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

class SoftmaxOpMetadata : public OpMetadata
{
public:
    static Expected<std::shared_ptr<OpMetadata>> create(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                                                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                                                        const std::string &network_name);
    std::string get_op_description() override;
    hailo_status validate_format_info() override;
    static hailo_format_t expand_output_format_autos(const hailo_format_t &output_format, const hailo_format_t &input_format);

    virtual Expected<hailo_vstream_info_t> get_output_vstream_info() override;

private:
    SoftmaxOpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                        const std::string &network_name)
        : OpMetadata(inputs_metadata, outputs_metadata, "Softmax-Post-Process", network_name, OperationType::SOFTMAX)
    {}

    hailo_status validate_params() override;
};

class SoftmaxPostProcessOp : public Op
{

private:
    SoftmaxPostProcessOp(std::shared_ptr<SoftmaxOpMetadata> metadata)
        : Op(static_cast<std::shared_ptr<OpMetadata>>(metadata))
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
                softmax(src_col, dst_col, input_metadata.shape.features);
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
        softmax(src_ptr, dst_ptr, input_metadata.shape.features);
        return HAILO_SUCCESS;
    }

    static hailo_status execute_not_supported(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
        const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs);

    public:
        static Expected<std::shared_ptr<Op>> create(std::shared_ptr<SoftmaxOpMetadata> metadata);
        virtual hailo_status execute(const std::map<std::string, MemoryView> &inputs,
            std::map<std::string, MemoryView> &outputs) override;

        // A 3D array of softmax functions to call:
        // 1st dim represent the data format order (NHWC and NC are supported)
        // 2nd dim represent the input data type (only float_32 is supported)
        // 3rd dim represent the output data type (only float_32 is supported)
        static SoftmaxFunction m_softmax_function_array[SOFTMAX_NUM_OF_POSSIBLE_FORMAT_ORDERS][SOFTMAX_NUM_OF_POSSIBLE_FORMAT_TYPES][SOFTMAX_NUM_OF_POSSIBLE_FORMAT_TYPES];

        static hailo_status softmax(float32_t *src, float32_t *dst, size_t num_of_elements);

};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_SOFTMAX_POST_PROCESS_HPP_ */