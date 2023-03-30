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

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "stream_common/stream_internal.hpp"
#include "vdevice/vdevice_internal.hpp"
#include "vdevice/vdevice_stream.hpp"
#include "vdma/vdma_device.hpp"


namespace hailort
{

class ScheduledInputStream : public InputVDeviceBaseStream {
public:
    ScheduledInputStream(
        std::vector<std::reference_wrapper<VdmaInputStream>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        EventPtr &&core_op_activated_event,
        const LayerInfo &layer_info,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        hailo_status &status) :
            InputVDeviceBaseStream(std::move(streams), std::move(core_op_activated_event), layer_info, status),
            m_core_op_handle(core_op_handle),
            m_core_ops_scheduler(core_ops_scheduler)
    {}

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override { return true; };

    virtual void notify_all() override
    {
        auto scheduler = m_core_ops_scheduler.lock();
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

    Expected<size_t> sync_write_raw_buffer_impl(const MemoryView &buffer, scheduler_core_op_handle_t core_op_handle,
        const std::function<bool()> &should_cancel);

    scheduler_core_op_handle_t m_core_op_handle;
    CoreOpsSchedulerWeakPtr m_core_ops_scheduler;

private:
    hailo_status abort_impl(scheduler_core_op_handle_t core_op_handle);
    hailo_status clear_abort_impl(scheduler_core_op_handle_t core_op_handle);
};

class ScheduledOutputStream : public OutputVDeviceBaseStream {
public:
    ScheduledOutputStream(
        std::vector<std::reference_wrapper<VdmaOutputStream>> &&streams,
        const scheduler_core_op_handle_t &core_op_handle,
        const LayerInfo &layer_info,
        EventPtr &&core_op_activated_event,
        CoreOpsSchedulerWeakPtr core_ops_scheduler,
        hailo_status &status) :
            OutputVDeviceBaseStream(std::move(streams), layer_info, std::move(core_op_activated_event), status),
            m_core_op_handle(core_op_handle),
            m_core_ops_scheduler(core_ops_scheduler)
    {}

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override { return true; };

protected:
    virtual hailo_status read(MemoryView buffer) override;
    hailo_status read_impl(MemoryView buffer, scheduler_core_op_handle_t core_op_handle);

    scheduler_core_op_handle_t m_core_op_handle;
    CoreOpsSchedulerWeakPtr m_core_ops_scheduler;

private:
    hailo_status abort_impl(scheduler_core_op_handle_t core_op_handle);
    hailo_status clear_abort_impl(scheduler_core_op_handle_t core_op_handle);
};

} /* namespace hailort */

#endif /* HAILO_SCHEDULED_STREAM_HPP_ */
