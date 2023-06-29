/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline_multiplexer.cpp
 * @brief: Pipeline Multiplexer
 **/

#include "hailo/hailort_common.hpp"
#include "hailo/vstream.hpp"

#include "common/utils.hpp"

#include "vdevice/pipeline_multiplexer.hpp"


namespace hailort
{

PipelineMultiplexer::PipelineMultiplexer() :
    m_should_core_op_stop(),
    m_input_streams_count(0),
    m_output_streams_count(0),
    m_next_to_write(0),
    m_order_queue(),
    m_currently_writing(INVALID_CORE_OP_HANDLE),
    m_written_streams_count(0),
    m_read_streams_count(0),
    m_next_to_read_after_drain(INVALID_CORE_OP_HANDLE)
{
    assert(is_multiplexer_supported());
}

bool PipelineMultiplexer::is_multiplexer_supported()
{
    auto disable_multiplexer_env = std::getenv(DISABLE_MULTIPLEXER_ENV_VAR);
    if ((nullptr != disable_multiplexer_env) && (strnlen(disable_multiplexer_env, 2) == 1) && (strncmp(disable_multiplexer_env, "1", 1) == 0)) {
        LOGGER__WARNING("Usage of '{}' env variable is deprecated.", DISABLE_MULTIPLEXER_ENV_VAR);
        return false;
    }
    return true;
}

hailo_status PipelineMultiplexer::add_core_op_instance(multiplexer_core_op_handle_t core_op_handle, CoreOp &core_op)
{
    std::unique_lock<std::mutex> lock(m_writing_mutex);
    std::unique_lock<std::mutex> read_lock(m_reading_mutex);
    assert(!contains(m_should_core_op_stop, core_op_handle));

    auto is_first_instance = (0 == instances_count());

    auto stream_infos = core_op.get_all_stream_infos();
    CHECK_EXPECTED_AS_STATUS(stream_infos);

    for (const auto &stream_info : stream_infos.value()) {
        m_should_core_op_stop[core_op_handle][stream_info.name] = false;
        if (is_first_instance) {
            // To be filled only on first instance
            if (HAILO_H2D_STREAM == stream_info.direction) {
                m_input_streams_count++;
            } else {
                m_output_streams_count++;
                m_is_stream_reading[stream_info.name] = false;
            }
        }
    }

    m_write_barriers[core_op_handle] = make_shared_nothrow<Barrier>(m_input_streams_count);
    CHECK(nullptr != m_write_barriers[core_op_handle], HAILO_OUT_OF_HOST_MEMORY);
    m_is_waiting_to_write[core_op_handle] = false;

    return HAILO_SUCCESS;
}

void PipelineMultiplexer::set_output_vstreams_names(multiplexer_core_op_handle_t core_op_handle, const std::vector<OutputVStream> &output_vstreams)
{
    std::unique_lock<std::mutex> lock(m_writing_mutex);
    for (const auto &output_vstream : output_vstreams) {
        m_can_output_vstream_read[core_op_handle][output_vstream.name()] = true;
    }
    m_can_core_op_read[core_op_handle] = true;
}

bool PipelineMultiplexer::has_more_than_one_core_op_instance() const
{
    return instances_count() > 1;
}

size_t PipelineMultiplexer::instances_count() const
{
    return m_should_core_op_stop.size();
}

bool PipelineMultiplexer::should_core_op_stop(multiplexer_core_op_handle_t core_op_handle)
{
    for (const auto &name_flag_pair : m_should_core_op_stop[core_op_handle]) {
        if (name_flag_pair.second) {
            return true;
        }
    }

    return false;
}

hailo_status PipelineMultiplexer::wait_for_write(multiplexer_core_op_handle_t core_op_handle)
{
    std::shared_ptr<hailort::Barrier> barrier;
    {
        std::unique_lock<std::mutex> lock(m_writing_mutex);
        assert(contains(m_write_barriers, core_op_handle));
        barrier = m_write_barriers[core_op_handle];
    }
    // TODO: This has no timeout
    // TODO: HRT-8634
    barrier->arrive_and_wait();
    {
        std::unique_lock<std::mutex> lock(m_writing_mutex);
        assert(contains(m_should_core_op_stop, core_op_handle));
        assert(contains(m_is_waiting_to_write, core_op_handle));

        m_is_waiting_to_write[core_op_handle] = true;
        hailo_status status = HAILO_SUCCESS;
        m_writing_cv.wait(lock, [this, core_op_handle, &status] {
            if (!has_more_than_one_core_op_instance()) {
                return true;
            }

            if (should_core_op_stop(core_op_handle)) {
                status = HAILO_STREAM_ABORTED_BY_USER;
                return true; // return true so that the wait will finish
            }

            if (m_currently_writing == core_op_handle) {
                return true;
            }

            if (!can_core_op_read(core_op_handle)) {
                return false;
            }

            if (INVALID_CORE_OP_HANDLE == m_currently_writing) {
                if ((m_next_to_write != core_op_handle) && m_is_waiting_to_write[m_next_to_write] && can_core_op_read(m_next_to_write)) {
                    return false;
                }

                return true;
            }

            return false;
        });
        m_is_waiting_to_write[core_op_handle] = false;

        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            return status;
        }
        CHECK_SUCCESS(status);

        if (INVALID_CORE_OP_HANDLE == m_currently_writing) {
            m_currently_writing = core_op_handle;
            m_next_to_write = m_currently_writing;
        }
    }
    m_writing_cv.notify_all();

