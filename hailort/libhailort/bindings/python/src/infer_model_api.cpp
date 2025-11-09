/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model_api.cpp
 * @brief Defines binding to a infer model class family usage over Python.
 **/
#include "infer_model_api.hpp"
#include "bindings_common.hpp"
#include "hailo/infer_model.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <pybind11/gil.h>           // py::gil_scoped_release
#include <pybind11/stl.h>           // handle std::vector
#include <pybind11/functional.h>    // handle std::function
#include <pybind11/chrono.h>        // handle std::chrono::milliseconds


using namespace hailort;

void InferModelWrapper::set_batch_size(uint16_t batch_size)
{
    m_infer_model->set_batch_size(batch_size);
}

void InferModelWrapper::set_power_mode(hailo_power_mode_t power_mode)
{
    m_infer_model->set_power_mode(power_mode);
}

std::vector<InferModelInferStreamWrapper> InferModelWrapper::inputs()
{
    auto infer_streams = m_infer_model->inputs();
    std::vector<InferModelInferStreamWrapper> wrappers;
    for (auto &infer_stream : infer_streams)
    {
        wrappers.push_back(InferModelInferStreamWrapper(std::move(infer_stream)));
    }
    return wrappers;
}

std::vector<InferModelInferStreamWrapper> InferModelWrapper::outputs()
{
    auto infer_streams = m_infer_model->outputs();
    std::vector<InferModelInferStreamWrapper> wrappers;
    for (auto &infer_stream : infer_streams)
    {
        wrappers.push_back(InferModelInferStreamWrapper(std::move(infer_stream)));
    }
    return wrappers;
}

ConfiguredInferModelWrapper InferModelWrapper::configure()
{
    auto configured_infer_model = m_infer_model->configure();
    VALIDATE_EXPECTED(configured_infer_model);

    // HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK does not require reallocation
    // of output buffers because the output buffers are not passed directly to
    // the HW. To know if the model's format is
    // HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK, it is sufficient to check
    // the first output's format since NMS with byte mask only supports a single
    // output. There might be more cases where the output buffers don't need to
    // be reallocated, but for now this is the only case where the output
    // buffers must not be reallocated
    // TODO: HRT-15167
    auto output_names = m_infer_model->get_output_names();
    auto should_reallocate_output_buffers =
        !((output_names.size() == 1) &&
          (m_infer_model->output()->format().order ==
           HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK));

    return ConfiguredInferModelWrapper(configured_infer_model.release(), m_is_using_service,
        output_names, should_reallocate_output_buffers);
}

std::vector<std::string> InferModelWrapper::get_input_names()
{
    return m_infer_model->get_input_names();
}

std::vector<std::string> InferModelWrapper::get_output_names()
{
    return m_infer_model->get_output_names();
}

InferModelInferStreamWrapper InferModelWrapper::input(const std::string &name)
{
    auto infer_stream = name.empty() ? m_infer_model->input() : m_infer_model->input(name);
    VALIDATE_EXPECTED(infer_stream);
    return InferModelInferStreamWrapper(infer_stream.release());
}

InferModelInferStreamWrapper InferModelWrapper::output(const std::string &name)
{
    auto infer_stream = name.empty() ? m_infer_model->output() : m_infer_model->output(name);
    VALIDATE_EXPECTED(infer_stream);
    return InferModelInferStreamWrapper(infer_stream.release());
}

void InferModelWrapper::set_enable_kv_cache(bool enable_kv_cache)
{
    m_infer_model->set_enable_kv_cache(enable_kv_cache);
}

ConfiguredInferModelBindingsWrapper ConfiguredInferModelWrapper::create_bindings()
{
    auto bindings = m_configured_infer_model.create_bindings();
    VALIDATE_EXPECTED(bindings);
    return ConfiguredInferModelBindingsWrapper(bindings.release(), m_output_names);
}

void ConfiguredInferModelWrapper::activate()
{
    auto status = m_configured_infer_model.activate();
    VALIDATE_STATUS(status);
}

void ConfiguredInferModelWrapper::deactivate()
{
    auto status = m_configured_infer_model.deactivate();
    VALIDATE_STATUS(status);
}

