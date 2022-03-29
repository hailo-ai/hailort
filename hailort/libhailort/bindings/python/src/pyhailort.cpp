#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/detail/common.h>
#include <pybind11/stl.h>
#include <pybind11/complex.h>
#include <pybind11/functional.h>
#include <vector>
using namespace std;

#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "hailo/transform.hpp"
#include "hailo/hef.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/quantization.hpp"

#include "hef_api.hpp"
#include "vstream_api.hpp"
#include "vdevice_api.hpp"
#include "utils.hpp"
#include "utils.h"

#include "common/socket.hpp"
#include "sensor_config_exports.h"
#include "hailort_defaults.hpp"
#if defined(__GNUC__)
#include "common/os/posix/traffic_control.hpp"
#endif

namespace hailort
{

#define MAX_HAILO_PACKET_SIZE (4*1024)


class PowerMeasurementData { 
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

PowerMeasurementData::PowerMeasurementData(hailo_power_measurement_data_t &&c_power_data)
{
    m_average_value = c_power_data.average_value;
    m_average_time_value_milliseconds = c_power_data.average_time_value_milliseconds;
    m_min_value = c_power_data.min_value;
    m_max_value = c_power_data.max_value;
    m_total_number_of_samples = c_power_data.total_number_of_samples;
}

/* Return a tuple that fully encodes the state of the object */
py::tuple PowerMeasurementData::get_state(const PowerMeasurementData &power_measurement_data){
    return py::make_tuple(
        power_measurement_data.m_average_value,
        power_measurement_data.m_average_time_value_milliseconds,
        power_measurement_data.m_min_value,
        power_measurement_data.m_max_value,
        power_measurement_data.m_total_number_of_samples);
}

PowerMeasurementData PowerMeasurementData::set_state(py::tuple t){
    if (PowerMeasurementData::NUM_OF_MEMBERS != t.size())
        throw std::runtime_error("Invalid power measurement data state!");

    /* Create a new C++ instance */
    hailo_power_measurement_data_t data;
    data.average_value = t[0].cast<float32_t>();
    data.average_time_value_milliseconds = t[1].cast<float32_t>();
    data.min_value = t[2].cast<float32_t>();
    data.max_value = t[3].cast<float32_t>();
    data.total_number_of_samples = t[4].cast<uint32_t>();
    return PowerMeasurementData(std::move(data));
}

bool PowerMeasurementData::equals(const PowerMeasurementData &other) {
    return ((this->m_average_value == other.m_average_value) &&
        (this->m_average_time_value_milliseconds == other.m_average_time_value_milliseconds) &&
        (this->m_min_value == other.m_min_value) &&
        (this->m_max_value == other.m_max_value) &&
        (this->m_total_number_of_samples == other.m_total_number_of_samples));
}

bool temperature_info_equals(hailo_chip_temperature_info_t &first, hailo_chip_temperature_info_t &second){
    return ((first.ts0_temperature == second.ts0_temperature) &&
        (first.ts1_temperature == second.ts1_temperature) &&
        (first.sample_count == second.sample_count));
}

bool hailo_format_equals(hailo_format_t &first, hailo_format_t &second){
    return ((first.type == second.type) &&
        (first.order == second.order) &&
        (first.flags == second.flags));
}
class UdpScan {
    public:
        UdpScan();
        std::list<std::string> scan_devices(char *interface_name, uint32_t timeout_milliseconds);
    private:
        static const size_t m_max_number_of_devices = 100;
        hailo_eth_device_info_t m_eth_device_infos[m_max_number_of_devices] = {};
};

/* Device */
// TODO HRT-5285: Change Python bindings of device class to use CPP API and move functionality to a new device module.
uintptr_t create_eth_device(char *device_address, size_t device_address_length, uint16_t port,
    uint32_t timeout_milliseconds, uint8_t max_number_of_attempts)
{
    hailo_status status = HAILO_UNINITIALIZED;
    hailo_device device = NULL;
    hailo_eth_device_info_t device_info = {};

    /* Validate address length */
    if (INET_ADDRSTRLEN < device_address_length) {
        EXIT_WITH_ERROR("device_address_length is invalid")
    }

    device_info.host_address.sin_family = AF_INET;
    device_info.host_address.sin_port = HAILO_ETH_PORT_ANY;
    status = Socket::pton(AF_INET, HAILO_ETH_ADDRESS_ANY, &(device_info.host_address.sin_addr));
    VALIDATE_STATUS(status);

    device_info.device_address.sin_family = AF_INET;
    device_info.device_address.sin_port = port;
    status = Socket::pton(AF_INET, device_address, &(device_info.device_address.sin_addr));
    VALIDATE_STATUS(status);

    device_info.timeout_millis = timeout_milliseconds;
    device_info.max_number_of_attempts = max_number_of_attempts;
    device_info.max_payload_size = HAILO_DEFAULT_ETH_MAX_PAYLOAD_SIZE;
    
    status = hailo_create_ethernet_device(&device_info, &device);
    VALIDATE_STATUS(status);

    return (uintptr_t)device;
}

std::vector<hailo_pcie_device_info_t> scan_pcie_devices(void)
{
    auto scan_result = Device::scan_pcie();
    VALIDATE_EXPECTED(scan_result);

    return scan_result.release();
}

uintptr_t create_pcie_device(hailo_pcie_device_info_t *device_info)
{
    hailo_device device = NULL;
    hailo_status status = hailo_create_pcie_device(device_info, &device);
    VALIDATE_STATUS(status);

    return (uintptr_t)device;
}

void release_device(uintptr_t device)
{
    hailo_status status = HAILO_UNINITIALIZED;

    status = hailo_release_device((hailo_device)device);
    VALIDATE_STATUS(status);

    return;
}

uintptr_t get_hlpcie_device(uintptr_t hailort_device)
{
    return hailort_device;
}

/* Controls */
hailo_device_identity_t identify(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto board_info = reinterpret_cast<Device *>(device)->identify();
    VALIDATE_EXPECTED(board_info);

    return board_info.release();
}

hailo_core_information_t core_identify(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto core_info = reinterpret_cast<Device *>(device)->core_identify();
    VALIDATE_EXPECTED(core_info);

    return core_info.release();
}

void set_fw_logger(uintptr_t device, hailo_fw_logger_level_t level, uint32_t interface_mask)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));

    auto status = reinterpret_cast<Device *>(device)->set_fw_logger(level, interface_mask);
    VALIDATE_STATUS(status);
}

void set_throttling_state(uintptr_t device, bool should_activate)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));

    auto status = reinterpret_cast<Device *>(device)->set_throttling_state(should_activate);
    VALIDATE_STATUS(status);
}

bool get_throttling_state(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));

    auto is_active_expected = reinterpret_cast<Device *>(device)->get_throttling_state();
    VALIDATE_EXPECTED(is_active_expected);

    return is_active_expected.release();
}

void set_overcurrent_state(uintptr_t device, bool should_activate)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));

    auto status = reinterpret_cast<Device*>(device)->set_overcurrent_state(should_activate);
    VALIDATE_STATUS(status);
}

bool get_overcurrent_state(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));

    auto is_required_expected = reinterpret_cast<Device*>(device)->get_overcurrent_state();
    VALIDATE_EXPECTED(is_required_expected);

    return is_required_expected.release();
}

py::bytes read_memory(uintptr_t device, uint32_t address, uint32_t length)
{
    std::unique_ptr<std::string> response = make_unique_nothrow<std::string>(length, '\x00');
    VALIDATE_NOT_NULL(response);
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));

    MemoryView data_view(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(response->data())), length);
    auto status = (reinterpret_cast<Device*>(device))->read_memory(address, data_view);
    VALIDATE_STATUS(status);

    return *response;
}

void write_memory(uintptr_t device, uint32_t address, py::bytes data, uint32_t length)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));

    auto status = (reinterpret_cast<Device*>(device))->write_memory(address, MemoryView(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(std::string(data).c_str())), length));
    VALIDATE_STATUS(status);

    return;
}

void test_chip_memories(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    hailo_status status = reinterpret_cast<Device *>(device)->test_chip_memories();
    VALIDATE_STATUS(status);
}

void i2c_write(uintptr_t device, hailo_i2c_slave_config_t *slave_config, uint32_t register_address, py::bytes data,
    uint32_t length)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    VALIDATE_NOT_NULL(slave_config);

    std::string data_str(data);
    MemoryView data_view = MemoryView::create_const(data_str.c_str(), length);
    auto status = reinterpret_cast<Device *>(device)->i2c_write(*slave_config, register_address, data_view);
    VALIDATE_STATUS(status);
}

py::bytes i2c_read(uintptr_t device, hailo_i2c_slave_config_t *slave_config, uint32_t register_address, uint32_t length)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    VALIDATE_NOT_NULL(slave_config);

    std::unique_ptr<std::string> response = make_unique_nothrow<std::string>(length, '\x00');
    VALIDATE_NOT_NULL(response);

    MemoryView data_view(const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(response->data())), length);
    auto status = reinterpret_cast<Device *>(device)->i2c_read(*slave_config, register_address, data_view);
    VALIDATE_STATUS(status);

    return *response;
}

float32_t power_measurement(uintptr_t device, hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto measurement = reinterpret_cast<Device *>(device)->power_measurement(dvm, measurement_type);
    VALIDATE_EXPECTED(measurement);
    
    return measurement.release();
}

void start_power_measurement(uintptr_t device, uint32_t delay_milliseconds,
    hailo_averaging_factor_t averaging_factor, hailo_sampling_period_t sampling_period)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto status = reinterpret_cast<Device *>(device)->start_power_measurement(delay_milliseconds, averaging_factor,
        sampling_period);
    VALIDATE_STATUS(status);

    return;
}

void set_power_measurement(uintptr_t device, uint32_t index, hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto status = reinterpret_cast<Device *>(device)->set_power_measurement(index, dvm, measurement_type);
    VALIDATE_STATUS(status);

    return;
}

PowerMeasurementData get_power_measurement(uintptr_t device, uint32_t index, bool should_clear)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto measurement_data = reinterpret_cast<Device *>(device)->get_power_measurement(index, should_clear);
    VALIDATE_EXPECTED(measurement_data);

    return PowerMeasurementData(measurement_data.release());
}

void stop_power_measurement(uintptr_t device)
{
    hailo_status status = HAILO_UNINITIALIZED;

    status = hailo_stop_power_measurement((hailo_device)device);
    VALIDATE_STATUS(status);

    return;
}

void reset(uintptr_t device, hailo_reset_device_mode_t mode)
{
    hailo_status status = HAILO_UNINITIALIZED;

    status = hailo_reset_device((hailo_device)device, mode);
    VALIDATE_STATUS(status);

    return;
}

hailo_fw_user_config_information_t examine_user_config(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto user_config_info = reinterpret_cast<Device *>(device)->examine_user_config();
    VALIDATE_EXPECTED(user_config_info);

    return user_config_info.release();
}

