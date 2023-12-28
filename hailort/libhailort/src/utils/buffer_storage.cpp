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

Expected<bool> HeapStorage::dma_map(VdmaDevice &, hailo_dma_buffer_direction_t)
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

Expected<std::shared_ptr<Buffer>> DmaStorage::create_dma_able_buffer_from_user_size(void *addr, size_t size)
{
    auto storage = create_from_user_address(addr, size);
    CHECK_EXPECTED(storage);

    auto buffer = make_shared_nothrow<Buffer>(storage.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer, HAILO_OUT_OF_HOST_MEMORY);

    return buffer;
}

Expected<DmaStoragePtr> DmaStorage::create(void *user_address, size_t size,
    hailo_dma_buffer_direction_t data_direction,
    std::vector<std::reference_wrapper<Device>> &&physical_devices)
{
    vdma::DmaAbleBufferPtr dma_able_buffer_ptr = nullptr;
    if (nullptr == user_address) {
        // TODO: HRT-10283 support sharing low memory buffers for DART and similar systems.
        auto dma_able_buffer = vdma::DmaAbleBuffer::create_by_allocation(size);
        CHECK_EXPECTED(dma_able_buffer);
        dma_able_buffer_ptr = dma_able_buffer.release();
    } else {
        auto dma_able_buffer = vdma::DmaAbleBuffer::create_from_user_address(user_address, size);
        CHECK_EXPECTED(dma_able_buffer);
        dma_able_buffer_ptr = dma_able_buffer.release();
    }

    auto result = make_shared_nothrow<DmaStorage>(std::move(dma_able_buffer_ptr));
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

DmaStorage::~DmaStorage()
{
    // TODO: deleter callback holds a reference to a device, which is bad since this BufferStorage could outlive
    //       the device. We need to doc that it isn't allowed. Later on, I think devices should use shared_ptrs
    //       and then the mapping will inc the reference count (HRT-12361)
    for (const auto &device_mapping_pair : m_mappings) {
        const auto &mapping = device_mapping_pair.second;
        if (nullptr != mapping.second) {
            mapping.second();
        }
    }
}

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
    return dma_map(*reinterpret_cast<VdmaDevice*>(&device), data_direction);
}

// TODO: change data_direction to hailo_stream_direction_t (HRT-12391)
Expected<bool> DmaStorage::dma_map(VdmaDevice &device, hailo_dma_buffer_direction_t data_direction)
{
    CHECK_AS_EXPECTED(data_direction <= HAILO_DMA_BUFFER_DIRECTION_BOTH, HAILO_INVALID_ARGUMENT,
        "Invalid data direction {}", data_direction);

    const auto device_id = device.get_dev_id();
    auto find_result = m_mappings.find(device_id);
    if (find_result != m_mappings.end()) {
        // The buffer has been mapped in this object => don't map it again
        return Expected<bool>(false); // not a new mapping
    }

    const auto direction = (data_direction == HAILO_DMA_BUFFER_DIRECTION_H2D) ? HAILO_H2D_STREAM : HAILO_D2H_STREAM;

    auto mapping_result = device.try_dma_map(m_dma_able_buffer, direction);
    CHECK_EXPECTED(mapping_result);

    const auto is_new_mapping = mapping_result->second;
    if (is_new_mapping) {
        const auto deleter = [&device, address = m_dma_able_buffer->user_address(), direction]() {
            // Best effort
            auto status = device.dma_unmap(address, direction);
            if (HAILO_SUCCESS != status) {
                LOGGER__ERROR("Failed to un-map buffer {} from device {} in direction {}",
                address, device.get_dev_id(), direction);
            }
        };
        m_mappings.emplace(device_id, std::make_pair(mapping_result->first, deleter));
    } else {
        m_mappings.emplace(device_id, std::make_pair(mapping_result->first, nullptr));
    }
    return Expected<bool>(is_new_mapping);
}

Expected<vdma::MappedBufferPtr> DmaStorage::get_dma_mapped_buffer(const std::string &device_id)
{
    auto mapped_buffer = m_mappings.find(device_id);
    if (mapped_buffer == m_mappings.end()) {
        // Don't print error message here
        LOGGER__INFO("Mapped buffer for {} not found", device_id);
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return Expected<vdma::MappedBufferPtr>(mapped_buffer->second.first);
}

Expected<UserBufferStoragePtr> UserBufferStorage::create(void *user_address, const size_t size)
{
    auto result = make_shared_nothrow<UserBufferStorage>(user_address, size);
    CHECK_NOT_NULL_AS_EXPECTED(result, HAILO_OUT_OF_HOST_MEMORY);

    return result;
}

UserBufferStorage::UserBufferStorage(void * user_address, const size_t size) :
    BufferStorage(Type::USER_BUFFER),
    m_user_address(user_address),
    m_size(size)
{}

size_t UserBufferStorage::size() const
{
    return m_size;
}

void *UserBufferStorage::user_address()
{
    return const_cast<void *>(m_user_address);
}

Expected<void *> UserBufferStorage::release() noexcept
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<bool> UserBufferStorage::dma_map(Device &/* device */, hailo_dma_buffer_direction_t /* data_direction */)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

// TODO: change data_direction to hailo_stream_direction_t (HRT-12391)
Expected<bool> UserBufferStorage::dma_map(VdmaDevice &/* device */, hailo_dma_buffer_direction_t /* data_direction */)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<vdma::MappedBufferPtr> UserBufferStorage::get_dma_mapped_buffer(const std::string &/* device_id */)
{
    return make_unexpected(HAILO_NOT_IMPLEMENTED);
}

Expected<std::shared_ptr<Buffer>> UserBufferStorage::create_storage_from_user_buffer(void *addr, size_t size)
{
    auto storage = UserBufferStorage::create(addr, size);
    CHECK_EXPECTED(storage);

    auto buffer = make_shared_nothrow<Buffer>(storage.release());
    CHECK_NOT_NULL_AS_EXPECTED(buffer, HAILO_OUT_OF_HOST_MEMORY);

    return buffer;
}

} /* namespace hailort */
