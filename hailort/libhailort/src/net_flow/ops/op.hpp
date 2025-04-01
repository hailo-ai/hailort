/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file op.hpp
 * @brief Net-Flow op
 *
 * https://learnopencv.com/object-detection-using-yolov5-and-opencv-dnn-in-c-and-python :
 * The headline '4.3.5 POST-PROCESSING YOLOv5 Prediction Output' contains explanations on the YOLOv5 post-process.
 **/

#ifndef _HAILO_NET_FLOW_OP_HPP_
#define _HAILO_NET_FLOW_OP_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/network_group.hpp"
#include "net_flow/ops_metadata/op_metadata.hpp"

#include "common/utils.hpp"
#include "common/logger_macros.hpp"


namespace hailort
{
namespace net_flow
{

class Op
{
public:
    virtual ~Op() = default;

    /**
     * Executes operation on inferred data.
     *
     * @param[in] inputs                A map between input names to input buffers.
     * @param[in] outputs               A map between outputs names and their pre-allocated buffers.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     *
     */
    virtual hailo_status execute(const std::map<std::string, MemoryView> &inputs, std::map<std::string, MemoryView> &outputs) = 0;

    const std::unordered_map<std::string, BufferMetaData> &inputs_metadata() const
    {
        return m_op_metadata->inputs_metadata();
    }

    const std::unordered_map<std::string, BufferMetaData> &outputs_metadata() const
    {
        return m_op_metadata->outputs_metadata();
    }

    std::string get_name() {
        return m_op_metadata->get_name();
    }

    const PostProcessOpMetadataPtr &metadata() { return m_op_metadata;}


protected:

    Op(PostProcessOpMetadataPtr op_metadata)
        : m_op_metadata(op_metadata)
    {}

    PostProcessOpMetadataPtr m_op_metadata;

};

}
}

#endif // _HAILO_NET_FLOW_OP_HPP_
