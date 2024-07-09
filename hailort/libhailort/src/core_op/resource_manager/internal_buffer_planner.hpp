/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file internal_buffer_manager.hpp
 * @brief Planner for all the internal buffers of the CoreOp
 *
 * The manager will hold all the internal buffers of the CoreOp.
 * The manager can optimize the memory consumption of the core op and provide API
 * about the total internal memory consumption.
 *
  **/

#ifndef _HAILO_INTERNAL_BUFFER_PLANNER_HPP_
#define _HAILO_INTERNAL_BUFFER_PLANNER_HPP_

#include "hailo/hef.hpp"
#include "common/utils.hpp"
#include "hef/layer_info.hpp"
#include "vdma/memory/vdma_buffer.hpp"
#include "vdma/memory/buffer_requirements.hpp"

namespace hailort
{

using EdgeLayerKey = std::pair<src_context_t, src_stream_index_t>;

struct EdgeLayerInfo {
    LayerType type;
    uint32_t transfer_size;
    uint16_t max_transfers_in_batch;
    uint16_t start_context;
    uint16_t end_context;
    bool reuse_buffer;
};

struct EdgeLayerBuffer {
    std::shared_ptr<vdma::VdmaBuffer> buffer;
    size_t offset;
};

struct BufferPlan {
    vdma::VdmaBuffer::Type buffer_type;
    size_t buffer_size;
    size_t total_edge_layer_size;
    std::vector<std::pair<EdgeLayerKey, size_t>> edge_layer_offsets;
    std::map<EdgeLayerKey, EdgeLayerInfo> edge_layer_infos;
};

struct BufferPlanReport {
    size_t cma_memory;
    size_t user_memory;
    size_t edge_layer_size;
    float memory_utilization_factor;
};

using InternalBufferPlanning = std::vector<BufferPlan>;


// BufferUsageSegment is a struct that represents a segment of a buffer that is used in a specific context
typedef struct {
    size_t offset;
    size_t size;
} BufferUsageSegment;

// ContextBufferUsageSegments represents all buffer segments that is used in a specific context
using ContextBufferUsageSegments = std::vector<BufferUsageSegment>;

class InternalBufferPlanner final
{
public:

    enum class Type {
        SINGLE_BUFFER_PER_BUFFER_TYPE = 0,
        SINGLE_SG_BUFFER,
        NAIVE_PER_BUFFER_TYPE,
        NAIVE_SG_BUFFER,

        // Must be last
        INVALID,
    };

    // Planning functions
    static Expected<InternalBufferPlanning> create_buffer_planning(
        const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layer_infos, Type plan_type,
        HailoRTDriver::DmaType dma_type, uint16_t max_page_size, size_t number_of_contexts);
    static Expected<InternalBufferPlanning> create_naive_buffer_planning(
        const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layer_infos, HailoRTDriver::DmaType dma_type,
        uint16_t max_page_size, bool force_sg_type_buffer = false);
    static Expected<InternalBufferPlanning> create_optimized_buffer_planning(
        const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layer_infos, HailoRTDriver::DmaType dma_type,
        uint16_t max_page_size, size_t number_of_contexts, bool force_sg_type_buffer = false);
    // Reporting functions
    static BufferPlanReport report_planning_info(const InternalBufferPlanning &buffer_planning);

    // Debug API
    static hailo_status change_edge_layer_buffer_offset(InternalBufferPlanning &buffer_planning, const EdgeLayerKey &edge_layer_key,
        size_t new_offset, uint16_t max_page_size);
    static Expected<size_t> get_edge_layer_buffer_offset(const InternalBufferPlanning &buffer_planning,
        const EdgeLayerKey &edge_layer_key);

private:

    // Helper functions
    static bool should_edge_layer_use_ccb(const LayerType &layer_type, HailoRTDriver::DmaType dma_type,
        bool force_sg_type_buffer);
    static std::vector<std::pair<EdgeLayerKey, EdgeLayerInfo>> sort_edge_layers_by_size(
        const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layers);
    static Expected<vdma::BufferSizesRequirements> return_buffer_requirements(
        const EdgeLayerInfo &edge_layer, const vdma::VdmaBuffer::Type buffer_type,
        uint16_t max_page_size);
    static Expected<EdgeLayerInfo> get_edge_info_from_buffer_plan(const InternalBufferPlanning &buffer_planning,
        const EdgeLayerKey &edge_layer_key);

    // Planning phase functions
    static ContextBufferUsageSegments merge_context_buffer_events(
        ContextBufferUsageSegments& combined, const ContextBufferUsageSegments& added_buffers);
    static size_t find_new_buffer_offset(const ContextBufferUsageSegments& unified_buffers, size_t new_buffer_size,
        uint16_t buffer_offset_alignment);
    static std::vector<BufferUsageSegment> build_availibility_map(
        const std::vector<ContextBufferUsageSegments> &context_buffer_usage_vector, uint16_t start_context, uint16_t end_context);
    static hailo_status add_edge_layer_to_planning(const std::pair<EdgeLayerKey, EdgeLayerInfo> &edge_layer,
        std::vector<std::vector<BufferUsageSegment>> &context_buffer_usage_vector, BufferPlan &buffer_plan,
        const vdma::VdmaBuffer::Type buffer_type, uint16_t max_page_size);


    static Expected<InternalBufferPlanning> create_single_buffer_planning(
        const std::map<EdgeLayerKey, EdgeLayerInfo> &sg_edge_layers, size_t number_of_contexts,
        const vdma::VdmaBuffer::Type buffer_type, uint16_t max_page_size);
};

} /* namespace hailort */

#endif /* _HAILO_INTERNAL_BUFFER_PLANNER_HPP_ */
