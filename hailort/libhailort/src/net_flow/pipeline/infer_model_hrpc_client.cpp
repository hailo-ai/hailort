/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model_hrpc_client.cpp
 * @brief InferModel HRPC client implementation
 **/

#include "infer_model_hrpc_client.hpp"
#include "configured_infer_model_hrpc_client.hpp"

namespace hailort
{

Expected<std::shared_ptr<InferModelHrpcClient>> InferModelHrpcClient::create(Hef &&hef, const std::string &network_name,
    std::shared_ptr<Client> client, uint32_t infer_model_handle_id, uint32_t vdevice_handle, VDevice &vdevice,
    std::shared_ptr<CallbacksDispatcher> callbacks_dispatcher)
{
    TRY(auto inputs, create_infer_stream_inputs(hef, network_name));
    TRY(auto outputs, create_infer_stream_outputs(hef, network_name));

    auto ptr = make_shared_nothrow<InferModelHrpcClient>(client, infer_model_handle_id,
        vdevice_handle, vdevice, callbacks_dispatcher, std::move(hef), network_name, std::move(inputs), std::move(outputs));
    CHECK_NOT_NULL_AS_EXPECTED(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

InferModelHrpcClient::InferModelHrpcClient(std::shared_ptr<Client> client, uint32_t handle,
    uint32_t vdevice_handle, VDevice &vdevice, std::shared_ptr<CallbacksDispatcher> callbacks_dispatcher,
    Hef &&hef, const std::string &network_name, std::vector<InferStream> &&inputs, std::vector<InferStream> &&outputs) :
        InferModelBase(vdevice, std::move(hef), network_name, std::move(inputs), std::move(outputs)),
        m_client(client),
        m_handle(handle),
        m_vdevice_handle(vdevice_handle),
        m_callbacks_dispatcher(callbacks_dispatcher)
{
}

InferModelHrpcClient::~InferModelHrpcClient()
{
    if (INVALID_HANDLE_ID == m_handle) {
        return;
    }

    auto request = DestroyInferModelSerializer::serialize_request(m_handle);
    if (!request) {
        LOGGER__CRITICAL("Failed to serialize InferModel_release request");
        return;
    }

    auto client = m_client.lock();
    if (client) {
        auto execute_request_result = client->execute_request(HailoRpcActionID::INFER_MODEL__DESTROY, MemoryView(*request));
        if (!execute_request_result) {
            LOGGER__CRITICAL("Failed to destroy infer model! status = {}", execute_request_result.status());
            return;
        }

        auto deserialize_reply_result = DestroyInferModelSerializer::deserialize_reply(MemoryView(*execute_request_result));
        if (HAILO_SUCCESS != deserialize_reply_result) {
            LOGGER__CRITICAL("Failed to destroy infer model! status = {}", deserialize_reply_result);
            return;
        }
    }
}

Expected<ConfiguredInferModel> InferModelHrpcClient::configure()
{
    rpc_create_configured_infer_model_request_params_t request_params;
    for (const auto &input : m_inputs) {
        rpc_stream_params_t current_stream_params;
        current_stream_params.format_order = static_cast<uint32_t>(input.second.format().order);
        current_stream_params.format_type = static_cast<uint32_t>(input.second.format().type);
        current_stream_params.nms_iou_threshold = input.second.nms_iou_threshold();
        current_stream_params.nms_score_threshold = input.second.nms_score_threshold();
        current_stream_params.nms_max_proposals_per_class = input.second.nms_max_proposals_per_class();
        current_stream_params.nms_max_proposals_total = input.second.nms_max_proposals_total();
        current_stream_params.nms_max_accumulated_mask_size = input.second.nms_max_accumulated_mask_size();

        request_params.input_streams_params[input.second.name()] = current_stream_params;
    }

    for (const auto &output : m_outputs) {
        rpc_stream_params_t current_stream_params;
        current_stream_params.format_order = static_cast<uint32_t>(output.second.format().order);
        current_stream_params.format_type = static_cast<uint32_t>(output.second.format().type);
        current_stream_params.nms_iou_threshold = output.second.nms_iou_threshold();
        current_stream_params.nms_score_threshold = output.second.nms_score_threshold();
        current_stream_params.nms_max_proposals_per_class = output.second.nms_max_proposals_per_class();
        current_stream_params.nms_max_proposals_total = output.second.nms_max_proposals_total();
        current_stream_params.nms_max_accumulated_mask_size = output.second.nms_max_accumulated_mask_size();

        request_params.output_streams_params[output.second.name()] = current_stream_params;
    }

    request_params.batch_size = m_config_params.batch_size;
    request_params.power_mode = m_config_params.power_mode;
    request_params.latency_flag = m_config_params.latency;
    request_params.infer_model_handle = m_handle;
    request_params.vdevice_handle = m_vdevice_handle;

    TRY(auto request, CreateConfiguredInferModelSerializer::serialize_request(request_params));
    auto client = m_client.lock();
    CHECK_AS_EXPECTED(nullptr != client, HAILO_INTERNAL_FAILURE,
        "Lost comunication with the server. This may happen if VDevice is released while the InferModel is in use.");
    TRY(auto result, client->execute_request(HailoRpcActionID::INFER_MODEL__CREATE_CONFIGURED_INFER_MODEL,
        MemoryView(request)));
    TRY(auto tuple, CreateConfiguredInferModelSerializer::deserialize_reply(MemoryView(result)));
    CHECK_SUCCESS_AS_EXPECTED(std::get<0>(tuple));
    auto configured_infer_model_handle = std::get<1>(tuple);
    auto async_queue_size = std::get<2>(tuple);

    std::unordered_map<std::string, size_t> inputs_frame_sizes;
    std::unordered_map<std::string, size_t> outputs_frame_sizes;
    for (const auto &input : m_inputs) {
        inputs_frame_sizes.emplace(input.second.name(), input.second.get_frame_size());
    }
    for (const auto &output : m_outputs) {
        outputs_frame_sizes.emplace(output.second.name(), output.second.get_frame_size());
    }

    auto callbacks_queue = make_shared_nothrow<CallbacksQueue>(m_output_names);
    CHECK_NOT_NULL_AS_EXPECTED(callbacks_queue, HAILO_OUT_OF_HOST_MEMORY);

    m_callbacks_dispatcher->add(configured_infer_model_handle, callbacks_queue);

    TRY(auto input_vstream_infos, m_hef.get_input_vstream_infos());
    TRY(auto output_vstream_infos, m_hef.get_output_vstream_infos());
    TRY(auto cim_client_ptr, ConfiguredInferModelHrpcClient::create(client,
        configured_infer_model_handle,
        std::move(input_vstream_infos), std::move(output_vstream_infos),
        async_queue_size, callbacks_queue, m_handle,
        inputs_frame_sizes, outputs_frame_sizes));

    return ConfiguredInferModelBase::create(cim_client_ptr);
}

Expected<ConfiguredInferModel> InferModelHrpcClient::configure_for_ut(std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
    const std::vector<std::string> &input_names, const std::vector<std::string> &output_names,
    const std::unordered_map<std::string, size_t> inputs_frame_sizes,
    const std::unordered_map<std::string, size_t> outputs_frame_sizes,
    std::shared_ptr<ConfiguredNetworkGroup> net_group)
{
    (void)async_infer_runner;
    (void)input_names;
    (void)output_names;
    (void)net_group;
    (void)inputs_frame_sizes;
    (void)outputs_frame_sizes;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

} /* namespace hailort */
