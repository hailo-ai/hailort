/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file buffer_storage.cpp
 * @brief TODO: fill me (HRT-10026)
 **/

#include "hailo/buffer_storage.hpp"
#include "hailo/hailort.h"
#include "hailo/vdevice.hpp"
#include "vdma/vdma_device.hpp"
#include "vdma/memory/dma_able_buffer.hpp"
#include "vdma/memory/mapped_buffer.hpp"
#include "common/utils.hpp"

namespace hailort
{

// Checking ABI of hailo_dma_buffer_direction_t vs HailoRTDriver::DmaDirection
static_assert(HAILO_DMA_BUFFER_DIRECTION_H2D == (int)HailoRTDriver::DmaDirection::H2D,
    "hailo_dma_buffer_direction_t must match HailoRTDriver::DmaDirection");
static_assert(HAILO_DMA_BUFFER_DIRECTION_D2H == (int)HailoRTDriver::DmaDirection::D2H,
    "hailo_dma_buffer_direction_t must match HailoRTDriver::DmaDirection");
static_assert(HAILO_DMA_BUFFER_DIRECTION_BOTH == (int)HailoRTDriver::DmaDirection::BOTH,
    "hailo_dma_buffer_direction_t must match HailoRTDriver::DmaDirection");

BufferStorageParams::HeapParams::HeapParams()
{}

Expected<BufferStorageParams::DmaMappingParams> BufferStorageParams::DmaMappingParams::create(
    const hailo_buffer_dma_mapping_params_t &params)
{
    CHECK_AS_EXPECTED((params.device == nullptr) || (params.vdevice == nullptr), HAILO_INVALID_ARGUMENT,
        "Can't set both device and vdevice fields");
    return DmaMappingParams(params);
}

BufferStorageParams::DmaMappingParams::DmaMappingParams(const hailo_buffer_dma_mapping_params_t &params) :
    device(reinterpret_cast<Device*>(params.device)),
    vdevice(reinterpret_cast<VDevice*>(params.vdevice)),
    data_direction(params.direction)
{}

BufferStorageParams::DmaMappingParams::DmaMappingParams(Device &device, hailo_dma_buffer_direction_t data_direction) :
    device(&device),
    vdevice(nullptr),
    data_direction(data_direction)
{}

BufferStorageParams::DmaMappingParams::DmaMappingParams(VDevice &vdevice, hailo_dma_buffer_direction_t data_direction) :
    device(nullptr),
    vdevice(&vdevice),
    data_direction(data_direction)
{}

BufferStorageParams::DmaMappingParams::DmaMappingParams() :
    device(nullptr),
    vdevice(nullptr),
    data_direction(HAILO_DMA_BUFFER_DIRECTION_MAX_ENUM)
{}

Expected<BufferStorageParams> BufferStorageParams::create(const hailo_buffer_parameters_t &params)
{
    BufferStorageParams result{};
    result.flags = params.flags;

    if (params.flags == HAILO_BUFFER_FLAGS_NONE) {
        result.heap_params = HeapParams();
    } else if ((params.flags & HAILO_BUFFER_FLAGS_DMA) != 0) {
        auto dma_mapping_params = DmaMappingParams::create(params.dma_mapping_params);
        CHECK_EXPECTED(dma_mapping_params);
        result.dma_mapping_params = dma_mapping_params.release();
    } else {
        // TODO: HRT-10903
        LOGGER__ERROR("Buffer storage flags not currently supported {}", params.flags);
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
    }

    return result;
}

BufferStorageParams BufferStorageParams::create_dma()
{
    BufferStorageParams result{};
    result.flags = HAILO_BUFFER_FLAGS_DMA;
    result.dma_mapping_params = DmaMappingParams();
    return result;
}

BufferStorageParams BufferStorageParams::create_dma(Device &device, hailo_dma_buffer_direction_t data_direction)
{
    BufferStorageParams result{};
    result.flags = HAILO_BUFFER_FLAGS_DMA;
    result.dma_mapping_params = DmaMappingParams(device, data_direction);
    return result;
}

BufferStorageParams BufferStorageParams::create_dma(VDevice &vdevice, hailo_dma_buffer_direction_t data_direction)
{
    BufferStorageParams result{};
    result.flags = HAILO_BUFFER_FLAGS_DMA;
    result.dma_mapping_params = DmaMappingParams(vdevice, data_direction);
    return result;
}

BufferStorageParams::BufferStorageParams() :
    flags(HAILO_BUFFER_FLAGS_NONE),
    heap_params()
{}

Expected<BufferStoragePtr> BufferStorage::create(size_t size, const BufferStorageParams &params)
{
    if (params.flags == HAILO_BUFFER_FLAGS_NONE) {
        auto result = HeapStorage::create(size);
        CHECK_EXPECTED(result);
        return std::static_pointer_cast<BufferStorage>(result.release());
    } else if (0 != (params.flags & HAILO_BUFFER_FLAGS_DMA)) {
        // TODO: check other flags here (HRT-10903)
        auto &dma_mapping_params = params.dma_mapping_params;

        DmaStoragePtr storage = nullptr;
        if ((dma_mapping_params.device != nullptr) && (dma_mapping_params.vdevice != nullptr)) {
            LOGGER__ERROR("Can't map a buffer to both vdevice and device");
            return make_unexpected(HAILO_INVALID_ARGUMENT);
        } else if (dma_mapping_params.device != nullptr) {
            auto result = DmaStorage::create(size, dma_mapping_params.data_direction,
                *dma_mapping_params.device);
            CHECK_EXPECTED(result);
            storage = result.release();
        } else if (dma_mapping_params.vdevice != nullptr) {
            auto result = DmaStorage::create(size, dma_mapping_params.data_direction,
                *dma_mapping_params.vdevice);
            CHECK_EXPECTED(result);
            storage = result.release();
        } else {
            auto result = DmaStorage::create(size);
            CHECK_EXPECTED(result);
            storage = result.release();
        }
        return std::static_pointer_cast<BufferStorage>(storage);
    }

    // TODO: HRT-10903
    LOGGER__ERROR("Buffer storage flags not currently supported {}", params.flags);
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

BufferStorage::BufferStorage(Type type) :
    m_type(type)
{}

BufferStorage::Type BufferStorage::type() const
{
    return m_type;
}

Expected<HeapStoragePtr> HeapStorage::create(size_t size)
{
    std::unique_ptr<uint8_t[]> data(new (std::nothrow) uint8_t[size]);
    CHECK_NOT_NULL_AS_EXPECTED(data, HAILO_OUT_OF_HOST_MEMORY);

    auto result = make_shared_nothrow<HeapStorage>(std::move(data), size);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

HeapStorage::HeapStorage(std::unique_ptr<uint8_t[]> data, size_t size) :
    BufferStorage(Type::HEAP),
    m_data(std::move(data)),
    m_size(size)
{}

HeapStorage::HeapStorage(HeapStorage&& other) noexcept :
    BufferStorage(std::move(other)),
    m_data(std::move(other.m_data)),
    m_size(std::exchange(other.m_size, 0))
{}

size_t HeapStorage::size() const
{
    return m_size;
}

void *HeapStorage::user_address()
{
    return m_data.get();
}

Expected<void *> HeapStorage::release() noexcept
{
    m_size = 0;
    return m_data.release();
}

Expected<bool> HeapStorage::dma_map(Device &, hailo_dma_buffer_direction_t)
{
    LOGGER__ERROR("Heap allocated buffers can't be mapped to DMA");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<bool> HeapStorage::dma_map(HailoRTDriver &, hailo_dma_buffer_direction_t)
{
    LOGGER__ERROR("Heap allocated buffers can't be mapped to DMA");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<vdma::MappedBufferPtr> HeapStorage::get_dma_mapped_buffer(const std::string &)
{
    LOGGER__ERROR("Mapped buffer is not supported for Heap allocated buffers");
    return make_unexpected(HAILO_INVALID_OPERATION);
}

Expected<DmaStoragePtr> DmaStorage::create(size_t size)
{
    static const auto ALLOCATE_BUFFER = nullptr;
    return create(ALLOCATE_BUFFER, size);
}

Expected<DmaStoragePtr> DmaStorage::create(size_t size,
    hailo_dma_buffer_direction_t data_direction, Device &device)
{
    static const auto ALLOCATE_BUFFER = nullptr;
    return create(ALLOCATE_BUFFER, size, data_direction,
        std::vector<std::reference_wrapper<Device>>{std::ref(device)});
}

Expected<DmaStoragePtr> DmaStorage::create(size_t size,
    hailo_dma_buffer_direction_t data_direction, VDevice &vdevice)
{
    static const auto ALLOCATE_BUFFER = nullptr;
    auto physical_devices = vdevice.get_physical_devices();
    CHECK_EXPECTED(physical_devices);
    return create(ALLOCATE_BUFFER, size, data_direction, physical_devices.release());
}

Expected<DmaStoragePtr> DmaStorage::create_from_user_address(void *user_address, size_t size)
{
    return create(user_address, size);
}

Expected<DmaStoragePtr> DmaStorage::create_from_user_address(void *user_address, size_t size,
    hailo_dma_buffer_direction_t data_direction, Device &device)
{
    CHECK_ARG_NOT_NULL_AS_EXPECTED(user_address);
    return create(user_address, size, data_direction,
        std::vector<std::reference_wrapper<Device>>{std::ref(device)});
}

Expected<DmaStoragePtr> DmaStorage::create_from_user_address(void *user_address, size_t size,
    hailo_dma_buffer_direction_t data_direction, VDevice &vdevice)
{
    CHECK_ARG_NOT_NULL_AS_EXPECTED(user_address);
    auto physical_devices = vdevice.get_physical_devices();
    CHECK_EXPECTED(physical_devices);
    return create(user_address, size, data_direction, physical_devices.release());
}

Expected<DmaStoragePtr> DmaStorage::create(void *user_address, size_t size,
    hailo_dma_buffer_direction_t data_direction,
    std::vector<std::reference_wrapper<Device>> &&physical_devices)
{
    // TODO: HRT-10283 support sharing low memory buffers for DART and similar systems.
    auto dma_able_buffer = vdma::DmaAbleBuffer::create(size, user_address);
    CHECK_EXPECTED(dma_able_buffer);

    auto result = make_shared_nothrow<DmaStorage>(dma_able_buffer.release());
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    for (auto &device : physical_devices) {
        auto is_new_mapping = result->dma_map(device, data_direction);
        CHECK_EXPECTED(is_new_mapping);
        CHECK_AS_EXPECTED(is_new_mapping.value(), HAILO_INTERNAL_FAILURE);
    }

    return result;
}

DmaStorage::DmaStorage(vdma::DmaAbleBufferPtr &&dma_able_buffer) :
    BufferStorage(Type::DMA),
    m_dma_able_buffer(std::move(dma_able_buffer)),
    m_mappings()
{}

size_t DmaStorage::size() const
{
    return m_dma_able_buffer->size();
}

void *DmaStorage::user_address()
{
    return m_dma_able_buffer->user_address();
}

Expected<void *> DmaStorage::release() noexcept
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<bool> DmaStorage::dma_map(Device &device, hailo_dma_buffer_direction_t data_direction)
{
    const auto device_type = device.get_type();
    CHECK_AS_EXPECTED(((Device::Type::INTEGRATED == device_type) || (Device::Type::PCIE == device_type)),
        HAILO_INVALID_ARGUMENT, "Invalid device type (expected integrated/pcie, received {})", device_type);
    VdmaDevice *vdma_device = reinterpret_cast<VdmaDevice*>(&device);

    return dma_map(vdma_device->get_driver(), data_direction);
}

Expected<bool> DmaStorage::dma_map(HailoRTDriver &driver, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_AS_EXPECTED(data_direction <= HAILO_DMA_BUFFER_DIRECTION_BOTH, HAILO_INVALID_ARGUMENT,
        "Invalid data direction {}", data_direction);

    const auto &device_id = driver.device_id();
    auto find_result = m_mappings.find(device_id);
    if (find_result != m_mappings.end()) {
        // The buffer has been mapped => don't map it again
        return Expected<bool>(false); // not a new mapping
    }

    // The buffer hasn't been mapped => map it now
    auto mapped_buffer = vdma::MappedBuffer::create_shared(driver, m_dma_able_buffer,
        static_cast<HailoRTDriver::DmaDirection>(data_direction));
    CHECK_EXPECTED(mapped_buffer);

    m_mappings.emplace(device_id, mapped_buffer.value());
    return Expected<bool>(true); // new mapping
}

Expected<vdma::MappedBufferPtr> DmaStorage::get_dma_mapped_buffer(const std::string &device_id)
{
    auto mapped_buffer = m_mappings.find(device_id);
    if (mapped_buffer == m_mappings.end()) {
        // Don't print error message here
        LOGGER__INFO("Mapped buffer for {} not found", device_id);
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return Expected<vdma::MappedBufferPtr>(mapped_buffer->second);
}

} /* namespace hailort */
