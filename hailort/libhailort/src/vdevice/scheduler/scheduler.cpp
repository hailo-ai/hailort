/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include <fstream>


namespace hailort
{

#define DEFAULT_BURST_SIZE (1)

CoreOpsScheduler::CoreOpsScheduler(hailo_scheduling_algorithm_t algorithm, std::vector<std::string> &devices_ids,
    std::vector<std::string> &devices_arch) :
    SchedulerBase(algorithm, devices_ids, devices_arch),
    m_closest_threshold_timeout(std::chrono::steady_clock::now() + std::chrono::milliseconds(UINT32_MAX)),
    m_scheduler_thread(*this),
    m_preparing_thread(*this)
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
        m_pending_requests[core_op_handle];
        m_ready_requests[core_op_handle];

        const core_op_priority_t normal_priority = HAILO_SCHEDULER_PRIORITY_NORMAL;
        m_core_op_to_run_priority[normal_priority].add(core_op_handle);
        m_core_op_to_prepare_priority[normal_priority].add(core_op_handle);
    }

    return HAILO_SUCCESS;
}

void CoreOpsScheduler::remove_core_op(scheduler_core_op_handle_t core_op_handle)
{
    std::unique_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    m_scheduled_core_ops.at(core_op_handle)->remove_instance();
    m_scheduler_thread.signal(true);
}

void CoreOpsScheduler::shutdown()
{
    // Locking shared_lock since we don't touch the internal scheduler structures.
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    m_scheduler_thread.stop();
    m_preparing_thread.stop();

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

    auto frames_count = std::min(get_frames_ready_to_transfer(core_op_handle, device_id, false), burst_size);
    auto hw_batch_size = scheduled_core_op->use_dynamic_batch_flow() ? frames_count : SINGLE_CONTEXT_BATCH_SIZE;

    if (frames_count == 0) {
        // TODO HRT-11753: don't allow this flow
        return HAILO_SUCCESS;
    }

    curr_device_info->frames_left_before_stop_streaming = burst_size;

    bool has_same_hw_batch_size_as_previous = curr_device_info->current_batch_size == hw_batch_size;
    curr_device_info->current_batch_size = hw_batch_size;

    if ((core_op_handle != curr_device_info->current_core_op_handle) || (!has_same_hw_batch_size_as_previous)) {
        if (curr_device_info->current_core_op_handle != INVALID_CORE_OP_HANDLE &&
            m_scheduled_core_ops.at(curr_device_info->current_core_op_handle)->get_num_pending_requests().load() > 0) {
            LOGGER__DEBUG("Switching core op while there are pending infer requests so we set timeout to current time");
            m_scheduled_core_ops.at(curr_device_info->current_core_op_handle)->set_last_run_timestamp(std::chrono::steady_clock::now());
        }

        TRY(auto next_core_op, get_vdma_core_op(core_op_handle, device_id));

        std::shared_ptr<VdmaConfigCoreOp> current_core_op = nullptr;
        if (curr_device_info->current_core_op_handle != INVALID_CORE_OP_HANDLE) {
            TRY(current_core_op, get_vdma_core_op(curr_device_info->current_core_op_handle, device_id));
        }

        SchedulerTrace switch_core_op_trace(
            [&](uint64_t id){ TRACE(SwitchCoreOpStartTrace, id, scheduled_core_op->get_core_op()->name(), static_cast<uint64_t>(scheduled_core_op->get_timeout().count()),
                scheduled_core_op->get_threshold(), static_cast<int>(hw_batch_size)); },
            [&](uint64_t id){ TRACE(SwitchCoreOpEndTrace, id, scheduled_core_op->get_core_op()->name()); });
        auto status = VdmaConfigManager::set_core_op(device_id, current_core_op, next_core_op, hw_batch_size);
        CHECK_SUCCESS(status, "Failed switching core-op");
    }

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

    TRY (auto vdma_core_op, get_vdma_core_op(core_op_handle, device_id));
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
    TRY (auto vdma_core_op, get_vdma_core_op(core_op_handle, device_id));

    auto infer_request = dequeue_infer_request(core_op_handle);
    CHECK_EXPECTED_AS_STATUS(infer_request);

    auto network_name = scheduled_core_op->get_core_op()->name();
    auto infer_async_trace = std::make_shared<SchedulerTrace>(
        [&](uint64_t id) { TRACE(SchedulerInferAsyncStartTrace, id, network_name, scheduled_core_op->get_num_pending_requests().load(),
                  scheduled_core_op->get_num_ready_requests().load(), current_device_info->ongoing_infer_requests.load()); },
        [network_name](uint64_t id) { TRACE(SchedulerInferAsyncEndTrace, id, network_name); });
        
    current_device_info->ongoing_infer_requests.fetch_add(1);

    auto original_callback = infer_request->callback;
    infer_request->callback = [current_device_info, this, original_callback, infer_async_trace]
        (hailo_status status) mutable {
        (void)infer_async_trace;    
        current_device_info->ongoing_infer_requests.fetch_sub(1);
        m_scheduler_thread.signal(true);
        // infer_async_trace destructor will call the END trace automatically when lambda ends
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

CoreOpsScheduler::ReadyInfo CoreOpsScheduler::is_core_op_ready_for_run(const scheduler_core_op_handle_t &core_op_handle,
    bool check_threshold, const device_id_t &device_id, bool use_ready_queue)
{
    ReadyInfo result;
    result.is_ready = false;

    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);
    auto frames_ready_to_transfer = get_frames_ready_to_transfer(core_op_handle, device_id, use_ready_queue);
    result.is_ready = (frames_ready_to_transfer > 0) && 
        (m_current_core_op_preparing != core_op_handle);

    if (check_threshold) {
        result.over_threshold = scheduled_core_op->is_over_threshold(frames_ready_to_transfer);
        result.over_timeout = scheduled_core_op->is_over_threshold_timeout();

        if (!result.over_threshold && !result.over_timeout){
            result.is_ready = false;
        }
    }

    return result;
}

bool CoreOpsScheduler::is_core_op_ready_for_prepare(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id)
{
    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);
    auto device_info = m_devices.at(device_id);

    if ((scheduled_core_op->instances_count() != 0) && (device_info->current_core_op_handle != core_op_handle) 
        && (scheduled_core_op->get_num_ready_requests().load() < scheduled_core_op->get_burst_size())
        && (scheduled_core_op->get_num_pending_requests().load() > 0)) {
        return true;
    }
    else {
        return false;
    }
}

