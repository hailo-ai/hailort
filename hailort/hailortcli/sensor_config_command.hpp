/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sensor_config_command.hpp
 * @brief Config sensor attached to the Hailo chip
 **/

#ifndef _HAILO_SENSOR_CONFIG_COMMAND_HPP_
#define _HAILO_SENSOR_CONFIG_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"
#include "hailo/hailort.h"
#include "sensor_config_exports.h"
#include "CLI/CLI.hpp"


struct store_config_params_t {
    uint32_t section_index;
    hailo_sensor_types_t sensor_type;
    uint32_t reset_config_size;
    uint16_t config_height;
    uint16_t config_width;
    uint16_t config_fps;
    std::string config_name;
};

class SensorStoreConfigSubcommand final : public DeviceCommand {
public:
    explicit SensorStoreConfigSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    store_config_params_t m_store_config_params;
    std::string m_config_file_path;
};

class SensorLoadConfigSubcommand final : public DeviceCommand {
public:
    explicit SensorLoadConfigSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    uint8_t m_section_index;
};

class SensorResetSubcommand final : public DeviceCommand {
public:
    explicit SensorResetSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    uint8_t m_section_index;
};

class SensorSectionsInfoSubcommand final : public DeviceCommand {
public:
    explicit SensorSectionsInfoSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    static hailo_status print_sections_info(SENSOR_CONFIG__section_info_t *operation_cfg);
};

class SensorDumpConfigSubcommand final : public DeviceCommand {
public:
    explicit SensorDumpConfigSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    uint8_t m_section_index;
    std::string m_output_file_path;
};

class SensorStoreISPConfigSubcommand final : public DeviceCommand {
public:
    explicit SensorStoreISPConfigSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    store_config_params_t m_store_config_params;
    std::string m_isp_static_config_file_path;
    std::string m_isp_runtime_config_file_path;
};

class SensorSetGenericSlaveSubcommand final : public DeviceCommand {
public:
    explicit SensorSetGenericSlaveSubcommand(CLI::App &parent_app);

protected:
    virtual hailo_status execute_on_device(Device &device) override;

private:
    SENSOR_I2C_SLAVE_INFO_t m_sensor_i2c_slave_info;
};

class SensorConfigCommand final : public ContainerCommand {
public:
    explicit SensorConfigCommand(CLI::App &parent_app);
};

#endif /* _HAILO_SENSOR_CONFIG_COMMAND_HPP_ */