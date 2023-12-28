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
#include "vdma/vdma_config_manager.hpp"
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
     std::shared_ptr<VDeviceCoreOp> added_cng)
{
    std::unique_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);

    auto scheduled_core_op_it = m_scheduled_core_ops.find(core_op_handle);
    if (scheduled_core_op_it != m_scheduled_core_ops.end()) {
        scheduled_core_op_it->second->add_instance();
    } else {
        auto stream_infos = added_cng->get_all_stream_infos();
        CHECK_EXPECTED_AS_STATUS(stream_infos);

        auto scheduled_core_op = ScheduledCoreOp::create(added_cng, stream_infos.value());
        CHECK_EXPECTED_AS_STATUS(scheduled_core_op);

        m_scheduled_core_ops.emplace(core_op_handle, scheduled_core_op.release());

        // To allow multiple instances of the same phyiscal core op, we don't limit the queue here. Each core-op and
        // scheduled should limit themself. Since the ctor accept no argument, we init it using operator[].
        // TODO HRT-12136: limit the queue size (based on instances count)
        m_infer_requests[core_op_handle];

        const core_op_priority_t normal_priority = HAILO_SCHEDULER_PRIORITY_NORMAL;
        m_core_op_priority[normal_priority].add(core_op_handle);
    }

    return HAILO_SUCCESS;
}

void CoreOpsScheduler::remove_core_op(scheduler_core_op_handle_t core_op_handle)
{
    std::unique_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    m_scheduled_core_ops.at(core_op_handle)->remove_instance();
}

void CoreOpsScheduler::shutdown()
{
    // Locking shared_lock since we don't touch the internal scheduler structures.
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    m_scheduler_thread.stop();

    // After the scheduler thread have stopped, we can safely deactivate all core ops
    for (const auto &pair : m_devices) {
        auto status = deactivate_core_op(pair.first);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Error deactivating core-op when destroying scheduler {}", status);
        }
    }
}

hailo_status CoreOpsScheduler::switch_core_op(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id)
{
    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);
    assert(contains(m_devices, device_id));
    auto curr_device_info = m_devices[device_id];
    assert(curr_device_info->is_idle());
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
        auto next_core_op = get_vdma_core_op(core_op_handle, device_id);

        std::shared_ptr<VdmaConfigCoreOp> current_core_op = nullptr;
        if (curr_device_info->current_core_op_handle != INVALID_CORE_OP_HANDLE) {
            current_core_op = get_vdma_core_op(curr_device_info->current_core_op_handle, device_id);
        }

        const bool is_batch_switch = (core_op_handle == curr_device_info->current_core_op_handle);
        auto status = VdmaConfigManager::switch_core_op(current_core_op, next_core_op, hw_batch_size, is_batch_switch);
        CHECK_SUCCESS(status, "Failed switching core-op");
    }

    scheduled_core_op->set_last_run_timestamp(std::chrono::steady_clock::now()); // Mark timestamp on activation
    curr_device_info->current_core_op_handle = core_op_handle;

    auto status = send_all_pending_buffers(core_op_handle, device_id, frames_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::deactivate_core_op(const device_id_t &device_id)
{
    const auto core_op_handle = m_devices[device_id]->current_core_op_handle;
    if (INVALID_CORE_OP_HANDLE == core_op_handle) {
        return HAILO_SUCCESS;
    }

    auto vdma_core_op = get_vdma_core_op(core_op_handle, device_id);
    auto status = VdmaConfigManager::deactivate_core_op(vdma_core_op);
    CHECK_SUCCESS(status, "Scheduler failed deactivate core op on {}", device_id);

    m_devices[device_id]->current_core_op_handle = INVALID_CORE_OP_HANDLE;
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

        auto status = infer_async(core_op_handle, device_id);
        CHECK_SUCCESS(status);
    }

    scheduled_core_op->set_last_device(device_id);
    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::infer_async(const scheduler_core_op_handle_t &core_op_handle,
    const device_id_t &device_id)
{
    auto current_device_info = m_devices[device_id];
    assert(core_op_handle == current_device_info->current_core_op_handle);
    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);
    auto vdma_core_op = get_vdma_core_op(core_op_handle, device_id);

    auto infer_request = dequeue_infer_request(core_op_handle);
    CHECK_EXPECTED_AS_STATUS(infer_request);

    current_device_info->ongoing_infer_requests.fetch_add(1);

    auto original_callback = infer_request->callback;
    infer_request->callback = [current_device_info, this, original_callback](hailo_status status) {
        current_device_info->ongoing_infer_requests.fetch_sub(1);
        m_scheduler_thread.signal();
        original_callback(status);
    };
    auto status = vdma_core_op->infer_async(infer_request.release());
    if (HAILO_SUCCESS != status) {
        current_device_info->ongoing_infer_requests.fetch_sub(1);
        original_callback(status);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

CoreOpsScheduler::ReadyInfo CoreOpsScheduler::is_core_op_ready(const scheduler_core_op_handle_t &core_op_handle,
    bool check_threshold, const device_id_t &device_id)
{
    ReadyInfo result;
    result.is_ready = false;

    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);

    result.is_ready = (get_frames_ready_to_transfer(core_op_handle, device_id) > 0);

    if (check_threshold) {
        result.over_threshold = scheduled_core_op->is_over_threshold();
        result.over_timeout = scheduled_core_op->is_over_timeout();

        if (!result.over_threshold && !result.over_timeout){
            result.is_ready = false;
        }
    }

    return result;
}

hailo_status CoreOpsScheduler::enqueue_infer_request(const scheduler_core_op_handle_t &core_op_handle,
    InferRequest &&infer_request)
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);

    CHECK(m_scheduled_core_ops.at(core_op_handle)->instances_count() > 0, HAILO_INTERNAL_FAILURE,
        "Trying to enqueue infer request on a core-op with instances_count==0");

    auto status = m_infer_requests.at(core_op_handle).enqueue(std::move(infer_request));
    if (HAILO_SUCCESS == status) {
        m_scheduled_core_ops.at(core_op_handle)->requested_infer_requests().fetch_add(1);
        m_scheduler_thread.signal();
    }
    return status;
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

    std::unique_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);

    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);

    // Remove core op from previous priority map
    auto &priority_group = m_core_op_priority[scheduled_core_op->get_priority()];
    assert(priority_group.contains(core_op_handle));
    priority_group.erase(core_op_handle);

    // Add it to the new priority map.
    m_scheduled_core_ops.at(core_op_handle)->set_priority(priority);
    m_core_op_priority[priority].add(core_op_handle);


    TRACE(SetCoreOpPriorityTrace, core_op_handle, priority);
    return HAILO_SUCCESS;
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
            CHECK_SUCCESS(status);
        }
    }
    return HAILO_SUCCESS;
}

