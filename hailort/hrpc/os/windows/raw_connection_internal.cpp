/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file raw_connection_internal.cpp
 * @brief Windows Sockets Raw Connection
 **/

#include "hrpc/os/windows/raw_connection_internal.hpp"

#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "hailo/hailort.h"

using namespace hrpc;

Expected<std::shared_ptr<ConnectionContext>> OsConnectionContext::create_shared(bool is_accepting)
{
    (void)is_accepting;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<std::shared_ptr<RawConnection>> OsRawConnection::create_shared(std::shared_ptr<OsConnectionContext> context)
{
    (void)context;
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<std::shared_ptr<RawConnection>> OsRawConnection::accept()
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

hailo_status OsRawConnection::connect()
{
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status OsRawConnection::write(const uint8_t *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status OsRawConnection::read(uint8_t *buffer, size_t size)
{
    (void)buffer;
    (void)size;
    return HAILO_NOT_IMPLEMENTED;
}

hailo_status OsRawConnection::close()
{
    return HAILO_NOT_IMPLEMENTED;
}