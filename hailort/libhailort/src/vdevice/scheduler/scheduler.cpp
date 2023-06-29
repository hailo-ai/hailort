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
#include "utils/profiler/tracer_macros.hpp"

#include <fstream>


namespace hailort
{

#define SINGLE_CONTEXT_BATCH_SIZE (1)
#define DEFAULT_BURST_SIZE (1)

// TODO: use device handles instead device count
CoreOpsScheduler::CoreOpsScheduler(hailo_scheduling_algorithm_t algorithm, std::vector<std::string> &devices_ids, 
    std::vector<std::string> &devices_arch) :
    SchedulerBase(algorithm, devices_ids, devices_arch),
    m_should_core_op_stop(),
    m_before_read_write_mutex(),
    m_core_ops_cvs(),
    m_scheduler_cv()
{
    TRACE(SchedulerStartTrace, get_device_count());
    for (const auto &pair : m_devices) {
        auto &device_info = pair.second;
        TRACE(AddDeviceTrace, device_info->device_id, device_info->device_arch);
    }

    m_is_running = true;
    m_scheduler_thread = std::thread(&CoreOpsScheduler::worker_thread_main, this);
    m_execute_worker_thread = true;
}

CoreOpsScheduler::~CoreOpsScheduler()
{
    for (const auto &pair : m_devices) {
        auto &device_info = pair.second;
        if (INVALID_CORE_OP_HANDLE != device_info->current_core_op_handle) {
            auto current_core_op = m_scheduled_core_ops[device_info->current_core_op_handle]->get_core_op();
            auto current_core_op_bundle = std::dynamic_pointer_cast<VDeviceCoreOp>(current_core_op);
            assert(nullptr != current_core_op_bundle);
            auto vdma_core_op = current_core_op_bundle->get_core_op_by_device_id(device_info->device_id);
            if (!vdma_core_op) {
                LOGGER__ERROR("Error retrieving core-op in scheduler destructor");
            } else {
                if (HAILO_SUCCESS != VdmaConfigManager::deactivate_core_op(vdma_core_op.value())) {
                    LOGGER__ERROR("Error deactivating core-op when destroying scheduler");
                }
            }
        }
    }

    // signal scheduler thread to stop and join
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        m_is_running = false;
        m_execute_worker_thread = true;
    }
    m_scheduler_cv.notify_one();
    if (m_scheduler_thread.joinable()) {
        m_scheduler_thread.join();
    }
}

