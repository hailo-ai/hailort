/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file periph_calculator.cpp
 * @brief Class that calculates periph register values based off layer, device and HEF information
 **/

#include "periph_calculator.hpp"
#include "hef/hef_internal.hpp"

namespace hailort
{

bool PeriphCalculator::is_valid_periph_bytes_value(const uint32_t periph_bytes_per_buffer, const uint32_t periph_frame_size, 
    const bool is_ddr, const uint32_t max_shmifo_size, const uint32_t desc_page_size, const uint32_t max_periph_bytes_value,
    const uint16_t core_bytes_per_buffer)
{
    if (0 == periph_bytes_per_buffer) {
        return false;
    }

    if (is_ddr) {
        // In DDR there is no residue of descriptor - but has to divide with no remainder by core_bytes_per_buffer
        // Calculated by DFC, Furthermore periph is aligned to PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE and we cant
        // force that hw_frame_size will be aligned to periph_bytes_per_buffer.
        return (periph_bytes_per_buffer < max_shmifo_size) && (periph_bytes_per_buffer <= max_periph_bytes_value) &&
            (0 == (core_bytes_per_buffer % periph_bytes_per_buffer));
    }
    return ((periph_bytes_per_buffer < (max_shmifo_size - desc_page_size)) &&
        (0 == (periph_frame_size % periph_bytes_per_buffer)) && (periph_bytes_per_buffer <= max_periph_bytes_value));
}

Expected<LayerInfo> PeriphCalculator::calculate_nms_periph_registers(const LayerInfo &layer_info)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(layer_info.nms_info.bbox_size * layer_info.nms_info.burst_size),
        HAILO_INVALID_HEF, "Invalid NMS parameters");
    LayerInfo updated_layer_info = layer_info;
    const auto nms_periph_bytes = static_cast<uint16_t>(layer_info.nms_info.bbox_size * layer_info.nms_info.burst_size);

    const auto transfer_size = LayerInfoUtils::get_nms_layer_transfer_size(layer_info);
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(transfer_size / nms_periph_bytes), HAILO_INVALID_HEF, "Invalid NMS parameters");
    // Will divide with no remainder seeing as transfer size is multiple of (bbox_size * burst_size)
    assert(0 == (transfer_size % nms_periph_bytes));
    const auto nms_periph_buffers = static_cast<uint16_t>(transfer_size / nms_periph_bytes);

    // In NMS - update periph variables to represent size of frame in case of "interrupt per frame" (where we know frame
    // size). Otherwise - size of burst / bbox (transfer size)
    updated_layer_info.nn_stream_config.periph_bytes_per_buffer = nms_periph_bytes;
    updated_layer_info.nn_stream_config.periph_buffers_per_frame = nms_periph_buffers;
    return updated_layer_info;
}

uint32_t PeriphCalculator::calculate_ddr_periph_buffers_per_frame(const LayerInfo &layer_info,
    const uint32_t periph_bytes_per_buffer)
{
    uint32_t periph_buffers_per_frame = layer_info.nn_stream_config.core_bytes_per_buffer *
        layer_info.nn_stream_config.core_buffers_per_frame / periph_bytes_per_buffer;

    // if we get a periph bytes per buffer so small that the periph buffers per frame cant fit in uint16
    // put uint16_t max - seeing as this value doesnt really affect anything and we should not fail in that case.
    if (!IS_FIT_IN_UINT16(periph_buffers_per_frame)) {
        LOGGER__WARNING("periph buffers per frame in DDR too large - putting uint16_t max (This may affect HW infer estimator results");
        periph_buffers_per_frame = UINT16_MAX;
    }

    return periph_buffers_per_frame;
}

