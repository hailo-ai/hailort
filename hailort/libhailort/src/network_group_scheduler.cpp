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
#include "hef_internal.hpp"
#include "vdevice_stream_wrapper.hpp"

#include <fstream>

namespace hailort
{

NetworkGroupScheduler::NetworkGroupScheduler(hailo_scheduling_algorithm_t algorithm) :
    m_is_switching_network_group(true),
    m_current_network_group(INVALID_NETWORK_GROUP_HANDLE),
    m_next_network_group(INVALID_NETWORK_GROUP_HANDLE),
    m_algorithm(algorithm),
    m_before_read_write_mutex(),
    m_current_batch_size(0),
    m_write_read_cv(),
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

NetworkGroupScheduler::~NetworkGroupScheduler()
{
    if (m_should_monitor) {
        m_should_monitor = false;
        m_mon_shutdown_event->signal();
        if (m_mon_thread.joinable()) {
            m_mon_thread.join();
        }
    }
}

Expected<NetworkGroupSchedulerPtr> NetworkGroupScheduler::create_round_robin()
{
    auto ptr = make_shared_nothrow<NetworkGroupSchedulerRoundRobin>();
    CHECK_AS_EXPECTED(nullptr != ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::static_pointer_cast<NetworkGroupScheduler>(ptr);
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

std::string NetworkGroupScheduler::get_network_group_name(const scheduler_ng_handle_t network_group_handle)
{
    auto cng = m_cngs[network_group_handle].lock();
    if (nullptr == cng) {
        LOGGER__CRITICAL("Configured network group is null!");
        return "";
    }
    return cng->name();
}

// TODO: HRT-7392 - Reduce core percentage when scheduler is idle
void NetworkGroupScheduler::log_monitor_networks_infos(ProtoMon &mon)
{
    auto curr_time = std::chrono::steady_clock::now();
    const auto measurement_duration = std::chrono::duration_cast<std::chrono::duration<double>>(curr_time - m_last_measured_timestamp).count();

    for (uint32_t network_group_handle = 0; network_group_handle < m_last_measured_activation_timestamp.size(); network_group_handle++) {
        assert(contains(m_active_duration, network_group_handle));
        auto curr_ng_core = m_active_duration[network_group_handle];
        
        if (network_group_handle == m_current_network_group) {
            // Network is currently active
            auto time_diff = std::chrono::duration_cast<std::chrono::duration<double>>(
                curr_time - m_last_measured_activation_timestamp[m_current_network_group]).count();
            curr_ng_core += time_diff;
            m_last_measured_activation_timestamp[m_current_network_group] = curr_time;
        }

        auto core_utilization = ((curr_ng_core * 100) /  measurement_duration);
        auto outputs_count = static_cast<uint32_t>(m_allowed_read[network_group_handle].size());
        auto fps = static_cast<double>((m_fps_accumulator[network_group_handle] / outputs_count) / measurement_duration);

        auto net_info = mon.add_networks_infos();
        net_info->set_network_name(get_network_group_name(network_group_handle));
        net_info->set_core_utilization(core_utilization);
        net_info->set_fps(fps);
    }
    
    m_last_measured_timestamp = curr_time;
}

void NetworkGroupScheduler::log_monitor_frames_infos(ProtoMon &mon)
{
    for (uint32_t network_group_handle = 0; network_group_handle < m_cngs.size(); network_group_handle++) {
        auto net_frames_info = mon.add_net_frames_infos();
        net_frames_info->set_network_name(get_network_group_name(network_group_handle));

        assert(contains(m_requested_write, network_group_handle));
        for (auto &streams_requested_write_pair : m_requested_write[network_group_handle]) {
            auto &stream_name = streams_requested_write_pair.first;

            auto stream_frames_info = net_frames_info->add_streams_frames_infos();
            stream_frames_info->set_stream_name(stream_name);
            stream_frames_info->set_stream_direction(PROTO__STREAM_DIRECTION__HOST_TO_DEVICE);
            auto status = set_h2d_frames_counters(network_group_handle, stream_name, *stream_frames_info);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to set stream's {} frames count, status = {}", stream_name, status);
                continue;
            }
        }
        
        assert(contains(m_allowed_read, network_group_handle));
        for (auto &streams_requested_read_pair : m_allowed_read[network_group_handle]) {
            auto &stream_name = streams_requested_read_pair.first;

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

hailo_status NetworkGroupScheduler::set_h2d_frames_counters(scheduler_ng_handle_t network_group_handle, const std::string &stream_name,
    ProtoMonStreamFramesInfo &stream_frames_info)
{
    assert(m_cngs.size() > network_group_handle);
    auto current_cng = m_cngs[network_group_handle].lock();
    CHECK(current_cng, HAILO_INTERNAL_FAILURE);

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

hailo_status NetworkGroupScheduler::set_d2h_frames_counters(scheduler_ng_handle_t network_group_handle, const std::string &stream_name,
    ProtoMonStreamFramesInfo &stream_frames_info)
{
    assert(m_cngs.size() > network_group_handle);
    auto current_cng = m_cngs[network_group_handle].lock();
    CHECK(current_cng, HAILO_INTERNAL_FAILURE);

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

Expected<scheduler_ng_handle_t> NetworkGroupScheduler::add_network_group(std::weak_ptr<ConfiguredNetworkGroup> added_cng)
{
    scheduler_ng_handle_t network_group_handle = INVALID_NETWORK_GROUP_HANDLE;
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        network_group_handle = static_cast<uint32_t>(m_cngs.size());

        m_cngs.emplace_back(added_cng);
        m_last_measured_activation_timestamp[network_group_handle] = {};
        m_active_duration[network_group_handle] = 0;
        m_fps_accumulator[network_group_handle] = 0;
        m_last_run_time_stamp[network_group_handle] = std::chrono::steady_clock::now();
        m_frame_was_sent_per_network_group[network_group_handle] = false;
        m_timeout_per_network_group[network_group_handle] = make_shared_nothrow<std::chrono::milliseconds>(DEFAULT_SCHEDULER_TIMEOUT);
        CHECK_AS_EXPECTED(nullptr != m_timeout_per_network_group[network_group_handle], HAILO_OUT_OF_HOST_MEMORY);

        auto added_cng_ptr = added_cng.lock();
        CHECK_AS_EXPECTED(added_cng_ptr, HAILO_INTERNAL_FAILURE);

        auto stream_infos = added_cng_ptr->get_all_stream_infos();
        CHECK_EXPECTED(stream_infos);

        m_max_batch_size[network_group_handle] = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE;
        auto cng_base = std::dynamic_pointer_cast<ConfiguredNetworkGroupBase>(added_cng_ptr);
        if (cng_base->get_supported_features().multi_context) {
            auto batch_size = cng_base->get_stream_batch_size(stream_infos.value()[0].name);
            CHECK_EXPECTED(batch_size);

            if (batch_size.value() > HAILO_DEFAULT_BATCH_SIZE) {
                m_max_batch_size[network_group_handle] = batch_size.release();
            }
        }

        // Prepare empty counters for the added cng
        for (const auto &stream_info : stream_infos.value()) {
            m_should_ng_stop[network_group_handle][stream_info.name] = false;
            m_min_threshold_per_stream[network_group_handle][stream_info.name] = DEFAULT_SCHEDULER_MIN_THRESHOLD;
            if (HAILO_H2D_STREAM == stream_info.direction) {
                m_requested_write[network_group_handle][stream_info.name] = 0;
                m_written_buffer[network_group_handle][stream_info.name] = 0;
                m_sent_pending_buffer[network_group_handle][stream_info.name] = 0;
                m_current_sent_pending_buffer[network_group_handle][stream_info.name] = 0;
                m_finished_sent_pending_buffer[network_group_handle][stream_info.name] = 0;

                auto event = Event::create_shared(Event::State::signalled);
                CHECK_AS_EXPECTED(nullptr != event, HAILO_OUT_OF_HOST_MEMORY);

                m_write_buffer_events[network_group_handle][stream_info.name] = event;
            } else {
                m_requested_read[network_group_handle][stream_info.name] = 0;
                m_allowed_read[network_group_handle][stream_info.name] = 0;
                m_finished_read[network_group_handle][stream_info.name] = 0;
                m_current_finished_read[network_group_handle][stream_info.name] = 0;
                m_pending_read[network_group_handle][stream_info.name] = 0;
            }
        }
    }
    m_write_read_cv.notify_all();
    return network_group_handle;
}

hailo_status NetworkGroupScheduler::wait_for_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    while (true) {
        auto status = block_write_if_needed(network_group_handle, stream_name);
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return HAILO_STREAM_INTERNAL_ABORT;
        }
        CHECK_SUCCESS(status);
        m_write_read_cv.notify_all();

        status = m_write_buffer_events[network_group_handle][stream_name]->wait(std::chrono::milliseconds(HAILO_INFINITE));
        CHECK_SUCCESS(status);

        {
            std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

            auto should_wait_again = should_wait_for_write(network_group_handle, stream_name);
            if (HAILO_STREAM_INTERNAL_ABORT == should_wait_again.status()) {
                return HAILO_STREAM_INTERNAL_ABORT;
            }
            CHECK_EXPECTED_AS_STATUS(should_wait_again);

            if (!should_wait_again.value()) {
                if (!m_frame_was_sent_per_network_group[network_group_handle]) {
                    m_frame_was_sent_per_network_group[network_group_handle] = true;
                }
                m_requested_write[network_group_handle][stream_name]++;
                status = allow_writes_for_other_inputs_if_needed(network_group_handle);
                CHECK_SUCCESS(status);
                break;
            }
        }
    }
    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::block_write_if_needed(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
    assert(contains(m_write_buffer_events, network_group_handle));
    auto should_wait = should_wait_for_write(network_group_handle, stream_name);
    if (HAILO_STREAM_INTERNAL_ABORT == should_wait.status()) {
        return HAILO_STREAM_INTERNAL_ABORT;
    }
    CHECK_EXPECTED_AS_STATUS(should_wait);
    if (should_wait.value()) {
        auto status = m_write_buffer_events[network_group_handle][stream_name]->reset();
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::has_enough_space_in_read_buffers(const scheduler_ng_handle_t &network_group_handle, uint32_t ongoing_frames)
{
    assert(network_group_handle < m_cngs.size());
    auto cng = m_cngs[network_group_handle].lock();
    assert(cng);

    auto output_streams = cng->get_output_streams();
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

bool NetworkGroupScheduler::has_input_written_most_frames(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    return m_requested_write[network_group_handle][stream_name] == get_max_value_of_unordered_map(m_requested_write[network_group_handle]);
}

bool NetworkGroupScheduler::should_ng_stop(const scheduler_ng_handle_t &network_group_handle)
{
    assert(contains(m_should_ng_stop, network_group_handle));
    for (const auto &name_flag_pair : m_should_ng_stop[network_group_handle]) {
        if (name_flag_pair.second) {
            return true;
        }
    }

    return false;
}

Expected<bool> NetworkGroupScheduler::should_wait_for_write(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    if (should_ng_stop(network_group_handle)) {
        return make_unexpected(HAILO_STREAM_INTERNAL_ABORT);
    }

    assert(contains(m_requested_write, network_group_handle));
    assert(contains(m_sent_pending_buffer, network_group_handle));
    assert(contains(m_max_batch_size, network_group_handle));

    auto pending_buffers = m_requested_write[network_group_handle][stream_name] - m_sent_pending_buffer[network_group_handle][stream_name];
    bool has_written_max_batch_size = (is_ng_multicontext(network_group_handle) && (m_max_batch_size[network_group_handle] == pending_buffers));

    bool should_stop_writing_because_switching = ((nullptr != m_ang) && m_is_switching_network_group &&
        (network_group_handle == m_current_network_group) && has_input_written_most_frames(network_group_handle, stream_name));

    auto min_finished_read = get_min_value_of_unordered_map(m_finished_read[network_group_handle]);
    auto ongoing_frames = (min_finished_read < m_requested_write[network_group_handle][stream_name]) ?
        (m_requested_write[network_group_handle][stream_name] - min_finished_read) : 0;
    bool has_enough_space_for_writes = has_enough_space_in_read_buffers(network_group_handle, ongoing_frames);

    if (has_written_max_batch_size || should_stop_writing_because_switching || (!has_enough_space_for_writes)) {
        return true;
    }

    return false;
}

hailo_status NetworkGroupScheduler::allow_writes_for_other_inputs_if_needed(const scheduler_ng_handle_t &network_group_handle)
{
    if (!has_ng_finished(network_group_handle) && m_is_switching_network_group) {
        auto max_write = get_max_value_of_unordered_map(m_requested_write[network_group_handle]);
        for (auto &name_event_pair : m_write_buffer_events[network_group_handle]) {
            if (m_requested_write[network_group_handle][name_event_pair.first] < max_write) {
                auto status = name_event_pair.second->signal();
                CHECK_SUCCESS(status);
            }
        }
    }
    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::signal_write_finish(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        if (should_ng_stop(network_group_handle)) {
            return HAILO_STREAM_INTERNAL_ABORT;
        }

        assert(contains(m_written_buffer, network_group_handle));
        assert(contains(m_written_buffer[network_group_handle], stream_name));
        m_written_buffer[network_group_handle][stream_name]++;

        auto status = switch_network_group_if_idle(network_group_handle, lock);
        CHECK_SUCCESS(status);

        status = switch_network_group_if_should_be_next(network_group_handle, lock);
        CHECK_SUCCESS(status);

        status = send_all_pending_buffers(network_group_handle, lock);
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            LOGGER__INFO("send_all_pending_buffers has failed with status=HAILO_STREAM_INTERNAL_ABORT");
            return status;
        }
        CHECK_SUCCESS(status);
    }
    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::switch_network_group_if_idle(const scheduler_ng_handle_t &network_group_handle,
    std::unique_lock<std::mutex> &read_write_lock)
{
    const bool check_threshold = false;
    if (!m_is_switching_network_group && has_ng_drained_everything(m_current_network_group) &&
        ((nullptr == m_ang) || (network_group_handle != m_current_network_group)) && is_network_group_ready(network_group_handle, check_threshold)) {
        auto status = activate_network_group(network_group_handle, read_write_lock);
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return status;
        }
        CHECK_SUCCESS(status);

        return HAILO_SUCCESS;
    }

    auto status = try_change_multicontext_ng_batch_size(network_group_handle, read_write_lock);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::has_pending_frames(const scheduler_ng_handle_t &network_group_handle)
{
    uint32_t transferred_frames = get_max_value_of_unordered_map(m_sent_pending_buffer[network_group_handle]);
    for (auto &name_counter_pair : m_finished_read[network_group_handle]) {
        if (name_counter_pair.second < transferred_frames) {
            return true;
        }
    }
    return false;
}

hailo_status NetworkGroupScheduler::try_change_multicontext_ng_batch_size(const scheduler_ng_handle_t &network_group_handle,
    std::unique_lock<std::mutex> &read_write_lock)
{
    if ((nullptr != m_ang) && (network_group_handle == m_current_network_group) && is_ng_multicontext(network_group_handle) && has_ng_finished(network_group_handle)) {
        if (get_buffered_frames_count() > 0) {
            hailo_status status = activate_network_group(network_group_handle, read_write_lock, true);
            CHECK_SUCCESS(status);
        }
    }

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::activate_network_group(const scheduler_ng_handle_t &network_group_handle,
    std::unique_lock<std::mutex> &read_write_lock, bool keep_nn_config)
{
    for (auto &name_counter_pair : m_current_sent_pending_buffer[network_group_handle]) {
        name_counter_pair.second = 0;
    }

    for (auto &name_counter_pair : m_current_finished_read[network_group_handle]) {
        name_counter_pair.second = 0;
    }

    uint16_t batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE;
    if (is_ng_multicontext(network_group_handle)) {
        batch_size = static_cast<uint16_t>(get_buffered_frames_count());
    }

    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE == batch_size) {
        batch_size = 1;
    }

    bool has_same_batch_size_as_previous = (m_current_batch_size == batch_size);
    m_current_batch_size = batch_size;

    if (m_current_network_group != INVALID_NETWORK_GROUP_HANDLE) {
        if ((network_group_handle != m_current_network_group) || (!has_same_batch_size_as_previous)) {
            if (nullptr != m_ang) {
                auto status = m_ang->set_keep_nn_config_during_reset(keep_nn_config);
                CHECK_SUCCESS(status);
            }
                
            deactivate_network_group();
        } else {
            reset_current_ng_timestamps();
        }
    }

    m_last_measured_activation_timestamp[network_group_handle] = std::chrono::steady_clock::now();

    if (m_current_network_group != network_group_handle) {
        m_is_switching_network_group = false;
    }

    auto status = allow_all_writes();
    CHECK_SUCCESS(status);

    if ((network_group_handle != m_current_network_group) || (!has_same_batch_size_as_previous)) {
        assert(m_cngs.size() > network_group_handle);
        auto cng = m_cngs[network_group_handle].lock();
        CHECK(cng, HAILO_INTERNAL_FAILURE);

        auto cng_base = std::dynamic_pointer_cast<ConfiguredNetworkGroupBase>(cng);
        auto expected_ang = cng_base->force_activate(batch_size);
        CHECK_EXPECTED_AS_STATUS(expected_ang);

        m_ang = expected_ang.release();

        // Register to get interrupts - has to be after network group is activated
        for (auto &output_stream : cng->get_output_streams()) {
            OutputStreamBase &vdevice_output = static_cast<OutputStreamBase&>(output_stream.get());
            status = vdevice_output.register_for_d2h_interrupts(
                [this, network_group_handle, name = output_stream.get().name(), format = vdevice_output.get_layer_info().format.order](uint32_t frames) {
                    if(hailo_format_order_t::HAILO_FORMAT_ORDER_HAILO_NMS != format) {
                        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
                        m_pending_read[network_group_handle][name] += frames;
                    }
                    m_write_read_cv.notify_all();
                });
            CHECK_SUCCESS(status);
        }
    }

    m_last_run_time_stamp[network_group_handle] = std::chrono::steady_clock::now(); // Mark timestamp on activation
    m_current_network_group = network_group_handle;

    status = send_all_pending_buffers(network_group_handle, read_write_lock);
    if (HAILO_STREAM_INTERNAL_ABORT == status) {
        LOGGER__INFO("send_all_pending_buffers has failed with status=HAILO_STREAM_INTERNAL_ABORT");
        return status;
    }
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::allow_all_writes()
{
    for (auto &handle_dict_pair : m_write_buffer_events) {
        for (auto &name_event_pair : handle_dict_pair.second) {
            auto status = name_event_pair.second->signal();
            CHECK_SUCCESS(status);
        }
    }
    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::send_all_pending_buffers(const scheduler_ng_handle_t &network_group_handle,
    std::unique_lock<std::mutex> &read_write_lock)
{
    if ((nullptr == m_ang) || (m_current_network_group != network_group_handle)) {
        return HAILO_SUCCESS;
    }

    while (true) {
        uint32_t finished_sending_count = 0;
        for (auto &name_counter_pair : m_written_buffer[network_group_handle]) {
            if ((m_sent_pending_buffer[network_group_handle][name_counter_pair.first] < name_counter_pair.second)
                    && ((!is_ng_multicontext(network_group_handle)) || (m_current_sent_pending_buffer[network_group_handle][name_counter_pair.first] < m_current_batch_size))) {
                auto status = send_pending_buffer(network_group_handle, name_counter_pair.first, read_write_lock);
                if (HAILO_STREAM_INTERNAL_ABORT == status) {
                    LOGGER__INFO("send_pending_buffer has failed with status=HAILO_STREAM_INTERNAL_ABORT");
                    return status;
                }
                CHECK_SUCCESS(status);
            } else {
                finished_sending_count++;
            }
        }
        if (finished_sending_count == m_written_buffer[network_group_handle].size()) {
            break;
        }
    }

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::send_pending_buffer(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name,
    std::unique_lock<std::mutex> &read_write_lock)
{
    assert(m_cngs.size() > network_group_handle);
    auto current_cng = m_cngs[network_group_handle].lock();
    CHECK(current_cng, HAILO_INTERNAL_FAILURE);

    auto input_stream = current_cng->get_input_stream_by_name(stream_name);
    CHECK_EXPECTED_AS_STATUS(input_stream);

    VDeviceInputStreamWrapper &vdevice_input = static_cast<VDeviceInputStreamWrapper&>(input_stream->get());
    auto pending_buffer_state = vdevice_input.send_pending_buffer();
    CHECK_EXPECTED_AS_STATUS(pending_buffer_state);

    assert(contains(m_sent_pending_buffer, network_group_handle));
    m_sent_pending_buffer[network_group_handle][stream_name]++;

    assert(contains(m_current_sent_pending_buffer, network_group_handle));
    m_current_sent_pending_buffer[network_group_handle][stream_name]++;

    auto status = pending_buffer_state->finish(vdevice_input.get_timeout(), read_write_lock);
    if (HAILO_STREAM_INTERNAL_ABORT == status) {
        LOGGER__INFO("finish has failed with status=HAILO_STREAM_INTERNAL_ABORT");
        return status;
    }
    CHECK_SUCCESS(status);

    assert(contains(m_finished_sent_pending_buffer, network_group_handle));
    m_finished_sent_pending_buffer[network_group_handle][stream_name]++;

    if (should_ng_stop(network_group_handle)) {
        return HAILO_STREAM_INTERNAL_ABORT;
    }

    return HAILO_SUCCESS;
}

void NetworkGroupScheduler::deactivate_network_group()
{
    if (m_ang) {
        m_ang.reset();
    }

    reset_current_ng_timestamps();
}

void NetworkGroupScheduler::reset_current_ng_timestamps()
{
    m_last_run_time_stamp[m_current_network_group] = std::chrono::steady_clock::now(); // Mark timestamp on de-activation
    assert(contains(m_last_measured_activation_timestamp, m_current_network_group));
    const auto active_duration_sec = std::chrono::duration_cast<std::chrono::duration<double>>(
        std::chrono::steady_clock::now() - m_last_measured_activation_timestamp[m_current_network_group]).count();
    m_active_duration[m_current_network_group] += active_duration_sec;
}

hailo_status NetworkGroupScheduler::switch_network_group_if_should_be_next(const scheduler_ng_handle_t &network_group_handle,
    std::unique_lock<std::mutex> &read_write_lock)
{
    const bool check_threshold = false;
    /* Checking (nullptr == m_ang) for activating the first time the scheduler is running.
       In this case we don't want to check threshold. */
    if (m_is_switching_network_group && has_ng_drained_everything(m_current_network_group) &&
        (((nullptr == m_ang) && is_network_group_ready(network_group_handle, check_threshold)) || (m_next_network_group == network_group_handle))) {
        auto status = activate_network_group(network_group_handle, read_write_lock);
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return status;
        }
        CHECK_SUCCESS(status);
    }
    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::is_network_group_ready(const scheduler_ng_handle_t &network_group_handle, bool check_threshold)
{
    assert(contains(m_written_buffer, network_group_handle));
    assert(contains(m_min_threshold_per_stream, network_group_handle));
    assert(contains(m_last_run_time_stamp, network_group_handle));

    // Check if there arent any write requests
    bool has_pending_writes = false;
    uint32_t written_frames = get_max_value_of_unordered_map(m_requested_write[network_group_handle]);
    for (const auto &name_counter_pair : m_pending_read[network_group_handle]) {
        uint32_t finished_read_frames = m_finished_read[network_group_handle][name_counter_pair.first];
        if ((finished_read_frames + name_counter_pair.second) < written_frames) {
            has_pending_writes = true;
            break;
        }
    }

    // Check if there arent any read requests
    bool has_pending_reads = false;
    uint32_t read_requests = get_max_value_of_unordered_map(m_requested_read[network_group_handle]);
    for (const auto &name_counter_pair : m_allowed_read[network_group_handle]) {
        if (name_counter_pair.second < read_requests) {
            has_pending_reads = true;
            break;
        }
    }

    if (check_threshold) {
        for (auto &name_counter_pair : m_written_buffer[network_group_handle]) {
            // Check if there arent enough write requests to reach threshold and timeout didnt passed
            if ((name_counter_pair.second < m_min_threshold_per_stream[network_group_handle][name_counter_pair.first]) &&
                ((*(m_timeout_per_network_group[network_group_handle]) > (std::chrono::steady_clock::now() - m_last_run_time_stamp[network_group_handle])))) {
                return false;
            }
        }
    }

    return has_pending_writes && has_pending_reads && (!has_pending_frames(network_group_handle));
}

hailo_status NetworkGroupScheduler::wait_for_read(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        assert(contains(m_allowed_read, network_group_handle));
        assert(contains(m_allowed_read[network_group_handle], stream_name));
        assert(contains(m_requested_read, network_group_handle));
        assert(contains(m_requested_read[network_group_handle], stream_name));

        m_requested_read[network_group_handle][stream_name]++;

        hailo_status status = HAILO_UNINITIALIZED;
        m_write_read_cv.wait(lock, [this, network_group_handle, stream_name, &status, &lock] {
            if (should_ng_stop(network_group_handle)) {
                status = HAILO_STREAM_INTERNAL_ABORT;
                return true; // return true so that the wait will finish
            }

            status = switch_network_group_if_idle(network_group_handle, lock);
            if (HAILO_SUCCESS != status) {
                return true; // return true so that the wait will finish
            }

            status = switch_network_group_if_should_be_next(network_group_handle, lock);
            if (HAILO_SUCCESS != status) {
                return true; // return true so that the wait will finish
            }

            return can_stream_read(network_group_handle, stream_name);
        });
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return status;
        }
        CHECK_SUCCESS(status);

        m_allowed_read[network_group_handle][stream_name]++;
    }
    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::can_stream_read(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    assert(contains(m_allowed_read, network_group_handle));
    assert(contains(m_sent_pending_buffer, network_group_handle));
    return m_allowed_read[network_group_handle][stream_name].load() < get_max_value_of_unordered_map(m_sent_pending_buffer[network_group_handle]);
}

hailo_status NetworkGroupScheduler::signal_read_finish(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        assert(contains(m_finished_read, network_group_handle));
        assert(contains(m_finished_read[network_group_handle], stream_name));

        // Prevent integer underflow in nms
        if (m_pending_read[network_group_handle][stream_name] > 0) {
            m_pending_read[network_group_handle][stream_name]--;
        }
        m_finished_read[network_group_handle][stream_name]++;
        m_current_finished_read[network_group_handle][stream_name]++;
        m_fps_accumulator[network_group_handle]++;

        hailo_status status = choose_next_network_group();
        CHECK_SUCCESS(status);

        if (!is_ng_multicontext(network_group_handle)) {
            // Prevents integer overflow of the counters
            decrease_current_ng_counters();
        } else {
            status = try_change_multicontext_ng_batch_size(network_group_handle, lock);
            CHECK_SUCCESS(status);
        }
    }

    auto status = allow_all_writes();
    CHECK_SUCCESS(status);
    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::has_ng_finished(scheduler_ng_handle_t network_group_handle)
{
    if (INVALID_NETWORK_GROUP_HANDLE == network_group_handle) {
        return true; // If no network group is running, consider it as finished
    }

    if (is_ng_multicontext(network_group_handle)) {
        for (auto &name_counter_pair : m_current_finished_read[network_group_handle]) {
            if (name_counter_pair.second < m_current_batch_size) {
                return false;
            }
        }

        return true;
    }

    uint32_t written_frames = get_max_value_of_unordered_map(m_requested_write[network_group_handle]);
    for (auto &name_counter_pair : m_finished_read[network_group_handle]) {
        if (name_counter_pair.second < written_frames) {
            return false;
        }
    }
    return true;
}

bool NetworkGroupScheduler::has_ng_drained_everything(scheduler_ng_handle_t network_group_handle)
{
    if (INVALID_NETWORK_GROUP_HANDLE == network_group_handle) {
        // If no network group is running, consider it as drained
        return true;
    }

    uint32_t written_frames = get_max_value_of_unordered_map(m_requested_write[network_group_handle]);
    for (auto &name_counter_pair : m_finished_sent_pending_buffer[network_group_handle]) {
        if (name_counter_pair.second < written_frames) {
            return false;
        }
    }

    assert(contains(m_finished_read, network_group_handle));
    for (const auto &name_counter_pair : m_pending_read[network_group_handle]) {
        uint32_t finished_read_frames = m_finished_read[network_group_handle][name_counter_pair.first];
        if ((finished_read_frames + name_counter_pair.second) < written_frames) {
            return false;
        }
    }
    return true;
}

void NetworkGroupScheduler::decrease_current_ng_counters()
{
    if (nullptr == m_ang) {
        return;
    }

    // Decrease only if counter is 2 or bigger because reaching 0 can cause states to change
    for (auto &name_counter_pair : m_requested_write[m_current_network_group]) {
        if (name_counter_pair.second <= 1) {
            return;
        }
    }
    for (auto &name_counter_pair : m_written_buffer[m_current_network_group]) {
        if (name_counter_pair.second <= 1) {
            return;
        }
    }
    for (auto &name_counter_pair : m_sent_pending_buffer[m_current_network_group]) {
        if (name_counter_pair.second <= 1) {
            return;
        }
    }
    for (auto &name_counter_pair : m_finished_sent_pending_buffer[m_current_network_group]) {
        if (name_counter_pair.second <= 1) {
            return;
        }
    }
    for (auto &name_counter_pair : m_requested_read[m_current_network_group]) {
        if (name_counter_pair.second <= 1) {
            return;
        }
    }
    for (auto &name_counter_pair : m_allowed_read[m_current_network_group]) {
        if (name_counter_pair.second <= 1) {
            return;
        }
    }
    for (auto &name_counter_pair : m_finished_read[m_current_network_group]) {
        if (name_counter_pair.second <= 1) {
            return;
        }
    }

    for (auto &name_counter_pair : m_requested_write[m_current_network_group]) {
        name_counter_pair.second--;
    }
    for (auto &name_counter_pair : m_written_buffer[m_current_network_group]) {
        name_counter_pair.second--;
    }
    for (auto &name_counter_pair : m_sent_pending_buffer[m_current_network_group]) {
        name_counter_pair.second--;
    }
    for (auto &name_counter_pair : m_finished_sent_pending_buffer[m_current_network_group]) {
        name_counter_pair.second--;
    }
    for (auto &name_counter_pair : m_requested_read[m_current_network_group]) {
        name_counter_pair.second--;
    }
    for (auto &name_counter_pair : m_allowed_read[m_current_network_group]) {
        name_counter_pair.second--;
    }
    for (auto &name_counter_pair : m_finished_read[m_current_network_group]) {
        name_counter_pair.second--;
    }
}

bool NetworkGroupScheduler::is_ng_multicontext(const scheduler_ng_handle_t &network_group_handle)
{
    return (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE != m_max_batch_size[network_group_handle]);
}

uint32_t NetworkGroupScheduler::get_buffered_frames_count()
{
    std::unordered_map<stream_name_t, std::atomic_uint32_t> buffered_frames;
    for (const auto &name_counter_pair : m_requested_write[m_current_network_group]) {
        buffered_frames[name_counter_pair.first] = name_counter_pair.second - m_sent_pending_buffer[m_current_network_group][name_counter_pair.first];
    }

    return get_max_value_of_unordered_map(buffered_frames);
}

hailo_status NetworkGroupScheduler::enable_stream(const scheduler_ng_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        assert(contains(m_should_ng_stop, network_group_handle));
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
        assert(contains(m_should_ng_stop, network_group_handle));
        if (m_should_ng_stop[network_group_handle][stream_name]) {
            return HAILO_SUCCESS;
        }

        m_should_ng_stop[network_group_handle][stream_name] = true;

        // Signal event to exit infinite timeout on wait_for_write if actually an input stream
        assert(contains(m_write_buffer_events, network_group_handle));
        if (contains(m_write_buffer_events[network_group_handle], stream_name)) {
            auto status = m_write_buffer_events[network_group_handle][stream_name]->signal();
            CHECK_SUCCESS(status);
        }
    }
    m_write_read_cv.notify_all();
    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::set_timeout(const scheduler_ng_handle_t &network_group_handle, const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    (void)network_name;

    assert(contains(m_timeout_per_network_group, network_group_handle));
    assert(contains(m_last_run_time_stamp, network_group_handle));
    assert(contains(m_frame_was_sent_per_network_group, network_group_handle));
    CHECK(!m_frame_was_sent_per_network_group[network_group_handle], HAILO_INVALID_OPERATION,
        "Setting scheduler timeout is allowed only before sending / receiving frames on the network group.");
    *(m_timeout_per_network_group[network_group_handle]) = timeout;

    assert(m_cngs.size() > network_group_handle);
    auto cng = m_cngs[network_group_handle].lock();
    CHECK(cng, HAILO_INTERNAL_FAILURE);

    auto name = (network_name.empty()) ? cng->name() : network_name;
    LOGGER__INFO("Setting scheduler timeout of {} to {}ms", name, timeout.count());

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::set_threshold(const scheduler_ng_handle_t &network_group_handle, uint32_t threshold, const std::string &network_name)
{
    (void)network_name;

    assert(contains(m_min_threshold_per_stream, network_group_handle));
    assert(contains(m_last_run_time_stamp, network_group_handle));
    assert(contains(m_frame_was_sent_per_network_group, network_group_handle));
    CHECK(!m_frame_was_sent_per_network_group[network_group_handle], HAILO_INVALID_OPERATION,
        "Setting scheduler threshold is allowed only before sending / receiving frames on the network group.");
    for (auto &threshold_per_stream_pair : m_min_threshold_per_stream[network_group_handle]) {
        threshold_per_stream_pair.second = threshold;
    }

    assert(m_cngs.size() > network_group_handle);
    auto cng = m_cngs[network_group_handle].lock();
    CHECK(cng, HAILO_INTERNAL_FAILURE);

    auto name = (network_name.empty()) ? cng->name() : network_name;
    LOGGER__INFO("Setting scheduler threshold of {} to {} frames", name, threshold);

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::choose_next_network_group()
{
    if (!m_is_switching_network_group) {
        bool check_threshold = true;
        if (find_next_network_group_by_algorithm(check_threshold)) {
            return HAILO_SUCCESS;
        }
    }
    return HAILO_SUCCESS;
}

bool NetworkGroupSchedulerRoundRobin::find_next_network_group_by_algorithm(bool check_threshold)
{
    for (uint32_t i = 0; i < m_cngs.size() - 1; i++) {
        uint32_t index = m_current_network_group + i + 1;
        index %= static_cast<uint32_t>(m_cngs.size());
        if (is_network_group_ready(index, check_threshold)) {
            m_is_switching_network_group = true;
            m_next_network_group = index;
            return true;
        }
    }
    return false;
}

} /* namespace hailort */
