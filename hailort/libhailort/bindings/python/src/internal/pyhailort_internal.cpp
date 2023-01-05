#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>
#include <vector>

#include "pyhailort_internal.hpp"
#include "control_api.hpp"
#include "utils.hpp"
#include "utils.h"

#include "hailo/hailort.h"
#include "transform_internal.hpp"
#include "bindings_common.hpp"


namespace hailort
{

static const uint32_t TEST_NUM_OF_CLASSES2 = 80;

py::array PyhailortInternal::get_yolov5_post_process_expected_buffer()
{
    static const uint32_t DETECTION_CLASS_ID_1 = 0;
    static const float32_t CLASS_ID_1_DETECTION_COUNT = 5;
    static const uint32_t DETECTION_CLASS_ID_3 = 2;
    static const float32_t CLASS_ID_3_DETECTION_COUNT = 2;
    static const uint32_t DETECTION_CLASS_ID_8 = 7;
    static const float32_t CLASS_ID_8_DETECTION_COUNT = 1;
    static const uint32_t DETECTION_CLASS_ID_26 = 25;
    static const float32_t CLASS_ID_26_DETECTION_COUNT = 1;

    static const hailo_bbox_float32_t bbox1_0 = {
        /*.y_min =*/ 0.5427529811859131f,
        /*.x_min =*/ 0.2485126256942749f,
        /*.y_max =*/ 0.6096446067f,
        /*.x_max =*/ 0.27035075984f,
        /*.score =*/ 0.7761699557304382f,
    };
    
    static const hailo_bbox_float32_t bbox1_1 = {
        /*.y_min =*/ 0.5454554557800293f,
        /*.x_min =*/ 0.33257606625556948f,
        /*.y_max =*/ 0.7027952075f,
        /*.x_max =*/ 0.40901548415f,
        /*.score =*/ 0.7637669444084168f,
    };
    
    static const hailo_bbox_float32_t bbox1_2 = {
        /*.y_min =*/ 0.5521867275238037f,
        /*.x_min =*/ 0.19988654553890229f,
        /*.y_max =*/ 0.60256312787f,
        /*.x_max =*/ 0.21917282976f,
        /*.score =*/ 0.7451231479644775f,
    };

    static const hailo_bbox_float32_t bbox1_3 = {
        /*.y_min =*/ 0.5514537692070007f,
        /*.x_min =*/ 0.2693796157836914f,
        /*.y_max =*/ 0.60397491604f,
        /*.x_max =*/ 0.28537025302f,
        /*.score =*/ 0.3756354749202728f,
    };
    
    static const hailo_bbox_float32_t bbox1_4 = {
        /*.y_min =*/ 0.553998589515686f,
        /*.x_min =*/ 0.18612079322338105f,
        /*.y_max =*/ 0.58339602686f,
        /*.x_max =*/ 0.2008818537f,
        /*.score =*/ 0.3166312277317047f,
    };

    static const hailo_bbox_float32_t bbox3_0 = {
        /*.y_min =*/ 0.5026738047599793f,
        /*.x_min =*/ -0.005611047148704529f,
        /*.y_max =*/ 0.65071095526f,
        /*.x_max =*/ 0.13888412714f,
        /*.score =*/ 0.5734351277351379f,
    };

    static const hailo_bbox_float32_t bbox3_1 = {
        /*.y_min =*/ 0.5620155334472656f,
        /*.x_min =*/ 0.16757474839687348f,
        /*.y_max =*/ 0.58410947769f,
        /*.x_max =*/ 0.19325175508f,
        /*.score =*/ 0.4062519371509552f,
    };

    static const hailo_bbox_float32_t bbox8_0 = {
        /*.y_min =*/ 0.5028372406959534f,
        /*.x_min =*/ -0.0017736181616783143f,
        /*.y_max =*/ 0.65114967525f,
        /*.x_max =*/ 0.13592261821f,
        /*.score =*/ 0.4223918318748474f,
    };

    static const hailo_bbox_float32_t bbox26_0 = {
        /*.y_min =*/ 0.5854946374893189f,
        /*.x_min =*/ 0.2693060040473938f,
        /*.y_max =*/ 0.68259389698f,
        /*.x_max =*/ 0.38090330362f,
        /*.score =*/ 0.6338639259338379f,
    };

    static const uint32_t DETECTION_COUNT = 9;
    auto buffer_size = (DETECTION_COUNT * sizeof(hailo_bbox_float32_t)) + (TEST_NUM_OF_CLASSES2 * sizeof(float32_t));
    auto buffer_expected = hailort::Buffer::create(buffer_size, 0);
    // CATCH_REQUIRE_EXPECTED(buffer_expected);
    auto buffer = buffer_expected.release();

    size_t offset = 0;
    for (uint32_t class_index = 0; class_index < TEST_NUM_OF_CLASSES2; class_index++) {
        if (DETECTION_CLASS_ID_1 == class_index) {
            memcpy(buffer.data() + offset, &CLASS_ID_1_DETECTION_COUNT, sizeof(CLASS_ID_1_DETECTION_COUNT));
            offset += sizeof(CLASS_ID_1_DETECTION_COUNT);

            memcpy(buffer.data() + offset, &bbox1_0, sizeof(bbox1_0));
            offset += sizeof(bbox1_0);

            memcpy(buffer.data() + offset, &bbox1_1, sizeof(bbox1_1));
            offset += sizeof(bbox1_1);

            memcpy(buffer.data() + offset, &bbox1_2, sizeof(bbox1_2));
            offset += sizeof(bbox1_2);

            memcpy(buffer.data() + offset, &bbox1_3, sizeof(bbox1_3));
            offset += sizeof(bbox1_3);

            memcpy(buffer.data() + offset, &bbox1_4, sizeof(bbox1_4));
            offset += sizeof(bbox1_4);
        }
        else if (DETECTION_CLASS_ID_3 == class_index) {
            memcpy(buffer.data() + offset, &CLASS_ID_3_DETECTION_COUNT, sizeof(CLASS_ID_3_DETECTION_COUNT));
            offset += sizeof(CLASS_ID_3_DETECTION_COUNT);

            memcpy(buffer.data() + offset, &bbox3_0, sizeof(bbox3_0));
            offset += sizeof(bbox3_0);

            memcpy(buffer.data() + offset, &bbox3_1, sizeof(bbox3_1));
            offset += sizeof(bbox3_1);
        }
        else if (DETECTION_CLASS_ID_8 == class_index) {
            memcpy(buffer.data() + offset, &CLASS_ID_8_DETECTION_COUNT, sizeof(CLASS_ID_8_DETECTION_COUNT));
            offset += sizeof(CLASS_ID_8_DETECTION_COUNT);

            memcpy(buffer.data() + offset, &bbox8_0, sizeof(bbox8_0));
            offset += sizeof(bbox8_0);
        }
        else if (DETECTION_CLASS_ID_26 == class_index) {
            memcpy(buffer.data() + offset, &CLASS_ID_26_DETECTION_COUNT, sizeof(CLASS_ID_26_DETECTION_COUNT));
            offset += sizeof(CLASS_ID_26_DETECTION_COUNT);

            memcpy(buffer.data() + offset, &bbox26_0, sizeof(bbox26_0));
            offset += sizeof(bbox26_0);
        }
        else {
            offset += sizeof(float32_t);
        }
    }

    // Note: The ownership of the buffer is transferred to Python wrapped as a py::array.
    //       When the py::array isn't referenced anymore in Python and is destructed, the py::capsule's dtor
    //       is called too (and it deletes the raw buffer)
    auto type = py::dtype(HailoRTBindingsCommon::convert_format_type_to_string(HAILO_FORMAT_TYPE_FLOAT32));
    auto shape = *py::array::ShapeContainer({buffer.size()});
    const auto unmanaged_addr = buffer.release();
    return py::array(type, shape, unmanaged_addr,
        py::capsule(unmanaged_addr, [](void *p) { delete reinterpret_cast<uint8_t*>(p); }));
}

void PyhailortInternal::demux_output_buffer(
    py::bytes src, const hailo_format_t &src_format, const hailo_3d_image_shape_t &src_shape,
    std::map<std::string, py::array> dst_buffers, const LayerInfo &mux_layer_info)
{
    const size_t hw_frame_size = HailoRTCommon::get_frame_size(src_shape, src_format);
    auto expected_output_demuxer = OutputDemuxerBase::create(hw_frame_size, mux_layer_info);
    VALIDATE_EXPECTED(expected_output_demuxer);

    auto demuxer = expected_output_demuxer.release();

    std::map<std::string, MemoryView> dst_ptrs;
    for (auto &dst_buffer_pair : dst_buffers) {
        dst_ptrs.insert(std::make_pair(dst_buffer_pair.first,
            MemoryView(reinterpret_cast<uint8_t*>(dst_buffer_pair.second.mutable_data()),
                dst_buffer_pair.second.nbytes())));
    }

    const auto src_str = static_cast<std::string>(src);
    auto status = demuxer.transform_demux(
        MemoryView(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(src_str.c_str())), src_str.length()), dst_ptrs);
    VALIDATE_STATUS(status);
}