ConfiguredInferModelBindingsInferStreamWrapper ConfiguredInferModelBindingsWrapper::input(const std::string &name)
{
    auto infer_stream = name.empty() ? m_bindings.input() : m_bindings.input(name);
    VALIDATE_EXPECTED(infer_stream);
    return ConfiguredInferModelBindingsInferStreamWrapper(infer_stream.release());
}

ConfiguredInferModelBindingsInferStreamWrapper ConfiguredInferModelBindingsWrapper::output(const std::string &name)
{
    auto infer_stream = name.empty() ? m_bindings.output() : m_bindings.output(name);
    VALIDATE_EXPECTED(infer_stream);
    return ConfiguredInferModelBindingsInferStreamWrapper(infer_stream.release());
}

const std::string InferModelInferStreamWrapper::name() const
{
    return m_infer_stream.name();
}

void InferModelInferStreamWrapper::set_format_type(hailo_format_type_t type)
{
    m_infer_stream.set_format_type(type);
}

void InferModelInferStreamWrapper::set_format_order(hailo_format_order_t order)
{
    m_infer_stream.set_format_order(order);
}

std::vector<hailo_quant_info_t> InferModelInferStreamWrapper::get_quant_infos() const
{
    return m_infer_stream.get_quant_infos();
}

std::vector<size_t> InferModelInferStreamWrapper::shape() const
{
    auto shape = m_infer_stream.shape();
    auto format = m_infer_stream.format();
    hailo_nms_shape_t nms_shape; // if the format is non-NMS, this struct won't be used

    if (HailoRTCommon::is_nms(format.order))
    {
        auto expected = m_infer_stream.get_nms_shape();
        VALIDATE_EXPECTED(expected);
        nms_shape = expected.release();
    }

    return HailoRTBindingsCommon::get_pybind_shape(shape, nms_shape, format);
}

hailo_format_t InferModelInferStreamWrapper::format() const
{
    return m_infer_stream.format();
}

bool InferModelInferStreamWrapper::is_nms() const
{
    return m_infer_stream.is_nms();
}

void InferModelInferStreamWrapper::set_nms_score_threshold(float32_t threshold)
{
    m_infer_stream.set_nms_score_threshold(threshold);
}

void InferModelInferStreamWrapper::set_nms_iou_threshold(float32_t threshold)
{
    m_infer_stream.set_nms_iou_threshold(threshold);
}

void InferModelInferStreamWrapper::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    m_infer_stream.set_nms_max_proposals_per_class(max_proposals_per_class);
}

void InferModelInferStreamWrapper::set_nms_max_proposals_total(uint32_t max_proposals_total)
{
    m_infer_stream.set_nms_max_proposals_total(max_proposals_total);
}

void InferModelInferStreamWrapper::set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size)
{
    m_infer_stream.set_nms_max_accumulated_mask_size(max_accumulated_mask_size);
}

void ConfiguredInferModelBindingsInferStreamWrapper::set_buffer(py::array buffer)
{
    MemoryView view(buffer.mutable_data(), static_cast<size_t>(buffer.nbytes()));
    auto status = m_infer_stream.set_buffer(view);
    VALIDATE_STATUS(status);
}

void ConfiguredInferModelWrapper::wait_for_async_ready(std::chrono::milliseconds timeout, uint32_t frames_count)
{
    auto status = m_configured_infer_model.wait_for_async_ready(timeout, frames_count);
    VALIDATE_STATUS(status);
}

void ConfiguredInferModelWrapper::run(
    ConfiguredInferModelBindingsWrapper bindings,
    std::chrono::milliseconds timeout)
{
    auto status = m_configured_infer_model.run(bindings.release(), timeout);
    VALIDATE_STATUS(status);
}

