/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline_multiplexer.cpp
 * @brief: Pipeline Multiplexer
 **/

#include "pipeline_multiplexer.hpp"
#include "common/utils.hpp"
#include "hailo/hailort_common.hpp"

namespace hailort
{

PipelineMultiplexer::PipelineMultiplexer() :
    m_next_to_write(0),
    m_order_queue(),
    m_currently_writing(INVALID_NETWORK_GROUP_HANDLE),
    m_written_streams_count(0),
    m_read_streams_count(0)
{}

hailo_status PipelineMultiplexer::add_network_group_instance(multiplexer_ng_handle_t network_group_handle, ConfiguredNetworkGroup &network_group)
{
    std::unique_lock<std::mutex> lock(m_writing_mutex);
    std::unique_lock<std::mutex> read_lock(m_reading_mutex);
    assert(!contains(m_should_ng_stop, network_group_handle));

    m_should_ng_stop[network_group_handle] = false;

    m_input_streams_count = static_cast<uint32_t>(network_group.get_input_streams().size());
    m_output_streams_count = static_cast<uint32_t>(network_group.get_output_streams().size());

    m_write_barriers[network_group_handle] = make_shared_nothrow<Barrier>(m_input_streams_count);
    CHECK(nullptr != m_write_barriers[network_group_handle], HAILO_OUT_OF_HOST_MEMORY);
    m_is_waiting_to_write[network_group_handle] = false;

    for (auto &output_stream : network_group.get_output_streams()) {
        m_is_stream_reading[network_group_handle][output_stream.get().name()] = false;
    }

    return HAILO_SUCCESS;
}

void PipelineMultiplexer::set_output_vstreams_names(multiplexer_ng_handle_t network_group_handle, const std::vector<OutputVStream> &output_vstreams)
{
    std::unique_lock<std::mutex> lock(m_writing_mutex);
    for (const auto &output_vstream : output_vstreams) {
        m_can_output_vstream_read[network_group_handle][output_vstream.name()] = true;
    }
    m_can_network_group_read[network_group_handle] = true;
}

bool PipelineMultiplexer::has_more_than_one_ng_instance() const
{
    return instances_count() > 1;
}

size_t PipelineMultiplexer::instances_count() const
{
    return m_should_ng_stop.size();
}

hailo_status PipelineMultiplexer::wait_for_write(multiplexer_ng_handle_t network_group_handle)
{
    std::shared_ptr<hailort::Barrier> barrier;
    {
        std::unique_lock<std::mutex> lock(m_writing_mutex);
        assert(contains(m_write_barriers, network_group_handle));
        barrier = m_write_barriers[network_group_handle];
    }
    barrier->arrive_and_wait();
    {
        std::unique_lock<std::mutex> lock(m_writing_mutex);
        assert(contains(m_should_ng_stop, network_group_handle));
        assert(contains(m_is_waiting_to_write, network_group_handle));

        m_is_waiting_to_write[network_group_handle] = true;
        m_writing_cv.wait(lock, [this, network_group_handle] {
            if (m_should_ng_stop[network_group_handle]) {
                return true;
            }

            if (m_currently_writing == network_group_handle) {
                return true;
            }

            if (!can_network_group_read(network_group_handle)) {
                return false;
            }

            if (INVALID_NETWORK_GROUP_HANDLE == m_currently_writing) {
                if ((m_next_to_write != network_group_handle) && m_is_waiting_to_write[m_next_to_write] && can_network_group_read(m_next_to_write)) {
                    return false;
                }

                return true;
            }

            return false;
        });
        m_is_waiting_to_write[network_group_handle] = false;

        if (m_should_ng_stop[network_group_handle]) {
            return HAILO_STREAM_INTERNAL_ABORT;
        }

        if (INVALID_NETWORK_GROUP_HANDLE == m_currently_writing) {
            m_currently_writing = network_group_handle;
            {
                std::unique_lock<std::mutex> reading_lock(m_reading_mutex);
                m_order_queue.push(m_currently_writing);
                m_next_to_write = m_currently_writing;
            }
            m_reading_cv.notify_all();
        }
    }
    m_writing_cv.notify_all();

    return HAILO_SUCCESS;
}

bool PipelineMultiplexer::can_network_group_read(multiplexer_ng_handle_t network_group_handle)
{
    if (!contains(m_can_network_group_read, network_group_handle)) {
        return true;
    }

    return m_can_network_group_read[network_group_handle];
}

hailo_status PipelineMultiplexer::signal_write_finish()
{
    std::unique_lock<std::mutex> lock(m_writing_mutex);
    m_written_streams_count++;
    if (m_written_streams_count == m_input_streams_count) {
        m_written_streams_count = 0;
        m_currently_writing = INVALID_NETWORK_GROUP_HANDLE;

        m_next_to_write++;
        m_next_to_write %= static_cast<uint32_t>(instances_count());

        lock.unlock();
        m_writing_cv.notify_all();
    }

    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::wait_for_read(multiplexer_ng_handle_t network_group_handle, const std::string &stream_name)
{
    std::unique_lock<std::mutex> lock(m_reading_mutex);

    assert(contains(m_should_ng_stop, network_group_handle));
    assert(contains(m_is_stream_reading, network_group_handle));
    assert(contains(m_is_stream_reading[network_group_handle], stream_name));

    m_reading_cv.wait(lock, [this, network_group_handle, stream_name] {
        if (m_should_ng_stop[network_group_handle]) {
            return true;
        }

        if (m_order_queue.empty()) {
            return false;
        }

        if (m_order_queue.front() != network_group_handle) {
            return false;
        }

        if (m_is_stream_reading[network_group_handle][stream_name]) {
            return false;
        }

        return true;
    });
    if (m_should_ng_stop[network_group_handle]) {
        return HAILO_STREAM_INTERNAL_ABORT;
    }

    m_is_stream_reading[network_group_handle][stream_name] = true;

    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::signal_read_finish(multiplexer_ng_handle_t network_group_handle)
{
    std::unique_lock<std::mutex> lock(m_reading_mutex);
    assert(contains(m_is_stream_reading, network_group_handle));

    m_read_streams_count++;
    if (m_read_streams_count == m_output_streams_count) {
        m_read_streams_count = 0;
        m_order_queue.pop();

        for (auto &name_flag_pair : m_is_stream_reading[network_group_handle]) {
            name_flag_pair.second = false;
        }

        lock.unlock();
        m_reading_cv.notify_all();
    }

    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::enable_network_group(multiplexer_ng_handle_t network_group_handle)
{
    {
        std::unique_lock<std::mutex> write_lock(m_writing_mutex);
        std::unique_lock<std::mutex> read_lock(m_reading_mutex);
        assert(contains(m_should_ng_stop, network_group_handle));
        if (!m_should_ng_stop[network_group_handle]) {
            return HAILO_SUCCESS;
        }

        m_should_ng_stop[network_group_handle] = false;
    }

    m_writing_cv.notify_all();
    m_reading_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::disable_network_group(multiplexer_ng_handle_t network_group_handle)
{
    {
        std::unique_lock<std::mutex> write_lock(m_writing_mutex);
        std::unique_lock<std::mutex> read_lock(m_reading_mutex);
        assert(contains(m_should_ng_stop, network_group_handle));
        if (m_should_ng_stop[network_group_handle]) {
            return HAILO_SUCCESS;
        }

        m_should_ng_stop[network_group_handle] = true;

        assert(contains(m_write_barriers, network_group_handle));
        m_write_barriers[network_group_handle]->terminate();
    }

    m_writing_cv.notify_all();
    m_reading_cv.notify_all();

    return HAILO_SUCCESS;
}

void PipelineMultiplexer::RunOnceForStream::add_instance()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_was_called[static_cast<uint32_t>(m_was_called.size())] = false;
}

void PipelineMultiplexer::RunOnceForStream::set_callback(std::function<hailo_status()> callback)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_callback = callback;
}

hailo_status PipelineMultiplexer::RunOnceForStream::run(multiplexer_ng_handle_t network_group_handle)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    assert(contains(m_was_called, network_group_handle));

    m_was_called[network_group_handle] = true;
    for (auto &handle_flag_pair : m_was_called) {
        if (!handle_flag_pair.second) {
            return HAILO_SUCCESS;
        }
    }

    for (auto &handle_flag_pair : m_was_called) {
        handle_flag_pair.second = false;
    }

    return m_callback();
}

hailo_status PipelineMultiplexer::register_run_once_for_stream(const std::string &stream_name, run_once_for_stream_handle_t handle,
    std::function<hailo_status()> callback)
{
    std::unique_lock<std::mutex> lock(m_register_run_once_mutex);
    if (!contains(m_run_once_db[stream_name], handle)) {
        m_run_once_db[stream_name][handle] = make_shared_nothrow<RunOnceForStream>();
        CHECK(nullptr != m_run_once_db[stream_name][handle], HAILO_OUT_OF_HOST_MEMORY);

        m_run_once_db[stream_name][handle]->set_callback(callback);
    }

    m_run_once_db[stream_name][handle]->add_instance();

    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::run_once_for_stream(const std::string &stream_name, run_once_for_stream_handle_t run_once_handle,
    multiplexer_ng_handle_t network_group_handle)
{
    return m_run_once_db[stream_name][run_once_handle]->run(network_group_handle);
}

void PipelineMultiplexer::set_can_output_vstream_read(multiplexer_ng_handle_t network_group_handle, const std::string &vstream_name, bool can_read)
{
    {
        std::unique_lock<std::mutex> lock(m_writing_mutex);
        assert(contains(m_can_output_vstream_read, network_group_handle));
        assert(contains(m_can_output_vstream_read[network_group_handle], vstream_name));
        assert(contains(m_can_network_group_read, network_group_handle));

        m_can_output_vstream_read[network_group_handle][vstream_name] = can_read;

        if (can_read != m_can_network_group_read[network_group_handle]) {
            m_can_network_group_read[network_group_handle] = true;
            for (const auto &name_bool_pair :  m_can_output_vstream_read[network_group_handle]) {
                if (!name_bool_pair.second) {
                    m_can_network_group_read[network_group_handle] = false;
                    break;
                }
            }
        }
    }
    m_writing_cv.notify_all();
}

} /* namespace hailort */
