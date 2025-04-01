/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file partial_cluster_reader.cpp
 * @brief class to read and parse file to determine which clusters are enabled.
 **/

#include "hailo/hailort_common.hpp"
#include "common/filesystem.hpp"
#include "partial_cluster_reader.hpp"

#include <fstream>
#include <algorithm>

namespace hailort
{

//TODO: HRT-16652 - support more architecture's SKU
// SKU is three bit value in fuse file in order to differentiate the different kind of boards
#define SKU_VALUE_BITMAP    (0x7)
#define HAILO15H_SKU_VALUE  (0x0)
#define HAILO10H_SKU_VALUE  (0x1)
#define HAILO15M_SKU_VALUE  (0x3)


// SKU and partial cluster layout bitmap are located at specific locations in the fuse file according to the spec
// Located in issue HRT-12971
#define SKU_BYTE_INDEX_IN_FUSE_FILE (32)
#define SKU_BIT_INDEX_IN_WORD (18)
#define ACTIVE_CLUSTER_LAYOUT_BITMAP_INDEX_IN_FUSE_FILE (80)

Expected<uint32_t> PartialClusterReader::get_arch_default_bitmap(hailo_device_architecture_t dev_arch)
{
    switch(dev_arch) {
        // Currently only supported architectures for this function are HAILO15H and HAILO15M - but in future can add
        case HAILO_ARCH_HAILO15H:
        case HAILO_ARCH_HAILO15M:
        case HAILO_ARCH_HAILO10H:
            return static_cast<uint32_t>(PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15_DEFAULT);
        default:
            LOGGER__ERROR("Error, Given architecture {} doesnt support partial cluster layout",
                HailoRTCommon::get_device_arch_str(dev_arch));
            return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

bool PartialClusterReader::validate_arch_partial_clusters_bitmap(uint32_t bitmap, uint8_t sku_value)
{
    switch (sku_value) {
        case HAILO15H_SKU_VALUE:
        case HAILO10H_SKU_VALUE:
            return (PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15_DEFAULT == bitmap);
        case HAILO15M_SKU_VALUE:
            return (std::find(HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY.begin(),
                HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY.end(), bitmap) !=
                HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY.end());
        default:
            return false;
    }
}

// NOTE: This Function assumes fuse file exists - and file not being able to be opened is considered error
Expected<std::pair<uint32_t, uint8_t>> PartialClusterReader::read_fuse_file()
{
    std::ifstream layout_bitmap_file;
    layout_bitmap_file.open(PARTIAL_CLUSTER_READER_CLUSTER_LAYOUT_FILE_PATH, std::ios::binary);

    CHECK_AS_EXPECTED(layout_bitmap_file.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed Opening layout bitmap file {}",
        PARTIAL_CLUSTER_READER_CLUSTER_LAYOUT_FILE_PATH);

    // SKU is located at SKU_BYTE_INDEX_IN_FUSE_FILE
    layout_bitmap_file.seekg(SKU_BYTE_INDEX_IN_FUSE_FILE, std::ios::beg);
    CHECK_AS_EXPECTED(layout_bitmap_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed seek in fuse file");

    // Read SKU value from file as well to validate arch type
    uint32_t misc_word = 0;
    layout_bitmap_file.read(reinterpret_cast<char*>(&misc_word), sizeof(misc_word));
    CHECK_AS_EXPECTED(layout_bitmap_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading fuse file");
    uint8_t sku_value = ((misc_word >> SKU_BIT_INDEX_IN_WORD) & SKU_VALUE_BITMAP);

    // active clusters bitmap is located at ACTIVE_CLUSTER_LAYOUT_BITMAP_INDEX_IN_FUSE_FILE
    layout_bitmap_file.seekg(ACTIVE_CLUSTER_LAYOUT_BITMAP_INDEX_IN_FUSE_FILE, std::ios::beg);
    CHECK_AS_EXPECTED(layout_bitmap_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed seek in fuse file");

    uint32_t partial_clusters_layout_bitmap = 0;
    layout_bitmap_file.read(reinterpret_cast<char*>(&partial_clusters_layout_bitmap),
        sizeof(partial_clusters_layout_bitmap));
    CHECK_AS_EXPECTED(layout_bitmap_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading fuse file");

    layout_bitmap_file.close();

    CHECK_AS_EXPECTED(validate_arch_partial_clusters_bitmap(partial_clusters_layout_bitmap, sku_value),
        HAILO_INTERNAL_FAILURE, "Error, Given SKU value {} doesnt support partial cluster layout {}", sku_value,
        partial_clusters_layout_bitmap);

    return std::make_pair(partial_clusters_layout_bitmap, static_cast<uint8_t>(sku_value));
}

Expected<uint8_t> PartialClusterReader::get_sku_value_from_arch(hailo_device_architecture_t dev_arch)
{
    switch(dev_arch) {
        case HAILO_ARCH_HAILO15H:
            return HAILO15H_SKU_VALUE;
        case HAILO_ARCH_HAILO15M:
            return HAILO15M_SKU_VALUE;
        case HAILO_ARCH_HAILO10H:
            return HAILO10H_SKU_VALUE;
        default:
            LOGGER__ERROR("Error, Unknown sku value for Given architecture {}",
                HailoRTCommon::get_device_arch_str(dev_arch));
            return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

Expected<uint32_t> PartialClusterReader::get_partial_clusters_layout_bitmap(hailo_device_architecture_t dev_arch)
{
    std::pair<uint32_t, uint8_t> fuse_file_data;

    // If file does not exist - get default values for dev_arch
    if (!Filesystem::does_file_exists(std::string(PARTIAL_CLUSTER_READER_CLUSTER_LAYOUT_FILE_PATH))) {
        LOGGER__INFO("partial cluster layout bitmap file not found, Enabling all clusters by default");
        TRY(fuse_file_data.first, get_arch_default_bitmap(dev_arch));
        TRY(fuse_file_data.second, get_sku_value_from_arch(dev_arch));
    } else {
        // This will read bitmap and verify with SKU value
        TRY(fuse_file_data, read_fuse_file());
    }

    const auto sku_value = fuse_file_data.second;
    switch (dev_arch) {
        case HAILO_ARCH_HAILO15H:
            CHECK(HAILO15H_SKU_VALUE == sku_value, HAILO_INTERNAL_FAILURE,
                "Device arch is of type {} but sku is {}", static_cast<int>(dev_arch), sku_value);
            break;
        case HAILO_ARCH_HAILO15M:
            CHECK(HAILO15M_SKU_VALUE == sku_value, HAILO_INTERNAL_FAILURE,
                "Device arch is of type {} but sku is {}", static_cast<int>(dev_arch), sku_value);
            break;
        case HAILO_ARCH_HAILO10H:
            CHECK(HAILO10H_SKU_VALUE == sku_value, HAILO_INTERNAL_FAILURE,
                "Device arch is of type {} but sku is {}", static_cast<int>(dev_arch), sku_value);
            break;
        default:
            LOGGER__ERROR("Error, Device architecture {} doesnt support partial cluster layout", static_cast<int>(dev_arch));
            return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    return Expected<uint32_t>(fuse_file_data.first);
}

Expected<hailo_device_architecture_t> PartialClusterReader::get_actual_dev_arch_from_fuse(hailo_device_architecture_t fw_dev_arch)
{
    // If fuse file does not exist - and fw_dev_arch is HAILO_ARCH_HAILO15H - then it is HAILO_ARCH_HAILO15H
    if (!Filesystem::does_file_exists(std::string(PARTIAL_CLUSTER_READER_CLUSTER_LAYOUT_FILE_PATH))
        && (HAILO_ARCH_HAILO15H == fw_dev_arch)) {
        return HAILO_ARCH_HAILO15H;
    } else {
        TRY(const auto fuse_file_data, read_fuse_file());
        const auto sku_value = fuse_file_data.second;
        if (HAILO15M_SKU_VALUE == sku_value) {
            return HAILO_ARCH_HAILO15M;
        } else if (HAILO15H_SKU_VALUE == sku_value) {
            return HAILO_ARCH_HAILO15H;
        } else if (HAILO10H_SKU_VALUE == sku_value) {
            return HAILO_ARCH_HAILO10H;
        } else {
            LOGGER__ERROR("Error, Invalid sku received {}", sku_value);
            return make_unexpected(HAILO_INVALID_ARGUMENT);
        }
    }
}

} /* namespace hailort */