hailo_status CoreOpsScheduler::enqueue_infer_request(const scheduler_core_op_handle_t &core_op_handle,
    InferRequest &&infer_request)
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);

    CHECK(m_scheduled_core_ops.at(core_op_handle)->instances_count() > 0, HAILO_INTERNAL_FAILURE,
        "Trying to enqueue infer request on a core-op with instances_count==0");
    if (m_scheduled_core_ops.at(core_op_handle)->is_first_frame()) {
        // Mark timestamp on activation
        m_scheduled_core_ops.at(core_op_handle)->set_last_run_timestamp(std::chrono::steady_clock::now());
    }

    SchedulerTrace enqueue_infer_request_trace(
        [&](uint64_t id){ TRACE(SchedulerEnqueueInferRequestStartTrace, id, m_scheduled_core_ops.at(core_op_handle)->get_core_op()->name(), m_scheduled_core_ops.at(core_op_handle)->get_num_pending_requests().load()); },
        [&](uint64_t id){ TRACE(SchedulerEnqueueInferRequestEndTrace, id, m_scheduled_core_ops.at(core_op_handle)->get_core_op()->name()); });

    auto status = m_pending_requests.at(core_op_handle).enqueue(std::move(infer_request));
    if (HAILO_SUCCESS == status) {
        m_scheduled_core_ops.at(core_op_handle)->get_num_pending_requests().fetch_add(1);
        m_scheduler_thread.signal(true);
    }
    return status;
}

