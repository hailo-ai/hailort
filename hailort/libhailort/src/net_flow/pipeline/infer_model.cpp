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
#include "vdevice/vdevice_internal.hpp"
#include "hef/hef_internal.hpp"
#include "net_flow/pipeline/infer_model_internal.hpp"
#include "net_flow/pipeline/async_infer_runner.hpp"


#define WAIT_FOR_ASYNC_IN_DTOR_TIMEOUT (std::chrono::milliseconds(10000))

namespace hailort
{

std::string InferModel::InferStream::Impl::name() const
{
    return m_vstream_info.name;
}

hailo_3d_image_shape_t InferModel::InferStream::Impl::shape() const
{
    return m_vstream_info.shape;
}

hailo_format_t InferModel::InferStream::Impl::format() const
{
    return m_user_buffer_format;
}

size_t InferModel::InferStream::Impl::get_frame_size() const
{
    return HailoRTCommon::get_frame_size(m_vstream_info, m_user_buffer_format);
}

Expected<hailo_nms_shape_t> InferModel::InferStream::Impl::get_nms_shape() const
{
    CHECK_AS_EXPECTED(HailoRTCommon::is_nms(m_vstream_info.format.order), HAILO_INVALID_OPERATION,
        "Output {} is not NMS", name());
    auto res = m_vstream_info.nms_shape;
    return res;
}

std::vector<hailo_quant_info_t> InferModel::InferStream::Impl::get_quant_infos() const
{
    // TODO: Support quant infos vector
    return {m_vstream_info.quant_info};
}

void InferModel::InferStream::Impl::set_format_type(hailo_format_type_t type)
{
    m_user_buffer_format.type = type;
}

void InferModel::InferStream::Impl::set_format_order(hailo_format_order_t order)
{
    m_user_buffer_format.order = order;
}

bool InferModel::InferStream::Impl::is_nms() const
{
    return HailoRTCommon::is_nms(m_vstream_info.format.order);
}

void InferModel::InferStream::Impl::set_nms_score_threshold(float32_t threshold)
{
    m_nms_score_threshold = threshold;
}

void InferModel::InferStream::Impl::set_nms_iou_threshold(float32_t threshold)
{
    m_nms_iou_threshold = threshold;
}

void InferModel::InferStream::Impl::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    m_nms_max_proposals_per_class = max_proposals_per_class;
    m_vstream_info.nms_shape.max_bboxes_per_class = max_proposals_per_class;
}

void InferModel::InferStream::Impl::set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size)
{
    m_nms_max_accumulated_mask_size = max_accumulated_mask_size;
    m_vstream_info.nms_shape.max_accumulated_mask_size = max_accumulated_mask_size;
}

InferModel::InferStream::InferStream(std::shared_ptr<InferModel::InferStream::Impl> pimpl) : m_pimpl(pimpl)
{
}

const std::string InferModel::InferStream::name() const
{
    return m_pimpl->name();
}

hailo_3d_image_shape_t InferModel::InferStream::shape() const
{
    return m_pimpl->shape();
}

hailo_format_t InferModel::InferStream::format() const
{
    return m_pimpl->format();
}

size_t InferModel::InferStream::get_frame_size() const
{
    return m_pimpl->get_frame_size();
}

Expected<hailo_nms_shape_t> InferModel::InferStream::get_nms_shape() const
{
    return m_pimpl->get_nms_shape();
}

std::vector<hailo_quant_info_t> InferModel::InferStream::get_quant_infos() const
{
    return m_pimpl->get_quant_infos();
}

void InferModel::InferStream::set_format_type(hailo_format_type_t type)
{
    m_pimpl->set_format_type(type);
}

void InferModel::InferStream::set_format_order(hailo_format_order_t order)
{
    m_pimpl->set_format_order(order);
}

bool InferModel::InferStream::is_nms() const
{
    return m_pimpl->is_nms();
}

void InferModel::InferStream::set_nms_score_threshold(float32_t threshold)
{
    m_pimpl->set_nms_score_threshold(threshold);
}

