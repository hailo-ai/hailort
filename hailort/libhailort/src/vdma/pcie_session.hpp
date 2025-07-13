/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_session.hpp
 * @brief Wrapper for 2 BoundaryChannels, one for input and one for output, used as a the transport layer of a
 *        session over PCIe.
 **/

#ifndef _HAILO_PCIE_SESSION_HPP_
#define _HAILO_PCIE_SESSION_HPP_

#include "hailo/hailort.h"
#include "vdma/channel/boundary_channel.hpp"
#include "vdma/channel/interrupts_dispatcher.hpp"
#include "vdma/channel/transfer_launcher.hpp"

namespace hailort
{

// A special magic number used to match each accept() with the corresponding connect().
// By using this magic, multiple servers can be implemented and run simultaneously on the same device.
using pcie_connection_port_t = uint16_t;
using PcieSessionType = HailoRTDriver::PcieSessionType;

struct CbParams {
    std::condition_variable cv;
    std::mutex mutex;
    hailo_status status;
};

/**
 * a PcieSession object need to be constructed both at the device side (via accept) or the host side (via connect).
 * After the session is created on both sides, the session can be used to send and receive data (based on the desired
 * protocol).
 *
 * This session object is a low-level object offering fast zero-copy data transfer over PCIe with negligible overhead.
 * To achieve this, the object have the following limitations:
 *      1. Buffers Alignment -
 *          a. The input/output buffers must be page aligned.
 *          b. The output buffer should own the full cache line size, otherwise invalidating the cache can cause memory
 *             corruption. Simple solution is to allocate the buffer using mmap.
 *      2. Buffer Sizes
 *          a. The buffer must be a multiple of 8 bytes.
 *          b. The max size of each buffer is (desc_page_size * (max_desc_count - 1)) = 32 MB.
 *      3. Read/Write synchronization - The size pattern of the writes in one edge must be the same as the size pattern
 *         of reads in the other edge.
 *         For example, if the host writes this pattern:
 *              WRITE 8 bytes
 *              WRITE 32 bytes
 *              WRITE 16 bytes
 *          The device must read the same pattern:
 *              READ 8 bytes
 *              READ 32 bytes
 *              READ 16 bytes
 *
 *          The protocol must ensure this behavior (for example by sending a const size header before each write).
 */
class PcieSession final {
public:
    static constexpr uint64_t MAX_ONGOING_TRANSFERS = 128;

    static Expected<PcieSession> connect(std::shared_ptr<HailoRTDriver> driver, pcie_connection_port_t port);
    static Expected<PcieSession> accept(std::shared_ptr<HailoRTDriver> driver, pcie_connection_port_t port);
    static hailo_status listen(std::shared_ptr<HailoRTDriver> driver, pcie_connection_port_t port, uint8_t backlog_size);

    ~PcieSession() { close(); }

    hailo_status write(const void *buffer, size_t size, std::chrono::milliseconds timeout);
    hailo_status read(void *buffer, size_t size, std::chrono::milliseconds timeout);

    bool is_read_ready(size_t transfer_size) const;
    bool is_write_ready(size_t transfer_size) const;

    hailo_status write_async(const void *buffer, size_t size, std::function<void(hailo_status)> &&callback);
    hailo_status read_async(void *buffer, size_t size, std::function<void(hailo_status)> &&callback);

    hailo_status write_async(TransferRequest &&request);
    hailo_status read_async(TransferRequest &&request);

    hailo_status close();

    inline PcieSessionType session_type() const
    {
        return m_session_type;
    }

    PcieSession(PcieSession &&other) :
        m_should_close(other.m_should_close.exchange(false)),
        m_driver(std::move(other.m_driver)),
        m_interrupts_dispatcher(std::move(other.m_interrupts_dispatcher)),
        m_transfer_launcher(std::move(other.m_transfer_launcher)),
        m_input(std::move(other.m_input)),
        m_output(std::move(other.m_output)),
        m_session_type(other.m_session_type)
    {}

private:

    using ChannelIdsPair = std::pair<vdma::ChannelId, vdma::ChannelId>;
    using DescriptorsListPair = std::pair<std::reference_wrapper<vdma::DescriptorList>, std::reference_wrapper<vdma::DescriptorList>>;

    static Expected<PcieSession> create(std::shared_ptr<HailoRTDriver> driver, vdma::ChannelId input_channel,
        vdma::ChannelId output_channel, vdma::DescriptorList &&input_desc_list, vdma::DescriptorList &&output_desc_list,
        PcieSessionType session_type);

    PcieSession(std::shared_ptr<HailoRTDriver> &&driver,
        std::unique_ptr<vdma::InterruptsDispatcher> &&interrupts_dispatcher,
        std::unique_ptr<vdma::TransferLauncher> &&transfer_launcher,
        vdma::BoundaryChannelPtr &&input, vdma::BoundaryChannelPtr &&output, PcieSessionType session_type) :
        m_driver(std::move(driver)),
        m_interrupts_dispatcher(std::move(interrupts_dispatcher)),
        m_transfer_launcher(std::move(transfer_launcher)),
        m_input(std::move(input)),
        m_output(std::move(output)),
        m_session_type(session_type)
    {}

    hailo_status launch_transfer_sync(vdma::BoundaryChannel &channel,
        void *buffer, size_t size, std::chrono::milliseconds timeout, CbParams &cb_params);
    // Static method to create descriptor lists for input and output channels
    static Expected<vdma::DescriptorList> create_desc_list(HailoRTDriver &driver);

    std::atomic<bool> m_should_close {true};
    std::shared_ptr<HailoRTDriver> m_driver;

    std::unique_ptr<vdma::InterruptsDispatcher> m_interrupts_dispatcher;
    std::unique_ptr<vdma::TransferLauncher> m_transfer_launcher;

    vdma::BoundaryChannelPtr m_input;
    vdma::BoundaryChannelPtr m_output;

    PcieSessionType m_session_type;

    // Following members are used only for sync transfers
    CbParams m_read_cb_params;
    CbParams m_write_cb_params;
};

} /* namespace hailort */

#endif /* _HAILO_PCIE_SESSION_HPP_ */
