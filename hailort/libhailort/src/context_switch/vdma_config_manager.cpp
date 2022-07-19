
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
#include "hlpcie.hpp"

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
    VdmaConfigManager manager(std::move(devices), is_vdevice, empty_weak_ptr);
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

    VdmaConfigManager manager(std::move(vdma_devices), is_vdevice, vdevice.network_group_scheduler());
    return manager;
}

VdmaConfigManager::VdmaConfigManager(std::vector<std::reference_wrapper<VdmaDevice>> &&devices, bool is_vdevice, NetworkGroupSchedulerWeakPtr network_group_scheduler)
    : m_devices(std::move(devices)), m_net_groups(), m_net_group_wrappers(), m_is_vdevice(is_vdevice), m_network_group_scheduler(network_group_scheduler) {}

static bool should_use_multiplexer(const std::shared_ptr<NetworkGroupMetadata> &metadata, const std::vector<std::shared_ptr<VdmaConfigNetworkGroup>> &network_groups)
{
    (void) metadata;
    (void) network_groups;
    // TODO HRT-6579 decide if the multiplexer should be used for this network group.
    return false;
}

Expected<ConfiguredNetworkGroupVector> VdmaConfigManager::add_hef(Hef &hef,
    const NetworkGroupsParamsMap &configure_params)
{
    auto &hef_network_groups = hef.pimpl->network_groups();
    auto current_net_group_index = static_cast<uint8_t>(m_net_groups.size());
    CHECK(CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS >= m_net_groups.size() + hef_network_groups.size(),
        make_unexpected(HAILO_INVALID_OPERATION),
        "Can't add network_groups from HEF because of too many network_groups. maximum allowed network_groups are: {}",
        CONTROL_PROTOCOL__MAX_CONTEXT_SWITCH_APPLICATIONS);

    ConfiguredNetworkGroupVector added_network_groups;
    added_network_groups.reserve(hef_network_groups.size());

    auto configure_params_copy = configure_params;
    for (const auto &network_group_proto : hef_network_groups) {
        CHECK_NOT_NULL_AS_EXPECTED(network_group_proto, HAILO_INTERNAL_FAILURE);
        auto status = Hef::Impl::validate_net_group_unique_layer_names(network_group_proto);
        CHECK_SUCCESS_AS_EXPECTED(status);

        static_assert(HAILO_DEFAULT_BATCH_SIZE <= std::numeric_limits<uint16_t>::max(),
            "Invalid HAILO_DEFAULT_BATCH_SIZE");

        ConfigureNetworkParams config_params = {};
        if (contains(configure_params, network_group_proto->network_group_metadata().network_group_name())) {
            config_params = configure_params_copy.at(network_group_proto->network_group_metadata().network_group_name());
            configure_params_copy.erase(network_group_proto->network_group_metadata().network_group_name());
        } else {
            auto first_streams_interface = m_devices[0].get().get_default_streams_interface();
            CHECK_EXPECTED(first_streams_interface);
#ifndef NDEBUG
            // Check that all physicall devices has the same interface
            for (auto &device : m_devices) {
                auto interface = device.get().get_default_streams_interface();
                CHECK_EXPECTED(interface);
                CHECK_AS_EXPECTED(interface.value() == first_streams_interface.value(), HAILO_INTERNAL_FAILURE,
                    "Not all default stream interfaces are the same");
            }
#endif
            auto config_params_exp = hef.create_configure_params(first_streams_interface.value(),
                network_group_proto->network_group_metadata().network_group_name());
            CHECK_EXPECTED(config_params_exp);
            config_params = config_params_exp.release();
        }

        /* Validate batch size (network group batch size vs network batch size) */
        status = update_network_batch_size(config_params);
        CHECK_SUCCESS_AS_EXPECTED(status);

        auto network_group_metadata = hef.pimpl->get_network_group_metadata(network_group_proto->network_group_metadata().network_group_name());
        CHECK_EXPECTED(network_group_metadata);

        auto network_group_metadata_ptr = make_shared_nothrow<NetworkGroupMetadata>(network_group_metadata.release());
        CHECK_AS_EXPECTED(nullptr != network_group_metadata_ptr, HAILO_OUT_OF_HOST_MEMORY);

        /* build HEF supported features */
        std::vector<std::shared_ptr<ResourcesManager>> resources_managers;
        for (auto device : m_devices) {
            auto resource_manager = Hef::Impl::create_resources_manager(network_group_proto, current_net_group_index,
                device.get(), device.get().get_driver(), config_params, network_group_metadata_ptr, hef.pimpl->get_device_arch());
            CHECK_EXPECTED(resource_manager);
            resources_managers.push_back(resource_manager.release());
        }

        auto net_group = VdmaConfigNetworkGroup::create(m_active_net_group_holder, config_params,
            resources_managers, network_group_metadata_ptr, m_network_group_scheduler);
        current_net_group_index++;

        auto net_group_ptr = make_shared_nothrow<VdmaConfigNetworkGroup>(net_group.release());
        CHECK_AS_EXPECTED(nullptr != net_group_ptr, HAILO_OUT_OF_HOST_MEMORY);
        m_net_groups.emplace_back(net_group_ptr);

        // TODO: move this func into VdmaConfigNetworkGroup c'tor
        if (m_is_vdevice) {
            auto network_group_handle = INVALID_NETWORK_GROUP_HANDLE;
            auto network_group_scheduler = m_network_group_scheduler.lock();
            if (network_group_scheduler) {
                auto network_group_handle_exp = network_group_scheduler->add_network_group(net_group_ptr);
                CHECK_EXPECTED(network_group_handle_exp);
                network_group_handle = network_group_handle_exp.value();
                net_group_ptr->set_network_group_handle(network_group_handle);
            }
            if (network_group_scheduler && should_use_multiplexer(network_group_metadata_ptr, m_net_groups)) {
                /*
                auto identical_network_groups = find_identical(m_net_groups);
                // Iterate through the other VdmaCOnfigNetworkGroup and clone each of its streams with
                //  `VDeviceOutputStreamWrapper::clone()` and `VDeviceInputStreamWrapper::clone()`.
                status = net_group_ptr->create_vdevice_streams_from_another(identical_wrappers[0], network_group_handle);
                CHECK_SUCCESS_AS_EXPECTED(status);
                */
            } else {
                auto multiplexer = make_shared_nothrow<PipelineMultiplexer>();
                CHECK_AS_EXPECTED(nullptr != multiplexer, HAILO_OUT_OF_HOST_MEMORY, "Failed to create PipelineMultiplexer");
                // Using id 0 because these are the streams of the first configured network group of this multiplexer
                status = net_group_ptr->create_vdevice_streams_from_config_params(multiplexer, network_group_handle);
                CHECK_SUCCESS_AS_EXPECTED(status);
            }
        } else {
            status = net_group_ptr->create_streams_from_config_params(net_group_ptr->get_resources_managers()[0]->get_device());
            CHECK_SUCCESS_AS_EXPECTED(status);
        }

        // Check that all boundary streams were created
        status = validate_boundary_streams_were_created(hef, network_group_proto->network_group_metadata().network_group_name(), *net_group_ptr);
        CHECK_SUCCESS_AS_EXPECTED(status);

        auto net_group_wrapper = ConfiguredNetworkGroupWrapper::create(net_group_ptr);
        CHECK_EXPECTED(net_group_wrapper);

        auto net_group_wrapper_ptr = make_shared_nothrow<ConfiguredNetworkGroupWrapper>(net_group_wrapper.release());
        CHECK_AS_EXPECTED(nullptr != net_group_wrapper_ptr, HAILO_OUT_OF_HOST_MEMORY);
        m_net_group_wrappers.emplace_back(net_group_wrapper_ptr);

        added_network_groups.emplace_back(std::static_pointer_cast<ConfiguredNetworkGroup>(net_group_wrapper_ptr));
    }
    std::string unmatched_keys = "";
    for (const auto &pair : configure_params_copy) {
        unmatched_keys.append(" ");
        unmatched_keys.append(pair.first);
    }
    CHECK_AS_EXPECTED(unmatched_keys.size() == 0, HAILO_INVALID_ARGUMENT,
        "Some network group names in the configuration are not found in the hef file:{}", unmatched_keys);

    for (auto device : m_devices) {
        CONTROL_PROTOCOL__context_switch_info_t context_switch_info = {};
        context_switch_info.context_switch_main_header.context_switch_version = CONTROL_PROTOCOL__CONTEXT_SWITCH_VER_V1_0_0;
        context_switch_info.context_switch_main_header.application_count = static_cast<uint8_t>(m_net_groups.size());

        auto is_abbale_supported = false;
        // TODO: fix is_abbale_supported
        // auto proto_message = hef.pimpl.proto_message();
        // auto has_included_features = proto_message->has_included_features();
        // if (has_included_features) {
        //     is_abbale_supported = proto_message->included_features().abbale();
        // }

        context_switch_info.context_switch_main_header.validation_features.is_abbale_supported = is_abbale_supported;
        for (size_t i = 0, contexts = 0; i < m_net_groups.size(); ++i) {
            for (auto &resource_manager : m_net_groups[i]->get_resources_managers()) {
                if (0 == strcmp(device.get().get_dev_id(), resource_manager->get_dev_id())) {
                    auto net_group_header_exp = resource_manager->get_control_network_group_header();
                    CHECK_EXPECTED(net_group_header_exp);
                    context_switch_info.context_switch_main_header.application_header[i] = net_group_header_exp.value();
                    auto net_group_contexts = resource_manager->get_contexts();
                    CHECK_AS_EXPECTED((contexts + net_group_contexts.size()) <= CONTROL_PROTOCOL__MAX_TOTAL_CONTEXTS,
                        HAILO_INVALID_ARGUMENT,
                        "There are too many contexts in the configured network groups. Max allowed for all network groups together: {}",
                        CONTROL_PROTOCOL__MAX_TOTAL_CONTEXTS);
                    std::memcpy(&context_switch_info.context[contexts], net_group_contexts.data(),
                        net_group_contexts.size() * sizeof(context_switch_info.context[0]));
                    contexts += net_group_contexts.size();
                }
            }
        }

        {
            // Add instance that guards scheduler to deactivate network_group temporary
            auto scheduler_idle_guard = NetworkGroupScheduler::create_scheduler_idle_guard();
            if (m_is_vdevice) {
                auto network_group_scheduler = m_network_group_scheduler.lock();
                if (network_group_scheduler) {
                    auto status = scheduler_idle_guard->set_scheduler(network_group_scheduler);
                    CHECK_SUCCESS_AS_EXPECTED(status);
                }
            }

            // Reset context_switch status
            static const auto REMOVE_NN_CONFIG_DURING_RESET = false;
            auto status = Control::reset_context_switch_state_machine(device.get(), REMOVE_NN_CONFIG_DURING_RESET);
            CHECK_SUCCESS_AS_EXPECTED(status);

            // Write context_switch info
            status = Control::write_context_switch_info(device.get(), &context_switch_info);
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
    }

    return added_network_groups;
}

hailo_status VdmaConfigManager::update_network_batch_size(ConfigureNetworkParams &config_params)
{
    auto single_network_default_batch = (HAILO_DEFAULT_BATCH_SIZE == config_params.batch_size);
    auto multi_network_default_batch = true;
    /* Batch size overide logic - if user modifies network group batch size
    and not the network batch size,  */

    for (auto const &network_params : config_params.network_params_by_name) {
        if (HAILO_DEFAULT_BATCH_SIZE != network_params.second.batch_size) {
            multi_network_default_batch = false;
        }
    }

    CHECK((single_network_default_batch || multi_network_default_batch), HAILO_INVALID_OPERATION, 
        "User provided non batch size for network group and for network as well. User is adviced to work with network's batch size only");

    if (!single_network_default_batch && multi_network_default_batch) {
        /* In case user works with network group, overide the network batch size.*/
        for (auto &network_params : config_params.network_params_by_name) {
            network_params.second.batch_size = config_params.batch_size;
        }
    }

    return HAILO_SUCCESS;
}

ConfigManagerType VdmaConfigManager::get_manager_type()
{
    return ConfigManagerType::VdmaConfigManager;
}

} /* namespace hailort */