void InferModel::InferStream::set_nms_iou_threshold(float32_t threshold)
{
    m_pimpl->set_nms_iou_threshold(threshold);
}

void InferModel::InferStream::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    m_pimpl->set_nms_max_proposals_per_class(max_proposals_per_class);
}

void InferModel::InferStream::set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size)
{
    m_pimpl->set_nms_max_accumulated_mask_size(max_accumulated_mask_size);
}

InferModel::InferModel(VDevice &vdevice, Hef &&hef, std::unordered_map<std::string, InferModel::InferStream> &&inputs,
        std::unordered_map<std::string, InferModel::InferStream> &&outputs)
    : m_vdevice(vdevice), m_hef(std::move(hef)), m_inputs(std::move(inputs)), m_outputs(std::move(outputs)),
    m_config_params(HailoRTDefaults::get_configure_params())
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
    m_output_names(std::move(other.m_output_names)),
    m_config_params(std::move(other.m_config_params))
{
}

const Hef &InferModel::hef() const
{
    return m_hef;
}

void InferModel::set_batch_size(uint16_t batch_size)
{
    m_config_params.batch_size = batch_size;
}

void InferModel::set_power_mode(hailo_power_mode_t power_mode)
{
    m_config_params.power_mode = power_mode;
}

void InferModel::set_hw_latency_measurement_flags(hailo_latency_measurement_flags_t latency)
{
    m_config_params.latency = latency;
}

