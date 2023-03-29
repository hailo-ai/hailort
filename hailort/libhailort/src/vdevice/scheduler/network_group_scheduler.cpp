/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * TODO: Rename in a different PR
 * @file network_group_scheduler.cpp
 * @brief: Network scheduler
 **/

#include "common/os_utils.hpp"


#include "vdevice/scheduler/network_group_scheduler.hpp"
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
CoreOpsScheduler::CoreOpsScheduler(hailo_scheduling_algorithm_t algorithm, uint32_t device_count, std::vector<std::string> &devices_bdf_id, 
    std::vector<std::string> &devices_arch) :
    SchedulerBase(algorithm, device_count, devices_bdf_id, devices_arch),
    m_changing_current_batch_size(),
    m_should_core_op_stop(),
    m_before_read_write_mutex(),
    m_core_ops_cvs(),
    m_should_monitor(false)
#if defined(__GNUC__)
    , m_mon_tmp_output()
#endif
{
    // TODO: HRT-7391 - Change scheduler monitor to work only when MON command is active
    m_should_monitor = SchedulerMon::should_monitor();
    if (m_should_monitor) {
        auto status = start_mon();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to initiate hailo monitor of networks, with status {}", status);
        }
    }
}

CoreOpsScheduler::~CoreOpsScheduler()
{
    for (auto device_info : m_devices) {
        if (INVALID_CORE_OP_HANDLE != device_info->current_core_op_handle) {
            auto current_core_op = m_scheduled_core_ops[device_info->current_core_op_handle]->get_core_op();
            auto current_core_op_bundle = std::dynamic_pointer_cast<VDeviceCoreOp>(current_core_op);
            assert(nullptr != current_core_op_bundle);
            auto vdma_core_op = current_core_op_bundle->get_core_op_by_device_index(device_info->device_id);
            if (!vdma_core_op) {
                LOGGER__ERROR("Error retrieving core-op in scheduler destructor");
            } else {
                static const auto RESUME_PENDING_STREAM_TRANSFERS = true;
                if (HAILO_SUCCESS != VdmaConfigManager::switch_core_op(vdma_core_op.value(), nullptr, 0,
                        RESUME_PENDING_STREAM_TRANSFERS)) {
                    LOGGER__ERROR("Error deactivating core-op when destroying scheduler");
                }
            }
        }
    }

    if (m_should_monitor) {
        m_should_monitor = false;
        m_mon_shutdown_event->signal();
        if (m_mon_thread.joinable()) {
            m_mon_thread.join();
        }
    }
}