    return HAILO_SUCCESS;
}

bool PipelineMultiplexer::can_core_op_read(multiplexer_core_op_handle_t core_op_handle)
{
    if (should_core_op_stop(core_op_handle)) {
        return false;
    }

    if (!contains(m_can_core_op_read, core_op_handle)) {
        return true;
    }

    return m_can_core_op_read[core_op_handle];
}

hailo_status PipelineMultiplexer::signal_write_finish(multiplexer_core_op_handle_t core_op_handle, bool did_write_fail)
{
    {
        std::unique_lock<std::mutex> lock(m_writing_mutex);
        m_written_streams_count++;
        if (m_written_streams_count == m_input_streams_count) {
            m_written_streams_count = 0;
            m_currently_writing = INVALID_CORE_OP_HANDLE;
            m_next_to_write++;
            m_next_to_write %= static_cast<uint32_t>(instances_count());

            if (!did_write_fail) {
                std::unique_lock<std::mutex> reading_lock(m_reading_mutex);
                m_order_queue.push_back(core_op_handle);
            }
            m_reading_cv.notify_all();
        }
    }

    m_writing_cv.notify_all();
    return HAILO_SUCCESS;
}

Expected<uint32_t> PipelineMultiplexer::wait_for_read(multiplexer_core_op_handle_t core_op_handle, const std::string &stream_name,
    const std::chrono::milliseconds &timeout)
{
    uint32_t drain_frames = 0;

    {
        std::unique_lock<std::mutex> lock(m_reading_mutex);

        assert(contains(m_should_core_op_stop, core_op_handle));
        assert(contains(m_is_stream_reading, stream_name));

        hailo_status status = HAILO_SUCCESS;
        auto wait_res = m_reading_cv.wait_for(lock, timeout, [this, core_op_handle, stream_name, &drain_frames, &status] {
            if (m_should_core_op_stop[core_op_handle][stream_name]) {
                status = HAILO_STREAM_ABORTED_BY_USER;
                return true; // return true so that the wait will finish
            }
            if (m_is_stream_reading[stream_name]) {
                return false;
            }

            if (m_next_to_read_after_drain == core_op_handle) {
                drain_frames = m_num_frames_to_drain[stream_name];
                return true;
            }

            if (m_order_queue.empty()) {
                return false;
            }

            if (m_order_queue.front() != core_op_handle) {
                if (!should_core_op_stop(m_order_queue.front())) {
                    return false;
                }

                // This means the NG that is currently writing was aborted so we have to wait for it to finish processing its frames
                if ((INVALID_CORE_OP_HANDLE != m_currently_writing) && (m_currently_writing != core_op_handle)) {
                    return false;
                }

                uint32_t max_drain_count = get_frame_count_to_drain(core_op_handle);
                if (0 == max_drain_count) {
                    return false;
                }

                drain_frames = drain_aborted_in_order_queue(core_op_handle, stream_name, max_drain_count);
            }

            return true;
        });
        CHECK_AS_EXPECTED(wait_res, HAILO_TIMEOUT, "{} (D2H) failed with status={}, timeout={}ms", stream_name, HAILO_TIMEOUT, timeout.count());
        if (HAILO_STREAM_ABORTED_BY_USER == status) {
            return make_unexpected(status);
        }
        CHECK_SUCCESS_AS_EXPECTED(status);

        m_is_stream_reading[stream_name] = true;
    }

    m_reading_cv.notify_all();
    return drain_frames;
}

uint32_t PipelineMultiplexer::get_frame_count_to_drain(multiplexer_core_op_handle_t core_op_handle)
{
    uint32_t drain_count = 0;
    for (const auto &handle : m_order_queue) {
        if (!should_core_op_stop(handle)) {
            if (handle == core_op_handle) {
                // Current instance is in the front after draining
                break;
            } else {
                // Someone else should drain these frames, the current instance won't be in front after draining
                return 0;
            }
        }

        drain_count++;
    }

    return drain_count;
}

