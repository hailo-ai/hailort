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
#include "shared_resource_manager.hpp"
#include "context_switch/network_group_internal.hpp"

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "rpc_client_utils.hpp"
#include "rpc/rpc_definitions.hpp"
#endif // HAILO_SUPPORT_MULTI_PROCESS

namespace hailort
{

template<>
std::string SharedResourceManager<std::string, VDeviceBase>::unique_key()
{
    return HAILO_UNIQUE_VDEVICE_GROUP_ID;
}

static hailo_status validate_device_ids_match(const hailo_vdevice_params_t &params,
    const std::set<std::string> &old_ids)
{
    std::set<std::string> new_ids;
    for (uint32_t i = 0; i < params.device_count; i++) {
        // TODO: maybe needs to normalize domain?
        new_ids.insert(params.device_ids[i].id);
    }

    CHECK(old_ids == new_ids, HAILO_INVALID_OPERATION, "Different VDevice ids used by group_id {}", (nullptr == params.group_id ? "NULL" : params.group_id));
    return HAILO_SUCCESS;
}

hailo_status validate_same_vdevice(const hailo_vdevice_params_t &params, const VDevice &vdevice)
{
    // Validate device ids
    if (params.device_ids != nullptr) {
        auto old_ids = vdevice.get_physical_devices_ids();
        CHECK_EXPECTED_AS_STATUS(old_ids);
        std::set<std::string> old_ids_set(old_ids->begin(), old_ids->end());

        auto status = validate_device_ids_match(params, old_ids_set);
        CHECK_SUCCESS(status);
    }

    // Validate count matches
    auto physical_devices = vdevice.get_physical_devices();
    CHECK_EXPECTED_AS_STATUS(physical_devices);
    CHECK(params.device_count == physical_devices->size(), HAILO_INVALID_OPERATION,
        "Different VDevice device count used by group_id {}", params.group_id);
    return HAILO_SUCCESS;
}

void release_resource_if(bool condition, uint32_t key) {
    if (condition) {
        SharedResourceManager<std::string, VDeviceBase>::get_instance().release_resource(key);
    }
}

VDeviceHandle::VDeviceHandle(uint32_t handle) : m_handle(handle)
{}

VDeviceHandle::~VDeviceHandle()
{
    SharedResourceManager<std::string, VDeviceBase>::get_instance().release_resource(m_handle);
}

Expected<std::unique_ptr<VDevice>> VDeviceHandle::create(const hailo_vdevice_params_t &params)
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto create = [&params]() {
        return VDeviceBase::create(params);
    };
    auto expected_handle = manager.register_resource(params.group_id == nullptr ? "" : std::string(params.group_id), create);
    CHECK_EXPECTED(expected_handle);

    auto expected_vdevice_base = manager.resource_lookup(expected_handle.value());
    CHECK_EXPECTED(expected_vdevice_base);

    auto same_vdevice_status = validate_same_vdevice(params, *expected_vdevice_base.value());
    release_resource_if(same_vdevice_status != HAILO_SUCCESS, expected_handle.value());
    CHECK_SUCCESS_AS_EXPECTED(same_vdevice_status);

    auto handle_vdevice = std::unique_ptr<VDeviceHandle>(new VDeviceHandle(expected_handle.value()));
    CHECK_AS_EXPECTED(handle_vdevice != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<VDevice>(std::move(handle_vdevice));
}

Expected<ConfiguredNetworkGroupVector> VDeviceHandle::configure(Hef &hef,
    const NetworkGroupsParamsMap &configure_params)
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto vdevice = manager.resource_lookup(m_handle);
    CHECK_EXPECTED(vdevice);

    return vdevice.value()->configure(hef, configure_params);
}

Expected<std::vector<std::reference_wrapper<Device>>> VDeviceHandle::get_physical_devices() const
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto vdevice = manager.resource_lookup(m_handle);
    CHECK_EXPECTED(vdevice);

    return vdevice.value()->get_physical_devices();
}

Expected<std::vector<std::string>> VDeviceHandle::get_physical_devices_ids() const
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto vdevice = manager.resource_lookup(m_handle);
    CHECK_EXPECTED(vdevice);

    return vdevice.value()->get_physical_devices_ids();
}

#ifdef HAILO_SUPPORT_MULTI_PROCESS

VDeviceClient::VDeviceClient(std::unique_ptr<HailoRtRpcClient> client, uint32_t handle)
    : m_client(std::move(client))
    , m_handle(handle)
{}

VDeviceClient::~VDeviceClient()
{
    auto reply = m_client->VDevice_release(m_handle);
    if (reply != HAILO_SUCCESS) {
        LOGGER__CRITICAL("VDevice_release failed!");
    }
}

