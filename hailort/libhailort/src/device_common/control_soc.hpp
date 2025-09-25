/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file control_soc.hpp
 * @brief Contains defines and declarations related to controlling Hailo SOC
 **/

#include "common/utils.hpp"
#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <thread>

namespace hailort {

template <typename T>
static Expected<T> read_number_from_file(const std::string &path)
{
    std::ifstream f(path);
    CHECK(f.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed to open file: {}", path);
    T number;
    f >> number;
    CHECK(!f.fail(), HAILO_FILE_OPERATION_FAILURE, "Failed to read number from file: {}", path);
    return number;
};

class SocPowerMeasurement final {
public:
    SocPowerMeasurement() = delete;
    SocPowerMeasurement(hailo_power_measurement_types_t type) : m_type(type), m_is_running(true) {}
    ~SocPowerMeasurement();

    hailo_power_measurement_data_t get_data();
    void set_data(const hailo_power_measurement_data_t &new_data);
    void clear_data();
    hailo_status start();
    hailo_status stop();
    void monitor();
    hailo_status config(hailo_averaging_factor_t averaging_factor,
        hailo_sampling_period_t sampling_period);
    static Expected<float32_t> measure(
        hailo_dvm_options_t dvm,
        hailo_power_measurement_types_t measurement_type);

  private:
    hailo_averaging_factor_t m_averaging_factor;
    hailo_dvm_options_t m_dvm;
    hailo_power_measurement_data_t m_data;
    hailo_power_measurement_types_t m_type;
    hailo_sampling_period_t m_sampling_period;
    std::atomic<bool> m_is_running;
    std::mutex m_mutex;
    std::thread m_thread;
    uint32_t m_average_factor_value;
    uint32_t m_sampling_interval_milliseconds;
    uint32_t m_sampling_period_value;
};

class ControlSoc final
{
public:
    ControlSoc() = delete;
    static Expected<hailo_chip_temperature_info_t> get_chip_temperature();
};

} // namespace hailort
