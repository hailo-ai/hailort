/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler.cpp
 * @brief: Network scheduler
 **/

#include "common/os_utils.hpp"


#include "vdevice/scheduler/scheduler.hpp"
#include "vdevice/vdevice_core_op.hpp"
#include "vdevice/scheduler/scheduler_oracle.hpp"
#include "vdevice/vdevice_stream_multiplexer_wrapper.hpp"
#include "hef/hef_internal.hpp"

#include <fstream>


namespace hailort
{

#define DEFAULT_BURST_SIZE (1)

CoreOpsScheduler::CoreOpsScheduler(hailo_scheduling_algorithm_t algorithm, std::vector<std::string> &devices_ids,
    std::vector<std::string> &devices_arch) :
    SchedulerBase(algorithm, devices_ids, devices_arch),
    m_scheduler_thread(*this)
{}

CoreOpsScheduler::~CoreOpsScheduler()
{
    shutdown();
}

Expected<CoreOpsSchedulerPtr> CoreOpsScheduler::create_round_robin(std::vector<std::string> &devices_bdf_id, std::vector<std::string> &devices_arch)
{
    auto ptr = make_shared_nothrow<CoreOpsScheduler>(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN, devices_bdf_id, devices_arch);
    CHECK_AS_EXPECTED(nullptr != ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

hailo_status CoreOpsScheduler::add_core_op(scheduler_core_op_handle_t core_op_handle,
     std::shared_ptr<CoreOp> added_cng)
{
    std::unique_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);

    auto stream_infos = added_cng->get_all_stream_infos();
    CHECK_EXPECTED_AS_STATUS(stream_infos);

    auto scheduled_core_op = ScheduledCoreOp::create(added_cng, stream_infos.value());
    CHECK_EXPECTED_AS_STATUS(scheduled_core_op);

    m_scheduled_core_ops.emplace(core_op_handle, scheduled_core_op.release());

    for (const auto &pair : m_devices) {
        auto &device_info = pair.second;
        for (const auto &stream_info : stream_infos.value()) {
            device_info->ongoing_frames[core_op_handle].insert(stream_info.name);
        }
    }

    const core_op_priority_t normal_priority = HAILO_SCHEDULER_PRIORITY_NORMAL;
    m_core_op_priority[normal_priority].emplace_back(core_op_handle);
    if (!contains(m_next_core_op, normal_priority)) {
        m_next_core_op[normal_priority] = 0;
    }

    return HAILO_SUCCESS;
}

void CoreOpsScheduler::shutdown()
{
    // Locking shared_lock since we don't touch the internal scheduler structures.
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    m_scheduler_thread.stop();

    // After the scheduler thread have stopped, we can safely deactivate all core ops
    for (const auto &pair : m_devices) {
        auto &device_info = pair.second;
        if (INVALID_CORE_OP_HANDLE != device_info->current_core_op_handle) {
            auto current_core_op = m_scheduled_core_ops.at(device_info->current_core_op_handle)->get_core_op();
            auto current_core_op_bundle = std::dynamic_pointer_cast<VDeviceCoreOp>(current_core_op);
            assert(nullptr != current_core_op_bundle);
            auto vdma_core_op = current_core_op_bundle->get_core_op_by_device_id(device_info->device_id);
            if (!vdma_core_op) {
                LOGGER__ERROR("Error retrieving core-op in scheduler destructor");
            } else {
                if (HAILO_SUCCESS != VdmaConfigManager::deactivate_core_op(vdma_core_op.value())) {
                    LOGGER__ERROR("Error deactivating core-op when destroying scheduler");
                }
                device_info->current_core_op_handle = INVALID_CORE_OP_HANDLE;
            }
        }
    }
}

hailo_status CoreOpsScheduler::switch_core_op(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id)
{
    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);
    assert(contains(m_devices, device_id));
    assert(is_device_idle(device_id));
    auto curr_device_info = m_devices[device_id];
    curr_device_info->is_switching_core_op = false;

    const auto burst_size = scheduled_core_op->get_burst_size();

    auto frames_count = std::min(get_frames_ready_to_transfer(core_op_handle, device_id), burst_size);
    auto hw_batch_size = scheduled_core_op->use_dynamic_batch_flow() ? frames_count : SINGLE_CONTEXT_BATCH_SIZE;

    if (frames_count == 0) {
        // TODO HRT-11753: don't allow this flow
        return HAILO_SUCCESS;
    }

    curr_device_info->frames_left_before_stop_streaming = burst_size;

    bool has_same_hw_batch_size_as_previous = curr_device_info->current_batch_size == hw_batch_size;
    curr_device_info->current_batch_size = hw_batch_size;

    if ((core_op_handle != curr_device_info->current_core_op_handle) || (!has_same_hw_batch_size_as_previous)) {
        auto next_active_cng = scheduled_core_op->get_core_op();
        auto next_active_cng_wrapper = std::dynamic_pointer_cast<VDeviceCoreOp>(next_active_cng);
        assert(nullptr != next_active_cng_wrapper);
        auto next_active_cng_expected = next_active_cng_wrapper->get_core_op_by_device_id(curr_device_info->device_id);
        CHECK_EXPECTED_AS_STATUS(next_active_cng_expected);

        std::shared_ptr<VdmaConfigCoreOp> current_active_vdma_cng = nullptr;
        if (curr_device_info->current_core_op_handle != INVALID_CORE_OP_HANDLE) {
            auto current_active_cng = m_scheduled_core_ops.at(curr_device_info->current_core_op_handle)->get_core_op();
            auto current_active_cng_bundle = std::dynamic_pointer_cast<VDeviceCoreOp>(current_active_cng);
            assert(nullptr != current_active_cng_bundle);
            auto current_active_cng_expected = current_active_cng_bundle->get_core_op_by_device_id(curr_device_info->device_id);
            CHECK_EXPECTED_AS_STATUS(current_active_cng_expected);
            current_active_vdma_cng = current_active_cng_expected.release();
        }

        const bool is_batch_switch = (core_op_handle == curr_device_info->current_core_op_handle);
        auto status = VdmaConfigManager::switch_core_op(current_active_vdma_cng, next_active_cng_expected.value(), hw_batch_size,
            is_batch_switch);
        CHECK_SUCCESS(status, "Failed switching core-op");
    }

    scheduled_core_op->set_last_run_timestamp(std::chrono::steady_clock::now()); // Mark timestamp on activation
    curr_device_info->current_core_op_handle = core_op_handle;

    auto status = send_all_pending_buffers(core_op_handle, device_id, frames_count);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("send_all_pending_buffers has failed with status=HAILO_STREAM_ABORTED_BY_USER");
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::send_all_pending_buffers(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id, uint32_t burst_size)
{
    auto current_device_info = m_devices[device_id];
    if ((INVALID_CORE_OP_HANDLE == current_device_info->current_core_op_handle) || (current_device_info->current_core_op_handle != core_op_handle)) {
        return HAILO_SUCCESS;
    }

    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);

    for (size_t i = 0; i < burst_size; i++) {
        if (current_device_info->frames_left_before_stop_streaming > 0) {
            current_device_info->frames_left_before_stop_streaming--;
        }

        for (auto &input_stream : scheduled_core_op->get_core_op()->get_input_streams()) {
            const auto &stream_name = input_stream.get().name();
            scheduled_core_op->pending_frames().decrease(stream_name);
            current_device_info->ongoing_frames[core_op_handle].increase(stream_name);

            // After launching the transfer, signal_frame_transferred may be called (and ongoing frames will be
            // decreased).
            auto &input_stream_base = static_cast<InputStreamBase&>(input_stream.get());
            auto status = input_stream_base.launch_transfer(device_id);
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("launch_transfer has failed with status=HAILO_STREAM_ABORTED_BY_USER");
                return status;
            }
            CHECK_SUCCESS(status);
        }

        for (auto &output_stream : scheduled_core_op->get_core_op()->get_output_streams()) {
            const auto &stream_name = output_stream.get().name();
            scheduled_core_op->pending_frames().decrease(stream_name);
            current_device_info->ongoing_frames[core_op_handle].increase(stream_name);

            // After launching the transfer, signal_frame_transferred may be called (and ongoing frames will be
            // decreased).
            auto &output_stream_base = static_cast<OutputStreamBase&>(output_stream.get());
            auto status = output_stream_base.launch_transfer(device_id);
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("launch_transfer has failed with status=HAILO_STREAM_ABORTED_BY_USER");
                return status;
            }
            CHECK_SUCCESS(status);
        }

