/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduled_stream.hpp
 * @brief Internal stream implementation for scheduled streams
 *
 **/

#ifndef HAILO_SCHEDULED_STREAM_HPP_
#define HAILO_SCHEDULED_STREAM_HPP_

#include "stream_internal.hpp"
#include "hailo/hailort.h"
#include "vdevice_internal.hpp"
#include "vdma_device.hpp"
#include "vdevice_stream.hpp"
#include "hailo/expected.hpp"

namespace hailort
{

class ScheduledInputStream : public InputVDeviceBaseStream {
public:
    ScheduledInputStream(ScheduledInputStream &&other) :
        InputVDeviceBaseStream(std::move(other)),
        m_network_group_handle(std::move(other.m_network_group_handle)),
        m_network_group_scheduler(std::move(other.m_network_group_scheduler))
    {}

    explicit ScheduledInputStream(
        std::vector<std::reference_wrapper<VdmaInputStream>> &&streams,
        const scheduler_ng_handle_t &network_group_handle,
        EventPtr &&network_group_activated_event,
        const LayerInfo &layer_info,
        NetworkGroupSchedulerWeakPtr network_group_scheduler,
        hailo_status &status) :
            InputVDeviceBaseStream(std::move(streams), std::move(network_group_activated_event), layer_info, status),
            m_network_group_handle(network_group_handle),
            m_network_group_scheduler(network_group_scheduler)
    {}

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override { return true; };

    virtual void notify_all() override
    {
        auto scheduler = m_network_group_scheduler.lock();
        if (nullptr == scheduler) {
            LOGGER__CRITICAL("Failed to acquire scheduler");
            return;
        }
        scheduler->notify_all();

        for (auto &stream : m_streams) {
            stream.get().notify_all();
        }
    }

protected:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer,
        const std::function<bool()> &should_cancel = []() { return false; });

    Expected<size_t> sync_write_raw_buffer_impl(const MemoryView &buffer, scheduler_ng_handle_t network_group_handle,
        const std::function<bool()> &should_cancel);

    scheduler_ng_handle_t m_network_group_handle;
    NetworkGroupSchedulerWeakPtr m_network_group_scheduler;

private:
    hailo_status abort_impl(scheduler_ng_handle_t network_group_handle);
    hailo_status clear_abort_impl(scheduler_ng_handle_t network_group_handle);
};

class ScheduledOutputStream : public OutputVDeviceBaseStream {
public:
    ScheduledOutputStream(ScheduledOutputStream &&other) :
        OutputVDeviceBaseStream(std::move(other)),
        m_network_group_handle(std::move(other.m_network_group_handle)),
        m_network_group_scheduler(std::move(other.m_network_group_scheduler))
    {}

    explicit ScheduledOutputStream(
        std::vector<std::reference_wrapper<VdmaOutputStream>> &&streams,
        const scheduler_ng_handle_t &network_group_handle,
        const LayerInfo &layer_info,
        EventPtr &&network_group_activated_event,
        NetworkGroupSchedulerWeakPtr network_group_scheduler,
        hailo_status &status) :
            OutputVDeviceBaseStream(std::move(streams), layer_info, std::move(network_group_activated_event), status),
            m_network_group_handle(network_group_handle),
            m_network_group_scheduler(network_group_scheduler)
    {}

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override { return true; };

protected:
    virtual hailo_status read(MemoryView buffer) override;
    hailo_status read_impl(MemoryView buffer, scheduler_ng_handle_t network_group_handle);

    scheduler_ng_handle_t m_network_group_handle;
    NetworkGroupSchedulerWeakPtr m_network_group_scheduler;

private:
    hailo_status abort_impl(scheduler_ng_handle_t network_group_handle);
    hailo_status clear_abort_impl(scheduler_ng_handle_t network_group_handle);
};

} /* namespace hailort */

#endif /* HAILO_SCHEDULED_STREAM_HPP_ */
