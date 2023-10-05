/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model.cpp
 * @brief Implemention of the async HL infer
 **/

#include <iostream>

#include "common/utils.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/infer_model.hpp"
#include "net_flow/pipeline/infer_model_internal.hpp"
#include "net_flow/pipeline/async_infer_runner_internal.hpp"

#define WAIT_FOR_ASYNC_IN_DTOR_TIMEOUT (10000)

namespace hailort
{

std::string InferModel::InferStream::Impl::name() const
{
    return m_vstream_info.name;
}

size_t InferModel::InferStream::Impl::get_frame_size() const
{
    return HailoRTCommon::get_frame_size(m_vstream_info, m_user_buffer_format);
}

void InferModel::InferStream::Impl::set_format_type(hailo_format_type_t type)
{
    m_user_buffer_format.type = type;
    if (HAILO_FORMAT_TYPE_FLOAT32 == type) {
        m_user_buffer_format.flags = HAILO_FORMAT_FLAGS_NONE;
    } else {
        m_user_buffer_format.flags = HAILO_FORMAT_FLAGS_QUANTIZED;
    }
}

void InferModel::InferStream::Impl::set_format_order(hailo_format_order_t order)
{
    m_user_buffer_format.order = order;
}

hailo_format_t InferModel::InferStream::Impl::get_user_buffer_format()
{
    return m_user_buffer_format;
}

InferModel::InferStream::InferStream(std::shared_ptr<InferModel::InferStream::Impl> pimpl) : m_pimpl(pimpl)
{
}

const std::string InferModel::InferStream::name() const
{
    return m_pimpl->name();
}

size_t InferModel::InferStream::get_frame_size() const
{
    return m_pimpl->get_frame_size();
}

void InferModel::InferStream::set_format_type(hailo_format_type_t type)
{
    m_pimpl->set_format_type(type);
}

void InferModel::InferStream::set_format_order(hailo_format_order_t order)
{
    m_pimpl->set_format_order(order);
}

hailo_format_t InferModel::InferStream::get_user_buffer_format()
{
    return m_pimpl->get_user_buffer_format();
}

InferModel::InferModel(VDevice &vdevice, Hef &&hef, std::unordered_map<std::string, InferModel::InferStream> &&inputs,
        std::unordered_map<std::string, InferModel::InferStream> &&outputs)
    : m_vdevice(vdevice), m_hef(std::move(hef)), m_inputs(std::move(inputs)), m_outputs(std::move(outputs))
{
    m_inputs_vector.reserve(m_inputs.size());
    m_input_names.reserve(m_inputs.size());
    for (const auto &pair : m_inputs) {
        m_inputs_vector.push_back(pair.second);
        m_input_names.push_back(pair.first);
    }

    m_outputs_vector.reserve(m_outputs.size());
    m_output_names.reserve(m_outputs.size());
    for (const auto &pair : m_outputs) {
        m_outputs_vector.push_back(pair.second);
        m_output_names.push_back(pair.first);
    }
}

InferModel::InferModel(InferModel &&other) :
    m_vdevice(std::move(other.m_vdevice)),
    m_hef(std::move(other.m_hef)),
    m_inputs(std::move(other.m_inputs)),
    m_outputs(std::move(other.m_outputs)),
    m_inputs_vector(std::move(other.m_inputs_vector)),
    m_outputs_vector(std::move(other.m_outputs_vector)),
    m_input_names(std::move(other.m_input_names)),
    m_output_names(std::move(other.m_output_names))
{
}

// TODO: document that this will check validity of format tpyes/orders
Expected<ConfiguredInferModel> InferModel::configure(const std::string &network_name)
{
    CHECK_AS_EXPECTED("" == network_name, HAILO_NOT_IMPLEMENTED, "Passing network name is not supported yet!");

    auto configure_params = m_vdevice.get().create_configure_params(m_hef);
    CHECK_EXPECTED(configure_params);

    for (auto &network_group_name_params_pair : *configure_params) {
        for (auto &stream_params_name_pair : network_group_name_params_pair.second.stream_params_by_name) {
            stream_params_name_pair.second.flags = HAILO_STREAM_FLAGS_ASYNC;
        }
    }

    auto network_groups = m_vdevice.get().configure(m_hef, configure_params.value());
    CHECK_EXPECTED(network_groups);

    std::unordered_map<std::string, hailo_format_t> inputs_formats;
    std::unordered_map<std::string, hailo_format_t> outputs_formats;

    auto input_vstream_infos = network_groups.value()[0]->get_input_vstream_infos();
    CHECK_EXPECTED(input_vstream_infos);

    for (const auto &vstream_info : input_vstream_infos.value()) {
        inputs_formats[vstream_info.name] = m_inputs.at(vstream_info.name).get_user_buffer_format();
    }

    auto output_vstream_infos = network_groups.value()[0]->get_output_vstream_infos();
    CHECK_EXPECTED(output_vstream_infos);

    for (const auto &vstream_info : output_vstream_infos.value()) {
        outputs_formats[vstream_info.name] = m_outputs.at(vstream_info.name).get_user_buffer_format();
    }

    // downcasting from ConfiguredNetworkGroup to ConfiguredNetworkGroupBase since we need some functions from ConfiguredNetworkGroupBase
    std::shared_ptr<ConfiguredNetworkGroupBase> configured_net_group_base = std::dynamic_pointer_cast<ConfiguredNetworkGroupBase>(network_groups.value()[0]);
    CHECK_NOT_NULL_AS_EXPECTED(configured_net_group_base, HAILO_INTERNAL_FAILURE);

    auto async_infer_runner = AsyncInferRunnerImpl::create(*configured_net_group_base, inputs_formats, outputs_formats);
    CHECK_EXPECTED(async_infer_runner);

    auto configured_infer_model_pimpl = make_shared_nothrow<ConfiguredInferModelImpl>(network_groups.value()[0], async_infer_runner.release(),
        get_input_names(), get_output_names());
    CHECK_NOT_NULL_AS_EXPECTED(configured_infer_model_pimpl, HAILO_OUT_OF_HOST_MEMORY);

    return ConfiguredInferModel(configured_infer_model_pimpl);
}

Expected<InferModel::InferStream> InferModel::input()
{
    CHECK_AS_EXPECTED(1 == m_inputs.size(), HAILO_INVALID_OPERATION, "Model has more than one input!");
    auto copy = m_inputs.begin()->second;
    return copy;
}

Expected<InferModel::InferStream> InferModel::output()
{
    CHECK_AS_EXPECTED(1 == m_outputs.size(), HAILO_INVALID_OPERATION, "Model has more than one output!");
    auto copy = m_outputs.begin()->second;
    return copy;
}

Expected<InferModel::InferStream> InferModel::input(const std::string &name)
{
    CHECK_AS_EXPECTED(contains(m_inputs, name), HAILO_NOT_FOUND, "Input {} not found!", name);
    auto copy = m_inputs.at(name);
    return copy;
}

Expected<InferModel::InferStream> InferModel::output(const std::string &name)
{
    CHECK_AS_EXPECTED(contains(m_outputs, name), HAILO_NOT_FOUND, "Output {}, not found!", name);
    auto copy = m_outputs.at(name);
    return copy;
}

const std::vector<InferModel::InferStream> &InferModel::inputs() const
{
    return m_inputs_vector;
}

const std::vector<InferModel::InferStream> &InferModel::outputs() const
{
    return m_outputs_vector;
}

const std::vector<std::string> &InferModel::get_input_names() const
{
    return m_input_names;
}

const std::vector<std::string> &InferModel::get_output_names() const
{
    return m_output_names;
}

ConfiguredInferModel::ConfiguredInferModel(std::shared_ptr<ConfiguredInferModelImpl> pimpl) : m_pimpl(pimpl)
{
}

Expected<ConfiguredInferModel::Bindings> ConfiguredInferModel::create_bindings()
{
    return m_pimpl->create_bindings();
}

hailo_status ConfiguredInferModel::wait_for_async_ready(std::chrono::milliseconds timeout)
{
    return m_pimpl->wait_for_async_ready(timeout);
}

hailo_status ConfiguredInferModel::activate()
{
    return m_pimpl->activate();
}

void ConfiguredInferModel::deactivate()
{
    m_pimpl->deactivate();
}

hailo_status ConfiguredInferModel::run(ConfiguredInferModel::Bindings bindings, std::chrono::milliseconds timeout)
{
    return m_pimpl->run(bindings, timeout);
}

Expected<AsyncInferJob> ConfiguredInferModel::run_async(ConfiguredInferModel::Bindings bindings,
    std::function<void(const CompletionInfoAsyncInfer &)> callback)
{
    return m_pimpl->run_async(bindings, callback);
}

ConfiguredInferModelImpl::ConfiguredInferModelImpl(std::shared_ptr<ConfiguredNetworkGroup> cng,
    std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner, 
    const std::vector<std::string> &input_names,
    const std::vector<std::string> &output_names) : m_cng(cng), m_async_infer_runner(async_infer_runner),
    m_ongoing_parallel_transfers(0), m_input_names(input_names), m_output_names(output_names)
{
}

Expected<ConfiguredInferModel::Bindings> ConfiguredInferModelImpl::create_bindings()
{
    std::unordered_map<std::string, ConfiguredInferModel::Bindings::InferStream> inputs;
    std::unordered_map<std::string, ConfiguredInferModel::Bindings::InferStream> outputs;

    auto input_vstream_infos = m_cng->get_input_vstream_infos();
    CHECK_EXPECTED(input_vstream_infos);

    for (const auto &vstream_info : input_vstream_infos.value()) {
        auto pimpl = make_shared_nothrow<ConfiguredInferModel::Bindings::InferStream::Impl>(vstream_info);
        CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);

        ConfiguredInferModel::Bindings::InferStream stream(pimpl);
        inputs.emplace(vstream_info.name, std::move(stream));
    }

