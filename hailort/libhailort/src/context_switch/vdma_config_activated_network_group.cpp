/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_context_activatedN_network_group.cpp
 * @brief VdmaConfigActivatedNetworkGroup implementation
 **/

#include "context_switch/multi_context/vdma_config_activated_network_group.hpp"
#include "control.hpp"
#include <chrono>

namespace hailort
{

Expected<VdmaConfigActivatedNetworkGroup> VdmaConfigActivatedNetworkGroup::create(
    VdmaConfigActiveAppHolder &active_net_group_holder,
    std::vector<std::shared_ptr<ResourcesManager>> resources_managers,
    const hailo_activate_network_group_params_t &network_group_params,
    std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
    std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,         
    EventPtr network_group_activated_event,
    AccumulatorPtr deactivation_time_accumulator)
{
    CHECK(!active_net_group_holder.is_any_active(), make_unexpected(HAILO_INVALID_OPERATION),
        "network group is currently active. You must deactivate before activating another network_group");

    CHECK_ARG_NOT_NULL_AS_EXPECTED(deactivation_time_accumulator);

    auto status = HAILO_UNINITIALIZED;
    VdmaConfigActivatedNetworkGroup object(network_group_params, input_streams, output_streams,
        std::move(resources_managers), active_net_group_holder, std::move(network_group_activated_event),
        deactivation_time_accumulator, status);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return object;
}

VdmaConfigActivatedNetworkGroup::VdmaConfigActivatedNetworkGroup(
    const hailo_activate_network_group_params_t &network_group_params,
    std::map<std::string, std::unique_ptr<InputStream>> &input_streams,
    std::map<std::string, std::unique_ptr<OutputStream>> &output_streams,
    std::vector<std::shared_ptr<ResourcesManager>> &&resources_managers,
    VdmaConfigActiveAppHolder &active_net_group_holder,
    EventPtr &&network_group_activated_event,
    AccumulatorPtr deactivation_time_accumulator,
    hailo_status &status) :
        ActivatedNetworkGroupBase(network_group_params, input_streams, output_streams,
            std::move(network_group_activated_event), status),
        m_should_reset_state_machine(true),
        m_active_net_group_holder(active_net_group_holder),
        m_resources_managers(std::move(resources_managers)),
        m_ddr_send_threads(),
        m_ddr_recv_threads(),
        m_deactivation_time_accumulator(deactivation_time_accumulator)
{
    // Validate ActivatedNetworkGroup status
    if (HAILO_SUCCESS != status) {
        return;
    }
    m_active_net_group_holder.set(*this);

    for (auto &resources_manager : m_resources_managers) {
        status = resources_manager->register_fw_managed_vdma_channels();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to start fw managed vdma channels.");
            return;
        }
    }

    /* If ddr buffers are not SDK padded, host should control the DDR buffering */
    if (!m_resources_managers[0]->get_supported_features().padded_ddr_buffers) { // All ResourceManagers shares the same supported_features
        status = init_ddr_resources();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to initialize DDR resources.");
            return;
        }
    }

    for (auto &resources_manager : m_resources_managers) {
        status = resources_manager->enable_state_machine();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to activate state-machine");
            return;
        }
    }
}

VdmaConfigActivatedNetworkGroup::VdmaConfigActivatedNetworkGroup(VdmaConfigActivatedNetworkGroup &&other) noexcept :
    ActivatedNetworkGroupBase(std::move(other)),
    m_should_reset_state_machine(std::exchange(other.m_should_reset_state_machine, false)),
    m_active_net_group_holder(other.m_active_net_group_holder),
    m_resources_managers(std::move(other.m_resources_managers)),
    m_ddr_send_threads(std::move(other.m_ddr_send_threads)),
    m_ddr_recv_threads(std::move(other.m_ddr_recv_threads)),
    m_deactivation_time_accumulator(std::move(other.m_deactivation_time_accumulator))
{}

VdmaConfigActivatedNetworkGroup::~VdmaConfigActivatedNetworkGroup()
{
    if (!m_should_reset_state_machine) {
        return;
    }

    auto status = HAILO_UNINITIALIZED;
    const auto start_time = std::chrono::steady_clock::now();

    // If ddr buffers are not SDK padded, host should control the DDR buffering
    // Note: All ResourceManagers share the same supported_features
    if (!m_resources_managers[0]->get_supported_features().padded_ddr_buffers) {
        status = cleanup_ddr_resources();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to cleanup DDR resources.");
        }
    }

    m_active_net_group_holder.clear();
    deactivate_resources();

    for (auto &resources_manager : m_resources_managers) {
        status = resources_manager->reset_state_machine();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to reset context switch status");
        }
    }

    for (auto &resources_manager : m_resources_managers) {
        status = resources_manager->unregister_fw_managed_vdma_channels();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to stop fw managed vdma channels");
        }
    }

    const auto elapsed_time_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start_time).count();
    LOGGER__INFO("Deactivating took {} ms", elapsed_time_ms);
    m_deactivation_time_accumulator->add_data_point(elapsed_time_ms);
}

