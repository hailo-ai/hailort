/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
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
#ifdef GPIO_V2_GET_LINE_IOCTL
#include <fcntl.h>
#include <sys/ioctl.h>
#endif // GPIO_V2_GET_LINE_IOCTL
#endif // __linux__
#include <memory>
#include <filesystem>
#include <charconv>

namespace fs = std::filesystem;

namespace hailort
{

static constexpr auto CURRENT_LIMIT_FILE = "/sys/devices/soc0/current_limit";
static constexpr auto CHIP_SERIAL_NUMBER_FILE = "/sys/devices/soc0/chip_serial_number";

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

    m_power_measurement_data = make_shared_nothrow<SocPowerMeasurement>();
    if (nullptr == m_power_measurement_data) {
        LOGGER__ERROR("Failed to create power measurement data");
        status = HAILO_OUT_OF_HOST_MEMORY;
        return;
    }

    status = set_default_notification_callbacks();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to set default notification callbacks for IntegratedDevice: {}", status);
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
    CHECK(has_power_sensor(), HAILO_INVALID_OPERATION,
        "HailoRT does not support power measurements for this architecture. Use the 'sensors' CLI command instead");
    return SocPowerMeasurement::measure(dvm, measurement_type);
}

hailo_status IntegratedDevice::start_power_measurement(
    hailo_averaging_factor_t averaging_factor,
    hailo_sampling_period_t sampling_period)
{
    CHECK(has_power_sensor(), HAILO_INVALID_OPERATION,
        "HailoRT does not support power measurements for this architecture. Use the 'sensors' CLI command instead");
    CHECK(nullptr != m_power_measurement_data, HAILO_INVALID_OPERATION, "Must call set_power_measurement before start_power_measurement");
    auto status = m_power_measurement_data->config(averaging_factor, sampling_period);
    CHECK_SUCCESS(status, "Failed to configure power measurement");
    return m_power_measurement_data->start();
}

hailo_status IntegratedDevice::set_power_measurement(
    hailo_measurement_buffer_index_t buffer_index,
    hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    (void)buffer_index;
    CHECK(has_power_sensor(), HAILO_INVALID_OPERATION,
        "HailoRT does not support power measurements for this architecture. Use the 'sensors' CLI command instead");
    CHECK((HAILO_DVM_OPTIONS_VDD_CORE == dvm) || (HAILO_DVM_OPTIONS_AUTO == dvm), HAILO_INVALID_ARGUMENT,
        "Only HAILO_DVM_OPTIONS_VDD_CORE or HAILO_DVM_OPTIONS_AUTO are supported");
    m_power_measurement_data = std::make_shared<SocPowerMeasurement>(measurement_type);
    return HAILO_SUCCESS;
}

Expected<hailo_power_measurement_data_t> IntegratedDevice::get_power_measurement(
    hailo_measurement_buffer_index_t buffer_index, bool should_clear)
{
    (void)buffer_index;
    CHECK(has_power_sensor(), HAILO_INVALID_OPERATION,
        "HailoRT does not support power measurements for this architecture. Use the 'sensors' CLI command instead");
    Expected<hailo_power_measurement_data_t> data(m_power_measurement_data->get_data());
    if (should_clear) {
        m_power_measurement_data->clear_data();
    }
    return data;
}

hailo_status IntegratedDevice::stop_power_measurement()
{
    CHECK(has_power_sensor(), HAILO_INVALID_OPERATION,
        "HailoRT does not support power measurements for this architecture. Use the 'sensors' CLI command instead");
    CHECK_NOT_NULL(m_power_measurement_data, HAILO_INVALID_OPERATION);
    return m_power_measurement_data->stop();
}