    auto output_vstream_infos = m_cng->get_output_vstream_infos();
    CHECK_EXPECTED(output_vstream_infos);

    for (const auto &vstream_info : output_vstream_infos.value()) {
        auto pimpl = make_shared_nothrow<ConfiguredInferModel::Bindings::InferStream::Impl>(vstream_info);
        CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);

        ConfiguredInferModel::Bindings::InferStream stream(pimpl);
        outputs.emplace(vstream_info.name, std::move(stream));
    }

    return ConfiguredInferModel::Bindings(std::move(inputs), std::move(outputs));
}

hailo_status ConfiguredInferModelImpl::wait_for_async_ready(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    // downcasting from ConfiguredNetworkGroup to ConfiguredNetworkGroupBase since we need some functions from ConfiguredNetworkGroupBase
    std::shared_ptr<ConfiguredNetworkGroupBase> configured_net_group_base = std::dynamic_pointer_cast<ConfiguredNetworkGroupBase>(m_cng);
    CHECK_NOT_NULL(configured_net_group_base, HAILO_INTERNAL_FAILURE);

    auto low_level_queue_size = m_async_infer_runner->get_min_buffer_pool_size(*configured_net_group_base);
    CHECK_EXPECTED_AS_STATUS(low_level_queue_size);

    bool was_successful = m_cv.wait_for(lock, timeout, [this, low_level_queue_size  = low_level_queue_size.value()] () -> bool {
        return m_ongoing_parallel_transfers < low_level_queue_size;
    });
    CHECK(was_successful, HAILO_TIMEOUT);

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelImpl::activate()
{
    auto activated_ng = m_cng->activate();
    CHECK_EXPECTED_AS_STATUS(activated_ng);

    m_ang = activated_ng.release();
    return HAILO_SUCCESS;;
}

void ConfiguredInferModelImpl::deactivate()
{
    m_ang = nullptr;
}

hailo_status ConfiguredInferModelImpl::run(ConfiguredInferModel::Bindings bindings, std::chrono::milliseconds timeout)
{
    auto job = run_async(bindings, [] (const CompletionInfoAsyncInfer &) {});
    CHECK_EXPECTED_AS_STATUS(job);

    auto status = job->wait(timeout);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<AsyncInferJob> ConfiguredInferModelImpl::run_async(ConfiguredInferModel::Bindings bindings,
    std::function<void(const CompletionInfoAsyncInfer &)> callback)
{
    auto job_pimpl = make_shared_nothrow<AsyncInferJob::Impl>(static_cast<uint32_t>(m_input_names.size() + m_output_names.size()));
    CHECK_NOT_NULL_AS_EXPECTED(job_pimpl, HAILO_OUT_OF_HOST_MEMORY);
    AsyncInferJob job(job_pimpl);

    TransferDoneCallbackAsyncInfer transfer_done = [this, bindings, job_pimpl, callback]
    (const CompletionInfoAsyncInferInternal &internal_completion_info) {
        bool should_call_callback = job_pimpl->stream_done();
        if (should_call_callback) {
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ongoing_parallel_transfers--;
            }
            m_cv.notify_all();

            CompletionInfoAsyncInfer completion_info(bindings, internal_completion_info.status);
            callback(completion_info);
        }
    };

    for (const auto &input_name : m_input_names) {
        m_async_infer_runner->set_input(input_name, bindings.input(input_name)->get_buffer(), transfer_done);
    }

    for (const auto &output_name : m_output_names) {
        m_async_infer_runner->set_output(output_name, bindings.output(output_name)->get_buffer(), transfer_done);
    }

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_ongoing_parallel_transfers++;
    }
    m_cv.notify_all();

    auto status = m_async_infer_runner->async_infer();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return job;
}

