/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file buffer_storage.hpp
 * @brief TODO: fill me (HRT-10026)
 **/

#ifndef _HAILO_BUFFER_STORAGE_HPP_
#define _HAILO_BUFFER_STORAGE_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include <memory>
#include <cstdint>
#include <functional>
#include <vector>
#include <unordered_map>
#include <string>


/** hailort namespace */
namespace hailort
{

// Forward declarations
class Device;
class VDevice;
class BufferStorage;
class HeapStorage;
class DmaStorage;
class HailoRTDriver;

namespace vdma {
    class DmaAbleBuffer;
    using DmaAbleBufferPtr = std::shared_ptr<DmaAbleBuffer>;

    class MappedBuffer;
    using MappedBufferPtr = std::shared_ptr<MappedBuffer>;
}


/*! Buffer storage parameters. Analogical to hailo_buffer_parameters_t */
struct HAILORTAPI BufferStorageParams
{
public:
    struct HeapParams
    {
    public:
        HeapParams();
    };

    struct DmaMappingParams
    {
    public:
        static Expected<DmaMappingParams> create(const hailo_buffer_dma_mapping_params_t &params);
        // DmaMappingParams for a buffer to be mapped to device
        DmaMappingParams(Device &device, hailo_dma_buffer_direction_t data_direction);
        // DmaMappingParams for a buffer to be mapped to all the underlying devices held by vdevice
        DmaMappingParams(VDevice &vdevice, hailo_dma_buffer_direction_t data_direction);
        // DmaMappingParams for a buffer to be lazily mapped upon it's first async transfer to a given device
        DmaMappingParams();

        // Note: We hold a pointer to a Device/VDevice/neither, since DmaMappingParams support mapping to
        //       a device, vdevice or lazy mapping
        Device *device;
        VDevice *vdevice;
        hailo_dma_buffer_direction_t data_direction;

    private:
        DmaMappingParams(const hailo_buffer_dma_mapping_params_t &params);
    };

    static Expected<BufferStorageParams> create(const hailo_buffer_parameters_t &params);
    // Dma buffer params for lazy mapping
    static BufferStorageParams create_dma();
    // Dma buffer params for mapping to device in data_direction
    static BufferStorageParams create_dma(Device &device, hailo_dma_buffer_direction_t data_direction);
    // Dma buffer params for mapping to vdevice in data_direction
    static BufferStorageParams create_dma(VDevice &vdevice, hailo_dma_buffer_direction_t data_direction);

    // Defaults to heap params
    BufferStorageParams();

    hailo_buffer_flags_t flags;
    union {
        HeapParams heap_params;
        DmaMappingParams dma_mapping_params;
    };
};

using BufferStoragePtr = std::shared_ptr<BufferStorage>;

class HAILORTAPI BufferStorage
{
public:
    enum class Type {
        HEAP,
        DMA
    };

    static Expected<BufferStoragePtr> create(size_t size, const BufferStorageParams &params);

    BufferStorage(BufferStorage&& other) noexcept = default;
    BufferStorage(const BufferStorage &) = delete;
    BufferStorage &operator=(BufferStorage &&) = delete;
    BufferStorage &operator=(const BufferStorage &) = delete;
    virtual ~BufferStorage() = default;

    Type type() const;
    virtual size_t size() const = 0;
    virtual void *user_address() = 0;
    // Returns the pointer managed by this object and releases ownership
    // TODO: Add a free function pointer? (HRT-10024)
    // // Free the returned pointer with `delete`
    // TODO: after release the containing buffer will hold pointers to values that were released.
    //       Document that this can happen? Disable this behavior somehow? (HRT-10024)
    virtual Expected<void *> release() noexcept = 0;
    // Maps the storage to device in data_direction.
    // - If the mapping is new - true is returned.
    // - If the mapping already exists - false is returned.
    // - Otherwise - Unexpected with a failure status is returned.
    virtual Expected<bool> dma_map(Device &device, hailo_dma_buffer_direction_t data_direction) = 0;
    // Maps the backing buffer to a device via driver in data_direction, returning a pointer to it.
    // - If the mapping is new - true is returned.
    // - If the mapping already exists - false is returned.
    // - Otherwise - Unexpected with a failure status is returned.
    virtual Expected<bool> dma_map(HailoRTDriver &driver, hailo_dma_buffer_direction_t data_direction) = 0;

    // Internal functions
    virtual Expected<vdma::MappedBufferPtr> get_dma_mapped_buffer(const std::string &device_id) = 0;

protected:
    explicit BufferStorage(Type type);

    const Type m_type;
};

using HeapStoragePtr = std::shared_ptr<HeapStorage>;

class HAILORTAPI HeapStorage : public BufferStorage
{
public:
    static Expected<HeapStoragePtr> create(size_t size);
    HeapStorage(std::unique_ptr<uint8_t[]> data, size_t size);
    HeapStorage(HeapStorage&& other) noexcept;
    HeapStorage(const HeapStorage &) = delete;
    HeapStorage &operator=(HeapStorage &&) = delete;
    HeapStorage &operator=(const HeapStorage &) = delete;
    virtual ~HeapStorage() = default;

