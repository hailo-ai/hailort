/**
 * Copyright (c) 2019-2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file cache_buffer.hpp
 * @brief Wrapper for intermediate buffers used as caches
 **/

#ifndef _HAILO_CACHE_BUFFER_HPP_
#define _HAILO_CACHE_BUFFER_HPP_

#include "hailo/hailort.h"
#include "core_op/resource_manager/intermediate_buffer.hpp"

namespace hailort
{

class CacheBuffer final
{
public:
    static Expected<CacheBuffer> create(std::shared_ptr<vdma::VdmaBuffer> backing_buffer, uint32_t cache_size,
        uint32_t input_size, uint32_t output_size, uint32_t entry_size);

    CacheBuffer(CacheBuffer &&) = default;
    CacheBuffer(const CacheBuffer &) = delete;
    CacheBuffer &operator=(CacheBuffer &&) = delete;
    CacheBuffer &operator=(const CacheBuffer &) = delete;
    ~CacheBuffer() = default;

    // Set input/output channels to/from the cache. Will only be set once for each direction.
    // (subsequent calls will return the same IntermediateBuffer.)
    ExpectedRef<IntermediateBuffer> set_input_channel(HailoRTDriver &driver, vdma::ChannelId channel_id);
    ExpectedRef<IntermediateBuffer> set_output_channel(HailoRTDriver &driver, vdma::ChannelId channel_id);
    ExpectedRef<IntermediateBuffer> get_input();
    ExpectedRef<IntermediateBuffer> get_output();
    Expected<Buffer> read_cache();
    hailo_status write_cache(MemoryView buffer);
    hailo_status reprogram_descriptors(uint32_t new_read_offset_entries);

    uint16_t entry_size() const;
    uint32_t cache_length() const;
    uint32_t input_length() const;
    uint32_t output_length() const;
    // Returns true if both input and output channels are set.
    bool is_configured() const;

    class Snapshot final
    {
    public:
        Snapshot() = default; // Required for updating std::unordered_map
        Snapshot(Snapshot &&) = default;
        Snapshot(const Snapshot &) = delete;
        Snapshot &operator=(Snapshot &&) = default;
        Snapshot &operator=(const Snapshot &) = delete;
        ~Snapshot() = default;

        const Buffer &buffer() const;
        uint32_t read_offset() const;

    private:
        friend class CacheBuffer;

        Snapshot(Buffer &&buffer, uint32_t read_offset);

        Buffer m_buffer;
        uint32_t m_read_offset;
    };
    Expected<Snapshot> create_snapshot(uint32_t read_offset);

private:
    CacheBuffer(uint32_t cache_size, uint32_t input_size, uint32_t output_size, uint16_t entry_size,
        std::shared_ptr<vdma::VdmaBuffer> backing_buffer);

    const uint16_t m_entry_size;
    const uint32_t m_cache_length;
    const uint32_t m_input_length;
    const uint32_t m_output_length;
    const std::shared_ptr<vdma::VdmaBuffer> m_backing_buffer;
    // Each cache buffer has an input and output IntermediateBuffer -
    // * They both share the same backing buffer.
    // * They each have separate descriptor lists that will be programmed separately.
    // * This way we can read/write/reprogram the cache buffer without affecting the other direction.
    std::shared_ptr<IntermediateBuffer> m_cache_input;
    std::shared_ptr<IntermediateBuffer> m_cache_output;
};

} /* namespace hailort */

#endif /* _HAILO_CACHE_BUFFER_HPP_ */
