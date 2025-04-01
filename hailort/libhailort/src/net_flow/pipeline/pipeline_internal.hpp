/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline_internal.hpp
 * @brief Hailo Infer Pipeline elements
 **/

#ifndef _HAILO_PIPELINE_ELEMENTS_HPP_
#define _HAILO_PIPELINE_ELEMENTS_HPP_

#include "net_flow/pipeline/pipeline.hpp"

#include "common/barrier.hpp"

namespace hailort
{

class AsyncPipeline; // Forward declaration

struct ElementBuildParams
{
    std::shared_ptr<std::atomic<hailo_status>> pipeline_status;
    std::chrono::milliseconds timeout;
    EventPtr shutdown_event;
    size_t buffer_pool_size_internal;
    size_t buffer_pool_size_edges;
    hailo_pipeline_elem_stats_flags_t elem_stats_flags;
    hailo_vstream_stats_flags_t vstream_stats_flags;
};

class PipelineElementInternal : public PipelineElement
{
public:
    PipelineElementInternal(const std::string &name, DurationCollector &&duration_collector,
                    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    PipelineElementInternal &operator=(PipelineElementInternal &&other) = delete;

protected:
    void handle_non_recoverable_async_error(hailo_status error_status);
    std::weak_ptr<AsyncPipeline> m_async_pipeline;

    friend class PipelinePad;
};

// Transfers data from one pad to another pad. Has one sink pad and one source pad.
class IntermediateElement : public PipelineElementInternal
{
public:
    IntermediateElement(const std::string &name, DurationCollector &&duration_collector,
                        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
                        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual PipelinePad &next_pad() = 0;

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
};

enum class AccumulatorType
{
    FPS,
    LATENCY,
    QUEUE_SIZE
};

} /* namespace hailort */

#endif /* _HAILO_PIPELINE_ELEMENTS_HPP_ */
