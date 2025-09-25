/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hef.hpp
 * @brief Hef parsing and configuration functions
 **/

#ifndef _HAILO_LAYER_INFO_HPP_
#define _HAILO_LAYER_INFO_HPP_

#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/hailort_defaults.hpp"

#include "vdma/driver/hailort_driver.hpp"

#include "control_protocol.h"
#include <vector>
#include <memory>
#include <map>


namespace hailort
{

#define INVALID_PAD_INDEX (UINT32_MAX)
#define PERIPH_BYTES_PER_BUFFER_ALIGNMENT_SIZE (8)
#define PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE (512)
#define NMS_NUMBER_OF_QPS (2)

enum class LayerType
{
    NOT_SET = 0,
    BOUNDARY = 1,
    INTER_CONTEXT = 2,
    DDR = 3,
    CFG = 4,
    CACHE = 5
};

struct BufferIndices {
    uint32_t index;
    uint32_t cluster_index;
};

struct ConnectedContextInfo {
    uint16_t context_index;
    uint8_t dma_engine_index;
    uint8_t stream_index;
};

struct DdrInfo {
    // total_buffers_per_frame not same as core_buffer_per frame.
    //(In DDR core buffer per frame is 1). Used to calc total host descriptors_per_frame.
    uint16_t total_buffers_per_frame;
    uint16_t min_buffered_rows;
};

struct CacheBufferInfo {
    uint32_t cache_id;
    uint16_t batch_size;
};

struct LayerInfo {
    LayerType type = LayerType::NOT_SET;
    hailo_stream_direction_t direction;
    uint8_t stream_index;
    uint8_t dma_engine_index;
    std::string name;
    std::string network_name;
    uint8_t network_index;
    CONTROL_PROTOCOL__nn_stream_config_t nn_stream_config;
    uint32_t max_shmifo_size;
    uint16_t context_index;
    uint32_t pad_index = INVALID_PAD_INDEX;

    // Transformation and shape info
    hailo_3d_image_shape_t shape;
    hailo_3d_image_shape_t hw_shape;
    uint32_t hw_data_bytes;
    hailo_format_t format;
    hailo_quant_info_t quant_info; // TODO: Remove, use vector
    std::vector<hailo_quant_info_t> quant_infos;
    hailo_nms_info_t nms_info;

    // Mux info
    bool is_mux;
    std::vector<LayerInfo> predecessor;
    uint32_t height_gcd;
    std::vector<uint32_t> height_ratios;

    // Multi planes info
    bool is_multi_planar;
    std::vector<LayerInfo> planes;
    uint8_t plane_index; // relevant for the underlying planes only

    // Defused nms info
    bool is_defused_nms;
    // TODO HRT-4441 change fused_layer from vector.
    std::vector<LayerInfo> fused_nms_layer;

    // Simulation Info
    BufferIndices buffer_indices;

    // Context switch info TODO: we should use std::optional for this structures (or implement our self).
    ConnectedContextInfo connected_context_info;
    DdrInfo ddr_info;
    CacheBufferInfo cache_info;
};

// LayerIdentifier = <LayerType, hailo_stream_direction_t, layer_name, stream_index>
using LayerIdentifier = std::tuple<LayerType, hailo_stream_direction_t, std::string, uint8_t>;

inline LayerIdentifier to_layer_identifier(const LayerInfo &info)
{
    return std::make_tuple(info.type, info.direction, info.name, info.stream_index);
}

class LayerInfoUtils {
public:
    static std::vector<hailo_stream_info_t> get_stream_infos_from_layer_info(const LayerInfo &layer_info)
    {
        std::vector<hailo_stream_info_t> res = {};
        size_t number_of_streams = (layer_info.is_multi_planar) ? layer_info.planes.size() : 1;
        res.reserve(number_of_streams);
        for (size_t i = 0; i < number_of_streams; i++) {
            auto &layer = (layer_info.is_multi_planar) ? layer_info.planes[i] : layer_info;
            hailo_stream_info_t stream_info = {};
            stream_info.hw_data_bytes = layer.hw_data_bytes;
            stream_info.format = layer.format;
            if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == stream_info.format.order) {
                stream_info.nms_info = layer.nms_info;
                stream_info.hw_frame_size =
                    HailoRTCommon::get_nms_hw_frame_size(stream_info.nms_info);
            } else {
                stream_info.shape.height = layer.shape.height;
                stream_info.shape.width = layer.shape.width;
                stream_info.shape.features = layer.shape.features;
                stream_info.hw_shape.height = layer.hw_shape.height;
                stream_info.hw_shape.width = layer.hw_shape.width;
                stream_info.hw_shape.features = layer.hw_shape.features;
                stream_info.hw_frame_size = HailoRTCommon::get_periph_frame_size(stream_info.hw_shape, stream_info.format);
            }
            stream_info.direction = layer.direction;
            stream_info.index = layer.stream_index;
            assert(layer.name.length() < HAILO_MAX_NAME_SIZE);
            strncpy(stream_info.name, layer.name.c_str(), layer.name.length() + 1);
            stream_info.quant_info = layer.quant_info;
            stream_info.is_mux = layer.is_mux;
            res.push_back(stream_info);
        }

