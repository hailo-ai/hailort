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
#include "hailo/network_group.hpp"
#include "network_group_scheduler.hpp"
#include "common/barrier.hpp"

#include <queue>
#include <mutex>

namespace hailort
{

using multiplexer_ng_handle_t = uint32_t;
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

    hailo_status add_network_group_instance(multiplexer_ng_handle_t network_group_handle, ConfiguredNetworkGroup &network_group);
    void set_output_vstreams_names(multiplexer_ng_handle_t network_group_handle, const std::vector<OutputVStream> &output_vstreams);
    bool has_more_than_one_ng_instance() const;
    size_t instances_count() const;
    hailo_status wait_for_write(multiplexer_ng_handle_t network_group_handle);
    hailo_status signal_write_finish();
    hailo_status wait_for_read(multiplexer_ng_handle_t network_group_handle, const std::string &stream_name);
    hailo_status signal_read_finish(multiplexer_ng_handle_t network_group_handle);
    hailo_status enable_network_group(multiplexer_ng_handle_t network_group_handle);
    hailo_status disable_network_group(multiplexer_ng_handle_t network_group_handle);

    hailo_status register_run_once_for_stream(const std::string &stream_name, run_once_for_stream_handle_t handle, std::function<hailo_status()> callback);
    hailo_status run_once_for_stream(const std::string &stream_name, run_once_for_stream_handle_t run_once_handle,
        multiplexer_ng_handle_t network_group_handle);

    void set_can_output_vstream_read(multiplexer_ng_handle_t network_group_handle, const std::string &vstream_name, bool can_read);

private:
    std::unordered_map<multiplexer_ng_handle_t, std::atomic_bool> m_should_ng_stop;
    std::unordered_map<multiplexer_ng_handle_t, std::atomic_bool> m_is_waiting_to_write;

    uint32_t m_input_streams_count;
    uint32_t m_output_streams_count;

    multiplexer_ng_handle_t m_next_to_write;
    std::unordered_map<multiplexer_ng_handle_t, std::shared_ptr<Barrier>> m_write_barriers;
    std::queue<multiplexer_ng_handle_t> m_order_queue;
    std::mutex m_writing_mutex;
    std::condition_variable m_writing_cv;
    multiplexer_ng_handle_t m_currently_writing;
    std::atomic_uint32_t m_written_streams_count;

    std::unordered_map<multiplexer_ng_handle_t, std::unordered_map<std::string, std::atomic_bool>> m_is_stream_reading;
    std::mutex m_reading_mutex;
    std::condition_variable m_reading_cv;
    std::atomic_uint32_t m_read_streams_count;

    std::unordered_map<multiplexer_ng_handle_t, std::unordered_map<std::string, std::atomic_bool>> m_can_output_vstream_read;
    std::unordered_map<multiplexer_ng_handle_t, std::atomic_bool> m_can_network_group_read;

    bool can_network_group_read(multiplexer_ng_handle_t network_group_handle);

    class RunOnceForStream final
    {
    public:
        RunOnceForStream() {};

    private:
        void add_instance();
        void set_callback(std::function<hailo_status()> callback);
        hailo_status run(multiplexer_ng_handle_t network_group_handle);

        std::unordered_map<multiplexer_ng_handle_t, std::atomic_bool> m_was_called;
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