AsyncInferJobWrapper ConfiguredInferModelWrapper::run_async(
    std::vector<ConfiguredInferModelBindingsWrapper> &user_bindings,
    AsyncInferCallBack pythonic_cb)
{
    std::function<void(const AsyncInferCompletionInfo &info)> cb;

    // create an event that will be held by AsyncInferJobWrapper to know if the callback has been executed
    auto is_callback_done_expected = Event::create_shared(Event::State::not_signalled);
    VALIDATE_EXPECTED(is_callback_done_expected);
    auto is_callback_done = is_callback_done_expected.release();

    if (!m_is_using_service)
    {
        // a worker thread will handle the user callbacks
        // this function should register the callback and notify the thread once the callback is ready to be executed
        if(!m_callbacks_thread)
        {
            m_callbacks_thread = std::make_shared<std::thread>(&ConfiguredInferModelWrapper::execute_callbacks, this);
        }

        // the pythonic_cb (i.e. user-defined callback) will signal once the callback has been executed
        auto pythonic_cb_wrapper = [pythonic_cb, is_callback_done](const int error_code)
        {
            pythonic_cb(error_code);
            is_callback_done->signal();
        };

        // enqueue the pythonic_cb (i.e. user-defined callback) only when libhailort's
        // async job is done. Once enqueued, the job will be dequeued by the worker thread
        cb = [this, pythonic_cb_wrapper](const AsyncInferCompletionInfo &info)
        {
            {
                std::lock_guard<std::mutex> lock(m_queue_mutex);
                // push the callback and the status to the queue. If the status is not HAILO_SUCCESS,
                // the user-defined callback will be get an indication of the failure
                m_callbacks_queue->push(std::make_pair(pythonic_cb_wrapper, info));
            }
            m_cv.notify_one();
        };
    }
    else
    {
        // the user callbacks will be called from libahilort, and this object won't have a worker thread to notify
        cb = [pythonic_cb, is_callback_done](const AsyncInferCompletionInfo &info)
        {
            pythonic_cb(info.status);
            is_callback_done->signal();
        };
    }

    std::vector<ConfiguredInferModel::Bindings> bindings;
    std::transform(user_bindings.begin(), user_bindings.end(), std::back_inserter(bindings),
        [](ConfiguredInferModelBindingsWrapper &wrapper) { return wrapper.release(); });

    // HW requires output buffers to be mmap-ed. Since the user buffers
    // are not mmap-ed, we need to allocate new (mmap-ed) buffers, pass them to
    // libhailort, and once the inference is done, copy the data from them
    // back to the user buffers. This is only necessary when the output buffer
    // is passed directly to the HW. If a post-processing is done in the SW, the
    // buffer will be copied to a valid buffer buffer during the
    // post-processing, making the copy in the callback unnecessary.
    // TODO: HRT-15167
    AsyncInferJob job;
    if(m_should_reallocate_output_buffers)
    {
        std::vector<void*> user_output_buffers;
        std::vector<BufferPtr> aligned_output_buffers;
        for (auto &binding : bindings)
        {
            for (const auto &name : m_output_names)
            {
                auto stream = binding.output(name);
                VALIDATE_EXPECTED(stream);

                auto buffer = stream->get_buffer();
                VALIDATE_EXPECTED(buffer);

                user_output_buffers.push_back(buffer->data());

                auto aligned_buffer = Buffer::create_shared(buffer->size(), BufferStorageParams::create_dma());
                VALIDATE_EXPECTED(aligned_buffer);

                auto buf = aligned_buffer.release();
                aligned_output_buffers.push_back(buf);

                auto status = stream->set_buffer(MemoryView(buf->data(), buf->size()));
                VALIDATE_STATUS(status);
            }
        }

        auto cb_wrapper_with_output_copy = [cb, user_output_buffers, aligned_output_buffers](const AsyncInferCompletionInfo &info)
        {
            for (size_t i = 0; i < user_output_buffers.size(); i++)
            {
                std::memcpy(user_output_buffers[i], aligned_output_buffers[i]->data(), aligned_output_buffers[i]->size());
            }

            cb(info);
        };

        auto job_expected = m_configured_infer_model.run_async(bindings, cb_wrapper_with_output_copy);
        VALIDATE_EXPECTED(job_expected);
        job = job_expected.release();
    } else {
        auto job_expected = m_configured_infer_model.run_async(bindings, cb);
        VALIDATE_EXPECTED(job_expected);
        job = job_expected.release();
    }

    // don't wait for the job at this location. The user should call job.wait() from the python side
    job.detach();
    return AsyncInferJobWrapper(std::move(job), is_callback_done);
}

