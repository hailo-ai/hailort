/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file transfer_splitter.cpp
 * @brief Splits up a TransferRequest into a vector of TransferChunks for launch.
 **/

#include "transfer_splitter.hpp"

#include "vdma/memory/descriptor_list.hpp"

namespace hailort {
namespace vdma {

Expected<TransferChunks> TransferSplitter::split(TransferRequest &&request)
{
    TransferChunks chunks;
    auto &buffers = request.transfer_buffers;

    if ((m_should_split_buffers) && (buffers.front().is_memview())) {
        // If buffers are memview, split buffers so that each chunk is of optimal size,
        // and no chunk has more than MAX_BUFFERS_IN_REQUEST.
        TRY(chunks, split_buffers_by_size(std::move(buffers), MAX_BUFFERS_IN_REQUEST, m_optimal_descs_in_chunk));
    } else {
        // Otherwise, split buffers into chunks of MAX_BUFFERS_IN_REQUEST.
        TRY(chunks, split_buffers_by_count(std::move(buffers), MAX_BUFFERS_IN_REQUEST));
    }

    // Set metadata for chunks. Only the final chunk has a valid callback.
    chunks.front().is_first = true;
    chunks.back().is_last = true;
    chunks.back().callback = request.callback;

    return chunks;
}

Expected<TransferChunks> TransferSplitter::split_buffers_by_count(TransferBuffers &&buffers, size_t buffers_per_chunk)
{
    const size_t total_buffers = buffers.size();

    TransferChunks transfer_chunks;
    transfer_chunks.reserve(DIV_ROUND_UP(total_buffers, buffers_per_chunk));

    size_t chunk_begin = 0;
    size_t chunk_end = 0;
    while (chunk_end != total_buffers) {
        chunk_end = std::min(chunk_begin + buffers_per_chunk, total_buffers);

        std::vector<TransferBuffer> chunk_buffers(buffers.begin() + chunk_begin, buffers.begin() + chunk_end);
        auto chunk = create_transfer_chunk(std::move(chunk_buffers));
        CHECK(chunk.num_descs <= m_max_descs_in_chunk, HAILO_INVALID_ARGUMENT, "Chunk-size is larger than maximum.");
        transfer_chunks.push_back(std::move(chunk));

        chunk_begin += buffers_per_chunk;
    }

    return transfer_chunks;
}

Expected<TransferChunks> TransferSplitter::split_buffers_by_size(TransferBuffers &&buffers,
    size_t max_buffers_in_chunk, size_t max_descs_in_chunk)
{
    TransferChunks chunks;
    TransferBuffers current_chunk_buffers;
    current_chunk_buffers.reserve(max_buffers_in_chunk);

    size_t descs_remaining = max_descs_in_chunk;
    size_t idx = 0;
    while (idx < buffers.size()) {
        auto &current_buffer = buffers[idx];
        const auto descs_in_buffer = DescriptorList::descriptors_in_buffer(current_buffer.size(), m_desc_page_size);

        // If we reached the maximum number of buffers, we have no more room remaining.
        if (current_chunk_buffers.size() == max_buffers_in_chunk) {
            descs_remaining = 0;
        }

        // Take next buffer and continue (so long as we have room).
        if (descs_in_buffer <= descs_remaining) {
            descs_remaining -= descs_in_buffer;
            current_chunk_buffers.push_back(std::move(current_buffer));
            idx++;
            continue;
        }

        // If we got here it means we need to split the chunk.

        // Split the current buffer, if we can.
        const size_t bytes_remaining = HailoRTCommon::align_down(descs_remaining * m_desc_page_size, m_dma_alignment);
        if (bytes_remaining > 0) {
            TRY(auto splits, current_buffer.split(bytes_remaining));

            // The head of the buffer is used in the current chunk.
            current_chunk_buffers.emplace_back(splits.first);

            // The tail of the buffer is added back to the original vector to be used next time.
            buffers[idx] = TransferBuffer(splits.second);
        }

        // Add a new TransferChunk to back of split, reset and start again.
        chunks.push_back(create_transfer_chunk(current_chunk_buffers));
        current_chunk_buffers.clear();
        descs_remaining = max_descs_in_chunk;
    }

    chunks.push_back(create_transfer_chunk(current_chunk_buffers));

    return chunks;
}

TransferChunk TransferSplitter::create_transfer_chunk(TransferBuffers buffers)
{
    uint32_t num_descs = 0;
    for (const auto &buffer : buffers) {
        num_descs += DescriptorList::descriptors_in_buffer(buffer.size(), m_desc_page_size);
    }

    TransferChunk chunk;
    chunk.buffers = std::move(buffers);
    chunk.callback = [] (hailo_status) {};
    chunk.num_descs = num_descs;
    chunk.is_first = false;
    chunk.is_last = false;

    return chunk;
}

} /* namespace vdma */
} /* namespace hailort */
