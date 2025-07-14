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
#ifndef _GST_HAILO_COMMON_HPP_
#define _GST_HAILO_COMMON_HPP_

#include "hailo/device.hpp"
#include "hailo/network_group.hpp"
#include "hailo/vstream.hpp"
#include "hailo/hailo_gst_tensor_metadata.hpp"
#include "include/hailo_gst.h"

#include <vector>

using namespace hailort;

#define GST_HAILO_USE_DMA_BUFFER_ENV_VAR "GST_HAILO_USE_DMA_BUFFER"

#define HAILONET_ERROR(msg, ...) g_print("HailoNet Error: " msg, ##__VA_ARGS__)
#define PLUGIN_AUTHOR "Hailo Technologies Ltd. (\"Hailo\")"

#ifdef _MSC_VER
    #define MAX_STRING_SIZE (MAX_PATH)
#else
    #define MAX_STRING_SIZE (PATH_MAX)
#endif

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

#define HAILO_SUPPORTED_FORMATS "{ RGB, RGBA, YUY2, NV12, NV21, I420, GRAY8 }"
#define HAILO_VIDEO_CAPS GST_VIDEO_CAPS_MAKE(HAILO_SUPPORTED_FORMATS)

#define HAILO_DEFAULT_SCHEDULER_TIMEOUT_MS (0)
#define HAILO_DEFAULT_SCHEDULER_THRESHOLD (0)

#define HAILO_DEFAULT_MULTI_PROCESS_SERVICE (false)

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

