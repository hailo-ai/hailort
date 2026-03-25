/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file soc_utils.cpp
 * @brief SOC related utilities.
 **/

#include "soc_utils.hpp"

#include "common/utils.hpp"
#include "common/filesystem.hpp"
#include "hailo/hailort_common.hpp"

#include <algorithm>
#include <array>
#include <fstream>

namespace hailort
{
namespace soc_utils
{

// TODO: HRT-16652 - support more architecture's SKU
// SKU is three bit value in fuse file in order to differentiate the different kind of boards
#define SKU_VALUE_BITMAP   (0x7)
#define HAILO15H_SKU_VALUE (0x0)
#define HAILO10H_SKU_VALUE (0x1)
#define HAILO15M_SKU_VALUE (0x3)

// SKU and partial cluster layout bitmap are located at specific locations in the fuse file according to the spec
// Located in issue HRT-12971
#define SKU_BYTE_INDEX_IN_FUSE_FILE                     (32)
#define SKU_BIT_INDEX_IN_WORD                           (18)
#define ACTIVE_CLUSTER_LAYOUT_BITMAP_INDEX_IN_FUSE_FILE (80)

constexpr const char *PARTIAL_CLUSTER_READER_CLUSTER_LAYOUT_FILE_PATH = "/sys/devices/soc0/fuse";

// Array that has all the valid layouts for Hailo15M
static constexpr std::array<uint32_t, 3> HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY = {
    PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_0,
    PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_1,
    PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15M_2
};

constexpr const char *DEVICE_ARCHITECTURE_FILE_PATH = "/sys/devices/soc0/product";

static Expected<hailo_device_architecture_t> read_device_architecture_file()
{
    bool file_exists = Filesystem::does_file_exists(std::string(DEVICE_ARCHITECTURE_FILE_PATH));
    CHECK(file_exists, HAILO_FILE_OPERATION_FAILURE, "Architecture file not found");

    std::ifstream arch_file(DEVICE_ARCHITECTURE_FILE_PATH);
    CHECK(arch_file.good(), HAILO_OPEN_FILE_FAILURE, "Failed to open architecture file");

    std::string arch_str;
    std::getline(arch_file, arch_str);
    CHECK(!arch_str.empty(), HAILO_FILE_OPERATION_FAILURE, "Failed to read architecture file");

    if ("Hailo-15H" == arch_str) return HAILO_ARCH_HAILO15H;
    if ("Hailo-15L" == arch_str) return HAILO_ARCH_HAILO15L;
    if ("Hailo-15M" == arch_str) return HAILO_ARCH_HAILO15M;
    if ("Hailo-10H" == arch_str) return HAILO_ARCH_HAILO10H;
    if ("Hailo-12L" == arch_str) return HAILO_ARCH_HAILO12L;

    LOGGER__ERROR("Invalid device architecture string: {}", arch_str);
    return make_unexpected(HAILO_NOT_SUPPORTED);
}

Expected<hailo_device_architecture_t> get_device_architecture()
{
    static hailo_device_architecture_t arch = HAILO_ARCH_MAX_ENUM;

    if (arch == HAILO_ARCH_MAX_ENUM) {
        TRY(arch, read_device_architecture_file());
    }

    auto arch_result = arch;
    return arch_result;
}

static Expected<uint32_t> get_arch_default_bitmap(hailo_device_architecture_t dev_arch)
{
    switch (dev_arch) {
    // Currently only supported architectures for this function are HAILO15H and HAILO15M - but in future can add
    case HAILO_ARCH_HAILO15H:
    case HAILO_ARCH_HAILO15M:
    case HAILO_ARCH_HAILO10H:
        return static_cast<uint32_t>(PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15_DEFAULT);
    default:
        LOGGER__ERROR("Arch {} doesnt support partial cluster layout", HailoRTCommon::get_device_arch_str(dev_arch));
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

static bool is_arch_partial_clusters_bitmap_valid(uint32_t bitmap, uint8_t sku_value)
{
    switch (sku_value) {
    case HAILO15H_SKU_VALUE:
    case HAILO10H_SKU_VALUE:
        return (PARTIAL_CLUSTERS_LAYOUT_BITMAP__HAILO15_DEFAULT == bitmap);
    case HAILO15M_SKU_VALUE:
        return (std::find(HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY.begin(),
            HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY.end(),
            bitmap) != HAILO15M__PARTIAL_CLUSTERS_LAYOUT_BITMAP_ARRAY.end());
    default:
        return false;
    }
}

static Expected<std::pair<uint32_t, uint8_t>> read_fuse_file()
{
    std::ifstream layout_bitmap_file(PARTIAL_CLUSTER_READER_CLUSTER_LAYOUT_FILE_PATH, std::ios::binary);
    CHECK(layout_bitmap_file.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed opening layout bitmap file");

    // SKU is located at SKU_BYTE_INDEX_IN_FUSE_FILE
    layout_bitmap_file.seekg(SKU_BYTE_INDEX_IN_FUSE_FILE, std::ios::beg);
    CHECK(layout_bitmap_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed seek in fuse file");

    // Read SKU value from file as well to validate arch type
    uint32_t misc_word = 0;
    layout_bitmap_file.read(reinterpret_cast<char *>(&misc_word), sizeof(misc_word));
    CHECK(layout_bitmap_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading fuse file");
    uint8_t sku_value = ((misc_word >> SKU_BIT_INDEX_IN_WORD) & SKU_VALUE_BITMAP);

    // active clusters bitmap is located at ACTIVE_CLUSTER_LAYOUT_BITMAP_INDEX_IN_FUSE_FILE
    layout_bitmap_file.seekg(ACTIVE_CLUSTER_LAYOUT_BITMAP_INDEX_IN_FUSE_FILE, std::ios::beg);
    CHECK(layout_bitmap_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed seek in fuse file");

    uint32_t partial_clusters_layout_bitmap = 0;
    layout_bitmap_file.read(reinterpret_cast<char *>(&partial_clusters_layout_bitmap),
        sizeof(partial_clusters_layout_bitmap));
    CHECK(layout_bitmap_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading fuse file");

    CHECK(is_arch_partial_clusters_bitmap_valid(partial_clusters_layout_bitmap, sku_value),
        HAILO_INTERNAL_FAILURE, "Error, Given SKU value {} doesnt support partial cluster layout {}", sku_value,
        partial_clusters_layout_bitmap);

    return std::make_pair(partial_clusters_layout_bitmap, static_cast<uint8_t>(sku_value));
}

static Expected<uint8_t> get_sku_value_from_arch(hailo_device_architecture_t dev_arch)
{
    switch (dev_arch) {
    case HAILO_ARCH_HAILO15H: return HAILO15H_SKU_VALUE;
    case HAILO_ARCH_HAILO15M: return HAILO15M_SKU_VALUE;
    case HAILO_ARCH_HAILO10H: return HAILO10H_SKU_VALUE;
    default:
        LOGGER__ERROR("Unknown sku value for arch {}", HailoRTCommon::get_device_arch_str(dev_arch));
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

Expected<uint32_t> get_partial_clusters_layout_bitmap(hailo_device_architecture_t dev_arch)
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
        CHECK(HAILO15H_SKU_VALUE == sku_value, HAILO_INTERNAL_FAILURE, "Device arch is of type {} but sku is {}",
            static_cast<int>(dev_arch), sku_value);
        break;
    case HAILO_ARCH_HAILO15M:
        CHECK(HAILO15M_SKU_VALUE == sku_value, HAILO_INTERNAL_FAILURE, "Device arch is of type {} but sku is {}",
            static_cast<int>(dev_arch), sku_value);
        break;
    case HAILO_ARCH_HAILO10H:
        CHECK(HAILO10H_SKU_VALUE == sku_value, HAILO_INTERNAL_FAILURE, "Device arch is of type {} but sku is {}",
            static_cast<int>(dev_arch), sku_value);
        break;
    default:
        LOGGER__ERROR("Arch {} doesn't support partial cluster layout", static_cast<int>(dev_arch));
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    return Expected<uint32_t>(fuse_file_data.first);
}

} // namespace soc_utils
} // namespace hailort
