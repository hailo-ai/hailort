/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file continuous_edge_layer.cpp
 * @brief Continuous physical vdma edge layer.
 **/

#include "continuous_edge_layer.hpp"

namespace hailort {
namespace vdma {


Expected<std::unique_ptr<VdmaEdgeLayer>> ContinuousEdgeLayer::create_unique(
    std::shared_ptr<ContinuousVdmaBuffer> &&buffer, size_t size, size_t offset, uint16_t page_size, uint32_t num_pages)
{
    CHECK(buffer->size() >= (offset + size), HAILO_INTERNAL_FAILURE,
        "Edge-layer (size: {}, offset: {}) does not fit in buffer (size: {})", size, offset, buffer->size());

    auto edge_layer = ContinuousEdgeLayer(std::move(buffer), size, offset, page_size, num_pages);
    auto edge_layer_ptr = make_unique_nothrow<ContinuousEdgeLayer>(std::move(edge_layer));
    CHECK_NOT_NULL(edge_layer_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<VdmaEdgeLayer>(std::move(edge_layer_ptr));
}

}; /* namespace vdma */
}; /* namespace hailort */
