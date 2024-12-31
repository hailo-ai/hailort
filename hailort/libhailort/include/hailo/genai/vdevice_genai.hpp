/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_genai.hpp
 * @brief This object represents the `VDevice` object for libhailort-genai.
 * This is a temporary implementation until the gen-ai will use the hrpc vdevice.
 **/

#ifndef _HAILO_GENAI_VDEVICE_GENAI_HPP_
#define _HAILO_GENAI_VDEVICE_GENAI_HPP_

#include "hailo/hailo_session.hpp"

namespace hailort
{
namespace genai
{

// Forward decleration
class GenAISession;

class HAILORTAPI VDeviceGenAI
{
public:
    static Expected<std::shared_ptr<VDeviceGenAI>> create_shared();
    static Expected<std::shared_ptr<VDeviceGenAI>> create_shared(const hailo_vdevice_params_t &params);

    VDeviceGenAI(VDeviceGenAI &&) = delete;
    VDeviceGenAI(const VDeviceGenAI &) = delete;
    VDeviceGenAI &operator=(VDeviceGenAI &&) = delete;
    VDeviceGenAI &operator=(const VDeviceGenAI &) = delete;
    virtual ~VDeviceGenAI() = default;

    Expected<std::shared_ptr<GenAISession>> create_session(uint16_t port);

    VDeviceGenAI(hailo_device_id_t device_id);
private:
    static hailo_status validate_params(const hailo_vdevice_params_t &params);

    hailo_device_id_t m_device_id;
};

class HAILORTAPI GenAISession
{
public:
    static Expected<std::shared_ptr<GenAISession>> create_shared(uint16_t port, const std::string &device_id);

    GenAISession(GenAISession &&) = delete;
    GenAISession(const GenAISession &) = delete;
    GenAISession &operator=(GenAISession &&) = delete;
    GenAISession &operator=(const GenAISession &) = delete;
    virtual ~GenAISession() = default;

    hailo_status write(const uint8_t *buffer, size_t size, std::chrono::milliseconds timeout = Session::DEFAULT_WRITE_TIMEOUT);
    Expected<size_t> read(uint8_t *buffer, size_t size, std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT);

    GenAISession(std::shared_ptr<Session> session);
private:
    std::shared_ptr<Session> m_session;
};

using VDevice = VDeviceGenAI;

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_VDEVICE_GENAI_HPP_ */