        scheduled_core_op->set_last_device(device_id);
    }

    return HAILO_SUCCESS;
}

CoreOpsScheduler::ReadyInfo CoreOpsScheduler::is_core_op_ready(const scheduler_core_op_handle_t &core_op_handle,
    bool check_threshold, const device_id_t &device_id)
{
    ReadyInfo result;
    result.is_ready = false;

    if (should_core_op_stop(core_op_handle)) {
        // Do not switch to an aborted core-op
        return result;
    }

    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);

    std::vector<bool> over_threshold;
    over_threshold.reserve(scheduled_core_op->get_inputs_names().size());
    std::vector<bool> over_timeout;
    over_timeout.reserve(scheduled_core_op->get_inputs_names().size());

    if (check_threshold) {
        for (const auto &name : scheduled_core_op->get_inputs_names()) {
            auto threshold_exp = scheduled_core_op->get_threshold(name);
            if (!threshold_exp) {
                LOGGER__ERROR("Failed to get threshold for stream {}", name);
                return result;
            }
            auto threshold = (DEFAULT_SCHEDULER_MIN_THRESHOLD == threshold_exp.value()) ? 1 : threshold_exp.value();
            auto timeout_exp = scheduled_core_op->get_timeout();
            if (!timeout_exp) {
                LOGGER__ERROR("Failed to get timeout for stream {}", name);
                return result;
            }
            auto timeout = timeout_exp.release();

            // Check if there arent enough write requests to reach threshold and timeout didnt passed
            const auto write_requests = scheduled_core_op->pending_frames()[name];
            auto stream_over_threshold = write_requests >= threshold;
            auto stream_over_timeout = timeout <= (std::chrono::steady_clock::now() - scheduled_core_op->get_last_run_timestamp());
            over_threshold.push_back(stream_over_threshold);
            over_timeout.push_back(stream_over_timeout);
            if (stream_over_threshold || stream_over_timeout) {
                continue;
            } else {
                result.is_ready = false;
                return result;
            }
        }
        result.over_threshold = std::all_of(over_threshold.begin(), over_threshold.end(), [](auto over) { return over; });
        result.over_timeout = std::all_of(over_timeout.begin(), over_timeout.end(), [](auto over) { return over; });
    }

    result.is_ready = (get_frames_ready_to_transfer(core_op_handle, device_id) > 0);

    return result;
}