Expected<InferRequest> CoreOpsScheduler::dequeue_infer_request(scheduler_core_op_handle_t core_op_handle)
{
    auto infer_request = m_infer_requests.at(core_op_handle).dequeue();
    CHECK_EXPECTED(infer_request);

    m_scheduled_core_ops.at(core_op_handle)->requested_infer_requests().fetch_sub(1);
    return infer_request.release();
}

uint16_t CoreOpsScheduler::get_frames_ready_to_transfer(scheduler_core_op_handle_t core_op_handle,
    const device_id_t &device_id) const
{
    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);
    auto device_info = m_devices.at(device_id);

    if (scheduled_core_op->instances_count() == 0) {
        // We don't want to schedule/execute core ops with instances_count() == 0. There may still be
        // requested_infer_requests until shutdown_core_op is called.
        // TODO: HRT-12218 after dequeue all infer requests for the instance in remove_core_op, this flow can be
        // removed any simplified (since on this case requested_infer_requests == 0).
        return 0;
    }

    const auto max_ongoing_frames = scheduled_core_op->get_max_ongoing_frames_per_device();
    const uint32_t ongoing_frames = (device_info->current_core_op_handle == core_op_handle) ?
        device_info->ongoing_infer_requests.load() : 0;
    assert(ongoing_frames <= max_ongoing_frames);

    const uint32_t requested_frames = scheduled_core_op->requested_infer_requests();

    return static_cast<uint16_t>(std::min(requested_frames, max_ongoing_frames - ongoing_frames));
}

std::shared_ptr<VdmaConfigCoreOp> CoreOpsScheduler::get_vdma_core_op(scheduler_core_op_handle_t core_op_handle,
    const device_id_t &device_id)
{
    return m_scheduled_core_ops.at(core_op_handle)->get_vdma_core_op(device_id);
}

void CoreOpsScheduler::shutdown_core_op(scheduler_core_op_handle_t core_op_handle)
{
    // Deactivate core op from all devices
    for (const auto &device_state : m_devices) {
        if (device_state.second->current_core_op_handle == core_op_handle) {
            auto status = deactivate_core_op(device_state.first);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Scheduler failed deactivate core op on {}", device_state.first);
                // continue
            }
        }
    }

    // Cancel all requests on the queue
    auto core_op = m_scheduled_core_ops.at(core_op_handle);
    while (core_op->requested_infer_requests() > 0) {
        auto request = dequeue_infer_request(core_op_handle);
        assert(request);
        for (auto &transfer : request->transfers) {
            transfer.second.callback(HAILO_STREAM_ABORTED_BY_USER);
        }
        request->callback(HAILO_STREAM_ABORTED_BY_USER);
    }
}

void CoreOpsScheduler::schedule()
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    // First, we are using streaming optimization (where switch is not needed)
    for (auto &core_op_pair : m_scheduled_core_ops) {
        auto status = optimize_streaming_if_enabled(core_op_pair.first);
        if ((HAILO_SUCCESS != status) &&
            (HAILO_STREAM_ABORTED_BY_USER != status)) {
            LOGGER__ERROR("optimize_streaming_if_enabled thread failed with status={}", status);
        }
    };

    // Now, get decisions which requires core op switch
    auto oracle_decisions = CoreOpsSchedulerOracle::get_oracle_decisions(*this);
    for (const auto &run_params : oracle_decisions) {
        auto status = switch_core_op(run_params.core_op_handle, run_params.device_id);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Scheduler thread failed with status={}", status);
            break;
        }
    }

    // Finally, we want to deactivate all core ops with instances_count() == 0
    for (auto &core_op_pair : m_scheduled_core_ops) {
        if (core_op_pair.second->instances_count() == 0) {
            shutdown_core_op(core_op_pair.first);
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