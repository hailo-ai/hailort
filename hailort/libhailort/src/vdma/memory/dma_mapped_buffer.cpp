/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file vmda_mapped_buffer.cpp
 * @brief Vdma mapped buffer implementation
 **/

#include "hailo/dma_mapped_buffer.hpp"

#include "vdma/memory/mapped_buffer_impl.hpp"
#include "vdma/vdma_device.hpp"


namespace hailort {

static Expected<HailoRTDriver::DmaDirection> convert_flags_to_driver_enum(hailo_vdma_buffer_direction_flags_t data_direction)
{
    static const auto BOTH_DIRECTIONS = HAILO_VDMA_BUFFER_DIRECTION_FLAGS_H2D | HAILO_VDMA_BUFFER_DIRECTION_FLAGS_D2H;
    if ((data_direction & BOTH_DIRECTIONS) == BOTH_DIRECTIONS) {
        return HailoRTDriver::DmaDirection::BOTH;
    }

    if ((data_direction & HAILO_VDMA_BUFFER_DIRECTION_FLAGS_H2D) == HAILO_VDMA_BUFFER_DIRECTION_FLAGS_H2D) {
        return HailoRTDriver::DmaDirection::H2D;
    }

    if ((data_direction & HAILO_VDMA_BUFFER_DIRECTION_FLAGS_D2H) == HAILO_VDMA_BUFFER_DIRECTION_FLAGS_D2H) {
        return HailoRTDriver::DmaDirection::D2H;
    }

    return make_unexpected(HAILO_INVALID_ARGUMENT);
}

// TODO: this should maybe be a vdevice (for mapping buffers to multiple devs)
// TODO: a helper function for the cast to VdmaDevice
Expected<DmaMappedBuffer> DmaMappedBuffer::create(size_t size,
    hailo_vdma_buffer_direction_flags_t data_direction_flags, Device &device)
{
    static const auto ALLOCATE_BUFFER = nullptr;
    return create(ALLOCATE_BUFFER, size, data_direction_flags, device);
}

Expected<DmaMappedBuffer> DmaMappedBuffer::create_from_user_address(void *user_address, size_t size,
    hailo_vdma_buffer_direction_flags_t data_direction_flags, Device &device)
{
    CHECK_ARG_NOT_NULL_AS_EXPECTED(user_address);
    return create(user_address, size, data_direction_flags, device);
}

Expected<DmaMappedBuffer> DmaMappedBuffer::create(void *user_address, size_t size,
    hailo_vdma_buffer_direction_flags_t data_direction_flags, Device &device)
{
    const auto device_type = device.get_type();
    CHECK_AS_EXPECTED(((Device::Type::INTEGRATED == device_type) || (Device::Type::PCIE == device_type)),
        HAILO_INVALID_ARGUMENT, "Invalid device type (expected integrated/pcie, received {})", device_type);
    VdmaDevice *vdma_device = reinterpret_cast<VdmaDevice*>(&device);

    auto data_direction = convert_flags_to_driver_enum(data_direction_flags);
    CHECK_EXPECTED(data_direction, "Invalid direction flags received {}", data_direction_flags);

    auto pimpl_exp = Impl::create(vdma_device->get_driver(), data_direction.release(), size, user_address);
    CHECK_EXPECTED(pimpl_exp);

    auto pimpl = make_unique_nothrow<Impl>(pimpl_exp.release());
    CHECK_NOT_NULL_AS_EXPECTED(pimpl, HAILO_OUT_OF_HOST_MEMORY);

    return DmaMappedBuffer(std::move(pimpl));
}

DmaMappedBuffer::DmaMappedBuffer(std::unique_ptr<Impl> pimpl) :
    pimpl(std::move(pimpl))
{}

// Note: These can't be defined in the header due to the use of pimpl (it'll cause a compilation error)
DmaMappedBuffer::DmaMappedBuffer(DmaMappedBuffer &&other) noexcept = default;
DmaMappedBuffer::~DmaMappedBuffer() = default;

void *DmaMappedBuffer::user_address()
{
    return pimpl->user_address();
}

size_t DmaMappedBuffer::size() const
{
    return pimpl->size();
}

hailo_status DmaMappedBuffer::synchronize()
{
    static constexpr auto BUFFER_START = 0;
    return pimpl->synchronize(BUFFER_START, size());
}

} /* namespace hailort */
