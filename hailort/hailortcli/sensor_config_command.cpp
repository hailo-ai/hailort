/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sensor_config_command.cpp
 * @brief Config sensor attached to the Hailo chip
 **/

#include "sensor_config_command.hpp"

SensorConfigCommand::SensorConfigCommand(CLI::App &parent_app) :
    ContainerCommand(parent_app.add_subcommand("sensor-config", "Config sensor attached to the Hailo chip"))
{
    add_subcommand<SensorStoreConfigSubcommand>();
    add_subcommand<SensorLoadConfigSubcommand>();
    add_subcommand<SensorResetSubcommand>();
    add_subcommand<SensorSectionsInfoSubcommand>();
    add_subcommand<SensorDumpConfigSubcommand>();
    add_subcommand<SensorStoreISPConfigSubcommand>();
    add_subcommand<SensorSetGenericSlaveSubcommand>();
}

Expected<std::string> convert_sensor_type_to_string(uint32_t sensor_type)
{
    switch (sensor_type) {
    case HAILO_SENSOR_TYPES_GENERIC:
        return std::string("SENSOR_GENERIC");

    case HAILO_SENSOR_TYPES_ONSEMI_AR0220AT:
        return std::string("ONSEMI_AR0220AT");

    case HAILO_SENSOR_TYPES_RASPICAM:
        return std::string("SENSOR_RASPICAM");

    case HAILO_SENSOR_TYPES_ONSEMI_AS0149AT:
        return std::string("ONSEMI_AS0149AT");

    case HAILO_SENSOR_TYPES_HAILO8_ISP:
        return std::string("HAILO8_ISP");

    default:
        LOGGER__ERROR("Failed converting sensor type to string");
        return make_unexpected(HAILO_NOT_FOUND);
    }
}

SensorStoreConfigSubcommand::SensorStoreConfigSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("store-sensor-config", "Store a sensor configuration to a Hailo device")), m_store_config_params()
{
    m_app->add_option("section_index", m_store_config_params.section_index, "Sensor index")
        ->required();
    m_app->add_option("config_file_path", m_config_file_path, "Config file path (csv)")
        ->check(CLI::ExistingFile)
        ->required();
    m_app->add_option("sensor_type", m_store_config_params.sensor_type, "Type of sensor")
        ->transform(HailoCheckedTransformer<hailo_sensor_types_t>({
            { "SENSOR_GENERIC", HAILO_SENSOR_TYPES_GENERIC },
            { "ONSEMI_AR0220AT", HAILO_SENSOR_TYPES_ONSEMI_AR0220AT },
            { "SENSOR_RASPICAM", HAILO_SENSOR_TYPES_RASPICAM },
            { "ONSEMI_AS0149AT", HAILO_SENSOR_TYPES_ONSEMI_AS0149AT },
            { "HAILO8_ISP", HAILO_SENSOR_TYPES_HAILO8_ISP }
        }))
        ->required();
    m_app->add_option("--reset-config-size", m_store_config_params.reset_config_size, "The size of the reset configuration data");
    m_app->add_option("--config-height", m_store_config_params.config_height, "Configuration resolution height");
    m_app->add_option("--config-width", m_store_config_params.config_width, "Configuration resolution width");
    m_app->add_option("--config-fps", m_store_config_params.config_fps, "Configuration resolution fps");
    m_app->add_option("--config-name", m_store_config_params.config_name, "Configuration name")
        ->default_val("UNINITIALIZED");
}

hailo_status SensorStoreConfigSubcommand::execute_on_device(Device &device)
{
    return device.store_sensor_config(m_store_config_params.section_index, m_store_config_params.sensor_type,
        m_store_config_params.reset_config_size, m_store_config_params.config_height,
        m_store_config_params.config_width, m_store_config_params.config_fps, m_config_file_path,
        m_store_config_params.config_name);
}

SensorLoadConfigSubcommand::SensorLoadConfigSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("load-config", "Load the sensor configuration stored in the given section"))
{
    m_app->add_option("section_index", m_section_index, "Sensor index")
        ->required();
}

hailo_status SensorLoadConfigSubcommand::execute_on_device(Device &device)
{
    return device.sensor_load_and_start_config(m_section_index);
}

SensorResetSubcommand::SensorResetSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("reset-sensor", "Load reset configuration stored in the given section"))
{
    m_app->add_option("section_index", m_section_index, "Sensor index")
        ->required();
}

hailo_status SensorResetSubcommand::execute_on_device(Device &device)
{
    return device.sensor_reset(m_section_index);
}

SensorSectionsInfoSubcommand::SensorSectionsInfoSubcommand(CLI::App &parent_app) : 
    DeviceCommand(parent_app.add_subcommand("get-sections-info", "Get the flash sections information"))
{}

hailo_status SensorSectionsInfoSubcommand::execute_on_device(Device &device)
{
    TRY(auto sections_info, device.sensor_get_sections_info());
    return print_sections_info((SENSOR_CONFIG__section_info_t*)sections_info.data());
}