void PyhailortInternal::transform_input_buffer(
    py::array src, const hailo_format_t &src_format, const hailo_3d_image_shape_t &src_shape,
    uintptr_t dst, size_t dst_size, const hailo_format_t &dst_format, const hailo_3d_image_shape_t &dst_shape,
    const hailo_quant_info_t &dst_quant_info)
{
    auto transform_context = InputTransformContext::create(src_shape, src_format, dst_shape, dst_format,
        dst_quant_info);
    VALIDATE_EXPECTED(transform_context);

    MemoryView dst_buffer(reinterpret_cast<uint8_t*>(dst), dst_size);
    auto status = transform_context.value()->transform(
        MemoryView::create_const(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(src.data())), src.nbytes()),
        dst_buffer);
    VALIDATE_STATUS(status);
}

void PyhailortInternal::transform_output_buffer(
    py::bytes src, const hailo_format_t &src_format, const hailo_3d_image_shape_t &src_shape,
    py::array dst, const hailo_format_t &dst_format, const hailo_3d_image_shape_t &dst_shape,
    const hailo_quant_info_t &dst_quant_info)
{
    auto transform_context = OutputTransformContext::create(src_shape, src_format, dst_shape, dst_format,
        dst_quant_info, {});
    VALIDATE_EXPECTED(transform_context);

    const auto src_str = static_cast<std::string>(src);
    MemoryView dst_buffer(reinterpret_cast<uint8_t*>(dst.mutable_data()), dst.nbytes());
    auto status = transform_context.value()->transform(MemoryView::create_const(src_str.c_str(),
        src_str.length()), dst_buffer);
    VALIDATE_STATUS(status);
}

