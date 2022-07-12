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
#include "vdma/vdma_buffer.hpp"
#include "vdma_descriptor_list.hpp"
#include "vdma/mapped_buffer.hpp"

namespace hailort {
namespace vdma {

class SgBuffer final : public VdmaBuffer {
public:
    static Expected<SgBuffer> create(HailoRTDriver &driver, uint32_t desc_count, uint16_t desc_page_size,
        HailoRTDriver::DmaDirection data_direction, uint8_t channel_index = 0);

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
    uint8_t depth() const;

    // Should be only used for host managed ddr buffer, in the future this function may return nullptr (on CCB
    // case where there is no descriptors list)
    virtual ExpectedRef<VdmaDescriptorList> get_desc_list() override;

    virtual hailo_status read(void *buf_dst, size_t count, size_t offset) override;
    virtual hailo_status write(const void *buf_src, size_t count, size_t offset) override;

    virtual Expected<uint32_t> program_descriptors(size_t transfer_size, VdmaInterruptsDomain first_desc_interrupts_domain,
        VdmaInterruptsDomain last_desc_interrupts_domain, size_t desc_offset, bool is_circular) override;
    virtual hailo_status reprogram_device_interrupts_for_end_of_batch(size_t transfer_size, uint16_t batch_size,
        VdmaInterruptsDomain new_interrupts_domain) override;

private:
    SgBuffer(VdmaDescriptorList &&desc_list, MappedBuffer &&mapped_buffer) :
        m_desc_list(std::move(desc_list)),
        m_mapped_buffer(std::move(mapped_buffer))
    {}

    VdmaDescriptorList m_desc_list;
    MappedBuffer m_mapped_buffer;
};

} /* vdma */
} /* hailort */

#endif /* _HAILO_VDMA_SG_BUFFER_HPP_ */
