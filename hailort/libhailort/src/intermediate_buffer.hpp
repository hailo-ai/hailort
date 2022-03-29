/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file intermediate_buffer.hpp
 * @brief Manages intermediate buffer for inter-context or ddr channels.
 */

#ifndef _HAILO_INTERMEDIATE_BUFFER_HPP_
#define _HAILO_INTERMEDIATE_BUFFER_HPP_

#include "os/hailort_driver.hpp"
#include "vdma_buffer.hpp"
#include "vdma_descriptor_list.hpp"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"


namespace hailort
{

class IntermediateBuffer {
public:

    enum class Type {
        EXTERNAL_DESC
    };

    static Expected<std::unique_ptr<IntermediateBuffer>> create(Type type, HailoRTDriver &driver,
        const uint32_t transfer_size, const uint16_t batch_size);

    virtual ~IntermediateBuffer() = default;
    IntermediateBuffer(const IntermediateBuffer &) = delete;
    IntermediateBuffer& operator=(const IntermediateBuffer &) = delete;
    IntermediateBuffer(IntermediateBuffer &&) = default;
    IntermediateBuffer& operator=(IntermediateBuffer &&) = delete;


    virtual hailo_status program_inter_context() = 0;

    // Returns the amount of programed descriptors
    virtual Expected<uint16_t> program_ddr() = 0;
    virtual Expected<uint16_t> program_host_managed_ddr(uint16_t row_size, uint32_t buffered_rows,
        uint16_t initial_desc_offset) = 0;

    virtual uint64_t dma_address() const = 0;
    virtual uint16_t descriptors_in_frame() const = 0;
    virtual uint16_t desc_page_size() const = 0;
    virtual uint16_t descs_count() const = 0;
    virtual uint8_t depth() const = 0;

    // Should be only used for host managed ddr buffer, in the future this function may return nullptr (on CCB
    // case where there is no descriptors list)
    virtual VdmaDescriptorList* get_desc_list() = 0;

    virtual Expected<Buffer> read() = 0;

protected:
    IntermediateBuffer() = default;
};

class ExternalDescIntermediateBuffer : public IntermediateBuffer
{
public:

    static Expected<std::unique_ptr<IntermediateBuffer>> create(HailoRTDriver &driver, const uint32_t transfer_size,
        const uint16_t batch_size);

    ExternalDescIntermediateBuffer(VdmaBuffer &&buffer, VdmaDescriptorList &&desc_list,
        const uint32_t transfer_size, const uint32_t transfers_count) :
           m_buffer(std::move(buffer)), m_desc_list(std::move(desc_list)),
           m_transfer_size(transfer_size), m_transfers_count(transfers_count) {};

    hailo_status program_inter_context() override;

    // Returns the amount of programed descriptors
    Expected<uint16_t> program_ddr() override;
    Expected<uint16_t> program_host_managed_ddr(uint16_t row_size, uint32_t buffered_rows,
        uint16_t initial_desc_offset) override;

    uint64_t dma_address() const override
    {
        return m_desc_list.dma_address();
    }

    uint16_t descriptors_in_frame() const override
    {
        return static_cast<uint16_t>(m_desc_list.descriptors_in_buffer(m_transfer_size));
    }

    uint16_t desc_page_size() const override
    {
        return m_desc_list.desc_page_size();
    }

    uint16_t descs_count() const override
    {
        return static_cast<uint16_t>(m_desc_list.count());
    }

    uint8_t depth() const override
    {
        return m_desc_list.depth();
    }

    // Should be only used for host managed ddr buffer, in the future this function may return nullptr (on CCB
    // case where there is no descriptors list)
    VdmaDescriptorList* get_desc_list() override
    {
        return &m_desc_list;
    }

    Expected<Buffer> read() override;

private:

    VdmaBuffer m_buffer;
    VdmaDescriptorList m_desc_list;
    const uint32_t m_transfer_size;
    const uint32_t m_transfers_count;
};

} /* namespace hailort */

#endif /* _HAILO_INTERMEDIATE_BUFFER_HPP_ */