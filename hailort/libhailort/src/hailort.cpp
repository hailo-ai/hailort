/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hailort.cpp
 * @brief HailoRT library.
 *
 * Hailo runtime (HailoRT) is a library for neural network inference on Hailo devices.
 **/


#include "hailo/hailort.h"
#include "hailo/hailort_common.hpp"
#include "hailo/hef.hpp"
#include "hailo/stream.hpp"
#include "hailo/device.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/transform.hpp"
#include "hailo/vstream.hpp"
#include "hailo/event.hpp"
#include "hailo/inference_pipeline.hpp"
#include "hailo/quantization.hpp"

#include "common/compiler_extensions_compat.hpp"
#include "common/os_utils.hpp"

#include "device_common/control.hpp"
#include "vdma/pcie/pcie_device.hpp"
#include "utils/sensor_config_utils.hpp"
#include "utils/hailort_logger.hpp"
#include "utils/shared_resource_manager.hpp"
#include "vdevice/vdevice_internal.hpp"
#include "utils/profiler/tracer_macros.hpp"
#include "utils/exported_resource_manager.hpp"
#include "utils/buffer_storage.hpp"

#include <chrono>
#include <tuple>


using namespace hailort;

// Note: Async stream API uses BufferPtr as a param. When exporting BufferPtrs to the user via c-api, they must be
//       stored in some container, otherwise their ref count may reach zero and they will be freed, despite the
//       c-api user still using them. (shared_ptr<T> doesn't have a release method like unique_ptr<T>)
// Singleton holding a mapping between the address of a buffer allocated/mapped via hailo_allocate_buffer
// to the underlying BufferPtr. When a buffer is freed via hailo_free_buffer, the BufferPtr object will be removed from
// the storage.
// TODO HRT-12726: remove the export manager
using ExportedBufferManager = ExportedResourceManager<BufferPtr, void *>;


// Stores state needed for c api for device.
struct _hailo_device {
    // Device can be either owned by this class (when created by hailo_created_vdevice), or not (when returned
    // from hailo_get_physical_devices, then it is owned by the vdevice).
    Device *device;

    // Store all the ConfiguredNetworkGroups created by the user. Need to store here since there is no way to release
    // them by api.
    std::vector<std::shared_ptr<ConfiguredNetworkGroup>> cngs;
};

// Stores state needed for c api for vdevice.
struct _hailo_vdevice {
    std::unique_ptr<VDevice> vdevice;

    // Store all physical devices to allow hailo_get_physical_devices to return pointer to them.
    std::vector<_hailo_device> physical_devices;

    // Store all the ConfiguredNetworkGroups created by the user. Need to store here since there is no way to release
    // them by api.
    std::vector<std::shared_ptr<ConfiguredNetworkGroup>> cngs;
};

COMPAT__INITIALIZER(hailort__initialize_logger)
{
    // Init logger singleton if compiling only HailoRT
    (void) HailoRTLogger::get_instance();
    (void) SharedResourceManager<std::string, VDeviceBase>::get_instance();
    TRACE(InitTrace);
}

hailo_status hailo_get_library_version(hailo_version_t *version)
{
    CHECK_ARG_NOT_NULL(version);
    version->major = HAILORT_MAJOR_VERSION;
    version->minor = HAILORT_MINOR_VERSION;
    version->revision = HAILORT_REVISION_VERSION;
    return HAILO_SUCCESS;
}

hailo_status hailo_identify(hailo_device device, hailo_device_identity_t *device_identity)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(device_identity);

    auto identity = device->device->identify();
    CHECK_EXPECTED_AS_STATUS(identity);
    *device_identity = identity.release();

    return HAILO_SUCCESS;
}

hailo_status hailo_core_identify(hailo_device device, hailo_core_information_t *core_information)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(core_information);

    auto identity = device->device->core_identify();
    CHECK_EXPECTED_AS_STATUS(identity);
    *core_information = identity.release();

    return HAILO_SUCCESS;
}

hailo_status hailo_get_extended_device_information(hailo_device device, hailo_extended_device_information_t *extended_device_information)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(extended_device_information);

    auto extended_device_info_expected = device->device->get_extended_device_information();
    CHECK_EXPECTED_AS_STATUS(extended_device_info_expected);
    *extended_device_information = extended_device_info_expected.release();

    return HAILO_SUCCESS;
}

hailo_status hailo_scan_devices(hailo_scan_devices_params_t *params, hailo_device_id_t *device_ids,
    size_t *device_ids_length)
{
    CHECK_ARG_NOT_NULL(device_ids);
    CHECK_ARG_NOT_NULL(device_ids_length);
    CHECK(params == nullptr, HAILO_INVALID_ARGUMENT, "Passing scan params is not allowed");

    auto device_ids_vector = Device::scan();
    CHECK_EXPECTED_AS_STATUS(device_ids_vector);

    if (device_ids_vector->size() > *device_ids_length) {
        LOGGER__ERROR("Too many devices detected. devices count: {}, scan_results_length: {}",
            device_ids_vector->size(), *device_ids_length);
        *device_ids_length = device_ids_vector->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }
    *device_ids_length = device_ids_vector->size();

    for (size_t i = 0; i < device_ids_vector->size(); i++) {
        auto device_id = HailoRTCommon::to_device_id(device_ids_vector.value()[i]);
        CHECK_EXPECTED_AS_STATUS(device_id);
        device_ids[i] = device_id.release();
    }

    return HAILO_SUCCESS;
}

hailo_status hailo_create_device_by_id(const hailo_device_id_t *device_id, hailo_device *device_out)
{
    CHECK_ARG_NOT_NULL(device_out);

    auto device = make_unique_nothrow<_hailo_device>();
    CHECK_NOT_NULL(device, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto device_obj, (device_id == nullptr) ? Device::create() : Device::create(device_id->id));
    device->device = device_obj.release();
    *device_out = device.release();

    return HAILO_SUCCESS;
}

// TODO: Fill pcie_device_infos_length items into pcie_device_infos, 
//       even if 'scan_results->size() > pcie_device_infos_length' (HRT-3163)
hailo_status hailo_scan_pcie_devices(
    hailo_pcie_device_info_t *pcie_device_infos, size_t pcie_device_infos_length, size_t *number_of_devices)
{
    CHECK_ARG_NOT_NULL(pcie_device_infos);
    CHECK_ARG_NOT_NULL(number_of_devices);

    auto scan_results = PcieDevice::scan();
    CHECK_EXPECTED_AS_STATUS(scan_results);

    CHECK(scan_results->size() <= pcie_device_infos_length, HAILO_INSUFFICIENT_BUFFER,
        "pcie_device_infos buffer not large enough (required: {}, buffer_size: {}))", scan_results->size(), pcie_device_infos_length);

    memcpy(pcie_device_infos, scan_results->data(), sizeof(*pcie_device_infos)*scan_results->size());
    *number_of_devices = scan_results->size();
    return HAILO_SUCCESS;
}

hailo_status hailo_parse_pcie_device_info(const char *device_info_str,
    hailo_pcie_device_info_t *device_info)
{
    CHECK_ARG_NOT_NULL(device_info_str);
    CHECK_ARG_NOT_NULL(device_info);

    auto local_device_info = Device::parse_pcie_device_info(std::string(device_info_str));
    CHECK_EXPECTED_AS_STATUS(local_device_info);

    *device_info = local_device_info.value();
    return HAILO_SUCCESS;
}

hailo_status hailo_create_pcie_device(hailo_pcie_device_info_t *device_info, hailo_device *device_out)
{
    CHECK_ARG_NOT_NULL(device_out);

    auto device = make_unique_nothrow<_hailo_device>();
    CHECK_NOT_NULL(device, HAILO_OUT_OF_HOST_MEMORY);

    TRY(auto device_obj, (device_info == nullptr) ? Device::create_pcie() : Device::create_pcie(*device_info));
    device->device = device_obj.release();
    *device_out = device.release();

    return HAILO_SUCCESS;
}

hailo_status hailo_release_device(hailo_device device_ptr)
{
    CHECK_ARG_NOT_NULL(device_ptr);
    Device *device = device_ptr->device;

    delete device_ptr;
    delete device;

    return HAILO_SUCCESS;
}

hailo_status hailo_device_get_type_by_device_id(const hailo_device_id_t *device_id,
    hailo_device_type_t *device_type)
{
    CHECK_ARG_NOT_NULL(device_id);
    const auto local_device_type = Device::get_device_type(device_id->id);
    CHECK_EXPECTED_AS_STATUS(local_device_type);

    switch (local_device_type.value()) {
    case Device::Type::PCIE:
        *device_type = HAILO_DEVICE_TYPE_PCIE;
        break;
    case Device::Type::ETH:
        *device_type = HAILO_DEVICE_TYPE_ETH;
        break;
    case Device::Type::INTEGRATED:
        *device_type = HAILO_DEVICE_TYPE_INTEGRATED;
        break;
    default:
        LOGGER__ERROR("Internal failure, invalid device type returned");
        return HAILO_INTERNAL_FAILURE;
    }

    return HAILO_SUCCESS;
}

hailo_status hailo_set_fw_logger(hailo_device device, hailo_fw_logger_level_t level, uint32_t interface_mask)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->set_fw_logger(level, interface_mask);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_set_throttling_state(hailo_device device, bool should_activate)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->set_throttling_state(should_activate);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_get_throttling_state(hailo_device device, bool *is_active)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(is_active);

    auto is_throttling_enabled_expected = device->device->get_throttling_state();
    CHECK_EXPECTED_AS_STATUS(is_throttling_enabled_expected);
    *is_active = is_throttling_enabled_expected.release();
    return HAILO_SUCCESS;
}

hailo_status hailo_wd_enable(hailo_device device, hailo_cpu_id_t cpu_id)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->wd_enable(cpu_id);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_wd_disable(hailo_device device, hailo_cpu_id_t cpu_id)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->wd_disable(cpu_id);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_wd_config(hailo_device device, hailo_cpu_id_t cpu_id, uint32_t wd_cycles, hailo_watchdog_mode_t wd_mode)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->wd_config(cpu_id, wd_cycles, wd_mode);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_get_previous_system_state(hailo_device device, hailo_cpu_id_t cpu_id, uint32_t *previous_system_state)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(previous_system_state);
    auto res = device->device->previous_system_state(cpu_id);
    CHECK_EXPECTED_AS_STATUS(res);
    *previous_system_state = res.release();
    return HAILO_SUCCESS;
}

hailo_status hailo_set_pause_frames(hailo_device device, bool rx_pause_frames_enable)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->set_pause_frames(rx_pause_frames_enable);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_get_device_id(hailo_device device, hailo_device_id_t *id)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(id);

    auto id_expected = HailoRTCommon::to_device_id(device->device->get_dev_id());
    CHECK_EXPECTED_AS_STATUS(id_expected);
    *id = id_expected.release();

    return HAILO_SUCCESS;
}

hailo_status hailo_get_chip_temperature(hailo_device device, hailo_chip_temperature_info_t *temp_info)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(temp_info);
    auto res = device->device->get_chip_temperature();
    CHECK_EXPECTED_AS_STATUS(res);
    *temp_info = res.release();
    return HAILO_SUCCESS;
}

