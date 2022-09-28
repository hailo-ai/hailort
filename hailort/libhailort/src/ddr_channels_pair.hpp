/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file ddr_channels_pair.hpp
 * @brief DDR channel pairs are pair of vdma channels used in the same context for skip-connection.
 **/

#ifndef _HAILO_DDR_CHANNELS_PAIR_HPP_
#define _HAILO_DDR_CHANNELS_PAIR_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "vdma/vdma_buffer.hpp"

namespace hailort
{

struct DdrChannelsInfo
{
    vdma::ChannelId d2h_channel_id;
    uint8_t d2h_stream_index;
    vdma::ChannelId h2d_channel_id;
    uint8_t h2d_stream_index;
    uint16_t row_size;
    uint16_t min_buffered_rows;
    // total_buffers_per_frame not same as core_buffer_per frame. 
    //(In DDR core buffer per frame is 1). Used to calc total host descriptors_per_frame. 
    uint16_t total_buffers_per_frame;
};

class DdrChannelsPair final
{
public:
    static Expected<DdrChannelsPair> create(HailoRTDriver &driver, const DdrChannelsInfo &ddr_channels_info);

    uint16_t descs_count() const;
    uint32_t descriptors_per_frame() const;
    Expected<Buffer> read();
    const DdrChannelsInfo & info() const;

    // Checks if the credits are automaticaly going from d2h channel to its h2d channel, or it needs to be done manually
    // (Using a fw task).
    bool need_manual_credit_management() const;

    CONTROL_PROTOCOL__host_buffer_info_t get_host_buffer_info() const;

private:
    DdrChannelsPair(std::unique_ptr<vdma::VdmaBuffer> &&buffer, const DdrChannelsInfo &ddr_channels_info);

    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_sg_buffer(HailoRTDriver &driver,
        uint32_t row_size, uint16_t buffered_rows);
    static Expected<std::unique_ptr<vdma::VdmaBuffer>> create_ccb_buffer(HailoRTDriver &driver,
        uint32_t row_size, uint16_t buffered_rows);

    static bool should_use_ccb(HailoRTDriver &driver);

    std::unique_ptr<vdma::VdmaBuffer> m_buffer;
    DdrChannelsInfo m_info;
};

} /* namespace hailort */

#endif /* _HAILO_DDR_CHANNELS_PAIR_HPP_ */
