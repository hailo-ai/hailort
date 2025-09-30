/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "hailo/hailort_defaults.hpp"
#include "hailo/infer_model.hpp"
#include "utils/profiler/tracer_macros.hpp"

#include "vdevice/vdevice_internal.hpp"
#include "vdevice/vdevice_core_op.hpp"
#include "vdevice/vdevice_hrpc_client.hpp"

#include "vdma/pcie/pcie_device.hpp"
#include "vdma/integrated/integrated_device.hpp"
#include "utils/shared_resource_manager.hpp"
#include "network_group/network_group_internal.hpp"
#include "net_flow/pipeline/infer_model_internal.hpp"
#include "core_op/core_op.hpp"
#include "hef/hef_internal.hpp"

#include "common/utils.hpp"

namespace hailort
{

static hailo_status validate_device_ids_match(const hailo_vdevice_params_t &params,
    const std::set<std::string> &old_ids)
{
    const auto group_id_name = (nullptr == params.group_id ? "NULL" : params.group_id);
    CHECK(old_ids.size() == static_cast<size_t>(params.device_count), HAILO_INVALID_OPERATION,
        "VDevice invalid device count for group_id {}", group_id_name);

    for (uint32_t i = 0; i < params.device_count; i++) {
        auto device_id_found = std::find_if(old_ids.begin(), old_ids.end(),
            [&](const std::string &device_id) {
                return Device::device_ids_equal(params.device_ids[i].id, device_id);
        });
        CHECK(device_id_found != old_ids.end(), HAILO_INVALID_OPERATION,
            "Device {} not used by group_id {}", params.device_ids[i].id, group_id_name);
    }

    return HAILO_SUCCESS;
}

static hailo_status validate_same_vdevice(const hailo_vdevice_params_t &params, const VDevice &vdevice)
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

Expected<NetworkGroupsParamsMap> VDevice::create_configure_params(Hef &hef) const
{
    auto stream_interface = get_default_streams_interface();
    CHECK_EXPECTED(stream_interface, "Failed to get default streams interface");

    return hef.create_configure_params(stream_interface.release());
}

Expected<ConfigureNetworkParams> VDevice::create_configure_params(Hef &hef, const std::string &network_group_name) const
{
    auto stream_interface = get_default_streams_interface();
    CHECK_EXPECTED(stream_interface, "Failed to get default streams interface");

    return hef.create_configure_params(stream_interface.release(), network_group_name);
}

hailo_status VDevice::before_fork()
{
    return HAILO_SUCCESS;
}

hailo_status VDevice::after_fork_in_parent()
{
    return HAILO_SUCCESS;
}

hailo_status VDevice::after_fork_in_child()
{
    return HAILO_SUCCESS;
}

VDeviceHandle::VDeviceHandle(const hailo_vdevice_params_t &params, uint32_t handle) :
    VDevice(params), m_handle(handle)
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

    auto handle_vdevice = std::unique_ptr<VDeviceHandle>(new VDeviceHandle(params, expected_handle.value()));
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

Expected<hailo_stream_interface_t> VDeviceHandle::get_default_streams_interface() const
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto vdevice = manager.resource_lookup(m_handle);
    CHECK_EXPECTED(vdevice);

    return vdevice.value()->get_default_streams_interface();
}

Expected<std::shared_ptr<InferModel>> VDeviceHandle::create_infer_model(const std::string &hef_path,
    const std::string &name)
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto vdevice = manager.resource_lookup(m_handle);
    CHECK_EXPECTED(vdevice);

    return vdevice.value()->create_infer_model(hef_path, name);
}

hailo_status VDeviceHandle::dma_map(void *address, size_t size, hailo_dma_buffer_direction_t direction)
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto vdevice = manager.resource_lookup(m_handle);
    CHECK_EXPECTED_AS_STATUS(vdevice);

    return vdevice.value()->dma_map(address, size, direction);
}

hailo_status VDeviceHandle::dma_unmap(void *address, size_t size, hailo_dma_buffer_direction_t direction)
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto vdevice = manager.resource_lookup(m_handle);
    CHECK_EXPECTED_AS_STATUS(vdevice);

    return vdevice.value()->dma_unmap(address, size, direction);
}