hailo_status hailo_reset_device(hailo_device device, hailo_reset_device_mode_t mode)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->reset(mode);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_update_firmware(hailo_device device, void *firmware_buffer, uint32_t firmware_buffer_size)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(firmware_buffer);
    auto status = device->device->firmware_update(MemoryView(firmware_buffer, firmware_buffer_size), true);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_update_second_stage(hailo_device device, void *second_stage_buffer, uint32_t second_stage_buffer_size)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(second_stage_buffer);
    auto status = device->device->second_stage_update(reinterpret_cast<uint8_t*>(second_stage_buffer), second_stage_buffer_size);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_power_measurement(hailo_device device, hailo_dvm_options_t dvm,
    hailo_power_measurement_types_t measurement_type, float32_t *measurement)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(measurement);
    auto status = Control::power_measurement(*device->device, static_cast<CONTROL_PROTOCOL__dvm_options_t>(dvm), static_cast<CONTROL_PROTOCOL__power_measurement_types_t>(measurement_type), measurement);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_start_power_measurement(hailo_device device,
    hailo_averaging_factor_t averaging_factor, hailo_sampling_period_t sampling_period)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = Control::start_power_measurement(*device->device, static_cast<CONTROL_PROTOCOL__averaging_factor_t>(averaging_factor), static_cast<CONTROL_PROTOCOL__sampling_period_t>(sampling_period));
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_set_power_measurement(hailo_device device, hailo_measurement_buffer_index_t buffer_index,
    hailo_dvm_options_t dvm, hailo_power_measurement_types_t measurement_type)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = Control::set_power_measurement(*device->device, buffer_index, static_cast<CONTROL_PROTOCOL__dvm_options_t>(dvm), static_cast<CONTROL_PROTOCOL__power_measurement_types_t>(measurement_type));
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_get_power_measurement(hailo_device device, hailo_measurement_buffer_index_t buffer_index, bool should_clear,
    hailo_power_measurement_data_t *measurement_data)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(measurement_data);
    auto status = Control::get_power_measurement(*device->device, buffer_index, should_clear, measurement_data);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_stop_power_measurement(hailo_device device)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = Control::stop_power_measurement(*device->device);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_reset_sensor(hailo_device device, uint8_t section_index)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->sensor_reset(section_index);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_set_sensor_i2c_bus_index(hailo_device device, hailo_sensor_types_t sensor_type, uint8_t bus_index)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = Control::sensor_set_i2c_bus_index(*device->device, sensor_type, bus_index);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_load_and_start_sensor(hailo_device device, uint8_t section_index)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->sensor_load_and_start_config(section_index);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_i2c_read(hailo_device device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address, uint8_t *data, uint32_t length)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(slave_config);
    CHECK_ARG_NOT_NULL(data);
    auto status = Control::i2c_read(*device->device, slave_config, register_address, data, length);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_i2c_write(hailo_device device, const hailo_i2c_slave_config_t *slave_config, uint32_t register_address, const uint8_t *data, uint32_t length)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(slave_config);
    CHECK_ARG_NOT_NULL(data);
    auto status = Control::i2c_write(*device->device, slave_config, register_address, data, length);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_dump_sensor_config(hailo_device device, uint8_t section_index, const char *config_file_path)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(config_file_path);

    auto status = device->device->sensor_dump_config(section_index, config_file_path);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_store_sensor_config(hailo_device device, uint32_t section_index, hailo_sensor_types_t sensor_type,
    uint32_t reset_config_size, uint16_t config_height, uint16_t config_width, uint16_t config_fps,
    const char *config_file_path, const char *config_name)
{   
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(config_file_path);
    CHECK_ARG_NOT_NULL(config_name);
    
    auto status = device->device->store_sensor_config(section_index, sensor_type, reset_config_size, config_height, config_width,
        config_fps, config_file_path, config_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_store_isp_config(hailo_device device, uint32_t reset_config_size, uint16_t config_height, uint16_t config_width,
    uint16_t config_fps,  const char *isp_static_config_file_path, const char *isp_runtime_config_file_path, const char *config_name)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(isp_static_config_file_path);
    CHECK_ARG_NOT_NULL(isp_runtime_config_file_path);
    CHECK_ARG_NOT_NULL(config_name);
    
    auto status = device->device->store_isp_config(reset_config_size, config_height, config_width,
        config_fps, isp_static_config_file_path, isp_runtime_config_file_path, config_name);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_create_hef_file(hailo_hef *hef_out, const char *file_name)
{
    CHECK_ARG_NOT_NULL(hef_out);
    CHECK_ARG_NOT_NULL(file_name);

    auto hef = Hef::create(file_name);
    CHECK_EXPECTED_AS_STATUS(hef);

    auto allocated_hef = new (std::nothrow) Hef(hef.release());
    CHECK_NOT_NULL(allocated_hef, HAILO_OUT_OF_HOST_MEMORY);

    *hef_out = reinterpret_cast<hailo_hef>(allocated_hef);
    return HAILO_SUCCESS;
}

hailo_status hailo_create_hef_buffer(hailo_hef *hef_out, const void *buffer, size_t size)
{
    CHECK_ARG_NOT_NULL(hef_out);
    CHECK_ARG_NOT_NULL(buffer);

    auto hef = Hef::create(MemoryView::create_const(buffer, size));
    CHECK_EXPECTED_AS_STATUS(hef);

    auto allocated_hef = new (std::nothrow) Hef(hef.release());
    CHECK_NOT_NULL(allocated_hef, HAILO_OUT_OF_HOST_MEMORY);

    *hef_out = reinterpret_cast<hailo_hef>(allocated_hef);
    return HAILO_SUCCESS;
}

hailo_status hailo_release_hef(hailo_hef hef_ptr)
{
    CHECK_ARG_NOT_NULL(hef_ptr);

    auto hef = reinterpret_cast<Hef*>(hef_ptr);
    delete hef;
    return HAILO_SUCCESS;
}

static hailo_status hailo_init_network_params(hailo_hef hef, std::string net_group_name, 
    hailo_network_parameters_by_name_t *network_params, size_t &network_params_by_name_count)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(network_params);

    auto network_params_by_name = reinterpret_cast<Hef*>(hef)->create_network_parameters_by_name(net_group_name);
    CHECK_EXPECTED_AS_STATUS(network_params_by_name);
    CHECK(HAILO_MAX_NETWORKS_IN_NETWORK_GROUP >= network_params_by_name->size(), HAILO_INTERNAL_FAILURE,
        "Too many networks in network group {}", net_group_name);
    network_params_by_name_count = network_params_by_name->size();
    int network = 0;
    for (const auto &network_params_pair : network_params_by_name.release()) {
        CHECK(network_params_pair.first.length() < HAILO_MAX_NETWORK_NAME_SIZE, HAILO_INTERNAL_FAILURE,
            "network '{}' has a too long name (max is HAILO_MAX_NETWORK_NAME_SIZE)", network_params_pair.first);

        hailo_network_parameters_by_name_t params_by_name = {};
        strncpy(params_by_name.name, network_params_pair.first.c_str(), network_params_pair.first.length() + 1);
        params_by_name.network_params = network_params_pair.second;
        network_params[network] = params_by_name;
        network++;
    }

    return HAILO_SUCCESS;
}

hailo_status hailo_init_configure_params(hailo_hef hef, hailo_stream_interface_t stream_interface,
    hailo_configure_params_t *params)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(params);

    auto network_groups_names = reinterpret_cast<Hef*>(hef)->get_network_groups_names();
    CHECK(HAILO_MAX_NETWORK_GROUPS >= network_groups_names.size(), HAILO_INVALID_HEF,
        "Too many network_groups on a given HEF");

    params->network_group_params_count = network_groups_names.size();
    uint8_t net_group = 0;
    for (const auto &net_group_name : network_groups_names) {
        auto status = hailo_init_configure_network_group_params(hef, stream_interface, net_group_name.c_str(),
            &(params->network_group_params[net_group]));
        CHECK_SUCCESS(status);
        net_group++;
    }

    return HAILO_SUCCESS;
}

void fill_cfg_params_struct_by_class(const std::string &network_group_name, const ConfigureNetworkParams &class_in, hailo_configure_network_group_params_t *struct_out)
{
    strncpy(struct_out->name, network_group_name.c_str(), network_group_name.size() + 1);
    struct_out->batch_size = class_in.batch_size;
    struct_out->power_mode = class_in.power_mode;
    struct_out->latency = class_in.latency;

    int i = 0;
    for (auto & pair: class_in.network_params_by_name) {
        strncpy(struct_out->network_params_by_name[i].name, pair.first.c_str(), pair.first.length() + 1);
        struct_out->network_params_by_name[i].network_params = pair.second;
        i++;
    }
    struct_out->network_params_by_name_count = class_in.network_params_by_name.size();

    i = 0;
    for (auto & pair: class_in.stream_params_by_name) {
        strncpy(struct_out->stream_params_by_name[i].name, pair.first.c_str(), pair.first.length() + 1);
        struct_out->stream_params_by_name[i].stream_params = pair.second;
        i++;
    }
    struct_out->stream_params_by_name_count = class_in.stream_params_by_name.size();
}

hailo_status hailo_init_configure_params_by_vdevice(hailo_hef hef, hailo_vdevice vdevice,
    hailo_configure_params_t *params)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(vdevice);
    CHECK_ARG_NOT_NULL(params);

    auto configure_params = vdevice->vdevice->create_configure_params(*reinterpret_cast<Hef*>(hef));
    CHECK_EXPECTED_AS_STATUS(configure_params);

    params->network_group_params_count = configure_params->size();
    uint8_t net_group = 0;
    for (auto &cfg_params : configure_params.value()) {
        fill_cfg_params_struct_by_class(cfg_params.first, cfg_params.second, &(params->network_group_params[net_group]));
        net_group++;
    }

    return HAILO_SUCCESS;
}

hailo_status hailo_init_configure_params_by_device(hailo_hef hef, hailo_device device,
    hailo_configure_params_t *params)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(params);

    auto configure_params = device->device->create_configure_params(*reinterpret_cast<Hef*>(hef));
    CHECK_EXPECTED_AS_STATUS(configure_params);

    params->network_group_params_count = configure_params->size();
    uint8_t net_group = 0;
    for (auto &cfg_params : configure_params.value()) {
        fill_cfg_params_struct_by_class(cfg_params.first, cfg_params.second, &(params->network_group_params[net_group]));
        net_group++;
    }

    return HAILO_SUCCESS;
}

hailo_status fill_configured_network_params_with_default(hailo_hef hef, const char *network_group_name, hailo_configure_network_group_params_t &params)
{
    std::string network_group_name_str; 
    if (nullptr == network_group_name) {
        auto network_groups_names = reinterpret_cast<Hef*>(hef)->get_network_groups_names();
        CHECK(HAILO_MAX_NETWORK_GROUPS >= network_groups_names.size(), HAILO_INVALID_HEF,
            "Too many network_groups on a given HEF");
        network_group_name_str = network_groups_names[0];

        LOGGER__INFO("No network_group name was given. Addressing default network_group: {}",
            network_group_name_str);
    } else {
        network_group_name_str = std::string(network_group_name);
    }

    CHECK(HAILO_MAX_NETWORK_GROUP_NAME_SIZE > network_group_name_str.length(), HAILO_INTERNAL_FAILURE,
        "Network group '{}' name is too long (max is HAILO_MAX_NETWORK_GROUP_NAME_SIZE, including NULL terminator)",
        network_group_name_str);
    strncpy(params.name, network_group_name_str.c_str(), network_group_name_str.length() + 1);

    auto config_params =  HailoRTDefaults::get_configure_params();
    params.batch_size = config_params.batch_size;
    params.power_mode = config_params.power_mode;

    return HAILO_SUCCESS;
}

hailo_status fill_configured_network_params_with_stream_params(const std::map<std::string, hailo_stream_parameters_t> &stream_params_by_name,
    hailo_configure_network_group_params_t &params)
{
    CHECK(HAILO_MAX_STREAMS_COUNT >= stream_params_by_name.size(), HAILO_INTERNAL_FAILURE,
        "Too many streams in HEF. found {} streams.", stream_params_by_name.size());
    params.stream_params_by_name_count = stream_params_by_name.size();

    uint8_t stream = 0;
    for (const auto &stream_params_pair : stream_params_by_name) {
        CHECK(stream_params_pair.first.length() < HAILO_MAX_STREAM_NAME_SIZE, HAILO_INTERNAL_FAILURE,
            "Stream '{}' has a too long name (max is HAILO_MAX_STREAM_NAME_SIZE)", stream_params_pair.first);

        hailo_stream_parameters_by_name_t params_by_name = {};
        strncpy(params_by_name.name, stream_params_pair.first.c_str(), stream_params_pair.first.length() + 1);
        params_by_name.stream_params = stream_params_pair.second;
        params.stream_params_by_name[stream] = params_by_name;
        stream++;
    }

    return HAILO_SUCCESS;
}

hailo_status hailo_init_configure_network_group_params(hailo_hef hef, hailo_stream_interface_t stream_interface,
    const char *network_group_name, hailo_configure_network_group_params_t *params)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(params);

    auto status = fill_configured_network_params_with_default(hef, network_group_name, *params);
    CHECK_SUCCESS(status);

    auto stream_params_by_name = reinterpret_cast<Hef*>(hef)->create_stream_parameters_by_name(network_group_name, stream_interface);
    CHECK_EXPECTED_AS_STATUS(stream_params_by_name);

    status = fill_configured_network_params_with_stream_params(stream_params_by_name.value(), *params);
    CHECK_SUCCESS(status);

    /* Fill network params */
    status = hailo_init_network_params(hef, network_group_name, 
        params->network_params_by_name, 
        params->network_params_by_name_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

NetworkGroupsParamsMap get_configure_params_map(hailo_configure_params_t *params)
{
    NetworkGroupsParamsMap configure_params;
    if (nullptr != params) {
        for (size_t i = 0; i < params->network_group_params_count; i++) {
            configure_params.emplace(std::string(params->network_group_params[i].name),
                ConfigureNetworkParams(params->network_group_params[i]));
        }
    }
    return configure_params;
}

hailo_status hailo_configure_device(hailo_device device, hailo_hef hef, hailo_configure_params_t *params,
    hailo_configured_network_group *network_groups, size_t *number_of_network_groups)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(network_groups);
    CHECK_ARG_NOT_NULL(number_of_network_groups);

    auto configure_params = get_configure_params_map(params);

    TRY(auto added_net_groups, device->device->configure(*reinterpret_cast<Hef*>(hef), configure_params));

    CHECK(added_net_groups.size() <= (*number_of_network_groups), HAILO_INSUFFICIENT_BUFFER,
        "Can't return all network_groups. HEF file contained {} network_groups, but output array is of size {}",
        added_net_groups.size(), (*number_of_network_groups));

    for (size_t i = 0; i < added_net_groups.size(); ++i) {
        network_groups[i] = reinterpret_cast<hailo_configured_network_group>(added_net_groups[i].get());
    }

    // Since the C API doesn't let the user to hold the cng, we need to keep it alive in the vdevice scope
    device->cngs.insert(device->cngs.end(), added_net_groups.begin(), added_net_groups.end());

    *number_of_network_groups = added_net_groups.size();
    return HAILO_SUCCESS;
}

