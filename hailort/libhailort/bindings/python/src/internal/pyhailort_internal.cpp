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

namespace hailort
{

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
    auto network_gorup_metadata = hef.hef_ptr()->pimpl->get_network_group_metadata(net_group_name);
    VALIDATE_EXPECTED(network_gorup_metadata);

    auto layers_info = network_gorup_metadata->get_all_layer_infos();
    VALIDATE_EXPECTED(layers_info);

    return py::cast(layers_info.release());
}

PYBIND11_MODULE(_pyhailort_internal, m) {
    ControlWrapper::add_to_python_module(m);
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