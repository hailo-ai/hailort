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

#include "hailo/hailort.h"

#include "net_flow/ops/yolo_post_process.hpp"

#include "utils.hpp"
#include "bindings_common.hpp"


namespace hailort
{
namespace net_flow
{

class YOLOv5PostProcessOpWrapper
{
public:
    static YOLOv5PostProcessOpWrapper create(const std::vector<std::vector<int>> &anchors,
        const std::vector<hailo_3d_image_shape_t> &shapes, const std::vector<hailo_format_t> &formats,
        const std::vector<hailo_quant_info_t> &quant_infos, float32_t image_height, float32_t image_width, float32_t confidence_threshold,
        float32_t iou_threshold, uint32_t num_of_classes, uint32_t max_boxes,
        bool cross_classes=true)
    {
        std::map<std::string, net_flow::BufferMetaData> inputs_metadata;
        std::map<std::string, net_flow::BufferMetaData> outputs_metadata;

        net_flow::NmsPostProcessConfig nms_post_process_config{};
        nms_post_process_config.nms_score_th = confidence_threshold;
        nms_post_process_config.nms_iou_th = iou_threshold;
        nms_post_process_config.max_proposals_per_class = max_boxes;
        nms_post_process_config.classes = num_of_classes;
        nms_post_process_config.background_removal = false;
        nms_post_process_config.background_removal_index = 0;
        nms_post_process_config.cross_classes = cross_classes;
        net_flow::YoloPostProcessConfig yolo_post_process_config{};
        yolo_post_process_config.image_height = image_height;
        yolo_post_process_config.image_width = image_width;
        // Each layer anchors vector is structured as {w,h} pairs.
        for (size_t i = 0; i < anchors.size(); ++i) {
            auto name = std::to_string(i);
            yolo_post_process_config.anchors.insert({name, anchors[i]});
            BufferMetaData input_metadata = {
                shapes[i],
                shapes[i],
                formats[i],
                quant_infos[i]
            };
            inputs_metadata.insert({name, input_metadata});
        }
        auto op = YOLOv5PostProcessOp::create(inputs_metadata, outputs_metadata, nms_post_process_config, yolo_post_process_config);
        VALIDATE_EXPECTED(op);

        return YOLOv5PostProcessOpWrapper(op.release(), num_of_classes, max_boxes);
    }

    static void add_to_python_module(py::module &m)
    {
        py::class_<YOLOv5PostProcessOpWrapper>(m, "YOLOv5PostProcessOp")
        .def("create", &YOLOv5PostProcessOpWrapper::create)
        .def("execute",[](YOLOv5PostProcessOpWrapper &self, const std::vector<py::array> &tensors)
        {
            std::map<std::string, MemoryView> data_views;
            for (size_t i = 0; i < tensors.size(); ++i) {
                data_views.insert({std::to_string(i),
                    MemoryView(const_cast<void*>(reinterpret_cast<const void*>(tensors[i].data())), tensors[i].nbytes())});
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
            std::map<std::string, MemoryView> outputs;
            outputs.insert({"", MemoryView(buffer.value().data(), buffer.value().size())});
            auto status = self.m_post_processing_op->execute(data_views, outputs);
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
    YOLOv5PostProcessOpWrapper(std::shared_ptr<Op> post_processing_op, uint32_t num_of_classes, uint32_t max_bboxes)
        : m_post_processing_op(post_processing_op),
          m_num_of_classes(num_of_classes),
          m_max_boxes(max_bboxes) {}

    std::shared_ptr<Op> m_post_processing_op;
    uint32_t m_num_of_classes = 0;
    uint32_t m_max_boxes = 0;
};

void NetFlow_api_initialize_python_module(py::module &m)
{
    YOLOv5PostProcessOpWrapper::add_to_python_module(m);
}


} /* namespace net_flow */
} /* namespace hailort */

#endif /* _HAILO_NET_FLOW_API_HPP_ */
