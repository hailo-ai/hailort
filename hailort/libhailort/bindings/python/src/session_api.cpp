/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file session_api.cpp
 * @brief Session python bindings wrapper implementation
 **/

#include "session_api.hpp"
#include "bindings_common.hpp"
#include "hailo/hailort.h"
#include "hailo/hailo_session.hpp"

namespace hailort
{

SessionWrapperPtr SessionWrapper::connect(uint16_t port, const std::string &device_id)
{
    auto session = Session::connect(port, device_id);
    VALIDATE_EXPECTED(session);

    return std::make_shared<SessionWrapper>(session.release());
}

void SessionWrapper::write(const py::bytes &input_data)
{
    // First we send the buffer's size. Then the buffer itself.
	uint64_t size_to_write = py::len(input_data);
    auto status = m_session->write(reinterpret_cast<const uint8_t*>(&size_to_write), sizeof(size_to_write));
    VALIDATE_STATUS(status);

    py::buffer_info info(py::buffer(input_data).request());
    status = m_session->write(reinterpret_cast<uint8_t*>(info.ptr), info.size);
    VALIDATE_STATUS(status);
}

py::bytes SessionWrapper::read()
{
	uint64_t size_to_read;
	auto status = m_session->read(reinterpret_cast<uint8_t*>(&size_to_read), sizeof(size_to_read));
    VALIDATE_STATUS(status);

    auto buffer = Buffer::create(size_to_read, BufferStorageParams::create_dma());
    VALIDATE_EXPECTED(buffer);

    status = m_session->read(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(buffer->data())), buffer->size());
    VALIDATE_STATUS(status);

    return py::bytes(buffer->as_pointer<char>(), buffer->size());
}

void SessionWrapper::close()
{
    VALIDATE_STATUS(m_session->close());
}

void SessionWrapper::bind(py::module &m)
{
    py::class_<SessionWrapper, SessionWrapperPtr>(m, "Session")
        .def("connect", &SessionWrapper::connect)
        .def("write", &SessionWrapper::write)
        .def("read", &SessionWrapper::read)
        .def("close", &SessionWrapper::close)
        ;
}

SessionWrapper::SessionWrapper(std::shared_ptr<Session> session) : m_session(session)
{}

SessionListenerWrapperPtr SessionListenerWrapper::create(uint16_t port, const std::string &device_id)
{
    auto listener = SessionListener::create_shared(port, device_id);
    VALIDATE_EXPECTED(listener);

    return std::make_shared<SessionListenerWrapper>(listener.release());
}

SessionWrapperPtr SessionListenerWrapper::accept()
{
    auto session = m_listener->accept();
    VALIDATE_EXPECTED(session);

    return std::make_shared<SessionWrapper>(session.release());
}

void SessionListenerWrapper::bind(py::module &m)
{
    py::class_<SessionListenerWrapper, SessionListenerWrapperPtr>(m, "SessionListener")
        .def("create", &SessionListenerWrapper::create)
        .def("accept", &SessionListenerWrapper::accept)
        ;
}

SessionListenerWrapper::SessionListenerWrapper(std::shared_ptr<SessionListener> listener) : m_listener(listener)
{}

} /* namespace hailort */
