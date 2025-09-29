/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline_internal.cpp
 * @brief Implemention of the pipeline elements
 **/
#include "net_flow/pipeline/pipeline_internal.hpp"
#include "net_flow/pipeline/async_infer_runner.hpp"

namespace hailort
{

PipelineElementInternal::PipelineElementInternal(const std::string &name, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
    PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElement(name, (async_pipeline ? async_pipeline->pipeline_unique_id() : 0), std::move(duration_collector), std::move(pipeline_status), pipeline_direction),
    m_async_pipeline(async_pipeline)
{}

std::string PipelineElementInternal::network_name() const
{
    if (auto async_pipeline = m_async_pipeline.lock()) {
        return async_pipeline->get_network_name();
    }
    return "";
}

void PipelineElementInternal::handle_non_recoverable_async_error(hailo_status error_status)
{
    hailo_status pipeline_status = m_pipeline_status->load();
    if ((HAILO_SUCCESS == pipeline_status) && (error_status != HAILO_SHUTDOWN_EVENT_SIGNALED)) {
        LOGGER__ERROR("Non-recoverable Async Infer Pipeline error. status error code: {}", error_status);
        m_is_terminating_element = true;
        if (auto async_pipeline = m_async_pipeline.lock()) {
            async_pipeline->shutdown(error_status);
        }
    }
}

IntermediateElement::IntermediateElement(const std::string &name, DurationCollector &&duration_collector,
    std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
    std::shared_ptr<AsyncPipeline> async_pipeline) :
    PipelineElementInternal(name, std::move(duration_collector), std::move(pipeline_status), pipeline_direction, async_pipeline)
{
    m_sinks.emplace_back(*this, name, PipelinePad::Type::SINK);
    m_sources.emplace_back(*this, name, PipelinePad::Type::SOURCE);
}

std::vector<PipelinePad*> IntermediateElement::execution_pads()
{
    std::vector<PipelinePad*> result{&next_pad()};
    return result;
}

} /* namespace hailort */