Expected<CoreOpsSchedulerPtr> CoreOpsScheduler::create_round_robin(uint32_t device_count, std::vector<std::string> &devices_bdf_id, std::vector<std::string> &devices_arch)
{
    auto ptr = make_shared_nothrow<CoreOpsScheduler>(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN, device_count, devices_bdf_id, devices_arch);
    CHECK_AS_EXPECTED(nullptr != ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

std::string get_curr_pid_as_str()
{
    return std::to_string(OsUtils::get_curr_pid());
}

hailo_status CoreOpsScheduler::start_mon()
{
#if defined(__GNUC__)
    m_last_measured_timestamp = std::chrono::steady_clock::now();
    m_mon_shutdown_event = Event::create_shared(Event::State::not_signalled);
    CHECK(nullptr != m_mon_shutdown_event, HAILO_OUT_OF_HOST_MEMORY);
    auto device_count = get_device_count();
    for (uint32_t i = 0; i < device_count; i++) {
        m_last_measured_utilization_timestamp[i] = {};
        m_device_has_drained_everything[i] = true;
        m_device_utilization[i] =  0;
    }

    auto tmp_file = open_temp_mon_file();
    CHECK_EXPECTED_AS_STATUS(tmp_file);
    m_mon_tmp_output = tmp_file.release();

    m_mon_thread = std::thread([this] ()
    {
        while (m_should_monitor) {
            auto status = m_mon_shutdown_event->wait(DEFAULT_SCHEDULER_MON_INTERVAL);
            if (HAILO_TIMEOUT == status) {
                dump_state();
            } else if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Scheduler monitor failed with status {}", status);
                return;
            }
        }
        return;
    });

    return HAILO_SUCCESS;
#else
    return HAILO_NOT_IMPLEMENTED;
#endif
}

#if defined(__GNUC__)
Expected<std::shared_ptr<TempFile>> CoreOpsScheduler::open_temp_mon_file()
{
    std::string file_name = get_curr_pid_as_str();
    auto tmp_file = TempFile::create(file_name, SCHEDULER_MON_TMP_DIR);
    CHECK_EXPECTED(tmp_file);
    
    auto tmp_file_ptr = make_shared_nothrow<TempFile>(tmp_file.release());
    CHECK_AS_EXPECTED(nullptr != tmp_file_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return tmp_file_ptr;
}

void CoreOpsScheduler::dump_state()
{
    auto file = LockedFile::create(m_mon_tmp_output->name(), "w");
    if (HAILO_SUCCESS != file.status()) {
        LOGGER__ERROR("Failed to open and lock file {}, with status: {}", m_mon_tmp_output->name(), file.status());
        return;
    }

    ProtoMon mon;
    mon.set_pid(get_curr_pid_as_str());
    time_dependent_events_cycle_calc();
    log_monitor_networks_infos(mon);
    log_monitor_device_infos(mon);
    log_monitor_frames_infos(mon);

    // Clear accumulators
    for (auto &handle_core_op_utilization_pair : m_core_op_utilization) {
        handle_core_op_utilization_pair.second = 0;
    }
    for (auto &handle_fps_pair : m_fps_accumulator) {
        handle_fps_pair.second = 0;
    }
    for (auto &handle_device_utilization_pair: m_device_utilization) {
        handle_device_utilization_pair.second = 0;
    }

    if (!mon.SerializeToFileDescriptor(file->get_fd())) {
        LOGGER__ERROR("Failed to SerializeToFileDescriptor(), with errno: {}", errno);
    }
}
#endif

std::string CoreOpsScheduler::get_core_op_name(const scheduler_core_op_handle_t &core_op_handle)
{
    assert(m_scheduled_core_ops.size() > core_op_handle);
    return m_scheduled_core_ops[core_op_handle]->get_core_op_name();
}

// TODO: HRT-9804 - Change monitor to use the tracer design mechanism (curently this functions uses private members)
void CoreOpsScheduler::time_dependent_events_cycle_calc()
{
    auto curr_time = std::chrono::steady_clock::now();
    m_last_measured_time_duration = std::chrono::duration_cast<std::chrono::duration<double>>(curr_time - m_last_measured_timestamp).count();

    for (auto device_info : m_devices) {
        if (!m_device_has_drained_everything[device_info->device_id]) {
            update_utilization_read_buffers_finished(device_info->device_id, device_info->current_core_op_handle, false);
        }
    }

    m_last_measured_timestamp = curr_time;
}

void CoreOpsScheduler::log_monitor_device_infos(ProtoMon &mon)
{
    for (auto device_info : m_devices) {
        assert(contains(m_device_utilization, device_info->device_id));
        auto curr_device_utilization = m_device_utilization[device_info->device_id];
        auto utilization_precentage = ((curr_device_utilization * 100) /  m_last_measured_time_duration);

        auto device_infos = mon.add_device_infos();
        device_infos->set_device_id(device_info->device_bdf_id);
        device_infos->set_utilization(utilization_precentage);
        device_infos->set_device_arch(device_info->device_arch);
    }
}

void CoreOpsScheduler::log_monitor_networks_infos(ProtoMon &mon)
{
    for (uint32_t core_op_handle = 0; core_op_handle < m_core_op_utilization.size(); core_op_handle++) {
        assert(contains(m_core_op_utilization, core_op_handle));
        auto curr_core_op_utilization = m_core_op_utilization[core_op_handle];
        auto utilization = ((curr_core_op_utilization * 100) /  m_last_measured_time_duration);
        auto outputs_count = static_cast<uint32_t>(m_scheduled_core_ops[core_op_handle]->get_outputs_names().size());
        auto fps = static_cast<double>((m_fps_accumulator[core_op_handle] / outputs_count) / m_last_measured_time_duration);

        auto net_info = mon.add_networks_infos();
        net_info->set_network_name(get_core_op_name(core_op_handle));
        net_info->set_utilization(utilization);
        net_info->set_fps(fps);
    }
}

void CoreOpsScheduler::log_monitor_frames_infos(ProtoMon &mon)
{
    for (uint32_t core_op_handle = 0; core_op_handle < m_scheduled_core_ops.size(); core_op_handle++) {
        auto net_frames_info = mon.add_net_frames_infos();
        net_frames_info->set_network_name(get_core_op_name(core_op_handle));

        for (auto &stream_name : m_scheduled_core_ops[core_op_handle]->get_inputs_names()) {
            auto stream_frames_info = net_frames_info->add_streams_frames_infos();
            stream_frames_info->set_stream_name(stream_name);
            stream_frames_info->set_stream_direction(PROTO__STREAM_DIRECTION__HOST_TO_DEVICE);
            auto status = set_h2d_frames_counters(core_op_handle, stream_name, *stream_frames_info);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to set stream's {} frames count, status = {}", stream_name, status);
                continue;
            }
        }
        
        for (auto &stream_name : m_scheduled_core_ops[core_op_handle]->get_outputs_names()) {
            auto stream_frames_info = net_frames_info->add_streams_frames_infos();
            stream_frames_info->set_stream_name(stream_name);
            stream_frames_info->set_stream_direction(PROTO__STREAM_DIRECTION__DEVICE_TO_HOST);
            auto status = set_d2h_frames_counters(core_op_handle, stream_name, *stream_frames_info);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to set stream's {} frames count, status = {}", stream_name, status);
                continue;
            }
        }
    }
}