    virtual size_t size() const override;
    virtual void *user_address() override;
    virtual Expected<void *> release() noexcept override;
    virtual Expected<bool> dma_map(Device &device, hailo_dma_buffer_direction_t data_direction) override;
    virtual Expected<bool> dma_map(HailoRTDriver &driver, hailo_dma_buffer_direction_t data_direction) override;

    // Internal functions
    virtual Expected<vdma::MappedBufferPtr> get_dma_mapped_buffer(const std::string &device_id) override;

private:
    std::unique_ptr<uint8_t[]> m_data;
    size_t m_size;
};

// ************************************* NOTE - START ************************************* //
// DmaStorage isn't currently supported and is for internal use only                        //
// **************************************************************************************** //
using DmaStoragePtr = std::shared_ptr<DmaStorage>;

// TODO: HRT-10026 doc this
class HAILORTAPI DmaStorage : public BufferStorage
{
public:
    // Creates a DmaStorage instance holding a dma-able buffer size bytes large.
    // The buffer isn't mapped to dma until dma_map is called.
    static Expected<DmaStoragePtr> create(size_t size);
    // Creates a DmaStorage instance holding a dma-able buffer size bytes large.
    // The buffer is mapped to device in data_direction.
    static Expected<DmaStoragePtr> create(size_t size,
        hailo_dma_buffer_direction_t data_direction, Device &device);
    // Creates a DmaStorage instance holding a dma-able buffer size bytes large.
    // The buffer is mapped to vdevice.get_physical_devices() in data_direction.
    static Expected<DmaStoragePtr> create(size_t size,
        hailo_dma_buffer_direction_t data_direction, VDevice &vdevice);

    // TODO: doc that the addr needs to be on a new page and aligned to 64B (HRT-9559)
    //       probably best just to call mmap
    // Creates a DmaStorage instance backed by the size bytes large buffer pointed to by user_address.
    // The buffer isn't mapped to dma until dma_map is called.
    static Expected<DmaStoragePtr> create_from_user_address(void *user_address, size_t size);
    // Creates a DmaStorage instance backed by the size bytes large buffer pointed to by user_address.
    // The buffer is mapped to device in data_direction.
    static Expected<DmaStoragePtr> create_from_user_address(void *user_address, size_t size,
        hailo_dma_buffer_direction_t data_direction, Device &device);
    // Creates a DmaStorage instance backed by the size bytes large buffer pointed to by user_address.
    // The buffer is mapped to vdevice.get_physical_devices() in data_direction.
    static Expected<DmaStoragePtr> create_from_user_address(void *user_address, size_t size,
        hailo_dma_buffer_direction_t data_direction, VDevice &device);

    DmaStorage(const DmaStorage &other) = delete;
    DmaStorage &operator=(const DmaStorage &other) = delete;
    DmaStorage(DmaStorage &&other) noexcept = default;
    DmaStorage &operator=(DmaStorage &&other) = delete;
    virtual ~DmaStorage() = default;

    virtual size_t size() const override;
    virtual void *user_address() override;
    virtual Expected<void *> release() noexcept override;
    // TODO: thread safety (HRT-10669)
    virtual Expected<bool> dma_map(Device &device, hailo_dma_buffer_direction_t data_direction) override;
    virtual Expected<bool> dma_map(HailoRTDriver &driver, hailo_dma_buffer_direction_t data_direction) override;

    // Internal functions
    DmaStorage(vdma::DmaAbleBufferPtr &&dma_able_buffer);
    virtual Expected<vdma::MappedBufferPtr> get_dma_mapped_buffer(const std::string &device_id) override;

private:
    // Creates a backing dma-able buffer (either user or hailort allocated).
    // Maps said buffer to physical_devices in data_direction.
    // By default (if physical_devices is empty), no mapping will occur
    static Expected<DmaStoragePtr> create(void *user_address, size_t size,
        hailo_dma_buffer_direction_t data_direction = HAILO_DMA_BUFFER_DIRECTION_MAX_ENUM,
        std::vector<std::reference_wrapper<Device>> &&physical_devices = {});

    vdma::DmaAbleBufferPtr m_dma_able_buffer;

    // For each device (key is device_id), we store some vdma mapping.
    // TODO: use (device_id, direction) as key - HRT-10656
    std::unordered_map<std::string, vdma::MappedBufferPtr> m_mappings;
};
// ************************************** NOTE - END ************************************** //
// DmaStorage isn't currently supported and is for internal use only                      //
// **************************************************************************************** //

} /* namespace hailort */

#endif /* _HAILO_BUFFER_STORAGE_HPP_ */