// Note: set_timeout is defined to be that if timeout passes and not threshold amount of frames has been sent since the
// last time frames were sent on this core op - send all the frames that are ready to be sent.
hailo_status CoreOpsScheduler::set_timeout(const scheduler_core_op_handle_t &core_op_handle, const std::chrono::milliseconds &timeout, const std::string &/*network_name*/)
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    // TODO: call in loop for set_timeout with the relevant stream-names (of the given network)
    auto status = m_scheduled_core_ops.at(core_op_handle)->set_timeout(timeout);
    if (HAILO_SUCCESS == status) {
        TRACE(SetCoreOpTimeoutTrace, core_op_handle, timeout);
    }

    // this will have to trigger event to recalculate timeouts and check if any have timed out - but dont execute 
    // worker thread unless threshold timeout on core op has actually expired
    update_closest_threshold_timeout();
    m_scheduler_thread.signal(false);

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
    auto &priority_group = m_core_op_to_run_priority[scheduled_core_op->get_priority()];
    assert(priority_group.contains(core_op_handle));
    priority_group.erase(core_op_handle);

    auto &priority_group_prepare = m_core_op_to_prepare_priority[scheduled_core_op->get_priority()];
    assert(priority_group_prepare.contains(core_op_handle));
    priority_group_prepare.erase(core_op_handle);
    
    // Add it to the new priority map.
    m_scheduled_core_ops.at(core_op_handle)->set_priority(priority);
    m_core_op_to_run_priority[priority].add(core_op_handle);
    m_core_op_to_prepare_priority[priority].add(core_op_handle);

    TRACE(SetCoreOpPriorityTrace, core_op_handle, priority);
    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::prepare_transfers()
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);

    struct ResetPreparing {
        CoreOpsScheduler *self;
        ~ResetPreparing() {
            self->m_current_core_op_preparing = INVALID_CORE_OP_HANDLE;
            self->m_scheduler_thread.signal(true);
        }
    } reset_preparing{this};

    auto active_core_op_handle = m_devices.begin()->second->current_core_op_handle;
    auto current_core_op_to_prepare_handle = m_current_core_op_preparing.load();
    auto current_core_op_to_prepare = m_scheduled_core_ops.at(current_core_op_to_prepare_handle);
    if ((m_current_core_op_preparing == INVALID_CORE_OP_HANDLE) || 
        (current_core_op_to_prepare->instances_count() == 0)) {
        return HAILO_SUCCESS;
    }

    CHECK(((current_core_op_to_prepare->get_num_ready_requests().load() < current_core_op_to_prepare->get_burst_size()) && 
        (current_core_op_to_prepare->get_num_pending_requests().load() > 0) &&
        (current_core_op_to_prepare_handle != active_core_op_handle)), HAILO_INTERNAL_FAILURE);

    while ((current_core_op_to_prepare->get_num_ready_requests().load() < current_core_op_to_prepare->get_burst_size()) && 
        (current_core_op_to_prepare->get_num_pending_requests().load() > 0)) {
    
        TRY(auto infer_request, m_pending_requests.at(current_core_op_to_prepare_handle).dequeue());
        current_core_op_to_prepare->get_num_pending_requests().fetch_sub(1);
        TRY(auto vdma_core_op, get_vdma_core_op(current_core_op_to_prepare_handle, m_devices.begin()->second->device_id));

        SchedulerTrace prepare_transfers_trace(
            [&](uint64_t id){ TRACE(PrepareCoreOpStartTrace, id, current_core_op_to_prepare->get_core_op()->name(), current_core_op_to_prepare->get_num_pending_requests().load(),
                current_core_op_to_prepare->get_num_ready_requests().load(), current_core_op_to_prepare->get_burst_size()); },
            [&](uint64_t id){ TRACE(PrepareCoreOpEndTrace, id, current_core_op_to_prepare->get_core_op()->name()); });
        
        auto status = (vdma_core_op->prepare_transfers(infer_request.transfers));
        m_ready_requests[current_core_op_to_prepare_handle].enqueue(std::move(infer_request));
        current_core_op_to_prepare->get_num_ready_requests().fetch_add(1);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to bind buffers for core op {}", current_core_op_to_prepare_handle);
            return status;
        }
    }
    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::cancel_prepared_transfers(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle)
{
    if (core_op_handle != INVALID_CORE_OP_HANDLE) {
        TRY(auto vdma_core_op, get_vdma_core_op(core_op_handle, device_id));
        CHECK_SUCCESS(vdma_core_op->cancel_prepared_transfers());
    }
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
        // if HAILO_ENABLE_IDLE_OPT_ENV_VAR then we want the burst size to be the threshold
        auto burst_size = is_env_variable_on(HAILO_ENABLE_IDLE_OPT_ENV_VAR)? DEFAULT_BURST_SIZE: scheduled_core_op->get_threshold();
        if (device_info->current_core_op_handle == core_op_handle && !device_info->is_switching_core_op &&
            !CoreOpsSchedulerOracle::should_stop_streaming(*this, scheduled_core_op->get_priority(), device_info->device_id) &&
            (get_frames_ready_to_transfer(core_op_handle, device_info->device_id, false) >= burst_size)) {
            auto status = send_all_pending_buffers(core_op_handle, device_info->device_id, burst_size);
            CHECK_SUCCESS(status);
        }
    }
    return HAILO_SUCCESS;
}

