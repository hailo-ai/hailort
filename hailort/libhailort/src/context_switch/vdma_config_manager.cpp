
// https://github.com/protocolbuffers/protobuf/tree/master/cmake#notes-on-compiler-warnings
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "hef.pb.h"
#if defined(_MSC_VER)
#pragma warning( pop ) 
#else
#pragma GCC diagnostic pop
#endif

#include "context_switch/multi_context/vdma_config_manager.hpp"
#include "network_group_internal.hpp"
#include "hailo/hailort.h"
#include "common/utils.hpp"
#include "hailo/device.hpp"
#include "hailo/vdevice.hpp"
#include "hailo/hef.hpp"
#include "control.hpp"
#include "hailort_defaults.hpp"
#include "vdevice_internal.hpp"
#include "pipeline_multiplexer.hpp"

#include "pcie_device.hpp"

#include <new>
#include <algorithm>

namespace hailort
{

Expected<VdmaConfigManager> VdmaConfigManager::create(VdmaDevice &device)
{
    const bool is_vdevice = false;
    std::vector<std::reference_wrapper<VdmaDevice>> devices;
    devices.push_back(device);

    auto empty_weak_ptr = NetworkGroupSchedulerWeakPtr();
    hailo_status status = HAILO_UNINITIALIZED;
    VdmaConfigManager manager(std::move(devices), is_vdevice, empty_weak_ptr, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return manager;
}

Expected<VdmaConfigManager> VdmaConfigManager::create(VDeviceBase &vdevice)
{
    const bool is_vdevice = true;
    auto devices = vdevice.get_physical_devices();
    CHECK_EXPECTED(devices);

    const auto device_type = vdevice.get_device_type();
    CHECK_EXPECTED(device_type);

    // Down casting Device to VdmaDevice
    std::vector<std::reference_wrapper<VdmaDevice>> vdma_devices;
    for (auto &dev : devices.release()) {
        assert(device_type.value() == dev.get().get_type());
        vdma_devices.emplace_back(static_cast<VdmaDevice&>(dev.get()));
    }

    hailo_status status = HAILO_UNINITIALIZED;
    VdmaConfigManager manager(std::move(vdma_devices), is_vdevice, vdevice.network_group_scheduler(), status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return manager;
}

VdmaConfigManager::VdmaConfigManager(std::vector<std::reference_wrapper<VdmaDevice>> &&devices,
                                     bool is_vdevice,
                                     NetworkGroupSchedulerWeakPtr network_group_scheduler,
                                     hailo_status &status) :
    m_devices(std::move(devices)),
    m_net_groups(),
    m_net_group_wrappers(),
    m_is_vdevice(is_vdevice),
    m_network_group_scheduler(network_group_scheduler)
{
    for (auto &device : m_devices) {
        // TODO: Do we need this control after fixing HRT-7519?
        // Reset context_switch state machine - it may have been in an active state if the previous VdmaConfigManager
        // wasn't dtor'd (due to SIGKILL for example)
        static const auto REMOVE_NN_CONFIG_DURING_RESET = false;
        status = Control::reset_context_switch_state_machine(device.get(), REMOVE_NN_CONFIG_DURING_RESET);
        if (HAILO_SUCCESS != status) {
            return;
        }

        // Remove the previously configured network groups
        status = Control::clear_configured_apps(device.get());
        if (HAILO_SUCCESS != status) {
            return;
        }
    }

    status = HAILO_SUCCESS;
}

VdmaConfigManager::~VdmaConfigManager()
{
    for (auto &device : m_devices) {
        // Remove the previously configured network groups
        const auto status = Control::clear_configured_apps(device.get());
        if (HAILO_SUCCESS != status) {
           LOGGER__ERROR("Failed to clear conigured network groups with status {}", status);
        }

        // Note: We don't call Control::reset_context_switch_state_machine in the dtor, since this isn't a resource
        //       managed by this class. The call to Control::reset_context_switch_state_machine in the ctor is
        //       present only due to possiblly active fw state machine (leaked if VdmaConfigActivatedNetworkGroup
        //       wasn't dtor'd, due to SIGKILL for example)
    }
}

Expected<std::shared_ptr<ConfiguredNetworkGroup>> VdmaConfigManager::create_configured_network_group(
    std::shared_ptr<NetworkGroupMetadata> network_group_metadata,
    const ProtoHEFNetworkGroup &network_group_proto, Hef &hef, const ConfigureNetworkParams &config_params,
    uint8_t network_group_index, bool &was_hef_already_configured)
{
    auto status = Hef::Impl::validate_net_group_unique_layer_names(network_group_proto);
    CHECK_SUCCESS_AS_EXPECTED(status);

    std::shared_ptr<VdmaConfigNetworkGroup> identical_network_group = nullptr;
    std::vector<std::shared_ptr<ResourcesManager>> resources_managers;
    bool should_create_resources_managers = true;

    auto network_group_scheduler = m_network_group_scheduler.lock();

    /* Validate batch size is identical for all networks in case scheduler is enabled */
    uint16_t ref_batch_size = UINT16_MAX;
    if (network_group_scheduler) {
        for (const auto &network_params_pair : config_params.network_params_by_name) {
            if (UINT16_MAX == ref_batch_size) {
                ref_batch_size = network_params_pair.second.batch_size;
            }
            CHECK_AS_EXPECTED(ref_batch_size == network_params_pair.second.batch_size, HAILO_INVALID_OPERATION,
                "When scheduler is enabled, all networks should have the same batch_size. configure_params contains {} and {}. " \
                "To disable scheduler, set HAILO_SCHEDULING_ALGORITHM_NONE in VDevice creation.", ref_batch_size, network_params_pair.second.batch_size);
        }
    }

    if (m_is_vdevice && network_group_scheduler && PipelineMultiplexer::should_use_multiplexer()) {
        for (auto &network_group : m_net_groups) {
            if (network_group->equals(hef, network_group_metadata->network_group_name())) {
                identical_network_group = network_group;
                LOGGER__INFO("Network group {} was already configured. Using its resources instead of creating new ones...",
                    network_group_metadata->network_group_name());
                break;
            }
        }

        if (nullptr != identical_network_group) {
            should_create_resources_managers = false;
            resources_managers = identical_network_group->get_resources_managers();
            was_hef_already_configured = true;
            if (config_params != identical_network_group->get_config_params()) {
                LOGGER__WARNING("Configured network group was already configured but has different parameters which will not take effect!");
            }
        }
    }

    if (should_create_resources_managers) {
        /* build HEF supported features */
        for (auto device : m_devices) {
            auto resource_manager = Hef::Impl::create_resources_manager(network_group_proto, network_group_index,
                device.get(), device.get().get_driver(), config_params, network_group_metadata, hef.pimpl->get_device_arch());
            CHECK_EXPECTED(resource_manager);
            resources_managers.push_back(resource_manager.release());
        }
    }

    auto net_group = VdmaConfigNetworkGroup::create(m_active_net_group_holder, config_params,
        resources_managers, hef.hash(), network_group_metadata, m_network_group_scheduler);

    auto net_group_ptr = make_shared_nothrow<VdmaConfigNetworkGroup>(net_group.release());
    CHECK_AS_EXPECTED(nullptr != net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);

    // TODO: move this func into VdmaConfigNetworkGroup c'tor
    if (m_is_vdevice) {
        if (network_group_scheduler && (nullptr != identical_network_group)) {
            status = net_group_ptr->create_vdevice_streams_from_duplicate(identical_network_group);
            CHECK_SUCCESS_AS_EXPECTED(status);

            net_group_ptr->set_network_group_handle(identical_network_group->network_group_handle());
        } else {
            auto network_group_handle = INVALID_NETWORK_GROUP_HANDLE;
            if (network_group_scheduler) {
                auto network_group_handle_exp = network_group_scheduler->add_network_group(net_group_ptr);
                CHECK_EXPECTED(network_group_handle_exp);

                network_group_handle = network_group_handle_exp.value();
                net_group_ptr->set_network_group_handle(network_group_handle);
            }

            auto multiplexer = make_shared_nothrow<PipelineMultiplexer>();
            CHECK_AS_EXPECTED(nullptr != multiplexer, HAILO_OUT_OF_HOST_MEMORY, "Failed to create PipelineMultiplexer");

            status = net_group_ptr->create_vdevice_streams_from_config_params(multiplexer, network_group_handle);
            CHECK_SUCCESS_AS_EXPECTED(status);

            m_net_groups.emplace_back(net_group_ptr);
        }
    } else {
        status = net_group_ptr->create_streams_from_config_params(net_group_ptr->get_resources_managers()[0]->get_device());
        CHECK_SUCCESS_AS_EXPECTED(status);

        m_net_groups.emplace_back(net_group_ptr);
    }

    // Check that all boundary streams were created
    status = validate_boundary_streams_were_created(hef, network_group_metadata->network_group_name(), *net_group_ptr);
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto net_group_wrapper = ConfiguredNetworkGroupWrapper::create(net_group_ptr);
    CHECK_EXPECTED(net_group_wrapper);

    auto net_group_wrapper_ptr = make_shared_nothrow<ConfiguredNetworkGroupWrapper>(net_group_wrapper.release());
    CHECK_AS_EXPECTED(nullptr != net_group_wrapper_ptr, HAILO_OUT_OF_HOST_MEMORY);
    m_net_group_wrappers.emplace_back(net_group_wrapper_ptr);

    return Expected<std::shared_ptr<ConfiguredNetworkGroup>>(net_group_wrapper_ptr);
}

Expected<ConfigureNetworkParams> VdmaConfigManager::get_default_configured_params(Hef &hef,
    const std::string &network_group_name)
{
    auto first_streams_interface = m_devices[0].get().get_default_streams_interface();
    CHECK_EXPECTED(first_streams_interface);
#ifndef NDEBUG
    // Check that all physical devices has the same interface
    for (auto &device : m_devices) {
        auto interface = device.get().get_default_streams_interface();
        CHECK_EXPECTED(interface);
        CHECK_AS_EXPECTED(interface.value() == first_streams_interface.value(), HAILO_INTERNAL_FAILURE,
            "Not all default stream interfaces are the same");
    }
#endif
    auto config_params = hef.create_configure_params(first_streams_interface.value(), network_group_name);
    CHECK_EXPECTED(config_params);
    return config_params.release();
}

Expected<ConfiguredNetworkGroupVector> VdmaConfigManager::add_hef(Hef &hef,
    const NetworkGroupsParamsMap &configure_params)
{
    /* We assume all devices under VDevice has the same device_arch and partial_clusters_layout_bitmap.
        May be changed in SDK-28729 */
    auto device_arch_exp = m_devices[0].get().get_architecture();
    CHECK_EXPECTED(device_arch_exp);
    auto device_arch = device_arch_exp.release();

    auto partial_clusters_layout_bitmap_exp = Control::get_partial_clusters_layout_bitmap(m_devices[0].get());
    CHECK_EXPECTED(partial_clusters_layout_bitmap_exp);
    auto partial_clusters_layout_bitmap = partial_clusters_layout_bitmap_exp.release();

    auto &hef_network_groups = hef.pimpl->network_groups();
    const auto prev_network_group_count = m_net_groups.size();
    const auto total_network_group_count = prev_network_group_count + hef_network_groups.size();
    CHECK_AS_EXPECTED(CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS >= total_network_group_count,
        HAILO_INVALID_OPERATION,
        "Can't add {} network groups from HEF. Currently {} network groups are configured; maximum allowed network groups: {}.",
        hef_network_groups.size(), prev_network_group_count, CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS);

    bool was_hef_already_configured = false;

    ConfiguredNetworkGroupVector added_network_groups;
    // TODO: can be optimized (add another loop the allocate the network group we're adding)
    added_network_groups.reserve(hef_network_groups.size());

    auto hef_arch = hef.pimpl->get_device_arch();

    auto current_net_group_index = static_cast<uint8_t>(prev_network_group_count);
    auto configure_params_copy = configure_params;
    for (const auto &base_network_group_proto : hef_network_groups) {
        CHECK_NOT_NULL_AS_EXPECTED(base_network_group_proto, HAILO_INTERNAL_FAILURE);
        auto network_group_proto = Hef::Impl::get_net_group_per_arch(*base_network_group_proto, hef_arch, device_arch,
            partial_clusters_layout_bitmap);
        CHECK_EXPECTED(network_group_proto);

        const std::string network_group_name = network_group_proto->get().network_group_metadata().network_group_name();

        /* If NG params are present, use them
           If no configure params are given, use default*/
        ConfigureNetworkParams config_params{};
        if (contains(configure_params, network_group_name)) {
            config_params = configure_params_copy.at(network_group_name);
            configure_params_copy.erase(network_group_name);
        } else if (configure_params.empty()) {
            auto config_params_exp = get_default_configured_params(hef, network_group_name);
            CHECK_EXPECTED(config_params_exp);
            config_params = config_params_exp.release();
        } else {
            continue;
        }

        /* Validate batch size (network group batch size vs network batch size) */
        auto status = update_network_batch_size(config_params);
        CHECK_SUCCESS_AS_EXPECTED(status);

        auto network_group_metadata = hef.pimpl->get_network_group_metadata(network_group_name, partial_clusters_layout_bitmap);
        CHECK_EXPECTED(network_group_metadata);

        auto network_group_metadata_ptr = make_shared_nothrow<NetworkGroupMetadata>(network_group_metadata.release());
        CHECK_AS_EXPECTED(nullptr != network_group_metadata_ptr, HAILO_OUT_OF_HOST_MEMORY);

        auto network_group = create_configured_network_group(network_group_metadata_ptr, network_group_proto->get(), hef,
            config_params, current_net_group_index, was_hef_already_configured);
        CHECK_EXPECTED(network_group);
        current_net_group_index++;
        added_network_groups.emplace_back(network_group.release());
    }
    std::string unmatched_keys = "";
    for (const auto &pair : configure_params_copy) {
        unmatched_keys.append(" ");
        unmatched_keys.append(pair.first);
    }
    CHECK_AS_EXPECTED(unmatched_keys.size() == 0, HAILO_INVALID_ARGUMENT,
        "Some network group names in the configuration are not found in the hef file:{}", unmatched_keys);

    if (was_hef_already_configured) {
        return added_network_groups;
    }

    for (auto device : m_devices) {
        // Allocate context_switch_info on the heap (to avoid stack overflows)
        auto context_switch_info = make_unique_nothrow<CONTROL_PROTOCOL__context_switch_info_t>();
        CHECK_NOT_NULL_AS_EXPECTED(context_switch_info, HAILO_OUT_OF_HOST_MEMORY);
        memset(context_switch_info.get(), 0, sizeof(CONTROL_PROTOCOL__context_switch_info_t));

        context_switch_info->context_switch_main_header.context_switch_version = CONTROL_PROTOCOL__CONTEXT_SWITCH_VER_V1_0_0;
        context_switch_info->context_switch_main_header.application_count = static_cast<uint8_t>(added_network_groups.size());
        for (size_t index_in_hef = 0, context_index = 0; index_in_hef < added_network_groups.size(); index_in_hef++) {
            for (auto &resource_manager : m_net_groups[prev_network_group_count + index_in_hef]->get_resources_managers()) {
                if (std::string(device.get().get_dev_id()) != std::string(resource_manager->get_dev_id())) {
                    continue;
                }
                auto net_group_header_exp = resource_manager->get_control_network_group_header();
                CHECK_EXPECTED(net_group_header_exp);
                context_switch_info->context_switch_main_header.application_header[index_in_hef] = net_group_header_exp.value();
                auto net_group_contexts = resource_manager->get_contexts();
                
                CHECK_AS_EXPECTED(ARRAY_ENTRIES(context_switch_info->context) > context_index + net_group_contexts.size(), HAILO_INVALID_OPERATION,
                    "Can't add {} contexts. Currently {} contexts are configured; maximum allowed contexts: {}.",
                    net_group_contexts.size(), context_index, ARRAY_ENTRIES(context_switch_info->context));
                std::memcpy(&context_switch_info->context[context_index], net_group_contexts.data(),
                    net_group_contexts.size() * sizeof(context_switch_info->context[0]));
                context_index += net_group_contexts.size();
            }
        }

        // Write context_switch info
        const auto status = Control::write_context_switch_info(device.get(), context_switch_info.get());
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    return added_network_groups;
}

hailo_status VdmaConfigManager::update_network_batch_size(ConfigureNetworkParams &network_group_config_params)
{
    static_assert(HAILO_DEFAULT_BATCH_SIZE == 0, "Invalid HAILO_DEFAULT_BATCH_SIZE");

    auto single_network_default_batch = (HAILO_DEFAULT_BATCH_SIZE == network_group_config_params.batch_size);
    auto multi_network_default_batch = true;
    /* Batch size overide logic - if user modifies network group batch size
    and not the network batch size,  */

    for (auto const &network_params : network_group_config_params.network_params_by_name) {
        if (HAILO_DEFAULT_BATCH_SIZE != network_params.second.batch_size) {
            multi_network_default_batch = false;
        }
    }

    CHECK((single_network_default_batch || multi_network_default_batch), HAILO_INVALID_OPERATION, 
        "User provided batch size for network group and for network as well. User is adviced to work with network's batch size only");

    if (!single_network_default_batch && multi_network_default_batch) {
        /* In case user works with network group, overide the network batch size.*/
        for (auto &network_params : network_group_config_params.network_params_by_name) {
            network_params.second.batch_size = network_group_config_params.batch_size;
        }
    }

    return HAILO_SUCCESS;
}

ConfigManagerType VdmaConfigManager::get_manager_type()
{
    return ConfigManagerType::VdmaConfigManager;
}

} /* namespace hailort */
