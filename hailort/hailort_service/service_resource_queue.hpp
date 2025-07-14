/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file service_resource_queue.hpp
 * @brief Queue used for the callbacks in infer async over service.
 * enqueue callback id means the transfer is done.
 * dequeue a callback id means the client is signaled to call the callback on his side.
 **/

#ifndef _HAILO_SERVICE_RESOURCE_QUEUE_HPP_
#define _HAILO_SERVICE_RESOURCE_QUEUE_HPP_

#include "hailo/hailort_common.hpp"
#include "common/thread_safe_queue.hpp"
#include "common/file_descriptor.hpp"

namespace hailort
{

#define MAX_QUEUE_SIZE (512) // Max inner reader-writer queue size

template<typename T>
class ServiceResourceQueue final
{
public:
    ~ServiceResourceQueue()
    {
        shutdown();
    };

    static Expected<std::unique_ptr<ServiceResourceQueue>> create(uint32_t max_queue_size)
    {
        TRY(auto shutdown_event, Event::create_shared(Event::State::not_signalled));

        TRY(auto queue,
            SpscQueue<T>::create(max_queue_size, shutdown_event, HAILO_INFINITE_TIMEOUT));

        auto queue_ptr = make_unique_nothrow<ServiceResourceQueue>(std::move(queue), shutdown_event);
        CHECK_AS_EXPECTED(nullptr != queue_ptr, HAILO_OUT_OF_HOST_MEMORY);

        return queue_ptr;
    }

    ServiceResourceQueue(SpscQueue<T> &&queue, EventPtr shutdown_event) :
        m_queue(std::move(queue)), m_shutdown_event(shutdown_event)
    {}

    hailo_status enqueue(T &&value)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        auto status = m_queue.enqueue(std::move(value));
        if (HAILO_SHUTDOWN_EVENT_SIGNALED == status) {
            return status;
        }
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    Expected<T> dequeue()
    {
        TRY_WITH_ACCEPTABLE_STATUS(HAILO_SHUTDOWN_EVENT_SIGNALED, auto value,
            m_queue.dequeue());
        return value;
    }

    hailo_status shutdown()
    {
        return m_shutdown_event->signal();
    }

private:
    std::mutex m_mutex;
    // TODO: HRT-12346 - Use folly's MPMC? (for multiple devices)
    SpscQueue<T> m_queue;
    EventPtr m_shutdown_event;
};

using VDeviceCallbacksQueue = ServiceResourceQueue<ProtoCallbackIdentifier>;
using DmaBufferQueue = ServiceResourceQueue<std::shared_ptr<FileDescriptor>>;

} /* namespace hailort */

#endif /* _HAILO_SERVICE_RESOURCE_QUEUE_HPP_ */
