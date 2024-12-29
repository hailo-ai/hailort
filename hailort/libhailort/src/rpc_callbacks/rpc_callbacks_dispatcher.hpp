/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file rpc_callbacks_dispatcher.hpp
 * @brief Dispatches callbacks to its specified destination (for each configured infer model).
 **/

#ifndef _HAILO_RPC_CALLBACKS_DISPATCHER_HPP_
#define _HAILO_RPC_CALLBACKS_DISPATCHER_HPP_

#include "hailo/infer_model.hpp"
#include "net_flow/pipeline/infer_model_internal.hpp"
#include "hrpc_protocol/serializer.hpp"

namespace hailort
{

using callback_id_t = uint32_t;
class CallbacksQueue;
class CallbacksDispatcher
{
public:
    void add(rpc_object_handle_t cim_handle, std::shared_ptr<CallbacksQueue> callbacks_queue)
    {
        m_callbacks_dispatcher[cim_handle] = callbacks_queue;
    }

    std::shared_ptr<CallbacksQueue> at(rpc_object_handle_t cim_handle)
    {
        return m_callbacks_dispatcher.at(cim_handle);
    }

private:
    std::unordered_map<rpc_object_handle_t, std::shared_ptr<CallbacksQueue>> m_callbacks_dispatcher;
};

class AsyncInferJobHrpcClient : public AsyncInferJobBase
{
public:
    AsyncInferJobHrpcClient(EventPtr event);

    virtual hailo_status wait(std::chrono::milliseconds timeout) override;
    hailo_status set_status(hailo_status status);

private:
    EventPtr m_event;
    std::atomic<hailo_status> m_job_status;
};

class CallbacksQueue
{
public:
    CallbacksQueue(const std::vector<std::string> &outputs_names);
    ~CallbacksQueue();

    CallbacksQueue(const CallbacksQueue &other) = delete;
    CallbacksQueue& operator=(const CallbacksQueue &other) = delete;
    CallbacksQueue(CallbacksQueue &&other) = delete;
    CallbacksQueue& operator=(CallbacksQueue &&other) = delete;

    Expected<std::shared_ptr<AsyncInferJobHrpcClient>> register_callback(callback_id_t id,
        const ConfiguredInferModel::Bindings &bindings,
        std::function<void(const AsyncInferCompletionInfo&)> callback);
    hailo_status push_callback(hailo_status callback_status, rpc_object_handle_t callback_handle_id,
        RpcConnection connection);
    hailo_status shutdown(hailo_status status);

private:
    const std::vector<std::string> m_outputs_names;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<callback_id_t> m_callbacks_queue;
    std::unordered_map<callback_id_t, std::function<void(const AsyncInferCompletionInfo&)>> m_callbacks;
    std::atomic_bool m_is_running;
    std::thread m_callback_thread;
    std::unordered_map<callback_id_t, ConfiguredInferModel::Bindings> m_bindings;
    std::unordered_map<callback_id_t, hailo_status> m_callbacks_status;
};

} /* namespace hailort */

#endif /* _HAILO_RPC_CALLBACKS_DISPATCHER_HPP_ */