AsyncInferJob::AsyncInferJob(std::shared_ptr<Impl> pimpl) : m_pimpl(pimpl), m_should_wait_in_dtor(true)
{
}

AsyncInferJob::AsyncInferJob(AsyncInferJob &&other) :
    m_pimpl(std::move(other.m_pimpl)),
    m_should_wait_in_dtor(std::exchange(other.m_should_wait_in_dtor, false))
{
}

AsyncInferJob &AsyncInferJob::operator=(AsyncInferJob &&other)
{
    m_pimpl = std::move(other.m_pimpl);
    m_should_wait_in_dtor = std::exchange(other.m_should_wait_in_dtor, false);
    return *this;
}

AsyncInferJob::~AsyncInferJob()
{
    if (m_should_wait_in_dtor) {
        auto status = wait(std::chrono::milliseconds(WAIT_FOR_ASYNC_IN_DTOR_TIMEOUT));
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Could not finish async infer request! status = {}", status);
        }
    }
}

hailo_status AsyncInferJob::wait(std::chrono::milliseconds timeout)
{
    auto status = m_pimpl->wait(timeout);
    CHECK_SUCCESS(status);

    m_should_wait_in_dtor = false;
    return HAILO_SUCCESS;
}

void AsyncInferJob::detach()
{
    m_should_wait_in_dtor = false;
}

