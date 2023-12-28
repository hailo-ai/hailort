/**
 * Copyright (c) 2020-2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file partial_cluster_reader.cpp
 * @brief class to read and parse file to determine which clusters are enabled.
 **/

#include "hailo/hailort_common.hpp"

#include "partial_cluster_reader.hpp"

#include <fstream>
#include <algorithm>

namespace hailort
{

Expected<uint32_t> PartialClusterReader::get_arch_default_bitmap(const hailo_device_architecture_t dev_arch)
{
    switch(dev_arch) {
        // Currently only supported architecture for this function is HAILO15M - but in future can add more
        case HAILO_ARCH_HAILO15M:
            return static_cast<uint32_t>(PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_DEFAULT);
        default:
            LOGGER__ERROR("Error, Given architecture {} doesnt support partial cluster layout",
                HailoRTCommon::get_device_arch_str(dev_arch));
            return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

bool PartialClusterReader::validate_arch_partial_clusters_bitmap(const hailo_device_architecture_t dev_arch,
    const uint32_t bitmap)
{
    switch(dev_arch) {
        // Currently only supported architecture for this function is HAILO15M - but in future can add more
        case HAILO_ARCH_HAILO15M:
            return (std::find(HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY.begin(),
                HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY.end(), bitmap) !=
                HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY.end());
        default:
            LOGGER__ERROR("Error, Given architecture {} doesnt support partial cluster layout",
                HailoRTCommon::get_device_arch_str(dev_arch));
            return false;
    }
}

Expected<uint32_t> PartialClusterReader::get_partial_clusters_layout_bitmap(const hailo_device_architecture_t dev_arch)
{
    std::ifstream layout_bitmap_file;
    layout_bitmap_file.open(PARTIAL_CLUSTER_READER_CLUSTER_LAYOUT_FILE_PATH, std::ios::binary);
    if (!layout_bitmap_file.is_open()) {
        LOGGER__WARNING("partial cluster layout bitmap file not found, Enabling all clusters by default");
        return get_arch_default_bitmap(dev_arch);
    }

    uint32_t partial_clusters_layout_bitmap = 0;
    layout_bitmap_file.read(reinterpret_cast<char*>(&partial_clusters_layout_bitmap),
        sizeof(partial_clusters_layout_bitmap));

    // Fuse file represents clusters that are enabled with 0 in bit value and clusters that are disabled with 1
    // We also ignore all the MSB's that dont represent clusters.
    // Therefore, after reading the uint32 layout - we mask with the default bitmap and bitwise flip the
    // relevant bits so that 1 will represent enabled clusters
    const auto arch_bitmap_mask_exp = get_arch_default_bitmap(dev_arch);
    CHECK_EXPECTED(arch_bitmap_mask_exp);
    partial_clusters_layout_bitmap = (~partial_clusters_layout_bitmap & arch_bitmap_mask_exp.value());
    layout_bitmap_file.close();

    CHECK_AS_EXPECTED(validate_arch_partial_clusters_bitmap(dev_arch, partial_clusters_layout_bitmap),
        HAILO_INTERNAL_FAILURE, "Error, Invalid partial clusters bitmap value given {}",
        partial_clusters_layout_bitmap);

    return partial_clusters_layout_bitmap;
}

} /* namespace hailort */
