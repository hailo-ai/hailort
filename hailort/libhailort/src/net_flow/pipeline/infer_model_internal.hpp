/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model_internal.hpp
 * @brief Implemention of the infer model
 **/

#ifndef _HAILO_INFER_MODEL_INTERNAL_HPP_
#define _HAILO_INFER_MODEL_INTERNAL_HPP_

#include "hailo/vstream.hpp"
#include "net_flow/pipeline/async_infer_runner_internal.hpp"

namespace hailort
{

class ConfiguredInferModel::Bindings::InferStream::Impl
{
public:
    Impl(const hailo_vstream_info_t &vstream_info);
    hailo_status set_buffer(MemoryView view);
    MemoryView get_buffer();
    void set_stream_callback(TransferDoneCallbackAsyncInfer callback);

private:
    std::string m_name;
    MemoryView m_view;
    TransferDoneCallbackAsyncInfer m_stream_callback;
};

class InferModel::InferStream::Impl
{
public:
    Impl(const hailo_vstream_info_t &vstream_info) : m_vstream_info(vstream_info)
    {
        m_user_buffer_format.order = HAILO_FORMAT_ORDER_AUTO;
        m_user_buffer_format.type = HAILO_FORMAT_TYPE_AUTO;
        m_user_buffer_format.flags = HAILO_FORMAT_FLAGS_QUANTIZED;
    }

    std::string name() const;
    size_t get_frame_size() const;
    void set_format_type(hailo_format_type_t type);
    void set_format_order(hailo_format_order_t order);
    hailo_format_t get_user_buffer_format();

private:
    hailo_vstream_info_t m_vstream_info;
    hailo_format_t m_user_buffer_format;
};

class AsyncInferJob::Impl
{
public:
    Impl(uint32_t streams_count);
    hailo_status wait(std::chrono::milliseconds timeout);
    bool stream_done();

private:
    std::condition_variable m_cv;
    std::mutex m_mutex;
    std::atomic_uint32_t m_ongoing_transfers;
};

class ConfiguredInferModelImpl
{
public:
    ConfiguredInferModelImpl(std::shared_ptr<ConfiguredNetworkGroup> cng,
        std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
        const std::vector<std::string> &input_names,
        const std::vector<std::string> &output_names);
    Expected<ConfiguredInferModel::Bindings> create_bindings();
    hailo_status wait_for_async_ready(std::chrono::milliseconds timeout);
    hailo_status activate();
    void deactivate();
    hailo_status run(ConfiguredInferModel::Bindings bindings, std::chrono::milliseconds timeout);
    Expected<AsyncInferJob> run_async(ConfiguredInferModel::Bindings bindings,
        std::function<void(const CompletionInfoAsyncInfer &)> callback);

private:
    std::shared_ptr<ConfiguredNetworkGroup> m_cng;
    std::unique_ptr<ActivatedNetworkGroup> m_ang;
    std::shared_ptr<AsyncInferRunnerImpl> m_async_infer_runner;
    uint32_t m_ongoing_parallel_transfers;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::string> m_input_names;
    std::vector<std::string> m_output_names;
};

} /* namespace hailort */

#endif /* _HAILO_INFER_MODEL_INTERNAL_HPP_ */
