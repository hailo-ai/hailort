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
#include "vdevice_stream.hpp"
#include "vdma_stream.hpp"

namespace hailort
{

NetworkGroupScheduler::~NetworkGroupScheduler()
{
    // Making sure the timer threads released first as they use other members in their lambdas
    m_timer_threads_per_network_group.clear();
}

Expected<NetworkGroupSchedulerPtr> NetworkGroupScheduler::create_round_robin()
{
    auto ptr = make_shared_nothrow<NetworkGroupSchedulerRoundRobin>();
    CHECK_AS_EXPECTED(nullptr != ptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::static_pointer_cast<NetworkGroupScheduler>(ptr);
}

hailo_status NetworkGroupScheduler::SchedulerIdleGuard::set_scheduler(std::shared_ptr<NetworkGroupScheduler> scheduler)
{
    CHECK(nullptr != scheduler, HAILO_INTERNAL_FAILURE);
    m_scheduler = scheduler;
    m_scheduler->force_idle_state();
    return HAILO_SUCCESS;
}

NetworkGroupScheduler::SchedulerIdleGuard::~SchedulerIdleGuard()
{
    if (m_scheduler) {
        m_scheduler->resume_from_idle_state();
    }
}

void NetworkGroupScheduler::force_idle_state()
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        m_write_read_cv.wait(lock, [this] {
            return has_current_ng_finished();
        });
        deactivate_network_group();
        m_forced_idle_state = true;
    }
    m_write_read_cv.notify_all();
}

void NetworkGroupScheduler::resume_from_idle_state()
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        m_forced_idle_state = false;
    }
    m_write_read_cv.notify_all();
}

Expected<network_group_handle_t> NetworkGroupScheduler::add_network_group(std::weak_ptr<ConfiguredNetworkGroup> added_cng)
{
    network_group_handle_t network_group_handle = INVALID_NETWORK_GROUP_HANDLE;
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        network_group_handle = static_cast<uint32_t>(m_cngs.size());

        m_cngs.emplace_back(added_cng);
        m_first_run_time_stamp[network_group_handle] = {}; // Default c'tor to mark this network_group didn't run yet
        m_timeout_per_network_group[network_group_handle] = make_shared_nothrow<std::chrono::milliseconds>(DEFAULT_SCHEDULER_TIMEOUT);
        CHECK_AS_EXPECTED(nullptr != m_timeout_per_network_group[network_group_handle], HAILO_OUT_OF_HOST_MEMORY);
        auto timeout_cpy = m_timeout_per_network_group[network_group_handle];

        m_timeout_passed_per_network_group[network_group_handle] = make_shared_nothrow<std::atomic_bool>(false);
        CHECK_AS_EXPECTED(nullptr != m_timeout_passed_per_network_group[network_group_handle], HAILO_OUT_OF_HOST_MEMORY);
        auto timeout_passed_cpy = m_timeout_passed_per_network_group[network_group_handle];

        m_timer_threads_per_network_group[network_group_handle] =
            make_unique_nothrow<ReusableThread>([this, timeout_passed_cpy, timeout_cpy] ()
                {
                    timeout_passed_cpy->store(false);
                    std::this_thread::sleep_for(*timeout_cpy);
                    timeout_passed_cpy->store(true);
                    m_write_read_cv.notify_all();
                }
            );
        CHECK_AS_EXPECTED(nullptr != m_timer_threads_per_network_group[network_group_handle], HAILO_OUT_OF_HOST_MEMORY);

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
                m_finished_sent_pending_buffer[network_group_handle][stream_info.name] = 0;

                auto event = Event::create_shared(Event::State::signalled);
                CHECK_AS_EXPECTED(nullptr != event, HAILO_OUT_OF_HOST_MEMORY);

                m_write_buffer_events[network_group_handle][stream_info.name] = event;
            } else {
                m_requested_read[network_group_handle][stream_info.name] = 0;
                m_finished_read[network_group_handle][stream_info.name] = 0;
            }
        }
    }
    m_write_read_cv.notify_all();
    return network_group_handle;
}

