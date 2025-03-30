/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file argmax_post_process.hpp
 * @brief: Argmax op perform argmax op as described: https://www.tensorflow.org/api_docs/python/tf/math/argmax
 * A few notes:
 *  - Support only on features axis
 *  - Support only on NHWC, NHCW and NC input data order
  *  - In case of 2 maximal values - the lower index one will be given.
 **/

#ifndef _HAILO_ARGMAX_POST_PROCESS_HPP_
#define _HAILO_ARGMAX_POST_PROCESS_HPP_


#include "hailo/hailort.h"
#include "net_flow/ops/op.hpp"
#include "net_flow/ops_metadata/argmax_op_metadata.hpp"
#include "common/utils.hpp"

#include <iostream>

namespace hailort
{
namespace net_flow
{

#define ARGMAX_NUM_OF_POSSIBLE_FORMAT_ORDERS (4)
#define ARGMAX_NUM_OF_POSSIBLE_FORMAT_TYPES (4)
#define F8CR_FEATURES_IN_CHUNK (8)

typedef hailo_status (*ArgmaxFunction)(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
    const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs);

class ArgmaxPostProcessOp : public Op
{

private:
    ArgmaxPostProcessOp(std::shared_ptr<ArgmaxOpMetadata> metadata)
        : Op(static_cast<std::shared_ptr<OpMetadata>>(metadata))
    {}

