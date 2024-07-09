/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
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

namespace hailort
{

class InferModelHrpcClient : public InferModelBase
{
public:
    static Expected<std::shared_ptr<InferModelHrpcClient>> create(Hef &&hef,
        std::shared_ptr<hrpc::Client> client, uint32_t infer_model_handle_id,
            uint32_t vdevice_handle, VDevice &vdevice);

    InferModelHrpcClient(std::shared_ptr<hrpc::Client> client, uint32_t id,
        uint32_t vdevice_handle, VDevice &vdevice, Hef &&hef, std::unordered_map<std::string, InferStream> &&inputs,
        std::unordered_map<std::string, InferStream> &&outputs);
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
    std::weak_ptr<hrpc::Client> m_client;
    uint32_t m_handle;
    uint32_t m_vdevice_handle;
};

} /* namespace hailort */

#endif /* _HAILO_INFER_MODEL_HRPC_CLIENT_HPP_ */