Expected<Buffer> VdmaConfigActivatedNetworkGroup::get_intermediate_buffer(const IntermediateBufferKey &key)
{
    CHECK_AS_EXPECTED(1 == m_resources_managers.size(), HAILO_INVALID_OPERATION,
        "'get_intermediate_buffer' function works only when working with 1 physical device. number of physical devices: {}",
        m_resources_managers.size());
    return m_resources_managers[0]->read_intermediate_buffer(key);
}

hailo_status VdmaConfigActivatedNetworkGroup::init_ddr_resources()
{
    for (auto &resources_manager : m_resources_managers) {
        auto status = resources_manager->open_ddr_channels();
        CHECK_SUCCESS(status);

        for (auto &ddr_info : resources_manager->ddr_infos()) {
            auto num_ready = make_shared_nothrow<std::atomic<uint16_t>>(static_cast<uint16_t>(0));
            CHECK_NOT_NULL(num_ready, HAILO_OUT_OF_HOST_MEMORY);

            m_ddr_send_threads.push_back(std::thread(ddr_send_thread_main, ddr_info, num_ready));
            m_ddr_recv_threads.push_back(std::thread(ddr_recv_thread_main, ddr_info, num_ready));
        }
    }
    return HAILO_SUCCESS;
}

hailo_status VdmaConfigActivatedNetworkGroup::cleanup_ddr_resources()
{
    hailo_status status = HAILO_SUCCESS; // Success oriented

    for (auto &resources_manager : m_resources_managers) {
        resources_manager->abort_ddr_channels();
    }

    for (auto& thread : m_ddr_send_threads) {
        thread.join();
    }
    m_ddr_send_threads.clear();

    for (auto& thread : m_ddr_recv_threads) {
        thread.join();
    }
    m_ddr_recv_threads.clear();

    for (auto &resources_manager : m_resources_managers) {
        resources_manager->close_ddr_channels();
    }

    return status;
}