hailo_status NetworkGroupScheduler::wait_for_write(const network_group_handle_t &network_group_handle, const std::string &stream_name)
{
    while (true) {
        auto status = block_write_if_needed(network_group_handle, stream_name);
        CHECK_SUCCESS(status);

        m_write_read_cv.notify_all();

        status = m_write_buffer_events[network_group_handle][stream_name]->wait(std::chrono::milliseconds(HAILO_INFINITE));
        CHECK_SUCCESS(status);

        {
            std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
            auto should_wait_again = should_wait_for_write_again(network_group_handle, stream_name);
            if (HAILO_STREAM_INTERNAL_ABORT == should_wait_again.status()) {
                return HAILO_STREAM_INTERNAL_ABORT;
            }
            CHECK_EXPECTED_AS_STATUS(should_wait_again);

            if (!should_wait_again.value()) {
                m_requested_write[network_group_handle][stream_name]++;

                if ((nullptr != m_ang) && (m_current_network_group == network_group_handle)) {
                    m_has_current_ng_finished = false;
                }

                status = allow_writes_for_other_inputs_if_needed(network_group_handle);
                CHECK_SUCCESS(status);
                break;
            }
        }
    }

    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::block_write_if_needed(const network_group_handle_t &network_group_handle, const std::string &stream_name)
{
    std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
    assert(contains(m_requested_write, network_group_handle));
    assert(contains(m_sent_pending_buffer, network_group_handle));
    assert(contains(m_max_batch_size, network_group_handle));
    assert(contains(m_write_buffer_events, network_group_handle));

    auto pending_buffers = m_requested_write[network_group_handle][stream_name] - m_sent_pending_buffer[network_group_handle][stream_name];
    bool has_written_max_batch_size = ((CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE != m_max_batch_size[network_group_handle]) &&
        (m_max_batch_size[network_group_handle] == pending_buffers));
    bool should_stop_writing_because_switching = ((nullptr != m_ang) && (m_is_switching_network_group || m_is_currently_transferring_batch) &&
        (network_group_handle == m_current_network_group) && has_input_written_most_frames(network_group_handle, stream_name));

    if (has_written_max_batch_size || should_stop_writing_because_switching) {
        auto status = m_write_buffer_events[network_group_handle][stream_name]->reset();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::has_input_written_most_frames(const network_group_handle_t &network_group_handle, const std::string &stream_name)
{
    return m_requested_write[network_group_handle][stream_name] == get_max_value_of_unordered_map(m_requested_write[network_group_handle]);
}

Expected<bool> NetworkGroupScheduler::should_wait_for_write_again(const network_group_handle_t &network_group_handle, const std::string &stream_name)
{
    assert(contains(m_should_ng_stop, network_group_handle));
    if (m_should_ng_stop[network_group_handle][stream_name]) {
        return make_unexpected(HAILO_STREAM_INTERNAL_ABORT);
    }

    if ((nullptr != m_ang) && (network_group_handle == m_current_network_group) && m_is_currently_transferring_batch &&
        has_input_written_most_frames(network_group_handle, stream_name)) {
        return true;
    }

    return false;
}

hailo_status NetworkGroupScheduler::allow_writes_for_other_inputs_if_needed(const network_group_handle_t &network_group_handle)
{
    if (!m_has_current_ng_finished && m_is_switching_network_group) {
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

hailo_status NetworkGroupScheduler::signal_write_finish(const network_group_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);

        assert(contains(m_should_ng_stop, network_group_handle));
        if (m_should_ng_stop[network_group_handle][stream_name]) {
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

hailo_status NetworkGroupScheduler::switch_network_group_if_idle(const network_group_handle_t &network_group_handle,
    std::unique_lock<std::mutex> &read_write_lock)
{
    if (!m_forced_idle_state && !m_is_switching_network_group && m_has_current_ng_finished &&
        ((nullptr == m_ang) || (network_group_handle != m_current_network_group)) && is_network_group_ready(network_group_handle)) {
        auto status = activate_network_group(network_group_handle, read_write_lock);
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::activate_network_group(const network_group_handle_t &network_group_handle,
    std::unique_lock<std::mutex> &read_write_lock)
{
    deactivate_network_group();

    m_is_switching_network_group = false;
    m_is_currently_transferring_batch = false;
    auto status = allow_all_writes();
    CHECK_SUCCESS(status);

    assert(m_cngs.size() > network_group_handle);
    auto cng = m_cngs[network_group_handle].lock();
    CHECK(cng, HAILO_INTERNAL_FAILURE);

    auto cng_base = std::dynamic_pointer_cast<ConfiguredNetworkGroupBase>(cng);

    uint16_t batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE;
    if (CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE != m_max_batch_size[network_group_handle]) {
        batch_size = static_cast<uint16_t>(get_max_value_of_unordered_map(m_requested_write[network_group_handle]));

        if (batch_size > HAILO_DEFAULT_BATCH_SIZE) {
            m_is_currently_transferring_batch = true;
        }
    }

    auto expected_ang = cng_base->force_activate(batch_size);
    CHECK_EXPECTED_AS_STATUS(expected_ang);
    m_first_run_time_stamp[network_group_handle] = std::chrono::steady_clock::now(); // Mark first timestamp on activation

    m_ang = expected_ang.release();

    m_current_network_group = network_group_handle;
    m_has_current_ng_finished = false;

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

hailo_status NetworkGroupScheduler::send_all_pending_buffers(const network_group_handle_t &network_group_handle,
    std::unique_lock<std::mutex> &read_write_lock)
{
    if ((nullptr == m_ang) || (m_current_network_group != network_group_handle)) {
        return HAILO_SUCCESS;
    }

    while (true) {
        uint32_t finished_sending_count = 0;
        for (auto &name_counter_pair : m_written_buffer[network_group_handle]) {
            if (m_sent_pending_buffer[network_group_handle][name_counter_pair.first] < name_counter_pair.second) {
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

hailo_status NetworkGroupScheduler::send_pending_buffer(const network_group_handle_t &network_group_handle, const std::string &stream_name,
    std::unique_lock<std::mutex> &read_write_lock)
{
    assert(m_cngs.size() > network_group_handle);
    auto current_cng = m_cngs[network_group_handle].lock();
    CHECK(current_cng, HAILO_INTERNAL_FAILURE);

    auto input_stream = current_cng->get_input_stream_by_name(stream_name);
    CHECK_EXPECTED_AS_STATUS(input_stream);

    m_has_current_ng_finished = false;

    InputStreamBase &vdevice_input = dynamic_cast<InputStreamBase&>(input_stream->get());
    auto pending_buffer_state = vdevice_input.send_pending_buffer();
    CHECK_EXPECTED_AS_STATUS(pending_buffer_state);

    assert(contains(m_sent_pending_buffer, network_group_handle));
    m_sent_pending_buffer[network_group_handle][stream_name]++;

    auto status = pending_buffer_state->finish(vdevice_input.get_timeout(), read_write_lock);
    if (HAILO_STREAM_INTERNAL_ABORT == status) {
        LOGGER__INFO("finish has failed with status=HAILO_STREAM_INTERNAL_ABORT");
        return status;
    }
    CHECK_SUCCESS(status);
    
    assert(contains(m_finished_sent_pending_buffer, network_group_handle));
    m_finished_sent_pending_buffer[network_group_handle][stream_name]++;

    // Update m_has_current_ng_finished here because after finishing send pending buffer the network group can actually be finished
    m_has_current_ng_finished = has_current_ng_finished();

    return HAILO_SUCCESS;
}

void NetworkGroupScheduler::deactivate_network_group()
{
    if (m_ang) {
        reset_current_ng_counters();
        m_ang.reset();
        assert(contains(m_timer_threads_per_network_group, m_current_network_group));
        m_timer_threads_per_network_group[m_current_network_group]->restart();
    }
}

hailo_status NetworkGroupScheduler::switch_network_group_if_should_be_next(const network_group_handle_t &network_group_handle,
    std::unique_lock<std::mutex> &read_write_lock)
{
    // Checking (nullptr == m_ang) for activating the first time the scheduler is running
    if (!m_forced_idle_state && m_is_switching_network_group && m_has_current_ng_finished &&
        (((nullptr == m_ang) && is_network_group_ready(network_group_handle)) || (m_next_network_group == network_group_handle))) {
        auto status = activate_network_group(network_group_handle, read_write_lock);
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::is_network_group_ready(const network_group_handle_t &network_group_handle)
{
    assert(contains(m_written_buffer, network_group_handle));
    assert(contains(m_min_threshold_per_stream, network_group_handle));
    assert(contains(m_timeout_passed_per_network_group, network_group_handle));

    for (auto &name_counter_pair : m_written_buffer[network_group_handle]) {
        // Check if there arent any write requests
        if (0 == name_counter_pair.second) {
            return false;
        }

        // Check if there arent enough write requests and timeout didnt passed
        if ((name_counter_pair.second < m_min_threshold_per_stream[network_group_handle][name_counter_pair.first]) && (!(m_timeout_passed_per_network_group[network_group_handle]->load()))) {
            return false;
        }
    }

    return true;
}

hailo_status NetworkGroupScheduler::wait_for_read(const network_group_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        assert(contains(m_requested_read, network_group_handle));
        assert(contains(m_requested_read[network_group_handle], stream_name));

        hailo_status status = HAILO_UNINITIALIZED;
        m_write_read_cv.wait(lock, [this, network_group_handle, stream_name, &status, &lock] {
            assert(contains(m_should_ng_stop, network_group_handle));
            if (m_should_ng_stop[network_group_handle][stream_name]) {
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

        assert(contains(m_requested_read, network_group_handle));
        m_requested_read[network_group_handle][stream_name]++;
    }
    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::can_stream_read(const network_group_handle_t &network_group_handle, const std::string &stream_name)
{
    if (nullptr == m_ang) {
        return false;
    }

    if (m_current_network_group != network_group_handle) {
        return false;
    }

    if (m_has_current_ng_finished) {
        return false;
    }

    assert(contains(m_requested_read, network_group_handle));
    assert(contains(m_sent_pending_buffer, network_group_handle));
    return m_requested_read[network_group_handle][stream_name].load() < get_max_value_of_unordered_map(m_sent_pending_buffer[network_group_handle]);
}

hailo_status NetworkGroupScheduler::signal_read_finish(const network_group_handle_t &network_group_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        assert(contains(m_finished_read, network_group_handle));
        assert(contains(m_finished_read[network_group_handle], stream_name));

        m_finished_read[network_group_handle][stream_name]++;

        m_has_current_ng_finished = has_current_ng_finished();
        if ((!m_is_currently_transferring_batch) || m_has_current_ng_finished) {
            hailo_status status = choose_next_network_group();
            CHECK_SUCCESS(status);
        }

        // Prevents integer overflow of the counters
        decrease_current_ng_counters();

        if (m_is_currently_transferring_batch && m_has_current_ng_finished && (!m_is_switching_network_group)) {
            m_next_network_group = INVALID_NETWORK_GROUP_HANDLE;
            m_is_currently_transferring_batch = false;
            m_is_switching_network_group = false;
            deactivate_network_group();
            auto status = allow_all_writes();
            CHECK_SUCCESS(status);
        }
    }
    m_write_read_cv.notify_all();

    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::has_current_ng_finished()
{
    uint32_t written_frames = get_max_value_of_unordered_map(m_requested_write[m_current_network_group]);
    for (auto &name_counter_pair : m_finished_read[m_current_network_group]) {
        if (name_counter_pair.second < written_frames) {
            return false;
        }
    }
    for (auto &name_counter_pair : m_finished_sent_pending_buffer[m_current_network_group]) {
        if (name_counter_pair.second < written_frames) {
            return false;
        }
    }
    return true;
}

void NetworkGroupScheduler::reset_current_ng_counters()
{
    uint32_t written_frames = get_max_value_of_unordered_map(m_sent_pending_buffer[m_current_network_group]);

    for (auto &name_counter_pair : m_requested_write[m_current_network_group]) {
        name_counter_pair.second -= written_frames;
    }
    for (auto &name_counter_pair : m_written_buffer[m_current_network_group]) {
        assert(name_counter_pair.second == written_frames);
        name_counter_pair.second = 0;
    }
    for (auto &name_counter_pair : m_sent_pending_buffer[m_current_network_group]) {
        assert(name_counter_pair.second == written_frames);
        name_counter_pair.second = 0;
    }
    for (auto &name_counter_pair : m_finished_sent_pending_buffer[m_current_network_group]) {
        assert(name_counter_pair.second == written_frames);
        name_counter_pair.second = 0;
    }
    for (auto &name_counter_pair : m_requested_read[m_current_network_group]) {
        // TODO (HRT-6811): Recover from timeout, verify counters
        name_counter_pair.second = 0;
    }
    for (auto &name_counter_pair : m_finished_read[m_current_network_group]) {
        // TODO (HRT-6811): Recover from timeout, verify counters
        name_counter_pair.second = 0;
    }
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
    for (auto &name_counter_pair : m_finished_read[m_current_network_group]) {
        name_counter_pair.second--;
    }
}

hailo_status NetworkGroupScheduler::enable_stream(const network_group_handle_t &network_group_handle, const std::string &stream_name)
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

hailo_status NetworkGroupScheduler::disable_stream(const network_group_handle_t &network_group_handle, const std::string &stream_name)
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

hailo_status NetworkGroupScheduler::set_timeout(const network_group_handle_t &network_group_handle, const std::chrono::milliseconds &timeout, const std::string &network_name)
{
    (void)network_name;

    assert(contains(m_timeout_per_network_group, network_group_handle));
    assert(contains(m_first_run_time_stamp, network_group_handle));
    CHECK((std::chrono::time_point<std::chrono::steady_clock>() == m_first_run_time_stamp[network_group_handle]), HAILO_INVALID_OPERATION,
        "Setting scheduler timeout is allowed only before sending / receiving frames on the network group.");
    *(m_timeout_per_network_group[network_group_handle]) = timeout;

    assert(m_cngs.size() > network_group_handle);
    auto cng = m_cngs[network_group_handle].lock();
    CHECK(cng, HAILO_INTERNAL_FAILURE);

    auto name = (network_name.empty()) ? cng->get_network_group_name() : network_name;
    LOGGER__INFO("Setting scheduler timeout of {} to {}ms", name, timeout.count());

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::set_threshold(const network_group_handle_t &network_group_handle, uint32_t threshold, const std::string &network_name)
{
    (void)network_name;

    assert(contains(m_min_threshold_per_stream, network_group_handle));
    assert(contains(m_first_run_time_stamp, network_group_handle));
    CHECK((std::chrono::time_point<std::chrono::steady_clock>() == m_first_run_time_stamp[network_group_handle]), HAILO_INVALID_OPERATION,
        "Setting scheduler threshold is allowed only before sending / receiving frames on the network group.");
    for (auto &threshold_per_stream_pair : m_min_threshold_per_stream[network_group_handle]) {
        threshold_per_stream_pair.second = threshold;
    }

    assert(m_cngs.size() > network_group_handle);
    auto cng = m_cngs[network_group_handle].lock();
    CHECK(cng, HAILO_INTERNAL_FAILURE);

    auto name = (network_name.empty()) ? cng->get_network_group_name() : network_name;
    LOGGER__INFO("Setting scheduler threshold of {} to {} frames", name, threshold);

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupSchedulerRoundRobin::choose_next_network_group()
{
    if (!m_is_switching_network_group) {
        for (uint32_t i = 0; i < m_cngs.size() - 1; i++) {
            uint32_t index = m_current_network_group + i + 1;
            index %= static_cast<uint32_t>(m_cngs.size());

            if (is_network_group_ready(index)) {
                m_is_switching_network_group = true;
                m_next_network_group = index;
                break;
            }
        }
    }

    return HAILO_SUCCESS;
}

} /* namespace hailort */
