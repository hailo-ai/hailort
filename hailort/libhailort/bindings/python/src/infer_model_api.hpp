/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model_api.hpp
 * @brief Defines binding to a infer model class family usage over Python.
 **/

#ifndef INFER_MODEL_API_HPP_
#define INFER_MODEL_API_HPP_

#include "hailo/hailort.h"
#include "hailo/event.hpp"
#include "hailo/infer_model.hpp"
#include "utils.hpp"
#include <functional>
#include <pybind11/numpy.h>
#include <thread>
#include <vector>
#include <queue>

namespace hailort {

class ConfiguredInferModelWrapper;
class ConfiguredInferModelBindingsWrapper;
class ConfiguredInferModelBindingsInferStreamWrapper;
class InferModelInferStreamWrapper;
class AsyncInferJobWrapper;

using AsyncInferCallBack = std::function<void(const int)>;
using AsyncInferCallBackAndStatus = std::pair<AsyncInferCallBack, AsyncInferCompletionInfo>;

class InferModelWrapper final
{
public:
    InferModelWrapper(std::shared_ptr<InferModel> infer_model,  bool is_using_service) :
        m_infer_model(std::move(infer_model)), m_is_using_service(is_using_service) {}
    void set_batch_size(uint16_t batch_size);
    void set_power_mode(hailo_power_mode_t power_mode);
    void set_hw_latency_measurement_flags(hailo_latency_measurement_flags_t latency);
    ConfiguredInferModelWrapper configure();
    std::vector<std::string> get_input_names();
    std::vector<std::string> get_output_names();
    std::vector<InferModelInferStreamWrapper> inputs();
    std::vector<InferModelInferStreamWrapper> outputs();
    InferModelInferStreamWrapper input(const std::string &name);
    InferModelInferStreamWrapper output(const std::string &name);
    void set_enable_kv_cache(bool enable_kv_cache);

    static void bind(py::module &m);

private:
    std::shared_ptr<InferModel> m_infer_model;
    bool m_is_using_service;
};

class ConfiguredInferModelWrapper final
{
public:
    ConfiguredInferModelWrapper(ConfiguredInferModel &&configured_infer_model, bool is_using_service,
        const std::vector<std::string> &output_names, bool should_reallocate_output_buffers=true) :
        m_configured_infer_model(std::move(configured_infer_model)),
        m_callbacks_queue(std::make_shared<std::queue<AsyncInferCallBackAndStatus>>()),
        m_is_alive(true),
        m_is_using_service(is_using_service),
        m_output_names(output_names),
        m_should_reallocate_output_buffers(should_reallocate_output_buffers)
    {
    }

    ConfiguredInferModelWrapper(ConfiguredInferModelWrapper &&other) :
        m_configured_infer_model(std::move(other.m_configured_infer_model)),
        m_callbacks_queue(std::move(other.m_callbacks_queue)),
        m_callbacks_thread(std::move(other.m_callbacks_thread)),
		m_is_alive(true),
		m_is_using_service(other.m_is_using_service),
        m_output_names(other.m_output_names),
        m_should_reallocate_output_buffers(other.m_should_reallocate_output_buffers)
    {
        other.m_is_alive = false;
    }

    ~ConfiguredInferModelWrapper()
    {
        if (m_is_alive) {
            m_configured_infer_model.shutdown();
            m_is_alive = false; // signal the thread to stop
            m_cv.notify_all(); // wake up the thread so it can return
            if (m_callbacks_thread && m_callbacks_thread->joinable())
            {
                m_callbacks_thread->join();
            }
        }
    }

    ConfiguredInferModelBindingsWrapper create_bindings();
    void activate();
    void deactivate();
    void wait_for_async_ready(std::chrono::milliseconds timeout, uint32_t frames_count = 1);
    void run(ConfiguredInferModelBindingsWrapper bindings, std::chrono::milliseconds timeout);
    AsyncInferJobWrapper run_async(
        std::vector<ConfiguredInferModelBindingsWrapper> &bindings,
        AsyncInferCallBack pythonic_cb);
    void set_scheduler_timeout(const std::chrono::milliseconds &timeout);
    void set_scheduler_threshold(uint32_t threshold);
    void set_scheduler_priority(uint8_t priority);
    size_t get_async_queue_size() const;
    void shutdown();
    void update_cache_offset(int32_t offset_delta_entries);
    void finalize_cache();

    static void bind(py::module &m);

private:
    void execute_callbacks();

    ConfiguredInferModel m_configured_infer_model;
    std::mutex m_queue_mutex;
    std::condition_variable m_cv;
    std::shared_ptr<std::queue<AsyncInferCallBackAndStatus>> m_callbacks_queue;
    std::shared_ptr<std::thread> m_callbacks_thread; // worker thread. Executes user defined (pythonic) callbacks
    std::atomic_bool m_is_alive; // allow main thread to write, while worker thread is reading
    bool m_is_using_service;
    std::vector<std::string> m_output_names;
    bool m_should_reallocate_output_buffers;
};

class ConfiguredInferModelBindingsWrapper final
{
public:
    ConfiguredInferModelBindingsWrapper(ConfiguredInferModel::Bindings &&bindings, std::vector<std::string> output_names) :
        m_bindings(std::move(bindings)),
        m_output_names(output_names)
    {}
    ConfiguredInferModelBindingsInferStreamWrapper input(const std::string &name);
    ConfiguredInferModelBindingsInferStreamWrapper output(const std::string &name);
    ConfiguredInferModel::Bindings &&release() { return std::move(m_bindings); }

    static void bind(py::module &m);

private:
    ConfiguredInferModel::Bindings m_bindings;
    std::vector<std::string> m_output_names;
};

class ConfiguredInferModelBindingsInferStreamWrapper final
{
public:
    ConfiguredInferModelBindingsInferStreamWrapper(ConfiguredInferModel::Bindings::InferStream&& infer_stream) :
        m_infer_stream(std::move(infer_stream)) {}
    void set_buffer(py::array buffer);

    static void bind(py::module &m);

private:
    ConfiguredInferModel::Bindings::InferStream m_infer_stream;
};

class InferModelInferStreamWrapper final
{
public:
    InferModelInferStreamWrapper(InferModel::InferStream&& infer_stream) :
        m_infer_stream(std::move(infer_stream)) {}
    const std::string name() const;
    void set_format_type(hailo_format_type_t type);
    void set_format_order(hailo_format_order_t order);
    std::vector<hailo_quant_info_t> get_quant_infos() const;
    std::vector<size_t> shape() const;
    hailo_format_t format() const;
    bool is_nms() const;
    void set_nms_score_threshold(float32_t threshold);
    void set_nms_iou_threshold(float32_t threshold);
    void set_nms_max_proposals_per_class(uint32_t max_proposals_per_class);
    void set_nms_max_proposals_total(uint32_t max_proposals_total);
    void set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size);

    static void bind(py::module &m);
private:
    InferModel::InferStream m_infer_stream;
};

class AsyncInferJobWrapper final
{
public:
    AsyncInferJobWrapper(AsyncInferJob&& job, EventPtr is_callback_done) :
        m_job(std::move(job)),
        m_is_callback_done(is_callback_done)
    {}
    void wait(std::chrono::milliseconds timeout);

    static void bind(py::module &m);

private:
    AsyncInferJob m_job;
    EventPtr m_is_callback_done;
};

}

#endif /* INFER_MODEL_API_HPP_ */
