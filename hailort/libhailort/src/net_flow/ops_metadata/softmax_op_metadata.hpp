/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file softmax_op_metadata.hpp
 * @brief: Softmax op metadata
 *
 **/

#ifndef _HAILO_SOFTMAX_OP_METADATA_HPP_
#define _HAILO_SOFTMAX_OP_METADATA_HPP_

#include "net_flow/ops_metadata/op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

constexpr std::size_t SOFTMAX_NUMBER_OF_SRCS {1};
constexpr std::size_t SOFTMAX_NUMBER_OF_DSTS {1};

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

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_SOFTMAX_OP_METADATA_HPP_ */