hailo_status CoreOpsScheduler::set_h2d_frames_counters(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
    ProtoMonStreamFramesInfo &stream_frames_info)
{
    assert(m_scheduled_core_ops.size() > core_op_handle);
    auto current_cng = m_scheduled_core_ops[core_op_handle]->get_core_op();

    auto input_stream = current_cng->get_input_stream_by_name(stream_name);
    CHECK_EXPECTED_AS_STATUS(input_stream);

    InputStreamBase &vdevice_input = static_cast<InputStreamBase&>(input_stream->get());
    auto buffer_frames_size = vdevice_input.get_buffer_frames_size();
    if (HAILO_SUCCESS == buffer_frames_size.status()) {
        stream_frames_info.set_buffer_frames_size(static_cast<int32_t>(buffer_frames_size.value()));
    } else {
        stream_frames_info.set_buffer_frames_size(SCHEDULER_MON_NAN_VAL);
    }
    
    auto pending_frames_count = vdevice_input.get_pending_frames_count();
    if (HAILO_SUCCESS == pending_frames_count.status()) {
        stream_frames_info.set_pending_frames_count(static_cast<int32_t>(pending_frames_count.value()));
    } else {
        stream_frames_info.set_pending_frames_count(SCHEDULER_MON_NAN_VAL);
    }

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::set_d2h_frames_counters(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
    ProtoMonStreamFramesInfo &stream_frames_info)
{
    assert(m_scheduled_core_ops.size() > core_op_handle);
    auto current_cng = m_scheduled_core_ops[core_op_handle]->get_core_op();

    auto output_stream = current_cng->get_output_stream_by_name(stream_name);
    CHECK_EXPECTED_AS_STATUS(output_stream);

    OutputStreamBase &vdevice_output = static_cast<OutputStreamBase&>(output_stream->get());
    auto buffer_frames_size = vdevice_output.get_buffer_frames_size();
    if (HAILO_SUCCESS == buffer_frames_size.status()) {
        stream_frames_info.set_buffer_frames_size(static_cast<int32_t>(buffer_frames_size.value()));
    } else {
        stream_frames_info.set_buffer_frames_size(SCHEDULER_MON_NAN_VAL);
    }

    auto pending_frames_count = vdevice_output.get_pending_frames_count();
    if (HAILO_SUCCESS == pending_frames_count.status()) {
        stream_frames_info.set_pending_frames_count(static_cast<int32_t>(pending_frames_count.value()));
    } else {
        stream_frames_info.set_pending_frames_count(SCHEDULER_MON_NAN_VAL);
    }

    return HAILO_SUCCESS;
}

Expected<scheduler_core_op_handle_t > CoreOpsScheduler::add_core_op(std::shared_ptr<CoreOp> added_cng)
{
    scheduler_core_op_handle_t core_op_handle = INVALID_CORE_OP_HANDLE;
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        core_op_handle = static_cast<uint32_t>(m_scheduled_core_ops.size());
        TRACE(AddCoreOpTrace, "", added_cng->name(), DEFAULT_SCHEDULER_TIMEOUT.count(), DEFAULT_SCHEDULER_MIN_THRESHOLD, core_op_handle);

        auto stream_infos = added_cng->get_all_stream_infos();
        CHECK_EXPECTED(stream_infos);

        auto scheduled_core_op = ScheduledCoreOp::create(added_cng, stream_infos.value());
        CHECK_EXPECTED(scheduled_core_op);

        m_scheduled_core_ops.emplace_back(scheduled_core_op.release());

        m_changing_current_batch_size[core_op_handle] = false;

        for (const auto &stream_info : stream_infos.value()) {
            m_should_core_op_stop[core_op_handle][stream_info.name] = false;
        }

        for (auto& device_info : m_devices) {
            for (const auto &stream_info : stream_infos.value()) {
                if (HAILO_H2D_STREAM == stream_info.direction) {
                    device_info->current_cycle_requested_transferred_frames_h2d[core_op_handle][stream_info.name] = 0;
                } else {
                    device_info->current_cycle_finished_transferred_frames_d2h[core_op_handle][stream_info.name] = 0;
                    device_info->current_cycle_finished_read_frames_d2h[core_op_handle][stream_info.name] = 0;
                }
            }
        }

        // Monitor members
        m_core_op_utilization[core_op_handle] = 0;
        m_fps_accumulator[core_op_handle] = 0;

        auto network_cvs = ScheduledCoreOpCV::create(added_cng);
        CHECK_EXPECTED(network_cvs);
        m_core_ops_cvs[core_op_handle] = network_cvs.release();
        m_core_op_priority[HAILO_SCHEDULER_PRIORITY_NORMAL].emplace_back(core_op_handle);
    }

    return core_op_handle;
}