Expected<InferRequest> CoreOpsScheduler::dequeue_infer_request(scheduler_core_op_handle_t core_op_handle)
{
    hailort::InferRequest infer_request;
    if (m_ready_requests.at(core_op_handle).size() > 0) {
        TRY(infer_request, m_ready_requests.at(core_op_handle).dequeue());
        m_scheduled_core_ops.at(core_op_handle)->get_num_ready_requests().fetch_sub(1);
    } else {
        TRY(infer_request, m_pending_requests.at(core_op_handle).dequeue());
        m_scheduled_core_ops.at(core_op_handle)->get_num_pending_requests().fetch_sub(1);
    }
    return infer_request;
}

uint16_t CoreOpsScheduler::get_frames_ready_to_transfer(scheduler_core_op_handle_t core_op_handle,
    const device_id_t &device_id, bool use_ready_queue) const
{
    auto scheduled_core_op = m_scheduled_core_ops.at(core_op_handle);
    auto device_info = m_devices.at(device_id);

    if (scheduled_core_op->instances_count() == 0) {
        // We don't want to schedule/execute core ops with instances_count() == 0. There may still be
        // get_num_pending_requests until shutdown_core_op is called.
        // TODO: HRT-12218 after dequeue all infer requests for the instance in remove_core_op, this flow can be
        // removed any simplified (since on this case get_num_pending_requests == 0).
        return 0;
    }

    const auto max_ongoing_frames = scheduled_core_op->get_max_ongoing_frames_per_device();
    const uint32_t ongoing_frames = (device_info->current_core_op_handle == core_op_handle) ?
        device_info->ongoing_infer_requests.load() : 0;
    assert(ongoing_frames <= max_ongoing_frames);

    const uint32_t requested_frames = (use_ready_queue)
        ? scheduled_core_op->get_num_ready_requests().load()
        : (scheduled_core_op->get_num_pending_requests().load() + scheduled_core_op->get_num_ready_requests().load());

    return static_cast<uint16_t>(std::min(requested_frames, max_ongoing_frames - ongoing_frames));
}

Expected<std::shared_ptr<VdmaConfigCoreOp>> CoreOpsScheduler::get_vdma_core_op(scheduler_core_op_handle_t core_op_handle,
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
       else {
            auto status = cancel_prepared_transfers(device_state.first, core_op_handle);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Scheduler failed cancel pending transfers on {}", device_state.first);
                // continue
            }
       } 
    }

    // Cancel all requests on the queue
    auto core_op = m_scheduled_core_ops.at(core_op_handle);
    while (core_op->get_num_pending_requests() > 0 || core_op->get_num_ready_requests() > 0) {
        auto request = dequeue_infer_request(core_op_handle);
        assert(request);
        for (auto &transfer : request->transfers) {
            transfer.second.callback(HAILO_STREAM_ABORT);
        }

        // Before calling infer_callback, we must ensure all stream callbacks were called and released (since the
        // user may capture some variables in the callbacks).
        request->transfers.clear();
        request->callback(HAILO_STREAM_ABORT);
    }
}

void CoreOpsScheduler::schedule()
{
    std::shared_lock<std::shared_timed_mutex> lock(m_scheduler_mutex);
    // First, we are using streaming optimization (where switch is not needed)
    for (auto &core_op_pair : m_scheduled_core_ops) {
        auto status = optimize_streaming_if_enabled(core_op_pair.first);
        if ((HAILO_SUCCESS != status) &&
            (HAILO_STREAM_ABORT != status)) {
            LOGGER__ERROR("optimize_streaming_if_enabled thread failed with status={}", status);
        }
    };

    // Now, get decisions which requires core op switch
    auto oracle_decisions = CoreOpsSchedulerOracle::get_oracle_run_decisions(*this);
    for (const auto &run_params : oracle_decisions) {
        auto status = switch_core_op(run_params.core_op_handle, run_params.device_id);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Scheduler thread failed with status={}", status);
            break;
        }
    }

    // Finally, we want to deactivate all core ops with instances_count() == 0
    for (auto &core_op_pair : m_scheduled_core_ops) {
        if (core_op_pair.second->instances_count() == 0 && m_current_core_op_preparing == INVALID_CORE_OP_HANDLE) {
            shutdown_core_op(core_op_pair.first);
        }
    }
    
    // get decision for next core op to prepare
    auto current_core_op_to_prepare_handle = CoreOpsSchedulerOracle::choose_next_model_to_prepare(*this, m_devices.begin()->second->device_id);
    if (INVALID_CORE_OP_HANDLE != current_core_op_to_prepare_handle) {
        m_current_core_op_preparing = current_core_op_to_prepare_handle;
        m_preparing_thread.signal();
    }

    update_closest_threshold_timeout();
}