    template<typename SrcType, typename DstType>
    static hailo_status NHCW_to_NHW_feature_axis(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
        const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
    {
        auto src_ptr = (SrcType*)inputs.begin()->second.data();
        auto dst_ptr = (DstType*)outputs.begin()->second.data();
        const auto src_row_size = input_metadata.padded_shape.width * input_metadata.padded_shape.features;
        const auto dst_row_size = output_metadata.shape.width;

        for (uint32_t r = 0; r < input_metadata.shape.height; r++) {
            const SrcType *src_row = src_ptr + (r * src_row_size);
            DstType *dst_row = dst_ptr + (r * dst_row_size);
            for (uint32_t w = 0; w < input_metadata.shape.width; w++) {
                const SrcType *offset_in_row = src_row + w;
                DstType max_index = 0;
                auto max_value = *offset_in_row;
                for (uint32_t c = 1; c < input_metadata.shape.features; c++) {
                    offset_in_row += input_metadata.padded_shape.width;
                    const auto &current_value = *offset_in_row;
                    if (current_value > max_value) {
                        max_index = static_cast<DstType>(c);
                        max_value = current_value;
                    }
                }
                dst_row[w] = max_index;
            }
        }
        return HAILO_SUCCESS;
    }

    template<typename SrcType, typename DstType>
    static hailo_status NHWC_to_NHW_feature_axis(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
        const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
    {
        auto src_ptr = (SrcType*)inputs.begin()->second.data();
        auto dst_ptr = (DstType*)outputs.begin()->second.data();
        const auto src_row_size = input_metadata.padded_shape.width * input_metadata.padded_shape.features;
        const auto dst_row_size = output_metadata.shape.width;

        for (uint32_t r = 0; r < input_metadata.shape.height; r++) {
            const SrcType *src_row = src_ptr + (r * src_row_size);
            DstType *dst_row = dst_ptr + (r * dst_row_size);
            for (uint32_t w = 0; w < input_metadata.shape.width; w++) {
                const SrcType *offset_in_row = src_row + (w * input_metadata.padded_shape.features);
                DstType max_index = 0;
                auto max_value = *offset_in_row;
                for (uint32_t c = 1; c < input_metadata.shape.features; c++) {
                    const auto &current_value = *(offset_in_row + c);
                    if (current_value > max_value) {
                        max_index = static_cast<DstType>(c);
                        max_value = current_value;
                    }
                }
                dst_row[w] = max_index;
            }
        }
        return HAILO_SUCCESS;
    }

    template<typename SrcType, typename DstType>
    static hailo_status NC_to_N(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
        const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
    {
        (void) output_metadata; // only reason to have output_metadata is so that the function array will work
        auto src_ptr = (SrcType*)inputs.begin()->second.data();
        auto dst_ptr = (DstType*)outputs.begin()->second.data();
        DstType max_index = 0;
        SrcType max_value = 0;

        for (uint32_t c = 0; c < input_metadata.shape.features; c++) {
            const auto &current_value = *(src_ptr + c);
            if (current_value > max_value) {
                max_index = static_cast<DstType>(c);
                max_value = current_value;
            }
        }
        *dst_ptr = max_index;
        return HAILO_SUCCESS;
    }

    template<typename SrcType, typename DstType>
    static hailo_status F8CR_to_NHW_feature_axis(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
        const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs)
    {
        auto src_ptr = (SrcType*)inputs.begin()->second.data();
        auto dst_ptr = (DstType*)outputs.begin()->second.data();
        const auto src_row_size = input_metadata.padded_shape.width * input_metadata.padded_shape.features;
        const auto dst_row_size = output_metadata.shape.width;
        const auto num_of_eight_channels_chunks = input_metadata.padded_shape.features / F8CR_FEATURES_IN_CHUNK;
        const auto eight_channels_x_width_size = input_metadata.padded_shape.width * F8CR_FEATURES_IN_CHUNK;

        for (uint32_t r = 0; r < input_metadata.shape.height; r++) {
            const SrcType *src_row = src_ptr + (r * src_row_size);
            DstType *dst_row = dst_ptr + (r * dst_row_size);
            for (uint32_t w = 0; w < input_metadata.shape.width; w++) {
                const SrcType *offset_in_row = src_row + (w * F8CR_FEATURES_IN_CHUNK);
                DstType max_index = 0;
                auto max_value = *offset_in_row;
                for (uint32_t channel_chunk_id = 0; channel_chunk_id < num_of_eight_channels_chunks; channel_chunk_id++) {
                    const SrcType *offset_in_column = offset_in_row + (eight_channels_x_width_size * channel_chunk_id);
                    uint32_t num_of_channels_in_chunk = ((channel_chunk_id + 1 == num_of_eight_channels_chunks) ? (input_metadata.shape.features % F8CR_FEATURES_IN_CHUNK) : F8CR_FEATURES_IN_CHUNK );
                    for (uint32_t c = 0; c < num_of_channels_in_chunk; c++) {
                        const auto &current_value = *(offset_in_column + c);
                        if (current_value > max_value) {
                            max_index = static_cast<DstType>(c + F8CR_FEATURES_IN_CHUNK * channel_chunk_id);
                            max_value = current_value;
                        }
                    }
                }
                dst_row[w] = max_index;
            }
        }
        return HAILO_SUCCESS;
    }

    static hailo_status execute_not_supported(const BufferMetaData &input_metadata, const BufferMetaData &output_metadata,
        const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs);

public:
    static Expected<std::shared_ptr<Op>> create(std::shared_ptr<ArgmaxOpMetadata> metadata);
    virtual hailo_status execute(const std::map<std::string, MemoryView> &inputs,
        std::map<std::string, MemoryView> &outputs) override;

    // A 3D array of argmax functions to call:
    // 1st dim represent the data format order
    // 2nd dim represent the input data type (only uint8 or uint16 are valid)
    // 3rd dim represent the output data type
    // Note: Assumption here the ordering of the enum hailo_format_type_t doesn't change
    static ArgmaxFunction m_argmax_function_array[ARGMAX_NUM_OF_POSSIBLE_FORMAT_ORDERS][ARGMAX_NUM_OF_POSSIBLE_FORMAT_TYPES][ARGMAX_NUM_OF_POSSIBLE_FORMAT_TYPES];

};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_ARGMAX_POST_PROCESS_HPP_ */