#define GST_CHECK_SUCCESS_AS_EXPECTED(status, element, domain, ...)      \
    do {                                \
        if (HAILO_SUCCESS != (status)) {                  \
            GST_ELEMENT_ERROR((element), domain, FAILED, (__VA_ARGS__), (NULL)); \
            return make_unexpected(status);           \
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

#define _CHECK(cond, ret_val, ...)      \
    do {                                \
        if (!(cond)) {                  \
            g_print(__VA_ARGS__); \
            g_print("\n"); \
            return (ret_val);           \
        }                               \
    } while(0)

#define CHECK(cond, ret_val, ...) _CHECK((cond), (ret_val),  ##__VA_ARGS__)

#define CHECK_AS_EXPECTED(cond, ret_val, ...) \
    _CHECK((cond), (make_unexpected(ret_val)),  ##__VA_ARGS__)

#define CHECK_NOT_NULL(arg, status) _CHECK(nullptr != (arg), status, "CHECK_NOT_NULL for %s failed", #arg)

#define CHECK_NOT_NULL_AS_EXPECTED(arg, status) \
    _CHECK(nullptr != (arg), make_unexpected(status), "CHECK_NOT_NULL_AS_EXPECTED for %s failed", #arg)

#define _CHECK_SUCCESS(status, ...)                                                                            \
    do {                                                                                                                        \
        const auto &__check_success_status = (status);                                                                          \
        _CHECK(                                                                                                                 \
            HAILO_SUCCESS == __check_success_status,                                                                            \
            __check_success_status,                                                                                             \
            "CHECK_SUCCESS failed with status=%d", status       \
        );                                                                                                                      \
    } while(0)
#define CHECK_SUCCESS(status, ...) _CHECK_SUCCESS(status, "" __VA_ARGS__)

#define _CHECK_SUCCESS_AS_EXPECTED(status, ...)                                                                       \
    do {                                                                                                                               \
        const auto &__check_success_status = (status);                                                                                 \
        _CHECK(                                                                                                                        \
            HAILO_SUCCESS == __check_success_status,                                                                                   \
            make_unexpected(__check_success_status),                                                                                   \
            "CHECK_SUCCESS_AS_EXPECTED failed with status=%d", status  \
        );                                                                                                                             \
    } while(0)
#define CHECK_SUCCESS_AS_EXPECTED(status, ...) _CHECK_SUCCESS_AS_EXPECTED(status, "" __VA_ARGS__)

#define _CHECK_EXPECTED_AS_STATUS(obj, ...)                                                                                      \
    do {                                                                                                                                          \
        const auto &__check_expected_obj = (obj);                                                                                                 \
        _CHECK(                                                                                                                                   \
            __check_expected_obj.has_value(),                                                                                                     \
            __check_expected_obj.status(),                                                                                                        \
            "CHECK_EXPECTED_AS_STATUS failed with status=%d", __check_expected_obj.status()       \
        );                                                                                                                                        \
    } while(0)
#define CHECK_EXPECTED_AS_STATUS(obj, ...) _CHECK_EXPECTED_AS_STATUS(obj, "" __VA_ARGS__)

#define _CHECK_EXPECTED(obj, ...)                                                                                      \
    do {                                                                                                                                \
        const auto &__check_expected_obj = (obj);                                                                                       \
        _CHECK(                                                                                                                         \
            __check_expected_obj.has_value(),                                                                                           \
            make_unexpected(__check_expected_obj.status()),                                                                             \
            "CHECK_EXPECTED failed with status=%d",  __check_expected_obj.status()       \
        );                                                                                                                              \
    } while(0)
#define CHECK_EXPECTED(obj, ...) _CHECK_EXPECTED(obj, "" __VA_ARGS__)

#define __HAILO_CONCAT(x, y) x ## y
#define _HAILO_CONCAT(x, y) __HAILO_CONCAT(x, y)

#define _TRY(expected_var_name, var_decl, expr, ...) \
    auto expected_var_name = (expr); \
    CHECK_EXPECTED(expected_var_name, __VA_ARGS__); \
    var_decl = expected_var_name.release()

/**
 * The TRY macro is used to allow easier validation and access for variables returned as Expected<T>.
 * If the expression returns an Expected<T> with status HAILO_SUCCESS, the macro will release the expected and assign
 * the var_decl.
 * Otherwise, the macro will cause current function to return the failed status.
 *
 * Usage example:
 *
 * Expected<int> func() {
 *     TRY(auto var, return_5());
 *     // Now var is int with value 5
 *
 *     // func will return Unexpected with status HAILO_INTERNAL_FAILURE
 *     TRY(auto var2, return_error(HAILO_INTERNAL_FAILURE), "Failed doing stuff {}", 5);
 */
#define TRY(var_decl, expr, ...) _TRY(_HAILO_CONCAT(__expected, __COUNTER__), var_decl, expr, __VA_ARGS__)

#define RGB_FEATURES_SIZE (3)
#define RGBA_FEATURES_SIZE (4)
#define GRAY8_FEATURES_SIZE (1)
#define YUY2_FEATURES_SIZE (2)
#define NV12_FEATURES_SIZE (3)
#define NV21_FEATURES_SIZE (3)
#define I420_FEATURES_SIZE (3)

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

    const T &get() const
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

class HailoElemStringProperty final
{
public:
    HailoElemStringProperty(const std::string &default_val) : m_was_changed(false) {
        memset(m_string, 0, sizeof(m_string));
        strncpy(m_string, default_val.c_str(), sizeof(m_string) - 1);
    }

    ~HailoElemStringProperty() {}
    
    HailoElemStringProperty &operator=(const std::string &value)
    {
        m_was_changed = true;
        strncpy(m_string, value.c_str(), sizeof(m_string) - 1);
        return *this;
    }

    const std::string get() const
    {
        return m_string;
    }

    bool was_changed()
    {
        return m_was_changed;
    }

private:
    char m_string[MAX_STRING_SIZE];
    bool m_was_changed;
};

#define GST_TYPE_SCHEDULING_ALGORITHM (gst_scheduling_algorithm_get_type ())
GType gst_scheduling_algorithm_get_type (void);

#define GST_TYPE_HAILO_FORMAT_TYPE (gst_hailo_format_type_get_type ())
GType gst_hailo_format_type_get_type (void);

bool do_versions_match(GstElement *self);

hailo_tensor_metadata_t tensor_metadata_from_vstream_info(const hailo_vstream_info_t &vstream_info);
HailoTensorFormatType tensor_format_type_from_vstream_format_type(hailo_format_type_t format_type);

#endif /* _GST_HAILO_COMMON_HPP_ */