void CoreOpsScheduler::update_closest_threshold_timeout()
{
    m_closest_threshold_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(UINT32_MAX);
    for (const auto &core_op_pair : m_scheduled_core_ops) {
        auto scheduled_core_op = core_op_pair.second;
        // Only update the closest threshold timeout if the core op has instances and timeout set to non default
        if ((0 < scheduled_core_op->instances_count()) && (std::chrono::milliseconds(0) != scheduled_core_op->get_timeout())) {
            m_closest_threshold_timeout = std::min(m_closest_threshold_timeout,
                scheduled_core_op->get_last_run_timestamp() + scheduled_core_op->get_timeout());
        }
    }
}

std::chrono::milliseconds CoreOpsScheduler::get_closest_threshold_timeout() const
{
    // Get closest timeout and wait for it or for signal
    const auto time_now = std::chrono::steady_clock::now();
    return (m_closest_threshold_timeout > time_now) ?
        std::chrono::duration_cast<std::chrono::milliseconds>(m_closest_threshold_timeout - time_now) :
        // In case time_now is bigger than m_closest_threshold_timeout - timeout has already occured and we should
        // signal the worker thread
        std::chrono::milliseconds(0);
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

void CoreOpsScheduler::SchedulerThread::signal(bool execute_worker_thread)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_execute_worker_thread = execute_worker_thread;
    }
    m_cv.notify_one();
}

void CoreOpsScheduler::SchedulerThread::stop()
{
    if (m_thread.joinable()) {
        m_is_running = false;
        signal(true);
        m_thread.join();
    }
}

void CoreOpsScheduler::SchedulerThread::worker_thread_main()
{
    OsUtils::set_current_thread_name("SCHEDULER");

    while (m_is_running) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            const auto next_timeout_in_ms = m_scheduler.get_closest_threshold_timeout();
            m_cv.wait_for(lock, next_timeout_in_ms, [this]() {
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

CoreOpsScheduler::PreparingThread::PreparingThread(CoreOpsScheduler &scheduler) :
    m_scheduler(scheduler),
    m_is_running(true),
    m_execute_worker_thread(false),
    m_thread([this] { prepare_worker_thread_main(); })
{}

CoreOpsScheduler::PreparingThread::~PreparingThread()
{
    stop();
}

void CoreOpsScheduler::PreparingThread::signal()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_execute_worker_thread = true;
    }
    m_cv.notify_one();
}

void CoreOpsScheduler::PreparingThread::stop()
{
    if (m_thread.joinable()) {
        m_is_running = false;
        signal();
        m_thread.join();
    }
}

void CoreOpsScheduler::PreparingThread::prepare_worker_thread_main()
{
    if (m_scheduler.m_devices.size() > 1) {
        return;
    }

    while (m_is_running) {
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this]() {
                return  m_execute_worker_thread.load();
            });
            m_execute_worker_thread = false;
        }

        if (!m_is_running) {
            break;
        }

   	    m_scheduler.prepare_transfers();
    }
}

CoreOpsScheduler::SchedulerTrace::~SchedulerTrace() {
    if (m_end) {
        m_end(m_id);
    }
}

CoreOpsScheduler::SchedulerTrace::SchedulerTrace(SchedulerTrace&& other) noexcept 
    : m_id(other.m_id), m_end(std::move(other.m_end))
{
    other.m_end = nullptr; 
}

CoreOpsScheduler::SchedulerTrace& CoreOpsScheduler::SchedulerTrace::operator=(SchedulerTrace&& other) noexcept
{
    if (this != &other) {
        if (m_end) {
            m_end(m_id);
        }
        
        m_id = other.m_id;
        m_end = std::move(other.m_end);
        other.m_end = nullptr;
    }
    return *this;
}

uint64_t CoreOpsScheduler::SchedulerTrace::next_trace_id() {
    static std::atomic<uint64_t> counter{0};
    return counter.fetch_add(1);
}

} /* namespace hailort */