Expected<CoreOpsSchedulerPtr> CoreOpsScheduler::create_round_robin(std::vector<std::string> &devices_bdf_id, std::vector<std::string> &devices_arch)
{
    auto ptr = make_shared_nothrow<CoreOpsScheduler>(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN, devices_bdf_id, devices_arch);
    CHECK_AS_EXPECTED(nullptr != ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

std::string CoreOpsScheduler::get_core_op_name(const scheduler_core_op_handle_t &core_op_handle)
{
    assert(m_scheduled_core_ops.size() > core_op_handle);
    return m_scheduled_core_ops[core_op_handle]->get_core_op_name();
}

Expected<scheduler_core_op_handle_t > CoreOpsScheduler::add_core_op(std::shared_ptr<CoreOp> added_cng)
{
    scheduler_core_op_handle_t core_op_handle = INVALID_CORE_OP_HANDLE;
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        core_op_handle = static_cast<uint32_t>(m_scheduled_core_ops.size());

        auto stream_infos = added_cng->get_all_stream_infos();
        CHECK_EXPECTED(stream_infos);

        auto scheduled_core_op = ScheduledCoreOp::create(added_cng, stream_infos.value());
        CHECK_EXPECTED(scheduled_core_op);

        bool is_nms = scheduled_core_op->get()->is_nms();
        TRACE(AddCoreOpTrace, "", added_cng->name(), DEFAULT_SCHEDULER_TIMEOUT.count(), DEFAULT_SCHEDULER_MIN_THRESHOLD,
            core_op_handle, is_nms);

        m_scheduled_core_ops.emplace_back(scheduled_core_op.release());


        for (const auto &stream_info : stream_infos.value()) {
            m_should_core_op_stop[core_op_handle][stream_info.name] = false;
        }

        for (const auto &pair : m_devices) {
	        auto &device_info = pair.second;
            for (const auto &stream_info : stream_infos.value()) {
                if (HAILO_H2D_STREAM == stream_info.direction) {
                    device_info->current_cycle_requested_transferred_frames_h2d[core_op_handle][stream_info.name] = 0;
                } else {
                    device_info->current_cycle_finished_transferred_frames_d2h[core_op_handle][stream_info.name] = 0;
                    device_info->pending_to_read_frames[core_op_handle][stream_info.name] = 0;
                }
            }
        }

        auto network_cvs = ScheduledCoreOpCV::create(added_cng);
        CHECK_EXPECTED(network_cvs);
        m_core_ops_cvs[core_op_handle] = network_cvs.release();
        m_core_op_priority[HAILO_SCHEDULER_PRIORITY_NORMAL].emplace_back(core_op_handle);
    }

    return core_op_handle;
}

bool CoreOpsScheduler::is_core_op_active(const scheduler_core_op_handle_t &core_op_handle)
{
    for (const auto &pair : m_devices) {
        auto &device_info = pair.second;
        if (core_op_handle == device_info->current_core_op_handle) {
            return true;
        }
    }

    return false;
}

bool CoreOpsScheduler::is_multi_device()
{
    return m_devices.size() > 1;
}

hailo_status CoreOpsScheduler::signal_frame_pending_to_send(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        assert(m_scheduled_core_ops.size() > core_op_handle);
        auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];

        if (should_core_op_stop(core_op_handle)) {
            return HAILO_STREAM_ABORTED_BY_USER;
        }

        TRACE(WriteFrameTrace, "", core_op_handle, stream_name);

        m_scheduled_core_ops[core_op_handle]->mark_frame_sent();
        scheduled_core_op->pending_to_send_frames().increase(stream_name);
        m_execute_worker_thread = true;
    }
    m_scheduler_cv.notify_one();

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::switch_core_op(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id, bool /*keep_nn_config*/)
{
    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
    assert(contains(m_devices, device_id));
    auto curr_device_info = m_devices[device_id];
    curr_device_info->is_switching_core_op = false;

    // initialize current cycle maps
    for (const auto &name : scheduled_core_op->get_inputs_names()) {
        curr_device_info->current_cycle_requested_transferred_frames_h2d[core_op_handle][name] = 0;
    }

    for (const auto &name : scheduled_core_op->get_outputs_names()) {
        curr_device_info->current_cycle_finished_transferred_frames_d2h[core_op_handle][name] = 0;
    }

    uint16_t batch_size = std::min(scheduled_core_op->get_max_batch_size(), get_min_avail_buffers_count(core_op_handle, device_id));
    uint16_t hw_batch_size = SINGLE_CONTEXT_BATCH_SIZE;

    if (scheduled_core_op->use_dynamic_batch_flow()) {
        batch_size = std::min(static_cast<uint16_t>(scheduled_core_op->pending_to_send_frames_min_value()), batch_size);
        hw_batch_size = batch_size;
    }

    if (batch_size == 0) {
        return HAILO_SUCCESS;
    }

    bool has_same_hw_batch_size_as_previous = scheduled_core_op->use_dynamic_batch_flow() ? (curr_device_info->current_batch_size == batch_size) : true;
    curr_device_info->current_batch_size = batch_size;

    if ((core_op_handle != curr_device_info->current_core_op_handle) || (!has_same_hw_batch_size_as_previous)) {
        assert(m_scheduled_core_ops.size() > core_op_handle);
        auto next_active_cng = scheduled_core_op->get_core_op();
        auto next_active_cng_wrapper = std::dynamic_pointer_cast<VDeviceCoreOp>(next_active_cng);
        assert(nullptr != next_active_cng_wrapper);
        auto next_active_cng_expected = next_active_cng_wrapper->get_core_op_by_device_id(curr_device_info->device_id);
        CHECK_EXPECTED_AS_STATUS(next_active_cng_expected);

        std::shared_ptr<VdmaConfigCoreOp> current_active_vdma_cng = nullptr;
        if (curr_device_info->current_core_op_handle != INVALID_CORE_OP_HANDLE) {
            auto current_active_cng = m_scheduled_core_ops[curr_device_info->current_core_op_handle]->get_core_op();
            auto current_active_cng_bundle = std::dynamic_pointer_cast<VDeviceCoreOp>(current_active_cng);
            assert(nullptr != current_active_cng_bundle);
            auto current_active_cng_expected = current_active_cng_bundle->get_core_op_by_device_id(curr_device_info->device_id);
            CHECK_EXPECTED_AS_STATUS(current_active_cng_expected);
            current_active_vdma_cng = current_active_cng_expected.release();

            // Flushing h2d channel in order to make sure we got all interrupts before switching the network.
            for (auto &stream : current_active_vdma_cng->get_input_streams()) {
                auto status = stream.get().flush();
                if (HAILO_STREAM_ABORTED_BY_USER == status) {
                    continue;
                }
                CHECK_SUCCESS(status);
            }
        }

        TRACE(SwitchCoreOpTrace, device_id, core_op_handle);
        static const auto RESUME_PENDING_STREAM_TRANSFERS = true;
        auto status = VdmaConfigManager::switch_core_op(current_active_vdma_cng, next_active_cng_expected.value(), hw_batch_size,
            RESUME_PENDING_STREAM_TRANSFERS);
        CHECK_SUCCESS(status, "Failed switching core-op");
    }

    scheduled_core_op->set_last_run_timestamp(std::chrono::steady_clock::now()); // Mark timestamp on activation
    curr_device_info->current_core_op_handle = core_op_handle;

    auto status = send_all_pending_buffers(core_op_handle, device_id, batch_size);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("send_all_pending_buffers has failed with status=HAILO_STREAM_ABORTED_BY_USER");
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

void CoreOpsScheduler::signal_read_finish_impl(const scheduler_core_op_handle_t &core_op_handle,
    const std::string &stream_name, const device_id_t &device_id)
{
    TRACE(ReadFrameTrace, "", core_op_handle, stream_name);

    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
    scheduled_core_op->requested_read_frames().decrease(stream_name);
    scheduled_core_op->finished_read_frames().increase(stream_name);
    scheduled_core_op->d2h_finished_transferred_frames().decrease(stream_name);

    if (m_devices[device_id]->pending_to_read_frames[core_op_handle][stream_name] > 0) {
        m_devices[device_id]->pending_to_read_frames[core_op_handle][stream_name]--;
    }

    decrease_core_op_counters(core_op_handle);

    auto has_drained_everything = has_core_op_drained_everything(core_op_handle, device_id);
    if (scheduled_core_op->is_nms() && has_drained_everything) {
        // In NMS networks there is possibility that next wasn't choosen yet
        choose_next_core_op(device_id, true);

        // If we didn't choose with threshold or timeout lets choose without threshold
        if (!m_devices[device_id]->is_switching_core_op) {
            choose_next_core_op(device_id, false);
        }

        if (has_drained_everything) {
            TRACE(CoreOpIdleTrace, device_id, core_op_handle);
        }
    }

    m_execute_worker_thread = true;
}

hailo_status CoreOpsScheduler::send_all_pending_buffers(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id, uint32_t burst_size)
{
    auto current_device_info = m_devices[device_id];
    if ((INVALID_CORE_OP_HANDLE == current_device_info->current_core_op_handle) || (current_device_info->current_core_op_handle != core_op_handle)) {
        return HAILO_SUCCESS;
    }

    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];

    for (size_t i = 0; i < burst_size; i++) {
        auto finished_send = false;
        for (const auto &name : scheduled_core_op->get_inputs_names()) {
            if (scheduled_core_op->pending_to_send_frames(name) == 0) {
                finished_send = true;
                break;
            }
        }
        if (finished_send) {
            break;
        }

        for (const auto &name : scheduled_core_op->get_outputs_names()) {
            auto output_stream = scheduled_core_op->get_core_op()->get_output_stream_by_name(name);
            CHECK_EXPECTED_AS_STATUS(output_stream);

            auto &output_stream_base = static_cast<OutputStreamBase&>(output_stream->get());
            auto status = output_stream_base.set_next_device_to_read(device_id);
            CHECK_SUCCESS(status);
        }

        for (const auto &name : scheduled_core_op->get_inputs_names()) {
            auto status = send_pending_buffer(core_op_handle, name, device_id);
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("send_pending_buffer has failed with status=HAILO_STREAM_ABORTED_BY_USER");
                return status;
            }
            CHECK_SUCCESS(status);
        }
        scheduled_core_op->set_last_device(device_id);
    }

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::send_pending_buffer(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
    const device_id_t &device_id)
{
    assert(m_scheduled_core_ops.size() > core_op_handle);
    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];

    auto current_cng = scheduled_core_op->get_core_op();
    auto input_stream = current_cng->get_input_stream_by_name(stream_name);
    CHECK_EXPECTED_AS_STATUS(input_stream);

    auto &input_stream_base = static_cast<InputStreamBase&>(input_stream->get());
    auto status = input_stream_base.send_pending_buffer(device_id);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("send_pending_buffer has failed with status=HAILO_STREAM_ABORTED_BY_USER");
        return status;
    }
    CHECK_SUCCESS(status);

    TRACE(InputVdmaDequeueTrace, device_id, core_op_handle, stream_name);

    m_devices[device_id]->current_cycle_requested_transferred_frames_h2d[core_op_handle][stream_name]++;
    scheduled_core_op->pending_to_send_frames().decrease(stream_name);
    // Notifying for flush
    m_core_ops_cvs[core_op_handle]->notify_one(stream_name);

    scheduled_core_op->h2d_finished_transferred_frames().increase(stream_name);

    if (should_core_op_stop(core_op_handle)) {
        return HAILO_STREAM_ABORTED_BY_USER;
    }

    return HAILO_SUCCESS;
}

