/*
 * Copyright (c) 2021-2022 Hailo Technologies Ltd. All rights reserved.
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
#ifndef _GST_HAILO_COMMON_HPP_
#define _GST_HAILO_COMMON_HPP_

#include "hailo/device.hpp"
#include "hailo/network_group.hpp"
#include "hailo/vstream.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#include <gst/gst.h>
#pragma GCC diagnostic pop

#include <vector>

using namespace hailort;

#define PLUGIN_AUTHOR "Hailo Technologies Ltd. (\"Hailo\")"

#define MAX_QUEUED_BUFFERS_IN_INPUT (16)
#define MAX_QUEUED_BUFFERS_IN_OUTPUT (16)
#define MAX_QUEUED_BUFFERS_IN_CORE (16)
#define MAX_BUFFER_COUNT(_batch_size) (MAX_QUEUED_BUFFERS_IN_INPUT + MAX_QUEUED_BUFFERS_IN_OUTPUT + (1 < (_batch_size) ? (_batch_size) : MAX_QUEUED_BUFFERS_IN_CORE))

#define MAX_GSTREAMER_BATCH_SIZE (16)
#define MIN_GSTREAMER_BATCH_SIZE (HAILO_DEFAULT_BATCH_SIZE)
#define DEFAULT_OUTPUTS_MIN_POOL_SIZE (MAX_GSTREAMER_BATCH_SIZE)
#define DEFAULT_OUTPUTS_MAX_POOL_SIZE (0) // 0 means unlimited upper limit

#define DEFAULT_VDEVICE_KEY (0)
#define MIN_VALID_VDEVICE_KEY (1)

#define HAILO_SUPPORTED_FORMATS "{ RGB, YUY2 }"
#define HAILO_VIDEO_CAPS GST_VIDEO_CAPS_MAKE(HAILO_SUPPORTED_FORMATS)

#define GST_CHECK(cond, ret_val, element, domain, ...)      \
    do {                                \
        if (!(cond)) {                  \
            GST_ELEMENT_ERROR((element), domain, FAILED, (__VA_ARGS__), (NULL)); \
            return (ret_val);             \
        }                               \
    } while(0)

#define GST_CHECK_SUCCESS(status, element, domain, ...)      \
    do {                                \
        if (HAILO_SUCCESS != (status)) {                  \
            GST_ELEMENT_ERROR((element), domain, FAILED, (__VA_ARGS__), (NULL)); \
            return (status);           \
        }                               \
    } while(0)

#define GST_CHECK_EXPECTED(obj, element, domain, ...)      \
    do {                                \
        if (!(obj)) {                  \
            GST_ELEMENT_ERROR((element), domain, FAILED, (__VA_ARGS__), (NULL)); \
            return make_unexpected(obj.status());           \
        }                               \
    } while(0)

#define GST_CHECK_EXPECTED_AS_STATUS(obj, element, domain, ...)      \
    do {                                \
        if (!(obj)) {                  \
            GST_ELEMENT_ERROR((element), domain, FAILED, (__VA_ARGS__), (NULL)); \
            return obj.status();           \
        }                               \
    } while(0)

// From https://stackoverflow.com/questions/57092289/do-stdmake-shared-and-stdmake-unique-have-a-nothrow-version
template <class T, class... Args>
static inline std::unique_ptr<T> make_unique_nothrow(Args&&... args)
    noexcept(noexcept(T(std::forward<Args>(args)...)))
{
    return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <class T, class... Args>
static inline std::shared_ptr<T> make_shared_nothrow(Args&&... args)
    noexcept(noexcept(T(std::forward<Args>(args)...)))
{
    return std::shared_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template<typename T>
class HailoElemProperty final
{
public:
    HailoElemProperty(T default_val) : m_value(default_val), m_was_changed(false) {}

    ~HailoElemProperty() {}
    
    HailoElemProperty<T> &operator=(const T &value)
    {
        m_was_changed = true;
        m_value = value;
        return *this;
    }

    const T &get()
    {
        return m_value;
    }

    bool was_changed()
    {
        return m_was_changed;
    }

private:
    T m_value;
    bool m_was_changed;
};

template<>
HailoElemProperty<gchar*>::~HailoElemProperty();

#endif /* _GST_HAILO_COMMON_HPP_ */