Expected<std::unique_ptr<VDevice>> VDeviceClient::create(const hailo_vdevice_params_t &params)
{
    grpc::ChannelArguments ch_args;
    ch_args.SetMaxReceiveMessageSize(-1);
    auto channel = grpc::CreateCustomChannel(HAILO_DEFAULT_UDS_ADDR, grpc::InsecureChannelCredentials(), ch_args);
    CHECK_AS_EXPECTED(channel != nullptr, HAILO_INTERNAL_FAILURE);

    auto client = std::unique_ptr<HailoRtRpcClient>(new HailoRtRpcClient(channel));
    CHECK_AS_EXPECTED(client != nullptr, HAILO_OUT_OF_HOST_MEMORY);
    auto init_status = HailoRtRpcClientUtils::get_instance().init_client_service_communication();
    CHECK_SUCCESS_AS_EXPECTED(init_status);
    auto reply = client->VDevice_create(params, getpid());
    CHECK_EXPECTED(reply);

    auto client_vdevice = std::unique_ptr<VDeviceClient>(new VDeviceClient(std::move(client), reply.value()));
    CHECK_AS_EXPECTED(client_vdevice != nullptr, HAILO_OUT_OF_HOST_MEMORY);

    return std::unique_ptr<VDevice>(std::move(client_vdevice));
}

Expected<ConfiguredNetworkGroupVector> VDeviceClient::configure(Hef &hef,
    const NetworkGroupsParamsMap &configure_params)
{
    auto networks_handles = m_client->VDevice_configure(m_handle, hef, getpid(), configure_params);
    CHECK_EXPECTED(networks_handles);

    ConfiguredNetworkGroupVector networks;
    networks.reserve(networks_handles->size());
    for (auto &handle : networks_handles.value()) {
        auto channel = grpc::CreateChannel(HAILO_DEFAULT_UDS_ADDR, grpc::InsecureChannelCredentials());
        CHECK_AS_EXPECTED(channel != nullptr, HAILO_INTERNAL_FAILURE);

        auto client = std::unique_ptr<HailoRtRpcClient>(new HailoRtRpcClient(channel));
        networks.emplace_back(make_shared_nothrow<ConfiguredNetworkGroupClient>(std::move(client), handle));
    }
    return networks;
}