hailo_status SensorSectionsInfoSubcommand::print_sections_info(SENSOR_CONFIG__section_info_t *operation_cfg)
{
    for (uint32_t section_index = 0; section_index < SENSOR_CONFIG__TOTAL_SECTIONS_BLOCK_COUNT; section_index++) {
        SENSOR_CONFIG__section_info_t *section_info = &operation_cfg[section_index];

        std::cout << "======== section_index: " << section_index << " =========" << std::endl;

        if (section_info->is_free) {
            std::cout << "Section is not active\n" << std::endl;
        }
        else {
            std::string reset_config = section_info->no_reset_offset ? "not valid" : "valid";
            TRY( const auto sensor_type, convert_sensor_type_to_string(section_info->sensor_type),
                "Failed convert sensor type to string");

            std::cout << "Configuration Name:               " << section_info->config_name << "\n";
            std::cout << "Sensor Type:                      " << sensor_type << "\n";
            std::cout << "Configuration lines number:       " << (section_info->config_size / sizeof(SENSOR_CONFIG__operation_cfg_t)) << "\n";
            std::cout << "Configuration size in bytes:      " << section_info->config_size << "\n";
            std::cout << "Reset configuration:              " << reset_config << "\n";
            std::cout << "Reset configuration lines number: " << section_info->reset_config_size << "\n";
            std::cout << "Configuration resolution:         [height " << section_info->config_height << " : width " << section_info->config_width << "]" << "\n";
            std::cout << "Configuration fps:                " << section_info->config_fps << "\n";
            std::cout << "Section configuration version:    " << section_info->section_version << "\n";
            std::cout << "Section is active\n" << std::endl;
        }
    }

    return HAILO_SUCCESS;
}

// TODO: change "get-config" to "dump-config" after solving backward compatibility issues 
SensorDumpConfigSubcommand::SensorDumpConfigSubcommand(CLI::App &parent_app) : 
    DeviceCommand(parent_app.add_subcommand("get-config", "Dumps the configuration stored in the given section into a csv file"))
{
    m_app->add_option("section_index", m_section_index, "Sensor index")
        ->check(CLI::Range(0, (SENSOR_CONFIG__TOTAL_SECTIONS_BLOCK_COUNT - 1)))
        ->required();
    m_app->add_option("config_file_path", m_output_file_path, "File path to write section configuration")
        ->required();
}

hailo_status SensorDumpConfigSubcommand::execute_on_device(Device &device)
{
    auto status = validate_specific_device_is_given();
    CHECK_SUCCESS(status,
        "'sensor-config get-config' command should get a specific device-id.");

    return device.sensor_dump_config(m_section_index, m_output_file_path);
}

SensorStoreISPConfigSubcommand::SensorStoreISPConfigSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("store_isp_config", "Store an ISP configuration to Hailo device, in section index " + std::to_string(SENSOR_CONFIG__ISP_SECTION_INDEX))),
    m_store_config_params()
{
    m_app->add_option("isp_static_config_file_path", m_isp_static_config_file_path, "ISP static config file path")
        ->required();
    m_app->add_option("isp_runtime_config_file_path", m_isp_runtime_config_file_path, "ISP runtime config file path")
        ->required();
    m_app->add_option("--reset-config-size", m_store_config_params.reset_config_size, "The size of the reset configuration data");
    m_app->add_option("--config-height", m_store_config_params.config_height, "Configuration resolution height");
    m_app->add_option("--config-width", m_store_config_params.config_width, "Configuration resolution width");
    m_app->add_option("--config-fps", m_store_config_params.config_fps, "Configuration resolution fps");
    m_app->add_option("--config-name", m_store_config_params.config_name, "Configuration name")
        ->default_val("UNINITIALIZED");
}

hailo_status SensorStoreISPConfigSubcommand::execute_on_device(Device &device)
{
    return device.store_isp_config(m_store_config_params.reset_config_size, m_store_config_params.config_height,
        m_store_config_params.config_width, m_store_config_params.config_fps, m_isp_static_config_file_path,
        m_isp_runtime_config_file_path, m_store_config_params.config_name);
}

SensorSetGenericSlaveSubcommand::SensorSetGenericSlaveSubcommand(CLI::App &parent_app) :
    DeviceCommand(parent_app.add_subcommand("set_generic_slave", "Set a custom i2c slave that can be used"))
{
    m_app->add_option("slave_address", m_sensor_i2c_slave_info.slave_address, "The address of the i2c slave")
        ->required();
    m_app->add_option("register_address_size", m_sensor_i2c_slave_info.register_address_size, "Slave offset length in bytes")
        ->required();
    m_app->add_option("bus_index", m_sensor_i2c_slave_info.bus_index, "The bus number the i2c slave is connected to")
        ->required();
    m_app->add_option("--should-hold-bus", m_sensor_i2c_slave_info.should_hold_bus, "Should hold the bus during the read")
        ->default_val("false");
    m_app->add_option("--slave-endianness", m_sensor_i2c_slave_info.endianness)
        ->transform(HailoCheckedTransformer<i2c_slave_endianness_t>({
            { "BIG_ENDIAN", I2C_SLAVE_ENDIANNESS_BIG_ENDIAN },
            { "LITTLE_ENDIAN", I2C_SLAVE_ENDIANNESS_LITTLE_ENDIAN }
            }))
        ->default_val(I2C_SLAVE_ENDIANNESS_BIG_ENDIAN);
}

hailo_status SensorSetGenericSlaveSubcommand::execute_on_device(Device &device)
{
    return device.sensor_set_generic_i2c_slave(m_sensor_i2c_slave_info.slave_address, m_sensor_i2c_slave_info.register_address_size,
        m_sensor_i2c_slave_info.bus_index, m_sensor_i2c_slave_info.should_hold_bus, m_sensor_i2c_slave_info.endianness);
}