hailo_status VDeviceHandle::dma_map_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction)
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto vdevice = manager.resource_lookup(m_handle);
    CHECK_EXPECTED_AS_STATUS(vdevice);

    return vdevice.value()->dma_map_dmabuf(dmabuf_fd, size, direction);
}

hailo_status VDeviceHandle::dma_unmap_dmabuf(int dmabuf_fd, size_t size, hailo_dma_buffer_direction_t direction)
{
    auto &manager = SharedResourceManager<std::string, VDeviceBase>::get_instance();
    auto vdevice = manager.resource_lookup(m_handle);
    CHECK_EXPECTED_AS_STATUS(vdevice);

    return vdevice.value()->dma_unmap_dmabuf(dmabuf_fd, size, direction);
}

Expected<std::unique_ptr<VDevice>> VDevice::create(const hailo_vdevice_params_t &params)
{
    LOGGER__INFO("Creating vdevice with params: device_count: {}, scheduling_algorithm: {}, multi_process_service: {}",
        params.device_count, HailoRTCommon::get_scheduling_algorithm_str(params.scheduling_algorithm), params.multi_process_service);
    auto status = VDeviceBase::validate_params(params);
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::unique_ptr<VDevice> vdevice = nullptr;

    TRY(auto do_device_ids_contain_eth, VDeviceBase::do_device_ids_contain_eth(params));

    if (params.multi_process_service || do_device_ids_contain_eth) {
        TRY(vdevice, VDeviceHrpcClient::create(params));
    } else {
        auto acc_type = HailoRTDriver::AcceleratorType::ACC_TYPE_MAX_VALUE;
        TRY(acc_type, VDeviceBase::get_accelerator_type(params.device_ids, params.device_count));

        if (acc_type == HailoRTDriver::AcceleratorType::SOC_ACCELERATOR) {
            TRY(vdevice, VDeviceHrpcClient::create(params));
        } else {
            TRY(vdevice, VDeviceHandle::create(params));
        }
    }
    // Upcasting to VDevice unique_ptr
    auto vdevice_ptr = std::unique_ptr<VDevice>(vdevice.release());
    return vdevice_ptr;
}

Expected<std::shared_ptr<VDevice>> VDevice::create_shared(const hailo_vdevice_params_t &params)
{
    TRY(std::shared_ptr<VDevice> vdevice, VDevice::create(params));
    return vdevice;
}

Expected<std::unique_ptr<VDevice>> VDevice::create()
{
    auto params = HailoRTDefaults::get_vdevice_params();
    return create(params);
}