py::bytes read_user_config(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto config_buffer = reinterpret_cast<Device *>(device)->read_user_config();
    VALIDATE_EXPECTED(config_buffer);

    std::unique_ptr<std::string> response = make_unique_nothrow<std::string>(
        const_cast<char*>(reinterpret_cast<const char*>(config_buffer->data())), config_buffer->size());
    VALIDATE_NOT_NULL(response);

    return *response;
}

void write_user_config(uintptr_t device, py::bytes data)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    std::string data_str(data); 
    MemoryView data_view = MemoryView::create_const(data_str.c_str(), data_str.size());
    auto status = reinterpret_cast<Device *>(device)->write_user_config(data_view);
    VALIDATE_STATUS(status);

    return;
}

void erase_user_config(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto status = reinterpret_cast<Device *>(device)->erase_user_config();
    VALIDATE_STATUS(status);

    return;
}

py::bytes read_board_config(uintptr_t device)
{
    auto config_buffer = reinterpret_cast<Device *>(device)->read_board_config();
    VALIDATE_EXPECTED(config_buffer);

    std::unique_ptr<std::string> response = make_unique_nothrow<std::string>(
        const_cast<char*>(reinterpret_cast<const char*>(config_buffer->data())), config_buffer->size());
    VALIDATE_NOT_NULL(response);
    
    return *response;
}

void write_board_config(uintptr_t device, py::bytes data)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    std::string data_str(data); 
    MemoryView data_view = MemoryView::create_const(data_str.c_str(), data_str.size());
    auto status = reinterpret_cast<Device *>(device)->write_board_config(data_view);
    VALIDATE_STATUS(status);

    return;
}

hailo_extended_device_information_t get_extended_device_information(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto extended_device_info = reinterpret_cast<Device *>(device)->get_extended_device_information();
    VALIDATE_EXPECTED(extended_device_info);
       
    return extended_device_info.release();
}

hailo_health_info_t get_health_information(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    auto health_info = reinterpret_cast<Device *>(device)->get_health_information();
    VALIDATE_EXPECTED(health_info);
       
    return health_info.release();
}

void sensor_store_config(uintptr_t device, uint32_t section_index, uint32_t reset_data_size, uint32_t sensor_type, const std::string &config_file_path,
    uint16_t config_height, uint16_t config_width, uint16_t config_fps, const std::string &config_name)
{
    hailo_status status = HAILO_UNINITIALIZED;
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));
    status = (reinterpret_cast<Device*>(device))->store_sensor_config(section_index, static_cast<hailo_sensor_types_t>(sensor_type), reset_data_size, config_height, config_width,
        config_fps, config_file_path, config_name);

    VALIDATE_STATUS(status);

    return;
}

void store_isp_config(uintptr_t device, uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps,
    const std::string &isp_static_config_file_path, const std::string &isp_runtime_config_file_path, const std::string &config_name)
{
    hailo_status status = HAILO_UNINITIALIZED;
    VALIDATE_NOT_NULL(reinterpret_cast<Device*>(device));
    status = (reinterpret_cast<Device*>(device))->store_isp_config(reset_config_size, config_height, config_width, config_fps,
        isp_static_config_file_path, isp_runtime_config_file_path, config_name);
    VALIDATE_STATUS(status);

    return;
}

py::bytes sensor_get_sections_info(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto buffer = (reinterpret_cast<Device*>(device))->sensor_get_sections_info();
    VALIDATE_EXPECTED(buffer);
    
    std::unique_ptr<std::string> response = make_unique_nothrow<std::string>(
        const_cast<char*>(reinterpret_cast<const char*>(buffer->data())), buffer->size());
    VALIDATE_NOT_NULL(response);

    return *response;
}

void sensor_set_i2c_bus_index(uintptr_t device, uint32_t sensor_type, uint32_t bus_index)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    hailo_status status = (reinterpret_cast<Device*>(device))->sensor_set_i2c_bus_index(
        static_cast<hailo_sensor_types_t>(sensor_type), bus_index);
    VALIDATE_STATUS(status);

    return;
}

void sensor_load_and_start_config(uintptr_t device, uint32_t section_index)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto status = (reinterpret_cast<Device*>(device))->sensor_load_and_start_config(section_index);
    VALIDATE_STATUS(status);

    return;
}

void sensor_reset(uintptr_t device, uint32_t section_index)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto status = (reinterpret_cast<Device*>(device))->sensor_reset(section_index);
    VALIDATE_STATUS(status);

    return;
}

void sensor_set_generic_i2c_slave(uintptr_t device, uint16_t slave_address,
    uint8_t register_address_size, uint8_t bus_index, uint8_t should_hold_bus, uint8_t endianness)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto status = (reinterpret_cast<Device*>(device))->sensor_set_generic_i2c_slave(slave_address, register_address_size,
        bus_index, should_hold_bus, endianness);
    VALIDATE_STATUS(status);

    return;
}

void firmware_update(uintptr_t device, py::bytes fw_bin, uint32_t fw_bin_length, bool should_reset)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    hailo_status status = reinterpret_cast<Device *>(device)->firmware_update(MemoryView::create_const(std::string(fw_bin).c_str(), fw_bin_length),
        should_reset);
    VALIDATE_STATUS(status);
}

void second_stage_update(uintptr_t device, py::bytes second_stage_bin, uint32_t second_stage_bin_length)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    hailo_status status = reinterpret_cast<Device *>(device)->second_stage_update((uint8_t *)std::string(second_stage_bin).c_str(), 
        second_stage_bin_length);
    VALIDATE_STATUS(status);
}

py::list configure_device_from_hef(uintptr_t device, const HefWrapper &hef,
    const NetworkGroupsParamsMap &configure_params={})
{
    if (nullptr == (void*)device) {
        EXIT_WITH_ERROR("Got NULL in parameter 'device'!");
    }

    auto network_groups = (reinterpret_cast<Device*>(device))->configure(*hef.hef_ptr(), configure_params);
    VALIDATE_EXPECTED(network_groups);

    py::list results;
    for (const auto &network_group : network_groups.value()) {
        results.append(network_group.get());
    }

    return results;
}