void PyhailortInternal::transform_output_buffer_nms(
    py::bytes src, const hailo_format_t &src_format, const hailo_3d_image_shape_t &src_shape,
    py::array dst, const hailo_format_t &dst_format, const hailo_3d_image_shape_t &dst_shape,
    const hailo_quant_info_t &dst_quant_info, const hailo_nms_info_t &nms_info)
{
    auto transform_context = OutputTransformContext::create(src_shape, src_format, dst_shape, dst_format,
        dst_quant_info, nms_info);
    VALIDATE_EXPECTED(transform_context);

    const auto src_str = static_cast<std::string>(src);
    MemoryView dst_buffer(reinterpret_cast<uint8_t*>(dst.mutable_data()), dst.nbytes());
    auto status = transform_context.value()->transform(MemoryView::create_const(src_str.c_str(),
        src_str.size()), dst_buffer);
    VALIDATE_STATUS(status);
}

bool PyhailortInternal::is_input_transformation_required(
    const hailo_3d_image_shape_t &src_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_shape, const hailo_format_t &dst_format,
    const hailo_quant_info_t &quant_info)
{
    return InputTransformContext::is_transformation_required(src_shape, src_format, dst_shape, dst_format,
        quant_info);
}

bool PyhailortInternal::is_output_transformation_required(
    const hailo_3d_image_shape_t &src_shape, const hailo_format_t &src_format,
    const hailo_3d_image_shape_t &dst_shape, const hailo_format_t &dst_format,
    const hailo_quant_info_t &quant_info)
{
    return OutputTransformContext::is_transformation_required(src_shape, src_format, dst_shape, dst_format,
        quant_info);
}

py::list PyhailortInternal::get_all_layers_info(const HefWrapper &hef, const std::string &net_group_name)
{
    auto network_group_metadata = hef.hef_ptr()->pimpl->get_network_group_metadata(net_group_name);
    VALIDATE_EXPECTED(network_group_metadata);

    return py::cast(network_group_metadata->get_all_layer_infos());
}

