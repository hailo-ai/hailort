/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_config_serializer.hpp
 * @brief User firmware configuration serializer.
 **/

#ifndef _HAILO_FW_CONFIG_SERIALIZER_HPP_
#define _HAILO_FW_CONFIG_SERIALIZER_HPP_

#include "hailortcli.hpp"
#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "user_config_common.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json;

#define MAC_ADDRESS_LENGTH (6)
#define IPV4_ADDRESS_LENGTH (4)
#define JSON_PRINT_INDENTATION (4)
#define USER_CONFIG_MAGIC (0x1FF6A40B)

/* NOTE: Tightly coupled with I2C_speed_mode_t defined in "i2c_handler.h"
 Configuring I2c_speed_high is not supported. */
typedef enum {
    I2C_SPEED_STANDARD = 1,
    I2C_SPEED_FAST = 2,
} i2c_speed_mode_t;

// TODO: HRT-3045 - Add support for big-endian serialization
class FwConfigJsonSerializer {
public:
    FwConfigJsonSerializer() = delete;

    static Expected<ordered_json> read_json_file(const std::string &file_path);
    static Expected<std::vector<std::vector<ordered_json>>> get_deserialize_vector();
    static Expected<std::map<std::string, std::map<std::string, ordered_json>>> get_serialize_map();
    static hailo_status dump_config(const ordered_json &config_json, const std::string &file_path);

    static hailo_status deserialize_config(const USER_CONFIG_header_t &user_config_header, size_t config_size, const std::string &file_path);
    static hailo_status deserialize_entry(ordered_json &config_json, const ordered_json &entry_definition, uint8_t *entry_value);
    static Expected<json> deserialize_str(uint8_t *entry_value,  uint32_t size);
    static Expected<json> deserialize_bool(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_supported_aspm_states(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_supported_aspm_l1_substates(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_mac_address(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_i2c_speed(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_logger_level(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_ipv4(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_clock_frequency(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_watchdog_mode(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_overcurrent_parameters_source(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_temperature_parameters_source(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_conversion_time(uint8_t *entry_value, uint32_t size);
    static Expected<json> deserialize_int(uint8_t *entry_value, uint32_t size);

    static Expected<uint32_t> serialize_config(USER_CONFIG_header_t &user_config_header, size_t config_size, const std::string &file_path);
    static hailo_status serialize_entry(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry, const ordered_json &entry_definition);
    static hailo_status serialize_str(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_bool(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_ipv4(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_i2c_speed(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_logger_level(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_supported_aspm_states(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_supported_aspm_l1_substates(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_mac_address(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_clock_frequency(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_watchdog_mode(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_overcurrent_parameters_source(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_temperature_parameters_source(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_conversion_time(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);
    static hailo_status serialize_int_by_size(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry);

    template<typename IntegerType>
    static Expected<IntegerType> get_int_value(uint8_t *entry_value, uint32_t size)
    {
        CHECK_AS_EXPECTED((sizeof(IntegerType) == size), HAILO_INTERNAL_FAILURE, "Entry size is incompatible with integer type");
        auto val = *((IntegerType*)entry_value);
        return val;
    }

    template<typename IntegerType>
    static hailo_status serialize_int(USER_CONFIG_ENTRY_t &entry, IntegerType value)
    {
        CHECK((sizeof(IntegerType) == entry.entry_size), HAILO_INVALID_ARGUMENT,
            "Entry size {} is incompatible with size of integer type {}", entry.entry_size, sizeof(IntegerType));

        auto entry_value_ptr = (IntegerType*)(&entry.value);
        *entry_value_ptr = value;

        return HAILO_SUCCESS;
    }
};

#endif /* _HAILO_FW_CONFIG_SERIALIZER_HPP_ */