Expected<std::shared_ptr<VDevice>> VDevice::create_shared()
{
    TRY(std::shared_ptr<VDevice> vdevice, VDevice::create());
    return vdevice;
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

Expected<std::shared_ptr<VDevice>> VDevice::create_shared(const std::vector<std::string> &device_ids)
{
    TRY(std::shared_ptr<VDevice> vdevice, VDevice::create(device_ids));
    return vdevice;
}

Expected<HailoRTDriver::AcceleratorType> VDeviceBase::get_accelerator_type(hailo_device_id_t *device_ids, size_t device_count)
{
    auto acc_type = HailoRTDriver::AcceleratorType::ACC_TYPE_MAX_VALUE;
    TRY(auto device_infos, HailoRTDriver::scan_devices());
    if (nullptr != device_ids) {
        // device_ids are provided - check that all ids are of the same type + that the id exists in the scan from device_infos
        for (uint32_t i = 0; i < device_count; i++) {
            const auto &id = device_ids[i].id;
            auto device_info = std::find_if(device_infos.begin(), device_infos.end(), [&](const auto &device_info) {
                return Device::device_ids_equal(device_info.device_id, id);
            });
            CHECK(device_info != device_infos.end(), HAILO_INVALID_ARGUMENT,
                "VDevice creation failed. device_id {} not found", id);
            CHECK(acc_type == HailoRTDriver::AcceleratorType::ACC_TYPE_MAX_VALUE || acc_type == device_info->accelerator_type, HAILO_INVALID_ARGUMENT,
                "VDevice creation failed. device_ids of devices with different types are provided (e.g. Hailo8 and Hailo10). Please provide device_ids of the same device types");
            acc_type = device_info->accelerator_type;
        }
    } else {
        // No device_id is provided - check that all devices are of the same type
        for (const auto &device_info : device_infos) {
            CHECK(acc_type == HailoRTDriver::AcceleratorType::ACC_TYPE_MAX_VALUE || acc_type == device_info.accelerator_type, HAILO_INVALID_ARGUMENT,
                "VDevice creation failed. Devices of different types are found and no device_id is provided. Please provide device_ids");
            acc_type = device_info.accelerator_type;
        }
    }
    return acc_type;
}

hailo_status VDeviceBase::validate_params(const hailo_vdevice_params_t &params)
{
    CHECK(0 != params.device_count, HAILO_INVALID_ARGUMENT,
        "VDevice creation failed. invalid device_count ({}).", params.device_count);

    TRY(auto do_device_ids_contain_eth, do_device_ids_contain_eth(params));
    CHECK(!(do_device_ids_contain_eth && (1 != params.device_count)), HAILO_INVALID_ARGUMENT,
        "VDevice over ETH is supported for 1 device. Passed device_count: {}", params.device_count);
    CHECK(!(do_device_ids_contain_eth && params.multi_process_service), HAILO_INVALID_ARGUMENT,
        "Multi process service is only supported with local devices");

    if (params.multi_process_service) {
        auto acc_type = HailoRTDriver::AcceleratorType::ACC_TYPE_MAX_VALUE;
        TRY(acc_type, get_accelerator_type(params.device_ids, params.device_count));
        CHECK(acc_type != HailoRTDriver::AcceleratorType::SOC_ACCELERATOR, HAILO_INVALID_OPERATION,
            "Multi process service is supported only on Hailo15 devices. Other devices support multi-process functionality without requiring a service");
    }

    return HAILO_SUCCESS;
}

Expected<std::unique_ptr<VDeviceBase>> VDeviceBase::create(const hailo_vdevice_params_t &params)
{
    TRACE(InitProfilerProtoTrace);
    auto unique_vdevice_hash = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    TRACE(MonitorStartTrace, unique_vdevice_hash);

    auto devices_expected = create_devices(params);
    CHECK_EXPECTED(devices_expected);
    auto devices = devices_expected.release();

    std::vector<std::string> device_ids;
    device_ids.reserve(params.device_count);
    std::vector<std::string> device_archs;
    device_archs.reserve(params.device_count);

    std::string vdevice_ids = "VDevice Infos:";
    for (const auto &pair : devices) {
        auto &device = pair.second;
        auto id_info_str = device->get_dev_id();
        device_ids.emplace_back(id_info_str);
        auto device_arch = device->get_architecture();
        CHECK_EXPECTED(device_arch);
        auto device_arch_str = HailoRTCommon::get_device_arch_str(device_arch.value());
        device_archs.emplace_back(device_arch_str);
        vdevice_ids += " " + std::string(id_info_str);
        TRACE(AddDeviceTrace, id_info_str, device_arch_str);
    }
    LOGGER__INFO("{}", vdevice_ids);

    CoreOpsSchedulerPtr scheduler_ptr;
    if (HAILO_SCHEDULING_ALGORITHM_NONE != params.scheduling_algorithm) {
        if (HAILO_SCHEDULING_ALGORITHM_ROUND_ROBIN == params.scheduling_algorithm) {
            auto core_ops_scheduler = CoreOpsScheduler::create_round_robin(device_ids, device_archs);
            CHECK_EXPECTED(core_ops_scheduler);
            scheduler_ptr = core_ops_scheduler.release();
        } else {
            LOGGER__ERROR("Unsupported scheduling algorithm");
            return make_unexpected(HAILO_INVALID_ARGUMENT);
        }
    }

    auto vdevice = std::unique_ptr<VDeviceBase>(new (std::nothrow) VDeviceBase(params, std::move(devices), scheduler_ptr, unique_vdevice_hash));
    CHECK_AS_EXPECTED(nullptr != vdevice, HAILO_OUT_OF_HOST_MEMORY);

    return vdevice;
}

VDeviceBase::~VDeviceBase()
{
    if (m_core_ops_scheduler) {
        // The scheduler is held as weak/shared ptr, so it may not be freed by this destructor implicitly.
        // The scheduler will be freed when the last reference is freed. If it will be freed inside some interrupt
        // dispatcher thread (which holds a reference to the shared ptr) we will get stuck since the scheduler
        // destructor will activate all core ops (and waits for the interrupt dispatcher).
        // To solve it, we manually shutdown the scheduler here to make sure all devices have no activated core op and
        // all interrupt dispatcher threads are idle.
        m_core_ops_scheduler->shutdown();
    }
    TRACE(DumpProfilerStateTrace);
    TRACE(MonitorEndTrace, m_unique_vdevice_hash);
}

Expected<ConfiguredNetworkGroupVector> VDeviceBase::configure(Hef &hef,
    const NetworkGroupsParamsMap &configure_params)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto start_time = std::chrono::steady_clock::now();

    auto local_config_params = create_local_config_params(hef, configure_params);
    CHECK_EXPECTED(local_config_params);

    ConfiguredNetworkGroupVector added_network_groups;
    added_network_groups.reserve(configure_params.size());

    for (const auto &network_params_pair : local_config_params.value()) {
        std::vector<std::shared_ptr<CoreOp>> core_ops;
        const bool use_multiplexer = should_use_multiplexer();

        std::shared_ptr<VDeviceCoreOp> identical_core_op = nullptr;
        if (use_multiplexer) {
            for (auto &network_group : m_vdevice_core_ops) {
                if (network_group->equals(hef, network_params_pair)) {
                    identical_core_op = network_group;
                    break;
                }
            }
        }
        std::shared_ptr<VDeviceCoreOp> vdevice_core_op = nullptr;
        if (identical_core_op) {
            auto vdevice_core_op_exp = VDeviceCoreOp::duplicate(identical_core_op, network_params_pair.second);
            CHECK_EXPECTED(vdevice_core_op_exp);
            vdevice_core_op = vdevice_core_op_exp.release();
        } else {
            auto vdevice_core_op_exp = create_vdevice_core_op(hef, network_params_pair);
            CHECK_EXPECTED(vdevice_core_op_exp);
            vdevice_core_op = vdevice_core_op_exp.release();
            m_vdevice_core_ops.emplace_back(vdevice_core_op);
        }

        if (m_core_ops_scheduler) {
            auto status = m_core_ops_scheduler->add_core_op(vdevice_core_op->core_op_handle(), vdevice_core_op);
            CHECK_SUCCESS_AS_EXPECTED(status);

            // On scheduler, the streams are always activated
            for (auto &input : vdevice_core_op->get_input_streams()) {
                status = dynamic_cast<InputStreamBase&>(input.get()).activate_stream();
                CHECK_SUCCESS_AS_EXPECTED(status);
            }

            for (auto &output : vdevice_core_op->get_output_streams()) {
                status = dynamic_cast<OutputStreamBase&>(output.get()).activate_stream();
                CHECK_SUCCESS_AS_EXPECTED(status);
            }
        }

        core_ops.push_back(vdevice_core_op);
        auto metadata = hef.pimpl->network_group_metadata(vdevice_core_op->name());
        auto net_group_expected = ConfiguredNetworkGroupBase::create(network_params_pair.second, std::move(core_ops), std::move(metadata));
        CHECK_EXPECTED(net_group_expected);
        auto network_group_ptr = net_group_expected.release();

        added_network_groups.push_back(network_group_ptr);
    }

    auto elapsed_time_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_time).count();
    LOGGER__INFO("Configuring HEF on VDevice took {} milliseconds", elapsed_time_ms);

    return added_network_groups;
}

