/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hcp_config_activated_core_op.cpp
 * @brief HcpConfigActivatedCoreOp implementation
 **/

#include "eth/hcp_config_activated_core_op.hpp"
#include "device_common/control.hpp"


namespace hailort
{

Expected<HcpConfigActivatedCoreOp> HcpConfigActivatedCoreOp::create(Device &device, std::vector<WriteMemoryInfo> &config,
        const std::string &core_op_name,
        // hailo_activate_network_group_params_t is currently an empty holder, if anything will be added to it,
        // it will require a check that these params will be relevant for this one core op only.
        const hailo_activate_network_group_params_t &network_group_params,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,
        ActiveCoreOpHolder &active_core_op_holder,
        hailo_power_mode_t power_mode, EventPtr core_op_activated_event,
        CoreOp &core_op)
{
    CHECK(!active_core_op_holder.is_any_active(), make_unexpected(HAILO_INVALID_OPERATION),
        "core-op is currently active. You must deactivate before activating another core-op");

    // Close older dataflows
    auto status = Control::close_all_streams(device);
    CHECK_SUCCESS_AS_EXPECTED(status);

    // Reset nn_core before writing configurations
    status = device.reset(HAILO_RESET_DEVICE_MODE_NN_CORE);
    CHECK_SUCCESS_AS_EXPECTED(status);

    for (auto &m : config) {
        status = device.write_memory(m.address, MemoryView(m.data));
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    HcpConfigActivatedCoreOp object(device, active_core_op_holder, core_op_name, network_group_params, input_streams, output_streams,
        power_mode, std::move(core_op_activated_event), core_op, status);
    CHECK_SUCCESS_AS_EXPECTED(status);
    return object;
}

HcpConfigActivatedCoreOp::HcpConfigActivatedCoreOp(
        Device &device,
        ActiveCoreOpHolder &active_core_op_holder,
        const std::string &core_op_name,
        const hailo_activate_network_group_params_t &network_group_params,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,    
        hailo_power_mode_t power_mode,
        EventPtr &&core_op_activated_event,
        CoreOp &core_op, hailo_status &status) :
    ActivatedCoreOp(network_group_params, input_streams, output_streams,
                              std::move(core_op_activated_event), status),
    m_active_core_op_holder(active_core_op_holder),
    m_is_active(true),
    m_power_mode(power_mode),
    m_device(device),
    m_core_op_name(core_op_name)
{
    // Validate ActivatedCoreOp status
    if (HAILO_SUCCESS != status) {
        return;
    }
    status = core_op.activate_impl(CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to activate core-op");
        return;
    }
}

HcpConfigActivatedCoreOp::~HcpConfigActivatedCoreOp()
{
    if (!m_is_active) {
        return;
    }

    auto expected_config_network_ref = m_active_core_op_holder.get();
    if (!expected_config_network_ref.has_value()) {
        LOGGER__ERROR("Error getting configured core-op");
        return;
    }
    const auto &config_core_op = expected_config_network_ref.value();

    const auto status = config_core_op.get().deactivate_impl();
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed to deactivate core-op");
    }
}

// TODO: add get_core_op_name() for better code readability?
const std::string &HcpConfigActivatedCoreOp::get_network_group_name() const
{
    // network_group name is the same as core_op name in this case.
    // HcpConfigActivatedCoreOp should be used only for single core ops network groups.
    return m_core_op_name;
}

} /* namespace hailort */
