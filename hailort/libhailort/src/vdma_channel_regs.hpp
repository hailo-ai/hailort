/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdma_channel_regs.hpp
 * @brief utilties used to parse/modify PLDA Vdma channel registers
 **/

#ifndef _HAILO_VDMA_CHANNEL__REGS_HPP_
#define _HAILO_VDMA_CHANNEL__REGS_HPP_

#include "hw_consts.hpp"
#include "hailo/expected.hpp"
#include "os/hailort_driver.hpp"

#include <cstdint>

namespace hailort
{

#define DESCPRIPTOR_LIST_MAX_DEPTH (16)

inline bool vdma_channel_control_is_aborted(uint8_t control_reg)
{
    return (control_reg & 1) == 0;
}

inline bool vdma_channel_control_is_paused(uint8_t control_reg)
{
    return (control_reg & 2) == 2;
}

class VdmaChannelRegs final {
public:
    VdmaChannelRegs(HailoRTDriver &driver, vdma::ChannelId channel_id, HailoRTDriver::DmaDirection direction) :
        m_driver(driver),
        m_channel_id(channel_id),
        m_direction(direction)
    {}

    Expected<uint8_t> get_control()
    {
        return read_integer<uint8_t>(VDMA_CHANNEL_CONTROL_OFFSET);
    }

    Expected<uint16_t> get_num_available()
    {
        return read_integer<uint16_t>(VDMA_CHANNEL_NUM_AVAIL_OFFSET);
    }

    hailo_status set_num_available(uint16_t value)
    {
        return write_integer<uint16_t>(VDMA_CHANNEL_NUM_AVAIL_OFFSET, value);
    }

    Expected<uint16_t> get_num_processed()
    {
        return read_integer<uint16_t>(VDMA_CHANNEL_NUM_PROC_OFFSET);
    }

    Expected<uint8_t> get_channel_error()
    {
        return read_integer<uint8_t>(VDMA_CHANNEL_ERROR_OFFSET);
    }

    hailo_status stop_channel()
    {
        auto reg_control = get_control();
        CHECK_EXPECTED_AS_STATUS(reg_control, "Fail to read vdma control register");

        // First pause channel
        auto status = set_control((reg_control.value() & 0xFC) | 0x3);
        CHECK_SUCCESS(status, "Fail to write vdma control register");

        std::this_thread::sleep_for(std::chrono::microseconds(2));

        // Then abort
        status = set_control((reg_control.value() & 0xFC) | 0x0);
        CHECK_SUCCESS(status, "Fail to write vdma control register");

        return HAILO_SUCCESS;
    }

private:

    template<typename IntegerType>
    Expected<IntegerType> read_integer(uint32_t offset)
    {
        auto value = m_driver.read_vdma_channel_register(m_channel_id, m_direction, offset, sizeof(IntegerType));
        CHECK_EXPECTED(value);
        return static_cast<IntegerType>(value.release());
    }

    hailo_status set_control(uint8_t value)
    {
        return write_integer<uint8_t>(VDMA_CHANNEL_CONTROL_OFFSET, value);
    }

    template<typename IntegerType>
    hailo_status write_integer(uint32_t offset, IntegerType value)
    {
        return m_driver.write_vdma_channel_register(m_channel_id, m_direction, offset,  sizeof(value), value);
    }

    HailoRTDriver &m_driver;
    const vdma::ChannelId m_channel_id;
    const HailoRTDriver::DmaDirection m_direction;
};

} /* namespace hailort */

#endif /*_HAILO_VDMA_CHANNEL__REGS_HPP_ */