CoreOpsScheduler::ReadyInfo CoreOpsScheduler::is_core_op_ready(const scheduler_core_op_handle_t &core_op_handle, bool check_threshold)
{
    ReadyInfo result;
    result.is_ready = false;

    if (should_core_op_stop(core_op_handle)) {
        // Do not switch to an aborted core-op
        return result;
    }

    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
    // Check if there arent any write requests
    const bool has_pending_writes = scheduled_core_op->pending_to_send_frames_min_value() > 0;

    // Check for read request on all the output streams
    const bool has_avail_pending_to_read_buffers = get_min_avail_output_buffers(core_op_handle) > 0;

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
            uint32_t write_requests = scheduled_core_op->pending_to_send_frames(name);
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

    result.is_ready = has_pending_writes && has_avail_pending_to_read_buffers;

    return result;
}

hailo_status CoreOpsScheduler::wait_for_read(const scheduler_core_op_handle_t &core_op_handle,
    const std::string &stream_name, const std::chrono::milliseconds &timeout, const std::function<bool()> &predicate)
{
    std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

    hailo_status status = HAILO_SUCCESS;
    auto wait_res = m_core_ops_cvs[core_op_handle]->wait_for(stream_name, lock, timeout,
        [this, core_op_handle, predicate, &stream_name, &status] {
        if (m_should_core_op_stop[core_op_handle][stream_name]) {
            status = HAILO_STREAM_ABORTED_BY_USER;
            return true; // return true so that the wait will finish
        }

        return predicate();
    });
    CHECK(wait_res, HAILO_TIMEOUT, "{} (D2H) failed with status={}, timeout={}ms", stream_name, HAILO_TIMEOUT, timeout.count());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::signal_frame_pending_to_read(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
        scheduled_core_op->requested_read_frames().increase(stream_name);
        m_execute_worker_thread = true;
    }
    m_scheduler_cv.notify_one();

    return HAILO_SUCCESS;
}

