/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
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

#include "common/utils.hpp"
#include "common/logger_macros.hpp"


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

    virtual hailo_status validate_metadata() = 0;

    const std::map<std::string, BufferMetaData> &inputs_metadata() const
    {
        return m_inputs_metadata;
    }

    const std::map<std::string, BufferMetaData> &outputs_metadata() const
    {
        return m_outputs_metadata;
    }

    void set_outputs_metadata(std::map<std::string, BufferMetaData> &outputs_metadata)
    {
        m_outputs_metadata = outputs_metadata;
    }

    void set_inputs_metadata(std::map<std::string, BufferMetaData> &inputs_metadata)
    {
        m_inputs_metadata = inputs_metadata;
    }

    std::string get_name() {
        return m_name;
    }

    virtual std::string get_op_description() = 0;

protected:
    Op(const std::map<std::string, BufferMetaData> &inputs_metadata,
       const std::map<std::string, BufferMetaData> &outputs_metadata,
       const std::string &name)
        : m_inputs_metadata(inputs_metadata)
        , m_outputs_metadata(outputs_metadata)
        , m_name(name)
    {}

    std::map<std::string, BufferMetaData> m_inputs_metadata;
    std::map<std::string, BufferMetaData> m_outputs_metadata;
    const std::string m_name;
};

}
}

#endif // _HAILO_NET_FLOW_OP_HPP_