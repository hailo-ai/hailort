/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file periph_calculator.hpp
 * @brief Class that calculates periph register values based off layer, device and HEF information
 **/

#ifndef _PERIPH_CALCULATOR_HPP_
#define _PERIPH_CALCULATOR_HPP_


#include "common/utils.hpp"
#include "hailo/hailort_common.hpp"
#include "hef/layer_info.hpp"
#include "device_common/device_internal.hpp"

namespace hailort
{

static const uint64_t PERIPH_FRAME_ALIGNMENT = 8;

class PeriphCalculator {
public:
    static Expected<LayerInfo> calculate_periph_registers(const LayerInfo &layer_info,
        const uint32_t desc_page_size, const HEFHwArch &hw_arch, const bool is_core_hw_padding_config_in_dfc);
private:
    static bool is_valid_periph_bytes_value(const uint32_t periph_bytes_per_buffer, const uint32_t hw_frame_size,
        const bool is_ddr, const uint32_t max_shmifo_size, const uint32_t desc_page_size,
        const uint32_t max_periph_bytes_value, const uint16_t core_bytes_per_buffer);
    static Expected<LayerInfo> calculate_nms_periph_registers(const LayerInfo &layer_info);
    static Expected<LayerInfo> calculate_periph_registers_impl(const LayerInfo &layer_info,
        const uint32_t desc_page_size, const uint32_t max_periph_bytes_value,
        const bool is_core_hw_padding_config_in_dfc, const HEFHwArch &hw_arch);
    static uint32_t calculate_ddr_periph_buffers_per_frame(const LayerInfo &layer_info,
        const uint32_t periph_bytes_per_buffer);

};

} /* namespace hailort */

#endif /* _PERIPH_CALCULATOR_HPP_ */