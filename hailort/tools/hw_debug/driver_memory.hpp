/**
 * @file driver_memory.hpp
 * @brief Implements MemorySource over HailoRT driver, reads/write all interfaces.
 */

#ifndef _HW_DEBUG_DRIVER_MEMORY_HPP_
#define _HW_DEBUG_DRIVER_MEMORY_HPP_

#include "memory_commands.hpp"
#include "vdma/driver/hailort_driver.hpp"

using hailort::HailoRTDriver;
using MemoryType = HailoRTDriver::MemoryType;

class DriverMemorySource : public MemorySource {
public:
    DriverMemorySource(std::shared_ptr<HailoRTDriver> driver, MemoryType memory_type);

    hailo_status read(uint64_t offset, uint8_t *data, size_t size) override;
    hailo_status write(uint64_t offset, const uint8_t *data, size_t size) override;
    size_t total_size() const override;

private:
    std::shared_ptr<HailoRTDriver> m_driver;
    MemoryType m_memory_type;
};

class VdmaMemorySource : public DriverMemorySource {
public:
    VdmaMemorySource(std::shared_ptr<HailoRTDriver> driver, MemoryType memory_type);
    size_t total_size() const override;
};

class DramDmaEngineMemorySource : public DriverMemorySource {
public:
    DramDmaEngineMemorySource(std::shared_ptr<HailoRTDriver> driver, MemoryType memory_type);
};

#endif /* _HW_DEBUG_DRIVER_MEMORY_HPP_ */
