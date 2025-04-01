/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_model_hrpc_client.hpp
 * @brief Infer model HRPC client, represents the user's handle to the InferModel object
 **/

#ifndef _HAILO_INFER_MODEL_HRPC_CLIENT_HPP_
#define _HAILO_INFER_MODEL_HRPC_CLIENT_HPP_

#include "hailo/hailort.h"
#include "hailo/infer_model.hpp"
#include "hrpc/client.hpp"
#include "net_flow/pipeline/infer_model_internal.hpp"
#include "rpc_callbacks/rpc_callbacks_dispatcher.hpp"

#define CREATE_CONFIGURED_INFER_MODEL_PROTO_MAX_SIZE (2048)

namespace hailort
{

class InferModelHrpcClient : public InferModelBase
{
public:
    static Expected<std::shared_ptr<InferModelHrpcClient>> create(Hef &&hef, const std::string &network_name,
        std::shared_ptr<Client> client, uint32_t infer_model_handle_id, uint32_t vdevice_handle, VDevice &vdevice,
        std::shared_ptr<ClientCallbackDispatcherManager> callback_dispatcher_manager);

    InferModelHrpcClient(std::shared_ptr<Client> client, uint32_t id,
        uint32_t vdevice_handle, VDevice &vdevice, std::shared_ptr<ClientCallbackDispatcherManager> callback_dispatcher_manager,
        Hef &&hef, const std::string &network_name, std::vector<InferStream> &&inputs, std::vector<InferStream> &&outputs);
    virtual ~InferModelHrpcClient();

    InferModelHrpcClient(const InferModelHrpcClient &) = delete;
    InferModelHrpcClient &operator=(const InferModelHrpcClient &) = delete;
    InferModelHrpcClient(InferModelHrpcClient &&) = delete;
    InferModelHrpcClient &operator=(InferModelHrpcClient &&) = delete;

    virtual Expected<ConfiguredInferModel> configure() override;

    virtual Expected<ConfiguredInferModel> configure_for_ut(std::shared_ptr<AsyncInferRunnerImpl> async_infer_runner,
        const std::vector<std::string> &input_names, const std::vector<std::string> &output_names,
        const std::unordered_map<std::string, size_t> inputs_frame_sizes = {},
        const std::unordered_map<std::string, size_t> outputs_frame_sizes = {},
        std::shared_ptr<ConfiguredNetworkGroup> net_group = nullptr) override;

private:
    std::weak_ptr<Client> m_client;
    uint32_t m_handle;
    uint32_t m_vdevice_handle;
    std::shared_ptr<ClientCallbackDispatcherManager> m_callback_dispatcher_manager;
};

} /* namespace hailort */

#endif /* _HAILO_INFER_MODEL_HRPC_CLIENT_HPP_ */
