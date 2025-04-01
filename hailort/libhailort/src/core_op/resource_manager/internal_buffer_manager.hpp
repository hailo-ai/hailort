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

#ifndef _HAILO_INTERNAL_BUFFER_MANAGER_HPP_
#define _HAILO_INTERNAL_BUFFER_MANAGER_HPP_

#include "common/utils.hpp"
#include "hef/layer_info.hpp"
#include "vdma/memory/vdma_buffer.hpp"
#include "internal_buffer_planner.hpp"


namespace hailort
{

#define MAX_EDGE_LAYERS_PER_CONTEXT (20)

struct EdgeLayerBuffer {
    std::shared_ptr<vdma::VdmaBuffer> buffer;
    EdgeLayerPlan edge_layer_plan;
};

class InternalBufferManager final
{
public:
    static Expected<std::shared_ptr<InternalBufferManager>> create(HailoRTDriver &driver);

    Expected<EdgeLayerBuffer> get_intermediate_buffer(const EdgeLayerKey &key);
    hailo_status plan_and_execute(const std::map<EdgeLayerKey, EdgeLayerInfo> &edge_layer_infos,
        InternalBufferPlanner::Type default_planner_type, size_t number_of_contexts);

private:
    InternalBufferManager(HailoRTDriver &driver);

    // Execute phase functions
    hailo_status execute_plan(InternalBufferPlanning &buffer_planning,
        std::vector<EdgeLayerKey> &edge_layers_executed, InternalBufferPlanning &buffers_executed);
    Expected<std::shared_ptr<vdma::VdmaBuffer>> create_intermediate_buffer(
        vdma::VdmaBuffer::Type &buffer_type, const size_t buffer_size);
    Expected<std::shared_ptr<vdma::VdmaBuffer>> create_intermediate_ccb_buffer(
        const size_t buffer_size);
    Expected<std::shared_ptr<vdma::VdmaBuffer>> create_intermediate_sg_buffer(
        const size_t buffer_size);

    // Reporting functions
    void print_execution_results(const BufferPlanReport &default_planner_report,
        bool default_planner_meet_requirements, const BufferPlanReport &executed_buffers_report);

    HailoRTDriver &m_driver;
    std::map<EdgeLayerKey, EdgeLayerBuffer> m_edge_layer_to_buffer_map;
};

} /* namespace hailort */

#endif /* _HAILO_INTERNAL_BUFFER_MANAGER_HPP_ */