Expected<LayerInfo> PeriphCalculator::calculate_periph_registers_impl(const LayerInfo &layer_info,
    const uint32_t desc_page_size, const uint32_t max_periph_bytes_value, const bool is_core_hw_padding_config_in_dfc,
    const HEFHwArch &hw_arch)
{
    // Calculate periph according to hw shape - the shape the core is epecting to get
    const hailo_3d_image_shape_t& periph_shape = layer_info.hw_shape;

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT32(periph_shape.width * periph_shape.features * periph_shape.height *
        layer_info.hw_data_bytes), HAILO_INVALID_HEF, "Invalid frame size");

    LayerInfo updated_layer_info = layer_info;
    const auto is_ddr = (LayerType::DDR == layer_info.type);
    const uint32_t alignment = is_ddr ? PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE : PERIPH_BYTES_PER_BUFFER_ALIGNMENT_SIZE;
    const auto row_size = static_cast<uint32_t>(periph_shape.width * periph_shape.features * layer_info.hw_data_bytes);
    auto periph_frame_size = periph_shape.height * row_size;

    CHECK_AS_EXPECTED(desc_page_size < layer_info.max_shmifo_size, HAILO_INVALID_ARGUMENT,
        "Cannot find possible periph buffer size solution since desc_page_size ({}) is equal or larger than max stream size ({}) for layer name {}",
        desc_page_size, layer_info.max_shmifo_size, layer_info.name);

    // In case of core hw padding in DFC extension - hw shape might not be aligned - use aligned frame size and
    // confgured periph registers will add / removed the extra padding
    if (is_core_hw_padding_config_in_dfc) {
        if (0 != (periph_frame_size % PERIPH_FRAME_ALIGNMENT)) {
            TRY(const auto max_periph_padding_payload, HefConfigurator::max_periph_padding_payload_value(
                    DeviceBase::hef_arch_to_device_arch(hw_arch)));

            // Currently case of payload larger than max periph padding payload value - not supported
            CHECK_AS_EXPECTED(max_periph_padding_payload > periph_frame_size, HAILO_INVALID_HEF,
                "Error, padded frame size larger than {} Currently not supported", max_periph_padding_payload);

            const auto padded_periph_frame_size = HailoRTCommon::align_to(periph_frame_size,
                static_cast<uint32_t>(PERIPH_FRAME_ALIGNMENT));
            // Configure periph padding registers
            updated_layer_info.nn_stream_config.buffer_padding_payload = periph_frame_size;
            updated_layer_info.nn_stream_config.buffer_padding = static_cast<uint16_t>(padded_periph_frame_size -
                periph_frame_size);
            periph_frame_size = padded_periph_frame_size;
        }
    }

    // Currently takes the largest periph_bytes_per_buffer that is possible with shmifo size and desc page size
    // TODO HRT-10961 : calculate optimal periph size
    auto periph_bytes_per_buffer = HailoRTCommon::align_to(row_size, alignment);
    while ((0 < periph_bytes_per_buffer) && !is_valid_periph_bytes_value(periph_bytes_per_buffer, periph_frame_size,
        is_ddr, layer_info.max_shmifo_size, desc_page_size, max_periph_bytes_value,
        layer_info.nn_stream_config.core_bytes_per_buffer)) {
        periph_bytes_per_buffer -= alignment;
    }
    CHECK_AS_EXPECTED(0 != periph_bytes_per_buffer, HAILO_INVALID_ARGUMENT,
        "Error, Could not find valid periph bytes per buffer value");

    // In ddr - the core make sure that row size is aligned to PERIPH_BYTES_PER_BUFFER_DDR_ALIGNMENT_SIZE but if a row
    // Is too large to fit in core bytes per buffer - they will divide it and put it in mutliple buffers - so in order to 
    // Get the exact size in periph buffers per frame - we must multiply core registers and divide by periph bytes per buffer
    uint32_t periph_buffers_per_frame = is_ddr ? calculate_ddr_periph_buffers_per_frame(layer_info, periph_bytes_per_buffer):
        (periph_frame_size / periph_bytes_per_buffer);
    // if we get a periph bytes per buffer so small that the periph buffers per frame cant fit in uint16
    // put uint16_t max and add warning - seeing as this value doesn't really affect anything and we should not fail in that case.
    if (!IS_FIT_IN_UINT16(periph_buffers_per_frame)) {
        LOGGER__WARNING("periph buffers per frame too large - putting uint16_t max (This may affect HW infer estimator results");
        periph_buffers_per_frame = UINT16_MAX;
    }

    updated_layer_info.nn_stream_config.periph_bytes_per_buffer = static_cast<uint16_t>(periph_bytes_per_buffer);
    updated_layer_info.nn_stream_config.periph_buffers_per_frame = static_cast<uint16_t>(periph_buffers_per_frame);

    return updated_layer_info;
}

Expected<LayerInfo> PeriphCalculator::calculate_periph_registers(const LayerInfo &layer_info,
    const uint32_t desc_page_size, const HEFHwArch &hw_arch, const bool is_core_hw_padding_config_in_dfc)
{
    TRY(const auto max_periph_bytes_from_hef, HefConfigurator::max_periph_bytes_value(DeviceBase::hef_arch_to_device_arch(hw_arch)));
    const auto max_periph_bytes = std::min(max_periph_bytes_from_hef, layer_info.max_shmifo_size);

    if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == layer_info.format.order) {
        return calculate_nms_periph_registers(layer_info);
    }

    // TODO : HRT-12051 - remove max_periph_bytes from parameters and calculate in impl when remove is_core_hw_padding
    return calculate_periph_registers_impl(layer_info, desc_page_size, max_periph_bytes,
        is_core_hw_padding_config_in_dfc, hw_arch);
}

} /* namespace hailort */