Expected<std::shared_ptr<InferModel>> VDevice::create_infer_model(const std::string &hef_path, const std::string &name)
{
    TRY(auto hef, Hef::create(hef_path));
    return create_infer_model(hef, name);
}

Expected<std::shared_ptr<InferModel>> VDevice::create_infer_model(const MemoryView hef_buffer, const std::string &name)
{
    TRY(auto hef, Hef::create(hef_buffer));
    return create_infer_model(hef, name);
}

Expected<std::shared_ptr<InferModel>> VDevice::create_infer_model(std::shared_ptr<Buffer> hef_buffer, const std::string &name)
{
    TRY(auto hef, Hef::create(hef_buffer));
    return create_infer_model(hef, name);
}

Expected<std::shared_ptr<InferModel>> VDevice::create_infer_model(Hef hef, const std::string &name)
{
    TRY(auto infer_model_base, InferModelBase::create(*this, hef, name));
    return std::shared_ptr<InferModel>(std::move(infer_model_base));
}

Expected<hailo_stream_interface_t> VDeviceBase::get_default_streams_interface() const
{
    auto stream_interface = m_devices.begin()->second.get()->get_default_streams_interface();
    CHECK_EXPECTED(stream_interface);
    for (const auto &pair : m_devices) {
        auto &dev = pair.second;
        auto current_stream_interface = dev->get_default_streams_interface();
        CHECK_EXPECTED(current_stream_interface);
        CHECK_AS_EXPECTED(*current_stream_interface == *stream_interface, HAILO_INTERNAL_FAILURE,
            "vDevice is supported only with homogeneous device type");
    }
    return stream_interface.release();
}

