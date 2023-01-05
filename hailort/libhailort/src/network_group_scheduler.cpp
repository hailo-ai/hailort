/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group_scheduler.cpp
 * @brief: Network scheduler
 **/

#include "network_group_scheduler.hpp"
#include "context_switch/network_group_internal.hpp"
#include "context_switch/vdevice_network_group.hpp"
#include "hef_internal.hpp"
#include "vdevice_stream_multiplexer_wrapper.hpp"
#include "tracer_macros.hpp"
#include "scheduler_oracle.hpp"

#include <fstream>

namespace hailort
{

#define SINGLE_CONTEXT_BATCH_SIZE (1)

// TODO: use device handles instead device count
NetworkGroupScheduler::NetworkGroupScheduler(hailo_scheduling_algorithm_t algorithm, uint32_t device_count) :
    m_changing_current_batch_size(),
    m_should_ng_stop(),
    m_algorithm(algorithm),
    m_before_read_write_mutex(),
    m_write_read_cv(),
    m_should_monitor(false)
#if defined(__GNUC__)
    , m_mon_tmp_output()
#endif
{
    for (uint32_t i = 0; i < device_count; i++) {
        m_devices.push_back(make_shared_nothrow<ActiveDeviceInfo>(i));
    }

    // TODO: HRT-7391 - Change scheduler monitor to work only when MON command is active
    m_should_monitor = SchedulerMon::should_monitor();
    if (m_should_monitor) {
        auto status = start_mon();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to initiate hailo monitor of networks, with status {}", status);
        }
    }
}

