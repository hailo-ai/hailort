/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/

#include "common/utils.hpp"
#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "common/logger_macros.hpp"
#include "vdma/integrated/integrated_device.hpp"

#include "md5.h"
#include <fstream>
#ifdef __linux__
#include <glob.h>
#endif
#include <memory>

namespace hailort
{

bool IntegratedDevice::is_loaded()
{
    return HailoRTDriver::is_integrated_nnc_loaded();
}

Expected<std::unique_ptr<IntegratedDevice>> IntegratedDevice::create()
{
    hailo_status status = HAILO_UNINITIALIZED;

    TRY(auto driver, HailoRTDriver::create_integrated_nnc());

    auto device = std::unique_ptr<IntegratedDevice>(new (std::nothrow) IntegratedDevice(std::move(driver), status));
    CHECK_AS_EXPECTED((nullptr != device), HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status, "Failed creating IntegratedDevice");

    return device;
}

IntegratedDevice::IntegratedDevice(std::unique_ptr<HailoRTDriver> &&driver, hailo_status &status) :
    VdmaDevice::VdmaDevice(std::move(driver), Device::Type::INTEGRATED, status)
{
    if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed to create VdmaDevice");
        return;
    }

    status = update_fw_state();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("update_fw_state() failed with status {}", status);
        return;
    }

    status = HAILO_SUCCESS;
}

hailo_status IntegratedDevice::reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type)
{
    if (CONTROL_PROTOCOL__RESET_TYPE__NN_CORE == reset_type) {
        return m_driver->reset_nn_core();
    }

    LOGGER__ERROR("Can't reset IntegratedDevice, please use linux reboot");
    return HAILO_NOT_IMPLEMENTED;
}

Expected<size_t> IntegratedDevice::read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id)
{
    if (hailo_cpu_id_t::HAILO_CPU_ID_0 == cpu_id) {
        LOGGER__ERROR("Read FW log is supported only on core CPU");
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    return VdmaDevice::read_log(buffer, cpu_id);
}


bool IntegratedDevice::is_stream_interface_supported(
    const hailo_stream_interface_t &stream_interface) const
{
    switch (stream_interface) {
    case HAILO_STREAM_INTERFACE_INTEGRATED:
        return true;
    case HAILO_STREAM_INTERFACE_PCIE:
    case HAILO_STREAM_INTERFACE_ETH:
    case HAILO_STREAM_INTERFACE_MIPI:
        return false;
    default:
        LOGGER__ERROR("Invalid stream interface");
        return false;
    }
}

Expected<hailo_chip_temperature_info_t> IntegratedDevice::get_chip_temperature()
{
    return ControlSoc::get_chip_temperature();
}

Expected<float32_t> IntegratedDevice::power_measurement(
    hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    return SocPowerMeasurement::measure(dvm, measurement_type);
}

hailo_status IntegratedDevice::start_power_measurement(
    hailo_averaging_factor_t averaging_factor,
    hailo_sampling_period_t sampling_period)
{
    CHECK(nullptr != m_power_measurement_data, HAILO_INVALID_OPERATION, "Must call set_power_measurement before start_power_measurement");
    CHECK(m_power_measurement_data->config(averaging_factor, sampling_period), HAILO_INTERNAL_FAILURE, "Failed to configure power measurement");
    return m_power_measurement_data->start();
}

hailo_status IntegratedDevice::set_power_measurement(
    hailo_measurement_buffer_index_t buffer_index,
    hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    (void)buffer_index;

    CHECK((HAILO_DVM_OPTIONS_VDD_CORE == dvm) || (HAILO_DVM_OPTIONS_AUTO == dvm), HAILO_INVALID_ARGUMENT,
        "Only HAILO_DVM_OPTIONS_VDD_CORE or HAILO_DVM_OPTIONS_AUTO are supported");
    m_power_measurement_data = std::make_shared<SocPowerMeasurement>(measurement_type);
    return HAILO_SUCCESS;
}

Expected<hailo_power_measurement_data_t> IntegratedDevice::get_power_measurement(
    hailo_measurement_buffer_index_t buffer_index, bool should_clear)
{
    (void)buffer_index;

    Expected<hailo_power_measurement_data_t> data(m_power_measurement_data->get_data());
    if (should_clear) {
        m_power_measurement_data->clear_data();
    }
    return data;
}

hailo_status IntegratedDevice::stop_power_measurement()
{
    return m_power_measurement_data->stop();
}

Expected<hailo_extended_device_information_t> IntegratedDevice::get_extended_device_information()
{
    constexpr auto STATUS_ENABLED = "okay";
    constexpr auto ETH_STATUS_FILE = "/proc/device-tree/ethernet@1b5000/status";
    constexpr auto PCI_STATUS_FILE = "/proc/device-tree/hailo_pci_ep_driver/status";
    constexpr auto NOT_AVAILABLE = 0;

    hailo_extended_device_information_t info = {};

    auto compare_file_content = [](const std::string &file_path, const std::string &expected_value) -> Expected<bool> {
        std::ifstream file(file_path, std::ios::binary);
        CHECK(file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed to open file {}", file_path);
        std::string content;
        file >> content;

        // ignore null characters that might be present in the file
        content.erase(std::remove(content.begin(), content.end(), '\0'), content.end());

        return content == expected_value;
    };

    TRY(auto is_eth_supported, compare_file_content(ETH_STATUS_FILE, STATUS_ENABLED));
    TRY(auto is_pci_supported, compare_file_content(PCI_STATUS_FILE, STATUS_ENABLED));

    info.boot_source = HAILO_DEVICE_BOOT_SOURCE_PCIE; // TODO: HRT-15562
    info.eth_mac_address[0] = NOT_AVAILABLE; // TODO: HRT-15562
    info.lcs = NOT_AVAILABLE; // TODO: HRT-15562
    info.neural_network_core_clock_rate = NOT_AVAILABLE; // TODO: HRT-15562
    info.soc_id[0] = NOT_AVAILABLE; // TODO: HRT-15562
    info.soc_pm_values[0] = NOT_AVAILABLE; // TODO: HRT-15562
    info.supported_features.current_monitoring = NOT_AVAILABLE; // TODO: HRT-15562
    info.supported_features.ethernet = is_eth_supported;
    info.supported_features.mdio = NOT_AVAILABLE; // TODO: HRT-15562
    info.supported_features.mipi = NOT_AVAILABLE; // TODO: HRT-15562
    info.supported_features.pcie = is_pci_supported;
    info.unit_level_tracking_id[0] = NOT_AVAILABLE; // TODO: HRT-15562

    return info;
}

Expected<bool> IntegratedDevice::has_INA231()
{
    bool has_INA231 = false;
    #ifdef __linux__
    glob_t glob_result;
    constexpr auto SENSOR_NAME_FILE_PATHS = "/sys/class/hwmon/hwmon*/name";
    glob(SENSOR_NAME_FILE_PATHS, GLOB_TILDE, NULL, &glob_result);

    for(unsigned int i = 0; i < glob_result.gl_pathc; ++i) {
        std::ifstream file(glob_result.gl_pathv[i]);
        if (!file.is_open()) {
            return make_unexpected(HAILO_FILE_OPERATION_FAILURE);
        }

        std::string line;
        std::getline(file, line);
        if (line == "ina231_precise") {
            has_INA231 = true;
            break;
        }
    }
    globfree(&glob_result);
    #endif // __linux__
    return has_INA231;
}

} /* namespace hailort */
