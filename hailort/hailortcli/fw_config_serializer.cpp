/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file fw_config_serializer.cpp
 * @brief User firmware configuration serializer.
 **/

#include "fw_config_serializer.hpp"
#include "definitions_json.auto.hpp"
#include "hailo/hailort.h"
#include "user_config_common.h"
#include "common/file_utils.hpp"
#include "common/utils.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>

Expected<ordered_json> FwConfigJsonSerializer::read_json_file(const std::string &file_path)
{
    std::ifstream ifs(file_path);
    CHECK_AS_EXPECTED(ifs.good(), HAILO_OPEN_FILE_FAILURE, "Failed opening json file: {} with errno: {}", file_path, errno);

    ordered_json j;
    ifs >> j;
    CHECK_AS_EXPECTED(ifs.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading json file {}", file_path);
    
    return j;
}

Expected<std::map<std::string, std::map<std::string, ordered_json>>> FwConfigJsonSerializer::get_serialize_map()
{
    ordered_json json_definitions = ordered_json::parse(g_fw_config_str_definitions);
    std::map<std::string, std::map<std::string, ordered_json>> definitions;

    auto version = json_definitions.find("version");
    CHECK_AS_EXPECTED(version != json_definitions.end(), HAILO_INTERNAL_FAILURE,
        "Failed to find version in definitions file");
    definitions["version"]["value"] = version.value();

    auto categories = json_definitions.find("categories");
    CHECK_AS_EXPECTED(categories != json_definitions.end(), HAILO_INTERNAL_FAILURE,
        "Failed to find categories in definitions file");

    uint32_t category_id = 0;
    for (auto &category : categories->items()) {
        auto entries = category.value().find("entries");
        CHECK_AS_EXPECTED(entries != category.value().end(), HAILO_INTERNAL_FAILURE,
            "Failed to find entries of category {} in definition file", category.key());

        std::map<std::string, ordered_json> entries_map;
        uint32_t entry_id = 0;
        for (auto &entry : entries.value().items()) {
            entry.value()["category_id"] = category_id;
            entry.value()["entry_id"] = entry_id;
            entries_map[entry.key()] = entry.value();
            entry_id++;
        }
        definitions[category.key()] = entries_map;
        category_id++;
    }
    return definitions;
}

Expected<std::vector<std::vector<ordered_json>>> FwConfigJsonSerializer::get_deserialize_vector()
{
    ordered_json json_definitions = ordered_json::parse(g_fw_config_str_definitions);

    auto categories = json_definitions.find("categories");
    CHECK_AS_EXPECTED(categories != json_definitions.end(), HAILO_INTERNAL_FAILURE,
        "Failed to find categories in definitions file");

    std::vector<std::vector<ordered_json>> categories_vector;
    for (auto &category : categories.value().items()) {
        auto entries = category.value().find("entries");
        CHECK_AS_EXPECTED(entries != category.value().end(), HAILO_INTERNAL_FAILURE,
            "Failed to find entries of category {} in definitions file", category.key());

        std::vector<ordered_json> entries_vector;
        for (auto &entry : entries.value().items()) {
            entry.value()["category_name"] = category.key();
            entry.value()["entry_name"] = entry.key();
            entries_vector.emplace_back(entry.value());
        }
        categories_vector.emplace_back(entries_vector);
    }  
    return categories_vector;
}

hailo_status FwConfigJsonSerializer::dump_config(const ordered_json &config_json, const std::string &file_path)
{
    if (file_path.empty()) {
        std::cout << config_json.dump(JSON_PRINT_INDENTATION) << std::endl; 
        CHECK(std::cout.good(), HAILO_FILE_OPERATION_FAILURE, "Failed writing config to stdout");
    }
    else {
        std::ofstream ofs(file_path);
        CHECK(ofs.good(), HAILO_OPEN_FILE_FAILURE, "Failed opening output file: {} with errno: {}", file_path, errno);
        
        ofs << config_json.dump(JSON_PRINT_INDENTATION) << std::endl;
        CHECK(ofs.good(), HAILO_FILE_OPERATION_FAILURE, "Failed writing firmware configuration into file: {} with errno: {}", file_path, errno);
    }

    return HAILO_SUCCESS;
}

/* Deserialization */
hailo_status FwConfigJsonSerializer::deserialize_config(const USER_CONFIG_header_t &user_config_header, size_t config_size, const std::string &file_path)
{
    try {
        TRY(const auto categories, get_deserialize_vector());

        ordered_json config_json;
        size_t current_deserialized_data_size = 0;
        uintptr_t current_entry_offset = (uintptr_t)(&(user_config_header.entries));
        for (size_t i = 0; i < user_config_header.entry_count; i++) {
            USER_CONFIG_ENTRY_t *config_entry = reinterpret_cast<USER_CONFIG_ENTRY_t*>(current_entry_offset);
            CHECK(config_entry->category < categories.size(), HAILO_INTERNAL_FAILURE,
                "Category id is out of bounds. Category id = {}, Max category id = {}", config_entry->category, (categories.size()-1));
            
            auto category = categories[config_entry->category];
            CHECK(config_entry->entry_id < category.size(), HAILO_INTERNAL_FAILURE,
                "Entry id is out of bounds. Entry id = {}, Max entry id = {}", config_entry->entry_id, (category.size() - 1));

            current_deserialized_data_size += sizeof(USER_CONFIG_ENTRY_t) + config_entry->entry_size;
            CHECK((current_deserialized_data_size <= config_size), HAILO_INVALID_OPERATION,
                "Overrun configuration size. Deserialized data size = {}, configuration data size is: {}", current_deserialized_data_size, config_size);

            hailo_status status = FwConfigJsonSerializer::deserialize_entry(config_json,
                category[config_entry->entry_id], reinterpret_cast<uint8_t*>(&(config_entry->value)));
            CHECK_SUCCESS(status);
            
            current_entry_offset += sizeof(USER_CONFIG_ENTRY_t) + config_entry->entry_size;
        }

        hailo_status status = dump_config(config_json, file_path);
        CHECK_SUCCESS(status, "Failed writing firmware configuration");
    }
    catch (json::exception &e) {
        LOGGER__ERROR("Exception caught: {}.\n Please check json definition file format", e.what());
        return HAILO_INTERNAL_FAILURE;
    }
    return HAILO_SUCCESS;
}

hailo_status FwConfigJsonSerializer::deserialize_entry(ordered_json &config_json, const ordered_json &entry_definition, uint8_t *entry_value)
{
    std::string category_name = entry_definition["category_name"].get<std::string>();
    std::string entry_name = entry_definition["entry_name"].get<std::string>();
    std::string deserialize_as = entry_definition["deserialize_as"].get<std::string>();
    uint32_t size = entry_definition.contains("length") ?
        (entry_definition["length"].get<uint32_t>() * entry_definition["size"].get<uint32_t>()) :
        entry_definition["size"].get<uint32_t>();

    if (deserialize_as == "str") {
        TRY(config_json[category_name][entry_name], deserialize_str(entry_value, size));
    }
    else if (deserialize_as == "bool") {
        TRY(config_json[category_name][entry_name], deserialize_bool(entry_value, size));
    }
    else if (deserialize_as == "int") {
        TRY(config_json[category_name][entry_name], deserialize_int(entry_value, size));
    }
    else if (deserialize_as == "i2c_speed") {
        TRY(config_json[category_name][entry_name]["value"], deserialize_i2c_speed(entry_value, size));
    }
    else if (deserialize_as == "supported_aspm_states") {
        TRY(config_json[category_name][entry_name]["value"], deserialize_supported_aspm_states(entry_value, size));
    }
    else if (deserialize_as == "supported_aspm_l1_substates") {
        TRY(config_json[category_name][entry_name]["value"],
            deserialize_supported_aspm_l1_substates(entry_value, size));
    }
    else if (deserialize_as == "ipv4") {
        TRY(config_json[category_name][entry_name]["value"], deserialize_ipv4(entry_value, size));
    }
    else if (deserialize_as == "mac_address") {
        TRY(config_json[category_name][entry_name]["value"], deserialize_mac_address(entry_value, size));
    }
    else if (deserialize_as == "clock_frequency") {
        TRY(config_json[category_name][entry_name]["value"],
            deserialize_clock_frequency(entry_value, size));
    }
    else if (deserialize_as == "logger_level") {
        TRY(config_json[category_name][entry_name]["value"], deserialize_logger_level(entry_value, size));
    }
    else if (deserialize_as == "watchdog_mode") {
        TRY(config_json[category_name][entry_name]["value"], deserialize_watchdog_mode(entry_value, size));
    }
    else if (deserialize_as == "overcurrent_parameters_source") {
        TRY(config_json[category_name][entry_name]["value"],
            deserialize_overcurrent_parameters_source(entry_value, size));
    }
    else if (deserialize_as == "temperature_parameters_source") {
        TRY(config_json[category_name][entry_name]["value"],
            deserialize_temperature_parameters_source(entry_value, size));
    }
    else if (deserialize_as == "conversion_time") {
        TRY(config_json[category_name][entry_name]["value"],
            deserialize_conversion_time(entry_value, size));
    }
    else {
        LOGGER__ERROR("Failed deserializing entry. Serialization format {} not found", deserialize_as);
        return HAILO_NOT_FOUND;
    }

    return HAILO_SUCCESS;
}

Expected<json> FwConfigJsonSerializer::deserialize_str(uint8_t *entry_value, uint32_t size)
{
    return json(std::string((char*)entry_value, strnlen((char*)entry_value, size)));
}

Expected<json> FwConfigJsonSerializer::deserialize_bool(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto bool_val, get_int_value<uint8_t>(entry_value, size));
    json bool_str = bool_val ? true : false;
    return bool_str;
}

