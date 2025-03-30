/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file op_metadata.cpp
 * @brief ops metadata's
 *
 **/

#ifndef _HAILO_OP_META_DATA_HPP_
#define _HAILO_OP_META_DATA_HPP_

#include "hailo/hailort.h"
#include <set>

namespace hailort
{
namespace net_flow
{

struct BufferMetaData
{
    hailo_3d_image_shape_t shape;
    hailo_3d_image_shape_t padded_shape;
    hailo_format_t format;
    hailo_quant_info_t quant_info;
};

enum class OperationType {
    YOLOX,
    YOLOV5,
    YOLOV8,
    YOLOV5SEG,
    SSD,
    SOFTMAX,
    ARGMAX,
    IOU
};

class OpMetadata
{
public:
    virtual ~OpMetadata() = default;
    const std::unordered_map<std::string, BufferMetaData> &inputs_metadata() { return m_inputs_metadata;};
    const std::unordered_map<std::string, BufferMetaData> &outputs_metadata() { return m_outputs_metadata;};
    std::string get_name() { return m_name;};
    OperationType type() { return m_type;};
    virtual std::string get_op_description() = 0;
    virtual hailo_status validate_format_info() = 0;

    void set_outputs_metadata(std::unordered_map<std::string, BufferMetaData> &outputs_metadata)
    {
        m_outputs_metadata = outputs_metadata;
    }

    void set_inputs_metadata(std::unordered_map<std::string, BufferMetaData> &inputs_metadata)
    {
        m_inputs_metadata = inputs_metadata;
    }

    virtual Expected<hailo_vstream_info_t> get_output_vstream_info() = 0;

    const std::set<std::string> get_input_names()
    {
        std::set<std::string> names;
        for (const auto &pair : m_inputs_metadata) {
            names.insert(pair.first);
        }
        return names;
    }

    static std::string get_operation_type_str(const OperationType &type)
    {
        switch (type) {
        case OperationType::YOLOX:
            return "YOLOX";
        case OperationType::YOLOV5:
            return "YOLOV5";
        case OperationType::YOLOV5SEG:
            return "YOLOV5SEG";
        case OperationType::YOLOV8:
            return "YOLOV8";
        case OperationType::SSD:
            return "SSD";
        case OperationType::SOFTMAX:
            return "SOFTMAX";
        case OperationType::ARGMAX:
            return "ARGMAX";
        case OperationType::IOU:
            return "IOU";
        default:
            return "Nan";
        }
    }

protected:
    // TODO - move inputs/outputs_metadata to the op itself, since they depend on the vstream_params (HRT-11426)
    std::unordered_map<std::string, BufferMetaData> m_inputs_metadata;
    std::unordered_map<std::string, BufferMetaData> m_outputs_metadata;
    const std::string m_name;
    const std::string m_network_name;
    OperationType m_type;

    OpMetadata(const std::unordered_map<std::string, BufferMetaData> &inputs_metadata,
               const std::unordered_map<std::string, BufferMetaData> &outputs_metadata,
               const std::string &name, const std::string &network_name, const OperationType type) :
                m_inputs_metadata(inputs_metadata), m_outputs_metadata(outputs_metadata),
                m_name(name), m_network_name(network_name), m_type(type)
    {}

    virtual hailo_status validate_params() = 0;
};

} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_OP_META_DATA_HPP_ */