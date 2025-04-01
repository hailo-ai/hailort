/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sensor_config_utils.hpp
 * @brief Utilities for sensor_config operations
 **/

#ifndef _HAILO_SENSOR_CONFIG_UTILS_HPP_
#define _HAILO_SENSOR_CONFIG_UTILS_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "control_protocol.h"

#include <vector>
#include <string>

namespace hailort
{

#define MAX_CONFIG_INFO_ENTRIES (CONTROL_PROTOCOL__MAX_REQUEST_PAYLOAD_SIZE / sizeof(SENSOR_CONFIG__operation_cfg_t))
#define MAX_CONFIG_ENTRIES_DATA_SIZE (MAX_CONFIG_INFO_ENTRIES * sizeof(SENSOR_CONFIG__operation_cfg_t))
#define MAX_NON_ISP_SECTIONS (6)
#define CONFIG_PREFIX_LENGTH (3)
#define CONFIG_HEX_VALUE_LAST_CHAR_OFFSET (9)

static_assert((MAX_CONFIG_INFO_ENTRIES > 0) ,"MAX_CONFIG_INFO_ENTRIES must be larger than 0");

class SensorConfigUtils {
public:
    static Expected<SENSOR_CONFIG_OPCODES_t> get_sensor_opcode_by_name(const std::string &name);
    static Expected<std::vector<SENSOR_CONFIG__operation_cfg_t>> read_config_file(const std::string &config_file_path);
    static Expected<SENSOR_CONFIG__operation_cfg_t> create_config_entry(uint8_t page, uint32_t address, uint8_t length, const std::string &hex_value);
    static Expected<std::vector<SENSOR_CONFIG__operation_cfg_t>> read_isp_config_file(const std::string &isp_static_config_file_path, const std::string &isp_runtime_config_file_path);
    static Expected<std::string> convert_opcode_to_string(uint8_t opcode);
    static hailo_status dump_config_to_csv(SENSOR_CONFIG__operation_cfg_t *operation_cfg, const std::string &config_file_path, uint32_t entries_count);
};

} /* namespace hailort */

#endif /* _HAILO_SENSOR_CONFIG_UTILS_HPP_ */