Expected<hailo_extended_device_information_t> IntegratedDevice::get_extended_device_information()
{
    constexpr auto STATUS_ENABLED = "okay";
    constexpr auto PCI_STATUS_FILE = "/proc/device-tree/hailo_pci_ep_driver/status";
    constexpr auto IDENTIFICATION_FILE = "/sys/devices/soc0/identification_attributes";
    constexpr auto NOT_AVAILABLE = 0;

    static_assert(HAILO_UNIT_LEVEL_TRACKING_BYTES_LENGTH == HAILO_CHIP_SERIAL_NUMBER_BYTES_LENGTH, "ULT and chip serial number must have the same size!");

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

    TRY(auto is_pci_supported, compare_file_content(PCI_STATUS_FILE, STATUS_ENABLED));
    TRY(auto is_power_measurement_supported, has_power_sensor());

    if (HAILO_ARCH_HAILO10H == m_device_architecture) {
        info.boot_source = is_pci_supported ? HAILO_DEVICE_BOOT_SOURCE_PCIE : HAILO_DEVICE_BOOT_SOURCE_FLASH;
    } else {
        info.boot_source = HAILO_DEVICE_BOOT_SOURCE_INVALID;
    }
    info.supported_features.pcie               = is_pci_supported;
    info.supported_features.power_measurement  = is_power_measurement_supported;
    
    // deprecated fields
    info.neural_network_core_clock_rate        = NOT_AVAILABLE;
    info.eth_mac_address[0]                    = NOT_AVAILABLE;
    info.supported_features.current_monitoring = NOT_AVAILABLE;
    info.supported_features.ethernet           = NOT_AVAILABLE;
    info.supported_features.mdio               = NOT_AVAILABLE;
    info.supported_features.mipi               = NOT_AVAILABLE;
    memset(info.unit_level_tracking_id, NOT_AVAILABLE, sizeof(info.unit_level_tracking_id));

    if (fs::exists(IDENTIFICATION_FILE)) {
        FileReader reader(IDENTIFICATION_FILE);
        auto status = reader.open();
        CHECK_SUCCESS(status, "Failed to open file {}", IDENTIFICATION_FILE);

        // parse IDENTIFICATION_FILE according to MSW-11266
        status = reader.read(&info.lcs, 1);
        CHECK_SUCCESS(status, "Failed to read lcs from file {}", IDENTIFICATION_FILE);

        status = reader.read(info.soc_id, HAILO_SOC_ID_LENGTH);
        CHECK_SUCCESS(status, "Failed to read soc_id from file {}", IDENTIFICATION_FILE);

        constexpr auto PM_VALUES_SIZE = 3 * sizeof(uint32_t); // lvt, svt, ulvt (4 bytes for each)
        status = reader.read(info.soc_pm_values, PM_VALUES_SIZE);
        CHECK_SUCCESS(status, "Failed to read soc_pm_values from file {}", IDENTIFICATION_FILE);
    }

    {
        FileReader reader(CHIP_SERIAL_NUMBER_FILE);
        auto status = reader.open();
        CHECK_SUCCESS(status, "Failed to open file {}", CHIP_SERIAL_NUMBER_FILE);

        status = reader.read(info.chip_serial_number, sizeof(info.chip_serial_number));
        CHECK_SUCCESS(status, "Failed to read {} bytes from file {}", sizeof(info.chip_serial_number), CHIP_SERIAL_NUMBER_FILE);
        
        // The serial number in the file is reversed - so we reverse it back, to match the order in the scu log.
        std::reverse(info.chip_serial_number, info.chip_serial_number + sizeof(info.chip_serial_number));
    }

#if defined(__linux__) && defined(GPIO_V2_GET_LINE_IOCTL)
    if (m_device_architecture == HAILO_ARCH_HAILO10H) {
        TRY(info.gpio_mask, GpioReader().read());
    }
#endif

    return info;
}

Expected<bool> IntegratedDevice::has_power_sensor()
{
    if ((HAILO_ARCH_HAILO15H == m_device_architecture) || (HAILO_ARCH_HAILO15M == m_device_architecture) ||
        (HAILO_ARCH_HAILO15L == m_device_architecture)) {
        return false;
    }

    bool has_power_sensor = false;
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
            has_power_sensor = true;
            break;
        }
    }
    globfree(&glob_result);
    #endif // __linux__
    return has_power_sensor;
}

#if defined(__linux__) && defined(GPIO_V2_GET_LINE_IOCTL)
IntegratedDevice::GpioReader::~GpioReader()
{
    if (m_request_fd >= 0) {
        (void)close(m_request_fd);
    }

    if (m_fd >= 0) {
        (void)close(m_fd);
    }
}

Expected<uint16_t> IntegratedDevice::GpioReader::read()
{
    // /dev/gpiochip1 is the GPIO bits 16-31
    constexpr auto GPIO_MASK_FILE = "/dev/gpiochip1";
    auto file_ptr = std::unique_ptr<FILE, int (*)(FILE *)>(std::fopen(GPIO_MASK_FILE, "r"), &std::fclose);
    CHECK_NOT_NULL(file_ptr, HAILO_FILE_OPERATION_FAILURE);
    m_fd = fileno(file_ptr.get());

    struct gpio_v2_line_request req = {};
    req.num_lines = HAILO_GPIO_MASK_VALUES_LENGTH;
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT;
    for (uint32_t i = 0; i < HAILO_GPIO_MASK_VALUES_LENGTH; i++) {
        req.offsets[i] = i;
    }

    int ret = ioctl(m_fd, GPIO_V2_GET_LINE_IOCTL, &req);
    CHECK(ret >= 0, HAILO_FILE_OPERATION_FAILURE, "Failed to get line from ioctl, errno = {}", errno);
    m_request_fd = req.fd;

    struct gpio_v2_line_values values = {};
    values.mask = UINT16_MAX;

    ret = ioctl(m_request_fd, GPIO_V2_LINE_GET_VALUES_IOCTL, &values);
    CHECK(ret >= 0, HAILO_FILE_OPERATION_FAILURE, "Failed to get line values from ioctl");

    return static_cast<uint16_t>(values.bits);
}
#endif // __linux__

Expected<uint32_t> IntegratedDevice::get_current_limit()
{
    constexpr auto MAX_LENGTH_OF_FILE = 4;
    uint8_t buf[MAX_LENGTH_OF_FILE];

    auto bytes_read = read_device_file(CURRENT_LIMIT_FILE, MemoryView(buf, sizeof(buf)));
    if (!bytes_read) {
        return HAILO_CURRENT_LIMIT_NA;
    }

    std::string content(reinterpret_cast<char*>(buf), bytes_read.value());
    
    auto trimmed_content = StringUtils::trim(content);
    if ("NA" == trimmed_content) {
        return HAILO_CURRENT_LIMIT_NA;
    }
    
    uint32_t result = 0;
    auto [ptr, ec] = std::from_chars(trimmed_content.data(), trimmed_content.data() + trimmed_content.size(), result);
    CHECK((std::errc() == ec) && ((trimmed_content.data() + trimmed_content.size()) == ptr), HAILO_FILE_OPERATION_FAILURE, "Failed to parse current limit value from {}", CURRENT_LIMIT_FILE);
    
    return result;
}

} /* namespace hailort */