void CoreOpsScheduler::signal_frame_transferred_d2h(const scheduler_core_op_handle_t &core_op_handle,
    const std::string &stream_name, const device_id_t &device_id)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
        if (!scheduled_core_op->is_nms()) {
            TRACE(OutputVdmaEnqueueTrace, "", core_op_handle, stream_name, 1);
            // TODO: Remove d2h_finished_transferred_frames and use current_cycle_finished_transferred_frames_d2h instead
            scheduled_core_op->d2h_finished_transferred_frames().increase(stream_name);
            m_devices[device_id]->pending_to_read_frames[core_op_handle][stream_name] += 1;
            m_devices[device_id]->current_cycle_finished_transferred_frames_d2h[core_op_handle][stream_name] += 1;
        }

        auto has_drained_everything = has_core_op_drained_everything(core_op_handle, device_id);

        if (has_drained_everything) {
            TRACE(CoreOpIdleTrace, device_id, core_op_handle);
        }

        // If ng finished and we didn't choose next lets choose without checking threshold
        if (!m_devices[device_id]->is_switching_core_op && has_drained_everything) {
            auto was_chosen  = choose_next_core_op(device_id, true);
            if (!was_chosen) {
                choose_next_core_op(device_id, false);
            }
        }

        if (m_devices[device_id]->is_switching_core_op) {
            m_execute_worker_thread = true;
        }
    }

    // Notify stream that new frame was accepted (wait_for read operation)
    m_core_ops_cvs[core_op_handle]->notify_one(stream_name);
    m_scheduler_cv.notify_one();
}

