/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model_internal.hpp
 * @brief Implemention of the infer model
 *
 * InferModel                       (Interface)
 *  |-- InferModelBase              (Base implementation)
 *      |-- InferModelHrpcClient    (RPC handle communicating with the server)
 **/

#ifndef _HAILO_INFER_MODEL_INTERNAL_HPP_
#define _HAILO_INFER_MODEL_INTERNAL_HPP_

#include "hailo/infer_model.hpp"
#include "net_flow/pipeline/async_infer_runner.hpp"
#include "net_flow/ops/nms_post_process.hpp"
#include "hrpc/client.hpp"

namespace hailort
{

class ConfiguredInferModel::Bindings::InferStream::Impl
{
public:
    Impl(const hailo_vstream_info_t &vstream_info);
    hailo_status set_buffer(MemoryView view);
    Expected<MemoryView> get_buffer() const;
    hailo_status set_pix_buffer(const hailo_pix_buffer_t &pix_buffer);
    Expected<hailo_pix_buffer_t> get_pix_buffer() const;
    hailo_status set_dma_buffer(hailo_dma_buffer_t dma_buffer);
    Expected<hailo_dma_buffer_t> get_dma_buffer() const;
    BufferType get_type();

    void set_stream_callback(TransferDoneCallbackAsyncInfer callback);

private:
    std::string m_name;
    BufferType m_buffer_type;
    union {
        MemoryView m_view;
        hailo_pix_buffer_t m_pix_buffer;
        hailo_dma_buffer_t m_dma_buffer;
    };
    TransferDoneCallbackAsyncInfer m_stream_callback;
};

class InferModelBase : public InferModel
{
public:
    static Expected<std::shared_ptr<InferModelBase>> create(VDevice &vdevice, Hef hef, const std::string &network_name);

    InferModelBase(VDevice &vdevice, Hef &&hef, const std::string &network_name, std::vector<InferStream> &&inputs,
        std::vector<InferStream> &&outputs);
    virtual ~InferModelBase() = default;
    InferModelBase(InferModelBase &&);

    virtual const Hef &hef() const override;
    virtual void set_batch_size(uint16_t batch_size) override;
    virtual void set_power_mode(hailo_power_mode_t power_mode) override;
    virtual void set_hw_latency_measurement_flags(hailo_latency_measurement_flags_t latency) override;
    virtual Expected<ConfiguredInferModel> configure() override;
    virtual Expected<InferStream> input() override;
    virtual Expected<InferStream> output() override;
    virtual Expected<InferStream> input(const std::string &name) override;
    virtual Expected<InferStream> output(const std::string &name) override;
    virtual const std::vector<InferStream> &inputs() const override;
    virtual const std::vector<InferStream> &outputs() const override;
    virtual const std::vector<std::string> &get_input_names() const override;
    virtual const std::vector<std::string> &get_output_names() const override;

    virtual Expected<ConfiguredInferModel> configure_for_ut(std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
        const std::vector<std::string> &input_names, const std::vector<std::string> &output_names,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes = {},
        const std::unordered_map<std::string, size_t> outputs_frame_sizes = {},
        std::shared_ptr<ConfiguredNetworkGroup> net_group = nullptr) override;

protected:
    static Expected<std::vector<InferModel::InferStream>> create_infer_stream_inputs(Hef &hef, const std::string &network_name);
    static Expected<std::vector<InferModel::InferStream>> create_infer_stream_outputs(Hef &hef, const std::string &network_name);

    std::reference_wrapper<VDevice> m_vdevice;
    Hef m_hef;
    const std::string m_network_name;
    std::vector<InferStream> m_inputs_vector;
    std::vector<InferStream> m_outputs_vector;
    std::map<std::string, InferStream> m_inputs;
    std::map<std::string, InferStream> m_outputs;
    std::vector<std::string> m_input_names;
    std::vector<std::string> m_output_names;
    ConfigureNetworkParams m_config_params;
};

class InferModel::InferStream::Impl
{
public:
    Impl(const hailo_vstream_info_t &vstream_info) : m_vstream_info(vstream_info), m_user_buffer_format(vstream_info.format),
        m_nms_score_threshold(static_cast<float32_t>(INVALID_NMS_CONFIG)), m_nms_iou_threshold(static_cast<float32_t>(INVALID_NMS_CONFIG)),
        m_nms_max_proposals_per_class(static_cast<uint32_t>(INVALID_NMS_CONFIG)), m_nms_max_proposals_total(static_cast<uint32_t>(INVALID_NMS_CONFIG)),
        m_nms_max_accumulated_mask_size(static_cast<uint32_t>(INVALID_NMS_CONFIG))
    {
        m_user_buffer_format.flags = HAILO_FORMAT_FLAGS_NONE; // Init user's format flags to NONE for transposed models
    }

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
    void set_nms_max_proposals_total(uint32_t max_proposals_total);
    void set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size);

    float32_t nms_score_threshold() const;
    float32_t nms_iou_threshold() const;
    uint32_t nms_max_proposals_per_class() const;
    uint32_t nms_max_proposals_total() const;
    uint32_t nms_max_accumulated_mask_size() const;

private:
    friend class InferModel;
    friend class InferModelBase;
    friend class VDevice;

    hailo_vstream_info_t m_vstream_info;
    hailo_format_t m_user_buffer_format;

    float32_t m_nms_score_threshold;
    float32_t m_nms_iou_threshold;
    uint32_t m_nms_max_proposals_per_class;
    uint32_t m_nms_max_proposals_total;
    uint32_t m_nms_max_accumulated_mask_size;
};

