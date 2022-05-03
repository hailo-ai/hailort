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

namespace hailort
{

Expected<NetworkGroupSchedulerPtr> NetworkGroupScheduler::create_shared(hailo_scheduling_algorithm_t algorithm)
{
    auto ptr = make_shared_nothrow<NetworkGroupScheduler>(algorithm);
    CHECK_AS_EXPECTED(nullptr != ptr, HAILO_OUT_OF_HOST_MEMORY);

    return ptr;
}

hailo_status NetworkGroupScheduler::add_network_group(std::weak_ptr<ConfiguredNetworkGroup> added_cng) {
    m_cngs.emplace_back(added_cng);

    auto added_cng_ptr = added_cng.lock();
    CHECK(added_cng_ptr, HAILO_INTERNAL_FAILURE);
    const auto &network_group_name = added_cng_ptr->get_network_group_name();

    m_index_by_cng_name[network_group_name] = static_cast<uint32_t>(m_cngs.size() - 1);
    m_cngs_by_name.emplace(network_group_name, added_cng);

    auto network_infos = added_cng_ptr->get_network_infos();
    CHECK_EXPECTED_AS_STATUS(network_infos);

    auto cng_base = std::dynamic_pointer_cast<ConfiguredNetworkGroupBase>(added_cng_ptr);

    for (const auto &network_info : network_infos.value()) {
        auto input_streams = added_cng_ptr->get_input_streams_by_network(network_info.name);
        CHECK_EXPECTED_AS_STATUS(input_streams);

        for (const auto &input_stream : input_streams.value()) {
            const auto &name = input_stream.get().name();
            m_got_write[network_group_name][name] = 0;
            m_should_ng_stop[network_group_name][name] = false;

            auto batch_size = cng_base->get_stream_batch_size(name);
            CHECK_EXPECTED_AS_STATUS(batch_size);
            m_batch_size_per_stream[network_group_name][name] = batch_size.value();
        }
        
        auto output_streams = added_cng_ptr->get_output_streams_by_network(network_info.name);
        CHECK_EXPECTED_AS_STATUS(output_streams);

        for (const auto &output_stream : output_streams.value()) {
            const auto &name = output_stream.get().name();
            m_got_read[network_group_name][name] = 0;
            m_finished_read[network_group_name][name] = 0;
            m_should_ng_stop[network_group_name][name] = false;

            auto batch_size = cng_base->get_stream_batch_size(name);
            CHECK_EXPECTED_AS_STATUS(batch_size);
            m_batch_size_per_stream[network_group_name][name] = batch_size.value();
        }
    }

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::wait_for_write(const std::string &network_group_name, const std::string &stream_name)
{
    assert((m_got_write.find(network_group_name) != m_got_write.end()) &&
        (m_got_write[network_group_name].find(stream_name) != m_got_write[network_group_name].end()));
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        m_got_write[network_group_name][stream_name]++;
        hailo_status status = check_for_first_activation(network_group_name);
        CHECK_SUCCESS(status);

        m_cv.wait(lock, [this, network_group_name, stream_name] {
            auto cng = m_cngs[m_current_network_group].lock();
            if (!cng) {
                LOGGER__ERROR("Configured network group pointer is invalid!");
                return true; // return true so that the wait will finish
            }
            return (m_should_ng_stop[network_group_name][stream_name] ||
                ((nullptr != m_ang) && m_is_current_ng_ready && (network_group_name == cng->get_network_group_name()) &&
                    (m_got_write[network_group_name][stream_name] <= m_batch_size_per_stream[network_group_name][stream_name])));
        });
    }
    m_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::check_for_first_activation(const std::string &network_group_name)
{
    if (nullptr != m_ang) {
        auto current_cng = m_cngs[m_current_network_group].lock();
        CHECK(current_cng, HAILO_INTERNAL_FAILURE);

        if (is_network_group_ready(current_cng->get_network_group_name())) {
            m_is_current_ng_ready = true;
            return HAILO_SUCCESS;
        }
    }

    if (is_network_group_ready(network_group_name)) {
        m_ang.reset();

        auto cng = m_cngs_by_name[network_group_name].lock();
        CHECK(cng, HAILO_INTERNAL_FAILURE);

        auto cng_base = std::dynamic_pointer_cast<ConfiguredNetworkGroupBase>(cng);
        auto expected = cng_base->force_activate();
        CHECK_EXPECTED_AS_STATUS(expected);

        m_ang = expected.release();
        m_current_network_group = m_index_by_cng_name[network_group_name];
        m_is_current_ng_ready = true; // We just activated the network group so we don't need to wait for it to be ready
    }

    return HAILO_SUCCESS;
}

bool NetworkGroupScheduler::is_network_group_ready(const std::string &network_group_name)
{
    for (auto &name_counter_pair : m_got_write[network_group_name]) {
        if (0 == name_counter_pair.second) {
            return false;
        }
    }

    for (auto &name_counter_pair : m_got_read[network_group_name]) {
        if (0 == name_counter_pair.second) {
            return false;
        }
    }

    return true;
}

hailo_status NetworkGroupScheduler::wait_for_read(const std::string &network_group_name, const std::string &stream_name)
{
    assert((m_got_read.find(network_group_name) != m_got_read.end()) &&
        (m_got_read[network_group_name].find(stream_name) != m_got_read[network_group_name].end()));
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        m_got_read[network_group_name][stream_name]++;
        hailo_status status = check_for_first_activation(network_group_name);
        CHECK_SUCCESS(status);

        m_cv.wait(lock, [this, network_group_name, stream_name] {
            auto cng = m_cngs[m_current_network_group].lock();
            if (!cng) {
                LOGGER__ERROR("Configured network group pointer is invalid!");
                return true; // return true so that the wait will finish
            }
            return (m_should_ng_stop[network_group_name][stream_name] ||
                ((nullptr != m_ang) && m_is_current_ng_ready && (network_group_name == cng->get_network_group_name()) &&
                    (m_got_read[network_group_name][stream_name] <= m_batch_size_per_stream[network_group_name][stream_name])));
        });
    }
    m_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::signal_read_finish(const std::string &network_group_name, const std::string &stream_name)
{
    assert((m_finished_read.find(network_group_name) != m_finished_read.end()) &&
        (m_finished_read[network_group_name].find(stream_name) != m_finished_read[network_group_name].end()));

    std::unique_lock<std::mutex> lock(m_after_read_mutex);
    m_finished_read[network_group_name][stream_name]++;

    bool has_current_ng_finished_batch = true;
    for (auto &name_counter_pair : m_finished_read[network_group_name]) {
        auto cng = m_cngs_by_name[network_group_name].lock();
        CHECK(cng, HAILO_INTERNAL_FAILURE);

        if (name_counter_pair.second < m_batch_size_per_stream[network_group_name][name_counter_pair.first]) {
            has_current_ng_finished_batch = false;
            break;
        }
    }

    if (has_current_ng_finished_batch) {
        hailo_status status = switch_network_group_if_ready(network_group_name);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::switch_network_group_if_ready(const std::string &old_network_group_name)
{
    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        m_is_current_ng_ready = false;
        for (auto &name_counter_pair : m_got_write[old_network_group_name]) {
            name_counter_pair.second -= m_batch_size_per_stream[old_network_group_name][name_counter_pair.first];
        }
        for (auto &name_counter_pair : m_got_read[old_network_group_name]) {
            name_counter_pair.second -= m_batch_size_per_stream[old_network_group_name][name_counter_pair.first];
        }
        for (auto &name_counter_pair : m_finished_read[old_network_group_name]) {
            name_counter_pair.second -= m_batch_size_per_stream[old_network_group_name][name_counter_pair.first];
        }

        for (uint32_t i = 0; i < m_cngs.size() - 1; i++) {
            uint32_t index = m_current_network_group + i + 1;
            index %= static_cast<uint32_t>(m_cngs.size());

            auto cng = m_cngs[index].lock();
            CHECK(cng, HAILO_INTERNAL_FAILURE);

            if (is_network_group_ready(cng->get_network_group_name())) {
                m_ang.reset();

                auto cng_base = std::dynamic_pointer_cast<ConfiguredNetworkGroupBase>(cng);
                auto expected = cng_base->force_activate();
                CHECK_EXPECTED_AS_STATUS(expected);

                m_ang = expected.release();
                m_current_network_group = index;
                m_is_current_ng_ready = true; // We just activated the network group so we don't need to wait for it to be ready
                break;
            }
        }
    }
    m_cv.notify_all();
    
    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::enable_stream(const std::string &network_group_name, const std::string &stream_name)
{
    if (!m_should_ng_stop[network_group_name][stream_name]) {
        return HAILO_SUCCESS;
    }

    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        m_should_ng_stop[network_group_name][stream_name] = false;
    }
    m_cv.notify_all();
    return HAILO_SUCCESS;
}

hailo_status NetworkGroupScheduler::disable_stream(const std::string &network_group_name, const std::string &stream_name)
{
    if (m_should_ng_stop[network_group_name][stream_name]) {
        return HAILO_SUCCESS;
    }

    {
        std::unique_lock<std::mutex> lock(m_before_read_write_mutex);
        m_should_ng_stop[network_group_name][stream_name] = true;
    }
    m_cv.notify_all();
    return HAILO_SUCCESS;
}

} /* namespace hailort */
