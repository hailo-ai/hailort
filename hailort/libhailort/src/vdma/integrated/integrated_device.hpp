/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file integrated_device
 * @brief Device used by Hailo-15
 *
 **/

#ifndef _HAILO_INTEGRATED_DEVICE_HPP_
#define _HAILO_INTEGRATED_DEVICE_HPP_

#include "hailo/expected.hpp"
#include "hailo/hailort.h"
#include "vdma/vdma_device.hpp"
#include "device_common/control_soc.hpp"

#include <memory>

#ifdef __linux__
#include <linux/gpio.h>
#endif


namespace hailort

{
class IntegratedDevice : public VdmaDevice {
public:
    static bool is_loaded();
    static Expected<std::unique_ptr<IntegratedDevice>> create();

    virtual ~IntegratedDevice() = default;

    Expected<size_t> read_log(MemoryView &buffer, hailo_cpu_id_t cpu_id) override;
    virtual bool is_stream_interface_supported(const hailo_stream_interface_t &stream_interface) const override;
    virtual Expected<hailo_chip_temperature_info_t> get_chip_temperature() override;
    virtual Expected<float32_t> power_measurement(
        hailo_dvm_options_t dvm,
        hailo_power_measurement_types_t measurement_type) override;
    virtual hailo_status start_power_measurement(
        hailo_averaging_factor_t averaging_factor,
        hailo_sampling_period_t sampling_period) override;
    virtual hailo_status set_power_measurement(
        hailo_measurement_buffer_index_t buffer_index,
        hailo_dvm_options_t dvm,
        hailo_power_measurement_types_t measurement_type) override;
    virtual Expected<hailo_power_measurement_data_t> get_power_measurement(
        hailo_measurement_buffer_index_t buffer_index,
        bool should_clear) override;
    virtual hailo_status stop_power_measurement() override;
    virtual Expected<hailo_extended_device_information_t> get_extended_device_information() override;

    static constexpr const char *DEVICE_ID = HailoRTDriver::INTEGRATED_NNC_DEVICE_ID;
    virtual Expected<bool> has_INA231() override;

protected:
    virtual hailo_status reset_impl(CONTROL_PROTOCOL__reset_type_t reset_type) override;

private:
    IntegratedDevice(std::unique_ptr<HailoRTDriver> &&driver, hailo_status &status);
    std::shared_ptr<SocPowerMeasurement> m_power_measurement_data;

#if defined(__linux__) && defined(GPIO_V2_GET_LINE_IOCTL)
    class GpioReader final {
    public:
        GpioReader() : m_fd(-1), m_request_fd(-1) {}
        Expected<uint16_t> read();
        ~GpioReader();
    private:
        int m_fd;
        int m_request_fd;
    };
#endif
};


} /* namespace hailort */

#endif /* _HAILO_INTEGRATED_DEVICE_HPP_ */
