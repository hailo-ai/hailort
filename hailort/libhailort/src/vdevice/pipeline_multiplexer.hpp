/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pipeline_multiplexer.hpp
 * @brief The pipeline multiplexer is a synchronization mechanism that allows communication
 *        between different pipelines that use the same low-level streams.
 **/

#ifndef _HAILO_PIPELINE_MULTIPLEXER_HPP_
#define _HAILO_PIPELINE_MULTIPLEXER_HPP_

#include "hailo/event.hpp"

#include "common/barrier.hpp"

#include "vdevice/scheduler/network_group_scheduler.hpp"

#include <mutex>
#include <queue>


namespace hailort
{

#define DISABLE_MULTIPLEXER_ENV_VAR "HAILO_DISABLE_MULTIPLEXER"

using multiplexer_core_op_handle_t = uint32_t;
using run_once_for_stream_handle_t = uint32_t;

class PipelineMultiplexer
{
public:
    PipelineMultiplexer();

    virtual ~PipelineMultiplexer() = default;
    PipelineMultiplexer(const PipelineMultiplexer &other) = delete;
    PipelineMultiplexer &operator=(const PipelineMultiplexer &other) = delete;
    PipelineMultiplexer &operator=(PipelineMultiplexer &&other) = delete;
    PipelineMultiplexer(PipelineMultiplexer &&other) = delete;

    hailo_status add_core_op_instance(multiplexer_core_op_handle_t core_op_handle, CoreOp &core_op);
    void set_output_vstreams_names(multiplexer_core_op_handle_t core_op_handle, const std::vector<OutputVStream> &output_vstreams);
    bool has_more_than_one_core_op_instance() const;
    size_t instances_count() const;
    hailo_status wait_for_write(multiplexer_core_op_handle_t core_op_handle);
    hailo_status signal_write_finish(multiplexer_core_op_handle_t core_op_handle, bool did_write_fail);
    Expected<uint32_t> wait_for_read(multiplexer_core_op_handle_t core_op_handle, const std::string &stream_name,
        const std::chrono::milliseconds &timeout);
    hailo_status signal_read_finish();
    hailo_status enable_stream(multiplexer_core_op_handle_t core_op_handle, const std::string &stream_name);
    hailo_status disable_stream(multiplexer_core_op_handle_t core_op_handle, const std::string &stream_name);

    hailo_status register_run_once_for_stream(const std::string &stream_name, run_once_for_stream_handle_t handle, std::function<hailo_status()> callback);
    hailo_status run_once_for_stream(const std::string &stream_name, run_once_for_stream_handle_t run_once_handle,
        multiplexer_core_op_handle_t core_op_handle);

    void set_can_output_vstream_read(multiplexer_core_op_handle_t core_op_handle, const std::string &vstream_name, bool can_read);

    static bool should_use_multiplexer();

private:

    bool should_core_op_stop(multiplexer_core_op_handle_t core_op_handle);

    std::unordered_map<scheduler_core_op_handle_t, std::unordered_map<stream_name_t, std::atomic_bool>> m_should_core_op_stop;
    std::unordered_map<multiplexer_core_op_handle_t, std::atomic_bool> m_is_waiting_to_write;

    uint32_t m_input_streams_count;
    uint32_t m_output_streams_count;

    multiplexer_core_op_handle_t m_next_to_write;
    std::unordered_map<multiplexer_core_op_handle_t, std::shared_ptr<Barrier>> m_write_barriers;
    std::deque<multiplexer_core_op_handle_t> m_order_queue;
    std::mutex m_writing_mutex;
    std::condition_variable m_writing_cv;
    multiplexer_core_op_handle_t m_currently_writing;
    std::atomic_uint32_t m_written_streams_count;

    std::unordered_map<std::string, std::atomic_bool> m_is_stream_reading;
    std::mutex m_reading_mutex;
    std::condition_variable m_reading_cv;
    std::atomic_uint32_t m_read_streams_count;
    std::unordered_map<std::string, std::atomic_uint32_t> m_num_frames_to_drain;
    multiplexer_core_op_handle_t m_next_to_read_after_drain;

    std::unordered_map<multiplexer_core_op_handle_t, std::unordered_map<std::string, std::atomic_bool>> m_can_output_vstream_read;
    std::unordered_map<multiplexer_core_op_handle_t, std::atomic_bool> m_can_core_op_read;

    bool can_core_op_read(multiplexer_core_op_handle_t core_op_handle);
    uint32_t get_frame_count_to_drain(multiplexer_core_op_handle_t core_op_handle);
    uint32_t drain_aborted_in_order_queue(multiplexer_core_op_handle_t core_op_handle, const std::string &stream_name, uint32_t max_drain_count);

    class RunOnceForStream final
    {
    public:
        RunOnceForStream() {};

    private:
        void add_instance();
        void set_callback(std::function<hailo_status()> callback);
        hailo_status run(multiplexer_core_op_handle_t core_op_handle);

        std::unordered_map<multiplexer_core_op_handle_t, std::atomic_uint32_t> m_calls_count;
        std::function<hailo_status()> m_callback;
        std::mutex m_mutex;

        friend class PipelineMultiplexer;
    };

    // The run once map stores for each stream (by name), a map of RunOnceForStream which the user can register to.
    // run_once_for_stream_handle_t is the handle which the user can access to his specific callback (for example, abort stream function).
    // This is used for flushing, aborting and clear aborting streams.
    std::unordered_map<std::string, std::unordered_map<run_once_for_stream_handle_t, std::shared_ptr<RunOnceForStream>>> m_run_once_db;
    std::mutex m_register_run_once_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_PIPELINE_MULTIPLEXER_HPP_ */
