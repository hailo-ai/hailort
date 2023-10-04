/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
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

    static Expected<BufferSizesRequirements> get_sg_buffer_requirements_single_transfer(uint16_t max_desc_page_size,
        uint16_t min_batch_size, uint16_t max_batch_size, uint32_t transfer_size, bool is_circular,
        const bool force_default_page_size, const bool force_batch_size);
    static Expected<BufferSizesRequirements> get_sg_buffer_requirements_multiple_transfers(uint16_t max_desc_page_size,
        uint16_t batch_size, const std::vector<uint32_t> &transfer_sizes, bool is_circular,
        const bool force_default_page_size, const bool force_batch_size);

    static Expected<BufferSizesRequirements> get_ccb_buffer_requirements_single_transfer(uint16_t batch_size,
        uint32_t transfer_size, bool is_circular);

private:
    static uint16_t find_initial_desc_page_size(const std::vector<uint32_t> &transfer_sizes);
    static uint32_t get_required_descriptor_count(const std::vector<uint32_t> &transfer_sizes, uint16_t desc_page_size);

    const uint32_t m_descs_count;
    const uint16_t m_desc_page_size;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_BUFFER_REQUIREMENTS_HPP_ */