bool CoreOpsScheduler::is_core_op_active(const scheduler_core_op_handle_t &core_op_handle)
{
    for (auto device_info : m_devices) {
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

hailo_status CoreOpsScheduler::wait_for_write(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
    const std::chrono::milliseconds &timeout, const std::function<bool()> &should_cancel)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        hailo_status status = HAILO_SUCCESS;
        auto wait_res = m_core_ops_cvs[core_op_handle]->wait_for(stream_name, lock, timeout, [this, core_op_handle, stream_name, &should_cancel, &status] {

            if (should_cancel()) {
                status = HAILO_STREAM_ABORTED_BY_USER;
                return true; // return true so that the wait will finish
            }

            if (should_core_op_stop(core_op_handle)) {
                status = HAILO_STREAM_ABORTED_BY_USER;
                return true; // return true so that the wait will finish
            }

            return m_scheduled_core_ops[core_op_handle]->can_stream_write(stream_name);
        });
        CHECK(wait_res, HAILO_TIMEOUT, "{} (H2D) failed with status={}, timeout={}ms", stream_name, HAILO_TIMEOUT, timeout.count());
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            return status;
        }
        CHECK_SUCCESS(status);

        m_scheduled_core_ops[core_op_handle]->mark_frame_sent();
        m_scheduled_core_ops[core_op_handle]->requested_write_frames().increase(stream_name);
    }

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::signal_write_finish(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
    bool did_write_fail)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        assert(m_scheduled_core_ops.size() > core_op_handle);
        auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];

        if (did_write_fail) {
            scheduled_core_op->requested_write_frames().decrease(stream_name);
            return HAILO_SUCCESS;
        }

        if (should_core_op_stop(core_op_handle)) {
            return HAILO_STREAM_ABORTED_BY_USER;
        }

        scheduled_core_op->finished_write_frames().increase(stream_name);
        scheduled_core_op->requested_write_frames().decrease(stream_name);

        auto device_id = CoreOpsSchedulerOracle::get_avail_device(*this, core_op_handle);
        if (INVALID_DEVICE_ID != device_id) {
            auto status = switch_core_op(core_op_handle, device_id);
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("switch_core_op has failed with status=HAILO_STREAM_ABORTED_BY_USER");
                return status;
            }
            CHECK_SUCCESS(status);
        }

        auto status = optimize_streaming_if_enabled(core_op_handle);
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::switch_core_op(const scheduler_core_op_handle_t &core_op_handle, uint32_t device_id, bool /*keep_nn_config*/)
{
    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
    auto curr_device_info = m_devices[device_id];

    // initialize current cycle maps
    for (const auto &name : scheduled_core_op->get_inputs_names()) {
        curr_device_info->current_cycle_requested_transferred_frames_h2d[core_op_handle][name] = 0;
    }

    for (const auto &name : scheduled_core_op->get_outputs_names()) {
        curr_device_info->current_cycle_finished_transferred_frames_d2h[core_op_handle][name] = 0;
        curr_device_info->current_cycle_finished_read_frames_d2h[core_op_handle][name] = 0;
    }

    uint16_t batch_size = SINGLE_CONTEXT_BATCH_SIZE;
    uint16_t burst_size = static_cast<uint16_t>(scheduled_core_op->finished_write_frames_min_value());
    // In multi device finished write frame could be bigger then the vdma buffers we have, can be removed after dynamic desc binding.
    if (is_multi_device()) {
        burst_size = std::min(burst_size, get_min_avail_buffers_count(core_op_handle, device_id));
        // We limit the max burst size to (dev_count * max_batch) to keep former behavior (this was the buffer_pool size)
        // TODO: remove this limitation and work with user-controlled max_burst_size
        burst_size = std::min(burst_size, static_cast<uint16_t>(scheduled_core_op->get_max_batch_size() * get_device_count()));
    }

    if (scheduled_core_op->use_dynamic_batch_flow()) {
        batch_size = std::min(static_cast<uint16_t>(scheduled_core_op->finished_write_frames_min_value()), scheduled_core_op->get_max_batch_size());
        burst_size = batch_size;
    }

    bool has_same_batch_size_as_previous = (curr_device_info->current_batch_size == batch_size);
    curr_device_info->current_batch_size = batch_size;

    if (curr_device_info->current_core_op_handle != core_op_handle) {
        curr_device_info->is_switching_core_op = false;
    }

    if ((core_op_handle != curr_device_info->current_core_op_handle) || (!has_same_batch_size_as_previous)) {
        assert(m_scheduled_core_ops.size() > core_op_handle);
        auto next_active_cng = scheduled_core_op->get_core_op();
        auto next_active_cng_wrapper = std::dynamic_pointer_cast<VDeviceCoreOp>(next_active_cng);
        assert(nullptr != next_active_cng_wrapper);
        auto next_active_cng_expected = next_active_cng_wrapper->get_core_op_by_device_index(curr_device_info->device_id);
        CHECK_EXPECTED_AS_STATUS(next_active_cng_expected);

        std::shared_ptr<VdmaConfigCoreOp> current_active_vdma_cng = nullptr;
        if (curr_device_info->current_core_op_handle != INVALID_CORE_OP_HANDLE) {
            auto current_active_cng = m_scheduled_core_ops[curr_device_info->current_core_op_handle]->get_core_op();
            auto current_active_cng_bundle = std::dynamic_pointer_cast<VDeviceCoreOp>(current_active_cng);
            assert(nullptr != current_active_cng_bundle);
            auto current_active_cng_expected = current_active_cng_bundle->get_core_op_by_device_index(curr_device_info->device_id);
            CHECK_EXPECTED_AS_STATUS(current_active_cng_expected);
            current_active_vdma_cng = current_active_cng_expected.release();
        }

        TRACE(SwitchCoreOpTrace, "", core_op_handle);
        static const auto RESUME_PENDING_STREAM_TRANSFERS = true;
        auto status = VdmaConfigManager::switch_core_op(current_active_vdma_cng, next_active_cng_expected.value(), batch_size,

            RESUME_PENDING_STREAM_TRANSFERS);
        CHECK_SUCCESS(status, "Failed switching core-op");
        // Clear the ready_to_switch flag from old activation
        scheduled_core_op->mark_unready_to_switch();

        // Register to get interrupts - has to be after core-op is activated
        for (auto &output_stream : next_active_cng_expected.value()->get_output_streams()) {
            OutputStreamBase &vdevice_output = static_cast<OutputStreamBase&>(output_stream.get());
            status = vdevice_output.register_interrupt_callback(
                [this, name = output_stream.get().name(), format = vdevice_output.get_layer_info().format.order, scheduled_core_op, core_op_handle, device_id]
                    (uint32_t frames) {
                        auto should_notify_next = false;
                        {
                            std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
                            // In order to meet performance requirement we enable switch only after first frame is arrived.
                            // TODO: remove this hack / move it to oracle and add another scheduling algorithm for it
                            scheduled_core_op->mark_ready_to_switch();
                            if (hailo_format_order_t::HAILO_FORMAT_ORDER_HAILO_NMS != format) {
                                TRACE(OutputVdmaEnqueueTrace, "", core_op_handle, name, frames);
                                // TODO: Remove d2h_finished_transferred_frames and use current_cycle_finished_transferred_frames_d2h instead
                                scheduled_core_op->d2h_finished_transferred_frames(name) += frames;
                                m_devices[device_id]->current_cycle_finished_transferred_frames_d2h[core_op_handle][name] += frames;
                            }

                            auto has_drained_everything = has_core_op_drained_everything(core_op_handle, device_id);

                            if (m_should_monitor) {
                                update_utilization_read_buffers_finished(device_id, core_op_handle, has_drained_everything);
                            }

                            // If ng finished and we didnt choose next lets choose without checking threshold
                            if (!m_devices[device_id]->is_switching_core_op && has_drained_everything) {
                                auto was_chosen  = choose_next_core_op(device_id, true);
                                if (!was_chosen) {
                                    choose_next_core_op(device_id, false);
                                }
                            }

                            if (m_devices[device_id]->is_switching_core_op && has_drained_everything) {
                                should_notify_next = true;
                            }
                        }
                        // Notify stream that new frame was accepted (wait_for_read)
                        m_core_ops_cvs[core_op_handle]->notify_one(name);
                        if (should_notify_next) {
                            auto next_core_op = m_devices[device_id]->next_core_op_handle;
                            // Notify all the threads of the next ng (wait_for_read)
                            m_core_ops_cvs[next_core_op]->notify_all();
                        }
                });
            CHECK_SUCCESS(status);
        }
    }

    scheduled_core_op->set_last_run_timestamp(std::chrono::steady_clock::now()); // Mark timestamp on activation
    curr_device_info->current_core_op_handle = core_op_handle;

    // Finished switching batch size
    m_changing_current_batch_size[core_op_handle] = false;

    auto status = send_all_pending_buffers(core_op_handle, device_id, burst_size);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("send_all_pending_buffers has failed with status=HAILO_STREAM_ABORTED_BY_USER");
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::send_all_pending_buffers(const scheduler_core_op_handle_t &core_op_handle, uint32_t device_id, uint32_t burst_size)
{
    auto current_device_info = m_devices[device_id];
    if ((INVALID_CORE_OP_HANDLE == current_device_info->current_core_op_handle) || (current_device_info->current_core_op_handle != core_op_handle)) {
        return HAILO_SUCCESS;
    }

    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];

    for (size_t i = 0; i < burst_size; i++) {
        auto finished_send = false;
        for (const auto &name : scheduled_core_op->get_inputs_names()) {
            if (scheduled_core_op->finished_write_frames(name) == 0) {
                finished_send = true;
                break;
            }
        }
        if (finished_send) {
            break;
        }
        for (const auto &name : scheduled_core_op->get_inputs_names()) {
            auto status = send_pending_buffer(core_op_handle, name, device_id);
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("send_pending_buffer has failed with status=HAILO_STREAM_ABORTED_BY_USER");
                return status;
            }
            CHECK_SUCCESS(status);
        }
        scheduled_core_op->push_device_index(device_id);
        scheduled_core_op->set_last_device_index(device_id);

        if (m_should_monitor) {
            update_utilization_send_started(device_id);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status CoreOpsScheduler::send_pending_buffer(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
    uint32_t device_id)
{
    assert(m_scheduled_core_ops.size() > core_op_handle);
    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];

    auto current_cng = scheduled_core_op->get_core_op();
    auto input_stream = current_cng->get_input_stream_by_name(stream_name);
    CHECK_EXPECTED_AS_STATUS(input_stream);

    VDeviceInputStreamMultiplexerWrapper &vdevice_input = static_cast<VDeviceInputStreamMultiplexerWrapper&>(input_stream->get());
    TRACE(InputVdmaDequeueTrace, "", core_op_handle, stream_name);
    auto status = vdevice_input.send_pending_buffer(device_id);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("send_pending_buffer has failed with status=HAILO_STREAM_ABORTED_BY_USER");
        return status;
    }
    CHECK_SUCCESS(status);

    m_devices[device_id]->current_cycle_requested_transferred_frames_h2d[core_op_handle][stream_name]++;
    scheduled_core_op->finished_write_frames().decrease(stream_name);

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
    bool has_pending_writes = scheduled_core_op->finished_write_frames_min_value() > 0;

    // Check if there arent any read requests
    bool has_pending_user_reads = false;
    for (const auto &name : scheduled_core_op->get_outputs_names()) {
        if (scheduled_core_op->requested_read_frames(name) > 0) {
            has_pending_user_reads = true;
            break;
        }
    }

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
            auto write_requests = scheduled_core_op->requested_write_frames(name) + scheduled_core_op->finished_write_frames(name);
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
    }

    result.threshold = std::all_of(over_threshold.begin(), over_threshold.end(), [](auto over) { return over; });
    result.timeout = std::all_of(over_timeout.begin(), over_timeout.end(), [](auto over) { return over; });
    result.is_ready = has_pending_writes && has_pending_user_reads;

    return result;
}

