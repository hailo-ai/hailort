/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sensor_config_utils.cpp
 * @brief Utilities for sensor_config operations
 **/

#include "common/utils.hpp"
#include "common/utils.hpp"

#include "utils/sensor_config_utils.hpp"

#include <fstream>
#include <sstream>
#include <iomanip>


namespace hailort
{

Expected<SENSOR_CONFIG_OPCODES_t> SensorConfigUtils::get_sensor_opcode_by_name(const std::string &name)
{
    if (name == "SENSOR_CONFIG_OPCODES_WR") {
        return SENSOR_CONFIG_OPCODES_WR;
    }
    else if (name == "SENSOR_CONFIG_OPCODES_RD") {
        return SENSOR_CONFIG_OPCODES_RD;
    }
    else if (name == "SENSOR_CONFIG_OPCODES_RMW") {
        return SENSOR_CONFIG_OPCODES_RMW;
    }
    else if (name == "SENSOR_CONFIG_OPCODES_DELAY") {
        return SENSOR_CONFIG_OPCODES_DELAY;
    } 
    else { 
        LOGGER__ERROR("Failed getting opcode value by name: {}", name);
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<std::string> SensorConfigUtils::convert_opcode_to_string(uint8_t opcode)
{
    switch (opcode) {
    case SENSOR_CONFIG_OPCODES_WR:
        return std::string("SENSOR_CONFIG_OPCODES_WR");

    case SENSOR_CONFIG_OPCODES_RD:
        return std::string("SENSOR_CONFIG_OPCODES_RD");

    case SENSOR_CONFIG_OPCODES_RMW:
        return std::string("SENSOR_CONFIG_OPCODES_RMW");

    case SENSOR_CONFIG_OPCODES_DELAY:
        return std::string("SENSOR_CONFIG_OPCODES_DELAY");

    default:
        LOGGER__ERROR("Failed converting opcode to string");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

Expected<std::vector<SENSOR_CONFIG__operation_cfg_t>> SensorConfigUtils::read_config_file(const std::string &config_file_path)
{
    std::ifstream config_file;
    config_file.open(config_file_path, std::ios::in);
    CHECK_AS_EXPECTED(config_file.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed opening sensor config file with errno: {}", errno);

    std::vector<SENSOR_CONFIG__operation_cfg_t> control_buffers;
    std::string line;
    std::string col; 

    while(std::getline(config_file, line)) {
        std::stringstream s(line); 
        CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading line in sensor config file with errno: {}", errno);

        SENSOR_CONFIG__operation_cfg_t config_entry = {};
    
        // opcode
        std::getline(s, col, ',' );
        CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading sensor config file opcode with errno: {}", errno);
        auto opcode = get_sensor_opcode_by_name(col);
        CHECK_EXPECTED(opcode, "Failed getting opcode value");
        config_entry.operation = static_cast<uint8_t>(opcode.value());

        // length
        std::getline(s, col, ',' );
        CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading sensor config file length with errno: {}", errno);
        auto length = StringUtils::to_uint8(col, 10);
        CHECK_EXPECTED(length);
        config_entry.length = length.value();

        // page
        std::getline(s, col, ',' );
        CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading sensor config file page with errno: {}", errno);
        auto page = StringUtils::to_int32(col, 16);
        CHECK_EXPECTED(page);
        if (0 > page.value()) {
            config_entry.page = 0xff;
        } else {
            auto page_uint8 = StringUtils::to_uint8(col, 16);
            CHECK_EXPECTED(page_uint8);
            config_entry.page = page_uint8.value();
        }
        
        // address
        std::getline(s, col, ',' );
        CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading sensor config file address with errno: {}", errno);
        auto address = StringUtils::to_uint32(col, 16);
        CHECK_EXPECTED(address);
        config_entry.address = address.value();

        // bitmask
        std::getline(s, col, ',' );
        CHECK_AS_EXPECTED(s.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading sensor config file bitmask with errno: {}", errno);
        auto bitmask = StringUtils::to_uint32(col, 16);
        CHECK_EXPECTED(bitmask);
        config_entry.bitmask = bitmask.value();
        
        // value
        std::getline(s, col, ',' );
        CHECK_AS_EXPECTED(!s.bad(), HAILO_FILE_OPERATION_FAILURE, "Failed reading sensor config file value with errno: {}", errno);
        auto value = StringUtils::to_uint32(col, 16);
        CHECK_EXPECTED(value);
        config_entry.value = value.value();

        control_buffers.emplace_back(config_entry);
    }
    CHECK_AS_EXPECTED(!config_file.bad(), HAILO_FILE_OPERATION_FAILURE, "Failed reading line in sensor config file with errno: {}", errno);

    return control_buffers;
}

Expected<SENSOR_CONFIG__operation_cfg_t> SensorConfigUtils::create_config_entry(uint8_t page, uint32_t address, uint8_t length, const std::string &hex_value)
{
    auto config_entry_value = StringUtils::to_uint32(hex_value, 16);
    CHECK_EXPECTED(config_entry_value);

    SENSOR_CONFIG__operation_cfg_t config_entry = {};
    config_entry.value = config_entry_value.value();
    config_entry.operation = SENSOR_CONFIG_OPCODES_WR;
    config_entry.length = length;
    config_entry.page = page;
    config_entry.address = address;
    config_entry.bitmask = 0xFFFF;

    return config_entry;
}

Expected<std::vector<SENSOR_CONFIG__operation_cfg_t>> SensorConfigUtils::read_isp_config_file(const std::string &isp_static_config_file_path, const std::string &isp_runtime_config_file_path)
{
    std::vector<std::string> config_files = {isp_static_config_file_path, isp_runtime_config_file_path};
    std::vector<SENSOR_CONFIG__operation_cfg_t> control_buffers;

    for (const auto &config_file_path : config_files) {
        std::ifstream config_file;
        config_file.open(config_file_path, std::ios::in);
        CHECK_AS_EXPECTED(config_file.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed opening sensor ISP config file with errno: {}", errno);

        std::string line;
        uint8_t page = 0;
        uint32_t address = 0;

        while (std::getline(config_file, line)) {
            size_t comment_index = line.find("//"); 
            if (((std::string::npos != comment_index) && (0 == comment_index)) || ("\n" == line) ||
                ("\r\n" == line) || ("\r" == line) || ("" == line)) {
                continue;
            }

            std::string::iterator it = line.begin();
            CHECK_AS_EXPECTED(line.size() >= CONFIG_HEX_VALUE_LAST_CHAR_OFFSET, HAILO_INVALID_ARGUMENT, "Failed processing line {}. The line is not in the expected format. ", line);
            std::string prefix(it, it + CONFIG_PREFIX_LENGTH);
            std::string hex_value(it + CONFIG_PREFIX_LENGTH, it + CONFIG_HEX_VALUE_LAST_CHAR_OFFSET);

            // page 
            if ("btp" == prefix) {
                auto page_expected = StringUtils::to_uint8(hex_value, 16);
                CHECK_EXPECTED(page_expected);
                page = page_expected.value();            
            }

            // address
            else if ("bta" == prefix) {
                auto address_expected = StringUtils::to_uint32(hex_value, 16);
                CHECK_EXPECTED(address_expected);
                address = address_expected.value();           
            }

            else if ("btb" == prefix) {
                auto config_entry = create_config_entry(page, address, 8, hex_value);
                CHECK_EXPECTED(config_entry);

                control_buffers.emplace_back(config_entry.release());
                address = address + 1;
            }

            else if ("bth" == prefix) {
                auto config_entry = create_config_entry(page, address, 16, hex_value);
                CHECK_EXPECTED(config_entry);

                control_buffers.emplace_back(config_entry.release());
                address = address + 2;
            } 

            else if ("btw" == prefix) {
                auto config_entry = create_config_entry(page, address, 32, hex_value);
                CHECK_EXPECTED(config_entry);

                control_buffers.emplace_back(config_entry.release());
                address = address + 4;
            }
            
            else {
                LOGGER__ERROR("Invalid configuration prefix: {}", prefix);
                return make_unexpected(HAILO_NOT_FOUND);
            }
        }
        CHECK_AS_EXPECTED(!config_file.bad(), HAILO_FILE_OPERATION_FAILURE, "Failed reading line in sensor ISP config file with errno: {}", errno);
    }
    
    return control_buffers;
}

hailo_status SensorConfigUtils::dump_config_to_csv(SENSOR_CONFIG__operation_cfg_t *operation_cfg, const std::string &config_file_path, uint32_t entries_count)
{
    std::ofstream config_file;
    config_file.open(config_file_path, std::ios::out);
    CHECK(config_file.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed opening sensor config file with errno: {}", errno);

    for (size_t i = 0; i < entries_count; i++) {
        SENSOR_CONFIG__operation_cfg_t *config_entry = &operation_cfg[i];

        int page = (config_entry->page == 0xff) ? -1 : config_entry->page;
        int hex_width_filler = (config_entry->length == 8) ? 2 : 4;
        auto opcode_string = convert_opcode_to_string(config_entry->operation);
        CHECK_EXPECTED_AS_STATUS(opcode_string);   

        // There is no need to restore flags since they only affect the fstream "config_file" and doens't affect std::cout or other files.
        config_file << std::dec << opcode_string.value() << "," << static_cast<uint32_t>(config_entry->length) << "," << page <<
            ",0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(4) << config_entry->address <<
            ",0x" << std::setfill('0') << std::setw(hex_width_filler) << config_entry->bitmask <<
            ",0x" << std::setfill('0') << std::setw(hex_width_filler) << config_entry->value << std::endl;
    }

    return HAILO_SUCCESS;
}

} /* namespace hailort */