Expected<json> FwConfigJsonSerializer::deserialize_ipv4(uint8_t *entry_value, uint32_t size)
{
    CHECK_AS_EXPECTED((IPV4_ADDRESS_LENGTH == size), HAILO_INTERNAL_FAILURE,
        "IPv4 address length is invalid. Length recieved is: {}, length expected: {}", size, IPV4_ADDRESS_LENGTH);

    std::stringstream ss;
    for (size_t i = 0; i < IPV4_ADDRESS_LENGTH; i++) {
        if (i != 0) {
            ss << '.';
        }
        //TODO: HRT-3045 - Support big-endian.
        ss << (int)(entry_value[IPV4_ADDRESS_LENGTH-i-1]);
    }
    return json(ss.str());
}

Expected<json> FwConfigJsonSerializer::deserialize_mac_address(uint8_t *entry_value, uint32_t size)
{
    CHECK_AS_EXPECTED((MAC_ADDRESS_LENGTH == size), HAILO_INTERNAL_FAILURE,
        "Mac address length is invalid. Length recieved is: {}, length expected: {}", size, MAC_ADDRESS_LENGTH);
    
    const bool UPPERCASE = true;
    return json(StringUtils::to_hex_string(entry_value, size, UPPERCASE, ":"));
}

Expected<json> FwConfigJsonSerializer::deserialize_supported_aspm_states(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto aspm_state, get_int_value<uint8_t>(entry_value, size));

    switch (static_cast<PCIE_CONFIG_SUPPOPRTED_ASPM_STATES_t>(aspm_state)) {
    case ASPM_DISABLED:
        return json("ASPM DISABLED");
    case ASPM_L1_ONLY:
        return json("ASPM L1 ONLY");
    case ASPM_L0S_L1:
        return json("ASPM L0S L1");
    default:
        LOGGER__ERROR("Failed deserializing supported aspm states.");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<json> FwConfigJsonSerializer::deserialize_supported_aspm_l1_substates(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto aspm_l1_substate, get_int_value<uint8_t>(entry_value, size));

    switch (static_cast<PCIE_CONFIG_SUPPOPRTED_L1_ASPM_SUBSTATES_t>(aspm_l1_substate)) {
    case ASPM_L1_SUBSTATES_DISABLED:
        return json("ASPM L1 SUBSTATES DISABLED");
    case ASPM_L1_SUBSTATES_L11_ONLY:
        return json("ASPM L1 SUBSTATES L1.1 ONLY");
    case ASPM_L1_SUBSTATES_L11_L12:
        return json("ASPM L1 SUBSTATES L1.1 L1.2");
    default:
        LOGGER__ERROR("Failed deserializing supported aspm l1 substates.");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<json> FwConfigJsonSerializer::deserialize_clock_frequency(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto clock_frequency, get_int_value<uint32_t>(entry_value, size));

    switch (clock_frequency) {
    case SOC__NN_CLOCK_400MHz:
        return json("400MHZ");
    case SOC__NN_CLOCK_375MHz:
        return json("375MHZ");
    case SOC__NN_CLOCK_350MHz:
        return json("350MHZ");
    case SOC__NN_CLOCK_325MHz:
        return json("325MHZ");
    case SOC__NN_CLOCK_300MHz:
        return json("300MHZ");
    case SOC__NN_CLOCK_275MHz:
        return json("275MHZ");
    case SOC__NN_CLOCK_250MHz:
        return json("250MHZ");
    case SOC__NN_CLOCK_225MHz:
        return json("225MHZ");
    case SOC__NN_CLOCK_200MHz:
        return json("200MHZ");
    case SOC__NN_CLOCK_100MHz:
        return json("100MHZ");
    default:
        LOGGER__ERROR("Failed deserializing clock_frequency.");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<json> FwConfigJsonSerializer::deserialize_watchdog_mode(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto watchdog_mode, get_int_value<uint8_t>(entry_value, size));

    switch (static_cast<WD_SERVICE_wd_mode_t>(watchdog_mode)) {
    case WD_SERVICE_MODE_HW_SW:
        return json("WD MODE HW SW");
    case WD_SERVICE_MODE_HW_ONLY:
        return json("WD MODE HW ONLY");
    default:
        LOGGER__ERROR("Failed deserializing watchdog_mode.");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<json> FwConfigJsonSerializer::deserialize_i2c_speed(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto i2c_speed, get_int_value<uint8_t>(entry_value, size));

    switch (static_cast<i2c_speed_mode_t>(i2c_speed)) {
    case I2C_SPEED_STANDARD:
        return json("I2C SPEED STANDARD");
    case I2C_SPEED_FAST:
        return json("I2C SPEED FAST");
    default:
        LOGGER__ERROR("Failed deserializing i2c speed");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<json> FwConfigJsonSerializer::deserialize_logger_level(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto logger_level, get_int_value<uint8_t>(entry_value, size));

    switch (static_cast<FW_LOGGER_LEVEL_t>(logger_level)) {
    case FW_LOGGER_LEVEL_TRACE:
        return json("TRACE");
    case FW_LOGGER_LEVEL_DEBUG:
        return json("DEBUG");
    case FW_LOGGER_LEVEL_INFO:
        return json("INFO");
    case FW_LOGGER_LEVEL_WARN:
        return json("WARNING");
    case FW_LOGGER_LEVEL_ERROR:
        return json("ERROR");
    case FW_LOGGER_LEVEL_FATAL:
        return json("FATAL");
    default:
        LOGGER__ERROR("Failed deserializing logger_level");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<json> FwConfigJsonSerializer::deserialize_overcurrent_parameters_source(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto overcurrent_parameters_source, get_int_value<uint8_t>(entry_value, size));

    switch (static_cast<OVERCURRENT_parameters_source_t>(overcurrent_parameters_source)) {
    case OVERCURRENT_PARAMETERS_SOURCE_FW_VALUES:
        return json("FW VALUES");
    case OVERCURRENT_PARAMETERS_SOURCE_USER_CONFIG_VALUES:
        return json("USER CONFIG VALUES");
    case OVERCURRENT_PARAMETERS_SOURCE_BOARD_CONFIG_VALUES:
        return json("BOARD CONFIG VALUES");
    case OVERCURRENT_PARAMETERS_SOURCE_OVERCURRENT_DISABLED:
        return json("OVERCURRENT DISABLED");
    default:
        LOGGER__ERROR("Failed deserializing overcurrent thresholds source");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<json> FwConfigJsonSerializer::deserialize_temperature_parameters_source(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto temperature_parameters_source, get_int_value<uint8_t>(entry_value, size));

    switch (static_cast<TEMPERATURE_PROTECTION_parameters_source_t>(temperature_parameters_source)) {
    case TEMPERATURE_PROTECTION_PARAMETERS_SOURCE_FW_VALUES:
        return json("FW VALUES");
    case TEMPERATURE_PROTECTION_PARAMETERS_SOURCE_USER_CONFIG_VALUES:
        return json("USER CONFIG VALUES");
    default:
        LOGGER__ERROR("Failed deserializing overcurrent thresholds source");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<json> FwConfigJsonSerializer::deserialize_conversion_time(uint8_t *entry_value, uint32_t size)
{
    TRY(const auto conversion_time, get_int_value<uint32_t>(entry_value, size));
    auto conversion_time_value = static_cast<OVERCURRENT_conversion_time_us_t>(conversion_time);

    if (conversion_time_value == OVERCURRENT_CONVERSION_PERIOD_140US ||
        conversion_time_value == OVERCURRENT_CONVERSION_PERIOD_204US ||
        conversion_time_value == OVERCURRENT_CONVERSION_PERIOD_332US ||
        conversion_time_value == OVERCURRENT_CONVERSION_PERIOD_588US ||
        conversion_time_value == OVERCURRENT_CONVERSION_PERIOD_1100US ||
        conversion_time_value == OVERCURRENT_CONVERSION_PERIOD_2116US ||
        conversion_time_value == OVERCURRENT_CONVERSION_PERIOD_4156US ||
        conversion_time_value == OVERCURRENT_CONVERSION_PERIOD_8244US) {

        return json(conversion_time_value);
    }
    else {
        LOGGER__ERROR("Got unvalid valid option for conversion_time.");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<json> FwConfigJsonSerializer::deserialize_int(uint8_t *entry_value, uint32_t size)
{
    switch (size) {
    case sizeof(uint8_t):
    {
        TRY(const auto uint8_val, get_int_value<uint8_t>(entry_value, size));
        return json(uint8_val);
    }
    case sizeof(uint16_t):
    {
        TRY(const auto uint16_val, get_int_value<uint16_t>(entry_value, size));
        return json(uint16_val);
    }
    case sizeof(uint32_t):
    {
        TRY(const auto uint32_val, get_int_value<uint32_t>(entry_value, size));
        return json(uint32_val);
    }
    default:
        LOGGER__ERROR("Failed deserializing int value");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

/* Serialization */
Expected<uint32_t> FwConfigJsonSerializer::serialize_config(USER_CONFIG_header_t &user_config_header, size_t config_size, const std::string &file_path)
{
    size_t data_size = sizeof(USER_CONFIG_header_t);

    try {
        TRY_V(const auto config_json, FwConfigJsonSerializer::read_json_file(file_path));
        TRY(auto definitions, FwConfigJsonSerializer::get_serialize_map());

        user_config_header.version = definitions["version"]["value"].get<uint32_t>();
        user_config_header.magic = USER_CONFIG_MAGIC;
        user_config_header.entry_count = 0;

        uintptr_t current_entry_offset = (uintptr_t)(&(user_config_header.entries));
        for (auto &config_category : config_json.items()) {
            for (auto &config_entry : config_category.value().items()) {
                ordered_json entry_definition = definitions[config_category.key()][config_entry.key()];
                USER_CONFIG_ENTRY_t *curr_entry = (USER_CONFIG_ENTRY_t *)current_entry_offset;
                curr_entry->entry_size = entry_definition.contains("length") ?
                    (entry_definition["length"].get<uint32_t>() * entry_definition["size"].get<uint32_t>()) :
                    entry_definition["size"].get<uint32_t>();

                data_size += sizeof(USER_CONFIG_ENTRY_t) + curr_entry->entry_size;
                CHECK_AS_EXPECTED((data_size <= config_size), HAILO_INVALID_OPERATION,
                    "User config overrun! Configuration is too big, data size {}, firmware configuration size: {}",
                    data_size, config_size);

                hailo_status status = serialize_entry(*curr_entry, config_entry.value(), entry_definition);
                CHECK_SUCCESS_AS_EXPECTED(status, "Failed serializing json config category: {}, entry: {}",
                    config_category.key(), config_entry.key());
                    
                user_config_header.entry_count++;
                current_entry_offset += sizeof(USER_CONFIG_ENTRY_t) + curr_entry->entry_size;
            }
        }
    }
    catch (json::exception &e) {
        LOGGER__ERROR("Exception caught: {}.\n Please check json files format", e.what());
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    CHECK_AS_EXPECTED(((std::numeric_limits<uint32_t>::min() <= data_size) && (data_size <= std::numeric_limits<uint32_t>::max())), 
        HAILO_INTERNAL_FAILURE, "Firmware configuration data size is out of bounds. data_size = {}", data_size);
    
    return static_cast<uint32_t>(data_size);
}

hailo_status FwConfigJsonSerializer::serialize_entry(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry, const ordered_json &entry_definition)
{
    ordered_json inner_config_entry = config_entry;
    entry.category = entry_definition["category_id"].get<uint16_t>();
    entry.entry_id = entry_definition["entry_id"].get<uint16_t>();

    std::string deserialize_as = entry_definition["deserialize_as"].get<std::string>();

    if (deserialize_as == "str") {
        return serialize_str(entry, inner_config_entry);
    }
    else if (deserialize_as == "bool") {
        return serialize_bool(entry, inner_config_entry);
    }
    else if (deserialize_as == "int") {
        return serialize_int_by_size(entry, inner_config_entry);
    }
    else {
        if (!config_entry.contains("value"))
        {
            inner_config_entry = {{"value", config_entry}};
        }
        if (deserialize_as == "ipv4") {
            return serialize_ipv4(entry, inner_config_entry);
        }
        else if (deserialize_as == "i2c_speed") {
            return serialize_i2c_speed(entry, inner_config_entry);
        }
        else if (deserialize_as == "supported_aspm_states") {
            return serialize_supported_aspm_states(entry, inner_config_entry);
        }
        else if (deserialize_as == "supported_aspm_l1_substates") {
            return serialize_supported_aspm_l1_substates(entry, inner_config_entry);
        }
        else if (deserialize_as == "mac_address") {
            return serialize_mac_address(entry, inner_config_entry);
        }
        else if (deserialize_as == "clock_frequency") {
            return serialize_clock_frequency(entry, inner_config_entry);
        }
        else if (deserialize_as == "logger_level") {
            return serialize_logger_level(entry, inner_config_entry);
        }
        else if (deserialize_as == "watchdog_mode") {
            return serialize_watchdog_mode(entry, inner_config_entry);
        }
        else if (deserialize_as == "overcurrent_parameters_source") {
            return serialize_overcurrent_parameters_source(entry, inner_config_entry);
        }
        else if (deserialize_as == "temperature_parameters_source") {
            return serialize_temperature_parameters_source(entry, inner_config_entry);
        }
        else if (deserialize_as == "conversion_time") {
            return serialize_conversion_time(entry, inner_config_entry);
        }
        else {
            LOGGER__ERROR("Failed serializing entry. Serialization format {} not found", deserialize_as);
            return HAILO_NOT_FOUND;
        }
    }

}

hailo_status FwConfigJsonSerializer::serialize_str(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string str = config_entry.get<std::string>();

    CHECK(0 != str.length(), HAILO_INVALID_ARGUMENT, "Failed serializing string. String length can't be 0.");

    CHECK(entry.entry_size >= str.length(), HAILO_INVALID_ARGUMENT,
        "Failed serializing string value {}. String length must be equal or shorter than {}", str, entry.entry_size);
    
    memset(&(entry.value), 0, entry.entry_size);
    memcpy(&(entry.value), str.c_str(), str.length());

    return HAILO_SUCCESS;
}

hailo_status FwConfigJsonSerializer::serialize_bool(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    uint8_t bool_value = config_entry.get<uint8_t>();
    return serialize_int(entry, bool_value);    
}

hailo_status FwConfigJsonSerializer::serialize_ipv4(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    CHECK(IPV4_ADDRESS_LENGTH == entry.entry_size, HAILO_INVALID_ARGUMENT,
        "IPv4 entry size is incompatible. Size recieved: {}, size expected: {}", entry.entry_size, IPV4_ADDRESS_LENGTH);
    
    std::string ipv4_str = config_entry["value"].get<std::string>();
    uint8_t ip[IPV4_ADDRESS_LENGTH];

    auto length = sscanf(ipv4_str.c_str(), "%hhd.%hhd.%hhd.%hhd", &ip[3], &ip[2], &ip[1], &ip[0]);
    CHECK(IPV4_ADDRESS_LENGTH == length, HAILO_INVALID_ARGUMENT, "Failed serializing ipv4: {}", ipv4_str);
    
    memcpy(&(entry.value), &ip, entry.entry_size);

    return HAILO_SUCCESS;
}

hailo_status FwConfigJsonSerializer::serialize_i2c_speed(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string i2c_speed = config_entry["value"].get<std::string>();
    uint8_t val = 0;
    if (("I2C_SPEED_STANDARD" == i2c_speed) || ("I2C SPEED STANDARD" == i2c_speed)) {
        val = I2C_SPEED_STANDARD;
    }
    else if (("I2C_SPEED_FAST" == i2c_speed) || ("I2C SPEED FAST" == i2c_speed)) {
        val = I2C_SPEED_FAST;
    }
    else {
        LOGGER__ERROR("Failed serializing i2c speed {}", i2c_speed);
        return HAILO_NOT_FOUND;
    }

    return serialize_int(entry, val);    
}

hailo_status FwConfigJsonSerializer::serialize_logger_level(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string logger_level = config_entry["value"].get<std::string>();
    uint8_t val = 0;

    if ("TRACE" == logger_level) {
        val = FW_LOGGER_LEVEL_TRACE;
    }
    else if ("DEBUG" == logger_level) {
        val = FW_LOGGER_LEVEL_DEBUG;
    }
    else if ("INFO" == logger_level) {
        val = FW_LOGGER_LEVEL_INFO;
    }
    else if ("WARNING" == logger_level) {
        val = FW_LOGGER_LEVEL_WARN;
    }
    else if ("ERROR" == logger_level) {
        val = FW_LOGGER_LEVEL_ERROR;
    }
    else if ("FATAL" == logger_level) {
        val = FW_LOGGER_LEVEL_FATAL;
    }
    else {
        LOGGER__ERROR("Failed serializing logger_level {}", logger_level);
        return HAILO_NOT_FOUND;
    }

    return serialize_int(entry, val);
}

hailo_status FwConfigJsonSerializer::serialize_supported_aspm_states(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string aspm_state = config_entry["value"].get<std::string>();

    uint8_t val = 0;
    if (("ASPM_DISABLED" == aspm_state) || ("ASPM DISABLED" == aspm_state)) {
        val = ASPM_DISABLED;
    }
    else if (("ASPM_L1_ONLY" == aspm_state) || ("ASPM L1 ONLY" == aspm_state)) {
        val = ASPM_L1_ONLY;
    }
    else if (("ASPM_L0S_L1" == aspm_state) || ("ASPM L0S L1" == aspm_state)) {
        val = ASPM_L0S_L1;
    }
    else {
        LOGGER__ERROR("Failed serializing supported aspm state {}.", aspm_state);
        return HAILO_NOT_FOUND;
    }

    return serialize_int(entry, val);    
}

hailo_status FwConfigJsonSerializer::serialize_supported_aspm_l1_substates(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string aspm_l1_substate = config_entry["value"].get<std::string>();

    uint8_t val = 0;
    if (("ASPM_L1_SUBSTATES_DISABLED" == aspm_l1_substate) || ("ASPM L1 SUBSTATES DISABLED" == aspm_l1_substate)) {
        val = ASPM_L1_SUBSTATES_DISABLED;
    }
    else if (("ASPM_L1_SUBSTATES_L11_ONLY" == aspm_l1_substate) || ("ASPM L1 SUBSTATES L1.1 ONLY" == aspm_l1_substate)) {
        val = ASPM_L1_SUBSTATES_L11_ONLY;
    }
    else if (("ASPM_L1_SUBSTATES_L11_L12" == aspm_l1_substate) || ("ASPM L1 SUBSTATES L1.1 L1.2" == aspm_l1_substate)) {
        val = ASPM_L1_SUBSTATES_L11_L12;
    }
    else {
        LOGGER__ERROR("Failed serializing supported aspm l1 substate {}", aspm_l1_substate);
        return HAILO_NOT_FOUND;
    }

    return serialize_int(entry, val);    
}

hailo_status FwConfigJsonSerializer::serialize_mac_address(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string mac_addr_str = config_entry["value"].get<std::string>();

    uint8_t mac_address[MAC_ADDRESS_LENGTH];
    auto length = std::sscanf(mac_addr_str.c_str(),
                    "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                    &mac_address[0], &mac_address[1], &mac_address[2],
                    &mac_address[3], &mac_address[4], &mac_address[5]);
    CHECK(MAC_ADDRESS_LENGTH == length, HAILO_INVALID_ARGUMENT, "Failed serializing mac address {}", mac_addr_str);

    memcpy(&(entry.value), &mac_address, entry.entry_size);

    return HAILO_SUCCESS;
}

hailo_status FwConfigJsonSerializer::serialize_clock_frequency(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string clock_frequency = config_entry["value"].get<std::string>();

    uint32_t val = 0;
    if ("400MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_400MHz;
    }
    else if ("375MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_375MHz;
    }
    else if ("350MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_350MHz;
    }
    else if ("325MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_325MHz;
    }
    else if ("300MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_300MHz;
    }
    else if ("275MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_275MHz;
    }
    else if ("250MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_250MHz;
    }
    else if ("225MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_225MHz;
    }
    else if ("200MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_200MHz;
    }
    else if ("100MHZ" == clock_frequency) {
        val = SOC__NN_CLOCK_100MHz;
    }
    else {
        LOGGER__ERROR("Failed serializing clock_frequency {}", clock_frequency);
        return HAILO_NOT_FOUND;
    }

    return serialize_int(entry, val);    
}

hailo_status FwConfigJsonSerializer::serialize_watchdog_mode(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string watchdog_mode = config_entry["value"].get<std::string>();

    uint8_t val = 0;
    if (("WD_MODE_HW_SW" == watchdog_mode) || ("WD MODE HW SW" == watchdog_mode)) {
        val = 0;
    }
    else if (("WD_MODE_HW_ONLY" == watchdog_mode) || ("WD MODE HW ONLY" == watchdog_mode)) {
        val = 1;
    }
    else {
        LOGGER__ERROR("Failed serializing watchdog_mode {}", watchdog_mode);
        return HAILO_NOT_FOUND;
    }

    return serialize_int(entry, val);
}

hailo_status FwConfigJsonSerializer::serialize_overcurrent_parameters_source(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string overcurrent_parameters_source = config_entry["value"].get<std::string>();

    uint8_t val = 0;
    if (("FW_VALUES" == overcurrent_parameters_source) || ("FW VALUES" == overcurrent_parameters_source)) {
        val = OVERCURRENT_PARAMETERS_SOURCE_FW_VALUES;
    }
    else if (("USER_CONFIG_VALUES" == overcurrent_parameters_source) || ("USER CONFIG VALUES" == overcurrent_parameters_source)) {
        val = OVERCURRENT_PARAMETERS_SOURCE_USER_CONFIG_VALUES;
    }
    else if (("BOARD_CONFIG_VALUES" == overcurrent_parameters_source) || ("BOARD CONFIG VALUES" == overcurrent_parameters_source)) {
        val = OVERCURRENT_PARAMETERS_SOURCE_BOARD_CONFIG_VALUES;
    }
    else if (("OVERCURRENT_DISABLED" == overcurrent_parameters_source) || ("OVERCURRENT DISABLED" == overcurrent_parameters_source)) {
        val = OVERCURRENT_PARAMETERS_SOURCE_OVERCURRENT_DISABLED;
    }
    else {
        LOGGER__ERROR("Failed serializing overcurrent_parameters_source {}", overcurrent_parameters_source);
        return HAILO_NOT_FOUND;
    }

    return serialize_int(entry, val);
}

hailo_status FwConfigJsonSerializer::serialize_temperature_parameters_source(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    std::string temperature_parameters_source = config_entry["value"].get<std::string>();

    uint8_t val = 0;
    if (("FW_VALUES" == temperature_parameters_source) || ("FW VALUES" == temperature_parameters_source)) {
        val = TEMPERATURE_PROTECTION_PARAMETERS_SOURCE_FW_VALUES;
    }
    else if (("USER_CONFIG_VALUES" == temperature_parameters_source) || ("USER CONFIG VALUES" == temperature_parameters_source)) {
        val = TEMPERATURE_PROTECTION_PARAMETERS_SOURCE_USER_CONFIG_VALUES;
    }
    else {
        LOGGER__ERROR("Failed serializing temperature_parameters_source {}", temperature_parameters_source);
        return HAILO_NOT_FOUND;
    }

    return serialize_int(entry, val);
}

hailo_status FwConfigJsonSerializer::serialize_conversion_time(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    uint32_t conversion_time = config_entry["value"].get<uint32_t>();
    uint32_t val = 0;

    if (conversion_time == OVERCURRENT_CONVERSION_PERIOD_140US ||
        conversion_time == OVERCURRENT_CONVERSION_PERIOD_204US ||
        conversion_time == OVERCURRENT_CONVERSION_PERIOD_332US ||
        conversion_time == OVERCURRENT_CONVERSION_PERIOD_588US ||
        conversion_time == OVERCURRENT_CONVERSION_PERIOD_1100US ||
        conversion_time == OVERCURRENT_CONVERSION_PERIOD_2116US ||
        conversion_time == OVERCURRENT_CONVERSION_PERIOD_4156US ||
        conversion_time == OVERCURRENT_CONVERSION_PERIOD_8244US) {

        val = conversion_time;
    }
    else {
        LOGGER__ERROR("Failed serializing conversion_time {}", conversion_time);
        return HAILO_NOT_FOUND;
    }

    return serialize_int(entry, val);    
}

hailo_status FwConfigJsonSerializer::serialize_int_by_size(USER_CONFIG_ENTRY_t &entry, const ordered_json &config_entry)
{
    int64_t value = config_entry.get<int64_t>();
    if (sizeof(uint8_t) == entry.entry_size) {
        CHECK(((std::numeric_limits<uint8_t>::min() <= value) && (value <= std::numeric_limits<uint8_t>::max())),
            HAILO_INVALID_ARGUMENT, "Failed serializing uint8_t value: {}. Value is out of numeric limits", value);

        return serialize_int<uint8_t>(entry, static_cast<uint8_t>(value));
    }
    else if (sizeof(uint16_t) == entry.entry_size) {
        CHECK(((std::numeric_limits<uint16_t>::min() <= value) && (value <= std::numeric_limits<uint16_t>::max())),
            HAILO_INVALID_ARGUMENT, "Failed serializing uint16_t value: {}. Value is out of numeric limits", value);
        
        return serialize_int<uint16_t>(entry, static_cast<uint16_t>(value));
    }
    else if (sizeof(uint32_t)  == entry.entry_size) {
        CHECK(((std::numeric_limits<uint32_t>::min() <= value) && (value <= std::numeric_limits<uint32_t>::max())),
            HAILO_INVALID_ARGUMENT, "Failed serializing uint32_t value: {}. Value is out of numeric limits", value);
        
        return serialize_int<uint32_t>(entry, static_cast<uint32_t>(value));
    }
    else {
        LOGGER__ERROR("Failed serializing int value. Invalid size {}", entry.entry_size);
        return HAILO_NOT_FOUND;
    }
}