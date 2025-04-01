/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the LGPL 2.1 license (https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifndef _HAILO_OUTPUT_INFO_HPP_
#define _HAILO_OUTPUT_INFO_HPP_

#include "common.hpp"
#include "hailo/platform.h"
#include "hailo/buffer.hpp"
#include "hailo/vstream.hpp"


// TODO: The name info is confusing because one might think that it holds only the info, so change it to "HailoOutputBuffer" or something like that
class HailoOutputInfo {
public:
    HailoOutputInfo(OutputVStream &vstream, GstBufferPool *pool)
        : m_vstream(vstream), m_pool(pool), m_last_acquired_buffer(nullptr), m_vstream_info(vstream.get_info())
    {}

    ~HailoOutputInfo()
    {
        if (nullptr != m_pool) {
            (void)gst_buffer_pool_set_active(m_pool, FALSE);
        }
    }

    HailoOutputInfo(const HailoOutputInfo &other) = delete;
    HailoOutputInfo &operator=(const HailoOutputInfo &other) = delete;
    HailoOutputInfo& operator=(HailoOutputInfo &&other) = delete;

    HailoOutputInfo(HailoOutputInfo &&other) : m_vstream(other.m_vstream), m_pool(std::exchange(other.m_pool, nullptr)),
        m_last_acquired_buffer(std::exchange(other.m_last_acquired_buffer, nullptr)), m_vstream_info(std::move(other.m_vstream_info))
    {}

    OutputVStream &vstream()
    {
        return m_vstream;
    }

    const hailo_vstream_info_t &vstream_info() const
    {
        return m_vstream_info;
    }

    Expected<GstBuffer*> acquire_buffer()
    {
        GstBuffer *buffer = nullptr;
        GstFlowReturn result = gst_buffer_pool_acquire_buffer(m_pool, &buffer, nullptr);
        if (GST_FLOW_OK != result) {
            g_critical("Acquiring buffer failed with flow status %d!", result);
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }

        m_last_acquired_buffer = buffer;
        return buffer;
    }

    GstBuffer *last_acquired_buffer()
    {
        return m_last_acquired_buffer;
    }

    void unref_last_acquired_buffer()
    {
        if (nullptr == m_last_acquired_buffer) {
            return;
        }

        gst_buffer_unref(m_last_acquired_buffer);
        m_last_acquired_buffer = nullptr;
    }

private:
    OutputVStream &m_vstream;
    GstBufferPool *m_pool;
    GstBuffer *m_last_acquired_buffer;
    hailo_vstream_info_t m_vstream_info;
};


#endif /* _HAILO_OUTPUT_INFO_HPP_ */
