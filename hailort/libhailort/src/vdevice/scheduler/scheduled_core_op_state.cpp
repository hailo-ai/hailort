/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduled_core_op_state.cpp
 * @brief: Scheduled CoreOp
 **/

#include "vdevice/vdevice_stream_multiplexer_wrapper.hpp"
#include "vdevice/scheduler/scheduler_oracle.hpp"
#include "vdevice/scheduler/scheduled_core_op_state.hpp"
#include "hef/hef_internal.hpp"


namespace hailort
{

#define SINGLE_CONTEXT_BATCH_SIZE (1)

ScheduledCoreOp::ScheduledCoreOp(std::shared_ptr<CoreOp> core_op, std::chrono::milliseconds timeout,
    uint16_t max_batch_size, StreamInfoVector &stream_infos, std::string core_op_name) :
    m_core_op(core_op),
    m_last_run_time_stamp(std::chrono::steady_clock::now()),
    m_timeout(std::move(timeout)),
    m_frame_was_sent(false),
    m_max_batch_size(max_batch_size),
    m_priority(HAILO_SCHEDULER_PRIORITY_NORMAL),
    m_last_device_index(INVALID_DEVICE_ID),
    m_core_op_name(core_op_name),
    m_inputs_names(),
    m_outputs_names(),
    m_is_nms(false),
    m_ready_to_switch(false)
{
    // Prepare empty counters for the added core-op
    for (const auto &stream_info : stream_infos) {
        m_min_threshold_per_stream[stream_info.name] = DEFAULT_SCHEDULER_MIN_THRESHOLD;
        if (HAILO_H2D_STREAM == stream_info.direction) {
            m_requested_write_frames.insert(stream_info.name);
            m_finished_write_frames.insert(stream_info.name);
            m_h2d_finished_transferred_frames.insert(stream_info.name);
            m_inputs_names.push_back(stream_info.name);
        } else {
            m_requested_read_frames.insert(stream_info.name);
            m_finished_read_frames.insert(stream_info.name);
            m_d2h_finished_transferred_frames.insert(stream_info.name);
            m_outputs_names.push_back(stream_info.name);
            m_output_streams_read_orders[stream_info.name] = std::queue<uint32_t>();
            if (HAILO_FORMAT_ORDER_HAILO_NMS == stream_info.format.order) {
                m_is_nms = true;
            }
        }
    }
}

Expected<std::shared_ptr<ScheduledCoreOp>> ScheduledCoreOp::create(std::shared_ptr<CoreOp> added_core_op, StreamInfoVector &stream_infos)
{
    auto timeout = DEFAULT_SCHEDULER_TIMEOUT;

    uint16_t max_batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE;
    if (added_core_op->get_supported_features().multi_context) {
        auto batch_size = added_core_op->get_stream_batch_size(stream_infos[0].name);
        CHECK_EXPECTED(batch_size);
        if (batch_size.value() > SINGLE_CONTEXT_BATCH_SIZE) {
            max_batch_size = batch_size.release();
        }
    }

    return make_shared_nothrow<ScheduledCoreOp>(added_core_op, timeout, max_batch_size, stream_infos, added_core_op->name());
}

bool ScheduledCoreOp::has_enough_space_in_read_buffers(uint32_t ongoing_frames)
{
    auto output_streams = m_core_op->get_output_streams();
    for (auto &output_stream : output_streams) {
        OutputStreamBase &vdevice_output = static_cast<OutputStreamBase&>(output_stream.get());
        if (auto pending_frames_size = vdevice_output.get_buffer_frames_size()) {
            if (pending_frames_size.value() <= ongoing_frames) {
                return false;
            }
            // If couldnt get pending frames size and count (e.g. NMS layer), assume we have space - scheduler switch will prevent deadlocks here
        }
    }
    return true;
}

uint16_t ScheduledCoreOp::get_min_input_buffers_count(uint32_t device_count)
{
    auto input_streams = m_core_op->get_input_streams();
    uint16_t buffers_count = UINT16_MAX;
    for (auto &input_stream : input_streams) {
        InputStreamBase &vdevice_input = static_cast<InputStreamBase&>(input_stream.get());
        if (auto pending_frames_size = vdevice_input.get_buffer_frames_size()) {
            buffers_count = std::min(buffers_count, static_cast<uint16_t>(pending_frames_size.value() / device_count));
        }
    }
    return buffers_count;
}

bool ScheduledCoreOp::has_input_written_most_frames(const std::string &stream_name)
{
    auto total_writes = total_written_frames_count();
    return total_writes[stream_name] == get_max_value_of_unordered_map(total_writes);
}

// TODO: Use get_pre_transfer_h2d_frames_count + get_h2d_transferred_frames_count
// TODO: Avoid returning map (malloc)
std::unordered_map<stream_name_t, uint32_t> ScheduledCoreOp::total_written_frames_count()
{
    std::unordered_map<stream_name_t, uint32_t> write_sum;
    for (const auto &name : get_inputs_names()) {
        write_sum[name] = m_requested_write_frames[name] + m_finished_write_frames[name]
            + m_h2d_finished_transferred_frames[name];
    }
    return write_sum;
}

// TODO: Use max(m_d2h_finished_transferred_frames) == 0 instead
bool ScheduledCoreOp::has_pending_frames()
{
    auto h2d_transferred_frames_count = m_h2d_finished_transferred_frames.get_max_value();
    for (const auto &name : get_outputs_names()) {
        if (m_finished_read_frames[name] < h2d_transferred_frames_count) {
            return true;
        }
    }
    return false;
}

bool ScheduledCoreOp::can_stream_read(const std::string &stream_name)
{
    return !m_output_streams_read_orders[stream_name].empty();
}

bool ScheduledCoreOp::can_stream_write(const std::string &stream_name)
{
    auto total_written_frames = total_written_frames_count()[stream_name];
    auto min_finished_read = finished_read_frames_min_value();
    auto ongoing_frames = (min_finished_read < total_written_frames) ? (total_written_frames - min_finished_read) : 0;
    return has_enough_space_in_read_buffers(ongoing_frames);
}


bool ScheduledCoreOp::use_dynamic_batch_flow()
{
    return (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE != m_max_batch_size);
}

bool ScheduledCoreOp::has_core_op_drained_everything()
{
    uint32_t written_frames = m_h2d_finished_transferred_frames.get_max_value();
    for (const auto &name : get_outputs_names()) {
        if ((m_finished_read_frames[name] + m_d2h_finished_transferred_frames[name]) < written_frames) {
            return false;
        }
    }
    return true;
}

void ScheduledCoreOp::decrease_current_core_op_counters()
{
    // Decrease only if counter is 2 or bigger because reaching 0 can cause states to change
    if (!m_h2d_finished_transferred_frames.all_values_bigger_or_equal(2)) {
            return;
    }
    if (!m_finished_read_frames.all_values_bigger_or_equal(2)) {
            return;
    }

    for (const auto &name : get_inputs_names()) {
        m_h2d_finished_transferred_frames[name]--;
    }
    for (const auto &name : get_outputs_names()) {
        m_finished_read_frames[name]--;
    }
}

uint32_t ScheduledCoreOp::get_pre_transfer_h2d_frames_count()
{
    std::unordered_map<stream_name_t, uint32_t> write_sum;
    for (const auto &name : get_inputs_names()) {
        write_sum[name] = m_requested_write_frames[name] + m_finished_write_frames[name];
    }
    return get_max_value_of_unordered_map(write_sum);
}

hailo_status ScheduledCoreOp::set_timeout(const std::chrono::milliseconds &timeout, const stream_name_t &stream_name)
{
    CHECK(!m_frame_was_sent, HAILO_INVALID_OPERATION,
        "Setting scheduler timeout is allowed only before sending / receiving frames on the core-op.");
    m_timeout = timeout;

    auto name = (stream_name.empty()) ? get_core_op_name() : stream_name;
    LOGGER__INFO("Setting scheduler timeout of {} to {}ms", name, timeout.count());

    return HAILO_SUCCESS;
}

hailo_status ScheduledCoreOp::set_threshold(uint32_t threshold, const stream_name_t &stream_name)
{
    CHECK((CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == m_max_batch_size) ||
        (threshold <= m_max_batch_size), HAILO_INVALID_ARGUMENT, "Threshold must be equal or lower than the maximum batch size!");

    CHECK(!m_frame_was_sent, HAILO_INVALID_OPERATION,
        "Setting scheduler threshold is allowed only before sending / receiving frames on the core-op.");

    // TODO: Support setting threshold per stream. currently stream_name is always empty and de-facto we set threshold for the whole NG
    for (auto &threshold_per_stream_pair : m_min_threshold_per_stream) {
        threshold_per_stream_pair.second = threshold;
    }

    auto name = (stream_name.empty()) ? get_core_op_name() : stream_name;
    LOGGER__INFO("Setting scheduler threshold of {} to {} frames", name, threshold);

    return HAILO_SUCCESS;
}

core_op_priority_t ScheduledCoreOp::get_priority()
{
    return m_priority;
}

void ScheduledCoreOp::set_priority(core_op_priority_t priority)
{
    m_priority = priority;
}

uint32_t ScheduledCoreOp::get_last_device_index()
{
    return m_last_device_index;
}

void ScheduledCoreOp::set_last_device_index(uint32_t device_index)
{
    m_last_device_index = device_index;
}

std::string ScheduledCoreOp::get_core_op_name()
{
    return m_core_op_name;
}

std::shared_ptr<CoreOp> ScheduledCoreOp::get_core_op()
{
    return m_core_op;
}

void ScheduledCoreOp::mark_frame_sent()
{
    m_frame_was_sent = true;
}

std::chrono::time_point<std::chrono::steady_clock> ScheduledCoreOp::get_last_run_timestamp()
{
    return m_last_run_time_stamp;
}

void ScheduledCoreOp::set_last_run_timestamp(const std::chrono::time_point<std::chrono::steady_clock> &timestamp)
{
    m_last_run_time_stamp = timestamp;
}

Expected<std::chrono::milliseconds> ScheduledCoreOp::get_timeout(const stream_name_t &stream_name)
{
    CHECK_AS_EXPECTED(stream_name.empty(), HAILO_INVALID_OPERATION, "timeout per network is not supported");
    auto timeout = m_timeout;
    return timeout;
}

Expected<uint32_t> ScheduledCoreOp::get_threshold(const stream_name_t &stream_name)
{
    CHECK_AS_EXPECTED(contains(m_min_threshold_per_stream, stream_name), HAILO_NOT_FOUND);
    return m_min_threshold_per_stream[stream_name].load();
}

uint16_t ScheduledCoreOp::get_max_batch_size()
{
    if (!use_dynamic_batch_flow()) {
        return SINGLE_CONTEXT_BATCH_SIZE;
    }
    return m_max_batch_size;
}

Counter &ScheduledCoreOp::requested_write_frames()
{
    return m_requested_write_frames;
}

std::atomic_uint32_t &ScheduledCoreOp::requested_write_frames(const stream_name_t &stream_name)
{
    return m_requested_write_frames[stream_name];
}

Counter &ScheduledCoreOp::finished_write_frames()
{
    return m_finished_write_frames;
}

std::atomic_uint32_t &ScheduledCoreOp::finished_write_frames(const stream_name_t &stream_name)
{
    return m_finished_write_frames[stream_name];
}

uint32_t ScheduledCoreOp::finished_write_frames_min_value()
{
    return m_finished_write_frames.get_min_value();
}

Counter &ScheduledCoreOp::h2d_finished_transferred_frames()
{
    return m_h2d_finished_transferred_frames;
}

std::atomic_uint32_t &ScheduledCoreOp::h2d_finished_transferred_frames(const stream_name_t &stream_name)
{
    return m_h2d_finished_transferred_frames[stream_name];
}

Counter &ScheduledCoreOp::requested_read_frames()
{
    return m_requested_read_frames;
}

std::atomic_uint32_t &ScheduledCoreOp::requested_read_frames(const stream_name_t &stream_name)
{
    return m_requested_read_frames[stream_name];
}

Counter &ScheduledCoreOp::d2h_finished_transferred_frames()
{
    return m_d2h_finished_transferred_frames;
}

std::atomic_uint32_t &ScheduledCoreOp::d2h_finished_transferred_frames(const stream_name_t &stream_name)
{
    return m_d2h_finished_transferred_frames[stream_name];
}

Counter &ScheduledCoreOp::finished_read_frames()
{
    return m_finished_read_frames;
}

std::atomic_uint32_t &ScheduledCoreOp::finished_read_frames(const stream_name_t &stream_name)
{
    return m_finished_read_frames[stream_name];
}

uint32_t ScheduledCoreOp::finished_read_frames_min_value()
{
    return m_finished_read_frames.get_min_value();
}

const std::vector<stream_name_t> &ScheduledCoreOp::get_inputs_names()
{
    return m_inputs_names;
}

const std::vector<stream_name_t> &ScheduledCoreOp::get_outputs_names()
{
    return m_outputs_names;
}

void ScheduledCoreOp::push_device_index(uint32_t device_index)
{
    for (auto& stream_name : get_outputs_names()) {
        m_output_streams_read_orders[stream_name].push(device_index);
    }
}

uint32_t ScheduledCoreOp::pop_device_index(const stream_name_t &stream_name)
{
    assert(contains(m_output_streams_read_orders, stream_name));
    assert(!m_output_streams_read_orders[stream_name].empty());
    auto device_index = m_output_streams_read_orders[stream_name].front();
    m_output_streams_read_orders[stream_name].pop();

    return device_index;
}

bool ScheduledCoreOp::is_ready_to_switch()
{
    return m_ready_to_switch;
}

void ScheduledCoreOp::mark_ready_to_switch()
{
    m_ready_to_switch = true;
}

void ScheduledCoreOp::mark_unready_to_switch()
{
    m_ready_to_switch = false;
}

} /* namespace hailort */