        return res;
    }

    static bool vstream_info_already_in_vector(const std::vector<hailo_vstream_info_t> &vec, const std::string &name)
    {
        for (const auto &info : vec) {
            if (name == info.name) {
                return true;
            }
        }
        return false;
    }

    static std::vector<hailo_vstream_info_t> get_vstream_infos_from_layer_info(const LayerInfo &layer_info)
    {
        std::vector<hailo_vstream_info_t> res = {};
        if (layer_info.is_mux) {
            for (auto &pred : layer_info.predecessor) {
                auto vstream_infos = get_vstream_infos_from_layer_info(pred);
                res.insert(res.end(), vstream_infos.begin(), vstream_infos.end());
            }
        } else if (layer_info.is_defused_nms) {
            for (auto &fused_nms : layer_info.fused_nms_layer) {
                // In case of fused nms layers, several LayerInfos will contain data about the same fused layer
                if (!vstream_info_already_in_vector(res, fused_nms.name)) {
                    auto vstream_info = get_vstream_info_from_layer_info_impl(fused_nms);
                    res.push_back(vstream_info);
                }
            }
        } else {
            auto vstream_info = get_vstream_info_from_layer_info_impl(layer_info);
            res.push_back(vstream_info);
        }

        return res;
    }

    static Expected<size_t> get_transfer_size(const LayerInfo &layer_info) {
        switch (layer_info.type) {
        case LayerType::BOUNDARY:
            if (is_nms_burst_layer(layer_info)) {
                return get_nms_layer_transfer_size(layer_info);
            }
            return layer_info.nn_stream_config.periph_bytes_per_buffer * layer_info.nn_stream_config.periph_buffers_per_frame;
        case LayerType::INTER_CONTEXT:
        case LayerType::DDR:
            return layer_info.nn_stream_config.periph_bytes_per_buffer * layer_info.nn_stream_config.periph_buffers_per_frame;
        default:
            return make_unexpected(HAILO_NOT_IMPLEMENTED);
        }
    }

