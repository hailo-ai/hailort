/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file transfer_common.hpp
 * @brief Common types/functions for async api
 **/

#ifndef _HAILO_TRANSFER_COMMON_HPP_
#define _HAILO_TRANSFER_COMMON_HPP_

#include "hailo/stream.hpp"
#include "hailo/buffer.hpp"

#include "os/hailort_driver.hpp"

namespace hailort
{

// Contains buffer that can be transferred. The buffer can be circular -
// It relies at [m_offset, m_base_buffer.size()) and [0, m_base_buffer.size() - m_size).
class TransferBuffer final {
public:

    TransferBuffer();
    TransferBuffer(BufferPtr base_buffer);
    TransferBuffer(BufferPtr base_buffer, size_t size, size_t offset);

    BufferPtr base_buffer() { return m_base_buffer; }
    size_t offset() const { return m_offset; }
    size_t size() const { return m_size; }

    Expected<vdma::MappedBufferPtr> map_buffer(HailoRTDriver &driver, HailoRTDriver::DmaDirection direction);

    hailo_status copy_to(MemoryView buffer);
    hailo_status copy_from(const MemoryView buffer);

    // Sync the buffer to the given direction, fails if the buffer is not mapped.
    hailo_status synchronize(HailoRTDriver &driver, HailoRTDriver::DmaSyncDirection sync_direction);

private:

    // Sync a signal continuous part
    hailo_status synchronize_part(vdma::MappedBufferPtr &mapped_buffer, MemoryView continuous_part,
        HailoRTDriver::DmaSyncDirection sync_direction);

    bool is_wrap_around() const;

    // Returns the continuous parts of the buffer.
    // There are 2 cases:
    //      1. If the buffer is_wrap_around(), both parts are valid, the first one starts at m_offset until
    //         m_base_buffer end
    //         The second part is the residue, starting from offset 0.
    //      2. If the buffer is not circular, the first part will contain the buffer, the second will point to nullptr.
    std::pair<MemoryView, MemoryView> get_continuous_parts();

    BufferPtr m_base_buffer;
    size_t m_size;
    size_t m_offset;
};

// Internal function, wrapper to the user callbacks, accepts the callback status as an argument.
using InternalTransferDoneCallback = std::function<void(hailo_status)>;

struct TransferRequest {
    TransferBuffer buffer;
    InternalTransferDoneCallback callback;
};

} /* namespace hailort */

#endif /* _HAILO_TRANSFER_COMMON_HPP_ */