hailo_status hailo_get_network_groups_infos(hailo_hef hef, hailo_network_group_info_t *infos,
    size_t *number_of_infos)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(infos);
    CHECK_ARG_NOT_NULL(number_of_infos);

    auto network_groups_infos = reinterpret_cast<Hef*>(hef)->get_network_groups_infos();
    CHECK_EXPECTED_AS_STATUS(network_groups_infos);
    if(*number_of_infos < network_groups_infos->size()) {
        LOGGER__ERROR(
            "The given array is to small to contain all network_groups infos. there are {} network_groups in the given HEF.",
            network_groups_infos->size());
        *number_of_infos = network_groups_infos->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }

    std::copy(network_groups_infos->begin(), network_groups_infos->end(), infos);
    *number_of_infos = network_groups_infos->size();

    return HAILO_SUCCESS;
}

static hailo_status convert_stream_infos_vector_to_array(std::vector<hailo_stream_info_t> &&stream_infos_vec, 
    hailo_stream_info_t *stream_infos, size_t *number_of_streams,
    bool include_input, bool include_output)
{
    stream_infos_vec.erase(std::remove_if(stream_infos_vec.begin(), stream_infos_vec.end(),
        [include_input, include_output](const hailo_stream_info_t &stream_info) {
            return (!include_input && (HAILO_H2D_STREAM == stream_info.direction)) ||
                   (!include_output && (HAILO_D2H_STREAM == stream_info.direction));
        }),
        stream_infos_vec.end());

    auto available_streams = *number_of_streams;
    *number_of_streams = stream_infos_vec.size();

    CHECK(stream_infos_vec.size() <= available_streams, HAILO_INSUFFICIENT_BUFFER,
          "The given buffer is too small to contain all stream infos. there are {} streams in the given hef, given buffer size is {}",
          stream_infos_vec.size(), available_streams);

    std::copy(stream_infos_vec.begin(), stream_infos_vec.end(), stream_infos);

    return HAILO_SUCCESS;
}

hailo_status hailo_network_group_get_all_stream_infos(hailo_configured_network_group network_group,
    hailo_stream_info_t *stream_infos, size_t stream_infos_length, size_t *number_of_streams)
{
    CHECK_ARG_NOT_NULL(network_group);
    CHECK_ARG_NOT_NULL(stream_infos);
    CHECK_ARG_NOT_NULL(number_of_streams);

    auto stream_infos_vec = (reinterpret_cast<ConfiguredNetworkGroup*>(network_group))->get_all_stream_infos();
    CHECK_EXPECTED_AS_STATUS(stream_infos_vec);
    auto detected_streams = stream_infos_length;
    auto status = convert_stream_infos_vector_to_array(stream_infos_vec.release(), stream_infos, &detected_streams,
        true, true);
    /* Update detected number of stream even in case of error */
    (*number_of_streams) = detected_streams;
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status hailo_network_group_get_input_stream_infos(hailo_configured_network_group network_group,
    hailo_stream_info_t *stream_infos, size_t stream_infos_length, size_t *number_of_streams)
{
    CHECK_ARG_NOT_NULL(network_group);
    CHECK_ARG_NOT_NULL(stream_infos);
    CHECK_ARG_NOT_NULL(number_of_streams);

    auto stream_infos_vec = (reinterpret_cast<ConfiguredNetworkGroup*>(network_group))->get_all_stream_infos();
    CHECK_EXPECTED_AS_STATUS(stream_infos_vec);
    auto detected_streams = stream_infos_length;
    auto status = convert_stream_infos_vector_to_array(stream_infos_vec.release(), stream_infos, &detected_streams,
        true, false);
    CHECK_SUCCESS(status);
    
    (*number_of_streams) = detected_streams;

    return HAILO_SUCCESS;
}

hailo_status hailo_network_group_get_output_stream_infos(hailo_configured_network_group network_group,
    hailo_stream_info_t *stream_infos, size_t stream_infos_length, size_t *number_of_streams)
{
    CHECK_ARG_NOT_NULL(network_group);
    CHECK_ARG_NOT_NULL(stream_infos);
    CHECK_ARG_NOT_NULL(number_of_streams);

    auto stream_infos_vec = (reinterpret_cast<ConfiguredNetworkGroup*>(network_group))->get_all_stream_infos();
    CHECK_EXPECTED_AS_STATUS(stream_infos_vec);
    auto detected_streams = stream_infos_length;
    auto status = convert_stream_infos_vector_to_array(stream_infos_vec.release(), stream_infos, &detected_streams,
        false, true);
    CHECK_SUCCESS(status);

    (*number_of_streams) = detected_streams;
    
    return HAILO_SUCCESS;
}

hailo_status hailo_activate_network_group(hailo_configured_network_group network_group,
    hailo_activate_network_group_params_t *activation_params,
    hailo_activated_network_group *activated_network_group_out)
{
    CHECK_ARG_NOT_NULL(network_group);
    CHECK_ARG_NOT_NULL(activated_network_group_out);

    hailo_activate_network_group_params_t actual_activation_params = (activation_params != nullptr) ?
        *activation_params :
        HailoRTDefaults::get_active_network_group_params();

    auto net_group_ptr = reinterpret_cast<ConfiguredNetworkGroup*>(network_group);
    auto activated_net_group = net_group_ptr->activate(actual_activation_params);
    CHECK_EXPECTED_AS_STATUS(activated_net_group);

    *activated_network_group_out = reinterpret_cast<hailo_activated_network_group>(activated_net_group.release().release());

    return HAILO_SUCCESS;
}

hailo_status hailo_get_input_stream(hailo_configured_network_group configured_network_group, const char *name, 
    hailo_input_stream *stream_out)
{
    CHECK_ARG_NOT_NULL(configured_network_group);
    CHECK_ARG_NOT_NULL(name);
    CHECK_ARG_NOT_NULL(stream_out);

    auto name_string = std::string(name);
    auto stream = (reinterpret_cast<ConfiguredNetworkGroup *>(configured_network_group))->get_input_stream_by_name(name_string);
    CHECK_EXPECTED_AS_STATUS(stream);

    *stream_out = reinterpret_cast<hailo_input_stream>(&stream.value().get());

    return HAILO_SUCCESS;
}

hailo_status hailo_get_output_stream(hailo_configured_network_group configured_network_group, const char *name, 
    hailo_output_stream *stream_out)
{
    CHECK_ARG_NOT_NULL(configured_network_group);
    CHECK_ARG_NOT_NULL(name);
    CHECK_ARG_NOT_NULL(stream_out);

    auto name_string = std::string(name);
    auto stream = (reinterpret_cast<ConfiguredNetworkGroup *>(configured_network_group))->get_output_stream_by_name(name_string);
    CHECK_EXPECTED_AS_STATUS(stream);

    *stream_out = reinterpret_cast<hailo_output_stream>(&stream.value().get());

    return HAILO_SUCCESS;
}

const std::string get_name_as_str(const char *name)
{
    return (nullptr == name) ? "" : std::string(name);
}

hailo_status hailo_hef_get_all_stream_infos(hailo_hef hef, const char *name,
    hailo_stream_info_t *stream_infos, size_t stream_infos_length, size_t *number_of_streams)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(stream_infos);
    CHECK_ARG_NOT_NULL(number_of_streams);
    const auto name_str = get_name_as_str(name);

    auto stream_infos_expected = (reinterpret_cast<Hef*>(hef))->get_all_stream_infos(name_str);
    CHECK_EXPECTED_AS_STATUS(stream_infos_expected);

    auto detected_streams = stream_infos_length;
    auto status = convert_stream_infos_vector_to_array(stream_infos_expected.release(), stream_infos, &detected_streams, 
        true, true);
    CHECK_SUCCESS(status);

    (*number_of_streams) = detected_streams;

    return HAILO_SUCCESS;
}

hailo_status hailo_hef_get_stream_info_by_name(hailo_hef hef, const char *network_group_name, const char *stream_name,
    hailo_stream_direction_t stream_direction, hailo_stream_info_t *stream_info)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(stream_name);
    CHECK_ARG_NOT_NULL(stream_info);
    const auto name_str = get_name_as_str(network_group_name);

    auto stream_info_expected = (reinterpret_cast<Hef*>(hef))->get_stream_info_by_name(stream_name,
        stream_direction, name_str);
    CHECK_EXPECTED_AS_STATUS(stream_info_expected);

    *stream_info = stream_info_expected.release();

    return HAILO_SUCCESS;
}

static hailo_status convert_vstream_infos_vector_to_array(std::vector<hailo_vstream_info_t> vstream_infos_vec, 
    hailo_vstream_info_t *vstream_infos, size_t *vstream_infos_count)
{
    size_t vstream_infos_array_entries = *vstream_infos_count;
    *vstream_infos_count = vstream_infos_vec.size();

    CHECK(vstream_infos_vec.size() <= vstream_infos_array_entries, HAILO_INSUFFICIENT_BUFFER,
          "The given buffer is too small to contain all vstream infos. there are {} vstreams in the given hef, given buffer size is {}",
          vstream_infos_vec.size(), vstream_infos_array_entries);

    std::copy(vstream_infos_vec.begin(), vstream_infos_vec.end(), vstream_infos);

    return HAILO_SUCCESS;
}

hailo_status hailo_hef_get_all_vstream_infos(hailo_hef hef, const char *name,
    hailo_vstream_info_t *vstream_infos, size_t *vstream_infos_count)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(vstream_infos);
    CHECK_ARG_NOT_NULL(vstream_infos_count);
    const auto name_str = get_name_as_str(name);

    auto vstream_infos_expected = (reinterpret_cast<Hef*>(hef))->get_all_vstream_infos(name_str);
    CHECK_EXPECTED_AS_STATUS(vstream_infos_expected);

    auto status = convert_vstream_infos_vector_to_array(vstream_infos_expected.release(), vstream_infos, vstream_infos_count);
    CHECK_SUCCESS(status);
    
    return HAILO_SUCCESS;
}

HAILORTAPI hailo_status hailo_get_latency_measurement(hailo_configured_network_group configured_network_group,
    const char *network_name, hailo_latency_measurement_result_t *result)
{
    CHECK_ARG_NOT_NULL(configured_network_group);
    CHECK_ARG_NOT_NULL(result);

    std::string network_name_str = (nullptr == network_name) ? "" : network_name;

    auto latency_result = ((ConfiguredNetworkGroup*)configured_network_group)->get_latency_measurement(network_name_str);
    CHECK_EXPECTED_AS_STATUS(latency_result);

    hailo_latency_measurement_result_t local_result {};
    local_result.avg_hw_latency_ms = std::chrono::duration<double, std::milli>(latency_result->avg_hw_latency).count();

    *result = local_result;
    return HAILO_SUCCESS;
}

hailo_status hailo_set_scheduler_timeout(hailo_configured_network_group configured_network_group,
    uint32_t timeout_ms, const char *network_name)
{
    CHECK_ARG_NOT_NULL(configured_network_group);

    std::string network_name_str = (nullptr == network_name) ? "" : network_name;
    return (reinterpret_cast<ConfiguredNetworkGroup*>(configured_network_group))->set_scheduler_timeout(std::chrono::milliseconds(timeout_ms), network_name_str);
}

hailo_status hailo_set_scheduler_threshold(hailo_configured_network_group configured_network_group,
    uint32_t threshold, const char *network_name)
{
    CHECK_ARG_NOT_NULL(configured_network_group);

    std::string network_name_str = (nullptr == network_name) ? "" : network_name;
    return (reinterpret_cast<ConfiguredNetworkGroup*>(configured_network_group))->set_scheduler_threshold(threshold, network_name_str);
}

hailo_status hailo_set_scheduler_priority(hailo_configured_network_group configured_network_group, uint8_t priority, const char *network_name)
{
    CHECK_ARG_NOT_NULL(configured_network_group);

    std::string network_name_str = (nullptr == network_name) ? "" : network_name;
    return (reinterpret_cast<ConfiguredNetworkGroup*>(configured_network_group))->set_scheduler_priority(priority, network_name_str);
}