void ConfiguredInferModelWrapper::set_scheduler_timeout(const std::chrono::milliseconds &timeout)
{
    auto status = m_configured_infer_model.set_scheduler_timeout(timeout);
    VALIDATE_STATUS(status);
}

void ConfiguredInferModelWrapper::set_scheduler_threshold(uint32_t threshold)
{
    auto status = m_configured_infer_model.set_scheduler_threshold(threshold);
    VALIDATE_STATUS(status);
}

void ConfiguredInferModelWrapper::set_scheduler_priority(uint8_t priority)
{
    auto status = m_configured_infer_model.set_scheduler_priority(priority);
    VALIDATE_STATUS(status);
}

size_t ConfiguredInferModelWrapper::get_async_queue_size() const
{
    auto size = m_configured_infer_model.get_async_queue_size();
    VALIDATE_EXPECTED(size);
    return size.release();
}

void ConfiguredInferModelWrapper::shutdown()
{
    auto status = m_configured_infer_model.shutdown();
    VALIDATE_STATUS(status);
}

void ConfiguredInferModelWrapper::update_cache_offset(int32_t offset_delta_entries)
{
    auto status = m_configured_infer_model.update_cache_offset(offset_delta_entries);
    VALIDATE_STATUS(status);
}

void ConfiguredInferModelWrapper::finalize_cache()
{
    return;
}

void ConfiguredInferModelWrapper::execute_callbacks()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        auto ret = m_cv.wait_for(lock, std::chrono::minutes(1), [this](){ return !m_callbacks_queue->empty() || !m_is_alive.load(); });

        if (!m_is_alive.load()) {
            while (!m_callbacks_queue->empty()) {
                auto cb_status_pair = m_callbacks_queue->front();
                auto &cb = cb_status_pair.first;
                auto status = cb_status_pair.second;
                cb(status.status);
                m_callbacks_queue->pop();
            }
            return;
        }

        if (!ret) {
            continue;
        }

        auto cb_status_pair = m_callbacks_queue->front();

        m_callbacks_queue->pop();
        lock.unlock(); // release the lock before calling the callback, allowing other threads to push to the queue

        auto &cb = cb_status_pair.first;
        auto status = cb_status_pair.second;
        cb(status.status);
    }
}

void AsyncInferJobWrapper::wait(std::chrono::milliseconds timeout)
{
    // TODO: currently waiting for 2 TIMEOUT (worst case). Fix it
    auto status = m_job.wait(timeout);
    VALIDATE_STATUS(status);

    status = m_is_callback_done->wait(timeout);
    VALIDATE_STATUS(status);
}

void InferModelWrapper::bind(py::module &m)
{
    py::class_<
        InferModelWrapper,
        std::shared_ptr<InferModelWrapper>
    >(m, "InferModel")
        //.def("hef", &InferModelWrapper::hef)
        .def("configure", &InferModelWrapper::configure)
        .def("set_batch_size", &InferModelWrapper::set_batch_size)
        .def("set_power_mode", &InferModelWrapper::set_power_mode)
        .def("get_input_names", &InferModelWrapper::get_input_names)
        .def("get_output_names", &InferModelWrapper::get_output_names)
        .def("inputs", &InferModelWrapper::inputs)
        .def("outputs", &InferModelWrapper::outputs)
        .def("input", &InferModelWrapper::input)
        .def("output", &InferModelWrapper::output)
        .def("set_enable_kv_cache", &InferModelWrapper::set_enable_kv_cache)
        ;
}