PYBIND11_MODULE(_pyhailort_internal, m) {
    ControlWrapper::add_to_python_module(m);
    m.def("get_yolov5_post_process_expected_buffer", &PyhailortInternal::get_yolov5_post_process_expected_buffer);
    m.def("demux_output_buffer", &PyhailortInternal::demux_output_buffer);
    m.def("transform_input_buffer", &PyhailortInternal::transform_input_buffer);
    m.def("transform_output_buffer", &PyhailortInternal::transform_output_buffer);
    m.def("transform_output_buffer_nms", &PyhailortInternal::transform_output_buffer_nms);
    m.def("is_input_transformation_required", &PyhailortInternal::is_input_transformation_required);
    m.def("is_output_transformation_required", &PyhailortInternal::is_output_transformation_required);
    m.def("get_all_layers_info", &PyhailortInternal::get_all_layers_info);

    py::class_<BufferIndices>(m, "BufferIndices", py::module_local())
        .def_readonly("index", &BufferIndices::index)
        .def_readonly("cluster_index", &BufferIndices::cluster_index)
        ;

    py::class_<LayerInfo>(m, "HailoLayerInfo", py::module_local())
        .def_readonly("is_mux", &LayerInfo::is_mux)
        .def_readonly("mux_predecessors", &LayerInfo::predecessor)
        .def_readonly("is_defused_nms", &LayerInfo::is_defused_nms)
        .def_readonly("fused_nms_layer", &LayerInfo::fused_nms_layer)
        .def_property_readonly("shape", [](LayerInfo& self)
        {
            switch (self.format.order) {
                case HAILO_FORMAT_ORDER_NC:
                    return py::make_tuple(self.shape.features);
                case HAILO_FORMAT_ORDER_NHW:
                    return py::make_tuple(self.shape.height, self.shape.width);
                default:
                    return py::make_tuple(self.shape.height, self.shape.width, self.shape.features);
            }
        })
        .def_property_readonly("height", [](LayerInfo& self)
        {
            return self.shape.height;
        })
        .def_property_readonly("width", [](LayerInfo& self)
        {
            return self.shape.width;
        })
        .def_property_readonly("features", [](LayerInfo& self)
        {
            return self.shape.features;
        })
        .def("hw_shape", [](LayerInfo& self)
        {
            return py::make_tuple(self.hw_shape.height, self.hw_shape.width, self.hw_shape.features);
        })
        .def_property_readonly("padded_height", [](LayerInfo& self)
        {
            return self.hw_shape.height;
        })
        .def_property_readonly("padded_width", [](LayerInfo& self)
        {
            return self.hw_shape.width;
        })
        .def_property_readonly("padded_features", [](LayerInfo& self)
        {
            return self.hw_shape.features;
        })
        .def_readonly("data_bytes", &LayerInfo::hw_data_bytes)
        .def_readonly("format", &LayerInfo::format)
        .def_property_readonly("format_order", [](LayerInfo& self)
        {
            return self.format.order;
        })
        .def_readonly("direction", &LayerInfo::direction)
        .def_readonly("sys_index", &LayerInfo::stream_index)
        .def_readonly("name", &LayerInfo::name)
        .def_readonly("quant_info", &LayerInfo::quant_info)
        // For backwards compatibility (accessing qp through layer_info directly)
        .def_property_readonly("qp_zp", [](LayerInfo& self)
        {
            return self.quant_info.qp_zp;
        })
        .def_property_readonly("qp_scale", [](LayerInfo& self)
        {
            return self.quant_info.qp_scale;
        })
        .def_property_readonly("limvals_min", [](LayerInfo& self)
        {
            return self.quant_info.limvals_min;
        })
        .def_property_readonly("limvals_max", [](LayerInfo& self)
        {
            return self.quant_info.limvals_max;
        })
        .def_readonly("nms_info", &LayerInfo::nms_info)
        .def_readonly("height_gcd", &LayerInfo::height_gcd)
        .def_readonly("height_ratios", &LayerInfo::height_ratios)
        .def_readonly("buffer_indices", &LayerInfo::buffer_indices)
        .def_property_readonly("core_bytes_per_buffer", [](LayerInfo& self)
        {
            return self.nn_stream_config.core_bytes_per_buffer;
        })
        .def_property_readonly("core_buffers_per_frame", [](LayerInfo& self)
        {
            return self.nn_stream_config.core_buffers_per_frame;
        })
        .def_readonly("network_name", &LayerInfo::network_name)
        ;
}

} /* namespace hailort */