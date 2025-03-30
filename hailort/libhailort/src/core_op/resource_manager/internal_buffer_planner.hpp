/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "hef/core_op_metadata.hpp"
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
};

struct EdgeLayerPlan {
    EdgeLayerKey key;
    size_t offset;
    vdma::BufferSizesRequirements buffer_requirements;
};

struct BufferPlan {
    vdma::VdmaBuffer::Type buffer_type;
    size_t buffer_size;
    std::vector<EdgeLayerPlan> edge_layer_plans;
};

struct BufferPlanReport {
    // Amount of CMA memory (Physically continous) in bytes needed for execution
    size_t cma_memory;

    // Amount of CMA memory (Physically continous) in bytes needed for creating descriptors list.
    size_t cma_memory_for_descriptors;

    // Amount of pinned memory (Memory pinned to physical memory) in bytes needed for execution
    size_t pinned_memory;

    // Total size of all edge layers in bytes
    size_t edge_layer_size;

    // How much memory is used compared to the edge layer size
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

    static Expected<std::map<EdgeLayerKey, EdgeLayerInfo>> get_edge_layer_infos(const CoreOpMetadata& core_op,
        const ConfigureNetworkParams& config_params);

    // Planning functions
    static Expected<InternalBufferPlanning> create_buffer_planning(
        const CoreOpMetadata& core_op, uint16_t batch_size, Type plan_type, HailoRTDriver::DmaType dma_type,
        uint16_t max_page_size);
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

private:

    // Helper functions
    static bool should_edge_layer_use_ccb(const LayerType &layer_type, HailoRTDriver::DmaType dma_type,
        bool force_sg_type_buffer);
    static std::vector<std::pair<EdgeLayerKey, EdgeLayerInfo>> sort_edge_layers_by_size(
        const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layers);
    static Expected<vdma::BufferSizesRequirements> return_buffer_requirements(
        const EdgeLayerInfo &edge_layer, const vdma::VdmaBuffer::Type buffer_type,
        uint16_t max_page_size);

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
