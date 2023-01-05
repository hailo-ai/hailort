/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file net_flow_api.hpp
 * @brief Defines binding to a HailoRT++ ops usage over Python.
 **/

#ifndef _HAILO_NET_FLOW_API_HPP_
#define _HAILO_NET_FLOW_API_HPP_

#include "utils.hpp"
#include "hailo/hailort.hpp"
#include "bindings_common.hpp"
#include "net_flow/ops/yolo_post_processing.hpp"

namespace hailort
{
namespace net_flow
{

class YOLOv5PostProcessingOpWrapper
{
public:
    static YOLOv5PostProcessingOpWrapper create(const std::vector<std::vector<int>> &anchors,
        const std::vector<hailo_3d_image_shape_t> &shapes, const std::vector<hailo_format_t> &formats,
        const std::vector<hailo_quant_info_t> &quant_infos, float32_t image_height, float32_t image_width, float32_t confidence_threshold,
        float32_t iou_threshold, uint32_t num_of_classes, bool should_dequantize, uint32_t max_boxes, bool should_sigmoid,
        bool one_class_per_bbox=true)
    {
        auto op = YOLOv5PostProcessingOp::create(anchors, shapes, formats, quant_infos, image_height, image_width,
            confidence_threshold, iou_threshold, num_of_classes, should_dequantize, max_boxes, should_sigmoid, one_class_per_bbox);
        VALIDATE_EXPECTED(op);
        
        return YOLOv5PostProcessingOpWrapper(op.release(), num_of_classes, max_boxes);
    }

    static void add_to_python_module(py::module &m)
    {
        py::class_<YOLOv5PostProcessingOpWrapper>(m, "YOLOv5PostProcessingOp")
        .def("create", &YOLOv5PostProcessingOpWrapper::create)
        .def("execute",[](YOLOv5PostProcessingOpWrapper &self, const std::vector<py::array> &tensors)
        {
            std::vector<MemoryView> data_views;
            data_views.reserve(tensors.size());
            for (auto &tensor : tensors) {
                data_views.push_back(MemoryView(const_cast<void*>(reinterpret_cast<const void*>(tensor.data())), tensor.nbytes()));
            }

            hailo_nms_info_t nms_info = {
                self.m_num_of_classes,
                self.m_max_boxes,
                sizeof(hailo_bbox_float32_t),
                1,
                false,
                hailo_nms_defuse_info_t()
            };
            hailo_format_t output_format = {
                HAILO_FORMAT_TYPE_FLOAT32,
                HAILO_FORMAT_ORDER_HAILO_NMS,
                HAILO_FORMAT_FLAGS_QUANTIZED,
            };

            auto buffer = Buffer::create(HailoRTCommon::get_nms_host_frame_size(nms_info, output_format), 0);
            VALIDATE_STATUS(buffer.status());
            auto status = self.m_post_processing_op.execute<float32_t>(data_views, MemoryView(buffer.value().data(), buffer.value().size()));
            VALIDATE_STATUS(status);

            // Note: The ownership of the buffer is transferred to Python wrapped as a py::array.
            //       When the py::array isn't referenced anymore in Python and is destructed, the py::capsule's dtor
            //       is called too (and it deletes the raw buffer)
            auto type = py::dtype(HailoRTBindingsCommon::convert_format_type_to_string(HAILO_FORMAT_TYPE_FLOAT32));
            auto shape = *py::array::ShapeContainer({buffer.value().size()});
            const auto unmanaged_addr = buffer.release().release();
            return py::array(type, shape, unmanaged_addr,
                py::capsule(unmanaged_addr, [](void *p) { delete reinterpret_cast<uint8_t*>(p); }));
        })
        ;
    }

private:
    YOLOv5PostProcessingOpWrapper(YOLOv5PostProcessingOp &&post_processing_op, uint32_t num_of_classes, uint32_t max_bboxes)
        : m_post_processing_op(post_processing_op),
          m_num_of_classes(num_of_classes),
          m_max_boxes(max_bboxes) {}

    YOLOv5PostProcessingOp m_post_processing_op;
    uint32_t m_num_of_classes = 0;
    uint32_t m_max_boxes = 0;
};

void NetFlow_api_initialize_python_module(py::module &m)
{
    YOLOv5PostProcessingOpWrapper::add_to_python_module(m);
}


} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_NET_FLOW_API_HPP_ */
