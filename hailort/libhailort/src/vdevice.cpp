/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "hailo/hailort.h"
#include "hailo/vdevice.hpp"
#include "vdevice_internal.hpp"
#include "pcie_device.hpp"
#include "core_device.hpp"
#include "hailort_defaults.hpp"

namespace hailort
{

Expected<std::unique_ptr<VDevice>> VDevice::create(const hailo_vdevice_params_t &params)
{
    CHECK_AS_EXPECTED(0 != params.device_count, HAILO_INVALID_ARGUMENT,
        "VDevice creation failed. invalid device_count ({}).", params.device_count);
    
    CHECK_AS_EXPECTED((HAILO_SCHEDULING_ALGORITHM_NONE == params.scheduling_algorithm) || (1 == params.device_count), HAILO_INVALID_ARGUMENT,
        "Network group scheduler can be active only when using one device in the vDevice!");

    auto vdevice = VDeviceBase::create(params);
    CHECK_EXPECTED(vdevice);
    // Upcasting to VDevice unique_ptr (from VDeviceBase unique_ptr)
    auto vdevice_ptr = std::unique_ptr<VDevice>(vdevice.release());
    return vdevice_ptr;
}

Expected<std::unique_ptr<VDevice>> VDevice::create()
{
    auto params = HailoRTDefaults::get_vdevice_params();
    return create(params);
}

Expected<std::unique_ptr<VDeviceBase>> VDeviceBase::create(const hailo_vdevice_params_t &params)
{
    NetworkGroupSchedulerPtr scheduler_ptr;
    if (HAILO_SCHEDULING_ALGORITHM_NONE != params.scheduling_algorithm) {
        if (HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN == params.scheduling_algorithm) {
            auto network_group_scheduler = NetworkGroupScheduler::create_round_robin();
            CHECK_EXPECTED(network_group_scheduler);
            scheduler_ptr = network_group_scheduler.release();
        } else {
            LOGGER__ERROR("Unsupported scheduling algorithm");
            return make_unexpected(HAILO_INVALID_ARGUMENT);
        }
    }

    auto devices_expected = create_devices(params);
    CHECK_EXPECTED(devices_expected);
    auto devices = devices_expected.release();

    std::string vdevice_ids = "VDevice Infos:";
    for (const auto &device : devices) {
        auto info_str = device->get_dev_id();
        vdevice_ids += " " + std::string(info_str);
    }
    LOGGER__INFO("{}", vdevice_ids);

    auto vdevice = std::unique_ptr<VDeviceBase>(new (std::nothrow) VDeviceBase(std::move(devices), scheduler_ptr));
    CHECK_AS_EXPECTED(nullptr != vdevice, HAILO_OUT_OF_HOST_MEMORY);

    return vdevice;
}

// TODO - make this function thread-safe.
Expected<ConfiguredNetworkGroupVector> VDeviceBase::configure(Hef &hef,
    const NetworkGroupsParamsMap &configure_params)
{
    auto start_time = std::chrono::steady_clock::now();
    if (!m_context_switch_manager) {
        auto local_context_switch_manager = VdmaConfigManager::create(*this);
        CHECK_EXPECTED(local_context_switch_manager);
        m_context_switch_manager = make_unique_nothrow<VdmaConfigManager>(local_context_switch_manager.release());
        CHECK_AS_EXPECTED(nullptr != m_context_switch_manager, HAILO_OUT_OF_HOST_MEMORY);
    }

    auto network_groups = m_context_switch_manager->add_hef(hef, configure_params);
    CHECK_EXPECTED(network_groups);

    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
    LOGGER__INFO("Configuring HEF on VDevice took {} milliseconds", elapsed_time_ms);

    return network_groups;
}

Expected<Device::Type> VDeviceBase::get_device_type()
{
    auto device_type = m_devices[0]->get_type();
    for (auto &dev : m_devices) {
        CHECK_AS_EXPECTED(device_type == dev->get_type(), HAILO_INTERNAL_FAILURE,
            "vDevice is supported only with homogeneous device type");
    }
    return device_type;
}

Expected<std::vector<std::unique_ptr<VdmaDevice>>> VDeviceBase::create_devices(const hailo_vdevice_params_t &params)
{
    // Currently we use either pcie device or core device. If the field device_infos is set, the user expects to use
    // PCIe device.
    const bool should_use_core = (params.device_infos == nullptr) && CoreDevice::is_core_driver_loaded();
    return should_use_core ?  create_core_devices(params) : create_pcie_devices(params);
}

Expected<std::vector<std::unique_ptr<VdmaDevice>>> VDeviceBase::create_pcie_devices(const hailo_vdevice_params_t &params)
{
    auto scan_res = PcieDevice::scan();
    CHECK_EXPECTED(scan_res);

    std::vector<std::unique_ptr<VdmaDevice>> devices;
    devices.reserve(params.device_count);

    hailo_pcie_device_info_t *device_infos_ptr = params.device_infos;
    uint32_t devices_pool_count = params.device_count;
    if (nullptr == device_infos_ptr) {
        /* If params.device_infos is not nullptr, we use a pool of the given device_infos.
           Otherwise, we use all available devices */
        device_infos_ptr = scan_res->data();
        devices_pool_count = static_cast<uint32_t>(scan_res->size());
    }

    for (uint32_t i = 0; i < devices_pool_count; i++) {
        if (devices.size() == params.device_count) {
            break;
        }
        auto pcie_device = PcieDevice::create(device_infos_ptr[i]);
        CHECK_EXPECTED(pcie_device);
        auto status = pcie_device.value()->mark_as_used();
        if ((nullptr == params.device_infos) && (HAILO_DEVICE_IN_USE == status)) {
            // Continue only if the user didnt ask for specific devices
            continue;
        }
        CHECK_SUCCESS_AS_EXPECTED(status);
        devices.emplace_back(pcie_device.release());
    }
    CHECK_AS_EXPECTED(params.device_count == devices.size(), HAILO_OUT_OF_PHYSICAL_DEVICES,
        "Failed to create vdevice. there are not enough free devices. requested: {}, found: {}",
        params.device_count, devices.size());

    return devices;
}

Expected<std::vector<std::unique_ptr<VdmaDevice>>> VDeviceBase::create_core_devices(const hailo_vdevice_params_t &params)
{
    CHECK_AS_EXPECTED(1 == params.device_count, HAILO_OUT_OF_PHYSICAL_DEVICES,
        "Only one core device can exist on the system, given {}", params.device_count);

    auto core_device = CoreDevice::create();
    CHECK_EXPECTED(core_device);

    auto status = core_device.value()->mark_as_used();
    if (status == HAILO_DEVICE_IN_USE) {
        return make_unexpected(HAILO_OUT_OF_PHYSICAL_DEVICES);
    }
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::vector<std::unique_ptr<VdmaDevice>> vdma_devices;
    vdma_devices.push_back(core_device.release());
    return vdma_devices;
}


} /* namespace hailort */
