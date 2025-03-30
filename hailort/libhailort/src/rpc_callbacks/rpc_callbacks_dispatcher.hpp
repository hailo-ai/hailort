/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file rpc_callbacks_dispatcher.hpp
 * @brief Dispatches callbacks to its specified destination (for each configured infer model).
 **/

#ifndef _HAILO_RPC_CALLBACKS_DISPATCHER_HPP_
#define _HAILO_RPC_CALLBACKS_DISPATCHER_HPP_

#include "hailo/infer_model.hpp"
#include "hrpc_protocol/serializer.hpp"
#include "vdma/channel/transfer_common.hpp"
#include "hrpc/rpc_connection.hpp"

namespace hailort
{

class ClientCallbackDispatcher
{
public:
    using CallbackFunc = std::function<void(const RpcCallback&, hailo_status)>;
    using AdditionalReadsFunc = std::function<Expected<std::vector<TransferBuffer>>(const RpcCallback&)>;

    ClientCallbackDispatcher(uint32_t dispatcher_id, RpcCallbackType callback_type,
        bool should_remove_callback_after_trigger);
    ~ClientCallbackDispatcher();

    ClientCallbackDispatcher(const ClientCallbackDispatcher &other) = delete;
    ClientCallbackDispatcher& operator=(const ClientCallbackDispatcher &other) = delete;
    ClientCallbackDispatcher(ClientCallbackDispatcher &&other) = delete;
    ClientCallbackDispatcher& operator=(ClientCallbackDispatcher &&other) = delete;

    void add_additional_reads(uint32_t callback_id, AdditionalReadsFunc additional_reads_func);
    void register_callback(uint32_t callback_id, CallbackFunc callback);
    hailo_status remove_callback(uint32_t callback_id);
    hailo_status trigger_callback(const RpcCallback &rpc_callback, RpcConnection connection);
    hailo_status shutdown(hailo_status status);
    uint32_t id() const { return m_dispatcher_id; }

private:
    const uint32_t m_dispatcher_id;
    const RpcCallbackType m_callback_type;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<RpcCallback> m_triggered_callbacks;
    std::unordered_map<uint32_t, CallbackFunc> m_registered_callbacks;
    std::unordered_map<uint32_t, AdditionalReadsFunc> m_additional_reads_funcs;
    std::atomic_bool m_is_running;
    std::thread m_callback_thread;
};

class ClientCallbackDispatcherManager
{
public:
    ClientCallbackDispatcherManager() : m_dispatcher_count(0) {} // TODO: move this module near client.cpp and add it to its CMake
    Expected<std::shared_ptr<ClientCallbackDispatcher>> new_dispatcher(RpcCallbackType callback_type, bool should_remove_callback_after_trigger);
    hailo_status remove_dispatcher(uint32_t dispatcher_id);
    std::shared_ptr<ClientCallbackDispatcher> at(uint32_t dispatcher_id);

private:
    std::unordered_map<uint32_t, std::shared_ptr<ClientCallbackDispatcher>> m_dispatchers;
    uint32_t m_dispatcher_count;
    std::mutex m_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_RPC_CALLBACKS_DISPATCHER_HPP_ */
