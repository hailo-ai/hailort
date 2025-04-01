/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "hailo/quantization.hpp"

#include "stream_common/stream_internal.hpp"
#include "hef/layer_info.hpp"

#include <map>
#include <vector>


namespace hailort
{

class HAILORTAPI TransformContextUtils final
{
public:
    static Expected<bool> is_transformation_required(const hailo_stream_direction_t stream_direction,
        const hailo_3d_image_shape_t &src_image_shape, 
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, 
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &quant_info);
    static Expected<bool> should_quantize(const hailo_stream_direction_t stream_direction, 
        const hailo_format_t &src_format, const hailo_format_t &dst_format);
    static bool should_transpose(const hailo_format_flags_t &src_flags, const hailo_format_flags_t &dst_flags);
    static bool should_reorder(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format);
    static bool should_pad_periph(const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format);
    static std::string make_quantization_description(hailo_format_type_t src_type, hailo_format_type_t dst_type,
                                                    const std::vector<hailo_quant_info_t> &quant_info);
    static std::string make_reorder_description(hailo_format_order_t src_order, hailo_3d_image_shape_t src_shape,
                                                hailo_format_order_t dst_order, hailo_3d_image_shape_t dst_shape);
    static std::string make_transpose_description(hailo_3d_image_shape_t original_shape, hailo_3d_image_shape_t transposed_shape);
    static std::string make_pad_periph_description(hailo_3d_image_shape_t src_shape, hailo_3d_image_shape_t dst_shape);

    template<typename T>
    static hailo_status transform__d2h_NHCW_to_NCHW(
        const T *src_ptr, hailo_3d_image_shape_t *src_image_shape,
        T *dst_ptr, hailo_3d_image_shape_t *dst_image_shape)
    {
        /* Validate arguments */
        ASSERT(NULL != src_ptr);
        ASSERT(NULL != dst_ptr);
        CHECK(src_image_shape->features == dst_image_shape->features, HAILO_INVALID_ARGUMENT,
            "NCHW_to_NHCW Transform features src/dst should be the same");
        CHECK(src_image_shape->height == dst_image_shape->height, HAILO_INVALID_ARGUMENT,
            "NCHW_to_NHCW Transform height src/dst should be the same");
        CHECK(dst_image_shape->width <= src_image_shape->width, HAILO_INVALID_ARGUMENT,
            "NCHW_to_NHCW Transform dst width should be smaller/equal than src width");
        CHECK(((src_image_shape->width * sizeof(T)) % HailoRTCommon::HW_DATA_ALIGNMENT) == 0, HAILO_INVALID_ARGUMENT,
            "NCHW_to_NHCW Transform src width must be aligned to {}", HailoRTCommon::HW_DATA_ALIGNMENT);

        size_t width_size = dst_image_shape->width;
        for (uint32_t r = 0; r < src_image_shape->height; r++) {
            for (uint32_t c = 0; c < src_image_shape->features; c++) {
                // Copy width
                T *dst = dst_ptr +
                    dst_image_shape->width * dst_image_shape->height * c +
                    dst_image_shape->width * r;
                const T *src = src_ptr +
                    src_image_shape->features * src_image_shape->width * r +
                    src_image_shape->width * c;

                std::copy_n(src, width_size, dst);
            }
        }

        return HAILO_SUCCESS;
    }
private:
    static Expected<bool> should_quantize_by_type(const hailo_stream_direction_t stream_direction,
        const hailo_format_type_t &src_format_type, const hailo_format_type_t &dst_format_type);
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

struct QuantInfoForDequantize
{
    float32_t m_qp_zp;
    float32_t m_qp_scale;
    QuantInfoForDequantize(float32_t qp_zp, float32_t qp_scale) : m_qp_zp(qp_zp), m_qp_scale(qp_scale)
    {}
};

class HAILORTAPI FrameOutputTransformContext final : public OutputTransformContext
{
public:
    static Expected<std::unique_ptr<OutputTransformContext>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_info);

    FrameOutputTransformContext(size_t src_frame_size, const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, size_t dst_frame_size, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_info, Buffer&& transpose_buffer,
        const bool should_quantize, const bool should_transpose, const bool should_reorder, const bool should_pad_periph);

    hailo_status transform_inner(const void *src_ptr, void *dst_ptr, MemoryView transpose_buffer);

    hailo_status quantize_stream(const void *dst_ptr);


    virtual hailo_status transform(const MemoryView src, MemoryView dst) override;
    virtual std::string description() const override;

private:
    template <typename T, typename Q>
    static inline void dequantize_output_by_feature(T *dst_ptr, uint32_t buffer_elements_count,
        const std::vector<QuantInfoForDequantize> &quant_infos, uint32_t repetition_count)
    {
        uint32_t elements_dequantized = 0;
        while (elements_dequantized < buffer_elements_count) {
            for (int32_t i = static_cast<int32_t>(quant_infos.size()) - 1; i >= 0; i--) {
                Quantization::dequantize_output_buffer_in_place<T, Q>(dst_ptr, buffer_elements_count - repetition_count - elements_dequantized,
                    repetition_count, quant_infos[i].m_qp_zp, quant_infos[i].m_qp_scale);
                elements_dequantized += repetition_count;
            }
        }
    }

    const hailo_3d_image_shape_t m_src_image_shape;
    const hailo_3d_image_shape_t m_dst_image_shape;
    Buffer m_transpose_buffer;
    bool m_are_all_qps_the_same;
    std::vector<QuantInfoForDequantize> m_quant_info_per_feature;
    uint32_t m_quant_infos_rep_count;
};

class HAILORTAPI NMSOutputTransformContext final : public OutputTransformContext
{
public:
    static Expected<std::unique_ptr<OutputTransformContext>> create(const hailo_format_t &src_format, 
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_info, const hailo_nms_info_t &nms_info);

    NMSOutputTransformContext(size_t src_frame_size, const hailo_format_t &src_format, size_t dst_frame_size,
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_info, const hailo_nms_info_t &nms_info, 
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