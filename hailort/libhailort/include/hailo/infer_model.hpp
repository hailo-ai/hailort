/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model.hpp
 * @brief Async Infer
 **/

#ifndef _HAILO_ASYNC_INFER_HPP_
#define _HAILO_ASYNC_INFER_HPP_

#include "hailo/network_group.hpp"
#include "hailo/hef.hpp"
#include "hailo/vdevice.hpp"

#include <condition_variable>
#include <mutex>

/** hailort namespace */
namespace hailort
{

class ConfiguredInferModelImpl;
class AsyncInferRunnerImpl;
class HAILORTAPI AsyncInferJob
{
public:
    AsyncInferJob() = default;
    virtual ~AsyncInferJob();

    AsyncInferJob(const AsyncInferJob &other) = delete;
    AsyncInferJob &operator=(const AsyncInferJob &other) = delete;
    AsyncInferJob(AsyncInferJob &&other);
    AsyncInferJob &operator=(AsyncInferJob &&other);

    hailo_status wait(std::chrono::milliseconds timeout);
    void detach();

private:
    friend class ConfiguredInferModelImpl;

    class Impl;
    AsyncInferJob(std::shared_ptr<Impl> pimpl);
    std::shared_ptr<Impl> m_pimpl;
    bool m_should_wait_in_dtor;
};

struct AsyncInferCompletionInfo;
class HAILORTAPI ConfiguredInferModel
{
public:
    ConfiguredInferModel() = default;

    class HAILORTAPI Bindings
    {
    public:
        Bindings() = default;

        class HAILORTAPI InferStream
        {
        public:
            hailo_status set_buffer(MemoryView view);
            Expected<MemoryView> get_buffer();
            hailo_status set_pix_buffer(const hailo_pix_buffer_t &pix_buffer);
            Expected<hailo_pix_buffer_t> get_pix_buffer();

        private:
            friend class ConfiguredInferModelImpl;

            class Impl;
            InferStream(std::shared_ptr<Impl> pimpl);
            std::shared_ptr<Impl> m_pimpl;
        };

        Expected<InferStream> input();
        Expected<InferStream> output();
        Expected<InferStream> input(const std::string &name);
        Expected<InferStream> output(const std::string &name);

    private:
        friend class ConfiguredInferModelImpl;

        Bindings(std::unordered_map<std::string, InferStream> &&inputs,
            std::unordered_map<std::string, InferStream> &&outputs);

        std::unordered_map<std::string, Bindings::InferStream> m_inputs;
        std::unordered_map<std::string, Bindings::InferStream> m_outputs;
    };

    Expected<Bindings> create_bindings();
    hailo_status wait_for_async_ready(std::chrono::milliseconds timeout);
    hailo_status activate();
    void deactivate();
    hailo_status run(Bindings bindings, std::chrono::milliseconds timeout);
    Expected<AsyncInferJob> run_async(Bindings bindings,
        std::function<void(const AsyncInferCompletionInfo &)> callback = [] (const AsyncInferCompletionInfo &) {});
    Expected<LatencyMeasurementResult> get_hw_latency_measurement(const std::string &network_name = "");
    hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout);
    hailo_status set_scheduler_threshold(uint32_t threshold);
    hailo_status set_scheduler_priority(uint8_t priority);
    Expected<size_t> get_async_queue_size();

private:
    friend class InferModel;

    ConfiguredInferModel(std::shared_ptr<ConfiguredInferModelImpl> pimpl);

    std::shared_ptr<ConfiguredInferModelImpl> m_pimpl;
};

struct HAILORTAPI AsyncInferCompletionInfo
{
    AsyncInferCompletionInfo(ConfiguredInferModel::Bindings _bindings, hailo_status _status) : bindings(_bindings), status(_status)
    {
    }

    ConfiguredInferModel::Bindings bindings;
    hailo_status status;
};

class HAILORTAPI InferModel final
{
public:
    ~InferModel() = default;

    class HAILORTAPI InferStream
    {
    public:
        // TODO: explain that the getters return what the user defined with set_ functions
        const std::string name() const;
        hailo_3d_image_shape_t shape() const;
        hailo_format_t format() const;
        size_t get_frame_size() const;
        Expected<hailo_nms_shape_t> get_nms_shape() const;
        
        void set_format_type(hailo_format_type_t type);
        void set_format_order(hailo_format_order_t order);
        std::vector<hailo_quant_info_t> get_quant_infos() const;
        bool is_nms() const;
        void set_nms_score_threshold(float32_t threshold);
        void set_nms_iou_threshold(float32_t threshold);
        void set_nms_max_proposals_per_class(uint32_t max_proposals_per_class);

    private:
        friend class InferModel;
        friend class VDevice;

        class Impl;
        InferStream(std::shared_ptr<Impl> pimpl);

        std::shared_ptr<Impl> m_pimpl;
    };

    const Hef &hef() const;
    void set_batch_size(uint16_t batch_size);
    void set_power_mode(hailo_power_mode_t power_mode);
    void set_hw_latency_measurement_flags(hailo_latency_measurement_flags_t latency);

    Expected<ConfiguredInferModel> configure(const std::string &network_name = "");
    Expected<InferStream> input();
    Expected<InferStream> output();
    Expected<InferStream> input(const std::string &name);
    Expected<InferStream> output(const std::string &name);
    const std::vector<InferStream> &inputs() const;
    const std::vector<InferStream> &outputs() const;
    const std::vector<std::string> &get_input_names() const;
    const std::vector<std::string> &get_output_names() const;
    
    InferModel(InferModel &&);

    Expected<ConfiguredInferModel> configure_for_ut(std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
        const std::vector<std::string> &input_names, const std::vector<std::string> &output_names);

private:
    friend class VDevice;

    InferModel(VDevice &vdevice, Hef &&hef, std::unordered_map<std::string, InferStream> &&inputs,
        std::unordered_map<std::string, InferStream> &&outputs);

    std::reference_wrapper<VDevice> m_vdevice;
    Hef m_hef;
    std::unordered_map<std::string, InferStream> m_inputs;
    std::unordered_map<std::string, InferStream> m_outputs;
    std::vector<InferStream> m_inputs_vector;
    std::vector<InferStream> m_outputs_vector;
    std::vector<std::string> m_input_names;
    std::vector<std::string> m_output_names;
    ConfigureNetworkParams m_config_params;
};

} /* namespace hailort */

#endif /* _HAILO_ASYNC_INFER_HPP_ */
