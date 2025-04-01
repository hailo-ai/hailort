/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file partial_cluster_reader.hpp
 * @brief class to read and parse file to determine which clusters are enabled.
 **/

#ifndef _HAILO_PARTIAL_CLUSTER_READER_HPP_
#define _HAILO_PARTIAL_CLUSTER_READER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/logger_macros.hpp"
#include "common/utils.hpp"

#include <array>

namespace hailort
{

// valid partial cluster layouts for Hailo15M
#define PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_0	((0x1 << 1) | (0x1 << 2) | (0x1 << 3))
#define PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_1	((0x1 << 0) | (0x1 << 2) | (0x1 << 3))
#define PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_2	((0x1 << 0) | (0x1 << 1) | (0x1 << 4))

#define HAILO15M_PARTIAL_CLUSTER_LAYOUTS_LIST PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_0,\
    PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_1, PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_2

// Default is all clusters are enabled
#define PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15_DEFAULT	((0x1 << 0) | (0x1 << 1) | (0x1 << 2) | (0x1 << 3) | (0x1 << 4))

constexpr const char* PARTIAL_CLUSTER_READER_CLUSTER_LAYOUT_FILE_PATH = "/sys/devices/soc0/fuse";

// Array that has all the valid layouts for Hailo15M
static constexpr std::array<uint32_t, 3> HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY = {
    HAILO15M_PARTIAL_CLUSTER_LAYOUTS_LIST
};

// Array that has all the valid layouts for Hailo15 (Either Hailo15M or Hailo15H)
static constexpr std::array<uint32_t, 4> HAILO15__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY = {
    HAILO15M_PARTIAL_CLUSTER_LAYOUTS_LIST, PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15_DEFAULT
};

class PartialClusterReader {
public:
    static Expected<uint32_t> get_partial_clusters_layout_bitmap(hailo_device_architecture_t dev_arch);
    static Expected<hailo_device_architecture_t> get_actual_dev_arch_from_fuse(hailo_device_architecture_t fw_dev_arch);
private:
    static Expected<uint32_t> get_arch_default_bitmap(hailo_device_architecture_t dev_arch);
    static Expected<uint8_t> get_sku_value_from_arch(hailo_device_architecture_t dev_arch);
    static bool validate_arch_partial_clusters_bitmap(uint32_t bitmap, uint8_t sku_value);
    static Expected<std::pair<uint32_t, uint8_t>> read_fuse_file();
};


} /* namespace hailort */

#endif /* _HAILO_SENSOR_CONFIG_UTILS_HPP_ */