Expected<std::vector<std::reference_wrapper<Device>>> VDeviceClient::get_physical_devices() const
{
    // TODO: HRT-6606
    LOGGER__ERROR("get_physical_devices is not supported when using multi process service");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<std::vector<std::string>> VDeviceClient::get_physical_devices_ids() const
{
    return m_client->VDevice_get_physical_devices_ids(m_handle);
}

#endif // HAILO_SUPPORT_MULTI_PROCESS

static Expected<std::vector<hailo_device_id_t>> convert_device_infos_to_ids(const hailo_vdevice_params_t &params)
{
IGNORE_DEPRECATION_WARNINGS_BEGIN
    assert(params.device_infos != nullptr);

    std::vector<hailo_device_id_t> device_ids;
    for (uint32_t i = 0; i < params.device_count; i++) {
        auto device_id_str = Device::pcie_device_info_to_string(params.device_infos[i]);
        CHECK_EXPECTED(device_id_str);

        auto device_id_struct = HailoRTCommon::to_device_id(device_id_str.value());
        CHECK_EXPECTED(device_id_struct);

        device_ids.push_back(device_id_struct.release());
    }

    return device_ids;
IGNORE_DEPRECATION_WARNINGS_END
}

Expected<std::unique_ptr<VDevice>> VDevice::create(const hailo_vdevice_params_t &params)
{
    CHECK_AS_EXPECTED(0 != params.device_count, HAILO_INVALID_ARGUMENT,
        "VDevice creation failed. invalid device_count ({}).", params.device_count);

    CHECK_AS_EXPECTED((HAILO_SCHEDULING_ALGORITHM_NONE == params.scheduling_algorithm)
            || (1 == params.device_count), HAILO_INVALID_ARGUMENT,
        "Network group scheduler can be active only when using one device in the vDevice!");

    // Convert device_infos to device_ids
IGNORE_DEPRECATION_WARNINGS_BEGIN
    // Save device_ids on this scope to guard the memory.
    auto local_params = params;
    std::vector<hailo_device_id_t> device_ids;
    if (local_params.device_infos != nullptr) {
        CHECK_AS_EXPECTED(local_params.device_ids == nullptr, HAILO_INVALID_ARGUMENT,
            "Invalid vdevice params. You can set either device_ids or device_infos");
        LOGGER__WARN("Passing 'device_infos' in 'hailo_vdevice_params_t' is deprecated. One should use 'device_ids'");

        auto device_ids_expected = convert_device_infos_to_ids(local_params);
        CHECK_EXPECTED(device_ids_expected);

        device_ids = device_ids_expected.release();
        local_params.device_ids = device_ids.data();
        local_params.device_infos = nullptr;
    }
IGNORE_DEPRECATION_WARNINGS_END

    std::unique_ptr<VDevice> vdevice;
    if (local_params.multi_process_service) {
#ifdef HAILO_SUPPORT_MULTI_PROCESS
        auto expected_vdevice = VDeviceClient::create(local_params);
        CHECK_EXPECTED(expected_vdevice);
        vdevice = expected_vdevice.release();
#else
        LOGGER__ERROR("multi_process_service requires service compilation with HAILO_BUILD_SERVICE");
        return make_unexpected(HAILO_INVALID_OPERATION);
#endif // HAILO_SUPPORT_MULTI_PROCESS
    } else {
        auto expected_vdevice = VDeviceHandle::create(local_params);
        CHECK_EXPECTED(expected_vdevice);
        vdevice = expected_vdevice.release();
    }
    // Upcasting to VDevice unique_ptr
    auto vdevice_ptr = std::unique_ptr<VDevice>(vdevice.release());
    return vdevice_ptr;
}

Expected<std::unique_ptr<VDevice>> VDevice::create()
{
    auto params = HailoRTDefaults::get_vdevice_params();
    return create(params);
}

Expected<std::vector<hailo_pcie_device_info_t>> VDevice::get_physical_devices_infos() const
{
    LOGGER__WARN("VDevice get_physical_devices_infos() is deprecated. one should use get_physical_devices_ids().");
    auto device_ids = get_physical_devices_ids();
    CHECK_EXPECTED(device_ids);

    std::vector<hailo_pcie_device_info_t> device_infos;
    device_infos.reserve(device_ids->size());
    for (const auto &device_id : device_ids.value()) {
        auto device_info = Device::parse_pcie_device_info(device_id);
        CHECK_EXPECTED(device_info);
        device_infos.emplace_back(device_info.release());
    }

    return device_infos;
}

Expected<std::unique_ptr<VDevice>> VDevice::create(const std::vector<std::string> &device_ids)
{
    auto params = HailoRTDefaults::get_vdevice_params();

    auto device_ids_vector = HailoRTCommon::to_device_ids_vector(device_ids);
    CHECK_EXPECTED(device_ids_vector);

    params.device_ids = device_ids_vector->data();
    params.device_count = static_cast<uint32_t>(device_ids_vector->size());

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
    std::unique_lock<std::mutex> lock(m_mutex);
    auto start_time = std::chrono::steady_clock::now();
    if (!m_context_switch_manager) {
        auto local_context_switch_manager = VdmaConfigManager::create(*this);
        CHECK_EXPECTED(local_context_switch_manager);
        m_context_switch_manager = make_unique_nothrow<VdmaConfigManager>(local_context_switch_manager.release());
        CHECK_AS_EXPECTED(nullptr != m_context_switch_manager, HAILO_OUT_OF_HOST_MEMORY);
    }

    for (auto &device : m_devices) {
        auto status = device->check_hef_is_compatible(hef);
        CHECK_SUCCESS_AS_EXPECTED(status);
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
    std::vector<std::unique_ptr<VdmaDevice>> devices;
    devices.reserve(params.device_count);

    const bool user_specific_devices = (params.device_ids != nullptr);

    auto device_ids = get_device_ids(params);
    CHECK_EXPECTED(device_ids);

    for (const auto &device_id : device_ids.value()) {
        if (devices.size() == params.device_count) {
            break;
        }
        auto device = VdmaDevice::create(device_id);
        CHECK_EXPECTED(device);

        // Validate That if (device_count != 1), device arch is not H8L. May be changed in SDK-28729
        if (1 != params.device_count) {
            auto device_arch = device.value()->get_architecture();
            CHECK_EXPECTED(device_arch);
            CHECK_AS_EXPECTED(HAILO_ARCH_HAILO8L != device_arch.value(), HAILO_INVALID_OPERATION,
                "VDevice with multiple devices is not supported on HAILO_ARCH_HAILO8L. device {} is HAILO_ARCH_HAILO8L", device_id);
        }

        auto status = device.value()->mark_as_used();
        if (!user_specific_devices && (HAILO_DEVICE_IN_USE == status)) {
            // Continue only if the user didn't ask for specific devices
            continue;
        }
        CHECK_SUCCESS_AS_EXPECTED(status);
        devices.emplace_back(device.release());
    }
    CHECK_AS_EXPECTED(params.device_count == devices.size(), HAILO_OUT_OF_PHYSICAL_DEVICES,
        "Failed to create vdevice. there are not enough free devices. requested: {}, found: {}",
        params.device_count, devices.size());

    return devices;
}

Expected<std::vector<std::string>> VDeviceBase::get_device_ids(const hailo_vdevice_params_t &params)
{
    if (params.device_ids == nullptr) {
        // Use device scan pool
        return Device::scan();
    }
    else {
        std::vector<std::string> device_ids;
        device_ids.reserve(params.device_count);

        for (size_t i = 0; i < params.device_count; i++) {
            device_ids.emplace_back(params.device_ids[i].id);
        }

        return device_ids;
    }
}


} /* namespace hailort */
