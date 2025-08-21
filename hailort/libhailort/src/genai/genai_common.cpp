/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file genai_common.cpp
 * @brief Common functions for GenAI
 **/

#include "genai_common.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"
#include "common/genai/connection_ports.hpp"


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

Expected<std::shared_ptr<SessionWrapper>> GenAICommon::create_session_wrapper(const hailo_vdevice_params_t &vdevice_params,
    uint16_t connection_port)
{
    CHECK_SUCCESS(validate_genai_vdevice_params(vdevice_params));

    std::string device_id = "";
    // If multi-process-service, use SERVER_ADDR_USE_UNIX_SOCKET for the connection
    if (vdevice_params.multi_process_service) {
        device_id = SERVER_ADDR_USE_UNIX_SOCKET;
    } else {
        device_id = (nullptr != vdevice_params.device_ids) ? vdevice_params.device_ids[0].id : "";
    }

    TRY(auto hailo_session, Session::connect(connection_port, device_id));
    auto session_wrapper = make_shared_nothrow<SessionWrapper>(hailo_session);
    CHECK_NOT_NULL_AS_EXPECTED(session_wrapper, HAILO_OUT_OF_HOST_MEMORY);

    return session_wrapper;
}

} /* namespace genai */
} /* namespace hailort */
