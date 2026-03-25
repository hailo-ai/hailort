/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file monitor_common.cpp
 * @brief Common utilities shared between accelerator and integrated monitors
 **/

#include "monitor_common.hpp"
#include "hailortcli.hpp"

namespace hailort
{

volatile bool InterruptHandler::m_is_running = true;

void InterruptHandler::sigint_handler(int /*dummy*/)
{
    m_is_running = false;
}

void InterruptHandler::register_handler()
{
    signal(SIGINT, sigint_handler);
}

} /* namespace hailort */