hailo_status hailo_allocate_buffer(size_t size, const hailo_buffer_parameters_t *allocation_params, void **buffer_out)
{
    CHECK_ARG_NOT_NULL(allocation_params);
    CHECK_ARG_NOT_NULL(buffer_out);
    CHECK(0 != size, HAILO_INVALID_ARGUMENT, "Buffer size must be greater than zero");

    BufferStorageParams buffer_storage_params{};
    buffer_storage_params.flags = allocation_params->flags;

    // Create buffer
    auto buffer = Buffer::create_shared(size, buffer_storage_params);
    CHECK_EXPECTED_AS_STATUS(buffer);

    // Store the buffer in manager (otherwise it'll be freed at the end of this func)
    const auto status = ExportedBufferManager::register_resource(*buffer, buffer->get()->data());
    CHECK_SUCCESS(status);

    *buffer_out = buffer->get()->data();

    return HAILO_SUCCESS;
}

hailo_status hailo_free_buffer(void *buffer)
{
    CHECK_ARG_NOT_NULL(buffer);
    return ExportedBufferManager::unregister_resource(buffer);
}

// TODO: hailo_device_dma_map_buffer/hailo_device_dma_unmap_buffer aren't thread safe when crossed with
//       hailo_allocate_buffer/hailo_free_buffer (HRT-10669)
hailo_status hailo_device_dma_map_buffer(hailo_device device, void *address, size_t size, hailo_dma_buffer_direction_t direction)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(address);
    return device->device->dma_map(address, size, direction);
}

hailo_status hailo_device_dma_unmap_buffer(hailo_device device, void *address, size_t size, hailo_dma_buffer_direction_t direction)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(address);
    return device->device->dma_unmap(address, size, direction);
}

hailo_status hailo_vdevice_dma_map_buffer(hailo_vdevice vdevice, void *address, size_t size, hailo_dma_buffer_direction_t direction)
{
    CHECK_ARG_NOT_NULL(vdevice);
    CHECK_ARG_NOT_NULL(address);
    return vdevice->vdevice->dma_map(address, size, direction);
}

hailo_status hailo_vdevice_dma_unmap_buffer(hailo_vdevice vdevice, void *address, size_t size, hailo_dma_buffer_direction_t direction)
{
    CHECK_ARG_NOT_NULL(vdevice);
    CHECK_ARG_NOT_NULL(address);
    return vdevice->vdevice->dma_unmap(address, size, direction);
}

hailo_status hailo_device_dma_map_dmabuf(hailo_device device, int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction)
{
    CHECK_ARG_NOT_NULL(device);
    return device->device->dma_map_dmabuf(dmabuf_fd, size, direction);
}

hailo_status hailo_device_dma_unmap_dmabuf(hailo_device device, int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction)
{
    CHECK_ARG_NOT_NULL(device);
    return device->device->dma_unmap_dmabuf(dmabuf_fd, size, direction);
}

hailo_status hailo_vdevice_dma_map_dmabuf(hailo_vdevice vdevice, int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction)
{
    CHECK_ARG_NOT_NULL(vdevice);
    return vdevice->vdevice->dma_map_dmabuf(dmabuf_fd, size, direction);
}

hailo_status hailo_vdevice_dma_unmap_dmabuf(hailo_vdevice vdevice, int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction)
{
    CHECK_ARG_NOT_NULL(vdevice);
    return vdevice->vdevice->dma_unmap_dmabuf(dmabuf_fd, size, direction);
}

hailo_status hailo_set_input_stream_timeout(hailo_input_stream stream, uint32_t timeout_ms)
{
    CHECK_ARG_NOT_NULL(stream);

    auto status = (reinterpret_cast<InputStream*>(stream))->set_timeout(std::chrono::milliseconds(timeout_ms));
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_set_output_stream_timeout(hailo_output_stream stream, uint32_t timeout_ms)
{
    CHECK_ARG_NOT_NULL(stream);

    auto status = (reinterpret_cast<OutputStream*>(stream))->set_timeout(std::chrono::milliseconds(timeout_ms));
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

size_t hailo_get_input_stream_frame_size(hailo_input_stream stream)
{
    return (reinterpret_cast<InputStream*>(stream))->get_frame_size();
}

size_t hailo_get_output_stream_frame_size(hailo_output_stream stream)
{
    return (reinterpret_cast<OutputStream*>(stream))->get_frame_size();
}

hailo_status hailo_get_input_stream_info(hailo_input_stream stream, hailo_stream_info_t *stream_info)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(stream_info);
    *stream_info = reinterpret_cast<InputStream*>(stream)->get_info();
    return HAILO_SUCCESS;
}

hailo_status hailo_get_output_stream_info(hailo_output_stream stream, hailo_stream_info_t *stream_info)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(stream_info);
    *stream_info = reinterpret_cast<OutputStream*>(stream)->get_info();
    return HAILO_SUCCESS;
}

hailo_status hailo_stream_read_raw_buffer(hailo_output_stream stream, void *buffer, size_t size)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(buffer);

    MemoryView buffer_view(buffer, size);
    auto status = (reinterpret_cast<OutputStream*>(stream))->read(buffer_view);
    if (HAILO_STREAM_ABORT == status) {
        return status;
    }
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_stream_write_raw_buffer(hailo_input_stream stream, const void *buffer, size_t size)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(buffer);

    auto status = (reinterpret_cast<InputStream*>(stream))->write(MemoryView::create_const(buffer, size));
    if (HAILO_STREAM_ABORT == status) {
        return status;
    }
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_stream_wait_for_async_output_ready(hailo_output_stream stream, size_t transfer_size, uint32_t timeout_ms)
{
    CHECK_ARG_NOT_NULL(stream);
    return (reinterpret_cast<OutputStream*>(stream))->wait_for_async_ready(transfer_size, std::chrono::milliseconds(timeout_ms));
}

hailo_status hailo_stream_wait_for_async_input_ready(hailo_input_stream stream, size_t transfer_size, uint32_t timeout_ms)
{
    CHECK_ARG_NOT_NULL(stream);
    return (reinterpret_cast<InputStream*>(stream))->wait_for_async_ready(transfer_size, std::chrono::milliseconds(timeout_ms));
}

hailo_status hailo_output_stream_get_async_max_queue_size(hailo_output_stream stream, size_t *queue_size)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(queue_size);

    auto local_queue_size = reinterpret_cast<OutputStream*>(stream)->get_async_max_queue_size();
    CHECK_EXPECTED_AS_STATUS(local_queue_size);
    *queue_size = local_queue_size.release();

    return HAILO_SUCCESS;
}

hailo_status hailo_input_stream_get_async_max_queue_size(hailo_input_stream stream, size_t *queue_size)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(queue_size);

    auto local_queue_size = reinterpret_cast<InputStream*>(stream)->get_async_max_queue_size();
    CHECK_EXPECTED_AS_STATUS(local_queue_size);
    *queue_size = local_queue_size.release();

    return HAILO_SUCCESS;
}

static InputStream::TransferDoneCallback wrap_c_user_callback(hailo_stream_write_async_callback_t callback, void *opaque)
{
    return [callback, opaque](const InputStream::CompletionInfo &completion_info) {
        hailo_stream_write_async_completion_info_t c_completion_info{};
        c_completion_info.status = completion_info.status;
        c_completion_info.buffer_addr = completion_info.buffer_addr;
        c_completion_info.buffer_size = completion_info.buffer_size;
        c_completion_info.opaque = opaque;
        callback(&c_completion_info);
    };
}

static OutputStream::TransferDoneCallback wrap_c_user_callback(hailo_stream_read_async_callback_t callback, void *opaque)
{
    return [callback, opaque](const OutputStream::CompletionInfo &completion_info) {
        hailo_stream_read_async_completion_info_t c_completion_info{};
        c_completion_info.status = completion_info.status;
        c_completion_info.buffer_addr = completion_info.buffer_addr;
        c_completion_info.buffer_size = completion_info.buffer_size;
        c_completion_info.opaque = opaque;
        callback(&c_completion_info);
    };
}

hailo_status hailo_stream_read_raw_buffer_async(hailo_output_stream stream, void *buffer, size_t size,
    hailo_stream_read_async_callback_t callback, void *opaque)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(buffer);
    CHECK_ARG_NOT_NULL(callback);

    return (reinterpret_cast<OutputStream*>(stream))->read_async(buffer, size,
        wrap_c_user_callback(callback, opaque));
}

hailo_status hailo_stream_write_raw_buffer_async(hailo_input_stream stream, const void *buffer, size_t size,
    hailo_stream_write_async_callback_t callback, void *opaque)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(buffer);
    CHECK_ARG_NOT_NULL(callback);

    return (reinterpret_cast<InputStream*>(stream))->write_async(buffer, size,
        wrap_c_user_callback(callback, opaque));
}

