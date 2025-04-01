/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file rpc_callbacks_dispatcher.cpp
 * @brief Implementation of the dispatcher and the callbacks queue
 **/

#include "rpc_callbacks_dispatcher.hpp"

namespace hailort
{

ClientCallbackDispatcher::ClientCallbackDispatcher(uint32_t dispatcher_id, RpcCallbackType callback_type,
    bool should_remove_callback_after_trigger) : m_dispatcher_id(dispatcher_id), m_callback_type(callback_type)
{
    m_is_running = true;
    m_callback_thread = std::thread([this, should_remove_callback_after_trigger] {
        while (true) {
            RpcCallback rpc_callback = {};
            ClientCallbackDispatcher::CallbackFunc cb;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] {
                    if (!m_is_running) {
                        return true;
                    }

                    if (m_triggered_callbacks.empty()) {
                        return false;
                    }

                    auto rpc_callback = m_triggered_callbacks.front();
                    return contains(m_registered_callbacks, rpc_callback.callback_id);
                });
                if (!m_is_running) {
                    break;
                }

                rpc_callback = m_triggered_callbacks.front();
                m_triggered_callbacks.pop();

                auto callback_id = rpc_callback.callback_id;
                cb = m_registered_callbacks[callback_id];
                if (should_remove_callback_after_trigger) {
                    m_registered_callbacks.erase(callback_id);
                }
            }
            cb(rpc_callback, HAILO_UNINITIALIZED);
        }
    });
}

hailo_status ClientCallbackDispatcher::shutdown(hailo_status status)
{
    if (!m_is_running) {
        return HAILO_SUCCESS;
    }

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_is_running = false;

        m_additional_reads_funcs.clear();
        for (const auto &callback : m_registered_callbacks) {
            RpcCallback rpc_callback = {};
            rpc_callback.type = RpcCallbackType::INVALID;
            callback.second(rpc_callback, status);
        }
        m_registered_callbacks.clear();
    }
    m_cv.notify_one();

    return HAILO_SUCCESS;
}

ClientCallbackDispatcher::~ClientCallbackDispatcher()
{
    auto status = shutdown(HAILO_COMMUNICATION_CLOSED);
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to shutdown callbacks dispatcher, status = {}", status);
    }

    if (m_callback_thread.joinable()) {
        m_callback_thread.join();
    }
}

void ClientCallbackDispatcher::add_additional_reads(uint32_t callback_id, AdditionalReadsFunc additional_reads_func)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_additional_reads_funcs[callback_id] = additional_reads_func;
}

void ClientCallbackDispatcher::register_callback(uint32_t callback_id, ClientCallbackDispatcher::CallbackFunc callback_func)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_registered_callbacks[callback_id] = callback_func;
    }
    m_cv.notify_one();
}

hailo_status ClientCallbackDispatcher::remove_callback(uint32_t callback_id)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    CHECK(contains(m_registered_callbacks, callback_id), HAILO_NOT_FOUND, "Did not find callback with id {}", callback_id);
    m_registered_callbacks.erase(callback_id);
    if (contains(m_additional_reads_funcs, callback_id)) {
        m_additional_reads_funcs.erase(callback_id);
    }
    return HAILO_SUCCESS;
}

hailo_status ClientCallbackDispatcher::trigger_callback(const RpcCallback &callback, RpcConnection connection)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        CHECK(callback.type == m_callback_type, HAILO_INTERNAL_FAILURE, "Callback type mismatch!, expected = {}, got = {}",
            static_cast<uint32_t>(m_callback_type), static_cast<uint32_t>(callback.type));

        if (contains(m_additional_reads_funcs, callback.callback_id)) {
            auto additional_reads_func = m_additional_reads_funcs[callback.callback_id];
            m_additional_reads_funcs.erase(callback.callback_id);
            TRY(auto transfers, additional_reads_func(callback));
            if (!transfers.empty()) {
                auto status = connection.read_buffers(std::move(transfers));
                // TODO: Errors here should be unrecoverable (HRT-14275)
                CHECK_SUCCESS(status);
            }
        }

        m_triggered_callbacks.push(callback);
    }

    m_cv.notify_one();
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<ClientCallbackDispatcher>> ClientCallbackDispatcherManager::new_dispatcher(RpcCallbackType callback_type,
    bool should_remove_callback_after_trigger)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto dispatcher_id = m_dispatcher_count++;
    auto ptr = make_shared_nothrow<ClientCallbackDispatcher>(dispatcher_id, callback_type, should_remove_callback_after_trigger);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);
    m_dispatchers[dispatcher_id] = ptr;
    return ptr;
}

hailo_status ClientCallbackDispatcherManager::remove_dispatcher(uint32_t dispatcher_id)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto dispatcher = m_dispatchers.find(dispatcher_id);
    CHECK(dispatcher != m_dispatchers.end(), HAILO_NOT_FOUND, "Did not find dispatcher with id {}", dispatcher_id);
    m_dispatchers.erase(dispatcher);
    return HAILO_SUCCESS;
}

std::shared_ptr<ClientCallbackDispatcher> ClientCallbackDispatcherManager::at(uint32_t dispatcher_id)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_dispatchers.at(dispatcher_id);
}

} // namespace hailort
