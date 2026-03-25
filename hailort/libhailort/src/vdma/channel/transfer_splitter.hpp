/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file transfer_splitter.hpp
 * @brief Splits up a TransferRequest into a vector of TransferChunks for launch.
 **/

#ifndef _HAILO_TRANSFER_SPLITTER_HPP_
#define _HAILO_TRANSFER_SPLITTER_HPP_

#include "hailo/expected.hpp"
#include "vdma/transfer_common.hpp"


namespace hailort {
namespace vdma {

struct TransferChunk {
    std::vector<TransferBuffer> buffers;
    std::function<void(hailo_status)> callback;
    uint32_t num_descs;
    bool is_first;
    bool is_last;
};

using TransferChunks = std::vector<TransferChunk>;

class TransferSplitter final
{
public:
    TransferSplitter(uint16_t desc_page_size, size_t descs_count, size_t dma_alignment, bool should_split_buffers) :
        m_desc_page_size(desc_page_size),
        m_optimal_descs_in_chunk(descs_count / CHUNKS_DIVISION_FACTOR),
        m_max_descs_in_chunk(descs_count - 1), // HW requires that we always leave 1 descriptor free.
        m_dma_alignment(dma_alignment),
        m_should_split_buffers(should_split_buffers)
    {}

    TransferSplitter(TransferSplitter &&) = delete;
    TransferSplitter(const TransferSplitter &) = delete;
    TransferSplitter &operator=(TransferSplitter &&) = delete;
    TransferSplitter &operator=(const TransferSplitter &) = delete;

    Expected<TransferChunks> split(TransferRequest &&req);

    size_t descs_per_chunk() const
    {
        return m_should_split_buffers ? m_optimal_descs_in_chunk : m_max_descs_in_chunk;
    }

private:
    Expected<TransferChunks> split_buffers_by_count(TransferBuffers &&buffers, size_t buffers_per_chunk);

    Expected<TransferChunks> split_buffers_by_size(TransferBuffers &&buffers, size_t max_buffers_in_chunk,
        size_t max_descs_in_chunk);

    TransferChunk create_transfer_chunk(TransferBuffers bufs);

    const uint16_t m_desc_page_size;
    const size_t m_optimal_descs_in_chunk;
    const size_t m_max_descs_in_chunk;
    const size_t m_dma_alignment;

    // TODO HRT-19950: Understand why removing this bool causes failures on some H15 networks.
    const bool m_should_split_buffers;

    // NOTE: By breaking up chunks into smaller sizes we increase the throughput of the DMA.
    //       Magic-number 4 was reached though testing, open to future changes.
    static constexpr uint32_t CHUNKS_DIVISION_FACTOR = 4;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_TRANSFER_SPLITTER_HPP_ */
