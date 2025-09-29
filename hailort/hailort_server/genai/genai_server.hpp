/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file genai_server.hpp
 * @brief Base class for all GenAI servers
 **/

#ifndef _HAILO_GENAI_SERVER_HPP_
#define _HAILO_GENAI_SERVER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/utils.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/genai/connection_ports.hpp"

namespace hailort
{
namespace genai
{


using Handler = std::function<Expected<Buffer>(const MemoryView &)>;

class GenAIServerManager
{
public:
    GenAIServerManager(std::shared_ptr<Session> session);
    virtual ~GenAIServerManager() = default;

    hailo_status flow();

protected:
    SessionWrapper m_session;
    std::unordered_map<HailoGenAIActionID, Handler> m_dispatcher;
};


} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_SERVER_HPP_ */
