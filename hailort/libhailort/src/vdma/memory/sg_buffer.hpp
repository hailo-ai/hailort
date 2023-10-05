/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sg_buffer.hpp
 * @brief Scatter-gather vdma buffer, from the user-mode point of view the buffer is continuous,
 *        but not from the physical-memory point of view.
 *        The sg buffer contains 2 parts:
 *              - MappedBuffer - the actual buffer stores the data.
 *              - Descriptors list - each descritpor points to a single "dma page" in the MappedBuffer.
 *        The hw accept the descriptors list address and parses it to get the actual data.
 **/

#ifndef _HAILO_VDMA_SG_BUFFER_HPP_
#define _HAILO_VDMA_SG_BUFFER_HPP_

#include "os/hailort_driver.hpp"
#include "vdma/memory/vdma_buffer.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "vdma/memory/mapped_buffer.hpp"


namespace hailort {
namespace vdma {

class SgBuffer final : public VdmaBuffer {
public:
    static Expected<SgBuffer> create(HailoRTDriver &driver, size_t size, uint32_t desc_count, uint16_t desc_page_size,
        bool is_circular, HailoRTDriver::DmaDirection data_direction, vdma::ChannelId channel_id);

    virtual ~SgBuffer() = default;

    SgBuffer(const SgBuffer &) = delete;
    SgBuffer(SgBuffer &&) = default;
    SgBuffer& operator=(const SgBuffer &) = delete;
    SgBuffer& operator=(SgBuffer &&) = delete;

    virtual Type type() const override
    {
        return Type::SCATTER_GATHER;
    }

    virtual size_t size() const override;
    virtual uint64_t dma_address() const override;
    virtual uint16_t desc_page_size() const override;
    virtual uint32_t descs_count() const override;

    virtual hailo_status read(void *buf_dst, size_t count, size_t offset) override;
    virtual hailo_status write(const void *buf_src, size_t count, size_t offset) override;

    virtual Expected<uint32_t> program_descriptors(size_t transfer_size, InterruptsDomain last_desc_interrupts_domain,
        size_t desc_offset) override;

private:
    SgBuffer(std::shared_ptr<MappedBuffer> mapped_buffer, std::shared_ptr<DescriptorList> desc_list);

    // Initialization Dependency: The descriptor list points into the mapped buffer so it must be freed before it
    std::shared_ptr<MappedBuffer> m_mapped_buffer;
    std::shared_ptr<DescriptorList> m_desc_list;
};

} /* vdma */
} /* hailort */

#endif /* _HAILO_VDMA_SG_BUFFER_HPP_ */
