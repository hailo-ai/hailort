/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file rpc_callbacks_dispatcher.cpp
 * @brief Implementation of the dispatcher and the callbacks queue
 **/

#include "rpc_callbacks_dispatcher.hpp"

namespace hailort
{

AsyncInferJobHrpcClient::AsyncInferJobHrpcClient(EventPtr event) : m_event(event), m_job_status(HAILO_UNINITIALIZED)
{
}

hailo_status AsyncInferJobHrpcClient::wait(std::chrono::milliseconds timeout)
{
    auto status = m_event->wait(timeout);
    if (HAILO_UNINITIALIZED != m_job_status) {
        return m_job_status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status AsyncInferJobHrpcClient::set_status(hailo_status status)
{
    m_job_status = status;
    return m_event->signal();
}

CallbacksQueue::CallbacksQueue(const std::vector<std::string> &outputs_names) : m_outputs_names(outputs_names)
{
    m_is_running = true;
    m_callback_thread = std::thread([this] {
        while (true) {
            callback_id_t callback_id;
            hailo_status info_status = HAILO_UNINITIALIZED;
            std::function<void(const AsyncInferCompletionInfo&)> cb;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return !m_is_running || !m_callbacks_queue.empty(); });
                if (!m_is_running) {
                    break;
                }

                callback_id = m_callbacks_queue.front();
                m_callbacks_queue.pop();

                m_cv.wait(lock, [this, callback_id] { return !m_is_running || (m_callbacks.find(callback_id) != m_callbacks.end()); });
                if (!m_is_running) {
                    break;
                }

                info_status = m_callbacks_status[callback_id];
                cb = m_callbacks[callback_id];
                m_callbacks.erase(callback_id);
                m_callbacks_status.erase(callback_id);
                m_bindings.erase(callback_id);
            }
            AsyncInferCompletionInfo info(info_status);
            cb(info);
        }
    });
}

hailo_status CallbacksQueue::shutdown(hailo_status status)
{
    if (!m_is_running) {
        return HAILO_SUCCESS;
    }

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_is_running = false;

        for (const auto &callback : m_callbacks) {
            m_callbacks_status.erase(callback.first);
            m_bindings.erase(callback.first);
            AsyncInferCompletionInfo info(status);
            callback.second(info);
        }
        m_callbacks.clear();
    }
    m_cv.notify_one();

    return HAILO_SUCCESS;
}

CallbacksQueue::~CallbacksQueue()
{
    auto status = shutdown(HAILO_COMMUNICATION_CLOSED);
    if (HAILO_SUCCESS != status) {
        LOGGER__CRITICAL("Failed to shutdown callbacks dispatcher, status = {}", status);
    }

    if (m_callback_thread.joinable()) {
        m_callback_thread.join();
    }
}

Expected<std::shared_ptr<AsyncInferJobHrpcClient>> CallbacksQueue::register_callback(callback_id_t id,
    const ConfiguredInferModel::Bindings &bindings,
    std::function<void(const AsyncInferCompletionInfo&)> callback)
{
    TRY(auto event_ptr, Event::create_shared(Event::State::not_signalled));

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_bindings[id] = bindings;
        m_callbacks_status[id] = HAILO_SUCCESS;
        m_callbacks[id] = [callback, event_ptr] (const AsyncInferCompletionInfo &info) {
            callback(info);
            auto status = event_ptr->signal();
            if (HAILO_SUCCESS != status) {
                LOGGER__CRITICAL("Could not signal event! status = {}", status);
            }
        };
    }
    m_cv.notify_one();

    auto ptr = make_shared_nothrow<AsyncInferJobHrpcClient>(event_ptr);
    CHECK_NOT_NULL(ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

hailo_status CallbacksQueue::push_callback(hailo_status callback_status, rpc_object_handle_t callback_handle_id,
    RpcConnection connection)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        CHECK(contains(m_callbacks, callback_handle_id), HAILO_NOT_FOUND, "Callback handle (id={}) not found!", callback_handle_id);
        m_callbacks_status[callback_handle_id] = callback_status;

        if (HAILO_SUCCESS == callback_status) {
            CHECK(contains(m_bindings, callback_handle_id), HAILO_NOT_FOUND, "Callback handle not found!");
            for (const auto &output_name : m_outputs_names) {
                TRY(auto buffer, m_bindings[callback_handle_id].output(output_name)->get_buffer());
                auto status = connection.read_buffer(buffer);
                // TODO: Errors here should be unrecoverable (HRT-14275)
                CHECK_SUCCESS(status);
            }
        }
        m_callbacks_queue.push(callback_handle_id);
    }

    m_cv.notify_one();
    return HAILO_SUCCESS;
}


} // namespace hailort
