/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#include "hailort_defaults.hpp"

#include <vector>
#include <memory>
#include <map>

namespace hailort
{

struct BufferIndices {
    uint32_t index;
    uint32_t cluster_index;
};

struct LayerInfo {
    bool is_mux;
    std::vector<LayerInfo> predecessor;
    bool is_defused_nms;
    // TODO HRT-4441 change fused_layer from vector.
    std::vector<LayerInfo> fused_nms_layer;
    hailo_3d_image_shape_t shape;
    hailo_3d_image_shape_t hw_shape;
    uint32_t hw_data_bytes;
    hailo_format_t format;
    hailo_stream_direction_t direction;
    uint8_t index;
    std::string name;
    hailo_quant_info_t quant_info;
    hailo_nms_info_t nms_info;
    uint32_t height_gcd;
    std::vector<uint32_t> height_ratios;
    // Note - network name as seen by the user is build in the following way - "network_group_name / partial_network name"
    // In layer info scope - we would use the partial network name only. 
    std::string partial_network_name;

    // Simulation Info
    BufferIndices buffer_indices;

    // HW Info
    uint16_t core_bytes_per_buffer;
    uint16_t core_buffers_per_frame;
};

class LayerInfoUtils {
public:
    static hailo_stream_info_t get_stream_info_from_layer_info(const LayerInfo &layer_info)
    {
        hailo_stream_info_t res = {};
        res.hw_data_bytes = layer_info.hw_data_bytes;
        res.format = layer_info.format;
        if (HAILO_FORMAT_ORDER_HAILO_NMS == res.format.order) {
            res.nms_info = layer_info.nms_info;
            res.hw_frame_size =
                HailoRTCommon::get_nms_hw_frame_size(res.nms_info);
        } else {
            res.shape.height = layer_info.shape.height;
            res.shape.width = layer_info.shape.width;
            res.shape.features = layer_info.shape.features;
            res.hw_shape.height = layer_info.hw_shape.height;
            res.hw_shape.width = layer_info.hw_shape.width;
            res.hw_shape.features = layer_info.hw_shape.features;
            res.hw_frame_size =
                res.hw_shape.height * res.hw_shape.width * res.hw_shape.features * res.hw_data_bytes;
        }
        res.direction = layer_info.direction;
        res.index = layer_info.index;
        assert(layer_info.name.length() < HAILO_MAX_NAME_SIZE);
        strncpy(res.name, layer_info.name.c_str(), layer_info.name.length() + 1);
        res.quant_info = layer_info.quant_info;
        res.is_mux = layer_info.is_mux;

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

    static std::vector<hailo_vstream_info_t> get_vstream_infos_from_layer_info(const LayerInfo &layer_info, const std::string &network_group_name)
    {
        std::vector<hailo_vstream_info_t> res = {};
        if (layer_info.is_mux) {
            for (auto &pred : layer_info.predecessor) {
                auto vstream_infos = get_vstream_infos_from_layer_info(pred, network_group_name);
                res.insert(res.end(), vstream_infos.begin(), vstream_infos.end());
            }
        } else if (layer_info.is_defused_nms) {
            for (auto &fused_nms : layer_info.fused_nms_layer) {
                // In case of fused nms layers, several LayerInfos will contain data about the same fused layer
                if (!vstream_info_already_in_vector(res, fused_nms.name)) {
                    auto vstream_info = get_vstream_info_from_layer_info_impl(fused_nms, network_group_name);
                    res.push_back(vstream_info);
                }
            }
        } else {
            auto vstream_info = get_vstream_info_from_layer_info_impl(layer_info, network_group_name);
            res.push_back(vstream_info);
        }

        return res;
    }

private:
    static hailo_vstream_info_t get_vstream_info_from_layer_info_impl(const LayerInfo &layer_info, const std::string &network_group_name)
    {
        hailo_vstream_info_t res = {};
        res.format.type = layer_info.format.type;
        res.format.flags = layer_info.format.flags;
        res.format.order = HailoRTDefaults::get_default_host_format_order(layer_info.format);
        if (HAILO_FORMAT_ORDER_HAILO_NMS == res.format.order) {
            res.nms_shape.max_bboxes_per_class = layer_info.nms_info.max_bboxes_per_class * layer_info.nms_info.chunks_per_frame;
            res.nms_shape.number_of_classes = layer_info.nms_info.number_of_classes;
        } else {
            res.shape.height = layer_info.shape.height;
            res.shape.width = layer_info.shape.width;
            res.shape.features = layer_info.shape.features;
        }
        res.direction = layer_info.direction;
        assert(layer_info.name.length() < HAILO_MAX_STREAM_NAME_SIZE);
        strncpy(res.name, layer_info.name.c_str(), layer_info.name.length() + 1);
        auto network_name = network_group_name + HAILO_DEFAULT_NETWORK_NAME_QUALIFIER + layer_info.partial_network_name;
        assert(network_name.length() < HAILO_MAX_NETWORK_NAME_SIZE);
        strncpy(res.network_name, network_name.c_str(), network_name.length() + 1);
        res.quant_info = layer_info.quant_info;

        return res;
    }
};

} /* namespace hailort */

#endif /* _HAILO_LAYER_INFO_HPP_ */
