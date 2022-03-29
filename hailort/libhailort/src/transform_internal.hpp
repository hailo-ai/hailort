/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file transform_internal.hpp
 * @brief Pre/post infer transformations
 **/

#ifndef _TRANSFORM_INTERNAL_HPP_
#define _TRANSFORM_INTERNAL_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/buffer.hpp"
#include "hailo/hef.hpp"
#include "hailo/transform.hpp"
#include "stream_internal.hpp"
#include "layer_info.hpp"

#include <map>
#include <vector>

namespace hailort
{

class HAILORTAPI TransformContextUtils final
{
public:
    static bool is_transformation_required(const hailo_stream_direction_t stream_direction,
        const hailo_3d_image_shape_t &src_image_shape, 
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, 
        const hailo_format_t &dst_format, const hailo_quant_info_t &quant_info);
    static bool should_quantize(const hailo_stream_direction_t stream_direction, 
        const hailo_format_t &src_format, const hailo_format_t &dst_format, const hailo_quant_info_t &quant_info);
    static bool should_transpose(const hailo_format_flags_t &src_flags, const hailo_format_flags_t &dst_flags);
    static bool should_reorder(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format);
    static std::string make_quantization_description(hailo_format_type_t src_type, hailo_format_type_t dst_type,
                                                    hailo_quant_info_t quant_info);
    static std::string make_reorder_description(hailo_format_order_t src_order, hailo_3d_image_shape_t src_shape,
                                                hailo_format_order_t dst_order, hailo_3d_image_shape_t dst_shape);
    static std::string make_transpose_description(hailo_3d_image_shape_t original_shape, hailo_3d_image_shape_t transposed_shape);
};

class OutputDemuxerBase : public OutputDemuxer {
public:
    static Expected<OutputDemuxerBase> create(size_t src_frame_size, const LayerInfo &layer_info);

    virtual std::vector<hailo_stream_info_t> get_edges_stream_info() override
    {
        std::vector<hailo_stream_info_t> res;
        for (auto &info : m_mux_infos) {
            if (!info.info.is_mux) {
                res.push_back(info.info);
            }
        }
        return res;
    }

    virtual hailo_status transform_demux(const MemoryView src, const std::map<std::string, MemoryView> &dst_ptrs) override;
    virtual hailo_status transform_demux(const MemoryView src, std::vector<MemoryView> &raw_buffers) override;

private:
    OutputDemuxerBase(size_t src_frame_size, std::vector<hailo_mux_info_t> &&mux_infos);

    static Expected<std::vector<hailo_mux_info_t>> get_mux_infos_from_layer_info(const LayerInfo &layer_info);
    static hailo_status get_mux_info_from_layer_info_impl(hailo_mux_info_t &mux_info, const LayerInfo &layer_info,
    uint32_t &offset, uint32_t height_ratio, std::vector<hailo_mux_info_t> &res, size_t &number_of_mux_infos);

    std::vector<hailo_mux_info_t> m_mux_infos;
};

class HAILORTAPI FrameOutputTransformContext final : public OutputTransformContext
{
public:
    static Expected<std::unique_ptr<OutputTransformContext>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info);

    FrameOutputTransformContext(size_t src_frame_size, const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, size_t dst_frame_size, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, Buffer&& transpose_buffer,
        const bool should_quantize, const bool should_transpose, const bool should_reorder);

    hailo_status transform_inner(const void *src_ptr, void *dst_ptr, MemoryView transpose_buffer);

    hailo_status quantize_stream(const void *dst_ptr);


    virtual hailo_status transform(const MemoryView src, MemoryView dst) override;
    virtual std::string description() const override;

private:
    const hailo_3d_image_shape_t m_src_image_shape;
    const hailo_3d_image_shape_t m_dst_image_shape;
    Buffer m_transpose_buffer;
};

class HAILORTAPI NMSOutputTransformContext final : public OutputTransformContext
{
public:
    static Expected<std::unique_ptr<OutputTransformContext>> create(const hailo_format_t &src_format, 
        const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, const hailo_nms_info_t &nms_info);

    NMSOutputTransformContext(size_t src_frame_size, const hailo_format_t &src_format, size_t dst_frame_size,
        const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, const hailo_nms_info_t &nms_info, 
        Buffer &&quant_buffer, const bool should_quantize, const bool should_transpose);

    virtual hailo_status transform(const MemoryView src, MemoryView dst) override;
    virtual std::string description() const override;

private:

    const hailo_nms_info_t m_nms_info;

    // For each chunk contains offset of current nms class. Used here in order to avoid run-time allocations
    std::vector<size_t> m_chunk_offsets;
    Buffer m_quant_buffer;
};

} /* namespace hailort */

#endif /* _TRANSFORM_INTERNAL_HPP_ */