Expected<ConfiguredInferModel> InferModel::configure()
{
    auto configure_params = m_vdevice.get().create_configure_params(m_hef);
    CHECK_EXPECTED(configure_params);

    for (auto &network_group_name_params_pair : *configure_params) {
        for (auto &stream_params_name_pair : network_group_name_params_pair.second.stream_params_by_name) {
            stream_params_name_pair.second.flags = HAILO_STREAM_FLAGS_ASYNC;
        }

        for (auto &network_name_params_pair : network_group_name_params_pair.second.network_params_by_name) {
            network_name_params_pair.second.batch_size = m_config_params.batch_size;
        }

        network_group_name_params_pair.second.power_mode = m_config_params.power_mode;
        network_group_name_params_pair.second.latency = m_config_params.latency;
    }

    auto network_groups = m_vdevice.get().configure(m_hef, configure_params.value());
    CHECK_EXPECTED(network_groups);

    CHECK_AS_EXPECTED(1 == network_groups->size(), HAILO_INVALID_HEF,
        "InferModel expects HEF with a single network group. found {}.", network_groups->size());

    // TODO (HRT-11293) : Remove this check
    TRY(auto internal_queue_size, network_groups.value()[0]->get_min_buffer_pool_size());
    CHECK_AS_EXPECTED(internal_queue_size >= m_config_params.batch_size, HAILO_INVALID_OPERATION,
        "Trying to configure a model with a batch={} bigger than internal_queue_size={}, which is not supported. Try using a smaller batch.",
            m_config_params.batch_size, internal_queue_size);

    std::unordered_map<std::string, hailo_format_t> inputs_formats;
    std::unordered_map<std::string, hailo_format_t> outputs_formats;

    auto input_vstream_infos = network_groups.value()[0]->get_input_vstream_infos();
    CHECK_EXPECTED(input_vstream_infos);

    for (const auto &vstream_info : input_vstream_infos.value()) {
        assert(contains(m_inputs, std::string(vstream_info.name)));
        inputs_formats[vstream_info.name] = m_inputs.at(vstream_info.name).format();
    }

    auto output_vstream_infos = network_groups.value()[0]->get_output_vstream_infos();
    CHECK_EXPECTED(output_vstream_infos);

    for (const auto &vstream_info : output_vstream_infos.value()) {
        assert(contains(m_outputs, std::string(vstream_info.name)));
        outputs_formats[vstream_info.name] = m_outputs.at(vstream_info.name).format();
    }

    CHECK_AS_EXPECTED(std::all_of(m_inputs.begin(), m_inputs.end(), [](const auto &input_pair) {
        return ((input_pair.second.m_pimpl->m_nms_score_threshold == INVALID_NMS_CONFIG) &&
                (input_pair.second.m_pimpl->m_nms_iou_threshold == INVALID_NMS_CONFIG) &&
                (input_pair.second.m_pimpl->m_nms_max_accumulated_mask_size == static_cast<uint32_t>(INVALID_NMS_CONFIG)) &&
                (input_pair.second.m_pimpl->m_nms_max_proposals_per_class == static_cast<uint32_t>(INVALID_NMS_CONFIG)));
    }), HAILO_INVALID_OPERATION, "NMS config was changed for input");

    for (const auto &output_pair : m_outputs) {
        auto &edge_name = output_pair.first;
        if ((output_pair.second.m_pimpl->m_nms_score_threshold == INVALID_NMS_CONFIG) &&
            (output_pair.second.m_pimpl->m_nms_iou_threshold == INVALID_NMS_CONFIG) &&
            (output_pair.second.m_pimpl->m_nms_max_accumulated_mask_size == static_cast<uint32_t>(INVALID_NMS_CONFIG)) &&
            (output_pair.second.m_pimpl->m_nms_max_proposals_per_class == static_cast<uint32_t>(INVALID_NMS_CONFIG))) {
                continue;
            }
        if (output_pair.second.m_pimpl->m_nms_score_threshold != INVALID_NMS_CONFIG) {
            auto status = network_groups.value()[0]->set_nms_score_threshold(edge_name, output_pair.second.m_pimpl->m_nms_score_threshold);
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
        if (output_pair.second.m_pimpl->m_nms_iou_threshold != INVALID_NMS_CONFIG) {
            auto status = network_groups.value()[0]->set_nms_iou_threshold(edge_name, output_pair.second.m_pimpl->m_nms_iou_threshold);
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
        if (output_pair.second.m_pimpl->m_nms_max_proposals_per_class != static_cast<uint32_t>(INVALID_NMS_CONFIG)) {
            auto status = network_groups.value()[0]->set_nms_max_bboxes_per_class(edge_name, output_pair.second.m_pimpl->m_nms_max_proposals_per_class);
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
        if (output_pair.second.m_pimpl->m_nms_max_accumulated_mask_size != static_cast<uint32_t>(INVALID_NMS_CONFIG)) {
            auto status = network_groups.value()[0]->set_nms_max_accumulated_mask_size(edge_name, output_pair.second.m_pimpl->m_nms_max_accumulated_mask_size);
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
    }

    auto configured_infer_model_pimpl = ConfiguredInferModelImpl::create(network_groups.value()[0], inputs_formats, outputs_formats,
        get_input_names(), get_output_names(), m_vdevice);
    CHECK_EXPECTED(configured_infer_model_pimpl);

    // The hef buffer is being used only when working with the service.
    // TODO HRT-12636 - Besides clearing the hef buffer, clear also unnecessary members of Hef object.
    // After HRT-12636 is done - The user can configure an infer model only once, with or without the service.
    m_hef.pimpl->clear_hef_buffer();

    return ConfiguredInferModel(configured_infer_model_pimpl.release());
}

Expected<ConfiguredInferModel> InferModel::configure_for_ut(std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
    const std::vector<std::string> &input_names, const std::vector<std::string> &output_names,
    std::shared_ptr<ConfiguredNetworkGroup> net_group)
{
    if (nullptr == net_group) {
        auto configure_params = m_vdevice.get().create_configure_params(m_hef);
        CHECK_EXPECTED(configure_params);

        for (auto &network_group_name_params_pair : *configure_params) {
            for (auto &stream_params_name_pair : network_group_name_params_pair.second.stream_params_by_name) {
                stream_params_name_pair.second.flags = HAILO_STREAM_FLAGS_ASYNC;
            }

            for (auto &network_name_params_pair : network_group_name_params_pair.second.network_params_by_name) {
                network_name_params_pair.second.batch_size = m_config_params.batch_size;
            }

            network_group_name_params_pair.second.power_mode = m_config_params.power_mode;
            network_group_name_params_pair.second.latency = m_config_params.latency;
        }

        auto network_groups = m_vdevice.get().configure(m_hef, configure_params.value());
        CHECK_EXPECTED(network_groups);
        net_group = network_groups.value()[0];
    }

    auto configured_infer_model_pimpl = ConfiguredInferModelImpl::create_for_ut(net_group, async_infer_runner, input_names, output_names);
    CHECK_EXPECTED(configured_infer_model_pimpl);

    return ConfiguredInferModel(configured_infer_model_pimpl.release());
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
    std::function<void(const AsyncInferCompletionInfo &)> callback)
{
    return m_pimpl->run_async(bindings, callback);
}

Expected<LatencyMeasurementResult> ConfiguredInferModel::get_hw_latency_measurement()
{
    return m_pimpl->get_hw_latency_measurement();
}

hailo_status ConfiguredInferModel::set_scheduler_timeout(const std::chrono::milliseconds &timeout)
{
    return m_pimpl->set_scheduler_timeout(timeout);
}

hailo_status ConfiguredInferModel::set_scheduler_threshold(uint32_t threshold)
{
    return m_pimpl->set_scheduler_threshold(threshold);
}

hailo_status ConfiguredInferModel::set_scheduler_priority(uint8_t priority)
{
    return m_pimpl->set_scheduler_priority(priority);
}

Expected<size_t> ConfiguredInferModel::get_async_queue_size()
{
    return m_pimpl->get_async_queue_size();
}

void ConfiguredInferModel::shutdown()
{
    m_pimpl->abort();
}

Expected<std::shared_ptr<ConfiguredInferModelImpl>> ConfiguredInferModelImpl::create(std::shared_ptr<ConfiguredNetworkGroup> net_group,
    const std::unordered_map<std::string, hailo_format_t> &inputs_formats,
    const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
    const std::vector<std::string> &input_names, const std::vector<std::string> &output_names, VDevice &vdevice, const uint32_t timeout)
{
    auto async_infer_runner = AsyncInferRunnerImpl::create(net_group, inputs_formats, outputs_formats, timeout);
    CHECK_EXPECTED(async_infer_runner);

    auto &hw_elem = async_infer_runner.value()->get_async_pipeline()->get_async_hw_element();
    for (auto &pool : hw_elem->get_hw_interacted_buffer_pools_h2d()) {
        if (!pool->is_holding_user_buffers()) {
            CHECK_SUCCESS_AS_EXPECTED(pool->map_to_vdevice(vdevice, HAILO_DMA_BUFFER_DIRECTION_H2D));
        }
    }
    for (auto &pool : hw_elem->get_hw_interacted_buffer_pools_d2h()) {
        if (!pool->is_holding_user_buffers()) {
            CHECK_SUCCESS_AS_EXPECTED(pool->map_to_vdevice(vdevice, HAILO_DMA_BUFFER_DIRECTION_D2H));
        }
    }

    auto configured_infer_model_pimpl = make_shared_nothrow<ConfiguredInferModelImpl>(net_group, async_infer_runner.release(),
        input_names, output_names);
    CHECK_NOT_NULL_AS_EXPECTED(configured_infer_model_pimpl, HAILO_OUT_OF_HOST_MEMORY);

    return configured_infer_model_pimpl;
}

Expected<std::shared_ptr<ConfiguredInferModelImpl>> ConfiguredInferModelImpl::create_for_ut(std::shared_ptr<ConfiguredNetworkGroup> net_group,
    std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner, const std::vector<std::string> &input_names, const std::vector<std::string> &output_names)
{
    auto configured_infer_model_pimpl = make_shared_nothrow<ConfiguredInferModelImpl>(net_group, async_infer_runner,
        input_names, output_names);
    CHECK_NOT_NULL_AS_EXPECTED(configured_infer_model_pimpl, HAILO_OUT_OF_HOST_MEMORY);

    return configured_infer_model_pimpl;
}

ConfiguredInferModelImpl::ConfiguredInferModelImpl(std::shared_ptr<ConfiguredNetworkGroup> cng,
    std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
    const std::vector<std::string> &input_names,
    const std::vector<std::string> &output_names) : m_cng(cng), m_async_infer_runner(async_infer_runner),
    m_ongoing_parallel_transfers(0), m_input_names(input_names), m_output_names(output_names)
{
}

ConfiguredInferModelImpl::~ConfiguredInferModelImpl()
{
    abort();
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
    hailo_status status = HAILO_SUCCESS;
    bool was_successful = m_cv.wait_for(lock, timeout, [this, &status] () -> bool {
        auto pools_are_ready = m_async_infer_runner->can_push_buffers();
        if (HAILO_SUCCESS != pools_are_ready.status()) {
            status = pools_are_ready.status();
            return true;
        }
        return pools_are_ready.release();
    });
    CHECK_SUCCESS(status);

    CHECK(was_successful, HAILO_TIMEOUT, "Got timeout in `wait_for_async_ready`");

    return HAILO_SUCCESS;
}

void ConfiguredInferModelImpl::abort()
{
    m_async_infer_runner->abort();
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait_for(lock, WAIT_FOR_ASYNC_IN_DTOR_TIMEOUT, [this] () -> bool {
        return m_ongoing_parallel_transfers == 0;
    });
}

hailo_status ConfiguredInferModelImpl::activate()
{
    auto activated_ng = m_cng->activate();
    CHECK_EXPECTED_AS_STATUS(activated_ng);

    m_ang = activated_ng.release();
    return HAILO_SUCCESS;
}

void ConfiguredInferModelImpl::deactivate()
{
    m_ang = nullptr;
}

hailo_status ConfiguredInferModelImpl::run(ConfiguredInferModel::Bindings bindings, std::chrono::milliseconds timeout)
{
    auto job = run_async(bindings, [] (const AsyncInferCompletionInfo &) {});
    CHECK_EXPECTED_AS_STATUS(job);

    auto status = job->wait(timeout);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status ConfiguredInferModelImpl::validate_bindings(ConfiguredInferModel::Bindings bindings)
{
    for (const auto &input_name : m_input_names) {
        auto buffer_type = bindings.input(input_name)->m_pimpl->get_type();
        switch (buffer_type) {
            case BufferType::VIEW:
            {
                CHECK_EXPECTED_AS_STATUS(bindings.input(input_name)->get_buffer());
                break;
            }
            case BufferType::PIX_BUFFER:
            {
                CHECK_EXPECTED_AS_STATUS(bindings.input(input_name)->get_pix_buffer());
                break;
            }
            case BufferType::DMA_BUFFER:
            {
                CHECK_EXPECTED_AS_STATUS(bindings.input(input_name)->get_dma_buffer());
                break;
            }
            default:
                CHECK(false, HAILO_NOT_FOUND, "Couldnt find input buffer for '{}'", input_name);
        }
    }
    for (const auto &output_name : m_output_names) {
        auto buffer_type = bindings.output(output_name)->m_pimpl->get_type();
        switch (buffer_type) {
            case BufferType::VIEW:
            {
                CHECK_EXPECTED_AS_STATUS(bindings.output(output_name)->get_buffer());
                break;
            }
            case BufferType::PIX_BUFFER:
            {
                CHECK(false, HAILO_NOT_SUPPORTED, "pix_buffer isn't supported for outputs in '{}'", output_name);
                break;
            }
            case BufferType::DMA_BUFFER:
            {
                CHECK_EXPECTED_AS_STATUS(bindings.output(output_name)->get_dma_buffer());
                break;
            }
            default:
                CHECK(false, HAILO_NOT_FOUND, "Couldnt find output buffer for '{}'", output_name);
        }
    }

    return HAILO_SUCCESS;
}

Expected<AsyncInferJob> ConfiguredInferModelImpl::run_async(ConfiguredInferModel::Bindings bindings,
    std::function<void(const AsyncInferCompletionInfo &)> callback)
{
    CHECK_SUCCESS_AS_EXPECTED(validate_bindings(bindings));

    auto job_pimpl = make_shared_nothrow<AsyncInferJob::Impl>(static_cast<uint32_t>(m_input_names.size() + m_output_names.size()));
    CHECK_NOT_NULL_AS_EXPECTED(job_pimpl, HAILO_OUT_OF_HOST_MEMORY);

    TransferDoneCallbackAsyncInfer transfer_done = [this, bindings, job_pimpl, callback](hailo_status status) {
        bool should_call_callback = job_pimpl->stream_done(status);
        if (should_call_callback) {
            auto final_status = (m_async_infer_runner->get_pipeline_status() == HAILO_SUCCESS) ?
                job_pimpl->completion_status() : m_async_infer_runner->get_pipeline_status();

            AsyncInferCompletionInfo completion_info(bindings, final_status);
            callback(completion_info);
            job_pimpl->mark_callback_done();

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ongoing_parallel_transfers--;
            }
            m_cv.notify_all();
        }
    };

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto status = m_async_infer_runner->run(bindings, transfer_done);
        CHECK_SUCCESS_AS_EXPECTED(status);
        m_ongoing_parallel_transfers++;
    }
    m_cv.notify_all();

    AsyncInferJob job(job_pimpl);
    return job;
}

Expected<LatencyMeasurementResult> ConfiguredInferModelImpl::get_hw_latency_measurement()
{
    return m_cng->get_latency_measurement();
}

hailo_status ConfiguredInferModelImpl::set_scheduler_timeout(const std::chrono::milliseconds &timeout)
{
    return m_cng->set_scheduler_timeout(timeout);
}

hailo_status ConfiguredInferModelImpl::set_scheduler_threshold(uint32_t threshold)
{
    return m_cng->set_scheduler_threshold(threshold);
}

hailo_status ConfiguredInferModelImpl::set_scheduler_priority(uint8_t priority)
{
    return m_cng->set_scheduler_priority(priority);
}

Expected<size_t> ConfiguredInferModelImpl::get_async_queue_size()
{
    return m_cng->get_min_buffer_pool_size();
}

AsyncInferJob::AsyncInferJob(std::shared_ptr<Impl> pimpl) : m_pimpl(pimpl), m_should_wait_in_dtor(true)
{
}

AsyncInferJob::AsyncInferJob(AsyncInferJob &&other) :
    m_pimpl(std::move(other.m_pimpl)), m_should_wait_in_dtor(std::exchange(other.m_should_wait_in_dtor, false))
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
    if (m_pimpl == nullptr) {
        // In case the user defines AsyncInferJob object without initializing it with a real object,
        // the parameter `m_should_wait_in_dtor` is initialized to true and the d'tor calls for `wait()`,
        // but `m_pimpl` is not initialized, resulting in seg-fault.
        return;
    }
    if (m_should_wait_in_dtor) {
        auto status = wait(WAIT_FOR_ASYNC_IN_DTOR_TIMEOUT);
        if (HAILO_SUCCESS != status) {
            LOGGER__CRITICAL("Could not finish async infer request! status = {}", status);
        }
    }
}

hailo_status AsyncInferJob::wait(std::chrono::milliseconds timeout)
{
    m_should_wait_in_dtor = false;
    auto status = m_pimpl->wait(timeout);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

void AsyncInferJob::detach()
{
    m_should_wait_in_dtor = false;
}

AsyncInferJob::Impl::Impl(uint32_t streams_count) : m_job_completion_status(HAILO_SUCCESS)
{
    m_ongoing_transfers = streams_count;
    m_callback_called = false;
}

hailo_status AsyncInferJob::Impl::wait(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    bool was_successful = m_cv.wait_for(lock, timeout, [this] () -> bool {
        return (m_callback_called);
    });
    CHECK(was_successful, HAILO_TIMEOUT, "Waiting for async job to finish has failed with timeout ({}ms)", timeout.count());

    return HAILO_SUCCESS;
}

bool AsyncInferJob::Impl::stream_done(const hailo_status &status)
{
    bool should_call_callback = false;
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_ongoing_transfers--;
        should_call_callback = (0 == m_ongoing_transfers);
        if (HAILO_SUCCESS != status) {
            m_job_completion_status = status;
        }
    }
    return should_call_callback;
}

hailo_status AsyncInferJob::Impl::completion_status()
{
    return m_job_completion_status;
}

void AsyncInferJob::Impl::mark_callback_done()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_callback_called = true;
    }
    m_cv.notify_all();
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

ConfiguredInferModel::Bindings::InferStream::Impl::Impl(const hailo_vstream_info_t &vstream_info) :
    m_name(vstream_info.name),m_buffer_type(BufferType::UNINITIALIZED)
{
}

hailo_status ConfiguredInferModel::Bindings::InferStream::Impl::set_buffer(MemoryView view)
{
    m_view = view;
    m_buffer_type = BufferType::VIEW;
    return HAILO_SUCCESS;
}

Expected<MemoryView> ConfiguredInferModel::Bindings::InferStream::Impl::get_buffer() const
{
    CHECK_AS_EXPECTED(BufferType::VIEW == m_buffer_type, HAILO_INVALID_OPERATION,
        "Trying to get buffer as view for '{}', while it is not configured as view", m_name);
    auto cp = m_view;
    return cp;
}

hailo_status ConfiguredInferModel::Bindings::InferStream::Impl::set_pix_buffer(const hailo_pix_buffer_t &pix_buffer)
{
    m_pix_buffer = pix_buffer;
    m_buffer_type = BufferType::PIX_BUFFER;
    return HAILO_SUCCESS;
}

Expected<hailo_pix_buffer_t> ConfiguredInferModel::Bindings::InferStream::Impl::get_pix_buffer()
{
    CHECK_AS_EXPECTED(BufferType::PIX_BUFFER == m_buffer_type, HAILO_INVALID_OPERATION,
        "Trying to get buffer as pix_buffer for '{}', while it is not configured as pix_buffer", m_name);
    auto cp = m_pix_buffer;
    return cp;
}

hailo_status ConfiguredInferModel::Bindings::InferStream::Impl::set_dma_buffer(hailo_dma_buffer_t dma_buffer)
{
    m_buffer_type = BufferType::DMA_BUFFER;
    m_dma_buffer = dma_buffer;

    return HAILO_SUCCESS;
}

Expected<hailo_dma_buffer_t> ConfiguredInferModel::Bindings::InferStream::Impl::get_dma_buffer()
{
    CHECK_AS_EXPECTED(BufferType::DMA_BUFFER == m_buffer_type, HAILO_INVALID_OPERATION,
        "Trying to get buffer as dma_buffer for '{}', while it is not configured as dma_buffer", m_name);
    auto cp = m_dma_buffer;
    return cp;
}

BufferType ConfiguredInferModel::Bindings::InferStream::Impl::get_type()
{
    return m_buffer_type;
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

hailo_status ConfiguredInferModel::Bindings::InferStream::set_pix_buffer(const hailo_pix_buffer_t &pix_buffer)
{
    return m_pimpl->set_pix_buffer(pix_buffer);
}

hailo_status ConfiguredInferModel::Bindings::InferStream::set_dma_buffer(hailo_dma_buffer_t dma_buffer)
{
    return m_pimpl->set_dma_buffer(dma_buffer);
}

Expected<MemoryView> ConfiguredInferModel::Bindings::InferStream::get_buffer()
{
    return m_pimpl->get_buffer();
}

Expected<hailo_pix_buffer_t> ConfiguredInferModel::Bindings::InferStream::get_pix_buffer()
{
    return m_pimpl->get_pix_buffer();
}

Expected<hailo_dma_buffer_t> ConfiguredInferModel::Bindings::InferStream::get_dma_buffer()
{
    return m_pimpl->get_dma_buffer();
}

} /* namespace hailort */
