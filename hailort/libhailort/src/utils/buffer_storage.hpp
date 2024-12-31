/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffer_storage.hpp
 * @brief Contains the internal storage object for the Buffer object.
 **/

#ifndef _HAILO_BUFFER_STORAGE_HPP_
#define _HAILO_BUFFER_STORAGE_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

#include "common/shared_memory_buffer.hpp"

#include "utils/exported_resource_manager.hpp"
#include "vdma/memory/continuous_buffer.hpp"

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
class VdmaDevice;
class BufferStorage;
class HeapStorage;
class DmaStorage;
class ContinuousStorage;
class SharedMemoryStorage;
class HailoRTDriver;
class Buffer;

namespace vdma {
    class DmaAbleBuffer;
    using DmaAbleBufferPtr = std::shared_ptr<DmaAbleBuffer>;

    class MappedBuffer;
    using MappedBufferPtr = std::shared_ptr<MappedBuffer>;
}


using BufferStoragePtr = std::shared_ptr<BufferStorage>;

// Using void* and size as key. Since the key is std::pair (not hash-able), we use std::map as the underlying container.
using BufferStorageKey = std::pair<void *, size_t>;

struct BufferStorageKeyHash {
    size_t operator()(const BufferStorageKey &key) const noexcept
    {
        return std::hash<void *>()(key.first) ^ std::hash<size_t>()(key.second);
    }
};

using BufferStorageResourceManager = ExportedResourceManager<BufferStoragePtr, BufferStorageKey, BufferStorageKeyHash>;
using BufferStorageRegisteredResource = RegisteredResource<BufferStoragePtr, BufferStorageKey, BufferStorageKeyHash>;

class BufferStorage
{
public:

    static Expected<BufferStoragePtr> create(size_t size, const BufferStorageParams &params);

    BufferStorage(BufferStorage&& other) noexcept = default;
    BufferStorage(const BufferStorage &) = delete;
    BufferStorage &operator=(BufferStorage &&) = delete;
    BufferStorage &operator=(const BufferStorage &) = delete;
    virtual ~BufferStorage() = default;

    virtual size_t size() const = 0;
    virtual void *user_address() = 0;
    // Returns the pointer managed by this object and releases ownership
    // TODO: Add a free function pointer? (HRT-10024)
    // // Free the returned pointer with `delete`
    // TODO: after release the containing buffer will hold pointers to values that were released.
    //       Document that this can happen? Disable this behavior somehow? (HRT-10024)
    virtual Expected<void *> release() noexcept = 0;

    // Internal functions
    virtual Expected<vdma::DmaAbleBufferPtr> get_dma_able_buffer();
    virtual Expected<uint64_t> dma_address();
    virtual Expected<std::string> shm_name();

    BufferStorage() = default;
};

using HeapStoragePtr = std::shared_ptr<HeapStorage>;

/**
 * Most basic storage for buffer - regular heap allocation.
 */
class HeapStorage : public BufferStorage
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

private:
    std::unique_ptr<uint8_t[]> m_data;
    size_t m_size;
};

using DmaStoragePtr = std::shared_ptr<DmaStorage>;

/**
 * Storage class for buffer that can be directly mapped to a device/vdevice for dma.
 */
class DmaStorage : public BufferStorage
{
public:
    // Creates a DmaStorage instance holding a dma-able buffer size bytes large.
    static Expected<DmaStoragePtr> create(size_t size);

    DmaStorage(const DmaStorage &other) = delete;
    DmaStorage &operator=(const DmaStorage &other) = delete;
    DmaStorage(DmaStorage &&other) noexcept = default;
    DmaStorage &operator=(DmaStorage &&other) = delete;
    virtual ~DmaStorage() = default;

    virtual size_t size() const override;
    virtual void *user_address() override;
    virtual Expected<void *> release() noexcept override;

    // Internal functions
    DmaStorage(vdma::DmaAbleBufferPtr &&dma_able_buffer);
    virtual Expected<vdma::DmaAbleBufferPtr> get_dma_able_buffer() override;

private:
    vdma::DmaAbleBufferPtr m_dma_able_buffer;
};


using ContinuousStoragePtr = std::shared_ptr<ContinuousStorage>;

/**
 * Storage class for buffer that is continuous
 */
class ContinuousStorage : public BufferStorage
{
public:
    static Expected<ContinuousStoragePtr> create(size_t size);
    ContinuousStorage(std::unique_ptr<HailoRTDriver> driver, vdma::ContinuousBuffer &&continuous_buffer);
    ContinuousStorage(ContinuousStorage&& other) noexcept;
    ContinuousStorage(const ContinuousStorage &) = delete;
    ContinuousStorage &operator=(ContinuousStorage &&) = delete;
    ContinuousStorage &operator=(const ContinuousStorage &) = delete;
    virtual ~ContinuousStorage() = default;

    virtual size_t size() const override;
    virtual void *user_address() override;
    virtual Expected<uint64_t> dma_address() override;
    virtual Expected<void *> release() noexcept override;

private:
    std::unique_ptr<HailoRTDriver> m_driver;
    vdma::ContinuousBuffer m_continuous_buffer;
};

using SharedMemoryStoragePtr = std::shared_ptr<SharedMemoryStorage>;

/**
 * Shared memory buffer
 */
class SharedMemoryStorage : public BufferStorage
{
public:
    static Expected<SharedMemoryStoragePtr> create(size_t size, const std::string &shm_name, bool memory_owner);
    SharedMemoryStorage(SharedMemoryBufferPtr shm_buffer);
    SharedMemoryStorage(SharedMemoryStorage&& other) noexcept;
    SharedMemoryStorage(const SharedMemoryStorage &) = delete;
    SharedMemoryStorage &operator=(SharedMemoryStorage &&) = delete;
    SharedMemoryStorage &operator=(const SharedMemoryStorage &) = delete;
    virtual ~SharedMemoryStorage() = default;

    virtual size_t size() const override;
    virtual void *user_address() override;
    virtual Expected<void *> release() noexcept override;
    virtual Expected<std::string> shm_name() override;

private:
    SharedMemoryBufferPtr m_shm_buffer;
};

} /* namespace hailort */

#endif /* _HAILO_BUFFER_STORAGE_HPP_ */