class AsyncInferJobBase
{
public:
    static AsyncInferJob create(std::shared_ptr<AsyncInferJobBase> base);
    virtual ~AsyncInferJobBase() = default;
    virtual hailo_status wait(std::chrono::milliseconds timeout) = 0;
};

class AsyncInferJobImpl : public AsyncInferJobBase
{
public:
    AsyncInferJobImpl(uint32_t streams_count);
    virtual hailo_status wait(std::chrono::milliseconds timeout) override;

private:
    friend class ConfiguredInferModelBase;

    bool stream_done(const hailo_status &status);
    hailo_status completion_status();
    void mark_callback_done();

    std::condition_variable m_cv;
    std::mutex m_mutex;
    std::atomic_uint32_t m_ongoing_transfers;
    bool m_callback_called;
    hailo_status m_job_completion_status;
};

/*
 * ConfiguredInferModel                      (interface wrapper - external API)
 * |-- ConfiguredInferModelBase              (interface)
 * |--|-- ConfiguredInferModelImpl           (non-RPC implementation)
 * |--|-- ConfiguredInferModelHrpcClient     (RPC handle communicating with the server)
 */

class ConfiguredInferModelBase
{
public:
    static ConfiguredInferModel create(std::shared_ptr<ConfiguredInferModelBase> base);

    ConfiguredInferModelBase(const std::unordered_map<std::string, size_t> inputs_frame_sizes,
        const std::unordered_map<std::string, size_t> outputs_frame_sizes);
    virtual ~ConfiguredInferModelBase() = default;
    virtual Expected<ConfiguredInferModel::Bindings> create_bindings(const std::map<std::string, MemoryView> &buffers) = 0;
    virtual hailo_status wait_for_async_ready(std::chrono::milliseconds timeout, uint32_t frames_count = 1) = 0;
    virtual hailo_status activate() = 0;
    virtual hailo_status deactivate() = 0;
    virtual hailo_status run(const ConfiguredInferModel::Bindings &bindings, std::chrono::milliseconds timeout);
    virtual Expected<AsyncInferJob> run_async(const ConfiguredInferModel::Bindings &bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback = ASYNC_INFER_EMPTY_CALLBACK) = 0;
    virtual Expected<LatencyMeasurementResult> get_hw_latency_measurement() = 0;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout) = 0;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold) = 0;
    virtual hailo_status set_scheduler_priority(uint8_t priority) = 0;
    virtual Expected<size_t> get_async_queue_size() const = 0;
    virtual hailo_status shutdown() = 0;
    virtual hailo_status update_cache_offset(int32_t offset_delta_entries) = 0;

    static Expected<ConfiguredInferModel::Bindings> create_bindings(
        std::unordered_map<std::string, ConfiguredInferModel::Bindings::InferStream> &&inputs,
        std::unordered_map<std::string, ConfiguredInferModel::Bindings::InferStream> &&outputs);
    static Expected<ConfiguredInferModel::Bindings::InferStream> create_infer_stream(const hailo_vstream_info_t &vstream_info);
    static BufferType get_infer_stream_buffer_type(ConfiguredInferModel::Bindings::InferStream expected_stream);
    static bool get_stream_done(hailo_status status, std::shared_ptr<AsyncInferJobImpl> job_pimpl);
    static hailo_status get_completion_status(std::shared_ptr<AsyncInferJobImpl> job_pimpl);
    static void mark_callback_done(std::shared_ptr<AsyncInferJobImpl> job_pimpl);

private:
    virtual hailo_status validate_bindings(const ConfiguredInferModel::Bindings &bindings) = 0;

protected:
    std::unordered_map<std::string, size_t> m_inputs_frame_sizes;
    std::unordered_map<std::string, size_t> m_outputs_frame_sizes;
    std::timed_mutex m_run_mutex;

};

class ConfiguredInferModelImpl : public ConfiguredInferModelBase
{
public:
    static Expected<std::shared_ptr<ConfiguredInferModelImpl>> create(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
        const std::vector<std::string> &input_names, const std::vector<std::string> &output_names, VDevice &vdevice,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes, const std::unordered_map<std::string, size_t> outputs_frame_sizes,
        const uint32_t timeout = HAILO_DEFAULT_VSTREAM_TIMEOUT_MS);

    ConfiguredInferModelImpl(std::shared_ptr<ConfiguredNetworkGroup> cng, std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
        const std::vector<std::string> &input_names, const std::vector<std::string> &output_names,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes, const std::unordered_map<std::string, size_t> outputs_frame_sizes);
    ~ConfiguredInferModelImpl();
    virtual Expected<ConfiguredInferModel::Bindings> create_bindings(const std::map<std::string, MemoryView> &buffers) override;
    virtual hailo_status wait_for_async_ready(std::chrono::milliseconds timeout, uint32_t frames_count) override;
    virtual hailo_status activate() override;
    virtual hailo_status deactivate() override;
    virtual Expected<AsyncInferJob> run_async(const ConfiguredInferModel::Bindings &bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback) override;
    virtual Expected<LatencyMeasurementResult> get_hw_latency_measurement() override;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout) override;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold) override;
    virtual hailo_status set_scheduler_priority(uint8_t priority) override;
    virtual Expected<size_t> get_async_queue_size() const override;
    virtual hailo_status shutdown() override;
    virtual hailo_status update_cache_offset(int32_t offset_delta_entries) override;

    static Expected<std::shared_ptr<ConfiguredInferModelImpl>> create_for_ut(std::shared_ptr<ConfiguredNetworkGroup> net_group,
        std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner, const std::vector<std::string> &input_names, const std::vector<std::string> &output_names,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes, const std::unordered_map<std::string, size_t> outputs_frame_sizes);

private:
    virtual hailo_status validate_bindings(const ConfiguredInferModel::Bindings &bindings) override;

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