void ConfiguredInferModelWrapper::bind(py::module &m)
{
    py::class_<
        ConfiguredInferModelWrapper,
        nogil_shared_ptr<ConfiguredInferModelWrapper>
    >(m, "ConfiguredInferModel")
        .def("create_bindings", &ConfiguredInferModelWrapper::create_bindings)
        .def("activate", &ConfiguredInferModelWrapper::activate)
        .def("deactivate", &ConfiguredInferModelWrapper::deactivate)
        // wait_for_async_ready is a blocking call which could take a long time, and in the meantime, the pythonic
        // callback that was passed to run_async might be called. Therefore, the GIL before calling must be released.
        // This is done in order to avoid any further deadlock. Any c++ function that is called from python and that might
        // be still running while another thread is trying to execute a python callback, should be able to release the GIL.
        .def("wait_for_async_ready", &ConfiguredInferModelWrapper::wait_for_async_ready, py::call_guard<py::gil_scoped_release>())
        .def("run", &ConfiguredInferModelWrapper::run)
        // run_async is not calling python callbacks*, so there is no need to keep the GIL while calling.
        // Releasing the GIL before calling run_async will allow the callbacks already registered to be called.
        // * callbacks will be called from another thread, and will acquire the GIL by themselves
        .def("run_async", &ConfiguredInferModelWrapper::run_async, py::call_guard<py::gil_scoped_release>())
        .def("set_scheduler_timeout", &ConfiguredInferModelWrapper::set_scheduler_timeout)
        .def("set_scheduler_threshold", &ConfiguredInferModelWrapper::set_scheduler_threshold)
        .def("set_scheduler_priority", &ConfiguredInferModelWrapper::set_scheduler_priority)
        .def("get_async_queue_size", &ConfiguredInferModelWrapper::get_async_queue_size)
        .def("shutdown", &ConfiguredInferModelWrapper::shutdown)
        .def("update_cache_offset", &ConfiguredInferModelWrapper::update_cache_offset)
        .def("finalize_cache", &ConfiguredInferModelWrapper::finalize_cache)
        ;
}

void ConfiguredInferModelBindingsWrapper::bind(py::module &m)
{
    py::class_<
        ConfiguredInferModelBindingsWrapper,
        std::shared_ptr<ConfiguredInferModelBindingsWrapper>
    >(m, "ConfiguredInferModelBindings")
        .def("input", &ConfiguredInferModelBindingsWrapper::input)
        .def("output", &ConfiguredInferModelBindingsWrapper::output)
        ;
}

void ConfiguredInferModelBindingsInferStreamWrapper::bind(py::module &m)
{
    py::class_<
        ConfiguredInferModelBindingsInferStreamWrapper,
        std::shared_ptr<ConfiguredInferModelBindingsInferStreamWrapper>
    >(m, "ConfiguredInferModelInferStream")
        .def("set_buffer", &ConfiguredInferModelBindingsInferStreamWrapper::set_buffer)
        ;
}

void InferModelInferStreamWrapper::bind(py::module &m)
{
    py::class_<
        InferModelInferStreamWrapper,
        std::shared_ptr<InferModelInferStreamWrapper>
    >(m, "InferModelInferStream")
        .def("name", &InferModelInferStreamWrapper::name)
        .def("set_format_type", &InferModelInferStreamWrapper::set_format_type)
        .def("set_format_order", &InferModelInferStreamWrapper::set_format_order)
        .def("get_quant_infos", &InferModelInferStreamWrapper::get_quant_infos)
        .def("shape", &InferModelInferStreamWrapper::shape)
        .def("format", &InferModelInferStreamWrapper::format)
        .def("is_nms", &InferModelInferStreamWrapper::is_nms)
        .def("set_nms_score_threshold", &InferModelInferStreamWrapper::set_nms_score_threshold)
        .def("set_nms_iou_threshold", &InferModelInferStreamWrapper::set_nms_iou_threshold)
        .def("set_nms_max_proposals_per_class", &InferModelInferStreamWrapper::set_nms_max_proposals_per_class)
        .def("set_nms_max_proposals_total", &InferModelInferStreamWrapper::set_nms_max_proposals_total)
        .def("set_nms_max_accumulated_mask_size", &InferModelInferStreamWrapper::set_nms_max_accumulated_mask_size)
        ;
}

void AsyncInferJobWrapper::bind(py::module &m)
{
    py::class_<
        AsyncInferJobWrapper, std::shared_ptr<AsyncInferJobWrapper>
    >(m, "AsyncInferJob")
        // wait is a blocking call that can take a long time, and in the meantime, the pythonic
        // callback that was passed to run_async might be called. Therefore, we release the GIL before calling.
        // This is done in order to avoid a deadlock. Any c++ function that is called from python and that might
        // be still running while another thread is trying to execute a python callback should release the GIL.
        .def("wait", &AsyncInferJobWrapper::wait, py::call_guard<py::gil_scoped_release>())
        ;
}
