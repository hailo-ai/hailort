/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file argmax_op_metadata.hpp
 * @brief Argmax op metadata
 *
 **/

#ifndef _HAILO_ARGMAX_OP_METADATA_HPP_
#define _HAILO_ARGMAX_OP_METADATA_HPP_

#include "net_flow/ops_metadata/op_metadata.hpp"

namespace hailort
{
namespace net_flow
{

constexpr std::size_t ARGMAX_OUTPUT_FEATURES_SIZE {1};
constexpr std::size_t ARGMAX_NUMBER_OF_SRCS {1};
constexpr std::size_t ARGMAX_NUMBER_OF_DSTS {1};

class ArgmaxOpMetadata : public OpMetadata
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
    ArgmaxOpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
                        const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
                        const std::string &network_name)
        : OpMetadata(inputs_metadata, outputs_metadata, "Argmax-Post-Process", network_name, OperationType::ARGMAX)
    {}

    hailo_status validate_params() override;
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_ARGMAX_POST_PROCESS_HPP_ */
