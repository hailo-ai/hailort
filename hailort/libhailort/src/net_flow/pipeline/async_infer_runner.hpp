/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_infer_runner.hpp
 * @brief Implementation of the async HL infer
 **/

#ifndef _HAILO_ASYNC_INFER_RUNNER_HPP_
#define _HAILO_ASYNC_INFER_RUNNER_HPP_

#include "hailo/infer_model.hpp"
#include "network_group/network_group_internal.hpp"
#include "net_flow/pipeline/pipeline.hpp"
#include "net_flow/pipeline/async_pipeline_builder.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"
#include "net_flow/ops/op.hpp"

namespace hailort
{

class AsyncPipeline
{
public:
    static Expected<std::shared_ptr<AsyncPipeline>> create_shared();
    AsyncPipeline &operator=(const AsyncPipeline &) = delete;
    AsyncPipeline();
    virtual ~AsyncPipeline() = default;

    void add_element_to_pipeline(std::shared_ptr<PipelineElement> pipeline_element);
    void set_async_hw_element(std::shared_ptr<AsyncHwElement> async_hw_element);
    void add_entry_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &input_name);
    void add_last_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &output_name);
    void set_build_params(ElementBuildParams &build_params);
    void shutdown(hailo_status error_status);

    const std::vector<std::shared_ptr<PipelineElement>>& get_pipeline() const;
    const std::unordered_map<std::string, std::shared_ptr<PipelineElement>>& get_entry_elements() const;
    const std::unordered_map<std::string, std::shared_ptr<PipelineElement>>& get_last_elements() const;
    const std::shared_ptr<AsyncHwElement> get_async_hw_element();
    const ElementBuildParams get_build_params();
    std::shared_ptr<std::atomic<hailo_status>> get_pipeline_status();

    void set_as_multi_planar();
    bool is_multi_planar();

private:
    std::shared_ptr<AsyncHwElement> m_async_hw_element;
    std::vector<std::shared_ptr<PipelineElement>> m_pipeline_elements;
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> m_entry_elements;
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> m_last_elements;
    ElementBuildParams m_build_params;
    bool m_is_multi_planar;
};

class AsyncInferRunnerImpl
{
public:
    static Expected<std::shared_ptr<AsyncInferRunnerImpl>> create(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
        const uint32_t timeout = HAILO_DEFAULT_ASYNC_INFER_TIMEOUT_MS);
    AsyncInferRunnerImpl(AsyncInferRunnerImpl &&) = delete;
    AsyncInferRunnerImpl(const AsyncInferRunnerImpl &) = delete;
    AsyncInferRunnerImpl &operator=(AsyncInferRunnerImpl &&) = delete;
    AsyncInferRunnerImpl &operator=(const AsyncInferRunnerImpl &) = delete;
    virtual ~AsyncInferRunnerImpl();
    AsyncInferRunnerImpl(std::shared_ptr<AsyncPipeline> async_pipeline, std::shared_ptr<std::atomic<hailo_status>> pipeline_status);

    hailo_status run(const ConfiguredInferModel::Bindings &bindings, TransferDoneCallbackAsyncInfer transfer_done);
    hailo_status set_buffers(std::unordered_map<std::string, PipelineBuffer> &inputs,
        std::unordered_map<std::string, PipelineBuffer> &outputs);

    void abort();

    // string - if can't push buffer - element name on which we cant push. if can push buffer - empty
    Expected<std::pair<bool, std::string>> can_push_buffers(uint32_t frames_count);

    void add_element_to_pipeline(std::shared_ptr<PipelineElement> pipeline_element);
    void add_entry_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &input_name);
    void add_last_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &output_name);

    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> get_entry_elements();
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> get_last_elements();

    std::vector<std::shared_ptr<PipelineElement>> get_pipeline() const;
    std::string get_pipeline_description() const;
    hailo_status get_pipeline_status() const;
    std::shared_ptr<AsyncPipeline> get_async_pipeline() const;

protected:
    hailo_status start_pipeline();
    hailo_status stop_pipeline();

    void set_pix_buffer_inputs(std::unordered_map<std::string, PipelineBuffer> &inputs, hailo_pix_buffer_t userptr_pix_buffer,
        TransferDoneCallbackAsyncInfer input_done, const std::string &input_name);

    std::shared_ptr<AsyncPipeline> m_async_pipeline;
    volatile bool m_is_activated;
    volatile bool m_is_aborted;
    std::shared_ptr<std::atomic<hailo_status>> m_pipeline_status;
    std::mutex m_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_ASYNC_INFER_RUNNER_HPP_ */