hailo_status CoreOpsScheduler::signal_frame_pending(const scheduler_core_op_handle_t &core_op_handle,
    const std::string &stream_name, hailo_stream_direction_t direction)
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);

    if (should_core_op_stop(core_op_handle)) {
        return HAILO_STREAM_ABORTED_BY_USER;
    }

    if (HAILO_H2D_STREAM == direction) {
        TRACE(WriteFrameTrace, core_op_handle, stream_name);
        scheduled_core_op->mark_frame_sent();
    }

    scheduled_core_op->pending_frames().increase(stream_name);
    m_scheduler_thread.signal();

    return HAILO_SUCCESS;
}

void CoreOpsScheduler::signal_frame_transferred(const scheduler_core_op_handle_t &core_op_handle,
    const std::string &stream_name, const device_id_t &device_id, hailo_stream_direction_t stream_direction)
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);

    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);

    m_devices[device_id]->ongoing_frames[core_op_handle].decrease(stream_name);
    if (HAILO_D2H_STREAM == stream_direction) {
        TRACE(OutputVdmaEnqueueTrace, device_id, core_op_handle, stream_name);
    }

    m_scheduler_thread.signal();
}

bool CoreOpsScheduler::is_device_idle(const device_id_t &device_id)
{
    const auto &device_info = m_devices[device_id];
    auto core_op_handle = device_info->current_core_op_handle;
    if (INVALID_CORE_OP_HANDLE == core_op_handle) {
        // If no core-op is running, consider it as drained
        return true;
    }

    if (m_scheduled_core_ops.at(core_op_handle)->all_stream_disabled()) {
        // We treat core-op as drained only if all streams are aborted - to make sure there aren't any ongoing transfers
        return true;
    }

    return m_devices[device_id]->is_idle();
}

void CoreOpsScheduler::enable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name)
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    m_scheduled_core_ops.at(core_op_handle)->enable_stream(stream_name);
}

void CoreOpsScheduler::disable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name)
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    m_scheduled_core_ops.at(core_op_handle)->disable_stream(stream_name);
}

hailo_status CoreOpsScheduler::set_timeout(const scheduler_core_op_handle_t &core_op_handle, const std::chrono::milliseconds &timeout, const std::string &/*network_name*/)
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    // TODO: call in loop for set_timeout with the relevant stream-names (of the given network)
    auto status = m_scheduled_core_ops.at(core_op_handle)->set_timeout(timeout);
    if (HAILO_SUCCESS == status) {
        TRACE(SetCoreOpTimeoutTrace, core_op_handle, timeout);
    }
    return status;
}

hailo_status CoreOpsScheduler::set_threshold(const scheduler_core_op_handle_t &core_op_handle, uint32_t threshold, const std::string &/*network_name*/)
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    auto status = m_scheduled_core_ops.at(core_op_handle)->set_threshold(threshold);
    if (HAILO_SUCCESS == status) {
        TRACE(SetCoreOpThresholdTrace, core_op_handle, threshold);
    }
    return status;
}

