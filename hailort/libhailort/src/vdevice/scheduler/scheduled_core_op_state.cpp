/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduled_core_op_state.cpp
 * @brief: Scheduled CoreOp
 **/

#include "vdevice/scheduler/scheduler_oracle.hpp"
#include "vdevice/scheduler/scheduled_core_op_state.hpp"
#include "vdevice/vdevice_core_op.hpp"

namespace hailort
{

ScheduledCoreOp::ScheduledCoreOp(std::shared_ptr<VDeviceCoreOp> core_op, std::chrono::milliseconds timeout,
    uint16_t max_batch_size,  uint32_t max_ongoing_frames_per_device, bool use_dynamic_batch_flow) :
    m_core_op(core_op),
    m_last_run_time_stamp(std::chrono::steady_clock::now()),
    m_timeout(std::move(timeout)),
    m_max_batch_size(max_batch_size),
    m_max_ongoing_frames_per_device(max_ongoing_frames_per_device),
    m_use_dynamic_batch_flow(use_dynamic_batch_flow),
    m_instances_count(1),
    m_pending_requests(0),
    m_ready_requests(0),
    m_min_threshold(DEFAULT_SCHEDULER_MIN_THRESHOLD),
    m_priority(HAILO_SCHEDULER_PRIORITY_NORMAL),
    m_last_device_id(INVALID_DEVICE_ID)
{}

Expected<std::shared_ptr<ScheduledCoreOp>> ScheduledCoreOp::create(std::shared_ptr<VDeviceCoreOp> added_core_op,
    StreamInfoVector &stream_infos)
{
    auto timeout = DEFAULT_SCHEDULER_TIMEOUT;

    auto batch_size_expected = added_core_op->get_stream_batch_size(stream_infos[0].name);
    CHECK_EXPECTED(batch_size_expected);
    const auto max_batch_size = batch_size_expected.release();

    TRY(auto max_queue_size_per_device, added_core_op->get_infer_queue_size_per_device());

    // DEFAULT_BATCH_SIZE and SINGLE_CONTEXT_BATCH_SIZE support streaming and therfore we are not using dynamic batch flow
    auto use_dynamic_batch_flow = added_core_op->get_supported_features().multi_context && (max_batch_size > SINGLE_CONTEXT_BATCH_SIZE);
    auto res = make_shared_nothrow<ScheduledCoreOp>(added_core_op, timeout, max_batch_size,
        static_cast<uint32_t>(max_queue_size_per_device), use_dynamic_batch_flow);
    CHECK_NOT_NULL_AS_EXPECTED(res, HAILO_OUT_OF_HOST_MEMORY);

    return res;
}

uint32_t ScheduledCoreOp::get_max_ongoing_frames_per_device() const
{
    return m_max_ongoing_frames_per_device;
}

bool ScheduledCoreOp::use_dynamic_batch_flow() const
{
    return m_use_dynamic_batch_flow;
}

hailo_status ScheduledCoreOp::set_timeout(const std::chrono::milliseconds &timeout)
{
    m_timeout = timeout;
    LOGGER__INFO("Setting scheduler threshold timeout of {} to {}ms", m_core_op->name(), timeout.count());
    return HAILO_SUCCESS;
}

hailo_status ScheduledCoreOp::set_threshold(uint32_t threshold)
{
    CHECK(!use_dynamic_batch_flow() ||
        (threshold <= m_max_batch_size), HAILO_INVALID_ARGUMENT, "Threshold must be equal or lower than the maximum batch size!");

    m_min_threshold = threshold;
    LOGGER__INFO("Setting scheduler threshold of {} to {} frames", m_core_op->name(), threshold);
    return HAILO_SUCCESS;
}

core_op_priority_t ScheduledCoreOp::get_priority()
{
    return m_priority;
}

void ScheduledCoreOp::set_priority(core_op_priority_t priority)
{
    LOGGER__INFO("Setting scheduler priority of {} to {}", m_core_op->name(), static_cast<uint8_t>(priority));
    m_priority = priority;
}

bool ScheduledCoreOp::is_over_threshold(uint32_t num_of_frames) const
{
    return num_of_frames >= m_min_threshold;
}

bool ScheduledCoreOp::is_over_threshold_timeout() const
{
    if (HAILO_INFINITE_TIMEOUT == m_timeout) {
        return false;
    }
    return m_timeout <= (std::chrono::steady_clock::now() - m_last_run_time_stamp);
}

bool ScheduledCoreOp::is_first_frame() const
{
    return (m_pending_requests.load() == 0);
}

device_id_t ScheduledCoreOp::get_last_device()
{
    return m_last_device_id;
}

void ScheduledCoreOp::set_last_device(const device_id_t &device_id)
{
    m_last_device_id = device_id;
}

std::shared_ptr<CoreOp> ScheduledCoreOp::get_core_op()
{
    return m_core_op;
}

Expected<std::shared_ptr<VdmaConfigCoreOp>> ScheduledCoreOp::get_vdma_core_op(const device_id_t &device_id)
{
    return m_core_op->get_core_op_by_device_id(device_id);
}

std::chrono::time_point<std::chrono::steady_clock> ScheduledCoreOp::get_last_run_timestamp()
{
    return m_last_run_time_stamp;
}

void ScheduledCoreOp::set_last_run_timestamp(const std::chrono::time_point<std::chrono::steady_clock> &timestamp)
{
    m_last_run_time_stamp = timestamp;
}

std::chrono::milliseconds ScheduledCoreOp::get_timeout()
{
    return m_timeout;
}

uint32_t ScheduledCoreOp::get_threshold()
{
    return (m_min_threshold != DEFAULT_SCHEDULER_MIN_THRESHOLD) ? m_min_threshold : 1;
}

uint16_t ScheduledCoreOp::get_max_batch_size() const
{
    return m_max_batch_size;
}

uint16_t ScheduledCoreOp::get_burst_size() const
{
    // When the user don't explicitly pass batch size, in order to preserve performance from previous scheduler version,
    // we don't want to stop streaming until we transferred at least m_max_ongoing_frames_per_device frames (This was
    // the behaviour in previous scheduler versions).
    return m_core_op->is_default_batch_size() ?
        static_cast<uint16_t>(m_max_ongoing_frames_per_device) :
        get_max_batch_size();
}

void ScheduledCoreOp::add_instance()
{
    m_instances_count++;
}

void ScheduledCoreOp::remove_instance()
{
    assert(m_instances_count > 0);
    m_instances_count--;
}

size_t ScheduledCoreOp::instances_count() const
{
    return m_instances_count;
}


} /* namespace hailort */
