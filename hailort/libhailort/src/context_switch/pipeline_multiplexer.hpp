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

#include <queue>
#include <mutex>

namespace hailort
{

class PipelineMultiplexer
{
public:
    virtual ~PipelineMultiplexer() = default;
    PipelineMultiplexer(const PipelineMultiplexer &other) = delete;
    PipelineMultiplexer &operator=(const PipelineMultiplexer &other) = delete;
    PipelineMultiplexer &operator=(PipelineMultiplexer &&other) = delete;
    PipelineMultiplexer(PipelineMultiplexer &&other) = delete;

    PipelineMultiplexer();

    hailo_status add_reader(EventPtr &reader_event);
    hailo_status signal_done_reading();

    void enable();
    bool is_enabled() const;

    void acquire_write_lock();
    void release_write_lock();

private:
    bool m_is_enabled;
    std::mutex m_mutex;
    std::queue<EventPtr> m_readers;

    std::mutex m_write_mutex;
};

} /* namespace hailort */

#endif /* _HAILO_PIPELINE_MULTIPLEXER_HPP_ */
