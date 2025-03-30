/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mipi_stream.cpp
 * @brief MIPI stream implementation.
 *
 * MIPI is an interface which defines the connection between a host processor and a display
 * module, which in our case is a camera. Here we define a MipiInputStream which when it is configured
 * and opened, the camera module should send input data, meaning that the write functions here are not
 * implemented.
 **/

#include "hailo/hailort.h"

#include "common/utils.hpp"

#include "device_common/control.hpp"
#include "mipi/mipi_stream.hpp"


namespace hailort
{

MipiInputStream::MipiInputStream(Device &device, const CONTROL_PROTOCOL__mipi_input_config_params_t &mipi_params,
    EventPtr &&core_op_activated_event, const LayerInfo &layer_info, hailo_status &status) :
    InputStreamBase(layer_info, std::move(core_op_activated_event), status),
    m_device(device),
    m_is_stream_activated(false),
    m_mipi_input_params(mipi_params)
{}

MipiInputStream::~MipiInputStream()
{
    if (m_is_stream_activated) {
        auto status = deactivate_stream();
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Close stream failed! (status {} stream index {})", status, m_stream_info.index);
        }
    }
}

hailo_status MipiInputStream::deactivate_stream()
{
    hailo_status status = HAILO_UNINITIALIZED;

    ASSERT(m_is_stream_activated);

    status = Control::close_stream(m_device, m_dataflow_manager_id, true);
    CHECK_SUCCESS(status);

    m_is_stream_activated = false;
    return HAILO_SUCCESS;
}

std::chrono::milliseconds MipiInputStream::get_timeout() const
{
    LOGGER__WARNING("get_timeout() in MipiInputStream is not supported!");
    assert(false);
    return std::chrono::milliseconds(0);
}

hailo_status MipiInputStream::abort_impl()
{
    return HAILO_INVALID_OPERATION;
}

hailo_status MipiInputStream::clear_abort_impl()
{
    return HAILO_INVALID_OPERATION;
}

CONTROL_PROTOCOL__mipi_input_config_params_t MipiInputStream::hailo_mipi_params_to_control_mipi_params(
    const hailo_mipi_input_stream_params_t &params)
{
    CONTROL_PROTOCOL__mipi_input_config_params_t control_mipi_params;
    control_mipi_params.common_params.data_type = static_cast<uint8_t>(params.data_type);
    control_mipi_params.common_params.pixels_per_clock = static_cast<uint8_t>(params.mipi_common_params.pixels_per_clock);
    control_mipi_params.mipi_rx_id = params.mipi_rx_id;
    control_mipi_params.common_params.number_of_lanes = params.mipi_common_params.number_of_lanes;
    control_mipi_params.common_params.clock_selection = static_cast<uint8_t>(params.mipi_common_params.clock_selection);
    control_mipi_params.common_params.data_rate = params.mipi_common_params.data_rate;
    control_mipi_params.common_params.virtual_channel_index = params.mipi_common_params.virtual_channel_index;
    control_mipi_params.common_params.img_width_pixels = params.mipi_common_params.img_width_pixels;
    control_mipi_params.common_params.img_height_pixels = params.mipi_common_params.img_height_pixels;
    control_mipi_params.isp_params.isp_enable = params.isp_enable;
    control_mipi_params.isp_params.isp_img_in_order = static_cast<uint8_t>(params.isp_params.isp_img_in_order);
    control_mipi_params.isp_params.isp_img_out_data_type = static_cast<uint8_t>(params.isp_params.isp_img_out_data_type);
    control_mipi_params.isp_params.isp_crop_enable = params.isp_params.isp_crop_enable;
    control_mipi_params.isp_params.isp_crop_output_width_pixels = params.isp_params.isp_crop_output_width_pixels;
    control_mipi_params.isp_params.isp_crop_output_height_pixels = params.isp_params.isp_crop_output_height_pixels;
    control_mipi_params.isp_params.isp_crop_output_width_start_offset_pixels = params.isp_params.isp_crop_output_width_start_offset_pixels;
    control_mipi_params.isp_params.isp_crop_output_height_start_offset_pixels = params.isp_params.isp_crop_output_height_start_offset_pixels;
    control_mipi_params.isp_params.isp_test_pattern_enable = params.isp_params.isp_test_pattern_enable;
    control_mipi_params.isp_params.isp_configuration_bypass = params.isp_params.isp_configuration_bypass;
    control_mipi_params.isp_params.isp_run_time_ae_enable = params.isp_params.isp_run_time_ae_enable;
    control_mipi_params.isp_params.isp_run_time_awb_enable = params.isp_params.isp_run_time_awb_enable;
    control_mipi_params.isp_params.isp_run_time_adt_enable = params.isp_params.isp_run_time_adt_enable;
    control_mipi_params.isp_params.isp_run_time_af_enable = params.isp_params.isp_run_time_af_enable;
    control_mipi_params.isp_params.isp_run_time_calculations_interval_ms = params.isp_params.isp_run_time_calculations_interval_ms;
    control_mipi_params.isp_params.isp_light_frequency = static_cast<uint8_t>(params.isp_params.isp_light_frequency);
    return control_mipi_params;
}

hailo_status MipiInputStream::activate_stream()
{
    hailo_status status = HAILO_UNINITIALIZED;
    CONTROL_PROTOCOL__config_stream_params_t params = {};

    // Core HW padding is not supported on MIPI
    m_layer_info.nn_stream_config.feature_padding_payload = 0;
    params.nn_stream_config = m_layer_info.nn_stream_config;

    params.communication_type = CONTROL_PROTOCOL__COMMUNICATION_TYPE_MIPI;
    params.is_input = true;
    params.stream_index = m_stream_info.index;
    params.skip_nn_stream_config = false;
    // Currently hardcoded assign as there are no power mode optimizations over mipi
    params.power_mode = static_cast<uint8_t>(CONTROL_PROTOCOL__MODE_ULTRA_PERFORMANCE);
    params.communication_params.mipi_input = m_mipi_input_params;

    status = Control::config_stream_mipi_input(m_device, &params, m_dataflow_manager_id);
    CHECK_SUCCESS(status);

    status = Control::open_stream(m_device, m_dataflow_manager_id, true);
    CHECK_SUCCESS(status);

    m_is_stream_activated = true;
    return HAILO_SUCCESS;
}

hailo_status MipiInputStream::write_impl(const MemoryView &buffer)
{
    (void)buffer;
    return HAILO_INVALID_OPERATION;
}

Expected<std::unique_ptr<MipiInputStream>> MipiInputStream::create(Device &device,
    const LayerInfo &edge_layer, const hailo_mipi_input_stream_params_t &params,
    EventPtr core_op_activated_event)
{
    auto mipi_params = MipiInputStream::hailo_mipi_params_to_control_mipi_params(params);
    auto status = HAILO_UNINITIALIZED;
    std::unique_ptr<MipiInputStream> stream(new (std::nothrow) MipiInputStream(device, mipi_params,
        std::move(core_op_activated_event), edge_layer, status));
    CHECK_AS_EXPECTED(stream != nullptr, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return stream;
}

} /* namespace hailort */
