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

#include "hailo/infer_model.hpp"
#include "hailo/vstream.hpp"
#include "net_flow/pipeline/async_infer_runner.hpp"
#include "net_flow/ops/nms_post_process.hpp"

namespace hailort
{

class ConfiguredInferModel::Bindings::InferStream::Impl
{
public:
    Impl(const hailo_vstream_info_t &vstream_info);
    hailo_status set_buffer(MemoryView view);
    Expected<MemoryView> get_buffer();
    hailo_status set_pix_buffer(const hailo_pix_buffer_t &pix_buffer);
    Expected<hailo_pix_buffer_t> get_pix_buffer();
    BufferType get_type();

    void set_stream_callback(TransferDoneCallbackAsyncInfer callback);

private:
    std::string m_name;
    BufferType m_buffer_type;
    union {
        MemoryView m_view;
        hailo_pix_buffer_t m_pix_buffer;
    };
    TransferDoneCallbackAsyncInfer m_stream_callback;
};

class InferModel::InferStream::Impl
{
public:
    Impl(const hailo_vstream_info_t &vstream_info) : m_vstream_info(vstream_info), m_user_buffer_format(vstream_info.format),
        m_nms_score_threshold(static_cast<float32_t>(INVALID_NMS_CONFIG)), m_nms_iou_threshold(static_cast<float32_t>(INVALID_NMS_CONFIG)),
        m_nms_max_proposals_per_class(static_cast<uint32_t>(INVALID_NMS_CONFIG))
    {}

    std::string name() const;
    hailo_3d_image_shape_t shape() const;
    hailo_format_t format() const;
    size_t get_frame_size() const;
    Expected<hailo_nms_shape_t> get_nms_shape() const;
    std::vector<hailo_quant_info_t> get_quant_infos() const;
    void set_format_type(hailo_format_type_t type);
    void set_format_order(hailo_format_order_t order);

    bool is_nms() const;
    void set_nms_score_threshold(float32_t threshold);
    void set_nms_iou_threshold(float32_t threshold);
    void set_nms_max_proposals_per_class(uint32_t max_proposals_per_class);

private:
    friend class InferModel;

    hailo_vstream_info_t m_vstream_info;
    hailo_format_t m_user_buffer_format;

    float32_t m_nms_score_threshold;
    float32_t m_nms_iou_threshold;
    uint32_t m_nms_max_proposals_per_class;
};

class AsyncInferJob::Impl
{
public:
    Impl(uint32_t streams_count);
    hailo_status wait(std::chrono::milliseconds timeout);
    bool stream_done(const hailo_status &status);
    hailo_status completion_status();
    void mark_callback_done();

private:
    std::condition_variable m_cv;
    std::mutex m_mutex;
    std::atomic_uint32_t m_ongoing_transfers;
    bool m_callback_called;
    hailo_status m_job_completion_status;
};

class ConfiguredInferModelImpl
{
public:
    static Expected<std::shared_ptr<ConfiguredInferModelImpl>> create(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
        const std::vector<std::string> &input_names, const std::vector<std::string> &output_names, const uint32_t timeout = HAILO_DEFAULT_VSTREAM_TIMEOUT_MS);

    ConfiguredInferModelImpl(std::shared_ptr<ConfiguredNetworkGroup> cng,
        std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
        const std::vector<std::string> &input_names,
        const std::vector<std::string> &output_names);
    ~ConfiguredInferModelImpl();
    Expected<ConfiguredInferModel::Bindings> create_bindings();
    hailo_status wait_for_async_ready(std::chrono::milliseconds timeout);
    void abort();
    hailo_status activate();
    void deactivate();
    hailo_status run(ConfiguredInferModel::Bindings bindings, std::chrono::milliseconds timeout);
    Expected<AsyncInferJob> run_async(ConfiguredInferModel::Bindings bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback);
    Expected<LatencyMeasurementResult> get_hw_latency_measurement(const std::string &network_name);
    hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout);
    hailo_status set_scheduler_threshold(uint32_t threshold);
    hailo_status set_scheduler_priority(uint8_t priority);
    Expected<size_t> get_async_queue_size();

    static Expected<std::shared_ptr<ConfiguredInferModelImpl>> create_for_ut(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner, const std::vector<std::string> &input_names, const std::vector<std::string> &output_names);

private:
    hailo_status validate_bindings(ConfiguredInferModel::Bindings bindings);

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