Expected<uint32_t> CoreOpsScheduler::wait_for_read(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name,
    const std::chrono::milliseconds &timeout)
{
    std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];
    scheduled_core_op->requested_read_frames().increase(stream_name);

    hailo_status status = HAILO_SUCCESS;
    auto wait_res = m_core_ops_cvs[core_op_handle]->wait_for(stream_name, lock, timeout, [this, core_op_handle, scheduled_core_op, stream_name, &status] {

        if (should_core_op_stop(core_op_handle)) {
            status = HAILO_STREAM_ABORTED_BY_USER;
            return true; // return true so that the wait will finish
        }

        auto device_id = CoreOpsSchedulerOracle::get_avail_device(*this, core_op_handle);
        if (INVALID_DEVICE_ID != device_id) {
            status = switch_core_op(core_op_handle, device_id);
            if (HAILO_SUCCESS != status) {
                return true; // return true so that the wait will finish
            }
        }

        return scheduled_core_op->can_stream_read(stream_name);
    });
    CHECK_AS_EXPECTED(wait_res, HAILO_TIMEOUT, "{} (D2H) failed with status={}, timeout={}ms", stream_name, HAILO_TIMEOUT, timeout.count());
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        return make_unexpected(status);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    scheduled_core_op->requested_read_frames().decrease(stream_name);

    return scheduled_core_op->pop_device_index(stream_name);
}