hailo_status CoreOpsScheduler::signal_read_finish(const scheduler_core_op_handle_t &core_op_handle,
    const std::string &stream_name, const device_id_t &device_id)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        signal_read_finish_impl(core_op_handle, stream_name, device_id);
    }
    m_scheduler_cv.notify_one();
    return HAILO_SUCCESS;
}

void CoreOpsScheduler::decrease_core_op_counters(const scheduler_core_op_handle_t &core_op_handle)
{
    return m_scheduled_core_ops[core_op_handle]->decrease_current_core_op_counters();
}

bool CoreOpsScheduler::has_core_op_drained_everything(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id)
{
    if (core_op_all_streams_aborted(core_op_handle)) {
        // We treat core-op as drained only if all streams are aborted - to make sure there aren't any ongoing transfers
        return true;
    }

    if (INVALID_CORE_OP_HANDLE == core_op_handle) {
        // If no core-op is running, consider it as drained
        return true;
    }

    if ((!m_scheduled_core_ops[core_op_handle]->is_nms()) && (is_multi_device() || m_scheduled_core_ops[core_op_handle]->use_dynamic_batch_flow())) {
        auto current_device_info = m_devices[device_id];
        auto max_transferred_h2d = get_max_value_of_unordered_map(current_device_info->current_cycle_requested_transferred_frames_h2d[core_op_handle]);
        auto min_transferred_d2h = get_min_value_of_unordered_map(current_device_info->current_cycle_finished_transferred_frames_d2h[core_op_handle]);

        return (max_transferred_h2d == min_transferred_d2h);
    }

    return m_scheduled_core_ops[core_op_handle]->has_core_op_drained_everything();
}

