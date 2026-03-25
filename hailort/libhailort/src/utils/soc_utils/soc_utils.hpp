/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file soc_utils.hpp
 * @brief SOC related utilities.
 **/

#ifndef _HAILO_SOC_UTILS_HPP_
#define _HAILO_SOC_UTILS_HPP_

#include "hailo/expected.hpp"
#include "hailo/hailort.h"

namespace hailort
{
namespace soc_utils
{

// valid partial cluster layouts for Hailo15M
#define PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_0 ((0x1 << 1) | (0x1 << 2) | (0x1 << 3))
#define PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_1 ((0x1 << 0) | (0x1 << 2) | (0x1 << 3))
#define PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_2 ((0x1 << 0) | (0x1 << 1) | (0x1 << 4))

// Default is all clusters are enabled
#define PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15_DEFAULT ((0x1 << 0) | (0x1 << 1) | (0x1 << 2) | (0x1 << 3) | (0x1 << 4))

Expected<hailo_device_architecture_t> get_device_architecture();
Expected<uint32_t> get_partial_clusters_layout_bitmap(hailo_device_architecture_t dev_arch);

} // namespace soc_utils
} // namespace hailort

#endif /* _HAILO_SOC_UTILS_HPP_ */