hailo_status CoreOpsScheduler::signal_read_finish(const scheduler_core_op_handle_t &core_op_handle, const std::string &stream_name, uint32_t device_id)
{
    auto should_notify_next = false;
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];

        scheduled_core_op->finished_read_frames().increase(stream_name);
        m_devices[device_id]->current_cycle_finished_read_frames_d2h[core_op_handle][stream_name]++;
        scheduled_core_op->d2h_finished_transferred_frames().decrease(stream_name);
        m_fps_accumulator[core_op_handle]++;

        decrease_core_op_counters(core_op_handle);

        auto has_drained_everything = has_core_op_drained_everything(core_op_handle, device_id);
        if (scheduled_core_op->is_nms() && has_drained_everything) {
            // In NMS networks there is possibility that next wasn't choosen yet
            choose_next_core_op(device_id, true);

            // If we didnt choose with treshold or timeout lets choose without treshold
            if (!m_devices[device_id]->is_switching_core_op) {
                choose_next_core_op(device_id, false);
            }

            if (m_devices[device_id]->is_switching_core_op) {
                should_notify_next = true;
            }

            if (m_should_monitor) {
                update_utilization_read_buffers_finished(device_id, core_op_handle, has_drained_everything);
            }
        }
    }

    // Notify stream that frame was read and we have a space in the read buffers (wait_for_write)
    m_core_ops_cvs[core_op_handle]->notify_all();

    if (should_notify_next) {
        // Notify all the threads of the next ng, for nms networks this is the only place we know the network was finished (wait_for_read)
        m_core_ops_cvs[m_devices[device_id]->next_core_op_handle]->notify_all();
    }

    return HAILO_SUCCESS;
}