// Quantization
void dequantize_output_buffer_from_uint8(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::dequantize_output_buffer<uint8_t, uint8_t>(static_cast<uint8_t*>(src_buffer.mutable_data()),
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::dequantize_output_buffer<uint16_t, uint8_t>(static_cast<uint8_t*>(src_buffer.mutable_data()),
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer<float32_t, uint8_t>(static_cast<uint8_t*>(src_buffer.mutable_data()),
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Output quantization isn't supported from src format type uint8 to dst format type = {}",
                convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void dequantize_output_buffer_from_uint16(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::dequantize_output_buffer<uint16_t, uint16_t>(static_cast<uint16_t*>(src_buffer.mutable_data()),
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer<float32_t, uint16_t>(static_cast<uint16_t*>(src_buffer.mutable_data()),
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Output quantization isn't supported from src dormat type uint16 to dst format type = {}",
                convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void dequantize_output_buffer_from_float32(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer<float32_t, float32_t>(static_cast<float32_t*>(src_buffer.mutable_data()),
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Output quantization isn't supported from src format type float32 to dst format type = {}",
                convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void dequantize_output_buffer_from_uint8_in_place(py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::dequantize_output_buffer_in_place<uint8_t, uint8_t>(
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::dequantize_output_buffer_in_place<uint16_t, uint8_t>(
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer_in_place<float32_t, uint8_t>(
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Output quantization isn't supported from src format type uint8 to dst format type = {}",
                convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void dequantize_output_buffer_from_uint16_in_place(py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::dequantize_output_buffer_in_place<uint16_t, uint16_t>(
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer_in_place<float32_t, uint16_t>(
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Output quantization isn't supported from src dormat type uint16 to dst format type = {}",
                convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void dequantize_output_buffer_from_float32_in_place(py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_FLOAT32:
            Quantization::dequantize_output_buffer_in_place<float32_t, float32_t>(
                static_cast<float32_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Output quantization isn't supported from src format type float32 to dst format type = {}",
                convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void dequantize_output_buffer_in_place(py::array dst_buffer, const hailo_format_type_t &src_dtype,
    const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (src_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            dequantize_output_buffer_from_uint8_in_place(dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            dequantize_output_buffer_from_uint16_in_place(dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            dequantize_output_buffer_from_float32_in_place(dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Unsupported src format type = {}", convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void dequantize_output_buffer(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &src_dtype,
    const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (src_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            dequantize_output_buffer_from_uint8(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            dequantize_output_buffer_from_uint16(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            dequantize_output_buffer_from_float32(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Unsupported src format type = {}", convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void quantize_input_buffer_from_uint8(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::quantize_input_buffer<uint8_t, uint8_t>(static_cast<uint8_t*>(src_buffer.mutable_data()),
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Input quantization isn't supported from src format type uint8 to dst format type = {}", convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void quantize_input_buffer_from_uint16(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::quantize_input_buffer<uint16_t, uint8_t>(static_cast<uint16_t*>(src_buffer.mutable_data()),
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::quantize_input_buffer<uint16_t, uint16_t>(static_cast<uint16_t*>(src_buffer.mutable_data()),
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Input quantization isn't supported from src format type uint16 to dst format type = {}",
                convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void quantize_input_buffer_from_float32(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &dst_dtype,
    uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (dst_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            Quantization::quantize_input_buffer<float32_t, uint8_t>(static_cast<float32_t*>(src_buffer.mutable_data()),
                static_cast<uint8_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            Quantization::quantize_input_buffer<float32_t, uint16_t>(static_cast<float32_t*>(src_buffer.mutable_data()),
                static_cast<uint16_t*>(dst_buffer.mutable_data()), shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Input quantization isn't supported from src format type float32 to dst format type = {}",
                convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void quantize_input_buffer(py::array src_buffer, py::array dst_buffer, const hailo_format_type_t &src_dtype,
    const hailo_format_type_t &dst_dtype, uint32_t shape_size, const hailo_quant_info_t &quant_info)
{
    switch (src_dtype) {
        case HAILO_FORMAT_TYPE_UINT8:
            quantize_input_buffer_from_uint8(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_UINT16:
            quantize_input_buffer_from_uint16(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        case HAILO_FORMAT_TYPE_FLOAT32:
            quantize_input_buffer_from_float32(src_buffer, dst_buffer, dst_dtype, shape_size, quant_info);
            break;
        default:
            LOGGER__ERROR("Input quantization isn't supported for src format type = {}", convert_format_type_to_string(dst_dtype));
            THROW_STATUS_ERROR(HAILO_INVALID_ARGUMENT);
            break;
    }
}

void set_pause_frames(uintptr_t device, bool rx_pause_frames_enable)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto status = reinterpret_cast<Device *>(device)->set_pause_frames(rx_pause_frames_enable);
    VALIDATE_STATUS(status);

    return;
}

void wd_enable(uintptr_t device, hailo_cpu_id_t cpu_id)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    hailo_status status = reinterpret_cast<Device *>(device)->wd_enable(cpu_id);
    VALIDATE_STATUS(status);

    return;
}

void wd_disable(uintptr_t device, hailo_cpu_id_t cpu_id)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    hailo_status status = reinterpret_cast<Device *>(device)->wd_disable(cpu_id);
    VALIDATE_STATUS(status);

    return;
}

void wd_config(uintptr_t device, hailo_cpu_id_t cpu_id, uint32_t wd_cycles, hailo_watchdog_mode_t wd_mode)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    auto status = reinterpret_cast<Device *>(device)->wd_config(cpu_id, wd_cycles, wd_mode);
    VALIDATE_STATUS(status);
}

uint32_t previous_system_state(uintptr_t device, hailo_cpu_id_t cpu_id)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));

    auto system_state = reinterpret_cast<Device *>(device)->previous_system_state(cpu_id);
    VALIDATE_EXPECTED(system_state);

    return system_state.release();
}

hailo_chip_temperature_info_t get_chip_temperature(uintptr_t device)
{
    VALIDATE_NOT_NULL(reinterpret_cast<Device *>(device));
    
    auto temp_info = reinterpret_cast<Device *>(device)->get_chip_temperature();
    VALIDATE_EXPECTED(temp_info);

    return temp_info.release();
}

void set_input_stream_timeout(uintptr_t input_stream, uint32_t timeout_milis)
{
    hailo_status status = hailo_set_input_stream_timeout((hailo_input_stream)input_stream, timeout_milis);
    VALIDATE_STATUS(status);
}

void set_output_stream_timeout(uintptr_t output_stream, uint32_t timeout_milis)
{
    hailo_status status = hailo_set_output_stream_timeout((hailo_output_stream)output_stream, timeout_milis);
    VALIDATE_STATUS(status);
}

void set_notification_callback(uintptr_t device, const std::function<void(uintptr_t, const hailo_notification_t&, py::object)> &callback,
    hailo_notification_id_t notification_id, py::object opaque)
{
    // we capture opaque and move it because when opaque goes out of score it will be deleted,
    // so capturing it ensures that it will not be deleted
    hailo_status status = ((Device*)device)->set_notification_callback(
        [callback, op = std::move(opaque)] (Device &device, const hailo_notification_t &notification, void* opaque) {
            (void)opaque;
            callback((uintptr_t)&device, notification, op);
        }, notification_id, nullptr);
    VALIDATE_STATUS(status);
}

void remove_notification_callback(uintptr_t device, hailo_notification_id_t notification_id)
{
    hailo_status status = hailo_remove_notification_callback(reinterpret_cast<hailo_device>(device), notification_id);
    VALIDATE_STATUS(status);
}

UdpScan::UdpScan()
{
}

std::list<std::string> UdpScan::scan_devices(char* interface_name, uint32_t timeout_milliseconds)
{
    hailo_status status = HAILO_UNINITIALIZED;
    size_t number_of_devices = 0;
    std::list<std::string> device_addresses;
    char textual_ip_address[INET_ADDRSTRLEN] = {0};
    const char *inet_ntop_rc = NULL;

    status = hailo_scan_ethernet_devices(interface_name, m_eth_device_infos, 1, &number_of_devices, timeout_milliseconds);
    VALIDATE_STATUS(status);

    for(size_t i = 0; i<number_of_devices; ++i) {
        inet_ntop_rc = inet_ntop(AF_INET, &(m_eth_device_infos[i].device_address.sin_addr), textual_ip_address, INET_ADDRSTRLEN);
        if (NULL == inet_ntop_rc) {
            EXIT_WITH_ERROR("Could not convert ip address to textual format (inet_ntop has failed)");
        }
        device_addresses.push_back(textual_ip_address);
    }

    return device_addresses;
}

py::bytes read_log(uintptr_t device, size_t byte_count, hailo_cpu_id_t cpu_id)
{
    std::string response;

    response.reserve(byte_count);
    response.resize(byte_count);

    MemoryView response_view ((&response[0]), byte_count);
    auto response_size_expected = ((Device*)device)->read_log(response_view, cpu_id);
    VALIDATE_EXPECTED(response_size_expected);

    response.resize(response_size_expected.release());
    return py::bytes(response);
}

void direct_write_memory(uintptr_t device, uint32_t address, py::bytes buffer)
{
    const auto buffer_str = static_cast<std::string>(buffer);
    hailo_status status = ((Device*)device)->direct_write_memory(address, buffer_str.c_str(),
        (uint32_t) (buffer_str.length()));
    VALIDATE_STATUS(status);
}

py::bytes direct_read_memory(uintptr_t device, uint32_t address, uint32_t size)
{
    std::string buffer_str;

    buffer_str.reserve(size);
    buffer_str.resize(size);

    hailo_status status = ((Device*)device)->direct_read_memory(address, (char*)buffer_str.c_str(), size);
    VALIDATE_STATUS(status);

    buffer_str.resize(size);
    return py::bytes(buffer_str);
}

std::string get_status_message(uint32_t status_in)
{
    auto status_str = hailo_get_status_message((hailo_status)status_in);
    if (status_str == nullptr) {
        // Invalid status
        return "";
    }
    else {
        return status_str;
    }
}

#if defined(__GNUC__)

class TrafficControlUtilWrapper final
{
public:
    static TrafficControlUtilWrapper create(const std::string &ip, uint16_t port, uint32_t rate_bytes_per_sec)
    {
        auto tc_expected = TrafficControlUtil::create(ip, port, rate_bytes_per_sec);
        VALIDATE_STATUS(tc_expected.status());

        auto tc_ptr = make_unique_nothrow<TrafficControlUtil>(tc_expected.release());
        if (nullptr == tc_ptr) {
            VALIDATE_STATUS(HAILO_OUT_OF_HOST_MEMORY);
        }
        return TrafficControlUtilWrapper(std::move(tc_ptr));
    }

    void set_rate_limit()
    {
        VALIDATE_STATUS(m_tc->set_rate_limit());
    }

    void reset_rate_limit()
    {
        VALIDATE_STATUS(m_tc->reset_rate_limit());
    }

    static std::string get_interface_name(const std::string &ip)
    {
        auto name = TrafficControlUtil::get_interface_name(ip);
        VALIDATE_STATUS(name.status());

        return name.value();
    }

    static void add_to_python_module(py::module &m)
    {
        py::class_<TrafficControlUtilWrapper>(m, "TrafficControlUtil")
        .def(py::init(&TrafficControlUtilWrapper::create))
        .def("set_rate_limit", &TrafficControlUtilWrapper::set_rate_limit)
        .def("reset_rate_limit", &TrafficControlUtilWrapper::reset_rate_limit)
        .def_static("get_interface_name", [](const std::string &ip) {
            return TrafficControlUtilWrapper::get_interface_name(ip);
        });
        ;
        ;
    }

private:
    TrafficControlUtilWrapper(std::unique_ptr<TrafficControlUtil> tc) :
        m_tc(std::move(tc))
    {}
    
    std::unique_ptr<TrafficControlUtil> m_tc;
};

#endif

// End of temp hack for hlpcie

PYBIND11_MODULE(_pyhailort, m) {
    m.def("get_status_message", &get_status_message);
    // Device
    m.def("create_eth_device", &create_eth_device);
    m.def("create_pcie_device", &create_pcie_device);
    m.def("scan_pcie_devices", &scan_pcie_devices);
    m.def("release_device", &release_device);
    m.def("get_hlpcie_device", &get_hlpcie_device);
    // Controls
    m.def("identify", &identify);
    m.def("core_identify", &core_identify);
    m.def("set_fw_logger", &set_fw_logger);
    m.def("read_memory", &read_memory);
    m.def("write_memory", &write_memory);
    m.def("power_measurement", &power_measurement);
    m.def("start_power_measurement", &start_power_measurement);
    m.def("stop_power_measurement", &stop_power_measurement);
    m.def("set_power_measurement", &set_power_measurement);
    m.def("get_power_measurement", &get_power_measurement);
    m.def("firmware_update", &firmware_update);
    m.def("second_stage_update", &second_stage_update);
    m.def("examine_user_config", &examine_user_config);
    m.def("read_user_config", &read_user_config);
    m.def("write_user_config", &write_user_config);
    m.def("erase_user_config", &erase_user_config);
    m.def("read_board_config", &read_board_config);
    m.def("write_board_config", &write_board_config);
    m.def("i2c_write", &i2c_write);
    m.def("i2c_read", &i2c_read);
    m.def("sensor_store_config", &sensor_store_config);
    m.def("store_isp_config", &store_isp_config);
    m.def("sensor_set_i2c_bus_index", &sensor_set_i2c_bus_index);
    m.def("sensor_load_and_start_config", &sensor_load_and_start_config);
    m.def("sensor_reset", &sensor_reset);
    m.def("sensor_set_generic_i2c_slave", &sensor_set_generic_i2c_slave);
    m.def("sensor_get_sections_info", &sensor_get_sections_info);
    m.def("reset", &reset);
    m.def("wd_enable", &wd_enable);
    m.def("wd_disable", &wd_disable);
    m.def("wd_config", &wd_config);
    m.def("previous_system_state", &previous_system_state);
    m.def("get_chip_temperature", &get_chip_temperature);
    m.def("get_extended_device_information", &get_extended_device_information);
    m.def("set_pause_frames", &set_pause_frames);
    m.def("test_chip_memories", &test_chip_memories);
    m.def("_get_health_information", &get_health_information);
    m.def("set_throttling_state", &set_throttling_state);
    m.def("get_throttling_state", &get_throttling_state);
    m.def("_set_overcurrent_state", &set_overcurrent_state);
    m.def("_get_overcurrent_state", &get_overcurrent_state);
    //HEF
    m.def("configure_device_from_hef", &configure_device_from_hef);
    //Stream related
    m.def("set_input_stream_timeout", &set_input_stream_timeout);
    m.def("set_output_stream_timeout", &set_output_stream_timeout);
    m.def("set_notification_callback", &set_notification_callback);
    m.def("remove_notification_callback", &remove_notification_callback);
    m.def("dequantize_output_buffer_in_place", &dequantize_output_buffer_in_place);
    m.def("dequantize_output_buffer", &dequantize_output_buffer);
    m.def("quantize_input_buffer", &quantize_input_buffer);

    py::class_<hailo_pcie_device_info_t>(m, "PcieDeviceInfo")
        .def(py::init<>())
        .def_static("_parse", [](const std::string &device_info_str) {
            auto device_info = Device::parse_pcie_device_info(device_info_str);
            VALIDATE_EXPECTED(device_info);
            return device_info.release();
        })
        .def_readwrite("domain", &hailo_pcie_device_info_t::domain)
        .def_readwrite("bus", &hailo_pcie_device_info_t::bus)
        .def_readwrite("device", &hailo_pcie_device_info_t::device)
        .def_readwrite("func", &hailo_pcie_device_info_t::func)
        .def("__str__", [](const hailo_pcie_device_info_t &self) {
            auto device_info_str = Device::pcie_device_info_to_string(self);
            VALIDATE_EXPECTED(device_info_str);
            return device_info_str.release();
        })
        ;

    py::class_<UdpScan>(m, "UdpScan")
        .def(py::init<>())
        .def("scan_devices", &UdpScan::scan_devices)
        ;

    py::class_<PowerMeasurementData>(m, "PowerMeasurementData")
        .def_readonly("average_value", &PowerMeasurementData::m_average_value, "float, The average value of the samples that were sampled")
        .def_readonly("average_time_value_milliseconds", &PowerMeasurementData::m_average_time_value_milliseconds, "float, Average time in milliseconds between sampels")
        .def_readonly("min_value", &PowerMeasurementData::m_min_value, "float, The minimum value of the samples that were sampled")
        .def_readonly("max_value", &PowerMeasurementData::m_max_value, "float, The maximun value of the samples that were sampled")
        .def_readonly("total_number_of_samples", &PowerMeasurementData::m_total_number_of_samples, "uint, The number of samples that were sampled")
        .def("equals", &PowerMeasurementData::equals)
        .def(py::pickle(&PowerMeasurementData::get_state, &PowerMeasurementData::set_state))
        ;

    py::enum_<hailo_device_architecture_t>(m, "DeviceArchitecture")
        .value("HAILO8_A0", HAILO_ARCH_HAILO8_A0)
        .value("HAILO8_B0", HAILO_ARCH_HAILO8_B0)
        .value("MERCURY_CA", HAILO_ARCH_MERCURY_CA)
    ;

    /* TODO: SDK-15648 */
    py::enum_<hailo_dvm_options_t>(m, "DvmTypes", "Enum-like class representing the different DVMs that can be measured.\nThis determines the device that would be measured.")
        .value("AUTO", HAILO_DVM_OPTIONS_AUTO, "Choose the default value according to the supported features.")
        .value("VDD_CORE", HAILO_DVM_OPTIONS_VDD_CORE, "Perform measurements over the core. Exists only in Hailo-8 EVB.")
        .value("VDD_IO", HAILO_DVM_OPTIONS_VDD_IO, "Perform measurements over the IO. Exists only in Hailo-8 EVB.")
        .value("MIPI_AVDD", HAILO_DVM_OPTIONS_MIPI_AVDD, "Perform measurements over the MIPI avdd. Exists only in Hailo-8 EVB.")
        .value("MIPI_AVDD_H", HAILO_DVM_OPTIONS_MIPI_AVDD_H, "Perform measurements over the MIPI avdd_h. Exists only in Hailo-8 EVB.")
        .value("USB_AVDD_IO", HAILO_DVM_OPTIONS_USB_AVDD_IO, "Perform measurements over the IO. Exists only in Hailo-8 EVB.")
        .value("VDD_TOP", HAILO_DVM_OPTIONS_VDD_TOP, "Perform measurements over the top. Exists only in Hailo-8 EVB.")
        .value("USB_AVDD_IO_HV", HAILO_DVM_OPTIONS_USB_AVDD_IO_HV, "Perform measurements over the USB_AVDD_IO_HV. Exists only in Hailo-8 EVB.")
        .value("AVDD_H", HAILO_DVM_OPTIONS_AVDD_H, "Perform measurements over the AVDD_H. Exists only in Hailo-8 EVB.")
        .value("SDIO_VDD_IO", HAILO_DVM_OPTIONS_SDIO_VDD_IO, "Perform measurements over the SDIO_VDDIO. Exists only in Hailo-8 EVB.")
        .value("OVERCURRENT_PROTECTION", HAILO_DVM_OPTIONS_OVERCURRENT_PROTECTION, "Perform measurements over the OVERCURRENT_PROTECTION dvm. Exists only for Hailo-8 platforms supporting current monitoring (such as M.2 and mPCIe).")
        ;

    py::enum_<hailo_power_measurement_types_t>(m, "PowerMeasurementTypes", "Enum-like class representing the different power measurement types. This determines what\nwould be measured on the device.")
        .value("AUTO", HAILO_POWER_MEASUREMENT_TYPES__AUTO, "Choose the default value according to the supported features.")
        .value("SHUNT_VOLTAGE", HAILO_POWER_MEASUREMENT_TYPES__SHUNT_VOLTAGE, "Measure the shunt voltage. Unit is mV")
        .value("BUS_VOLTAGE", HAILO_POWER_MEASUREMENT_TYPES__BUS_VOLTAGE, "Measure the bus voltage. Unit is mV")
        .value("POWER", HAILO_POWER_MEASUREMENT_TYPES__POWER, "Measure the power. Unit is W")
        .value("CURRENT", HAILO_POWER_MEASUREMENT_TYPES__CURRENT, "Measure the current. Unit is mA")
        ;

    py::enum_<hailo_sampling_period_t>(m, "SamplingPeriod", "Enum-like class representing all bit options and related conversion times for each bit\nsetting for Bus Voltage and Shunt Voltage.")
        .value("PERIOD_140us", HAILO_SAMPLING_PERIOD_140US, "The sensor provides a new sampling every 140us.")
        .value("PERIOD_204us", HAILO_SAMPLING_PERIOD_204US, "The sensor provides a new sampling every 204us.")
        .value("PERIOD_332us", HAILO_SAMPLING_PERIOD_332US, "The sensor provides a new sampling every 332us.")
        .value("PERIOD_588us", HAILO_SAMPLING_PERIOD_588US, "The sensor provides a new sampling every 588us.")
        .value("PERIOD_1100us", HAILO_SAMPLING_PERIOD_1100US, "The sensor provides a new sampling every 1100us.")
        .value("PERIOD_2116us", HAILO_SAMPLING_PERIOD_2116US, "The sensor provides a new sampling every 2116us.")
        .value("PERIOD_4156us", HAILO_SAMPLING_PERIOD_4156US, "The sensor provides a new sampling every 4156us.")
        .value("PERIOD_8244us", HAILO_SAMPLING_PERIOD_8244US, "The sensor provides a new sampling every 8244us.")
        ;

    py::enum_<hailo_averaging_factor_t>(m, "AveragingFactor", "Enum-like class representing all the AVG bit settings and related number of averages\nfor each bit setting.")
        .value("AVERAGE_1", HAILO_AVERAGE_FACTOR_1, "Each sample reflects a value of 1 sub-samples.")
        .value("AVERAGE_4", HAILO_AVERAGE_FACTOR_4, "Each sample reflects a value of 4 sub-samples.")
        .value("AVERAGE_16", HAILO_AVERAGE_FACTOR_16, "Each sample reflects a value of 16 sub-samples.")
        .value("AVERAGE_64", HAILO_AVERAGE_FACTOR_64, "Each sample reflects a value of 64 sub-samples.")
        .value("AVERAGE_128", HAILO_AVERAGE_FACTOR_128, "Each sample reflects a value of 128 sub-samples.")
        .value("AVERAGE_256", HAILO_AVERAGE_FACTOR_256, "Each sample reflects a value of 256 sub-samples.")
        .value("AVERAGE_512", HAILO_AVERAGE_FACTOR_512, "Each sample reflects a value of 512 sub-samples.")
        .value("AVERAGE_1024", HAILO_AVERAGE_FACTOR_1024, "Each sample reflects a value of 1024 sub-samples.")
        ;

    py::class_<hailo_notification_t>(m, "Notification")
        .def_readonly("notification_id", &hailo_notification_t::id)
        .def_readonly("sequence", &hailo_notification_t::sequence)
        .def_readonly("body", &hailo_notification_t::body)
        ;

    py::class_<hailo_notification_message_parameters_t>(m, "NotificationMessageParameters")
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_rx_error_notification_message_t, rx_error_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_debug_notification_message_t, debug_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_dataflow_shutdown_notification_message_t, health_monitor_dataflow_shutdown_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_temperature_alarm_notification_message_t, health_monitor_temperature_alarm_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_overcurrent_alert_notification_message_t, health_monitor_overcurrent_alert_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_lcu_ecc_error_notification_message_t, health_monitor_lcu_ecc_error_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_cpu_ecc_notification_message_t, health_monitor_cpu_ecc_notification)
        UNION_PROPERTY(hailo_notification_message_parameters_t, hailo_health_monitor_clock_changed_notification_message_t, health_monitor_clock_changed_notification)
        ;

    py::enum_<hailo_overcurrent_protection_overcurrent_zone_t>(m, "OvercurrentAlertState")
        .value("OVERCURRENT_ZONE_NONE", HAILO_OVERCURRENT_PROTECTION_OVERCURRENT_ZONE__NONE)
        .value("OVERCURRENT_ZONE_ORANGE", HAILO_OVERCURRENT_PROTECTION_OVERCURRENT_ZONE__ORANGE)
        .value("OVERCURRENT_ZONE_RED", HAILO_OVERCURRENT_PROTECTION_OVERCURRENT_ZONE__RED)
        ;
    
    py::enum_<hailo_temperature_protection_temperature_zone_t>(m, "TemperatureZone")
        .value("TEMPERATURE_ZONE_GREEN", HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__GREEN)
        .value("TEMPERATURE_ZONE_ORANGE", HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__ORANGE)
        .value("TEMPERATURE_ZONE_RED", HAILO_TEMPERATURE_PROTECTION_TEMPERATURE_ZONE__RED)
        ;
    
    py::class_<hailo_rx_error_notification_message_t>(m, "RxErrorNotificationMessage")
        .def_readonly("error", &hailo_rx_error_notification_message_t::error)
        .def_readonly("queue_number", &hailo_rx_error_notification_message_t::queue_number)
        .def_readonly("rx_errors_count", &hailo_rx_error_notification_message_t::rx_errors_count)
        ;

    py::class_<hailo_debug_notification_message_t>(m, "DebugNotificationMessage")
        .def_readonly("connection_status", &hailo_debug_notification_message_t::connection_status)
        .def_readonly("connection_type", &hailo_debug_notification_message_t::connection_type)
        .def_readonly("pcie_is_active", &hailo_debug_notification_message_t::pcie_is_active)
        .def_readonly("host_port", &hailo_debug_notification_message_t::host_port)
        .def_readonly("host_ip_addr", &hailo_debug_notification_message_t::host_ip_addr)
        ;

    py::class_<hailo_health_monitor_dataflow_shutdown_notification_message_t>(m, "HealthMonitorDataflowShutdownNotificationMessage")
        .def_readonly("closed_input_streams", &hailo_health_monitor_dataflow_shutdown_notification_message_t::closed_input_streams)
        .def_readonly("closed_output_streams", &hailo_health_monitor_dataflow_shutdown_notification_message_t::closed_output_streams)
        .def_readonly("ts0_temperature", &hailo_health_monitor_dataflow_shutdown_notification_message_t::ts0_temperature)
        .def_readonly("ts1_temperature", &hailo_health_monitor_dataflow_shutdown_notification_message_t::ts1_temperature)
        ;

    py::class_<hailo_health_monitor_temperature_alarm_notification_message_t>(m, "HealthMonitorTemperatureAlarmNotificationMessage")
        .def_readonly("temperature_zone", &hailo_health_monitor_temperature_alarm_notification_message_t::temperature_zone)
        .def_readonly("alarm_ts_id", &hailo_health_monitor_temperature_alarm_notification_message_t::alarm_ts_id)
        .def_readonly("ts0_temperature", &hailo_health_monitor_temperature_alarm_notification_message_t::ts0_temperature)
        .def_readonly("ts1_temperature", &hailo_health_monitor_temperature_alarm_notification_message_t::ts1_temperature)
        ;

    py::class_<hailo_health_monitor_overcurrent_alert_notification_message_t>(m, "HealthMonitorOvercurrentAlertNotificationMessage")
        .def_readonly("overcurrent_zone", &hailo_health_monitor_overcurrent_alert_notification_message_t::overcurrent_zone)
        .def_readonly("exceeded_alert_threshold", &hailo_health_monitor_overcurrent_alert_notification_message_t::exceeded_alert_threshold)
        .def_readonly("sampled_current_during_alert", &hailo_health_monitor_overcurrent_alert_notification_message_t::sampled_current_during_alert)
        ;

    py::class_<hailo_health_monitor_lcu_ecc_error_notification_message_t>(m, "HealthMonitorLcuEccErrorNotificationMessage")
        .def_readonly("cluster_error", &hailo_health_monitor_lcu_ecc_error_notification_message_t::cluster_error)
        ;

    py::class_<hailo_health_monitor_cpu_ecc_notification_message_t>(m, "HealthMonitorCpuEccNotificationMessage")
        .def_readonly("memory_bitmap", &hailo_health_monitor_cpu_ecc_notification_message_t::memory_bitmap)
        ;

    py::class_<hailo_health_monitor_clock_changed_notification_message_t>(m, "HealthMonitoClockChangedNotificationMessage")
        .def_readonly("previous_clock", &hailo_health_monitor_clock_changed_notification_message_t::previous_clock)
        .def_readonly("current_clock", &hailo_health_monitor_clock_changed_notification_message_t::current_clock)
        ;

    py::enum_<hailo_notification_id_t>(m, "NotificationId")
        .value("ETHERNET_RX_ERROR", HAILO_NOTIFICATION_ID_ETHERNET_RX_ERROR)
        .value("HEALTH_MONITOR_TEMPERATURE_ALARM", HAILO_NOTIFICATION_ID_HEALTH_MONITOR_TEMPERATURE_ALARM)
        .value("HEALTH_MONITOR_DATAFLOW_SHUTDOWN", HAILO_NOTIFICATION_ID_HEALTH_MONITOR_DATAFLOW_SHUTDOWN)
        .value("HEALTH_MONITOR_OVERCURRENT_ALARM", HAILO_NOTIFICATION_ID_HEALTH_MONITOR_OVERCURRENT_ALARM)
        .value("HEALTH_MONITOR_LCU_ECC_CORRECTABLE_ERROR", HAILO_NOTIFICATION_ID_LCU_ECC_CORRECTABLE_ERROR)
        .value("HEALTH_MONITOR_LCU_ECC_UNCORRECTABLE_ERROR",  HAILO_NOTIFICATION_ID_LCU_ECC_UNCORRECTABLE_ERROR)
        .value("HEALTH_MONITOR_CPU_ECC_ERROR",  HAILO_NOTIFICATION_ID_CPU_ECC_ERROR)
        .value("HEALTH_MONITOR_CPU_ECC_FATAL",  HAILO_NOTIFICATION_ID_CPU_ECC_FATAL)
        .value("DEBUG", HAILO_NOTIFICATION_ID_DEBUG)
        .value("CONTEXT_SWITCH_BREAKPOINT_REACHED", HAILO_NOTIFICATION_ID_CONTEXT_SWITCH_BREAKPOINT_REACHED)
        .value("HEALTH_MONITOR_CLOCK_CHANGED_EVENT", HAILO_NOTIFICATION_ID_HEALTH_MONITOR_CLOCK_CHANGED_EVENT)
        ;

    py::enum_<hailo_watchdog_mode_t>(m, "WatchdogMode")
        .value("WATCHDOG_MODE_HW_SW", HAILO_WATCHDOG_MODE_HW_SW)
        .value("WATCHDOG_MODE_HW_ONLY", HAILO_WATCHDOG_MODE_HW_ONLY)
        ;

    py::class_<hailo_firmware_version_t>(m, "FirmwareVersion")
        .def_readonly("major", &hailo_firmware_version_t::major)
        .def_readonly("minor", &hailo_firmware_version_t::minor)
        .def_readonly("revision", &hailo_firmware_version_t::revision)
        ;

    py::class_<hailo_device_identity_t>(m, "BoardInformation")
        .def_readonly("protocol_version", &hailo_device_identity_t::protocol_version)
        .def_readonly("fw_version", &hailo_device_identity_t::fw_version)
        .def_readonly("logger_version", &hailo_device_identity_t::logger_version)
        .def_readonly("board_name_length", &hailo_device_identity_t::board_name_length)
        .def_readonly("is_release", &hailo_device_identity_t::is_release)
        .def_readonly("device_architecture", &hailo_device_identity_t::device_architecture)
        .def_property_readonly("board_name", [](const hailo_device_identity_t& board_info) -> py::str {
            return py::str(board_info.board_name, board_info.board_name_length);
        })
        .def_readonly("serial_number_length", &hailo_device_identity_t::serial_number_length)
        .def_property_readonly("serial_number", [](const hailo_device_identity_t& board_info) -> py::str {
            return py::str(board_info.serial_number, board_info.serial_number_length);
        })
        .def_readonly("part_number_length", &hailo_device_identity_t::part_number_length)
        .def_property_readonly("part_number", [](const hailo_device_identity_t& board_info) -> py::str {
            return py::str(board_info.part_number, board_info.part_number_length);
        })
        .def_readonly("product_name_length", &hailo_device_identity_t::product_name_length)
        .def_property_readonly("product_name", [](const hailo_device_identity_t& board_info) -> py::str {
            return py::str(board_info.product_name, board_info.product_name_length);
        })
        ;

    py::class_<hailo_core_information_t>(m, "CoreInformation")
        .def_readonly("is_release", &hailo_core_information_t::is_release)
        .def_readonly("fw_version", &hailo_core_information_t::fw_version)
        ;

    py::class_<hailo_fw_user_config_information_t>(m, "FirmwareUserConfigInformation")
        .def_readonly("version", &hailo_fw_user_config_information_t::version)
        .def_readonly("entry_count", &hailo_fw_user_config_information_t::entry_count)
        .def_readonly("total_size", &hailo_fw_user_config_information_t::total_size)
        ;

    py::enum_<hailo_endianness_t>(m, "Endianness")
        .value("BIG_ENDIAN", HAILO_BIG_ENDIAN)
        .value("LITTLE_ENDIAN", HAILO_LITTLE_ENDIAN)
        ;

    py::enum_<hailo_sensor_types_t>(m, "SensorConfigTypes")
        .value("SENSOR_GENERIC", HAILO_SENSOR_TYPES_GENERIC)
        .value("ONSEMI_AR0220AT", HAILO_SENSOR_TYPES_ONSEMI_AR0220AT)
        .value("SENSOR_RASPICAM", HAILO_SENSOR_TYPES_RASPICAM)
        .value("ONSEMI_AS0149AT", HAILO_SENSOR_TYPES_ONSEMI_AS0149AT)
        .value("HAILO8_ISP", HAILO_SENSOR_TYPES_HAILO8_ISP)
        ;

    py::enum_<SENSOR_CONFIG_OPCODES_t>(m, "SensorConfigOpCode")
        .value("SENSOR_CONFIG_OPCODES_WR", SENSOR_CONFIG_OPCODES_WR)
        .value("SENSOR_CONFIG_OPCODES_RD", SENSOR_CONFIG_OPCODES_RD)
        .value("SENSOR_CONFIG_OPCODES_RMW", SENSOR_CONFIG_OPCODES_RMW)
        .value("SENSOR_CONFIG_OPCODES_DELAY", SENSOR_CONFIG_OPCODES_DELAY)
        ;

    py::class_<hailo_i2c_slave_config_t>(m, "I2CSlaveConfig")
        .def(py::init<>())
        .def_readwrite("endianness", &hailo_i2c_slave_config_t::endianness)
        .def_readwrite("slave_address", &hailo_i2c_slave_config_t::slave_address)
        .def_readwrite("register_address_size", &hailo_i2c_slave_config_t::register_address_size)
        .def_readwrite("bus_index", &hailo_i2c_slave_config_t::bus_index)
        .def_readwrite("should_hold_bus", &hailo_i2c_slave_config_t::should_hold_bus)
        ;

    py::enum_<hailo_reset_device_mode_t>(m, "ResetDeviceMode")
        .value("CHIP", HAILO_RESET_DEVICE_MODE_CHIP)
        .value("NN_CORE", HAILO_RESET_DEVICE_MODE_NN_CORE)
        .value("SOFT", HAILO_RESET_DEVICE_MODE_SOFT)
        .value("FORCED_SOFT", HAILO_RESET_DEVICE_MODE_FORCED_SOFT)
        ;

    py::enum_<hailo_stream_direction_t>(m, "StreamDirection")
        .value("H2D", HAILO_H2D_STREAM)
        .value("D2H", HAILO_D2H_STREAM)
        ;

    py::class_<hailo_3d_image_shape_t>(m, "ImageShape")
        .def(py::init<>())
        .def(py::init<const uint32_t, const uint32_t, const uint32_t>())
        .def_readwrite("height", &hailo_3d_image_shape_t::height)
        .def_readwrite("width", &hailo_3d_image_shape_t::width)
        .def_readwrite("features", &hailo_3d_image_shape_t::features)
        ;

    py::class_<hailo_nms_shape_t>(m, "NmsShape")
        .def(py::init<>())
        .def_readonly("number_of_classes", &hailo_nms_shape_t::number_of_classes)
        .def_readonly("max_bboxes_per_class", &hailo_nms_shape_t::max_bboxes_per_class)
        ;

    py::class_<hailo_nms_info_t>(m, "NmsInfo")
        .def(py::init<>())
        .def_readwrite("number_of_classes", &hailo_nms_info_t::number_of_classes)
        .def_readwrite("max_bboxes_per_class", &hailo_nms_info_t::max_bboxes_per_class)
        .def_readwrite("bbox_size", &hailo_nms_info_t::bbox_size)
        .def_readwrite("chunks_per_frame", &hailo_nms_info_t::chunks_per_frame)
        ;

    py::enum_<hailo_format_type_t>(m, "FormatType", "Data formats accepted by HailoRT.")
        .value("AUTO", HAILO_FORMAT_TYPE_AUTO, "Chosen automatically to match the format expected by the device, usually UINT8.")
        .value("UINT8", HAILO_FORMAT_TYPE_UINT8)
        .value("UINT16", HAILO_FORMAT_TYPE_UINT16)
        .value("FLOAT32", HAILO_FORMAT_TYPE_FLOAT32)
        ;

    py::enum_<hailo_format_order_t>(m, "FormatOrder")
        .value("AUTO", HAILO_FORMAT_ORDER_AUTO)
        .value("NHWC", HAILO_FORMAT_ORDER_NHWC)
        .value("NHCW", HAILO_FORMAT_ORDER_NHCW)
        .value("FCR", HAILO_FORMAT_ORDER_FCR)
        .value("F8CR", HAILO_FORMAT_ORDER_F8CR)
        .value("NHW", HAILO_FORMAT_ORDER_NHW)
        .value("NC", HAILO_FORMAT_ORDER_NC)
        .value("BAYER_RGB", HAILO_FORMAT_ORDER_BAYER_RGB)
        .value("12_BIT_BAYER_RGB", HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB)
        .value("HAILO_NMS", HAILO_FORMAT_ORDER_HAILO_NMS)
        .value("RGB888", HAILO_FORMAT_ORDER_RGB888)
        .value("NCHW", HAILO_FORMAT_ORDER_NCHW)
        .value("YUY2", HAILO_FORMAT_ORDER_YUY2)
        ;

    py::enum_<hailo_format_flags_t>(m, "FormatFlags", py::arithmetic())
        .value("NONE", HAILO_FORMAT_FLAGS_NONE)
        .value("QUANTIZED", HAILO_FORMAT_FLAGS_QUANTIZED)
        .value("TRANSPOSED", HAILO_FORMAT_FLAGS_TRANSPOSED)
        .value("HOST_ARGMAX", HAILO_FORMAT_FLAGS_HOST_ARGMAX)
        ;

    py::enum_<hailo_stream_transform_mode_t>(m, "TransformMode")
        .value("NO_TRANSFORM", HAILO_STREAM_NO_TRANSFORM)
        .value("TRANSFORM_COPY", HAILO_STREAM_TRANSFORM_COPY)
        ;

    py::class_<hailo_format_t>(m, "HailoFormat")
        .def(py::init<>())
        .def_readwrite("type", &hailo_format_t::type)
        .def_readwrite("order", &hailo_format_t::order)
        .def_readwrite("flags", &hailo_format_t::flags)
        .def("equals", &hailo_format_equals)
        .def(py::pickle(
            [](const hailo_format_t &hailo_format) { // __getstate__
                return py::make_tuple(
                    hailo_format.type,
                    hailo_format.order,
                    hailo_format.flags);
            },
            [](py::tuple t) { // __setstate__
                hailo_format_t hailo_format;
                hailo_format.type = t[0].cast<hailo_format_type_t>();
                hailo_format.order = t[1].cast<hailo_format_order_t>();
                hailo_format.flags = t[2].cast<hailo_format_flags_t>();
                return hailo_format;
            }
        ))
        ;

    py::class_<hailo_quant_info_t>(m, "QuantInfo")
        .def(py::init<>())
        .def(py::init<const float32_t, const float32_t, const float32_t, const float32_t>())
        .def_readwrite("qp_zp", &hailo_quant_info_t::qp_zp)
        .def_readwrite("qp_scale", &hailo_quant_info_t::qp_scale)
        .def_readwrite("limvals_min", &hailo_quant_info_t::limvals_min)
        .def_readwrite("limvals_max", &hailo_quant_info_t::limvals_max)
        ;

    py::enum_<hailo_mipi_pixels_per_clock_t>(m, "MipiPixelsPerClock")
        .value("PIXELS_PER_CLOCK_1", HAILO_MIPI_PIXELS_PER_CLOCK_1)
        .value("PIXELS_PER_CLOCK_2", HAILO_MIPI_PIXELS_PER_CLOCK_2)
        .value("PIXELS_PER_CLOCK_4", HAILO_MIPI_PIXELS_PER_CLOCK_4)
        ;

    py::enum_<hailo_mipi_clock_selection_t>(m, "MipiClockSelection")
        .value("SELECTION_80_TO_100_MBPS", HAILO_MIPI_CLOCK_SELECTION_80_TO_100_MBPS)
        .value("SELECTION_100_TO_120_MBPS", HAILO_MIPI_CLOCK_SELECTION_100_TO_120_MBPS)
        .value("SELECTION_120_TO_160_MBPS", HAILO_MIPI_CLOCK_SELECTION_120_TO_160_MBPS)
        .value("SELECTION_160_TO_200_MBPS", HAILO_MIPI_CLOCK_SELECTION_160_TO_200_MBPS)
        .value("SELECTION_200_TO_240_MBPS", HAILO_MIPI_CLOCK_SELECTION_200_TO_240_MBPS)
        .value("SELECTION_240_TO_280_MBPS", HAILO_MIPI_CLOCK_SELECTION_240_TO_280_MBPS)
        .value("SELECTION_280_TO_320_MBPS", HAILO_MIPI_CLOCK_SELECTION_280_TO_320_MBPS)
        .value("SELECTION_320_TO_360_MBPS", HAILO_MIPI_CLOCK_SELECTION_320_TO_360_MBPS)
        .value("SELECTION_360_TO_400_MBPS", HAILO_MIPI_CLOCK_SELECTION_360_TO_400_MBPS)
        .value("SELECTION_400_TO_480_MBPS", HAILO_MIPI_CLOCK_SELECTION_400_TO_480_MBPS)
        .value("SELECTION_480_TO_560_MBPS", HAILO_MIPI_CLOCK_SELECTION_480_TO_560_MBPS)
        .value("SELECTION_560_TO_640_MBPS", HAILO_MIPI_CLOCK_SELECTION_560_TO_640_MBPS)
        .value("SELECTION_640_TO_720_MBPS", HAILO_MIPI_CLOCK_SELECTION_640_TO_720_MBPS)
        .value("SELECTION_720_TO_800_MBPS", HAILO_MIPI_CLOCK_SELECTION_720_TO_800_MBPS)
        .value("SELECTION_800_TO_880_MBPS", HAILO_MIPI_CLOCK_SELECTION_800_TO_880_MBPS)
        .value("SELECTION_880_TO_1040_MBPS", HAILO_MIPI_CLOCK_SELECTION_880_TO_1040_MBPS)
        .value("SELECTION_1040_TO_1200_MBPS", HAILO_MIPI_CLOCK_SELECTION_1040_TO_1200_MBPS)
        .value("SELECTION_1200_TO_1350_MBPS", HAILO_MIPI_CLOCK_SELECTION_1200_TO_1350_MBPS)
        .value("SELECTION_1350_TO_1500_MBPS", HAILO_MIPI_CLOCK_SELECTION_1350_TO_1500_MBPS)
        .value("SELECTION_1500_TO_1750_MBPS", HAILO_MIPI_CLOCK_SELECTION_1500_TO_1750_MBPS)
        .value("SELECTION_1750_TO_2000_MBPS", HAILO_MIPI_CLOCK_SELECTION_1750_TO_2000_MBPS)
        .value("SELECTION_2000_TO_2250_MBPS", HAILO_MIPI_CLOCK_SELECTION_2000_TO_2250_MBPS)
        .value("SELECTION_2250_TO_2500_MBPS", HAILO_MIPI_CLOCK_SELECTION_2250_TO_2500_MBPS)
        .value("SELECTION_AUTOMATIC", HAILO_MIPI_CLOCK_SELECTION_AUTOMATIC)
        ;

    py::enum_<hailo_mipi_data_type_rx_t>(m, "MipiDataTypeRx")
        .value("RGB_444", HAILO_MIPI_RX_TYPE_RGB_444)
        .value("RGB_555", HAILO_MIPI_RX_TYPE_RGB_555)
        .value("RGB_565", HAILO_MIPI_RX_TYPE_RGB_565)
        .value("RGB_666", HAILO_MIPI_RX_TYPE_RGB_666)
        .value("RGB_888", HAILO_MIPI_RX_TYPE_RGB_888)
        .value("RAW_6", HAILO_MIPI_RX_TYPE_RAW_6)
        .value("RAW_7", HAILO_MIPI_RX_TYPE_RAW_7)
        .value("RAW_8", HAILO_MIPI_RX_TYPE_RAW_8)
        .value("RAW_10", HAILO_MIPI_RX_TYPE_RAW_10)
        .value("RAW_12", HAILO_MIPI_RX_TYPE_RAW_12)
        .value("RAW_14", HAILO_MIPI_RX_TYPE_RAW_14)
        ;

    py::enum_<hailo_mipi_isp_image_in_order_t>(m, "MipiIspImageInOrder")
        .value("B_FIRST", HAILO_MIPI_ISP_IMG_IN_ORDER_B_FIRST)
        .value("GB_FIRST", HAILO_MIPI_ISP_IMG_IN_ORDER_GB_FIRST)
        .value("GR_FIRST", HAILO_MIPI_ISP_IMG_IN_ORDER_GR_FIRST)
        .value("R_FIRST", HAILO_MIPI_ISP_IMG_IN_ORDER_R_FIRST)
        ;

    py::enum_<hailo_mipi_isp_image_out_data_type_t>(m, "MipiIspImageOutDataType")
        .value("RGB_888", HAILO_MIPI_IMG_OUT_DATA_TYPE_RGB_888)
        .value("YUV_422", HAILO_MIPI_IMG_OUT_DATA_TYPE_YUV_422)
        ;

    py::enum_<hailo_mipi_isp_light_frequency_t>(m, "IspLightFrequency")
        .value("LIGHT_FREQ_60_HZ", HAILO_MIPI_ISP_LIGHT_FREQUENCY_60HZ)
        .value("LIGHT_FREQ_50_HZ", HAILO_MIPI_ISP_LIGHT_FREQUENCY_50HZ)
        ;
    
    py::class_<hailo_isp_params_t>(m, "MipiIspParams")
        .def_readwrite("img_in_order", &hailo_isp_params_t::isp_img_in_order)
        .def_readwrite("img_out_data_type", &hailo_isp_params_t::isp_img_out_data_type)
        .def_readwrite("crop_enable", &hailo_isp_params_t::isp_crop_enable)
        .def_readwrite("crop_output_width_pixels", &hailo_isp_params_t::isp_crop_output_width_pixels)
        .def_readwrite("crop_output_height_pixels", &hailo_isp_params_t::isp_crop_output_height_pixels)
        .def_readwrite("crop_output_width_start_offset_pixels", &hailo_isp_params_t::isp_crop_output_width_start_offset_pixels)
        .def_readwrite("crop_output_height_start_offset_pixels", &hailo_isp_params_t::isp_crop_output_height_start_offset_pixels)
        .def_readwrite("test_pattern_enable", &hailo_isp_params_t::isp_test_pattern_enable)
        .def_readwrite("configuration_bypass", &hailo_isp_params_t::isp_configuration_bypass)
        .def_readwrite("run_time_ae_enable", &hailo_isp_params_t::isp_run_time_ae_enable)
        .def_readwrite("run_time_awb_enable", &hailo_isp_params_t::isp_run_time_awb_enable)
        .def_readwrite("run_time_adt_enable", &hailo_isp_params_t::isp_run_time_adt_enable)
        .def_readwrite("run_time_af_enable", &hailo_isp_params_t::isp_run_time_af_enable)
        .def_readwrite("isp_run_time_calculations_interval_ms", &hailo_isp_params_t::isp_run_time_calculations_interval_ms)
        .def_readwrite("isp_light_frequency", &hailo_isp_params_t::isp_light_frequency)
        ;

    py::class_<hailo_mipi_common_params_t>(m, "MipiCommonParams")
        .def_readwrite("img_width_pixels", &hailo_mipi_common_params_t::img_width_pixels)
        .def_readwrite("img_height_pixels", &hailo_mipi_common_params_t::img_height_pixels)
        .def_readwrite("pixels_per_clock", &hailo_mipi_common_params_t::pixels_per_clock)
        .def_readwrite("number_of_lanes", &hailo_mipi_common_params_t::number_of_lanes)
        .def_readwrite("clock_selection", &hailo_mipi_common_params_t::clock_selection)
        .def_readwrite("virtual_channel_index", &hailo_mipi_common_params_t::virtual_channel_index)
        .def_readwrite("data_rate", &hailo_mipi_common_params_t::data_rate)
        ;

    py::class_<hailo_transform_params_t>(m, "TransformParams")
        .def(py::init<>())
        .def_readwrite("transform_mode", &hailo_transform_params_t::transform_mode)
        .def_readwrite("user_buffer_format", &hailo_transform_params_t::user_buffer_format)
        ;

    py::class_<hailo_eth_output_stream_params_t>(m, "EthOutputStreamParams")
        .def(py::init<>())
        .def_readwrite("device_port", &hailo_eth_output_stream_params_t::device_port)
        .def_readwrite("host_address", &hailo_eth_output_stream_params_t::host_address)
        .def_readwrite("is_sync_enabled", &hailo_eth_output_stream_params_t::is_sync_enabled)
        .def_readwrite("max_payload_size", &hailo_eth_output_stream_params_t::max_payload_size)
        .def_readwrite("buffers_threshold", &hailo_eth_output_stream_params_t::buffers_threshold)
        ;

    py::class_<hailo_eth_input_stream_params_t>(m, "EthInputStreamParams")
        .def(py::init<>())
        .def_readwrite("device_port", &hailo_eth_input_stream_params_t::device_port)
        .def_readwrite("host_address", &hailo_eth_input_stream_params_t::host_address)
        .def_readwrite("max_payload_size", &hailo_eth_input_stream_params_t::max_payload_size)
        .def_readwrite("is_sync_enabled", &hailo_eth_input_stream_params_t::is_sync_enabled)
        .def_readwrite("frames_per_sync", &hailo_eth_input_stream_params_t::frames_per_sync)
        .def_readwrite("buffers_threshold", &hailo_eth_input_stream_params_t::buffers_threshold)
        ;

    py::class_<hailo_pcie_output_stream_params_t>(m, "PcieOutputStreamParams")
        .def(py::init<>())
        ;

    py::class_<hailo_pcie_input_stream_params_t>(m, "PcieInputStreamParams")
        .def(py::init<>())
        ;

    py::class_<hailo_core_input_stream_params_t>(m, "CoreInputStreamParams")
        .def(py::init<>())
        ;

    py::class_<hailo_core_output_stream_params_t>(m, "CoreOutputStreamParams")
        .def(py::init<>())
        ;

    py::class_<hailo_mipi_input_stream_params_t>(m, "MipiInputStreamParams")
        .def(py::init<>())
        .def_readwrite("mipi_common_params", &hailo_mipi_input_stream_params_t::mipi_common_params)
        .def_readwrite("mipi_rx_id", &hailo_mipi_input_stream_params_t::mipi_rx_id)
        .def_readwrite("data_type", &hailo_mipi_input_stream_params_t::data_type)
        .def_readwrite("isp_enable", &hailo_mipi_input_stream_params_t::isp_enable)
        .def_readwrite("isp_params", &hailo_mipi_input_stream_params_t::isp_params)
        ;

    py::enum_<hailo_stream_interface_t>(m, "StreamInterface")
        .value("PCIe", HAILO_STREAM_INTERFACE_PCIE)
        .value("CORE", HAILO_STREAM_INTERFACE_CORE)
        .value("ETH", HAILO_STREAM_INTERFACE_ETH)
        .value("MIPI", HAILO_STREAM_INTERFACE_MIPI)
        ;

    py::class_<hailo_vstream_params_t>(m, "VStreamParams")
        .def(py::init<>())
        .def_readwrite("user_buffer_format", &hailo_vstream_params_t::user_buffer_format)
        .def_readwrite("timeout_ms", &hailo_vstream_params_t::timeout_ms)
        .def_readwrite("queue_size", &hailo_vstream_params_t::queue_size)
        ;

    py::enum_<hailo_latency_measurement_flags_t>(m, "LatencyMeasurementFlags")
        .value("NONE", HAILO_LATENCY_NONE)
        .value("CLEAR_AFTER_GET", HAILO_LATENCY_CLEAR_AFTER_GET)
        .value("MEASURE", HAILO_LATENCY_MEASURE)
        ;

    py::enum_<hailo_power_mode_t>(m, "PowerMode")
        .value("ULTRA_PERFORMANCE", HAILO_POWER_MODE_ULTRA_PERFORMANCE)
        .value("PERFORMANCE", HAILO_POWER_MODE_PERFORMANCE)
        ;

    py::class_<hailo_activate_network_group_params_t>(m, "ActivateNetworkGroupParams")
        .def(py::init<>())
        .def_static("default", []() {
            return HailoRTDefaults::get_network_group_params();
        });
        ;

    py::class_<hailo_vdevice_params_t>(m, "VDeviceParams")
        .def(py::init<>())
        .def_readwrite("device_count", &hailo_vdevice_params_t::device_count)
        .def_static("default", []() {
            return HailoRTDefaults::get_vdevice_params();
        });
        ;

    py::class_<hailo_stream_parameters_t>(m, "StreamParameters")
        .def_readwrite("stream_interface", &hailo_stream_parameters_t::stream_interface)
        .def_readonly("direction", &hailo_stream_parameters_t::direction)
        STREAM_PARAMETERS_UNION_PROPERTY(pcie_input_params, hailo_pcie_input_stream_params_t,
            HAILO_STREAM_INTERFACE_PCIE, HAILO_H2D_STREAM)
        STREAM_PARAMETERS_UNION_PROPERTY(core_input_params, hailo_core_input_stream_params_t,
            HAILO_STREAM_INTERFACE_CORE, HAILO_H2D_STREAM)
        STREAM_PARAMETERS_UNION_PROPERTY(eth_input_params, hailo_eth_input_stream_params_t,
            HAILO_STREAM_INTERFACE_ETH, HAILO_H2D_STREAM)
        STREAM_PARAMETERS_UNION_PROPERTY(mipi_input_params, hailo_mipi_input_stream_params_t,
            HAILO_STREAM_INTERFACE_MIPI, HAILO_H2D_STREAM)
        STREAM_PARAMETERS_UNION_PROPERTY(pcie_output_params, hailo_pcie_output_stream_params_t,
            HAILO_STREAM_INTERFACE_PCIE, HAILO_D2H_STREAM)
        STREAM_PARAMETERS_UNION_PROPERTY(eth_output_params, hailo_eth_output_stream_params_t,
            HAILO_STREAM_INTERFACE_ETH, HAILO_D2H_STREAM)
        STREAM_PARAMETERS_UNION_PROPERTY(core_output_params, hailo_core_output_stream_params_t,
            HAILO_STREAM_INTERFACE_CORE, HAILO_D2H_STREAM)
        ;

    py::class_<hailo_network_parameters_t>(m, "NetworkParameters")
        .def(py::init<>())
        .def_readwrite("batch_size", &hailo_network_parameters_t::batch_size)
        ;


    py::class_<ConfigureNetworkParams>(m, "ConfigureParams")
        .def(py::init<>())
        .def_readwrite("batch_size", &ConfigureNetworkParams::batch_size)
        .def_readwrite("power_mode", &ConfigureNetworkParams::power_mode)
        .def_readwrite("stream_params_by_name", &ConfigureNetworkParams::stream_params_by_name)
        .def_readwrite("network_params_by_name", &ConfigureNetworkParams::network_params_by_name)
        ;

    py::class_<hailo_chip_temperature_info_t>(m, "TemperatureInfo")
        .def_readonly("ts0_temperature", &hailo_chip_temperature_info_t::ts0_temperature)
        .def_readonly("ts1_temperature", &hailo_chip_temperature_info_t::ts1_temperature)
        .def_readonly("sample_count", &hailo_chip_temperature_info_t::sample_count)
        .def("equals", &temperature_info_equals)
        .def(py::pickle(
            [](const hailo_chip_temperature_info_t &temperature_info) { // __getstate__
                return py::make_tuple(
                    temperature_info.ts0_temperature,
                    temperature_info.ts1_temperature,
                    temperature_info.sample_count);
            },
            [](py::tuple t) { // __setstate__
                hailo_chip_temperature_info_t temperature_info;
                temperature_info.ts0_temperature = t[0].cast<float32_t>();
                temperature_info.ts1_temperature = t[1].cast<float32_t>();
                temperature_info.sample_count = t[2].cast<uint16_t>();
                return temperature_info;
            }
        ))
        ;

    py::class_<hailo_throttling_level_t>(m, "ThrottlingLevel", py::module_local())
        .def_readonly("temperature_threshold", &hailo_throttling_level_t::temperature_threshold)
        .def_readonly("hysteresis_temperature_threshold", &hailo_throttling_level_t::hysteresis_temperature_threshold)
        .def_readonly("throttling_nn_clock_freq", &hailo_throttling_level_t::throttling_nn_clock_freq)
        ;

    py::class_<hailo_health_info_t>(m, "HealthInformation")
        .def_readonly("overcurrent_protection_active", &hailo_health_info_t::overcurrent_protection_active)
        .def_readonly("current_overcurrent_zone", &hailo_health_info_t::current_overcurrent_zone)
        .def_readonly("red_overcurrent_threshold", &hailo_health_info_t::red_overcurrent_threshold)
        .def_readonly("orange_overcurrent_threshold", &hailo_health_info_t::orange_overcurrent_threshold)
        .def_readonly("temperature_throttling_active", &hailo_health_info_t::temperature_throttling_active)
        .def_readonly("current_temperature_zone", &hailo_health_info_t::current_temperature_zone)
        .def_readonly("current_temperature_throttling_level", &hailo_health_info_t::current_temperature_throttling_level)
        .def_readonly("temperature_throttling_levels", &hailo_health_info_t::temperature_throttling_levels)
        .def_property_readonly("temperature_throttling_levels", [](const hailo_health_info_t& info) -> py::list {
            std::vector<hailo_throttling_level_t> throttling_levels;
            for (const auto &temperature_throttling_level : info.temperature_throttling_levels) {
                throttling_levels.push_back(temperature_throttling_level);
            }
            return py::cast(throttling_levels);
        })
        .def_readonly("orange_temperature_threshold", &hailo_health_info_t::orange_temperature_threshold)
        .def_readonly("orange_hysteresis_temperature_threshold", &hailo_health_info_t::orange_hysteresis_temperature_threshold)
        .def_readonly("red_temperature_threshold", &hailo_health_info_t::red_temperature_threshold)
        .def_readonly("red_hysteresis_temperature_threshold", &hailo_health_info_t::red_hysteresis_temperature_threshold)
        ;

    py::class_<hailo_extended_device_information_t>(m, "ExtendedDeviceInformation")
        .def_readonly("neural_network_core_clock_rate", &hailo_extended_device_information_t::neural_network_core_clock_rate)
        .def_readonly("supported_features", &hailo_extended_device_information_t::supported_features)
        .def_readonly("boot_source", &hailo_extended_device_information_t::boot_source)
        .def_readonly("lcs", &hailo_extended_device_information_t::lcs)
        .def_property_readonly("unit_level_tracking_id", [](const hailo_extended_device_information_t& info) -> py::bytes {
            return std::string((const char*) info.unit_level_tracking_id, sizeof(info.unit_level_tracking_id));
        })
        .def_property_readonly("eth_mac_address", [](const hailo_extended_device_information_t& info) -> py::bytes {
            return std::string((const char*) info.eth_mac_address, sizeof(info.eth_mac_address));
        })
        .def_property_readonly("soc_id", [](const hailo_extended_device_information_t& info) -> py::bytes {
            return std::string((const char*) info.soc_id, sizeof(info.soc_id));
        })
        .def_property_readonly("soc_pm_values", [](const hailo_extended_device_information_t& info) -> py::bytes {
            return std::string((const char*) info.soc_pm_values, sizeof(info.soc_pm_values));
        })
        ;

    py::enum_<hailo_device_boot_source_t>(m, "BootSource")
        .value("INVALID", HAILO_DEVICE_BOOT_SOURCE_INVALID)
        .value("PCIE", HAILO_DEVICE_BOOT_SOURCE_PCIE)
        .value("FLASH", HAILO_DEVICE_BOOT_SOURCE_FLASH)
        ;

    py::enum_<hailo_fw_logger_interface_t>(m, "FwLoggerInterface", py::arithmetic())
        .value("PCIE", HAILO_FW_LOGGER_INTERFACE_PCIE)
        .value("UART", HAILO_FW_LOGGER_INTERFACE_UART)
        ;

    py::enum_<hailo_fw_logger_level_t>(m, "FwLoggerLevel")
        .value("TRACE", HAILO_FW_LOGGER_LEVEL_TRACE)
        .value("DEBUG", HAILO_FW_LOGGER_LEVEL_DEBUG)
        .value("INFO", HAILO_FW_LOGGER_LEVEL_INFO)
        .value("WARN", HAILO_FW_LOGGER_LEVEL_WARN)
        .value("ERROR", HAILO_FW_LOGGER_LEVEL_ERROR)
        .value("FATAL", HAILO_FW_LOGGER_LEVEL_FATAL)
        ;

    py::class_<hailo_device_supported_features_t>(m, "SupportedFeatures")
        .def_readonly("ethernet", &hailo_device_supported_features_t::ethernet)
        .def_readonly("mipi", &hailo_device_supported_features_t::mipi)
        .def_readonly("pcie", &hailo_device_supported_features_t::pcie)
        .def_readonly("current_monitoring", &hailo_device_supported_features_t::current_monitoring)
        .def_readonly("mdio", &hailo_device_supported_features_t::mdio)
        ;

    py::class_<sockaddr_in>(m, "sockaddr_in")
        .def_readwrite("sin_port", &sockaddr_in::sin_port)
        ;

    py::register_exception<HailoRTException>(m, "HailoRTException");
    py::register_exception<HailoRTCustomException>(m, "HailoRTCustomException");
    py::register_exception<HailoRTStatusException>(m, "HailoRTStatusException");

    py::enum_<hailo_cpu_id_t>(m, "CpuId")
        .value("CPU0", HAILO_CPU_ID_0)
        .value("CPU1", HAILO_CPU_ID_1)
        ;

    py::enum_<hailo_bootloader_version_t>(m, "BootloaderVersion")
        .value("UNSIGNED", BOOTLOADER_VERSION_HAILO8_B0_UNSIGNED)
        .value("SIGNED", BOOTLOADER_VERSION_HAILO8_B0_SIGNED)
        ;

    py::class_<uint32_t>(m, "HailoRTDefaults")
        .def_static("HAILO_INFINITE", []() { return HAILO_INFINITE;} )
        .def_static("HAILO_DEFAULT_ETH_CONTROL_PORT", []() { return HAILO_DEFAULT_ETH_CONTROL_PORT;} )
        .def_static("BBOX_PARAMS", []() { return HailoRTCommon::BBOX_PARAMS;} )
        .def_static("DEVICE_BASE_INPUT_STREAM_PORT", []() { return HailoRTCommon::ETH_INPUT_BASE_PORT;} )
        .def_static("DEVICE_BASE_OUTPUT_STREAM_PORT", []() { return HailoRTCommon::ETH_OUTPUT_BASE_PORT;} )
        .def_static("PCIE_ANY_DOMAIN", []() { return HAILO_PCIE_ANY_DOMAIN;} )
        ;

    py::class_<hailo_network_group_info_t>(m, "NetworkGroupInfo", py::module_local())
        .def_readonly("name", &hailo_network_group_info_t::name)
        .def_readonly("is_multi_context", &hailo_network_group_info_t::is_multi_context)
        ;

    py::class_<hailo_vstream_info_t>(m, "VStreamInfo", py::module_local())
        .def_property_readonly("shape", [](const hailo_vstream_info_t &self) {
            switch (self.format.order) {
                case HAILO_FORMAT_ORDER_NC:
                    return py::make_tuple(self.shape.features);
                case HAILO_FORMAT_ORDER_NHW:
                    return py::make_tuple(self.shape.height, self.shape.width);
                case HAILO_FORMAT_ORDER_HAILO_NMS:
                    return py::make_tuple(self.nms_shape.number_of_classes, HailoRTCommon::BBOX_PARAMS, self.nms_shape.max_bboxes_per_class);
                default:
                    return py::make_tuple(self.shape.height, self.shape.width, self.shape.features);
            }
        })
        .def_property_readonly("nms_shape", [](const hailo_vstream_info_t &self) {
            if (HAILO_FORMAT_ORDER_HAILO_NMS != self.format.order) {
                throw HailoRTCustomException("nms_shape is availale only on nms order vstreams");
            }
            return self.nms_shape;
        })
        .def_readonly("direction", &hailo_vstream_info_t::direction)
        .def_readonly("format", &hailo_vstream_info_t::format)
        .def_readonly("quant_info", &hailo_vstream_info_t::quant_info)
        .def_readonly("name", &hailo_vstream_info_t::name)
        .def_readonly("network_name", &hailo_vstream_info_t::network_name)
        .def("__repr__", [](const hailo_vstream_info_t &self) {
            return std::string("VStreamInfo(\"") + std::string(self.name) + std::string("\")");
        })
        ;

    py::class_<hailo_stream_info_t>(m, "StreamInfo", py::module_local())
        .def_property_readonly("shape", [](const hailo_stream_info_t &self) {
            switch (self.format.order) {
                case HAILO_FORMAT_ORDER_NC:
                    return py::make_tuple(self.hw_shape.features);
                case HAILO_FORMAT_ORDER_NHW:
                    return py::make_tuple(self.hw_shape.height, self.hw_shape.width);
                case HAILO_FORMAT_ORDER_HAILO_NMS:
                    return py::make_tuple(HailoRTCommon::get_nms_hw_frame_size(self.nms_info));
                default:
                    return py::make_tuple(self.hw_shape.height, self.hw_shape.width, self.hw_shape.features);
            }
        })
        .def_property_readonly("nms_shape", [](const hailo_stream_info_t &self) {
            if (HAILO_FORMAT_ORDER_HAILO_NMS != self.format.order) {
                throw HailoRTCustomException("nms_shape is availale only on nms order streams");
            }
            return py::make_tuple(HailoRTCommon::get_nms_hw_frame_size(self.nms_info));
        })
        .def_readonly("direction", &hailo_stream_info_t::direction)
        .def_readonly("format", &hailo_stream_info_t::format)
        .def_readonly("name", &hailo_stream_info_t::name)
        .def_readonly("sys_index", &hailo_stream_info_t::index)
        .def_readonly("data_bytes", &hailo_stream_info_t::hw_data_bytes)
        .def("__repr__", [](const hailo_stream_info_t &self) {
            return std::string("StreamInfo(\"") + std::string(self.name) + std::string("\")");
        })
        ;

    // https://github.com/pybind/pybind11/blob/master/docs/advanced/classes.rst
    py::class_<uint32_t>(m, "HailoSocketDefs", py::module_local())
        .def_static("MAX_UDP_PAYLOAD_SIZE", []() { return MAX_UDP_PAYLOAD_SIZE;} )
        .def_static("MIN_UDP_PAYLOAD_SIZE", []() { return MIN_UDP_PAYLOAD_SIZE;} )
        .def_static("MAX_UDP_PADDED_PAYLOAD_SIZE", []() { return MAX_UDP_PADDED_PAYLOAD_SIZE;} )
        .def_static("MIN_UDP_PADDED_PAYLOAD_SIZE", []() { return MIN_UDP_PAYLOAD_SIZE;} )
        .def_static("MAX_ALIGNED_UDP_PAYLOAD_SIZE_RTP", []() { return 1472;} )
        ;

    m.def("read_log", &read_log, py::return_value_policy::move);
    m.def("direct_write_memory", &direct_write_memory);
    m.def("direct_read_memory", &direct_read_memory);
    m.def("get_format_data_bytes", &HailoRTCommon::get_format_data_bytes);

    HEF_API_initialize_python_module(m);
    VStream_api_initialize_python_module(m);
    VDevice_api_initialize_python_module(m);
    #if defined(__GNUC__)
    TrafficControlUtilWrapper::add_to_python_module(m);
    #endif

#ifdef VERSION_INFO
    m.attr("__version__") = VERSION_INFO;
#else
    m.attr("__version__") = "dev";
#endif
}

} /* namespace hailort */