Expected<std::map<device_id_t, std::unique_ptr<Device>>> VDeviceBase::create_devices(const hailo_vdevice_params_t &params)
{
    std::map<device_id_t, std::unique_ptr<Device>> devices;

    const bool user_specific_devices = (params.device_ids != nullptr);

    auto device_ids = get_device_ids(params);
    CHECK_EXPECTED(device_ids);

    for (const auto &device_id : device_ids.value()) {
        if (devices.size() == params.device_count) {
            break;
        }
        auto device = Device::create(device_id);
        CHECK_EXPECTED(device);

        // Validate That if (device_count != 1), device arch is not H8L. May be changed in SDK-28729
        if (1 != params.device_count) {
            auto device_arch = device.value()->get_architecture();
            CHECK_EXPECTED(device_arch);
            CHECK_AS_EXPECTED(HAILO_ARCH_HAILO8L != device_arch.value(), HAILO_INVALID_OPERATION,
                "VDevice with multiple devices is not supported on HAILO_ARCH_HAILO8L. device {} is HAILO_ARCH_HAILO8L", device_id);
            CHECK_AS_EXPECTED(HAILO_ARCH_HAILO15M != device_arch.value(), HAILO_INVALID_OPERATION,
                "VDevice with multiple devices is not supported on HAILO_ARCH_HAILO15M. device {} is HAILO_ARCH_HAILO15M", device_id);
            CHECK_AS_EXPECTED(HAILO_ARCH_HAILO10H != device_arch.value(), HAILO_INVALID_OPERATION,
                "VDevice with multiple devices is not supported on HAILO_ARCH_HAILO10H. device {} is HAILO_ARCH_HAILO10H", device_id);
        }

        auto dev_type = Device::get_device_type(device_id);
        CHECK_EXPECTED(dev_type);
        if ((Device::Type::INTEGRATED == dev_type.value()) || (Device::Type::PCIE == dev_type.value())) {
            auto status = dynamic_cast<VdmaDevice&>(*device.value()).mark_as_used();
            if (!user_specific_devices && (HAILO_DEVICE_IN_USE == status)) {
                // Continue only if the user didn't ask for specific devices
                continue;
            }
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
        devices[device_id] = device.release();
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
    } else {
        std::vector<std::string> device_ids;
        device_ids.reserve(params.device_count);

        for (size_t i = 0; i < params.device_count; i++) {
            device_ids.emplace_back(StringUtils::to_lower(params.device_ids[i].id));
        }

        return device_ids;
    }
}

Expected<NetworkGroupsParamsMap> VDeviceBase::create_local_config_params(Hef &hef, const NetworkGroupsParamsMap &configure_params)
{
    for (const auto &pair : m_devices) {
        auto &device = pair.second;
        auto status = dynamic_cast<DeviceBase&>(*device).check_hef_is_compatible(hef);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    auto local_config_params = configure_params;
    if (local_config_params.empty()) {
        // All stream iface should be the same
        auto config_params_exp = m_devices.begin()->second->create_configure_params(hef);
        CHECK_EXPECTED(config_params_exp);
        local_config_params = config_params_exp.release();
    }

    for (auto &ng_params_pair : local_config_params) {
        if (m_core_ops_scheduler) {
            // Validate batch size is identical for all networks in case scheduler is enabled.
            uint16_t ref_batch_size = UINT16_MAX;
            for (const auto &network_params_pair : ng_params_pair.second.network_params_by_name) {
                if (UINT16_MAX == ref_batch_size) {
                    ref_batch_size = network_params_pair.second.batch_size;
                }
                CHECK_AS_EXPECTED(ref_batch_size == network_params_pair.second.batch_size, HAILO_INVALID_OPERATION,
                    "When scheduler is enabled, all networks should have the same batch_size. "
                    "configure_params contains {} and {}. "
                    "To disable scheduler, set HAILO_SCHEDULING_ALGORITHM_NONE in VDevice creation.", ref_batch_size,
                    network_params_pair.second.batch_size);
            }
        }

        // Validate batch size (network group batch size vs network batch size).
        auto status = Hef::Impl::update_network_batch_size(ng_params_pair.second);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    return local_config_params;
}

Expected<std::shared_ptr<CoreOp>> VDeviceBase::create_physical_core_op(Device &device, Hef &hef,
    const std::string &core_op_name, const ConfigureNetworkParams &params)
{
    ConfigureNetworkParams params_copy = params;
    if (m_core_ops_scheduler) {
        // When the scheduler is enabled, all low level streams must be async (even if the user uses sync API).
        for (auto &stream_params : params_copy.stream_params_by_name) {
            stream_params.second.flags |= HAILO_STREAM_FLAGS_ASYNC;
        }
    }

    auto ng_vector = device.configure(hef, { std::make_pair(core_op_name, params_copy) });
    CHECK_EXPECTED(ng_vector);

    assert(1 == ng_vector->size());
    auto &network_group_base = dynamic_cast<ConfiguredNetworkGroupBase&>(*ng_vector.value()[0]);

    auto networks_info = network_group_base.get_network_infos();
    CHECK_EXPECTED(networks_info);
    if (m_core_ops_scheduler && (networks_info->size() > 1)) {
        LOGGER__WARNING("Configuring '{}' which is a multi-networks model with scheduler enabled."
            " The model will be scheduled only when all inputs and outputs of the network group will be ready",
            core_op_name);
    }

    auto ng_core_ops = network_group_base.get_core_ops();
    CHECK_AS_EXPECTED(ng_core_ops.size() == 1, HAILO_NOT_IMPLEMENTED,
        "Only one core op for network group is supported");

    auto core_op = ng_core_ops[0];
    return core_op;
}

Expected<std::shared_ptr<VDeviceCoreOp>> VDeviceBase::create_vdevice_core_op(Hef &hef,
    const std::pair<const std::string, ConfigureNetworkParams> &params)
{
    std::map<device_id_t, std::shared_ptr<CoreOp>> physical_core_ops;

	for (const auto &device : m_devices) {
        auto physical_core_op = create_physical_core_op(*device.second, hef, params.first, params.second);
        CHECK_EXPECTED(physical_core_op);
        physical_core_ops.emplace(device.first, physical_core_op.release());
    }

    auto core_op_handle = allocate_core_op_handle();

    return VDeviceCoreOp::create(*this, m_active_core_op_holder, params.second, physical_core_ops,
        m_core_ops_scheduler, core_op_handle, hef.hash());
}

vdevice_core_op_handle_t VDeviceBase::allocate_core_op_handle()
{
    return m_next_core_op_handle++;
}

bool VDeviceBase::should_use_multiplexer()
{
    if (!m_core_ops_scheduler) {
        return false;
    }

    auto is_disabled_by_user = is_env_variable_on(DISABLE_MULTIPLEXER_ENV_VAR);
    if (is_disabled_by_user) {
        LOGGER__WARNING("Usage of '{}' env variable is deprecated.", DISABLE_MULTIPLEXER_ENV_VAR);
    }
    return !is_disabled_by_user;
}

Expected<bool> VDeviceBase::do_device_ids_contain_eth(const hailo_vdevice_params_t &params)
{
    if (params.device_ids != nullptr) {
        for (uint32_t i = 0; i < params.device_count; i++) {
            TRY(auto dev_type, Device::get_device_type(params.device_ids[i].id));
            if (Device::Type::ETH == dev_type) {
                return true;
            }
        }
    }
    return false; // in case no device_ids were provided, we assume there's no ETH device
}

} /* namespace hailort */
