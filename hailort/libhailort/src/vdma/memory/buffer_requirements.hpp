/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file buffer_requirements.hpp
 * @brief Calculate all vdma buffer size requirements, including actual size, amount of descriptors and the actual desc
 *        count.
 **/

#ifndef _HAILO_BUFFER_REQUIREMENTS_HPP_
#define _HAILO_BUFFER_REQUIREMENTS_HPP_

#include "hailo/expected.hpp"
#include "vdma/memory/vdma_edge_layer.hpp"

#include <cstdint>
#include <cassert>
#include <vector>


namespace hailort {
namespace vdma {

class BufferSizesRequirements final {
public:
    BufferSizesRequirements(uint32_t descs_count, uint16_t desc_page_size) :
        m_descs_count(descs_count),
        m_desc_page_size(desc_page_size)
    {
        assert(m_descs_count > 0);
        assert(m_desc_page_size > 0);
    }

    uint32_t descs_count() const { return m_descs_count; }
    uint16_t desc_page_size() const { return m_desc_page_size; }
    uint32_t buffer_size() const { return m_descs_count * m_desc_page_size; }

    static Expected<BufferSizesRequirements> get_buffer_requirements_for_boundary_channels(HailoRTDriver &driver,
        uint32_t max_shmifo_size, uint16_t min_active_trans, uint16_t max_active_trans, uint32_t transfer_size);

    static Expected<BufferSizesRequirements> get_buffer_requirements_multiple_transfers(
        vdma::VdmaBuffer::Type buffer_type, uint16_t max_desc_page_size,
        uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes, bool is_circular,
        bool force_default_page_size, bool force_batch_size, bool is_ddr);

    static Expected<BufferSizesRequirements> get_buffer_requirements_single_transfer(
        vdma::VdmaBuffer::Type buffer_type, uint16_t max_desc_page_size,
        uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size, bool is_circular,
        bool force_default_page_size, bool force_batch_size, bool is_vdma_aligned_buffer, bool is_ddr);

private:
    static uint16_t find_initial_desc_page_size(vdma::VdmaBuffer::Type buffer_type, const std::vector<uint32_t> &transfer_sizes,
        uint16_t max_desc_page_size, bool force_default_page_size, uint16_t min_page_size);
    static uint32_t get_required_descriptor_count(const std::vector<uint32_t> &transfer_sizes, uint16_t desc_page_size,
        bool is_ddr_layer);

    uint32_t m_descs_count;
    uint16_t m_desc_page_size;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_BUFFER_REQUIREMENTS_HPP_ */
