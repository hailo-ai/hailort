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

#ifndef _HAILO_INTERNAL_BUFFER_MANAGER_HPP_
#define _HAILO_INTERNAL_BUFFER_MANAGER_HPP_

#include "hailo/hailort.h"
#include "hailo/hef.hpp"
#include "common/utils.hpp"
#include "hef/layer_info.hpp"
#include "vdma/memory/vdma_buffer.hpp"
#include "internal_buffer_planner.hpp"


namespace hailort
{

#define MAX_EDGE_LAYERS_PER_CONTEXT (20)

class InternalBufferManager final
{
public:
    static Expected<std::shared_ptr<InternalBufferManager>> create(HailoRTDriver &driver,
        const ConfigureNetworkParams &config_params);

    hailo_status add_config_buffer_info(const uint16_t context_index, const size_t config_stream_index,
        const std::vector<uint32_t> &cfg_sizes);
    hailo_status add_layer_buffer_info(const LayerInfo &layer_info);
    ExpectedRef<EdgeLayerInfo> get_layer_buffer_info(const EdgeLayerKey &key);
    Expected<EdgeLayerBuffer> get_intermediate_buffer(const EdgeLayerKey &key);
    hailo_status plan_and_execute(InternalBufferPlanner::Type default_planner_type, const size_t number_of_contexts);

private:
    InternalBufferManager(HailoRTDriver &driver, const ConfigureNetworkParams &config_params);

    // Add buffer info phase functions
    void add_buffer_info(const EdgeLayerKey &edge_layer_key, const EdgeLayerInfo &buffer_info);
    hailo_status add_inter_context_buffer(const LayerInfo &layer_info);
    hailo_status add_ddr_buffer(const LayerInfo &layer_info);
    Expected<uint16_t> get_network_batch_size(const std::string &network_name) const;

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
    const ConfigureNetworkParams &m_config_params;
    // m_edge_layer_infos is filled by add_buffer_info API
    std::map<EdgeLayerKey, EdgeLayerInfo> m_edge_layer_infos;
    std::map<EdgeLayerKey, EdgeLayerBuffer> m_edge_layer_to_buffer_map;
};

} /* namespace hailort */

#endif /* _HAILO_INTERNAL_BUFFER_MANAGER_HPP_ */