hailo_status hailo_fuse_nms_frames(const hailo_nms_fuse_input_t *nms_fuse_inputs, uint32_t inputs_count,
    uint8_t *fused_buffer, size_t fused_buffer_size)
{
    CHECK_ARG_NOT_NULL(nms_fuse_inputs);
    CHECK_ARG_NOT_NULL(fused_buffer);

    std::vector<MemoryView> mem_views;
    mem_views.reserve(inputs_count);
    for (uint32_t i = 0; i < inputs_count; i++) {
        mem_views.emplace_back(nms_fuse_inputs[i].buffer, nms_fuse_inputs[i].size);
    }

    std::vector<hailo_nms_info_t> params_of_buffers;
    params_of_buffers.reserve(inputs_count);
    for (uint32_t i = 0; i < inputs_count; i++) {
        params_of_buffers.push_back(nms_fuse_inputs[i].nms_info);
    }

    // TODO: check if passing vectors is hurting performance
    auto status = fuse_buffers(mem_views, params_of_buffers, MemoryView(fused_buffer, fused_buffer_size));
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

size_t hailo_get_host_frame_size(const hailo_stream_info_t *stream_info, const hailo_transform_params_t *transform_params)
{
    return HailoRTCommon::get_frame_size(*stream_info, *transform_params);
}

hailo_status hailo_deactivate_network_group(hailo_activated_network_group activated_network_group)
{
    CHECK_ARG_NOT_NULL(activated_network_group);

    auto net_group_casted = reinterpret_cast<ActivatedNetworkGroup*>(activated_network_group);
    delete net_group_casted;

    return HAILO_SUCCESS;
}

hailo_status hailo_shutdown_network_group(hailo_configured_network_group network_group)
{
    CHECK_ARG_NOT_NULL(network_group);
    return reinterpret_cast<ConfiguredNetworkGroup *>(network_group)->shutdown();
}

hailo_status hailo_set_notification_callback(hailo_device device, hailo_notification_callback callback,
    hailo_notification_id_t notification_id, void *opaque)
{
    CHECK_ARG_NOT_NULL(device);
    CHECK_ARG_NOT_NULL(callback);

    auto status = device->device->set_notification_callback(
        [callback, device] (Device &, const hailo_notification_t &notification, void* opaque) {
            callback(device, &notification, opaque);
        },
        notification_id, opaque);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_remove_notification_callback(hailo_device device, hailo_notification_id_t notification_id)
{
    CHECK_ARG_NOT_NULL(device);

    auto status = device->device->remove_notification_callback(notification_id);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_test_chip_memories(hailo_device device)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = Control::test_chip_memories(*device->device);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_set_sleep_state(hailo_device device, hailo_sleep_state_t sleep_state)
{
    CHECK_ARG_NOT_NULL(device);
    auto status = device->device->set_sleep_state(sleep_state);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_create_input_transform_context(const hailo_stream_info_t *stream_info,
    const hailo_transform_params_t *transform_params, hailo_input_transform_context *transform_context)
{
    CHECK_ARG_NOT_NULL(stream_info);
    CHECK_ARG_NOT_NULL(transform_params);
    CHECK_ARG_NOT_NULL(transform_context);

    if (!Quantization::is_qp_valid(stream_info->quant_info)) {
        LOGGER__ERROR("quant_info of stream_info is invalid as the model was compiled with multiple quant_infos. "
                    "Please compile again or call hailo_create_input_transform_context_by_stream instead");
        return HAILO_INVALID_ARGUMENT;
    }

    auto local_transform_context = InputTransformContext::create(*stream_info, *transform_params);
    CHECK_EXPECTED_AS_STATUS(local_transform_context);

    *transform_context = reinterpret_cast<hailo_input_transform_context>(local_transform_context.release().release());
    return HAILO_SUCCESS;
}

hailo_status hailo_create_input_transform_context_by_stream(hailo_input_stream stream,
    const hailo_transform_params_t *transform_params, hailo_input_transform_context *transform_context)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(transform_params);
    CHECK_ARG_NOT_NULL(transform_context);

    InputStream *input_stream = reinterpret_cast<InputStream*>(stream);
    auto local_transform_context = InputTransformContext::create(*input_stream, *transform_params);
    CHECK_EXPECTED_AS_STATUS(local_transform_context);

    *transform_context = reinterpret_cast<hailo_input_transform_context>(local_transform_context.release().release());
    return HAILO_SUCCESS;
}

hailo_status hailo_release_input_transform_context(hailo_input_transform_context transform_context)
{
    CHECK_ARG_NOT_NULL(transform_context);
    delete reinterpret_cast<InputTransformContext*>(transform_context);
    return HAILO_SUCCESS;
}

hailo_status hailo_is_input_transformation_required(const hailo_3d_image_shape_t *src_image_shape, const hailo_format_t *src_format,
    const hailo_3d_image_shape_t *dst_image_shape, const hailo_format_t *dst_format, const hailo_quant_info_t *quant_info,
    bool *transformation_required)
{
    LOGGER__WARNING("Using a deprecated function. Use hailo_is_input_transformation_required2 instead");
    CHECK_ARG_NOT_NULL(src_image_shape);
    CHECK_ARG_NOT_NULL(src_format);
    CHECK_ARG_NOT_NULL(dst_image_shape);
    CHECK_ARG_NOT_NULL(dst_format);
    CHECK_ARG_NOT_NULL(quant_info);
    CHECK_ARG_NOT_NULL(transformation_required);

    auto exp = InputTransformContext::is_transformation_required(*src_image_shape, *src_format, *dst_image_shape, *dst_format,
        std::vector<hailo_quant_info_t>{*quant_info}); // TODO: Get quant vector (HRT-11052)
    CHECK_EXPECTED_AS_STATUS(exp);
    *transformation_required  = exp.value();

    return HAILO_SUCCESS;
}

hailo_status hailo_is_input_transformation_required2(const hailo_3d_image_shape_t *src_image_shape, const hailo_format_t *src_format,
    const hailo_3d_image_shape_t *dst_image_shape, const hailo_format_t *dst_format, const hailo_quant_info_t *quant_infos, 
    size_t quant_infos_count, bool *transformation_required)
{
    CHECK_ARG_NOT_NULL(src_image_shape);
    CHECK_ARG_NOT_NULL(src_format);
    CHECK_ARG_NOT_NULL(dst_image_shape);
    CHECK_ARG_NOT_NULL(dst_format);
    CHECK_ARG_NOT_NULL(quant_infos);
    CHECK_ARG_NOT_NULL(transformation_required);

    std::vector<hailo_quant_info_t> quant_info_vector;
    const hailo_quant_info_t* ptr = quant_infos;
    size_t count = quant_infos_count;
    for (size_t i = 0; i < count; ++i) {
        const hailo_quant_info_t& quant_info = *(ptr + i);
        quant_info_vector.push_back(quant_info);
    }
    auto exp = InputTransformContext::is_transformation_required(*src_image_shape, *src_format, *dst_image_shape, *dst_format, quant_info_vector);
    CHECK_EXPECTED_AS_STATUS(exp);
    *transformation_required  = exp.value();

    return HAILO_SUCCESS;
}

hailo_status hailo_transform_frame_by_input_transform_context(hailo_input_transform_context transform_context,
    const void *src, size_t src_size, void *dst, size_t dst_size)
{
    CHECK_ARG_NOT_NULL(transform_context);
    CHECK_ARG_NOT_NULL(src);
    CHECK_ARG_NOT_NULL(dst);

    MemoryView dst_buffer(dst, dst_size);
    auto status = reinterpret_cast<InputTransformContext*>(transform_context)->transform(
        MemoryView::create_const(src, src_size), dst_buffer);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

static hailo_status convert_quant_infos_vector_to_array(std::vector<hailo_quant_info_t> quant_infos_vec, 
    hailo_quant_info_t *quant_infos, size_t *quant_infos_count)
{
    size_t quant_infos_array_entries = *quant_infos_count;
    *quant_infos_count = quant_infos_vec.size();

    CHECK(quant_infos_vec.size() <= quant_infos_array_entries, HAILO_INSUFFICIENT_BUFFER,
          "The given buffer is too small to contain all quant infos. there are {} quant infos in the given stream, given buffer size is {}",
          quant_infos_vec.size(), quant_infos_array_entries);

    std::copy(quant_infos_vec.begin(), quant_infos_vec.end(), quant_infos);

    return HAILO_SUCCESS;
}

hailo_status hailo_get_input_stream_quant_infos(hailo_input_stream stream, hailo_quant_info_t *quant_infos, size_t *quant_infos_count)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(quant_infos);
    CHECK_ARG_NOT_NULL(quant_infos_count);

    const auto quant_infos_vector = (reinterpret_cast<const InputStream*>(stream))->get_quant_infos();
    auto status = convert_quant_infos_vector_to_array(quant_infos_vector, quant_infos, quant_infos_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status hailo_get_input_vstream_quant_infos(hailo_input_vstream vstream, hailo_quant_info_t *quant_infos, size_t *quant_infos_count)
{
    CHECK_ARG_NOT_NULL(vstream);
    CHECK_ARG_NOT_NULL(quant_infos);
    CHECK_ARG_NOT_NULL(quant_infos_count);

    const auto quant_infos_vector = (reinterpret_cast<const InputVStream*>(vstream))->get_quant_infos();
    auto status = convert_quant_infos_vector_to_array(quant_infos_vector, quant_infos, quant_infos_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status hailo_create_output_transform_context(const hailo_stream_info_t *stream_info,
    const hailo_transform_params_t *transform_params, hailo_output_transform_context *transform_context)
{
    CHECK_ARG_NOT_NULL(stream_info);
    CHECK_ARG_NOT_NULL(transform_params);
    CHECK_ARG_NOT_NULL(transform_context);

    if (!Quantization::is_qp_valid(stream_info->quant_info)) {
        LOGGER__ERROR("quant_info of stream_info is invalid as the model was compiled with multiple quant_infos. "
                    "Please compile again or call hailo_create_output_transform_context_by_stream instead");
        return HAILO_INVALID_ARGUMENT;
    }

    auto local_transform_context = OutputTransformContext::create(*stream_info, *transform_params);
    CHECK_EXPECTED_AS_STATUS(local_transform_context);

    *transform_context = reinterpret_cast<hailo_output_transform_context>(local_transform_context.release().release());
    return HAILO_SUCCESS;
}

hailo_status hailo_create_output_transform_context_by_stream(hailo_output_stream stream,
    const hailo_transform_params_t *transform_params, hailo_output_transform_context *transform_context)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(transform_params);
    CHECK_ARG_NOT_NULL(transform_context);

    OutputStream *output_stream = reinterpret_cast<OutputStream*>(stream);
    auto local_transform_context = OutputTransformContext::create(*output_stream, *transform_params);
    CHECK_EXPECTED_AS_STATUS(local_transform_context);

    *transform_context = reinterpret_cast<hailo_output_transform_context>(local_transform_context.release().release());
    return HAILO_SUCCESS;
}

hailo_status hailo_release_output_transform_context(hailo_output_transform_context transform_context)
{
    CHECK_ARG_NOT_NULL(transform_context);
    delete reinterpret_cast<OutputTransformContext*>(transform_context);
    return HAILO_SUCCESS;
}

hailo_status hailo_is_output_transformation_required(const hailo_3d_image_shape_t *src_image_shape, const hailo_format_t *src_format,
    const hailo_3d_image_shape_t *dst_image_shape, const hailo_format_t *dst_format, const hailo_quant_info_t *quant_info,
    bool *transformation_required)
{
    CHECK_ARG_NOT_NULL(src_image_shape);
    CHECK_ARG_NOT_NULL(src_format);
    CHECK_ARG_NOT_NULL(dst_image_shape);
    CHECK_ARG_NOT_NULL(dst_format);
    CHECK_ARG_NOT_NULL(quant_info);
    CHECK_ARG_NOT_NULL(transformation_required);

    auto exp = OutputTransformContext::is_transformation_required(*src_image_shape, *src_format, *dst_image_shape, *dst_format,
        std::vector<hailo_quant_info_t>{*quant_info}); // TODO: Get quant vector (HRT-11052)
    CHECK_EXPECTED_AS_STATUS(exp);
    *transformation_required = exp.value();

    return HAILO_SUCCESS;
}

hailo_status hailo_is_output_transformation_required2(
    const hailo_3d_image_shape_t *src_image_shape, const hailo_format_t *src_format,
    const hailo_3d_image_shape_t *dst_image_shape, const hailo_format_t *dst_format,
    const hailo_quant_info_t *quant_infos, size_t quant_infos_count, bool *transformation_required)
{
    CHECK_ARG_NOT_NULL(src_image_shape);
    CHECK_ARG_NOT_NULL(src_format);
    CHECK_ARG_NOT_NULL(dst_image_shape);
    CHECK_ARG_NOT_NULL(dst_format);
    CHECK_ARG_NOT_NULL(quant_infos);
    CHECK_ARG_NOT_NULL(transformation_required);

    std::vector<hailo_quant_info_t> quant_info_vector;
    const hailo_quant_info_t* ptr = quant_infos;
    size_t count = quant_infos_count;
    for (size_t i = 0; i < count; ++i) {
        const hailo_quant_info_t& quant_info = *(ptr + i);
        quant_info_vector.push_back(quant_info);
    }
    auto expected_tranformation_required = OutputTransformContext::is_transformation_required(*src_image_shape, *src_format, *dst_image_shape, *dst_format, quant_info_vector);
    CHECK_EXPECTED_AS_STATUS(expected_tranformation_required);
    *transformation_required = expected_tranformation_required.release();

    return HAILO_SUCCESS;
}

hailo_status hailo_transform_frame_by_output_transform_context(hailo_output_transform_context transform_context,
    const void *src, size_t src_size, void *dst, size_t dst_size)
{
    CHECK_ARG_NOT_NULL(transform_context);
    CHECK_ARG_NOT_NULL(src);
    CHECK_ARG_NOT_NULL(dst);

    MemoryView dst_buffer(dst, dst_size);
    auto status = reinterpret_cast<OutputTransformContext*>(transform_context)->transform(MemoryView::create_const(src,
        src_size), dst_buffer);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_get_output_stream_quant_infos(hailo_output_stream stream, hailo_quant_info_t *quant_infos, size_t *quant_infos_count)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(quant_infos);
    CHECK_ARG_NOT_NULL(quant_infos_count);

    auto quant_infos_vector = (reinterpret_cast<OutputStream*>(stream))->get_quant_infos();
    auto status = convert_quant_infos_vector_to_array(quant_infos_vector, quant_infos, quant_infos_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status hailo_get_output_vstream_quant_infos(hailo_output_vstream vstream, hailo_quant_info_t *quant_infos, size_t *quant_infos_count)
{
    CHECK_ARG_NOT_NULL(vstream);
    CHECK_ARG_NOT_NULL(quant_infos);
    CHECK_ARG_NOT_NULL(quant_infos_count);

    const auto quant_infos_vector = (reinterpret_cast<const OutputVStream*>(vstream))->get_quant_infos();
    auto status = convert_quant_infos_vector_to_array(quant_infos_vector, quant_infos, quant_infos_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status hailo_is_qp_valid(const hailo_quant_info_t quant_info, bool *is_qp_valid)
{
    CHECK_ARG_NOT_NULL(is_qp_valid);

    *is_qp_valid = Quantization::is_qp_valid(quant_info);

    return HAILO_SUCCESS;
}

hailo_status hailo_create_demuxer_by_stream(hailo_output_stream stream,
    const hailo_demux_params_t *demux_params, hailo_output_demuxer *demuxer)
{
    CHECK_ARG_NOT_NULL(stream);
    CHECK_ARG_NOT_NULL(demux_params);
    CHECK_ARG_NOT_NULL(demuxer);

    auto local_demuxer = OutputDemuxer::create(*reinterpret_cast<OutputStream*>(stream));
    CHECK_EXPECTED_AS_STATUS(local_demuxer);

    auto allocated_demuxer = local_demuxer.release().release();

    *demuxer = reinterpret_cast<hailo_output_demuxer>(allocated_demuxer);
    return HAILO_SUCCESS;
}

hailo_status hailo_release_output_demuxer(hailo_output_demuxer demuxer)
{
    CHECK_ARG_NOT_NULL(demuxer);
    delete reinterpret_cast<OutputDemuxer*>(demuxer);
    return HAILO_SUCCESS;
}

hailo_status hailo_demux_raw_frame_by_output_demuxer(hailo_output_demuxer demuxer, const void *src, size_t src_size,
    hailo_stream_raw_buffer_t *raw_buffers, size_t raw_buffers_count)
{
    CHECK_ARG_NOT_NULL(src);
    CHECK_ARG_NOT_NULL(raw_buffers);
    CHECK_ARG_NOT_NULL(demuxer);

    std::vector<MemoryView> raw_buffers_vector;
    for (size_t i = 0; i < raw_buffers_count; i++) {
        raw_buffers_vector.emplace_back(raw_buffers[i].buffer, raw_buffers[i].size);
    }
    auto src_memview = MemoryView::create_const(src, src_size);
    auto status = reinterpret_cast<OutputDemuxer*>(demuxer)->transform_demux(src_memview, raw_buffers_vector);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status hailo_demux_by_name_raw_frame_by_output_demuxer(hailo_output_demuxer demuxer, const void *src,
    size_t src_size, hailo_stream_raw_buffer_by_name_t *raw_buffers_by_name, size_t raw_buffers_count)
{
    CHECK_ARG_NOT_NULL(src);
    CHECK_ARG_NOT_NULL(raw_buffers_by_name);
    CHECK_ARG_NOT_NULL(demuxer);

    std::map<std::string, MemoryView> raw_buffers_map;
    for (size_t i = 0; i < raw_buffers_count; i++) {
        raw_buffers_map.emplace(std::string(raw_buffers_by_name[i].name),
            MemoryView(raw_buffers_by_name[i].raw_buffer.buffer, raw_buffers_by_name[i].raw_buffer.size));
    }
    auto src_memview = MemoryView::create_const(src, src_size);
    auto status = reinterpret_cast<OutputDemuxer*>(demuxer)->transform_demux(src_memview, raw_buffers_map);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status hailo_get_mux_infos_by_output_demuxer(hailo_output_demuxer demuxer, hailo_stream_info_t *stream_infos,
    size_t *number_of_streams)
{
    CHECK_ARG_NOT_NULL(demuxer);
    CHECK_ARG_NOT_NULL(stream_infos);
    CHECK_ARG_NOT_NULL(number_of_streams);

    const auto &mux_infos = reinterpret_cast<OutputDemuxer*>(demuxer)->get_edges_stream_info();
    if (*number_of_streams < mux_infos.size()) {
        LOGGER__ERROR("Too many mux infos detected. Mux infos detected: {}, stream_infos array size: {}",
            mux_infos.size(), *number_of_streams);
        *number_of_streams = mux_infos.size();
        return HAILO_INSUFFICIENT_BUFFER;
    }
    *number_of_streams = mux_infos.size();

    for (size_t i = 0; i < mux_infos.size(); i++) {
        stream_infos[i] = mux_infos[i];
    }
    return HAILO_SUCCESS;
}

hailo_status hailo_hef_get_vstream_name_from_original_name(hailo_hef hef, const char *network_group_name,
    const char *original_name, hailo_layer_name_t *vstream_name)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(original_name);
    CHECK_ARG_NOT_NULL(vstream_name);

    const auto network_group_name_str = get_name_as_str(network_group_name);

    auto results = reinterpret_cast<Hef*>(hef)->get_vstream_name_from_original_name(original_name, network_group_name_str);
    CHECK_EXPECTED_AS_STATUS(results);

    CHECK(results->length() < HAILO_MAX_STREAM_NAME_SIZE, HAILO_INTERNAL_FAILURE,
        "Stream '{}' name is too long. max allowed (including NULL terminator): {}, recived: {}", results.value(),
        HAILO_MAX_STREAM_NAME_SIZE, results->length() + 1);

    // + 1 for NULL terminator
    strncpy(vstream_name->name, results->c_str(), results->length() + 1);
    return HAILO_SUCCESS;
}

hailo_status hailo_hef_get_original_names_from_vstream_name(hailo_hef hef, const char *network_group_name,
    const char *vstream_name, hailo_layer_name_t *original_names, size_t *original_names_length)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(vstream_name);
    CHECK_ARG_NOT_NULL(original_names);
    CHECK_ARG_NOT_NULL(original_names_length);

    const auto network_group_name_str = get_name_as_str(network_group_name);

    auto results = reinterpret_cast<Hef*>(hef)->get_original_names_from_vstream_name(vstream_name, network_group_name_str);
    CHECK_EXPECTED_AS_STATUS(results);

    if (results->size() > *original_names_length) {
        LOGGER__ERROR("Too many original names detected. original_names_count: {}, original_names_array_size: {}",
            results->size(), *original_names_length);
        *original_names_length = results->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }
    *original_names_length = results->size();

    int i = 0;
    for (const auto &original_name : results.value()) {
        CHECK(original_name.length() < HAILO_MAX_STREAM_NAME_SIZE, HAILO_INTERNAL_FAILURE,
            "Layer '{}' name is too long. max allowed (including NULL terminator): {}, received: {}", original_name,
            HAILO_MAX_STREAM_NAME_SIZE, original_name.length() + 1);

        // + 1 for NULL terminator
        strncpy(original_names[i].name, original_name.c_str(), original_name.length() + 1);
        i++;
    }
    return HAILO_SUCCESS;
}

hailo_status hailo_hef_get_vstream_names_from_stream_name(hailo_hef hef, const char *network_group_name,
    const char *stream_name, hailo_layer_name_t *vstream_names, size_t *vstream_names_length)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(stream_name);
    CHECK_ARG_NOT_NULL(vstream_names);
    CHECK_ARG_NOT_NULL(vstream_names_length);

    const auto network_group_name_str = get_name_as_str(network_group_name);

    auto results = reinterpret_cast<Hef*>(hef)->get_vstream_names_from_stream_name(stream_name, network_group_name_str);
    CHECK_EXPECTED_AS_STATUS(results);

    if (results->size() > *vstream_names_length) {
        LOGGER__ERROR("Too many vstream names detected. vstream_names_count: {}, vstream_names_array_size: {}",
            results->size(), *vstream_names_length);
        *vstream_names_length = results->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }
    *vstream_names_length = results->size();

    int i = 0;
    for (const auto &vstream_name : results.value()) {
        CHECK(vstream_name.length() < HAILO_MAX_STREAM_NAME_SIZE, HAILO_INTERNAL_FAILURE,
            "Layer '{}' name is too long. max allowed (including NULL terminator): {}, received: {}", vstream_name,
            HAILO_MAX_STREAM_NAME_SIZE, vstream_name.length() + 1);

        // + 1 for NULL terminator
        strncpy(vstream_names[i].name, vstream_name.c_str(), vstream_name.length() + 1);
        i++;
    }
    return HAILO_SUCCESS;
}

hailo_status hailo_hef_get_stream_names_from_vstream_name(hailo_hef hef, const char *network_group_name,
    const char *vstream_name, hailo_layer_name_t *stream_names, size_t *stream_names_length)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(vstream_name);
    CHECK_ARG_NOT_NULL(stream_names);
    CHECK_ARG_NOT_NULL(stream_names_length);

    const auto network_group_name_str = get_name_as_str(network_group_name);

    auto results = reinterpret_cast<Hef*>(hef)->get_stream_names_from_vstream_name(vstream_name, network_group_name_str);
    CHECK_EXPECTED_AS_STATUS(results);

    if (results->size() > *stream_names_length) {
        LOGGER__ERROR("Too many stream names detected. vstream_names_count: {}, vstream_names_array_size: {}",
            results->size(), *stream_names_length);
        *stream_names_length = results->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }
    *stream_names_length = results->size();

    int i = 0;
    for (const auto &stream_name : results.value()) {
        CHECK(stream_name.length() < HAILO_MAX_STREAM_NAME_SIZE, HAILO_INTERNAL_FAILURE,
            "Layer '{}' name is too long. max allowed (including NULL terminator): {}, received: {}", stream_name,
            HAILO_MAX_STREAM_NAME_SIZE, stream_name.length() + 1);

        // + 1 for NULL terminator
        strncpy(stream_names[i].name, stream_name.c_str(), stream_name.length() + 1);
        i++;
    }
    return HAILO_SUCCESS;
}

hailo_status hailo_hef_get_sorted_output_names(hailo_hef hef, const char *network_group_name,
    hailo_layer_name_t *sorted_output_names, size_t *sorted_output_names_count)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(sorted_output_names);
    CHECK_ARG_NOT_NULL(sorted_output_names_count);

    const auto network_group_name_str = get_name_as_str(network_group_name);

    auto results = reinterpret_cast<Hef*>(hef)->get_sorted_output_names(network_group_name_str);
    CHECK_EXPECTED_AS_STATUS(results);

    if (results->size() > *sorted_output_names_count) {
        LOGGER__ERROR("Failed to get sorted output names. Results size is {}, given size is {}",
            results->size(), *sorted_output_names_count);
        *sorted_output_names_count = results->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }
    *sorted_output_names_count = results->size();
    
    int i = 0;
    for (const auto &name : results.value()) {
        CHECK(name.length() < HAILO_MAX_STREAM_NAME_SIZE, HAILO_INTERNAL_FAILURE,
            "Output '{}' name is too long. max allowed (including NULL terminator): {}, received: {}", name,
            HAILO_MAX_STREAM_NAME_SIZE, name.length() + 1);

        // + 1 for NULL terminator
        strncpy(sorted_output_names[i].name, name.c_str(), name.length() + 1);
        i++;
    }
    return HAILO_SUCCESS;
}

hailo_status hailo_hef_get_bottleneck_fps(hailo_hef hef, const char *network_group_name,
    float64_t *bottleneck_fps)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(bottleneck_fps);

    const auto network_group_name_str = get_name_as_str(network_group_name);

    auto bottleneck_fps_expected = reinterpret_cast<Hef*>(hef)->get_bottleneck_fps(network_group_name_str);
    CHECK_EXPECTED_AS_STATUS(bottleneck_fps_expected);

    *bottleneck_fps = bottleneck_fps_expected.value();

    return HAILO_SUCCESS;
}

hailo_status hailo_make_input_vstream_params(hailo_configured_network_group network_group, bool /*unused*/,
    hailo_format_type_t format_type, hailo_input_vstream_params_by_name_t *input_params,
    size_t *input_params_count)
{
    CHECK_ARG_NOT_NULL(network_group);
    CHECK_ARG_NOT_NULL(input_params);
    CHECK_ARG_NOT_NULL(input_params_count);

    auto net_group_ptr = reinterpret_cast<ConfiguredNetworkGroup*>(network_group);
    auto input_params_map = net_group_ptr->make_input_vstream_params({}, format_type, 
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS , HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    CHECK_EXPECTED_AS_STATUS(input_params_map);

    if (input_params_map->size() > *input_params_count) {
        LOGGER__ERROR(
            "The given buffer is too small to contain all input_vstream_params. There are {} input vstreams in the given network_group, given size is {}",
            input_params_map->size(), *input_params_count);
        *input_params_count = input_params_map->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }

    int i = 0;
    for (auto &name_pair : input_params_map.value()) {
        // Adding +1 for NULL terminator
        CHECK(HAILO_MAX_STREAM_NAME_SIZE >= (name_pair.first.length() + 1), HAILO_INVALID_ARGUMENT,
            "Name too long (max is {}, received {})", HAILO_MAX_STREAM_NAME_SIZE, name_pair.first);
        memcpy(input_params[i].name, name_pair.first.c_str(), name_pair.first.length() + 1);
        input_params[i].params = name_pair.second;
        i++;
    }
    *input_params_count = input_params_map->size();

    return HAILO_SUCCESS;
}

hailo_status hailo_make_output_vstream_params(hailo_configured_network_group network_group, bool /*unused*/,
    hailo_format_type_t format_type, hailo_output_vstream_params_by_name_t *output_vstream_params,
    size_t *output_params_count)
{
    CHECK_ARG_NOT_NULL(network_group);
    CHECK_ARG_NOT_NULL(output_vstream_params);
    CHECK_ARG_NOT_NULL(output_params_count);

    auto net_group_ptr = reinterpret_cast<ConfiguredNetworkGroup*>(network_group);
    auto output_params_map = net_group_ptr->make_output_vstream_params({}, format_type,
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS , HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    CHECK_EXPECTED_AS_STATUS(output_params_map);

    if (output_params_map->size() > *output_params_count) {
        LOGGER__ERROR(
            "The given buffer is too small to contain all output_vstream_params. There are {} output vstreams in the given network_group, given size is {}",
            output_params_map->size(), *output_params_count);
        *output_params_count = output_params_map->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }

    int i = 0;
    for (auto &name_pair : output_params_map.value()) {
        // Adding +1 for NULL terminator
        CHECK(HAILO_MAX_STREAM_NAME_SIZE >= (name_pair.first.length() + 1), HAILO_INVALID_ARGUMENT,
            "Name too long (max is {}, received {})", HAILO_MAX_STREAM_NAME_SIZE, name_pair.first);
        memcpy(output_vstream_params[i].name, name_pair.first.c_str(), name_pair.first.length() + 1);
        output_vstream_params[i].params = name_pair.second;
        i++;
    }
    *output_params_count = output_params_map->size();

    return HAILO_SUCCESS;
}

hailo_status hailo_get_output_vstream_groups(hailo_configured_network_group network_group,
    hailo_output_vstream_name_by_group_t *output_name_by_group, size_t *output_name_by_group_count)
{
    CHECK_ARG_NOT_NULL(network_group);
    CHECK_ARG_NOT_NULL(output_name_by_group);
    CHECK_ARG_NOT_NULL(output_name_by_group_count);

    auto net_group_ptr = reinterpret_cast<ConfiguredNetworkGroup*>(network_group);
    auto output_vstream_groups = net_group_ptr->get_output_vstream_groups();
    CHECK_EXPECTED_AS_STATUS(output_vstream_groups);

    
    size_t number_of_output_vstreams = 0;
    for (auto &vstreams_group : output_vstream_groups.value()) {
        number_of_output_vstreams += vstreams_group.size();
    }
    if (number_of_output_vstreams > *output_name_by_group_count) {
        LOGGER__ERROR(
            "The given buffer is too small to contain all output_vstream_params. There are {} output vstreams in the given network_group, given size is {}",
            number_of_output_vstreams, *output_name_by_group_count);
        *output_name_by_group_count = number_of_output_vstreams;
        return HAILO_INSUFFICIENT_BUFFER;
    }

    uint8_t pipeline_group_index = 0;
    int vstream_index = 0;
    for (auto &vstreams_group : output_vstream_groups.value()) {
        for (auto &vstream_name : vstreams_group) {
            // Adding +1 for NULL terminator
            CHECK(HAILO_MAX_STREAM_NAME_SIZE >= (vstream_name.length() + 1), HAILO_INVALID_ARGUMENT,
                "Name too long (max is {}, received {})", HAILO_MAX_STREAM_NAME_SIZE, vstream_name);
            memcpy(output_name_by_group[vstream_index].name, vstream_name.c_str(), vstream_name.length() + 1);
            output_name_by_group[vstream_index].pipeline_group_index = pipeline_group_index;
            vstream_index++;
        }
        pipeline_group_index++;
    }
    *output_name_by_group_count = number_of_output_vstreams;

    return HAILO_SUCCESS;
}

hailo_status hailo_create_input_vstreams(hailo_configured_network_group configured_network_group,
    const hailo_input_vstream_params_by_name_t *inputs_params, size_t inputs_count, hailo_input_vstream *input_vstreams)
{
    CHECK_ARG_NOT_NULL(configured_network_group);
    CHECK_ARG_NOT_NULL(inputs_params);
    CHECK_ARG_NOT_NULL(input_vstreams);

    std::map<std::string, hailo_vstream_params_t> inputs_params_map;
    for (size_t i = 0; i < inputs_count; i++) {
        inputs_params_map.emplace(inputs_params[i].name, inputs_params[i].params);
    }

    auto vstreams_vec_expected = VStreamsBuilder::create_input_vstreams(*reinterpret_cast<ConfiguredNetworkGroup*>(configured_network_group), 
        inputs_params_map);
    CHECK_EXPECTED_AS_STATUS(vstreams_vec_expected);
    auto vstreams_vec = vstreams_vec_expected.release();

    std::vector<std::unique_ptr<InputVStream>> vstreams_ptrs;
    for (auto &vstream: vstreams_vec) {
        auto vstream_ptr = new (std::nothrow) InputVStream(std::move(vstream));
        CHECK_NOT_NULL(vstream_ptr, HAILO_OUT_OF_HOST_MEMORY);

        vstreams_ptrs.emplace_back(vstream_ptr);
    }

    for (size_t i = 0; i < inputs_count; i++) {
        input_vstreams[i] = reinterpret_cast<hailo_input_vstream>(vstreams_ptrs[i].release());
    }

    return HAILO_SUCCESS;
}

hailo_status hailo_create_output_vstreams(hailo_configured_network_group configured_network_group,
    const hailo_output_vstream_params_by_name_t *outputs_params, size_t outputs_count, hailo_output_vstream *output_vstreams)
{
    CHECK_ARG_NOT_NULL(configured_network_group);
    CHECK_ARG_NOT_NULL(outputs_params);
    CHECK_ARG_NOT_NULL(output_vstreams);

    std::map<std::string, hailo_vstream_params_t> outputs_params_map;
    for (size_t i = 0; i < outputs_count; i++) {
        outputs_params_map.emplace(outputs_params[i].name, outputs_params[i].params);
    }

    auto vstreams_vec_expected = VStreamsBuilder::create_output_vstreams(*reinterpret_cast<ConfiguredNetworkGroup*>(configured_network_group), 
        outputs_params_map);
    CHECK_EXPECTED_AS_STATUS(vstreams_vec_expected);
    auto vstreams_vec = vstreams_vec_expected.release();

    std::vector<std::unique_ptr<OutputVStream>> vstreams_ptrs;
    for (auto &vstream: vstreams_vec) {
        auto vstream_ptr = new (std::nothrow) OutputVStream(std::move(vstream));
        CHECK_NOT_NULL(vstream_ptr, HAILO_OUT_OF_HOST_MEMORY);

        vstreams_ptrs.emplace_back(vstream_ptr);
    }

    for (size_t i = 0; i < outputs_count; i++) {
        output_vstreams[i] = reinterpret_cast<hailo_output_vstream>(vstreams_ptrs[i].release());
    }

    return HAILO_SUCCESS;
}

hailo_status hailo_get_input_vstream_frame_size(hailo_input_vstream input_vstream, size_t *frame_size)
{
    CHECK_ARG_NOT_NULL(input_vstream);
    CHECK_ARG_NOT_NULL(frame_size);

    *frame_size = reinterpret_cast<InputVStream*>(input_vstream)->get_frame_size();
    return HAILO_SUCCESS;
}

hailo_status hailo_get_input_vstream_info(hailo_input_vstream input_vstream, hailo_vstream_info_t *vstream_info)
{
    CHECK_ARG_NOT_NULL(input_vstream);
    CHECK_ARG_NOT_NULL(vstream_info);

    *vstream_info = reinterpret_cast<InputVStream*>(input_vstream)->get_info();
    return HAILO_SUCCESS;
}

hailo_status hailo_get_input_vstream_user_format(hailo_input_vstream input_vstream, hailo_format_t *user_buffer_format)
{
    CHECK_ARG_NOT_NULL(input_vstream);
    CHECK_ARG_NOT_NULL(user_buffer_format);

    *user_buffer_format = reinterpret_cast<InputVStream*>(input_vstream)->get_user_buffer_format();
    return HAILO_SUCCESS;
}

hailo_status hailo_get_output_vstream_frame_size(hailo_output_vstream output_vstream, size_t *frame_size)
{
    CHECK_ARG_NOT_NULL(output_vstream);
    CHECK_ARG_NOT_NULL(frame_size);

    *frame_size = reinterpret_cast<OutputVStream*>(output_vstream)->get_frame_size();
    return HAILO_SUCCESS;
}

hailo_status hailo_get_output_vstream_info(hailo_output_vstream output_vstream, hailo_vstream_info_t *vstream_info)
{
    CHECK_ARG_NOT_NULL(output_vstream);
    CHECK_ARG_NOT_NULL(vstream_info);

    *vstream_info = reinterpret_cast<OutputVStream*>(output_vstream)->get_info();
    return HAILO_SUCCESS;
}

hailo_status hailo_get_output_vstream_user_format(hailo_output_vstream output_vstream, hailo_format_t *user_buffer_format)
{
    CHECK_ARG_NOT_NULL(output_vstream);
    CHECK_ARG_NOT_NULL(user_buffer_format);

    *user_buffer_format = reinterpret_cast<OutputVStream*>(output_vstream)->get_user_buffer_format();
    return HAILO_SUCCESS;
}

hailo_status hailo_get_vstream_frame_size(hailo_vstream_info_t *vstream_info, hailo_format_t *user_buffer_format, size_t *frame_size)
{
    CHECK_ARG_NOT_NULL(vstream_info);
    CHECK_ARG_NOT_NULL(user_buffer_format);
    CHECK_ARG_NOT_NULL(frame_size);

    *frame_size = HailoRTCommon::get_frame_size(*vstream_info, *user_buffer_format);
    return HAILO_SUCCESS;
}

hailo_status hailo_vstream_write_raw_buffer(hailo_input_vstream input_vstream, const void *buffer, size_t buffer_size)
{
    CHECK_ARG_NOT_NULL(input_vstream);
    CHECK_ARG_NOT_NULL(buffer);

    auto status = reinterpret_cast<InputVStream*>(input_vstream)->write(MemoryView::create_const(buffer, buffer_size));
    if (HAILO_STREAM_ABORT == status) {
        return status;
    }
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_vstream_write_pix_buffer(hailo_input_vstream input_vstream, const hailo_pix_buffer_t *buffer)
{
    CHECK(HAILO_PIX_BUFFER_MEMORY_TYPE_USERPTR == buffer->memory_type, HAILO_NOT_SUPPORTED, "Memory type of pix buffer must be of type USERPTR!");

    CHECK_ARG_NOT_NULL(input_vstream);
    CHECK_ARG_NOT_NULL(buffer);

    auto status = reinterpret_cast<InputVStream*>(input_vstream)->write(*buffer);
    if (HAILO_STREAM_ABORT == status) {
        return status;
    }
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_vstream_read_raw_buffer(hailo_output_vstream output_vstream, void *dst, size_t dst_size)
{
    CHECK_ARG_NOT_NULL(output_vstream);
    CHECK_ARG_NOT_NULL(dst);

    auto status = reinterpret_cast<OutputVStream*>(output_vstream)->read(MemoryView(dst, dst_size));
    if (HAILO_STREAM_ABORT == status) {
        return status;
    }
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_vstream_set_nms_score_threshold(hailo_output_vstream output_vstream, float32_t threshold)
{
    CHECK_ARG_NOT_NULL(output_vstream);

    auto status = reinterpret_cast<OutputVStream*>(output_vstream)->set_nms_score_threshold(threshold);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_vstream_set_nms_iou_threshold(hailo_output_vstream output_vstream, float32_t threshold)
{
    CHECK_ARG_NOT_NULL(output_vstream);

    auto status = reinterpret_cast<OutputVStream*>(output_vstream)->set_nms_iou_threshold(threshold);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_vstream_set_nms_max_proposals_per_class(hailo_output_vstream output_vstream, uint32_t max_proposals_per_class)
{
    CHECK_ARG_NOT_NULL(output_vstream);

    auto status = reinterpret_cast<OutputVStream*>(output_vstream)->set_nms_max_proposals_per_class(max_proposals_per_class);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_release_input_vstreams(const hailo_input_vstream *input_vstreams, size_t inputs_count)
{
    CHECK_ARG_NOT_NULL(input_vstreams);

    for (size_t i = 0; i < inputs_count; i++) {
        delete reinterpret_cast<InputVStream*>(input_vstreams[i]);
    }
    return HAILO_SUCCESS;
}

hailo_status hailo_release_output_vstreams(const hailo_output_vstream *output_vstreams, size_t outputs_count)
{
    CHECK_ARG_NOT_NULL(output_vstreams);

    for (size_t i = 0; i < outputs_count; i++) {
        delete reinterpret_cast<OutputVStream*>(output_vstreams[i]);
    }
    return HAILO_SUCCESS;
}

hailo_status hailo_clear_input_vstreams(const hailo_input_vstream *input_vstreams, size_t inputs_count)
{
    CHECK_ARG_NOT_NULL(input_vstreams);

    std::vector<std::reference_wrapper<InputVStream>> vstreams;
    vstreams.reserve(inputs_count);
    for (size_t i = 0; i < inputs_count; i++) {
        vstreams.emplace_back(std::ref(*reinterpret_cast<InputVStream*>(input_vstreams[i])));
    }

    auto status = InputVStream::clear(vstreams);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

hailo_status hailo_clear_output_vstreams(const hailo_output_vstream *output_vstreams, size_t outputs_count)
{
    CHECK_ARG_NOT_NULL(output_vstreams);

    std::vector<std::reference_wrapper<OutputVStream>> vstreams;
    vstreams.reserve(outputs_count);
    for (size_t i = 0; i < outputs_count; i++) {
        vstreams.emplace_back(std::ref(*reinterpret_cast<OutputVStream*>(output_vstreams[i])));
    }

    auto status = OutputVStream::clear(vstreams);
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

HAILORTAPI hailo_status hailo_flush_input_vstream(hailo_input_vstream input_vstream)
{
    CHECK_ARG_NOT_NULL(input_vstream);

    auto status = reinterpret_cast<InputVStream*>(input_vstream)->flush();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status hailo_wait_for_network_group_activation(hailo_configured_network_group network_group,
    uint32_t timeout_ms)
{
    CHECK_ARG_NOT_NULL(network_group);

    auto status = reinterpret_cast<ConfiguredNetworkGroup*>(network_group)->wait_for_activation(std::chrono::milliseconds(timeout_ms));
    CHECK_SUCCESS(status);
    return HAILO_SUCCESS;
}

static const char *hailo_status_msg_format[] =
{
#define HAILO_STATUS__X(value, name) #name,
    HAILO_STATUS_VARIABLES
#undef HAILO_STATUS__X
};

const char* hailo_get_status_message(hailo_status status)
{
    if (status >= HAILO_STATUS_COUNT) {
        LOGGER__ERROR("Failed to get hailo_status message because of invalid hailo_status value. Max hailo_status value = {}, given value = {}",
            (HAILO_STATUS_COUNT-1), static_cast<int>(status));
        return nullptr;
    }
    return hailo_status_msg_format[status];
}

hailo_status hailo_init_vdevice_params(hailo_vdevice_params_t *params)
{
    CHECK_ARG_NOT_NULL(params);
    *params = HailoRTDefaults::get_vdevice_params();
    return HAILO_SUCCESS;
}

hailo_status hailo_create_vdevice(hailo_vdevice_params_t *params, hailo_vdevice *vdevice)
{
    CHECK_ARG_NOT_NULL(vdevice);

    auto vdevice_obj = make_unique_nothrow<_hailo_vdevice>();
    CHECK_NOT_NULL(vdevice_obj, HAILO_OUT_OF_HOST_MEMORY);

    TRY(vdevice_obj->vdevice, (params == nullptr) ? VDevice::create() : VDevice::create(*params));

    TRY(auto phys_devices, vdevice_obj->vdevice->get_physical_devices());
    vdevice_obj->physical_devices.reserve(phys_devices.size());
    for (auto phys_device : phys_devices) {
        vdevice_obj->physical_devices.emplace_back(_hailo_device{&phys_device.get(), {}});
    }

    *vdevice = reinterpret_cast<hailo_vdevice>(vdevice_obj.release());
    return HAILO_SUCCESS;
}

hailo_status hailo_configure_vdevice(hailo_vdevice vdevice, hailo_hef hef,
    hailo_configure_params_t *params, hailo_configured_network_group *network_groups, size_t *number_of_network_groups)
{
    CHECK_ARG_NOT_NULL(vdevice);
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(network_groups);
    CHECK_ARG_NOT_NULL(number_of_network_groups);

    auto configure_params = get_configure_params_map(params);

    TRY(auto added_net_groups, vdevice->vdevice->configure(*reinterpret_cast<Hef*>(hef), configure_params));

    CHECK(added_net_groups.size() <= (*number_of_network_groups), HAILO_INSUFFICIENT_BUFFER,
        "Can't return all network_groups. HEF file contained {} network_groups, but output array is of size {}",
        added_net_groups.size(), (*number_of_network_groups));

    for (size_t i = 0; i < added_net_groups.size(); ++i) {
        network_groups[i] = reinterpret_cast<hailo_configured_network_group>(added_net_groups[i].get());
    }

    // Since the C API doesn't let the user to hold the cng, we need to keep it alive in the vdevice scope
    vdevice->cngs.insert(vdevice->cngs.end(), added_net_groups.begin(), added_net_groups.end());

    *number_of_network_groups = added_net_groups.size();
    return HAILO_SUCCESS;
}

hailo_status hailo_get_physical_devices(hailo_vdevice vdevice, hailo_device *devices,
    size_t *number_of_devices)
{
    CHECK_ARG_NOT_NULL(devices);
    CHECK_ARG_NOT_NULL(number_of_devices);

    if (*number_of_devices < vdevice->physical_devices.size()) {
        LOGGER__ERROR("Can't return all physical devices. there are {} physical devices under the vdevice, but output array is of size {}",
            vdevice->physical_devices.size(), *number_of_devices);
        *number_of_devices = vdevice->physical_devices.size();
        return HAILO_INSUFFICIENT_BUFFER;
    }
    *number_of_devices = vdevice->physical_devices.size();

    for (size_t i = 0; i < vdevice->physical_devices.size(); i++) {
        devices[i] = &vdevice->physical_devices[i];
    }

    return HAILO_SUCCESS;
}

hailo_status hailo_vdevice_get_physical_devices_ids(hailo_vdevice vdevice, hailo_device_id_t *devices_ids,
    size_t *number_of_devices)
{
    CHECK_ARG_NOT_NULL(vdevice);
    CHECK_ARG_NOT_NULL(devices_ids);
    CHECK_ARG_NOT_NULL(number_of_devices);

    const auto phys_devices_ids = vdevice->vdevice->get_physical_devices_ids();
    CHECK_EXPECTED_AS_STATUS(phys_devices_ids);

    if (*number_of_devices < phys_devices_ids->size()) {
        LOGGER__ERROR("Can't return all physical devices ids. There are {} physical devices ids under the vdevice, but output array is of size {}",
            phys_devices_ids->size(), *number_of_devices);
        *number_of_devices = phys_devices_ids->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }
    *number_of_devices = phys_devices_ids->size();

    for (size_t i = 0; i < phys_devices_ids->size(); i++) {
        auto id_expected = HailoRTCommon::to_device_id(phys_devices_ids.value()[i]);
        CHECK_EXPECTED_AS_STATUS(id_expected);
        devices_ids[i] = id_expected.release();
    }

    return HAILO_SUCCESS;
}

hailo_status hailo_release_vdevice(hailo_vdevice vdevice_ptr)
{
    CHECK_ARG_NOT_NULL(vdevice_ptr);
    delete vdevice_ptr;
    return HAILO_SUCCESS;
}

hailo_status hailo_infer(hailo_configured_network_group network_group,
    hailo_input_vstream_params_by_name_t *inputs_params,
    hailo_stream_raw_buffer_by_name_t *input_buffers,
    size_t inputs_count,
    hailo_output_vstream_params_by_name_t *outputs_params,
    hailo_stream_raw_buffer_by_name_t *output_buffers,
    size_t outputs_count,
    size_t frames_count)
{
    CHECK_ARG_NOT_NULL(network_group);
    CHECK_ARG_NOT_NULL(inputs_params);
    CHECK_ARG_NOT_NULL(outputs_params);
    CHECK_ARG_NOT_NULL(input_buffers);
    CHECK_ARG_NOT_NULL(output_buffers);

    std::map<std::string, MemoryView> input_data;
    for (size_t i = 0; i < inputs_count; i++) {
        input_data.emplace(input_buffers[i].name, MemoryView(input_buffers[i].raw_buffer.buffer,
            input_buffers[i].raw_buffer.size));
    }

    std::map<std::string, MemoryView> output_data;
    for (size_t i = 0; i < outputs_count; i++) {
        output_data.emplace(output_buffers[i].name, MemoryView(output_buffers[i].raw_buffer.buffer,
            output_buffers[i].raw_buffer.size));
    }

    std::map<std::string, hailo_vstream_params_t> inputs_params_map;
    for (size_t i = 0; i < inputs_count; i++) {
        inputs_params_map.emplace(inputs_params[i].name, inputs_params[i].params);
    }

    std::map<std::string, hailo_vstream_params_t> outputs_params_map;
    for (size_t i = 0; i < outputs_count; i++) {
        outputs_params_map.emplace(outputs_params[i].name, outputs_params[i].params);
    }

    auto net_group_ptr = reinterpret_cast<ConfiguredNetworkGroup*>(network_group);
    auto infer_pipeline = InferVStreams::create(*net_group_ptr, inputs_params_map, outputs_params_map);
    CHECK_EXPECTED_AS_STATUS(infer_pipeline);

    auto status = infer_pipeline->infer(input_data, output_data, frames_count);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

/* Multi network API functions */
static hailo_status convert_network_infos_vector_to_array(std::vector<hailo_network_info_t> &&network_infos_vec, 
    hailo_network_info_t *network_infos, size_t *number_of_networks)
{
    auto max_entries = *number_of_networks;
    *number_of_networks = network_infos_vec.size();

    CHECK((*number_of_networks) <= max_entries, HAILO_INSUFFICIENT_BUFFER,
          "The given buffer is too small to contain all network infos. There are {} networks in the given hef, given buffer size is {}",
          (*number_of_networks), max_entries);

    std::copy(network_infos_vec.begin(), network_infos_vec.end(), network_infos);

    return HAILO_SUCCESS;
}

HAILORTAPI hailo_status hailo_hef_get_network_infos(hailo_hef hef, const char *network_group_name,
    hailo_network_info_t *networks_infos, size_t *number_of_networks)
{
    CHECK_ARG_NOT_NULL(hef);
    CHECK_ARG_NOT_NULL(networks_infos);
    CHECK_ARG_NOT_NULL(number_of_networks);
    const auto name_str = get_name_as_str(network_group_name);

    auto network_infos_expected = (reinterpret_cast<Hef*>(hef))->get_network_infos(name_str);
    CHECK_EXPECTED_AS_STATUS(network_infos_expected);

    auto status = convert_network_infos_vector_to_array(network_infos_expected.release(), networks_infos, number_of_networks);
    CHECK_SUCCESS(status);
    
    return HAILO_SUCCESS;
}

HAILORTAPI hailo_status hailo_get_network_infos(hailo_configured_network_group network_group,
    hailo_network_info_t *networks_infos, size_t *number_of_networks)
{
    CHECK_ARG_NOT_NULL(network_group);
    CHECK_ARG_NOT_NULL(networks_infos);
    CHECK_ARG_NOT_NULL(number_of_networks);

    auto network_infos_expected = (reinterpret_cast<ConfiguredNetworkGroup*>(network_group))->get_network_infos();
    CHECK_EXPECTED_AS_STATUS(network_infos_expected);

    auto status = convert_network_infos_vector_to_array(network_infos_expected.release(), networks_infos, number_of_networks);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

HAILORTAPI hailo_status hailo_hef_make_input_vstream_params(hailo_hef hef, const char *name, 
    bool /*unused*/, hailo_format_type_t format_type, 
    hailo_input_vstream_params_by_name_t *input_params, size_t *input_params_count)
{
    CHECK_ARG_NOT_NULL(input_params);
    CHECK_ARG_NOT_NULL(input_params_count);
    const auto name_str = get_name_as_str(name);

    auto input_params_map = (reinterpret_cast<Hef*>(hef))->make_input_vstream_params(name_str, {}, format_type, 
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS , HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    CHECK_EXPECTED_AS_STATUS(input_params_map);

    if (input_params_map->size() > *input_params_count) {
        LOGGER__ERROR(
            "The given buffer is too small to contain all input_vstream_params. There are {} detected input vstreams in the given name {} , input param length is {}",
            input_params_map->size(), name, *input_params_count);
        *input_params_count = input_params_map->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }

    int i = 0;
    for (auto &name_pair : input_params_map.value()) {
        // Adding +1 for NULL terminator
        CHECK(HAILO_MAX_STREAM_NAME_SIZE >= (name_pair.first.length() + 1), HAILO_INVALID_ARGUMENT,
            "Name too long (max is {}, received {})", HAILO_MAX_STREAM_NAME_SIZE, name_pair.first);
        memcpy(input_params[i].name, name_pair.first.c_str(), name_pair.first.length() + 1);
        input_params[i].params = name_pair.second;
        i++;
    }
    *input_params_count = input_params_map->size();

    return HAILO_SUCCESS;    
}

hailo_status hailo_hef_make_output_vstream_params(hailo_hef hef, const char *name,
    bool /*unused*/, hailo_format_type_t format_type, 
    hailo_output_vstream_params_by_name_t *output_vstream_params, size_t *output_params_count)
{
    CHECK_ARG_NOT_NULL(output_vstream_params);
    CHECK_ARG_NOT_NULL(output_params_count);
    const auto name_str = get_name_as_str(name);

    auto output_params_map = (reinterpret_cast<Hef*>(hef))->make_output_vstream_params(name_str, {}, format_type,
        HAILO_DEFAULT_VSTREAM_TIMEOUT_MS , HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
    CHECK_EXPECTED_AS_STATUS(output_params_map);

    if (output_params_map->size() > *output_params_count) {
        LOGGER__ERROR(
            "The given buffer is too small to contain all input_vstream_params. There are {} detected output vstreams in the given name {} , output param length is {}",
            output_params_map->size(), name, *output_params_count);
        *output_params_count = output_params_map->size();
        return HAILO_INSUFFICIENT_BUFFER;
    }

    int i = 0;
    for (auto &name_pair : output_params_map.value()) {
        // Adding +1 for NULL terminator
        CHECK(HAILO_MAX_STREAM_NAME_SIZE >= (name_pair.first.length() + 1), HAILO_INVALID_ARGUMENT,
            "Name too long (max is {}, received {})", HAILO_MAX_STREAM_NAME_SIZE, name_pair.first);
        memcpy(output_vstream_params[i].name, name_pair.first.c_str(), name_pair.first.length() + 1);
        output_vstream_params[i].params = name_pair.second;
        i++;
    }
    *output_params_count = output_params_map->size();

    return HAILO_SUCCESS;
}
/* End of multi network API functions */
