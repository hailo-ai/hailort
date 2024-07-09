#ifndef HAILORT_SERVER_HPP_
/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file hailort_server.hpp
 * @brief RPC Hailort Server Header
 **/

#define HAILORT_SERVER_HPP_

#include "hrpc/server.hpp"
#include "hailort_service/cng_buffer_pool.hpp"

namespace hrpc
{

using infer_model_handle_t = uint32_t;

class Server;
class HailoRTServer : public Server {
public:
    static Expected<std::unique_ptr<HailoRTServer>> create_unique();
    explicit HailoRTServer(std::shared_ptr<ConnectionContext> connection_context) : Server(connection_context) {};

    std::unordered_map<uint32_t, uint32_t> &get_infer_model_to_info_id() { return m_infer_model_to_info_id; };
    std::unordered_map<uint32_t, std::shared_ptr<ServiceNetworkGroupBufferPool>> &get_buffer_pool_per_cim() { return m_buffer_pool_per_cim; };
    std::unordered_map<infer_model_handle_t, Buffer> &get_hef_buffers() { return m_hef_buffers_per_infer_model; };

private:

    std::unordered_map<uint32_t, uint32_t> m_infer_model_to_info_id;
    std::unordered_map<uint32_t, std::shared_ptr<ServiceNetworkGroupBufferPool>> m_buffer_pool_per_cim;
    std::unordered_map<infer_model_handle_t, Buffer> m_hef_buffers_per_infer_model;
    virtual hailo_status cleanup_client_resources(RpcConnection client_connection) override;
    void cleanup_cim_buffer_pools(const std::vector<uint32_t> &cim_handles);
    void cleanup_infer_model_hef_buffers(const std::vector<uint32_t> &infer_model_handles);

};

} // namespace hrpc

#endif // HAILORT_SERVER_HPP_