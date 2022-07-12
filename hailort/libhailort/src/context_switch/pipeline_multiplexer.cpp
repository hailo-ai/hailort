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

namespace hailort
{

PipelineMultiplexer::PipelineMultiplexer() :
    m_is_enabled(false),
    m_mutex(),
    m_readers(),
    m_write_mutex()
{}

hailo_status PipelineMultiplexer::add_reader(EventPtr &reader_event)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    auto was_empty = m_readers.empty();
    m_readers.push(reader_event);
    if (was_empty) {
        auto status = reader_event->signal();
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status PipelineMultiplexer::signal_done_reading()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_readers.empty()) {
        return HAILO_INTERNAL_FAILURE;
    }
    m_readers.front()->reset();
    m_readers.pop();

    if (!m_readers.empty()) {
        m_readers.front()->signal();
    }

    return HAILO_SUCCESS;
}

void PipelineMultiplexer::enable()
{
    m_is_enabled = true;
}

bool PipelineMultiplexer::is_enabled() const
{
    return m_is_enabled;
}

void PipelineMultiplexer::acquire_write_lock()
{
    m_write_mutex.lock();
}

void PipelineMultiplexer::release_write_lock()
{
    m_write_mutex.unlock();
}

} /* namespace hailort */