uint32_t PipelineMultiplexer::drain_aborted_in_order_queue(multiplexer_core_op_handle_t core_op_handle, const std::string &stream_name,
    uint32_t max_drain_count)
{
    // In case of multiple outputs where one or more already read the frame we need to drain one less frame
     for (auto &name_flag_pair : m_is_stream_reading) {
        if (name_flag_pair.second) {
            m_num_frames_to_drain[name_flag_pair.first] = max_drain_count - 1;
        } else {
            m_num_frames_to_drain[name_flag_pair.first] = max_drain_count;
        }
    }

    m_next_to_read_after_drain = core_op_handle;
    m_read_streams_count = 0;
    for (uint32_t i = 0; i < max_drain_count; i++) {
        for (auto &name_flag_pair : m_is_stream_reading) {
            name_flag_pair.second = false;
        }
        m_order_queue.pop_front();
    }

    return m_num_frames_to_drain[stream_name];
}

hailo_status PipelineMultiplexer::signal_read_finish()
{
    std::unique_lock<std::mutex> lock(m_reading_mutex);

    m_read_streams_count++;
    if (m_read_streams_count == m_output_streams_count) {
        m_read_streams_count = 0;
        m_order_queue.pop_front();
        for (auto &name_flag_pair : m_is_stream_reading) {
            name_flag_pair.second = false;
        }

        m_next_to_read_after_drain = INVALID_CORE_OP_HANDLE;

        lock.unlock();
        m_reading_cv.notify_all();
    }

    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::enable_stream(multiplexer_core_op_handle_t core_op_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> write_lock(m_writing_mutex);
        std::unique_lock<std::mutex> read_lock(m_reading_mutex);
        assert(contains(m_should_core_op_stop, core_op_handle));
        assert(contains(m_should_core_op_stop[core_op_handle], stream_name));

        if (!m_should_core_op_stop[core_op_handle][stream_name]) {
            return HAILO_SUCCESS;
        }

        m_should_core_op_stop[core_op_handle][stream_name] = false;

        // TODO: should we 'enable' barrier?
    }

    m_writing_cv.notify_all();
    m_reading_cv.notify_all();

    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::disable_stream(multiplexer_core_op_handle_t core_op_handle, const std::string &stream_name)
{
    {
        std::unique_lock<std::mutex> write_lock(m_writing_mutex);
        std::unique_lock<std::mutex> read_lock(m_reading_mutex);
        assert(contains(m_should_core_op_stop, core_op_handle));
        assert(contains(m_should_core_op_stop[core_op_handle], stream_name));

        if (m_should_core_op_stop[core_op_handle][stream_name]) {
            return HAILO_SUCCESS;
        }

        m_should_core_op_stop[core_op_handle][stream_name] = true;

        assert(contains(m_write_barriers, core_op_handle));
        m_write_barriers[core_op_handle]->terminate();
    }

    m_writing_cv.notify_all();
    m_reading_cv.notify_all();

    return HAILO_SUCCESS;
}

void PipelineMultiplexer::RunOnceForStream::add_instance()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_calls_count[static_cast<uint32_t>(m_calls_count.size())] = 0;
}

void PipelineMultiplexer::RunOnceForStream::set_callback(std::function<hailo_status()> callback)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_callback = callback;
}

hailo_status PipelineMultiplexer::RunOnceForStream::run(multiplexer_core_op_handle_t core_op_handle)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    assert(contains(m_calls_count, core_op_handle));

    m_calls_count[core_op_handle]++;
    for (auto &handle_flag_pair : m_calls_count) {
        if (0 == handle_flag_pair.second) {
            return HAILO_SUCCESS;
        }
    }

    for (auto &handle_flag_pair : m_calls_count) {
        handle_flag_pair.second--;
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
    multiplexer_core_op_handle_t core_op_handle)
{
    return m_run_once_db[stream_name][run_once_handle]->run(core_op_handle);
}

void PipelineMultiplexer::set_can_output_vstream_read(multiplexer_core_op_handle_t core_op_handle, const std::string &vstream_name, bool can_read)
{
    {
        std::unique_lock<std::mutex> lock(m_writing_mutex);
        assert(contains(m_can_output_vstream_read, core_op_handle));
        assert(contains(m_can_output_vstream_read[core_op_handle], vstream_name));
        assert(contains(m_can_core_op_read, core_op_handle));

        m_can_output_vstream_read[core_op_handle][vstream_name] = can_read;

        if (can_read != m_can_core_op_read[core_op_handle]) {
            m_can_core_op_read[core_op_handle] = true;
            for (const auto &name_bool_pair :  m_can_output_vstream_read[core_op_handle]) {
                if (!name_bool_pair.second) {
                    m_can_core_op_read[core_op_handle] = false;
                    break;
                }
            }
        }
    }
    m_writing_cv.notify_all();
}

} /* namespace hailort */
