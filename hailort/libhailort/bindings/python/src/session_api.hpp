/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file session_api.hpp
 * @brief Session python bindings wrapper
 **/

#ifndef _HAILO_SESSION_API_HPP_
#define _HAILO_SESSION_API_HPP_

#include "hailo/hailort.h"
#include "hailo/hailo_session.hpp"
#include "utils.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

namespace hailort
{

class SessionWrapper;
using SessionWrapperPtr = std::shared_ptr<SessionWrapper>;

class SessionWrapper
{
public:
    static SessionWrapperPtr connect(uint16_t port, const std::string &device_id);
    void write(const py::bytes &input_data);
    py::bytes read();
    void close();

    static void bind(py::module &m);

    SessionWrapper(SessionWrapper &&) = delete;
    SessionWrapper(const SessionWrapper &) = delete;
    SessionWrapper &operator=(SessionWrapper &&) = delete;
    SessionWrapper &operator=(const SessionWrapper &) = delete;
    virtual ~SessionWrapper() = default;

    SessionWrapper(std::shared_ptr<Session> session);
private:
    std::shared_ptr<Session> m_session;
};


class SessionListenerWrapper;
using SessionListenerWrapperPtr = std::shared_ptr<SessionListenerWrapper>;

class SessionListenerWrapper
{
public:
    static SessionListenerWrapperPtr create(uint16_t port, const std::string &device_id);
    SessionWrapperPtr accept();

    static void bind(py::module &m);

    SessionListenerWrapper(SessionListenerWrapper &&) = delete;
    SessionListenerWrapper(const SessionListenerWrapper &) = delete;
    SessionListenerWrapper &operator=(SessionListenerWrapper &&) = delete;
    SessionListenerWrapper &operator=(const SessionListenerWrapper &) = delete;
    virtual ~SessionListenerWrapper() = default;

    SessionListenerWrapper(std::shared_ptr<SessionListener> listener);
private:
    std::shared_ptr<SessionListener> m_listener;
};


} /* namespace hailort */

#endif /* _HAILO_SESSION_API_HPP_ */
