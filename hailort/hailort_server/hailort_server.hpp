/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort_server.hpp
 * @brief RPC Hailort Server Header
 **/

#ifndef HAILORT_SERVER_HPP_
#define HAILORT_SERVER_HPP_

#include "hrpc/server.hpp"
#include "cng_buffer_pool.hpp"
#include "hailo/infer_model.hpp"
#include "utils/thread_safe_map.hpp"
#include "common/object_pool.hpp"

namespace hailort
{

using infer_model_handle_t = uint32_t;

class VDeviceManager final
{
public:
    VDeviceManager() = default;

    Expected<std::unique_ptr<VDevice>> create_vdevice(const hailo_vdevice_params_t &params, uint32_t client_id);
    void mark_vdevice_for_close(uint32_t client_id);
    void remove_vdevice(uint32_t client_id);

private:
    std::string m_active_vdevice_group_id;
    std::set<uint32_t> m_active_clients_with_vdevice;
    std::set<uint32_t> m_pending_close_clients_with_vdevice;
    std::queue<uint32_t> m_requesting_clients_queue;
    std::mutex m_vdevice_clients_mutex;
    std::condition_variable m_vdevice_clients_cv;
};

struct RunAsyncInfo
{
    ConfiguredInferModel::Bindings bindings;
    std::vector<Pooled<Buffer>> buffer_inputs;
    std::vector<Pooled<Buffer>> buffer_outputs;
    std::vector<std::shared_ptr<FileDescriptor>> fd_inputs;
    std::vector<std::shared_ptr<FileDescriptor>> fd_outputs;
};

class Server;
class ConfiguredInferModelRunAsyncHandler;
class HailoRTServer : public Server {
public:
    static Expected<std::unique_ptr<HailoRTServer>> create_unique(const std::string& ip = "");
    explicit HailoRTServer(std::shared_ptr<ConnectionContext> connection_context, std::shared_ptr<std::mutex> write_mutex,
        bool is_unix_socket) : Server(connection_context, write_mutex, [this] (uint32_t client_id) {
            m_vdevice_manager.mark_vdevice_for_close(client_id);
        }), m_is_unix_socket(is_unix_socket) {}

    virtual ~HailoRTServer() = default;

    void cleanup_cim_buffer_pools(const std::vector<uint32_t> &cim_handles);
    Expected<Dispatcher> create_dispatcher();

    hailo_status handle_vdevice_create(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_vdevice_destroy(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_vdevice_create_infer_model(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_infer_model_destroy(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_infer_model_create_configured_infer_model(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_destroy(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_set_scheduler_timeout(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_set_scheduler_threshold(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_set_scheduler_priority(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_get_hw_latency_measurement(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_activate(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_deactivate(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_shutdown(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_update_cache_offset(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_init_cache(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_finalize_cache(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_configured_infer_model_run_async(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_create(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_destroy(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_identify(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_extended_info(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_get_chip_temperature(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_query_health_stats(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_query_performance_stats(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_power_measurement(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_set_power_measurement(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_start_power_measurement(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_get_power_measurement(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_stop_power_measurement(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_get_architecture(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_set_notification_callback(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_remove_notification_callback(const MemoryView&, ClientConnectionPtr, ResponseWriter);
    hailo_status handle_device_fetch_logs(const MemoryView&, ClientConnectionPtr, ResponseWriter);

    friend class ConfiguredInferModelRunAsyncHandler;

private:
    virtual hailo_status cleanup_client_resources(ClientConnectionPtr client_connection) override;
    void cleanup_infer_model_infos(const std::vector<uint32_t> &infer_model_handles);

    std::unordered_map<uint32_t, uint32_t> m_infer_model_to_info_id;
    ThreadSafeMap<uint32_t, std::shared_ptr<ServerNetworkGroupBufferPool>> m_buffer_pool_per_cim;
    ThreadSafeMap<uint32_t, ObjectPoolPtr<RunAsyncInfo>> m_run_async_info_per_cim;
    bool m_is_unix_socket;
    VDeviceManager m_vdevice_manager;
};

class HailoRTServerActionHandler : public ActionHandler {
public:
    HailoRTServerActionHandler(HailoRTServer &server) : m_server(server) {}
    virtual ~HailoRTServerActionHandler() = default;

protected:
    HailoRTServer &m_server;
};

class VDeviceCreateInferModelHandler final : public HailoRTServerActionHandler {
public:
    VDeviceCreateInferModelHandler(HailoRTServer &server) : HailoRTServerActionHandler(server) {}
    virtual hailo_status parse_request(const MemoryView &request, ClientConnectionPtr client_connection) override;
    virtual hailo_status do_action(ResponseWriter response_writer) override;

private:
    uint32_t m_vdevice_handle;
    std::string m_name;
    BufferPtr m_hef_buffer;
    uint32_t m_client_id;
};

class ConfiguredInferModelRunAsyncHandler final : public HailoRTServerActionHandler {
public:
    ConfiguredInferModelRunAsyncHandler(HailoRTServer &server) : HailoRTServerActionHandler(server) {}
    virtual hailo_status parse_request(const MemoryView &request, ClientConnectionPtr client_connection) override;
    virtual hailo_status do_action(ResponseWriter response_writer) override;

private:
    Pooled<RunAsyncInfo> m_run_async_info;
    uint32_t m_configured_infer_model_handle;
};

} // namespace hailort

#endif // HAILORT_SERVER_HPP_