hailo_status CoreOpsScheduler::flush_pending_buffers(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
    const std::chrono::milliseconds &timeout)
{
    std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

    hailo_status status = HAILO_SUCCESS;
    auto wait_res = m_core_ops_cvs[core_op_handle]->wait_for(stream_name, lock, timeout,
        [this, core_op_handle, &stream_name, &status] {
        if (should_core_op_stop(core_op_handle)) {
            status = HAILO_STREAM_ABORTED_BY_USER;
            return true; // return true so that the wait will finish
        }

        assert(m_scheduled_core_ops.size() > core_op_handle);
        auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
        auto pending = scheduled_core_op->pending_to_send_frames(stream_name).load();
        return (pending == 0);
    });
    CHECK(wait_res, HAILO_TIMEOUT, "{} (H2D) failed with status={}, timeout={}ms", stream_name, HAILO_TIMEOUT, timeout.count());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("flush pending buffers was aborted in stream ={}", stream_name);
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::enable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        if (!m_should_core_op_stop[core_op_handle][stream_name]) {
            return HAILO_SUCCESS;
        }

        m_should_core_op_stop[core_op_handle][stream_name] = false;
    }
    m_core_ops_cvs[core_op_handle]->notify_all();

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::disable_stream(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        if (m_should_core_op_stop[core_op_handle][stream_name]) {
            return HAILO_SUCCESS;
        }

        m_should_core_op_stop[core_op_handle][stream_name] = true;
    }
    m_core_ops_cvs[core_op_handle]->notify_all();

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::set_timeout(const scheduler_core_op_handle_t &core_op_handle, const std::chrono::milliseconds &timeout, const std::string &/*network_name*/)
{
    // TODO: call in loop for set_timeout with the relevant stream-names (of the given network)
    return m_scheduled_core_ops[core_op_handle]->set_timeout(timeout);
}

hailo_status CoreOpsScheduler::set_threshold(const scheduler_core_op_handle_t &core_op_handle, uint32_t threshold, const std::string &/*network_name*/)
{
    // TODO: call in loop for set_timeout with the relevant stream-names (of the given network)
    return m_scheduled_core_ops[core_op_handle]->set_threshold(threshold);
}

hailo_status CoreOpsScheduler::set_priority(const scheduler_core_op_handle_t &core_op_handle, core_op_priority_t priority, const std::string &/*network_name*/)
{
    CHECK(priority <= HAILO_SCHEDULER_PRIORITY_MAX, HAILO_INVALID_ARGUMENT);
    std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
    auto old_priority = m_scheduled_core_ops[core_op_handle]->get_priority();
    auto &priority_vector = m_core_op_priority[old_priority];
    auto it = std::find(priority_vector.begin(), priority_vector.end(), core_op_handle);
    CHECK(it != priority_vector.end(), HAILO_INTERNAL_FAILURE);

    priority_vector.erase(it);
    m_scheduled_core_ops[core_op_handle]->set_priority(priority);
    m_core_op_priority[priority].push_back(core_op_handle);

    return HAILO_SUCCESS;
}

bool CoreOpsScheduler::choose_next_core_op(const device_id_t &device_id, bool check_threshold)
{
    if (!m_devices[device_id]->is_switching_core_op) {
        return CoreOpsSchedulerOracle::choose_next_model(*this, m_devices[device_id]->device_id, check_threshold) != INVALID_CORE_OP_HANDLE;
    }
    return false;
}

bool CoreOpsScheduler::should_core_op_stop(const scheduler_core_op_handle_t &core_op_handle)
{
    for (const auto &name_flag_pair : m_should_core_op_stop[core_op_handle]) {
        if (name_flag_pair.second) {
            return true;
        }
    }

    return false;
}

bool CoreOpsScheduler::core_op_all_streams_aborted(const scheduler_core_op_handle_t &core_op_handle)
{
    for (const auto &name_flag_pair : m_should_core_op_stop[core_op_handle]) {
        if (!name_flag_pair.second) {
            return false;
        }
    }
    return true;
}

void CoreOpsScheduler::notify_all()
{
    {
        // Acquire mutex to make sure the notify_all will wake the blocking threads on the cv
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
    }
    // TODO: consider notify only the relevant ng or stream
    for (auto &cng_cvs : m_core_ops_cvs) {
        cng_cvs.second->notify_all();
    }
}

