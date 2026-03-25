/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file expected.hpp
 * @brief Expected type for libhailopp, wrapping tl::expected with hailopp_status
 **/

#ifndef _HAILOPP_EXPECTED_HPP_
#define _HAILOPP_EXPECTED_HPP_

#include <tl/expected.hpp>
#include "status.h"

namespace hailopp {

template<typename T>
using Expected = tl::expected<T, hailopp_status>;

using Unexpected = tl::unexpected<hailopp_status>;

inline Unexpected make_unexpected(hailopp_status status)
{
    return Unexpected(status);
}

} // namespace hailopp

#endif /* _HAILOPP_EXPECTED_HPP_ */
