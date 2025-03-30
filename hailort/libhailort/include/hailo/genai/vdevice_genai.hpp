/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "hailo/genai/common.hpp"
#include "hailo/buffer.hpp"


namespace hailort
{
namespace genai
{

// TODO (HRT-16126): - adjusting all ack's once server is written in cpp
const size_t SERVER_ACK_SIZE = 128;

// Forward decleration
class GenAISession;
class SessionWrapper;

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

    const hailo_vdevice_params_t get_params() const {
        return m_vdevice_params;
    }

    VDeviceGenAI(hailo_device_id_t device_id, const hailo_vdevice_params_t &params);
private:
    static hailo_status validate_params(const hailo_vdevice_params_t &params);

    hailo_device_id_t m_device_id;
    hailo_vdevice_params_t m_vdevice_params;
};

// TODO (HRT-16126): Delete this class
class HAILORTAPI GenAISession
{
public:
    static Expected<std::shared_ptr<GenAISession>> create_shared(uint16_t port, const std::string &device_id);

    GenAISession(GenAISession &&) = delete;
    GenAISession(const GenAISession &) = delete;
    GenAISession &operator=(GenAISession &&) = delete;
    GenAISession &operator=(const GenAISession &) = delete;
    virtual ~GenAISession() = default;

    hailo_status write(MemoryView buffer, std::chrono::milliseconds timeout = Session::DEFAULT_WRITE_TIMEOUT);
    Expected<size_t> read(MemoryView buffer, std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT);
    Expected<std::shared_ptr<Buffer>> read(std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT);

    hailo_status send_file(const std::string &path);
    Expected<std::string> get_ack(std::chrono::milliseconds timeout = Session::DEFAULT_READ_TIMEOUT);

    GenAISession(std::shared_ptr<SessionWrapper> session_wrapper);
private:
    std::shared_ptr<SessionWrapper> m_session_wrapper;
};

using VDevice = VDeviceGenAI;

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_VDEVICE_GENAI_HPP_ */