AsyncInferJob::Impl::Impl(uint32_t streams_count)
{
    m_ongoing_transfers = streams_count;
}

hailo_status AsyncInferJob::Impl::wait(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    bool was_successful = m_cv.wait_for(lock, timeout, [this] () -> bool {
        return (0 == m_ongoing_transfers);
    });
    CHECK(was_successful, HAILO_TIMEOUT);

    return HAILO_SUCCESS;
}

bool AsyncInferJob::Impl::stream_done()
{
    bool should_call_callback = false;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_ongoing_transfers--;
        should_call_callback = (0 == m_ongoing_transfers);
    }
    m_cv.notify_all();
    return should_call_callback;
}

ConfiguredInferModel::Bindings::Bindings(std::unordered_map<std::string, Bindings::InferStream> &&inputs,
        std::unordered_map<std::string, Bindings::InferStream> &&outputs) :
    m_inputs(std::move(inputs)), m_outputs(std::move(outputs))
{
}

Expected<ConfiguredInferModel::Bindings::InferStream> ConfiguredInferModel::Bindings::input()
{
    CHECK_AS_EXPECTED(1 == m_inputs.size(), HAILO_INVALID_OPERATION, "Model has more than one input!");
    auto copy = m_inputs.begin()->second;
    return copy;
}

Expected<ConfiguredInferModel::Bindings::InferStream> ConfiguredInferModel::Bindings::output()
{
    CHECK_AS_EXPECTED(1 == m_outputs.size(), HAILO_INVALID_OPERATION, "Model has more than one output!");
    auto copy = m_outputs.begin()->second;
    return copy;
}

Expected<ConfiguredInferModel::Bindings::InferStream> ConfiguredInferModel::Bindings::input(const std::string &name)
{
    CHECK_AS_EXPECTED(contains(m_inputs, name), HAILO_NOT_FOUND, "Input {} not found!", name);
    auto copy = m_inputs.at(name);
    return copy;
}

Expected<ConfiguredInferModel::Bindings::InferStream> ConfiguredInferModel::Bindings::output(const std::string &name)
{
    CHECK_AS_EXPECTED(contains(m_outputs, name), HAILO_NOT_FOUND, "Output {}, not found!", name);
    auto copy = m_outputs.at(name);
    return copy;
}

ConfiguredInferModel::Bindings::InferStream::Impl::Impl(const hailo_vstream_info_t &vstream_info) : m_name(vstream_info.name)
{
}

hailo_status ConfiguredInferModel::Bindings::InferStream::Impl::set_buffer(MemoryView view)
{
    m_view = view;
    return HAILO_SUCCESS;
}

MemoryView ConfiguredInferModel::Bindings::InferStream::Impl::get_buffer()
{
    return m_view;
}

void ConfiguredInferModel::Bindings::InferStream::Impl::set_stream_callback(TransferDoneCallbackAsyncInfer callback)
{
    m_stream_callback = callback;
}

ConfiguredInferModel::Bindings::InferStream::InferStream(std::shared_ptr<Bindings::InferStream::Impl> pimpl) : m_pimpl(pimpl)
{
}

hailo_status ConfiguredInferModel::Bindings::InferStream::set_buffer(MemoryView view)
{
    return m_pimpl->set_buffer(view);
}

MemoryView ConfiguredInferModel::Bindings::InferStream::get_buffer()
{
    return m_pimpl->get_buffer();
}

} /* namespace hailort */
