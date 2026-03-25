/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file genai_common.cpp
 * @brief Common functions for GenAI
 **/

#include "genai_common.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "common/genai/connection_ports.hpp"
#include "common/logger_macros.hpp"


namespace hailort
{
namespace genai
{

hailo_status GenAICommon::validate_genai_vdevice_params(const hailo_vdevice_params_t &vdevice_params)
{
    CHECK(vdevice_params.scheduling_algorithm != HAILO_SCHEDULING_ALGORITHM_NONE, HAILO_NOT_SUPPORTED,
        "Working without scheduler is not supported for GenAI");

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<SessionWrapper>> GenAICommon::create_session_wrapper(std::shared_ptr<VDevice> vdevice,
    uint16_t connection_port)
{
    CHECK_SUCCESS(validate_genai_vdevice_params(vdevice->get_params()));

    TRY(auto hailo_session, vdevice->create_session(connection_port));
    auto session_wrapper = make_shared_nothrow<SessionWrapper>(hailo_session);
    CHECK_NOT_NULL_AS_EXPECTED(session_wrapper, HAILO_OUT_OF_HOST_MEMORY);

    return session_wrapper;
}

} /* namespace genai */
} /* namespace hailort */
