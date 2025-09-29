/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file genai_server.cpp
 * @brief Implementation of the GenAIServerManager class
 **/

#include "genai_server.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"

namespace hailort
{
namespace genai
{

GenAIServerManager::GenAIServerManager(std::shared_ptr<Session> session) :
    m_session(session), m_dispatcher()
{}

hailo_status GenAIServerManager::flow()
{
    while (true) {
        TRY(auto request, m_session.read());
        TRY(auto action_id, GenAISerializerUtils::get_action_id(MemoryView(*request)));
        CHECK(contains(m_dispatcher, static_cast<HailoGenAIActionID>(action_id)), HAILO_INVALID_OPERATION, "Invalid action id: {}", action_id);
        TRY(auto reply, m_dispatcher.at(static_cast<HailoGenAIActionID>(action_id))(MemoryView(*request)));
        CHECK_SUCCESS(m_session.write(MemoryView(reply)));
    }

    return HAILO_SUCCESS;
}  


} /* namespace genai */
} /* namespace hailort */
