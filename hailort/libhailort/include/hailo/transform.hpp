/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file transform.hpp
 * @brief Pre/post infer transformations
 **/

#ifndef _HAILO_TRANSFORM_HPP_
#define _HAILO_TRANSFORM_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/buffer.hpp"
#include "hailo/hef.hpp"
#include "hailo/stream.hpp"

#include <map>
#include <vector>

/** hailort namespace */
namespace hailort
{

/*! Object used for input stream transformation*/
class HAILORTAPI InputTransformContext final
{
public:

    /**
     * Creates input transform_context.
     * 
     * @param[in] src_image_shape          The shape of the src buffer to be transformed.
     * @param[in] src_format               The format of the src buffer to be transformed.
     * @param[in] dst_image_shape          The shape of the dst buffer that receives the transformed data.
     * @param[in] dst_format               The format of the dst buffer that receives the transformed data.
     * @param[in] dst_quant_infos          A vector of ::hailo_quant_info_t object containing quantization information per feature.
     *                                     Might also contain a vector with a single ::hailo_quant_info_t object.
     * @return Upon success, returns Expected of a pointer to InputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<InputTransformContext>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos);

    /**
     * Creates input transform_context.
     * 
     * @param[in] src_image_shape          The shape of the src buffer to be transformed.
     * @param[in] src_format               The format of the src buffer to be transformed.
     * @param[in] dst_image_shape          The shape of the dst buffer that receives the transformed data.
     * @param[in] dst_format               The format of the dst buffer that receives the transformed data.
     * @param[in] dst_quant_info           A ::hailo_quant_info_t object containing quantization information.
     * @return Upon success, returns Expected of a pointer to InputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function is deprecated
     */
    static Expected<std::unique_ptr<InputTransformContext>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info)
        DEPRECATED("The use of a single hailo_quant_info_t in this function is deprecated. Please pass a vector of hailo_quant_info_t instead.");

    /**
     * Creates input transform_context.
     * 
     * @param[in] stream_info       Creates transform_context that fits this stream info.
     * @param[in] transform_params  A ::hailo_transform_params_t object containing user transformation parameters.
     * @return Upon success, returns Expected of a pointer to InputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<InputTransformContext>> create(const hailo_stream_info_t &stream_info,
        const hailo_transform_params_t &transform_params);

    /**
     * Creates input transform_context.
     * 
     * @param[in] stream_info    Creates transform_context that fits this stream info.
     * @param[in] unused         Unused.
     * @param[in] format_type    The type of the buffer sent to the transform_context.
     * @return Upon success, returns Expected of a pointer to InputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<InputTransformContext>> create(const hailo_stream_info_t &stream_info, bool unused,
        hailo_format_type_t format_type);

    /**
     * Creates input transform_context by output stream
     * 
     * @param[in] input_stream  Creates transform_context that fits this input stream.
     * @param[in] transform_params  A ::hailo_transform_params_t object containing user transformation parameters.
     * @return Upon success, returns Expected of a pointer to InputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<InputTransformContext>> create(InputStream &input_stream,
    const hailo_transform_params_t &transform_params);

    /**
     * Transforms an input frame referred by @a src directly to the buffer referred by @a dst.
     * 
     * @param[in]  src          A src buffer to be transformed.
     * @param[out] dst          A dst buffer that receives the transformed data.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status transform(const MemoryView src, MemoryView dst);

    /**
     * @return The size of the src frame on the host side in bytes.
     */
    size_t get_src_frame_size() const;

    /**
     * @return The size of the dst frame on the hw side in bytes.
     */
    size_t get_dst_frame_size() const;

    /**
     * Check whether or not a transformation is needed. 
     *
     * @param[in] src_image_shape          The shape of the src buffer (host shape).
     * @param[in] src_format               The format of the src buffer (host format).
     * @param[in] dst_image_shape          The shape of the dst buffer (hw shape).
     * @param[in] dst_format               The format of the dst buffer (hw format).
     * @param[in] quant_infos              A vector of ::hailo_quant_info_t object containing quantization information per feature.
     * @return Returns Expected of boolean, whether or not a transformation is needed.
     * @note In case the function returns false, the src frame is ready to be sent to HW without any transformation.
     */
    static Expected<bool> is_transformation_required(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
        const std::vector<hailo_quant_info_t> &quant_infos);

