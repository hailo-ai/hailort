/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file sg_buffer.hpp
 * @brief Scatter-gather vdma buffer, from the user-mode point of view the buffer is continuous,
 *        but not from the physical-memory point of view.
 **/

#ifndef _HAILO_VDMA_SG_BUFFER_HPP_
#define _HAILO_VDMA_SG_BUFFER_HPP_

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/memory/vdma_buffer.hpp"
#include "vdma/memory/descriptor_list.hpp"
#include "vdma/memory/mapped_buffer.hpp"


namespace hailort {
namespace vdma {

class SgBuffer final : public VdmaBuffer {
public:
    static Expected<SgBuffer> create(HailoRTDriver &driver, size_t size, HailoRTDriver::DmaDirection data_direction);

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
    virtual hailo_status read(void *buf_dst, size_t count, size_t offset) override;
    virtual hailo_status write(const void *buf_src, size_t count, size_t offset) override;
    std::shared_ptr<MappedBuffer> get_mapped_buffer();

private:
    SgBuffer(std::shared_ptr<MappedBuffer> mapped_buffer);

    std::shared_ptr<MappedBuffer> m_mapped_buffer;
};

} /* vdma */
} /* hailort */

#endif /* _HAILO_VDMA_SG_BUFFER_HPP_ */
