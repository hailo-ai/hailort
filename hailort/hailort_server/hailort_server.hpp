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
#include "hailo/infer_model.hpp"
#include "utils/thread_safe_map.hpp"

namespace hailort
{

using infer_model_handle_t = uint32_t;

struct FinishedInferRequest
{
public:
    FinishedInferRequest() : completion_info(HAILO_UNINITIALIZED) {}
    RpcConnection connection;
    hailort::AsyncInferCompletionInfo completion_info;
    uint32_t callback_id;
    uint32_t configured_infer_model_handle;
    std::vector<BufferPtr> outputs;
    std::vector<std::string> outputs_names;
};

class Server;
class HailoRTServer : public Server {
public:
    static Expected<std::unique_ptr<HailoRTServer>> create_unique();
    explicit HailoRTServer(std::shared_ptr<ConnectionContext> connection_context,
        std::shared_ptr<SpscQueue<FinishedInferRequest>> callbacks_done_queue,
        EventPtr callbacks_queue_shutdown_event);
    virtual ~HailoRTServer();

    std::unordered_map<uint32_t, uint32_t> &infer_model_to_info_id() { return m_infer_model_to_info_id; };
    ThreadSafeMap<uint32_t, std::shared_ptr<ServiceNetworkGroupBufferPool>> &buffer_pool_per_cim() { return m_buffer_pool_per_cim; };
    std::unordered_map<infer_model_handle_t, Buffer> &hef_buffers() { return m_hef_buffers_per_infer_model; };
    std::shared_ptr<SpscQueue<FinishedInferRequest>> &callbacks_done_queue() { return m_callbacks_done_queue; };


    void cleanup_cim_buffer_pools(const std::vector<uint32_t> &cim_handles);

private:
    virtual hailo_status cleanup_client_resources(RpcConnection client_connection) override;
    void cleanup_infer_model_hef_buffers(const std::vector<uint32_t> &infer_model_handles);
    hailo_status callbacks_thread_loop();

    std::unordered_map<uint32_t, uint32_t> m_infer_model_to_info_id;
    ThreadSafeMap<uint32_t, std::shared_ptr<ServiceNetworkGroupBufferPool>> m_buffer_pool_per_cim;
    std::mutex m_buffer_pool_mutex;
    std::unordered_map<infer_model_handle_t, Buffer> m_hef_buffers_per_infer_model;
    std::shared_ptr<SpscQueue<FinishedInferRequest>> m_callbacks_done_queue;
    EventPtr m_callbacks_queue_shutdown_event;
    std::thread m_callbacks_thread;
};

} // namespace hailort

#endif // HAILORT_SERVER_HPP_