    /**
     * Check whether or not a transformation is needed. 
     *
     * @param[in] src_image_shape          The shape of the src buffer (host shape).
     * @param[in] src_format               The format of the src buffer (host format).
     * @param[in] dst_image_shape          The shape of the dst buffer (hw shape).
     * @param[in] dst_format               The format of the dst buffer (hw format).
     * @param[in] quant_info               A ::hailo_quant_info_t object containing quantization information.
     * @return Returns whether or not a transformation is needed.
     * @note In case the function returns false, the src frame is ready to be sent to HW without any transformation.
     * @note This function is deprecated.
     */
    static bool is_transformation_required(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
        const hailo_quant_info_t &quant_info)
        DEPRECATED("The use of a single hailo_quant_info_t in this function is deprecated. Please pass a vector of hailo_quant_info_t instead.");

    /**
     * @return A human-readable description of the transformation parameters.
     */
    virtual std::string description() const;

private:
    InputTransformContext(size_t src_frame_size, const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, size_t dst_frame_size, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, Buffer &&quant_buffer,
        Buffer &&transpose_buffer, const bool should_quantize, const bool should_transpose, const bool should_reorder,
        const bool should_pad_periph);

    inline MemoryView quant_buffer() {
        return MemoryView(m_quant_buffer);
    }

    inline MemoryView transpose_buffer() {
        return MemoryView(m_transpose_buffer);
    }

    hailo_status transform_inner(const void *src_ptr, void *quant_buffer, void *dst_ptr, 
        MemoryView transpose_buffer);

    hailo_status quantize_stream(const void *src_ptr, void *quant_buffer);

    const size_t m_src_frame_size;
    const hailo_3d_image_shape_t m_src_image_shape;
    const hailo_format_t m_src_format;
    const size_t m_dst_frame_size;
    const hailo_3d_image_shape_t m_dst_image_shape;
    const hailo_format_t m_dst_format;
    const std::vector<hailo_quant_info_t> m_dst_quant_infos;
    const bool m_should_quantize;
    const bool m_should_transpose;
    const bool m_should_reorder;
    const bool m_should_pad_periph;

    Buffer m_quant_buffer;
    Buffer m_transpose_buffer;
};

/*! Object used for output stream transformation*/
class HAILORTAPI OutputTransformContext
{
public:

    virtual ~OutputTransformContext() = default;
    OutputTransformContext(OutputTransformContext &&) = default;
    OutputTransformContext(const OutputTransformContext &) = delete;
    OutputTransformContext& operator=(const OutputTransformContext &) = delete;