hailo_status CoreOpsScheduler::set_priority(const scheduler_core_op_handle_t &core_op_handle, core_op_priority_t priority, const std::string &/*network_name*/)
{
    CHECK(priority <= HAILO_SCHEDULER_PRIORITY_MAX, HAILO_INVALID_ARGUMENT);

    // Remove core of from previous priority map
    std::unique_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    auto old_priority = m_scheduled_core_ops.at(core_op_handle)->get_priority();
    auto &priority_vector = m_core_op_priority[old_priority];
    auto it = std::find(priority_vector.begin(), priority_vector.end(), core_op_handle);
    CHECK(it != priority_vector.end(), HAILO_INTERNAL_FAILURE);
    priority_vector.erase(it);
    m_next_core_op[old_priority] = 0; // Avoiding overflow by reseting next core op.

    // Add it to the new priority map.
    m_scheduled_core_ops.at(core_op_handle)->set_priority(priority);
    m_core_op_priority[priority].push_back(core_op_handle);
    if (!contains(m_next_core_op, priority)) {
        m_next_core_op[priority] = 0;
    }

    TRACE(SetCoreOpPriorityTrace, core_op_handle, priority);
    return HAILO_SUCCESS;
}

bool CoreOpsScheduler::should_core_op_stop(const scheduler_core_op_handle_t &core_op_handle)
{
    return m_scheduled_core_ops.at(core_op_handle)->any_stream_disabled();
}

hailo_status CoreOpsScheduler::optimize_streaming_if_enabled(const scheduler_core_op_handle_t &core_op_handle)
{
    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);
    if (!scheduled_core_op->use_dynamic_batch_flow()) {
        auto next_pair = m_devices.upper_bound(scheduled_core_op->get_last_device()); // Get last device and go to the next device in the map
        if (m_devices.end() == next_pair){ // In case we reached to the end of the map - start from the beginning
            next_pair = m_devices.begin();
        }
        auto &device_info = next_pair->second;
        if (device_info->current_core_op_handle == core_op_handle && !device_info->is_switching_core_op &&
            !CoreOpsSchedulerOracle::should_stop_streaming(*this, scheduled_core_op->get_priority(), device_info->device_id) &&
            (get_frames_ready_to_transfer(core_op_handle, device_info->device_id) >= DEFAULT_BURST_SIZE)) {
            auto status = send_all_pending_buffers(core_op_handle, device_info->device_id, DEFAULT_BURST_SIZE);
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("send_all_pending_buffers has failed with status=HAILO_STREAM_ABORTED_BY_USER");
                return status;
            }
            CHECK_SUCCESS(status);
        }
    }
    return HAILO_SUCCESS;
}

uint16_t CoreOpsScheduler::get_frames_ready_to_transfer(scheduler_core_op_handle_t core_op_handle,
    const device_id_t &device_id) const
{
    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);
    auto device_info = m_devices.at(device_id);

    const auto max_ongoing_frames = scheduled_core_op->get_max_ongoing_frames_per_device();
    const auto ongoing_frames = device_info->ongoing_frames[core_op_handle].get_max_value();
    assert(ongoing_frames <= max_ongoing_frames);

    const auto pending_frames = scheduled_core_op->pending_frames().get_min_value();

    return static_cast<uint16_t>(std::min(pending_frames, max_ongoing_frames - ongoing_frames));
}

void CoreOpsScheduler::schedule()
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    m_scheduled_core_ops.for_each([this](const std::pair<vdevice_core_op_handle_t, ScheduledCoreOpPtr> &core_op_pair) {
        auto status = optimize_streaming_if_enabled(core_op_pair.first);
        if ((HAILO_SUCCESS != status) &&
            (HAILO_STREAM_ABORTED_BY_USER != status)) {
            LOGGER__ERROR("optimize_streaming_if_enabled thread failed with status={}", status);
        }

    });

    auto oracle_decisions = CoreOpsSchedulerOracle::get_oracle_decisions(*this);

    for (const auto &run_params : oracle_decisions) {
        auto status = switch_core_op(run_params.core_op_handle, run_params.device_id);
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            continue;
        }

        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Scheduler thread failed with status={}", status);
            break;
        }
    }
}

CoreOpsScheduler::SchedulerThread::SchedulerThread(CoreOpsScheduler &scheduler) :
    m_scheduler(scheduler),
    m_is_running(true),
    m_execute_worker_thread(false),
    m_thread([this]() { worker_thread_main(); })
{}

CoreOpsScheduler::SchedulerThread::~SchedulerThread()
{
    stop();
}

void CoreOpsScheduler::SchedulerThread::signal()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_execute_worker_thread = true;
    }
    m_cv.notify_one();
}

void CoreOpsScheduler::SchedulerThread::stop()
{
    if (m_thread.joinable()) {
        m_is_running = false;
        signal();
        m_thread.join();
    }
}

void CoreOpsScheduler::SchedulerThread::worker_thread_main()
{
    OsUtils::set_current_thread_name("SCHEDULER");

    while (m_is_running) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() {
                return m_execute_worker_thread.load();
            });
            m_execute_worker_thread = false;
        }

        if (!m_is_running) {
            break;
        }

        m_scheduler.schedule();
    }
}

} /* namespace hailort */