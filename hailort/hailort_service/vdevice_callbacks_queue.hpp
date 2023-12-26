/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file vdevice_callbacks_queue.hpp
 * @brief Queue used for the callbacks in infer async over service.
 * enqueue callback id means the transfer is done.
 * dequeue a callback id means the client is signaled to call the callback on his side.
 **/

#ifndef _HAILO_VDEVICE_CALLBACKS_QUEUE_HPP_
#define _HAILO_VDEVICE_CALLBACKS_QUEUE_HPP_

#include "hailort_rpc_service.hpp"

#include "hailo/hailort.h"
#include "hailo/network_group.hpp"
#include "hailo/hailort_common.hpp"
#include "utils/thread_safe_queue.hpp"

namespace hailort
{

#define MAX_QUEUE_SIZE (512) // Max inner reader-writer queue size

class VDeviceCallbacksQueue final
{
public:
    static Expected<std::unique_ptr<VDeviceCallbacksQueue>> create(uint32_t max_queue_size)
    {
        auto shutdown_event_exp = Event::create_shared(Event::State::not_signalled);
        CHECK_EXPECTED(shutdown_event_exp);
        auto shutdown_event = shutdown_event_exp.release();

        auto cb_ids_queue = SpscQueue<ProtoCallbackIdentifier>::create(max_queue_size, shutdown_event, HAILO_INFINITE_TIMEOUT);
        CHECK_EXPECTED(cb_ids_queue);

        auto queue_ptr = make_unique_nothrow<VDeviceCallbacksQueue>(cb_ids_queue.release(), shutdown_event);
        CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY);

        return queue_ptr;
    }

    VDeviceCallbacksQueue(SpscQueue<ProtoCallbackIdentifier> &&cb_ids_queue, EventPtr shutdown_event) :
        m_callbacks_ids_queue(std::move(cb_ids_queue)), m_shutdown_event(shutdown_event)
    {}

    hailo_status enqueue(ProtoCallbackIdentifier &&callback_id)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto status = m_callbacks_ids_queue.enqueue(std::move(callback_id));
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    Expected<ProtoCallbackIdentifier> dequeue()
    {
        auto callback_id = m_callbacks_ids_queue.dequeue();
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == callback_id.status()) {
            return make_unexpected(callback_id.status());
        }
        else if (HAILO_TIMEOUT == callback_id.status()) {
            LOGGER__WARNING("Failed to dequeue callback_id because the queue is empty, status={}", HAILO_TIMEOUT);
            return make_unexpected(callback_id.status());
        }
        CHECK_EXPECTED(callback_id);

        return callback_id;
    }

    hailo_status shutdown()
    {
        return m_shutdown_event->signal();
    }

private:
    std::mutex m_mutex;
    uint32_t m_vdevice_handle;
    // TODO: HRT-12346 - Use folly's MPMC? (for multiple devices)
    SpscQueue<ProtoCallbackIdentifier> m_callbacks_ids_queue;
    EventPtr m_shutdown_event;
};

} /* namespace hailort */

#endif /* _HAILO_VDEVICE_CALLBACKS_QUEUE_HPP_ */