    /**
     * Creates output transform_context.
     * 
     * @param[in] src_image_shape          The shape of the src buffer to be transformed.
     * @param[in] src_format               The format of the src buffer to be transformed.
     * @param[in] dst_image_shape          The shape of the dst buffer that receives the transformed data.
     * @param[in] dst_format               The format of the dst buffer that receives the transformed data.
     * @param[in] dst_quant_infos          A vector of ::hailo_quant_info_t object containing quantization information per feature.
     *                                     Might also contain a vector with a single ::hailo_quant_info_t object.
     * @param[in] nms_info                 A ::hailo_nms_info_t object containing nms information.
     * @return Upon success, returns Expected of a pointer to OutputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<OutputTransformContext>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info);

    /**
     * Creates output transform_context.
     * 
     * @param[in] src_image_shape          The shape of the src buffer to be transformed.
     * @param[in] src_format               The format of the src buffer to be transformed.
     * @param[in] dst_image_shape          The shape of the dst buffer that receives the transformed data.
     * @param[in] dst_format               The format of the dst buffer that receives the transformed data.
     * @param[in] dst_quant_info           A ::hailo_quant_info_t object containing quantization information.
     * @param[in] nms_info                 A ::hailo_nms_info_t object containing nms information.
     * @return Upon success, returns Expected of a pointer to OutputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note This function is deprecated.
     */
    static Expected<std::unique_ptr<OutputTransformContext>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape,
        const hailo_format_t &dst_format, const hailo_quant_info_t &dst_quant_info, const hailo_nms_info_t &nms_info)
        DEPRECATED("The use of a single hailo_quant_info_t in this function is deprecated. Please pass a vector of hailo_quant_info_t instead.");

    /**
     * Creates output transform_context.
     * 
     * @param[in] stream_info       Creates transform_context that fits this stream info.
     * @param[in] transform_params  A ::hailo_transform_params_t object containing user transformation parameters.
     * @return Upon success, returns Expected of a pointer to OutputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<OutputTransformContext>> create(const hailo_stream_info_t &stream_info,
        const hailo_transform_params_t &transform_params);

    /**
     * Creates output transform_context with default transform parameters
     * 
     * @param[in] stream_info    Creates transform_context that fits this stream info.
     * @param[in] unused         Unused.
     * @param[in] format_type    The type of the buffer returned from the transform_context
     * @return Upon success, returns Expected of a pointer to OutputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<OutputTransformContext>> create(const hailo_stream_info_t &stream_info, bool unused,
        hailo_format_type_t format_type);

    /**
     * Creates output transform_context by output stream
     * 
     * @param[in] output_stream  Creates transform_context that fits this output stream.
     * @param[in] transform_params  A ::hailo_transform_params_t object containing user transformation parameters.
     * @return Upon success, returns Expected of a pointer to OutputTransformContext.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<OutputTransformContext>> create(OutputStream &output_stream,
    const hailo_transform_params_t &transform_params);

    /**
     * Transforms an output frame referred by @a src directly to the buffer referred by @a dst.
     * 
     * @param[in]  src             A src buffer to be transformed.
     * @param[out] dst             A dst buffer that receives the transformed data.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status transform(const MemoryView src, MemoryView dst) = 0;

    /**
     * @return The size of the src frame on the hw side in bytes.
     */
    size_t get_src_frame_size() const;

    /**
     * @return The size of the dst frame on the host side in bytes.
     */
    size_t get_dst_frame_size() const;

    /**
     * Check whether or not a transformation is needed.
     * 
     * @param[in] src_image_shape          The shape of the src buffer (hw shape).
     * @param[in] src_format               The format of the src buffer (hw format).
     * @param[in] dst_image_shape          The shape of the dst buffer (host shape).
     * @param[in] dst_format               The format of the dst buffer (host format).
     * @param[in] quant_infos              A vector of ::hailo_quant_info_t object containing quantization information per feature.
     * @return Returns Expected of boolean, whether or not a transformation is needed.
     * @note In case the function returns false, the src frame is already in the required format without any transformation.
     */
    static Expected<bool> is_transformation_required(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, 
        const std::vector<hailo_quant_info_t> &quant_infos);

    /**
     * Check whether or not a transformation is needed.
     * 
     * @param[in] src_image_shape          The shape of the src buffer (hw shape).
     * @param[in] src_format               The format of the src buffer (hw format).
     * @param[in] dst_image_shape          The shape of the dst buffer (host shape).
     * @param[in] dst_format               The format of the dst buffer (host format).
     * @param[in] quant_info               A ::hailo_quant_info_t object containing quantization information.
     * @return Returns whether or not a transformation is needed.
     * @note In case the function returns false, the src frame is already in the required format without any transformation.
     * @note This function is deprecated.
     */
    static bool is_transformation_required(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, 
        const hailo_quant_info_t &quant_info)
        DEPRECATED("The use of a single hailo_quant_info_t in this function is deprecated. Please pass a vector of hailo_quant_info_t instead.");

    /**
     * @return A human-readable description of the transformation parameters.
     */
    virtual std::string description() const = 0;