    /**
     * Gets stream's transfer size in bytes by stream info and layer info params.
     *
     * @param[in] stream_info         A ::hailo_stream_info_t object.
     * @param[in] layer_info          A ::LayerInfo object.
     * @return The streams's transfer size in bytes.
     */
    static constexpr uint32_t get_stream_transfer_size(const hailo_stream_info_t &stream_info, const LayerInfo &layer_info)
    {
        if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == layer_info.format.order) {
            return get_nms_layer_transfer_size(layer_info);
        }
        return stream_info.hw_frame_size;
    }

    /**
     * Get NMS layers's transfer size in bytes by NMS.
     *
     * @param[in] layer_info          A ::LayerInfo object.
     * @return The layer's transfer size in bytes.
     */
    static constexpr uint32_t get_nms_layer_transfer_size(const LayerInfo &layer_info)
    {
        switch (layer_info.nms_info.burst_type) {
            // If No Burst mode - size of transfer is size of bbox
            case HAILO_BURST_TYPE_H8_BBOX:
            case HAILO_BURST_TYPE_H15_BBOX:
                return layer_info.nms_info.bbox_size;
            // In hailo8 per class and hailo15 per class mode - check if can support interrupt per frame and if not do interrupt per burst
            case HAILO_BURST_TYPE_H8_PER_CLASS:
            case HAILO_BURST_TYPE_H15_PER_CLASS:
            {
                // In case of hailo8 - nn-core adds one delimeter per burst - in case of hailo15 nn-core adds delimeter and image delimeter per class
                const size_t bboxes_needed_for_delimeter = (HAILO_BURST_TYPE_H8_PER_CLASS == layer_info.nms_info.burst_type) ?
                    1 : 2;
                // If burst size is bigger than max bboxes per class + bboxes_needed_for_delimeter - we can enable 1 interrupt per frame
                // Becasue we know output size will be burst size * num classes
                if (layer_info.nms_info.burst_size >= (layer_info.nms_info.max_bboxes_per_class + bboxes_needed_for_delimeter)) {
                    return layer_info.nms_info.burst_size * layer_info.nms_info.bbox_size *
                        layer_info.nms_info.number_of_classes * layer_info.nms_info.chunks_per_frame;
                } else {
                    // support regular interrupt per burst
                    return layer_info.nms_info.burst_size * layer_info.nms_info.bbox_size;
                }
            }
            // Currently HAILO_BURST_TYPE_H15_PER_FRAME mode isnt supported - Shouldn't reach here
            case HAILO_BURST_TYPE_H15_PER_FRAME:
            default:
                assert(false);
                return 0;
        }
    }

    static constexpr size_t get_nms_layer_max_transfers_per_frame(const LayerInfo &layer_info)
    {
        const auto &nms_info = layer_info.nms_info;
        switch (nms_info.burst_type) {
            // If No Burst mode - size of transfer is size of bbox
            case HAILO_BURST_TYPE_H8_BBOX:
            case HAILO_BURST_TYPE_H15_BBOX:
                return nms_info.number_of_classes * nms_info.max_bboxes_per_class * nms_info.chunks_per_frame;
            // In hailo8 per class and hailo15 per class mode - check if can support interrupt per frame and if not do interrupt per burst
            case HAILO_BURST_TYPE_H8_PER_CLASS:
            case HAILO_BURST_TYPE_H15_PER_CLASS:
            {
                // In case of hailo8 - nn-core adds one delimeter per burst - in case of hailo15 nn-core adds delimeter and image delimeter per class
                const size_t bboxes_needed_for_delimeter = (HAILO_BURST_TYPE_H8_PER_CLASS == nms_info.burst_type) ?
                    1 : 2;
                const size_t max_bboxes_per_class = nms_info.max_bboxes_per_class + bboxes_needed_for_delimeter;
                const size_t bursts_per_class = (max_bboxes_per_class + nms_info.burst_size - 1) / nms_info.burst_size;
                return bursts_per_class * nms_info.number_of_classes * nms_info.chunks_per_frame;
            }
            // Currently HAILO_BURST_TYPE_H15_PER_FRAME mode isnt supported - Shouldn't reach here
            case HAILO_BURST_TYPE_H15_PER_FRAME:
            default:
                assert(false);
                return 0;
        }
    }

    /**
     * Return if layer is NMS Burst layers.
     *
     * @param[in] layer_info          A ::LayerInfo object.
     * @return True if the layer is NMS layer with burst mode - false otherwise.
     */
    static constexpr uint32_t is_nms_burst_layer(const LayerInfo &layer_info)
    {
        return (1 < layer_info.nms_info.burst_size);
    }

    /**
     * Get layers's transfer size.
     *
     * @param[in] layer_info          A ::LayerInfo object.
     * @return The layer's transfer size in bytes.
     */
    static constexpr uint32_t get_layer_transfer_size(const LayerInfo &layer_info)
    {
        if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == layer_info.format.order) {
            return get_nms_layer_transfer_size(layer_info);
        }
        return HailoRTCommon::get_periph_frame_size(layer_info.hw_shape, layer_info.format);
    }

private:
    static hailo_vstream_info_t get_vstream_info_from_layer_info_impl(const LayerInfo &layer_info)
    {
        hailo_vstream_info_t res = {};
        res.format.type = layer_info.format.type;
        res.format.flags = layer_info.format.flags;
        // If a layer is multi-planar, its format_order is already the host-side format order
        res.format.order = (layer_info.is_multi_planar) ? layer_info.format.order : HailoRTDefaults::get_default_host_format_order(layer_info.format);
        if (HailoRTCommon::is_nms(res)) {
            res.nms_shape.max_bboxes_per_class = layer_info.nms_info.max_bboxes_per_class * layer_info.nms_info.chunks_per_frame;
            res.nms_shape.number_of_classes = layer_info.nms_info.number_of_classes;
            res.nms_shape.max_bboxes_total = res.nms_shape.max_bboxes_per_class * layer_info.nms_info.number_of_classes;
            res.format.type = HAILO_FORMAT_TYPE_FLOAT32; // NMS on vstream is always float32s
        } else {
            res.shape.height = layer_info.shape.height;
            res.shape.width = layer_info.shape.width;
            res.shape.features = layer_info.shape.features;
        }
        res.direction = layer_info.direction;
        assert(layer_info.name.length() < HAILO_MAX_STREAM_NAME_SIZE);
        strncpy(res.name, layer_info.name.c_str(), layer_info.name.length() + 1);
        assert(layer_info.network_name.length() < HAILO_MAX_NETWORK_NAME_SIZE);
        strncpy(res.network_name, layer_info.network_name.c_str(), layer_info.network_name.length() + 1);
        res.quant_info = layer_info.quant_info;

        return res;
    }
};

} /* namespace hailort */

#endif /* _HAILO_LAYER_INFO_HPP_ */