hailo_status CoreOpsScheduler::optimize_streaming_if_enabled(const scheduler_core_op_handle_t &core_op_handle)
{
    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
    if (!scheduled_core_op->use_dynamic_batch_flow()) {
        auto next_pair = m_devices.upper_bound(scheduled_core_op->get_last_device()); // Get last device and go to the next device in the map
        if (m_devices.end() == next_pair){ // In case we reached to the end of the map - start from the beggining
            next_pair = m_devices.begin();
        }
        auto &device_info = next_pair->second;
        if (device_info->current_core_op_handle == core_op_handle && !device_info->is_switching_core_op &&
            !CoreOpsSchedulerOracle::should_stop_streaming(*this, scheduled_core_op->get_priority(), device_info->device_id) &&
            (get_min_avail_buffers_count(core_op_handle, device_info->device_id) >= DEFAULT_BURST_SIZE)) {
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

uint16_t CoreOpsScheduler::get_min_avail_buffers_count(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id)
{
    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
    auto device_info = m_devices[device_id];

    uint16_t avail_buffer_count = UINT16_MAX;
    for (auto &output_stream : scheduled_core_op->get_core_op()->get_output_streams()) {
        auto &vdevice_output = static_cast<OutputStreamBase&>(output_stream.get());
        if (auto buffer_size_in_frames = vdevice_output.get_buffer_frames_size()) {
            auto &pending_frames_in_buffer = device_info->pending_to_read_frames[core_op_handle][vdevice_output.name()];
            auto ongoing_frames = get_max_value_of_unordered_map(device_info->current_cycle_requested_transferred_frames_h2d[core_op_handle]) -
                device_info->current_cycle_finished_transferred_frames_d2h[core_op_handle][vdevice_output.name()];
            assert(*buffer_size_in_frames >= (pending_frames_in_buffer + ongoing_frames));
            avail_buffer_count = std::min(avail_buffer_count, static_cast<uint16_t>(*buffer_size_in_frames - pending_frames_in_buffer - ongoing_frames));
        }
    }

    auto transferred_frames = get_max_value_of_unordered_map(device_info->current_cycle_requested_transferred_frames_h2d[core_op_handle]) -
        get_min_value_of_unordered_map(device_info->current_cycle_finished_transferred_frames_d2h[core_op_handle]);
    if (is_multi_device()) {
        auto avail_input_buffer_count = static_cast<uint16_t>((scheduled_core_op->get_min_input_buffers_count()) - transferred_frames);
        avail_buffer_count = std::min(avail_input_buffer_count, avail_buffer_count);
    }

    return avail_buffer_count;
}

uint16_t CoreOpsScheduler::get_min_avail_output_buffers(const scheduler_core_op_handle_t &core_op_handle)
{
    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
    auto sent_frames = scheduled_core_op->h2d_finished_transferred_frames_max_value() -
        scheduled_core_op->finished_read_frames_min_value();

    return static_cast<uint16_t>((scheduled_core_op->get_min_output_buffers_count()) - sent_frames);
}

void CoreOpsScheduler::worker_thread_main()
{
    OsUtils::set_current_thread_name("SCHEDULER");
    std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
    while (m_is_running) {

        m_scheduler_cv.wait(lock, [this]() {
            return m_execute_worker_thread.load();
        });
        m_execute_worker_thread = false;

        if (!m_is_running) {
            break;
        }

        for (uint32_t core_op_handle = 0; core_op_handle < m_scheduled_core_ops.size(); core_op_handle++) {
            auto status = optimize_streaming_if_enabled(core_op_handle);
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                continue;
            }

            if (HAILO_SUCCESS != status) {
                if (m_is_running) {
                    LOGGER__ERROR("Scheduler thread failed with status={}", status);
                }
                break;
            }
        }

        auto oracle_decisions = CoreOpsSchedulerOracle::get_oracle_decisions(*this);

        for (const auto &run_params : oracle_decisions) {
            auto status = switch_core_op(run_params.core_op_handle, run_params.device_id);
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                continue;
            }

            if (HAILO_SUCCESS != status) {
                if (m_is_running) {
                    LOGGER__ERROR("Scheduler thread failed with status={}", status);
                }
                break;
            }
        }
    }
}

} /* namespace hailort */