protected:
    OutputTransformContext(size_t src_frame_size, const hailo_format_t &src_format, size_t dst_frame_size,
        const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, const bool should_quantize,
        const bool should_transpose, const bool should_reorder, const bool should_pad_periph);

    const size_t m_src_frame_size;
    const hailo_format_t m_src_format;
    const size_t m_dst_frame_size;
    const hailo_format_t m_dst_format;
    const std::vector<hailo_quant_info_t> m_dst_quant_infos;
    const bool m_should_quantize;
    const bool m_should_transpose;
    const bool m_should_reorder;
    const bool m_should_pad_periph;
};

/*! Object used to demux muxed stream */
class HAILORTAPI OutputDemuxer {
public:
    virtual ~OutputDemuxer() = default;

    OutputDemuxer(const OutputDemuxer &) = delete;
    OutputDemuxer& operator=(const OutputDemuxer &) = delete;
    OutputDemuxer(OutputDemuxer &&) = default;

    /**
     * Creates an output demuxer for the given @a output_stream.
     * 
     * @param[in] output_stream     An OutputStream object to create demuxer from.
     * @return Upon success, returns Expected of a pointer to OutputDemuxer.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::unique_ptr<OutputDemuxer>> create(OutputStream &output_stream);

    /**
     * @return The demuxer's edges information.
     */
    virtual std::vector<hailo_stream_info_t> get_edges_stream_info() = 0;

    /**
     * Demultiplexing an output frame referred by @a src directly into the buffers referred by @a dst_ptrs.
     * 
     * @param[in]  src             A buffer to be demultiplexed.
     * @param[out] dst_ptrs        A mapping of output_name to its resulted demuxed data.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status transform_demux(const MemoryView src, const std::map<std::string, MemoryView> &dst_ptrs) = 0;

    /**
     * Demultiplexing an output frame referred by @a src directly into the buffers referred by @a raw_buffers.
     * 
     * @param[in]  src               A buffer to be demultiplexed.
     * @param[out] raw_buffers       A vector of buffers that receives the demultiplexed data read from the stream.
     *                               The order of @a raw_buffers vector will remain as is.
     * @note The order of @a raw_buffers should be the same as returned from the function 'get_edges_stream_info()'.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status transform_demux(const MemoryView src, std::vector<MemoryView> &raw_buffers) = 0;

    const size_t src_frame_size;

protected:
    OutputDemuxer(size_t src_frame_size) : src_frame_size(src_frame_size) {}
};

/** @defgroup group_transform Transformations functions
 *  @{
 */

/**
 * Transposed @a src buffer (whose shape and format are given) to @a dst buffer (whose shape will be
 * the same as `shape` but the height and width are replaced)
 * 
 * @param[in]  src           A buffer containing the data to be transposed.
 * @param[in]  shape         The shape of @a src.
 * @param[in]  format        The format of @a src.
 * @param[out] dst           The dst buffer to contain the transposed data.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 * @note assumes src and dst not overlas
 */
HAILORTAPI hailo_status transpose_buffer(const MemoryView src, const hailo_3d_image_shape_t &shape,
    const hailo_format_t &format, MemoryView dst);

/**
 * @return The size of transpose buffer by its src shape and dst format type.
 */
HAILORTAPI inline size_t get_transpose_buffer_size(const hailo_3d_image_shape_t &src_shape, const hailo_format_type_t &dst_type)
{
    return HailoRTCommon::get_shape_size(src_shape) * HailoRTCommon::get_data_bytes(dst_type);
}

/**
 * Fuse multiple defused NMS buffers referred by @a buffers to the buffer referred by @a dst.
 * This function expects @a buffers to be ordered by their @a class_group_index (lowest to highest).
 * 
 * @param[in]  buffers                  A vector of buffers to be fused.
 * @param[in]  infos_of_buffers         A vector of ::hailo_nms_info_t containing the @a buffers information.
 * @param[out] dst                      A pointer to a buffer which will contain the fused buffer.
 * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
 */
HAILORTAPI hailo_status fuse_buffers(const std::vector<MemoryView> &buffers,
    const std::vector<hailo_nms_info_t> &infos_of_buffers, MemoryView dst);

/** @} */ // end of group_transform

} /* namespace hailort */

#endif /* _HAILO_TRANSFORM_HPP_ */