void VdmaConfigActivatedNetworkGroup::ddr_send_thread_main(DdrChannelsInfo ddr_info,
    std::shared_ptr<std::atomic<uint16_t>> desc_list_num_ready)
{
    hailo_status status = HAILO_UNINITIALIZED;
    const auto timeout = std::chrono::milliseconds(DDR_THREAD_DEFAULT_TIMEOUT_MS);
    uint16_t last_num_proc = 0;
    uint32_t acc_sent_descs = 0;
    auto descs_per_interrupt = ddr_info.intermediate_buffer->descriptors_in_frame();
    assert(0 == (ddr_info.min_buffered_rows % DDR_NUMBER_OF_ROWS_PER_INTERRUPT));
    auto interrupts_per_min_buffered_rows = ddr_info.min_buffered_rows / DDR_NUMBER_OF_ROWS_PER_INTERRUPT;
    uint32_t descs_per_min_buffered_rows = interrupts_per_min_buffered_rows * descs_per_interrupt;

    if (nullptr == ddr_info.h2d_ch) {
        LOGGER__ERROR("Failed to find DDR H2D channel {}.", ddr_info.h2d_channel_index);
        return;
    }
    if (nullptr == ddr_info.d2h_ch) {
        LOGGER__ERROR("Failed to find DDR D2H channel {}.", ddr_info.d2h_channel_index);
        return;
    }

    while (true) {
        /* H2D */
        status = ddr_info.h2d_ch->wait_channel_interrupts_for_ddr(timeout);
        if (HAILO_SUCCESS != status) {
            goto l_exit;
        }

        /* D2H */
        auto hw_num_proc = ddr_info.h2d_ch->get_hw_num_processed_ddr(ddr_info.desc_list_size_mask);
        if (!hw_num_proc) {
            LOGGER__ERROR("Failed to get DDR_H2D_Channel {} num_processed.", ddr_info.h2d_channel_index);
            status = hw_num_proc.status();
            goto l_exit;
        }

        auto transferred_descs = (last_num_proc <= hw_num_proc.value()) ?
            static_cast<uint16_t>(hw_num_proc.value() - last_num_proc) :
            static_cast<uint16_t>(ddr_info.intermediate_buffer->descs_count() - last_num_proc + hw_num_proc.value());
        acc_sent_descs += transferred_descs;
        last_num_proc = hw_num_proc.value();

        auto sent_batches = acc_sent_descs / descs_per_min_buffered_rows;
        if (0 < sent_batches) {
            auto desc_count = ddr_info.intermediate_buffer->program_host_managed_ddr(ddr_info.row_size,
                (ddr_info.min_buffered_rows * sent_batches), *desc_list_num_ready);
            if (!desc_count) {
                LOGGER__ERROR("Failed to program descs for DDR");
                status = desc_count.status();
                goto l_exit;
            }
            acc_sent_descs -= desc_count.value();
            *desc_list_num_ready =
                static_cast<uint16_t>((desc_count.value() + *desc_list_num_ready) & ddr_info.desc_list_size_mask);

            status = ddr_info.d2h_ch->inc_num_available_for_ddr(desc_count.value(),
                ddr_info.desc_list_size_mask);
            if (HAILO_SUCCESS != status) {
                goto l_exit;
            }
        }
    }
l_exit:
    if (HAILO_SUCCESS != status) {
        if ((HAILO_STREAM_INTERNAL_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
            LOGGER__INFO("DDR thread main for channels {}:(h2d), {}:(d2h) exit with status {}",ddr_info.h2d_channel_index,
                ddr_info.d2h_channel_index, status);
        } else {
            LOGGER__ERROR("DDR thread main for channels {}:(h2d), {}:(d2h) exit with status {}",ddr_info.h2d_channel_index,
                ddr_info.d2h_channel_index, status);
        }
    }
    return;
}

void VdmaConfigActivatedNetworkGroup::ddr_recv_thread_main(DdrChannelsInfo ddr_info,
    std::shared_ptr<std::atomic<uint16_t>> desc_list_num_ready)
{
    hailo_status status = HAILO_UNINITIALIZED;
    uint16_t last_num_proc = 0;
    uint16_t transferred_descs = 0;
    const auto timeout = std::chrono::milliseconds(DDR_THREAD_DEFAULT_TIMEOUT_MS);

    if (nullptr == ddr_info.h2d_ch) {
        LOGGER__ERROR("Failed to find DDR H2D channel {}.", ddr_info.h2d_channel_index);
        return;
    }
    if (nullptr == ddr_info.d2h_ch) {
        LOGGER__ERROR("Failed to find DDR D2H channel {}.", ddr_info.d2h_channel_index);
        return;
    }

    *desc_list_num_ready = ddr_info.initial_programed_descs;

    status = ddr_info.d2h_ch->set_num_avail_value(ddr_info.initial_programed_descs);
    if (HAILO_SUCCESS != status) {
        goto l_exit;
    }

    while (true) {
        /* D2H */
        status = ddr_info.d2h_ch->wait_channel_interrupts_for_ddr(timeout);
        if (HAILO_SUCCESS != status) {
            goto l_exit;
        }

        /* H2D */
        auto hw_num_proc = ddr_info.d2h_ch->get_hw_num_processed_ddr(ddr_info.desc_list_size_mask);
        if (!hw_num_proc) {
            LOGGER__ERROR("Failed to get DDR_D2H_Channel {} num_processed.", ddr_info.d2h_channel_index);
            status = hw_num_proc.status();
            goto l_exit;
        }

        transferred_descs = (last_num_proc <= hw_num_proc.value()) ?
            static_cast<uint16_t>(hw_num_proc.value() - last_num_proc) :
            static_cast<uint16_t>(ddr_info.intermediate_buffer->descs_count() - last_num_proc + hw_num_proc.value());
        last_num_proc = hw_num_proc.value();

        status = ddr_info.h2d_ch->inc_num_available_for_ddr(transferred_descs,
            ddr_info.desc_list_size_mask);
        if (HAILO_SUCCESS != status) {
            goto l_exit;
        }
    }
l_exit:
    if (HAILO_SUCCESS != status) {
        if ((HAILO_STREAM_INTERNAL_ABORT == status) || (HAILO_STREAM_NOT_ACTIVATED == status)) {
            LOGGER__INFO("DDR thread main for channels {}:(h2d), {}:(d2h) exit with status {}",ddr_info.h2d_channel_index,
                ddr_info.d2h_channel_index, status);
        } else {
            LOGGER__ERROR("DDR thread main for channels {}:(h2d), {}:(d2h) exit with status {}",ddr_info.h2d_channel_index,
                ddr_info.d2h_channel_index, status);
        }
    }
    return;
}

} /* namespace hailort */
