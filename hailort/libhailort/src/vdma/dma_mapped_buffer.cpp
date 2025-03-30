/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file dma_mapped_buffer.cpp
 **/

#include "hailo/dma_mapped_buffer.hpp"
#include "hailo/hailort.h"
#include "hailo/vdevice.hpp"

#include "common/logger_macros.hpp"
#include "common/utils.hpp"

namespace hailort
{

class DmaMappedBuffer::Impl final {
public:
    Impl(VDevice &vdevice, void *address, size_t size, hailo_dma_buffer_direction_t direction, hailo_status &status)
    {
        create_mapping(vdevice, address, size, direction, status);
    }

    Impl(Device &device, void *address, size_t size, hailo_dma_buffer_direction_t direction, hailo_status &status)
    {
        create_mapping(device, address, size, direction, status);
    }

    ~Impl()
    {
        if (m_unmap) {
            m_unmap();
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

private:

    template<typename DeviceType>
    void create_mapping(DeviceType &device, void *address, size_t size, hailo_dma_buffer_direction_t direction, hailo_status &status) {
        status = device.dma_map(address, size, direction);
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Failed to map dma buffer, status: {}", status);
            return;
        }

        m_unmap = [&device, address, size, direction]() {
            auto status = device.dma_unmap(address, size, direction);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to unmap dma buffer, status: {}", status);
            }
        };
    }

    std::function<void()> m_unmap;
};

Expected<DmaMappedBuffer> DmaMappedBuffer::create(VDevice &vdevice, void *user_address, size_t size,
    hailo_dma_buffer_direction_t direction) {

    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<Impl> impl(new (std::nothrow) Impl(vdevice, user_address, size, direction, status));
    CHECK_NOT_NULL_AS_EXPECTED(impl, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return Expected<DmaMappedBuffer>(DmaMappedBuffer{std::move(impl)});
}

Expected<DmaMappedBuffer> DmaMappedBuffer::create(Device &device, void *user_address, size_t size,
    hailo_dma_buffer_direction_t direction) {

    hailo_status status = HAILO_UNINITIALIZED;
    std::unique_ptr<Impl> impl(new (std::nothrow) Impl(device, user_address, size, direction, status));
    CHECK_NOT_NULL_AS_EXPECTED(impl, HAILO_OUT_OF_HOST_MEMORY);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return Expected<DmaMappedBuffer>(DmaMappedBuffer{std::move(impl)});
}

// Defined in cpp since Impl definition is needed.
DmaMappedBuffer::~DmaMappedBuffer() = default;
DmaMappedBuffer::DmaMappedBuffer(DmaMappedBuffer &&) = default;
DmaMappedBuffer &DmaMappedBuffer::operator=(DmaMappedBuffer &&) = default;

DmaMappedBuffer::DmaMappedBuffer(std::unique_ptr<Impl> impl) :
    m_impl(std::move(impl))
{}

} /* namespace hailort */