void CoreOpsScheduler::decrease_core_op_counters(const scheduler_core_op_handle_t &core_op_handle)
{
    return m_scheduled_core_ops[core_op_handle]->decrease_current_core_op_counters();
}

bool CoreOpsScheduler::has_core_op_drained_everything(const scheduler_core_op_handle_t &core_op_handle, uint32_t device_id)
{
    if (INVALID_CORE_OP_HANDLE == core_op_handle) {
        // If no core-op is running, consider it as drained
        return true;
    }

    if (core_op_all_streams_aborted(core_op_handle)) {
        // We treat core-op as drained only if all streams are aborted - to make sure there aren't any ongoing transfers
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

bool CoreOpsScheduler::choose_next_core_op(size_t device_id, bool check_threshold)
{
    if (!m_devices[device_id]->is_switching_core_op) {
        return CoreOpsSchedulerOracle::choose_next_model(*this, m_devices[device_id]->device_id, check_threshold);
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

    if ((!scheduled_core_op->use_dynamic_batch_flow()) && !(scheduled_core_op->is_ready_to_switch() &&
        CoreOpsSchedulerOracle::should_stop_streaming(*this, scheduled_core_op->get_priority()))) {
        for (uint32_t i = 0; i < m_devices.size(); i++) {
            uint32_t index = scheduled_core_op->get_last_device_index() + i + 1;
            index %= static_cast<uint32_t>(m_devices.size());
            auto device_info = m_devices[index];
            // If multi device check for space in the vdma buffers, the send pending buffer is waitable in the current implementation.
            // can be removed after dynamic descriptor binding support
            if (device_info->current_core_op_handle == core_op_handle &&
                (!is_multi_device() || (get_min_avail_buffers_count(core_op_handle, device_info->device_id) >= DEFAULT_BURST_SIZE))) {
                auto status = send_all_pending_buffers(core_op_handle, device_info->device_id, DEFAULT_BURST_SIZE);
                if (HAILO_STREAM_ABORTED_BY_USER == status) {
                    LOGGER__INFO("send_all_pending_buffers has failed with status=HAILO_STREAM_ABORTED_BY_USER");
                    return status;
                }
                CHECK_SUCCESS(status);
            }
        }
    }

    return HAILO_SUCCESS;
}

uint16_t CoreOpsScheduler::get_min_avail_buffers_count(const scheduler_core_op_handle_t &core_op_handle, uint32_t device_id)
{
    auto device_info = m_devices[device_id];
    auto scheduled_core_op = m_scheduled_core_ops[core_op_handle];

    auto max_transferred_h2d = get_max_value_of_unordered_map(device_info->current_cycle_requested_transferred_frames_h2d[core_op_handle]);
    auto min_d2h_frames = scheduled_core_op->is_nms() ? get_min_value_of_unordered_map(device_info->current_cycle_finished_read_frames_d2h[core_op_handle]) :
        get_min_value_of_unordered_map(device_info->current_cycle_finished_transferred_frames_d2h[core_op_handle]);
    auto ongoing_frames = static_cast<uint16_t>(max_transferred_h2d - min_d2h_frames);

    uint16_t avail_buffers = static_cast<uint16_t>(scheduled_core_op->get_min_input_buffers_count(get_device_count()) - ongoing_frames);

    return avail_buffers;
}

void CoreOpsScheduler::update_utilization_timers(scheduler_device_idx_t device_id, scheduler_core_op_handle_t core_op_handle)
{
    assert(contains(m_core_op_utilization, core_op_handle));

    auto time_diff = std::chrono::duration_cast<std::chrono::duration<double>>(
        std::chrono::steady_clock::now() - m_last_measured_utilization_timestamp[device_id]).count();

    m_device_utilization[device_id] += time_diff;
    m_core_op_utilization[core_op_handle] += time_diff;
}

void CoreOpsScheduler::update_utilization_timestamp(scheduler_device_idx_t device_id)
{
    m_last_measured_utilization_timestamp[device_id] = std::chrono::steady_clock::now();
}

void CoreOpsScheduler::update_utilization_send_started(scheduler_device_idx_t device_id)
{
    if (m_device_has_drained_everything[device_id]) {
        update_device_drained_state(device_id, false);
        update_utilization_timestamp(device_id);
    }
}

void CoreOpsScheduler::update_device_drained_state(scheduler_device_idx_t device_id, bool state)
{
    m_device_has_drained_everything[device_id] = state;
}

void CoreOpsScheduler::update_utilization_read_buffers_finished(scheduler_device_idx_t device_id, 
    scheduler_core_op_handle_t core_op_handle, bool is_drained_everything)
{
    update_utilization_timers(device_id, core_op_handle);
    update_device_drained_state(device_id, is_drained_everything);
    if (!is_drained_everything) {
        update_utilization_timestamp(device_id);
    }
}

} /* namespace hailort */