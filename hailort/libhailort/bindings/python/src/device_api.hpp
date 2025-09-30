/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file device_api.hpp
 * @brief Defines binding to hailo device
 *
 **/

#ifndef _DEVICE_API_HPP_
#define _DEVICE_API_HPP_

#include "hailo/hailort.h"
#include <hailo/platform.h>
#include "hailo/device.hpp"

#include "utils.hpp"
#include "hef_api.hpp"
#include "network_group_api.hpp"

#include <pybind11/pybind11.h>


namespace hailort
{


class PowerMeasurementData
{ 
public:
    float32_t m_average_value;
    float32_t m_average_time_value_milliseconds;
    float32_t m_min_value;
    float32_t m_max_value;
    uint32_t m_total_number_of_samples;
    PowerMeasurementData(hailo_power_measurement_data_t &&c_power_data);
    bool equals(const PowerMeasurementData &other);
    static py::tuple get_state(const PowerMeasurementData &power_measurement_data);
    static PowerMeasurementData set_state(py::tuple t);
    const static uint32_t NUM_OF_MEMBERS = 5;
};


class DeviceWrapper;
using DeviceWrapperPtr = std::shared_ptr<DeviceWrapper>;

class DeviceWrapper final
{
public:

    static std::vector<std::string> scan();
    static DeviceWrapperPtr create(const std::string &device_id);
    static DeviceWrapperPtr create_pcie(hailo_pcie_device_info_t &device_info);
    void release();

    DeviceWrapper(std::unique_ptr<Device> &&device) : m_device(std::move(device))
#ifdef HAILO_IS_FORK_SUPPORTED
    , m_atfork_guard(this, {
        .before_fork = [this]() { if (m_device) m_device->before_fork(); },
        .after_fork_in_parent = [this]() { if (m_device) m_device->after_fork_in_parent(); },
        .after_fork_in_child = [this]() { if (m_device) m_device->after_fork_in_child(); },
    })
#endif
    {}

    Device& device()
    {
        VALIDATE_NOT_NULL(m_device, HAILO_INTERNAL_FAILURE);
        return *(m_device.get());
    }

    const Device& device() const
    {
        VALIDATE_NOT_NULL(m_device, HAILO_INTERNAL_FAILURE);
        return *(m_device.get());
    }

    bool is_valid()
    {
        return (nullptr != m_device);
    }

    Device& operator*() // Used for control_internals
    {
        return device();
    }

    /* Controls */
    hailo_device_identity_t identify();
    hailo_core_information_t core_identify();
    void set_fw_logger(hailo_fw_logger_level_t level, uint32_t interface_mask);
    void set_throttling_state(bool should_activate);
    bool get_throttling_state();
    void set_overcurrent_state(bool should_activate);
    bool get_overcurrent_state();
    py::bytes read_memory(uint32_t address, uint32_t length);
    void write_memory(uint32_t address, py::bytes data, uint32_t length);
    void test_chip_memories();
    void i2c_write(hailo_i2c_slave_config_t *slave_config, uint32_t register_address, py::bytes data,
        uint32_t length);
    py::bytes i2c_read(hailo_i2c_slave_config_t *slave_config, uint32_t register_address, uint32_t length);
    float32_t power_measurement(hailo_dvm_options_t dvm,
        hailo_power_measurement_types_t measurement_type);
    void start_power_measurement(hailo_averaging_factor_t averaging_factor, hailo_sampling_period_t sampling_period);
    void set_power_measurement(hailo_measurement_buffer_index_t buffer_index, hailo_dvm_options_t dvm,
        hailo_power_measurement_types_t measurement_type);
    PowerMeasurementData get_power_measurement(hailo_measurement_buffer_index_t buffer_index, bool should_clear);
    void stop_power_measurement();
    void reset(hailo_reset_device_mode_t mode);
    hailo_fw_user_config_information_t examine_user_config();
    py::bytes read_user_config();
    void write_user_config(py::bytes data);
    void erase_user_config();
    py::bytes read_board_config();
    void write_board_config(py::bytes data);
    hailo_extended_device_information_t get_extended_device_information();
    hailo_health_info_t get_health_information();
    void sensor_store_config(uint32_t section_index, uint32_t reset_data_size, uint32_t sensor_type,
        const std::string &config_file_path, uint16_t config_height, uint16_t config_width, uint16_t config_fps, const std::string &config_name);
    void store_isp_config(uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps,
        const std::string &isp_static_config_file_path, const std::string &isp_runtime_config_file_path, const std::string &config_name);
    py::bytes sensor_get_sections_info();
    void sensor_set_i2c_bus_index(uint32_t sensor_type, uint32_t bus_index);
    void sensor_load_and_start_config(uint32_t section_index);
    void sensor_reset(uint32_t section_index);
    void sensor_set_generic_i2c_slave(uint16_t slave_address,
        uint8_t register_address_size, uint8_t bus_index, uint8_t should_hold_bus, uint8_t endianness);
    void firmware_update(py::bytes fw_bin, uint32_t fw_bin_length, bool should_reset);
    void second_stage_update(py::bytes second_stage_bin, uint32_t second_stage_bin_length);
    void set_pause_frames(bool rx_pause_frames_enable);
    void wd_enable(hailo_cpu_id_t cpu_id);
    void wd_disable(hailo_cpu_id_t cpu_id);
    void wd_config(hailo_cpu_id_t cpu_id, uint32_t wd_cycles, hailo_watchdog_mode_t wd_mode);
    uint32_t previous_system_state(hailo_cpu_id_t cpu_id);
    hailo_chip_temperature_info_t get_chip_temperature();
    void set_notification_callback(const std::function<void(uintptr_t, const hailo_notification_t&, py::object)> &callback,
        hailo_notification_id_t notification_id, py::object opaque);
    void remove_notification_callback(hailo_notification_id_t notification_id);
    const char *get_dev_id() const;
    void set_sleep_state(hailo_sleep_state_t sleep_state);

    static void bind(py::module &m);

private:
    std::unique_ptr<Device> m_device;
#ifdef HAILO_IS_FORK_SUPPORTED
    AtForkRegistry::AtForkGuard m_atfork_guard;
#endif
};

} /* namespace hailort */

#endif /* _DEVICE_API_HPP_ */