NetworkGroupScheduler::~NetworkGroupScheduler()
{
    for (auto device_info : m_devices) {
        if (INVALID_NETWORK_GROUP_HANDLE != device_info->current_network_group_handle) {
            auto current_ng = m_cngs[device_info->current_network_group_handle]->get_network_group();
            auto current_network_group_bundle = std::dynamic_pointer_cast<VDeviceNetworkGroup>(current_ng);
            assert(nullptr != current_network_group_bundle);
            auto vdma_network_group = current_network_group_bundle->get_network_group_by_device_index(device_info->device_id);
            if (!vdma_network_group) {
                LOGGER__ERROR("Error retrieving network group in scheduler destructor");
            } else {
                if (HAILO_SUCCESS != VdmaConfigManager::switch_network_group(vdma_network_group.value(), nullptr, 0)) {
                    LOGGER__ERROR("Error deactivating network group when destroying scheduler");
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

Expected<NetworkGroupSchedulerPtr> NetworkGroupScheduler::create_round_robin(uint32_t device_count)
{
    auto ptr = make_shared_nothrow<NetworkGroupScheduler>(HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN, device_count);
    CHECK_AS_EXPECTED(nullptr != ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

std::string get_curr_pid_as_str()
{
#ifdef _WIN32
    auto pid = GetCurrentProcessId();
#else
    auto pid = getpid();
#endif
    return std::to_string(pid);
}

hailo_status NetworkGroupScheduler::start_mon()
{
#if defined(__GNUC__)
    m_last_measured_timestamp = std::chrono::steady_clock::now();
    m_mon_shutdown_event = Event::create_shared(Event::State::not_signalled);
    CHECK(nullptr != m_mon_shutdown_event, HAILO_OUT_OF_HOST_MEMORY);

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
Expected<std::shared_ptr<TempFile>> NetworkGroupScheduler::open_temp_mon_file()
{
    std::string file_name = get_curr_pid_as_str();
    auto tmp_file = TempFile::create(file_name, SCHEDULER_MON_TMP_DIR);
    CHECK_EXPECTED(tmp_file);
    
    auto tmp_file_ptr = make_shared_nothrow<TempFile>(tmp_file.release());
    CHECK_AS_EXPECTED(nullptr != tmp_file_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return tmp_file_ptr;
}

void NetworkGroupScheduler::dump_state()
{
    auto file = LockedFile::create(m_mon_tmp_output->name(), "w");
    if (HAILO_SUCCESS != file.status()) {
        LOGGER__ERROR("Failed to open and lock file {}, with status: {}", m_mon_tmp_output->name(), file.status());
        return;
    }

    ProtoMon mon;
    mon.set_pid(get_curr_pid_as_str());
    log_monitor_networks_infos(mon);
    log_monitor_frames_infos(mon);

    // Clear accumulators
    for (auto &handle_duration_pair : m_active_duration) {
        handle_duration_pair.second = 0;
    }
    for (auto &handle_fps_pair : m_fps_accumulator) {
        handle_fps_pair.second = 0;
    }

    if (!mon.SerializeToFileDescriptor(file->get_fd())) {
        LOGGER__ERROR("Failed to SerializeToFileDescriptor(), with errno: {}", errno);
    }
}
#endif

std::string NetworkGroupScheduler::get_network_group_name(const scheduler_ng_handle_t &network_group_handle)
{
    return m_cngs[network_group_handle]->get_network_group_name();
}

// TODO: HRT-7392 - Reduce core percentage when scheduler is idle
void NetworkGroupScheduler::log_monitor_networks_infos(ProtoMon &mon)
{
    auto curr_time = std::chrono::steady_clock::now();
    const auto measurement_duration = std::chrono::duration_cast<std::chrono::duration<double>>(curr_time - m_last_measured_timestamp).count();

    for (uint32_t network_group_handle = 0; network_group_handle < m_last_measured_activation_timestamp.size(); network_group_handle++) {
        assert(contains(m_active_duration, network_group_handle));
        auto curr_ng_active_time = m_active_duration[network_group_handle];

        for (auto device_info : m_devices) {
            if (network_group_handle == device_info->current_network_group_handle) {
                // Network is currently active
                auto time_diff = std::chrono::duration_cast<std::chrono::duration<double>>(
                    curr_time - m_last_measured_activation_timestamp[device_info->current_network_group_handle]).count();
                curr_ng_active_time += time_diff;
                m_last_measured_activation_timestamp[device_info->current_network_group_handle] = curr_time;
            }
        }

        auto active_time = ((curr_ng_active_time * 100) /  measurement_duration);
        auto outputs_count = static_cast<uint32_t>(m_cngs[network_group_handle]->get_outputs_names().size());
        auto fps = static_cast<double>((m_fps_accumulator[network_group_handle] / outputs_count) / measurement_duration);

        auto net_info = mon.add_networks_infos();
        net_info->set_network_name(get_network_group_name(network_group_handle));
        net_info->set_active_time(active_time);
        net_info->set_fps(fps);
    }

    m_last_measured_timestamp = curr_time;
}

void NetworkGroupScheduler::log_monitor_frames_infos(ProtoMon &mon)
{
    for (uint32_t network_group_handle = 0; network_group_handle < m_cngs.size(); network_group_handle++) {
        auto net_frames_info = mon.add_net_frames_infos();
        net_frames_info->set_network_name(get_network_group_name(network_group_handle));

        for (auto &stream_name : m_cngs[network_group_handle]->get_inputs_names()) {
            auto stream_frames_info = net_frames_info->add_streams_frames_infos();
            stream_frames_info->set_stream_name(stream_name);
            stream_frames_info->set_stream_direction(PROTO__STREAM_DIRECTION__HOST_TO_DEVICE);
            auto status = set_h2d_frames_counters(network_group_handle, stream_name, *stream_frames_info);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to set stream's {} frames count, status = {}", stream_name, status);
                continue;
            }
        }
        
        for (auto &stream_name : m_cngs[network_group_handle]->get_outputs_names()) {
            auto stream_frames_info = net_frames_info->add_streams_frames_infos();
            stream_frames_info->set_stream_name(stream_name);
            stream_frames_info->set_stream_direction(PROTO__STREAM_DIRECTION__DEVICE_TO_HOST);
            auto status = set_d2h_frames_counters(network_group_handle, stream_name, *stream_frames_info);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to set stream's {} frames count, status = {}", stream_name, status);
                continue;
            }
        }
    }
}

hailo_status NetworkGroupScheduler::set_h2d_frames_counters(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
    ProtoMonStreamFramesInfo &stream_frames_info)
{
    assert(m_cngs.size() > network_group_handle);
    auto current_cng = m_cngs[network_group_handle]->get_network_group();

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

hailo_status NetworkGroupScheduler::set_d2h_frames_counters(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
    ProtoMonStreamFramesInfo &stream_frames_info)
{
    assert(m_cngs.size() > network_group_handle);
    auto current_cng = m_cngs[network_group_handle]->get_network_group();

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

Expected<scheduler_ng_handle_t> NetworkGroupScheduler::add_network_group(std::shared_ptr<ConfiguredNetworkGroup> added_cng)
{
    scheduler_ng_handle_t network_group_handle = INVALID_NETWORK_GROUP_HANDLE;
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        network_group_handle = static_cast<uint32_t>(m_cngs.size());
        TRACE(AddNetworkGroupTrace, "", added_cng->name(), DEFAULT_SCHEDULER_TIMEOUT.count(), DEFAULT_SCHEDULER_MIN_THRESHOLD, network_group_handle);

        auto stream_infos = added_cng->get_all_stream_infos();
        CHECK_EXPECTED(stream_infos);

        auto scheduled_ng = ScheduledNetworkGroup::create(added_cng, stream_infos.value());
        CHECK_EXPECTED(scheduled_ng);

        m_cngs.emplace_back(scheduled_ng.release());

        m_changing_current_batch_size[network_group_handle] = false;

        for (const auto &stream_info : stream_infos.value()) {
            m_should_ng_stop[network_group_handle][stream_info.name] = false;
        }

        for (auto& device_info : m_devices) {
            for (const auto &stream_info : stream_infos.value()) {
                if (HAILO_H2D_STREAM == stream_info.direction) {
                    device_info->current_cycle_requested_transferred_frames_h2d[network_group_handle][stream_info.name] = 0;
                } else {
                    device_info->current_cycle_finished_transferred_frames_d2h[network_group_handle][stream_info.name] = 0;
                    device_info->current_cycle_finished_read_frames_d2h[network_group_handle][stream_info.name] = 0;
                }
            }
        }

        // Monitor members
        m_last_measured_activation_timestamp[network_group_handle] = {};
        m_active_duration[network_group_handle] = 0;
        m_fps_accumulator[network_group_handle] = 0;
    }
    m_write_read_cv.notify_all();
    return network_group_handle;
}

bool NetworkGroupScheduler::is_network_group_active(const scheduler_ng_handle_t &network_group_handle)
{
    for (auto device_info : m_devices) {
        if (network_group_handle == device_info->current_network_group_handle) {
            return true;
        }
    }

    return false;
}

bool NetworkGroupScheduler::is_switching_current_network_group(const scheduler_ng_handle_t &network_group_handle)
{
    for (auto device_info : m_devices) {
        if (network_group_handle == device_info->current_network_group_handle && device_info->is_switching_network_group) {
            return true;
        }
    }

    return false;
}

bool NetworkGroupScheduler::is_multi_device()
{
    return m_devices.size() > 1;
}

hailo_status NetworkGroupScheduler::wait_for_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
    const std::chrono::milliseconds &timeout, const std::function<bool()> &should_cancel)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        hailo_status status = HAILO_SUCCESS;
        auto wait_res = m_write_read_cv.wait_for(lock, timeout, [this, network_group_handle, stream_name, &should_cancel, &status] {

            if (should_cancel()) {
                status = HAILO_STREAM_ABORTED_BY_USER;
                return true; // return true so that the wait will finish
            }

            if (should_ng_stop(network_group_handle)) {
                status = HAILO_STREAM_ABORTED_BY_USER;
                return true; // return true so that the wait will finish
            }

            auto should_wait = should_wait_for_write(network_group_handle, stream_name);
            if (HAILO_SUCCESS != should_wait.status()) {
                status = should_wait.status();
                return true; // return true so that the wait will finish
            }
            return !should_wait.value();
        });
        CHECK(wait_res, HAILO_TIMEOUT, "{} (H2D) failed with status={}, timeout={}ms", stream_name, HAILO_TIMEOUT, timeout.count());
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            return status;
        }
        CHECK_SUCCESS(status);

        m_cngs[network_group_handle]->mark_frame_sent();
        m_cngs[network_group_handle]->requested_write_frames().increase(stream_name);
    }
    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}

Expected<bool> NetworkGroupScheduler::should_wait_for_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    auto scheduled_ng = m_cngs[network_group_handle];

    if (should_ng_stop(network_group_handle)) {
        return make_unexpected(HAILO_STREAM_ABORTED_BY_USER);
    }

    auto pre_transfer_h2d_frames = scheduled_ng->requested_write_frames(stream_name) + scheduled_ng->finished_write_frames(stream_name);
    bool has_written_max_batch_size = ((scheduled_ng->use_dynamic_batch_flow() || is_multi_device()) &&
        ((scheduled_ng->get_max_batch_size() * m_devices.size()) == pre_transfer_h2d_frames));

    bool should_stop_writing_because_switching = ((!(scheduled_ng->use_dynamic_batch_flow() || is_multi_device())) &&
        (is_switching_current_network_group(network_group_handle) || m_changing_current_batch_size[network_group_handle]) &&
        is_network_group_active(network_group_handle) && scheduled_ng->has_input_written_most_frames(stream_name));

    auto total_written_frames = scheduled_ng->total_written_frames_count()[stream_name];
    auto min_finished_read = scheduled_ng->finished_read_frames_min_value();
    auto ongoing_frames = (min_finished_read < total_written_frames) ? (total_written_frames - min_finished_read) : 0;
    bool has_enough_space_for_writes = scheduled_ng->has_enough_space_in_read_buffers(ongoing_frames);

    if (has_written_max_batch_size || should_stop_writing_because_switching || (!has_enough_space_for_writes)) {
        return true;
    }

    return false;
}

hailo_status NetworkGroupScheduler::signal_write_finish(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        auto scheduled_ng = m_cngs[network_group_handle];

        if (should_ng_stop(network_group_handle)) {
            return HAILO_STREAM_ABORTED_BY_USER;
        }

        scheduled_ng->finished_write_frames().increase(stream_name);
        scheduled_ng->requested_write_frames().decrease(stream_name);

        auto device_id = NetworkGroupSchedulerOracle::get_avail_device(*this, network_group_handle);
        if (INVALID_DEVICE_ID != device_id) {
            auto status = switch_network_group(network_group_handle, device_id);
            CHECK_SUCCESS(status);
        }

        for (auto &device_info : m_devices) {
            if (device_info->current_network_group_handle == network_group_handle && !(scheduled_ng->use_dynamic_batch_flow() || is_multi_device())) {
                auto status = send_all_pending_buffers(network_group_handle, device_info->device_id);
                if (HAILO_STREAM_ABORTED_BY_USER == status) {
                    LOGGER__INFO("send_all_pending_buffers has failed with status=HAILO_STREAM_ABORTED_BY_USER");
                    return status;
                }
                CHECK_SUCCESS(status);
            }
        }
    }
    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::switch_network_group(const scheduler_ng_handle_t &network_group_handle, uint32_t device_id, bool /*keep_nn_config*/)
{
    auto scheduled_ng = m_cngs[network_group_handle];
    auto curr_device_info = m_devices[device_id];

    // initialize current cycle maps
    for (const auto &name : scheduled_ng->get_inputs_names()) {
        curr_device_info->current_cycle_requested_transferred_frames_h2d[network_group_handle][name] = 0;
    }

    for (const auto &name : scheduled_ng->get_outputs_names()) {
        curr_device_info->current_cycle_finished_transferred_frames_d2h[network_group_handle][name] = 0;
        curr_device_info->current_cycle_finished_read_frames_d2h[network_group_handle][name] = 0;
    }

    uint16_t batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE;
    uint16_t burst_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE;
    if (scheduled_ng->use_dynamic_batch_flow()) {
        burst_size = std::min(static_cast<uint16_t>(scheduled_ng->finished_write_frames_min_value()), scheduled_ng->get_max_batch_size());
        batch_size = burst_size;
    } else {
        burst_size = is_multi_device() ? static_cast<uint16_t>(scheduled_ng->finished_write_frames_min_value()) : SINGLE_CONTEXT_BATCH_SIZE;
        batch_size = SINGLE_CONTEXT_BATCH_SIZE;
    }

    bool has_same_batch_size_as_previous = (curr_device_info->current_batch_size == batch_size);
    curr_device_info->current_batch_size = batch_size;
    curr_device_info->current_burst_size = burst_size;

    m_last_measured_activation_timestamp[network_group_handle] = std::chrono::steady_clock::now();

    if (curr_device_info->current_network_group_handle != network_group_handle) {
        curr_device_info->is_switching_network_group = false;
    }

    if ((network_group_handle != curr_device_info->current_network_group_handle) || (!has_same_batch_size_as_previous)) {
        assert(m_cngs.size() > network_group_handle);
        auto next_active_cng = scheduled_ng->get_network_group();
        auto next_active_cng_wrapper = std::dynamic_pointer_cast<VDeviceNetworkGroup>(next_active_cng);
        assert(nullptr != next_active_cng_wrapper);
        auto next_active_cng_expected = next_active_cng_wrapper->get_network_group_by_device_index(curr_device_info->device_id);
        CHECK_EXPECTED_AS_STATUS(next_active_cng_expected);

        std::shared_ptr<VdmaConfigNetworkGroup> current_active_vdma_cng = nullptr;
        if (curr_device_info->current_network_group_handle != INVALID_NETWORK_GROUP_HANDLE) {
            reset_current_ng_timestamps(curr_device_info->device_id);
            auto current_active_cng = m_cngs[curr_device_info->current_network_group_handle]->get_network_group();
            auto current_active_cng_bundle = std::dynamic_pointer_cast<VDeviceNetworkGroup>(current_active_cng);
            assert(nullptr != current_active_cng_bundle);
            auto current_active_cng_expected = current_active_cng_bundle->get_network_group_by_device_index(curr_device_info->device_id);
            CHECK_EXPECTED_AS_STATUS(current_active_cng_expected);
            current_active_vdma_cng = current_active_cng_expected.release();
        }

        TRACE(SwitchNetworkGroupTrace, "", network_group_handle);
        auto status = VdmaConfigManager::switch_network_group(current_active_vdma_cng, next_active_cng_expected.value(), batch_size);
        CHECK_SUCCESS(status, "Failed switching network group");

        // Register to get interrupts - has to be after network group is activated
        for (auto &output_stream : next_active_cng_expected.value()->get_output_streams()) {
            OutputStreamBase &vdevice_output = static_cast<OutputStreamBase&>(output_stream.get());
            status = vdevice_output.register_for_d2h_interrupts(
                [this, name = output_stream.get().name(), format = vdevice_output.get_layer_info().format.order, scheduled_ng, network_group_handle, device_id]
                    (uint32_t frames) {
                        {
                            std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
                            if (hailo_format_order_t::HAILO_FORMAT_ORDER_HAILO_NMS != format) {
                                TRACE(OutputVdmaEnqueueTrace, "", network_group_handle, name, frames);
                                // TODO: Remove d2h_finished_transferred_frames and use current_cycle_finished_transferred_frames_d2h instead
                                scheduled_ng->d2h_finished_transferred_frames(name) += frames;
                                m_devices[device_id]->current_cycle_finished_transferred_frames_d2h[network_group_handle][name] += frames;
                            }
                            if (!(is_multi_device() || scheduled_ng->use_dynamic_batch_flow()) || has_ng_drained_everything(network_group_handle, device_id)) {
                                choose_next_network_group(device_id);
                            }
                        }
                        m_write_read_cv.notify_all();
                });
            CHECK_SUCCESS(status);
        }
    }

    scheduled_ng->set_last_run_timestamp(std::chrono::steady_clock::now()); // Mark timestamp on activation
    curr_device_info->current_network_group_handle = network_group_handle;

    // Finished switching batch size
    m_changing_current_batch_size[network_group_handle] = false;

    auto status = send_all_pending_buffers(network_group_handle, device_id);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("send_all_pending_buffers has failed with status=HAILO_STREAM_ABORTED_BY_USER");
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::send_all_pending_buffers(const scheduler_ng_handle_t &network_group_handle, uint32_t device_id)
{
    auto current_device_info = m_devices[device_id];
    if ((INVALID_NETWORK_GROUP_HANDLE == current_device_info->current_network_group_handle) || (current_device_info->current_network_group_handle != network_group_handle)) {
        return HAILO_SUCCESS;
    }

    auto scheduled_ng = m_cngs[network_group_handle];

    while(true) {
        auto finished_send = false;
        for (const auto &name : scheduled_ng->get_inputs_names()) {
            if ((scheduled_ng->finished_write_frames(name) == 0) || (((scheduled_ng->use_dynamic_batch_flow()) || (is_multi_device())) &&
                    ((current_device_info->current_cycle_requested_transferred_frames_h2d[network_group_handle][name] == current_device_info->current_burst_size)))) {
                finished_send = true;
                break;
            }
        }
        if (finished_send) {
            break;
        }

        for (const auto &name : scheduled_ng->get_inputs_names()) {
            auto status = send_pending_buffer(network_group_handle, name, device_id);
            if (HAILO_STREAM_ABORTED_BY_USER == status) {
                LOGGER__INFO("send_pending_buffer has failed with status=HAILO_STREAM_ABORTED_BY_USER");
                return status;
            }
            CHECK_SUCCESS(status);
        }
        scheduled_ng->push_device_index(device_id);
    }

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::send_pending_buffer(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
    uint32_t device_id)
{
    assert(m_cngs.size() > network_group_handle);
    auto scheduled_ng = m_cngs[network_group_handle];

    auto current_cng = scheduled_ng->get_network_group();
    auto input_stream = current_cng->get_input_stream_by_name(stream_name);
    CHECK_EXPECTED_AS_STATUS(input_stream);

    VDeviceInputStreamMultiplexerWrapper &vdevice_input = static_cast<VDeviceInputStreamMultiplexerWrapper&>(input_stream->get());
    TRACE(InputVdmaEnqueueTrace, "", network_group_handle, stream_name);
    auto status = vdevice_input.send_pending_buffer(device_id);
    if (HAILO_STREAM_ABORTED_BY_USER == status) {
        LOGGER__INFO("send_pending_buffer has failed with status=HAILO_STREAM_ABORTED_BY_USER");
        return status;
    }
    CHECK_SUCCESS(status);

    scheduled_ng->h2d_requested_transferred_frames().increase(stream_name);
    m_devices[device_id]->current_cycle_requested_transferred_frames_h2d[network_group_handle][stream_name]++;
    scheduled_ng->finished_write_frames().decrease(stream_name);

    scheduled_ng->h2d_finished_transferred_frames().increase(stream_name);
    scheduled_ng->h2d_requested_transferred_frames().decrease(stream_name);

    if (should_ng_stop(network_group_handle)) {
        return HAILO_STREAM_ABORTED_BY_USER;
    }

    return HAILO_SUCCESS;
}

void NetworkGroupScheduler::reset_current_ng_timestamps(uint32_t device_id)
{
    auto curr_device_info = m_devices[device_id];
    if (INVALID_NETWORK_GROUP_HANDLE == curr_device_info->current_network_group_handle) {
        return;
    }

    m_cngs[curr_device_info->current_network_group_handle]->set_last_run_timestamp(std::chrono::steady_clock::now()); // Mark timestamp on de-activation

    const auto active_duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(
        std::chrono::steady_clock::now() - m_last_measured_activation_timestamp[curr_device_info->current_network_group_handle]).count();

    assert(contains(m_active_duration, curr_device_info->current_network_group_handle));
    m_active_duration[curr_device_info->current_network_group_handle] += active_duration_sec;
}

NetworkGroupScheduler::ReadyInfo NetworkGroupScheduler::is_network_group_ready(const scheduler_ng_handle_t &network_group_handle, bool check_threshold, uint32_t device_id)
{
    ReadyInfo result;
    result.is_ready = false;

    if (should_ng_stop(network_group_handle)) {
        // Do not switch to an aborted network group
        return result;
    }

    auto scheduled_ng = m_cngs[network_group_handle];
    // Check if there arent any write requests
    bool has_pending_writes = scheduled_ng->finished_write_frames_min_value() > 0;

    // Check if there arent any read requests
    bool has_pending_user_reads = false;
    for (const auto &name : scheduled_ng->get_outputs_names()) {
        if (scheduled_ng->requested_read_frames(name) > 0) {
            has_pending_user_reads = true;
            break;
        }
    }

    std::vector<bool> over_threshold;
    over_threshold.reserve(scheduled_ng->get_inputs_names().size());
    std::vector<bool> over_timeout;
    over_timeout.reserve(scheduled_ng->get_inputs_names().size());

    if (check_threshold) {
        for (const auto &name : scheduled_ng->get_inputs_names()) {
            auto threshold_exp = scheduled_ng->get_threshold(name);
            if (!threshold_exp) {
                LOGGER__ERROR("Failed to get threshold for stream {}", name);
                return result;
            }
            auto threshold = (DEFAULT_SCHEDULER_MIN_THRESHOLD == threshold_exp.value()) ? 1 : threshold_exp.value();
            auto timeout_exp = scheduled_ng->get_timeout();
            if (!timeout_exp) {
                LOGGER__ERROR("Failed to get timeout for stream {}", name);
                return result;
            }
            auto timeout = timeout_exp.release();

            // Check if there arent enough write requests to reach threshold and timeout didnt passed
            auto write_requests = scheduled_ng->requested_write_frames(name) + scheduled_ng->finished_write_frames(name);
            auto stream_over_threshold = write_requests >= threshold;
            auto stream_over_timeout = timeout <= (std::chrono::steady_clock::now() - scheduled_ng->get_last_run_timestamp());
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

    auto has_pending_vdma_frames = get_max_value_of_unordered_map(m_devices[device_id]->current_cycle_requested_transferred_frames_h2d[network_group_handle]) !=
        get_min_value_of_unordered_map(m_devices[device_id]->current_cycle_finished_read_frames_d2h[network_group_handle]);

    result.threshold = std::all_of(over_threshold.begin(), over_threshold.end(), [](auto over) { return over; });
    result.timeout = std::all_of(over_timeout.begin(), over_timeout.end(), [](auto over) { return over; });
    result.is_ready = has_pending_writes && has_pending_user_reads && (!has_pending_vdma_frames);

    return result;
}

Expected<uint32_t> NetworkGroupScheduler::wait_for_read(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
    const std::chrono::milliseconds &timeout)
{
    uint32_t device_id = INVALID_DEVICE_ID;
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        auto scheduled_ng = m_cngs[network_group_handle];

        scheduled_ng->requested_read_frames().increase(stream_name);

        hailo_status status = HAILO_SUCCESS;
        auto wait_res = m_write_read_cv.wait_for(lock, timeout, [this, network_group_handle, scheduled_ng, stream_name, &status] {

            if (should_ng_stop(network_group_handle)) {
                status = HAILO_STREAM_ABORTED_BY_USER;
                return true; // return true so that the wait will finish
            }

            auto device_id = NetworkGroupSchedulerOracle::get_avail_device(*this, network_group_handle);
            if (INVALID_DEVICE_ID != device_id) {
                status = switch_network_group(network_group_handle, device_id);
                if (HAILO_SUCCESS != status) {
                    return true; // return true so that the wait will finish
                }
            }

            return scheduled_ng->can_stream_read(stream_name);
        });
        CHECK_AS_EXPECTED(wait_res, HAILO_TIMEOUT, "{} (D2H) failed with status={}, timeout={}ms", stream_name, HAILO_TIMEOUT, timeout.count());
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            return make_unexpected(status);
        }
        CHECK_SUCCESS_AS_EXPECTED(status);

        scheduled_ng->ongoing_read_frames().increase(stream_name);
        scheduled_ng->requested_read_frames().decrease(stream_name);
        device_id = scheduled_ng->pop_device_index(stream_name);
    }
    m_write_read_cv.notify_all();

    return device_id;
}


hailo_status NetworkGroupScheduler::signal_read_finish(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name, uint32_t device_id)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        auto scheduled_ng = m_cngs[network_group_handle];

        scheduled_ng->finished_read_frames().increase(stream_name);
        m_devices[device_id]->current_cycle_finished_read_frames_d2h[network_group_handle][stream_name]++;
        scheduled_ng->d2h_finished_transferred_frames().decrease(stream_name);
        scheduled_ng->ongoing_read_frames().decrease(stream_name);
        m_fps_accumulator[network_group_handle]++;

        decrease_ng_counters(network_group_handle);
    }
    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}


bool NetworkGroupScheduler::has_ng_finished(const scheduler_ng_handle_t &network_group_handle, uint32_t device_id)
{
    if (INVALID_NETWORK_GROUP_HANDLE == network_group_handle) {
        return true; // If no network group is running, consider it as finished
    }

    auto scheduled_ng = m_cngs[network_group_handle];

    if (scheduled_ng->use_dynamic_batch_flow() || is_multi_device()) {
        for (const auto &name : scheduled_ng->get_outputs_names()) {
            if (m_devices[device_id]->current_cycle_finished_read_frames_d2h[network_group_handle][name] < m_devices[device_id]->current_batch_size) {
                return false;
            }
        }

        return true;
    }

    uint32_t written_frames = get_max_value_of_unordered_map(scheduled_ng->total_written_frames_count());
    for (const auto &name : scheduled_ng->get_outputs_names()) {
        if (scheduled_ng->finished_read_frames(name) < written_frames) {
            return false;
        }
    }
    return true;
}

void NetworkGroupScheduler::decrease_ng_counters(const scheduler_ng_handle_t &network_group_handle)
{
    return m_cngs[network_group_handle]->decrease_current_ng_counters();
}

bool NetworkGroupScheduler::has_ng_drained_everything(const scheduler_ng_handle_t &network_group_handle, uint32_t device_id)
{
    if (INVALID_NETWORK_GROUP_HANDLE == network_group_handle) {
        // If no network group is running, consider it as drained
        return true;
    }

    if (ng_all_streams_aborted(network_group_handle)) {
        // We treat NG as drained only if all streams are aborted - to make sure there aren't any ongoing transfers
        return true;
    }

    if ((!m_cngs[network_group_handle]->is_nms()) && (is_multi_device() || m_cngs[network_group_handle]->use_dynamic_batch_flow())) {
        auto current_device_info = m_devices[device_id];
        auto max_transferred_h2d = get_max_value_of_unordered_map(current_device_info->current_cycle_requested_transferred_frames_h2d[network_group_handle]);
        auto min_transferred_d2h = get_min_value_of_unordered_map(current_device_info->current_cycle_finished_transferred_frames_d2h[network_group_handle]);

        return (max_transferred_h2d == min_transferred_d2h);
    }

    return m_cngs[network_group_handle]->has_ng_drained_everything(!(m_cngs[network_group_handle]->use_dynamic_batch_flow() || is_multi_device()));
}

hailo_status NetworkGroupScheduler::enable_stream(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        if (!m_should_ng_stop[network_group_handle][stream_name]) {
            return HAILO_SUCCESS;
        }

        m_should_ng_stop[network_group_handle][stream_name] = false;
    }
    m_write_read_cv.notify_all();
    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::disable_stream(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        if (m_should_ng_stop[network_group_handle][stream_name]) {
            return HAILO_SUCCESS;
        }

        m_should_ng_stop[network_group_handle][stream_name] = true;
    }
    m_write_read_cv.notify_all();
    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::set_timeout(const scheduler_ng_handle_t &network_group_handle, const std::chrono::milliseconds &timeout, const std::string &/*network_name*/)
{
    // TODO: call in loop for set_timeout with the relevant stream-names (of the given network)
    return m_cngs[network_group_handle]->set_timeout(timeout);
}

hailo_status NetworkGroupScheduler::set_threshold(const scheduler_ng_handle_t &network_group_handle, uint32_t threshold, const std::string &/*network_name*/)
{
    // TODO: call in loop for set_timeout with the relevant stream-names (of the given network)
    return m_cngs[network_group_handle]->set_threshold(threshold);
}

void NetworkGroupScheduler::choose_next_network_group(size_t device_id)
{
    if (!m_devices[device_id]->is_switching_network_group) {
        NetworkGroupSchedulerOracle::choose_next_model(*this, m_devices[device_id]->device_id);
    }
}

bool NetworkGroupScheduler::should_ng_stop(const scheduler_ng_handle_t &network_group_handle)
{
    for (const auto &name_flag_pair : m_should_ng_stop[network_group_handle]) {
        if (name_flag_pair.second) {
            return true;
        }
    }

    return false;
}

bool NetworkGroupScheduler::ng_all_streams_aborted(const scheduler_ng_handle_t &network_group_handle)
{
    for (const auto &name_flag_pair : m_should_ng_stop[network_group_handle]) {
        if (!name_flag_pair.second) {
            return false;
        }
    }
    return true;
}

void NetworkGroupScheduler::notify_all()
{
    {
        // Acquire mutex to make sure the notify_all will wake the blocking threads on the cv
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
    }
    m_write_read_cv.notify_all();
}

void NetworkGroupScheduler::mark_failed_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        assert(m_cngs.size() > network_group_handle);
        m_cngs[network_group_handle]->requested_write_frames().decrease(stream_name);
    }
    m_write_read_cv.notify_all();
}


} /* namespace hailort */
