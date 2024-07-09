/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file pcie_session.cpp
 **/

#include "pcie_session.hpp"
#include "vdma/channel/channels_group.hpp"

namespace hailort
{

static constexpr uint64_t MAX_ONGOING_TRANSFERS = 128;

Expected<PcieSession> PcieSession::connect(std::shared_ptr<HailoRTDriver> driver, pcie_connection_port_t port)
{
    TRY(auto input_desc_list, create_desc_list(*driver));
    TRY(auto output_desc_list, create_desc_list(*driver));

    TRY(auto channel_pair, driver->soc_connect(input_desc_list.handle(), output_desc_list.handle()));

    (void)port;

    return PcieSession::create(driver, channel_pair.first, channel_pair.second, std::move(input_desc_list),
        std::move(output_desc_list), PcieSessionType::CLIENT);
}

Expected<PcieSession> PcieSession::accept(std::shared_ptr<HailoRTDriver> driver, pcie_connection_port_t port)
{
    TRY(auto input_desc_list, create_desc_list(*driver));
    TRY(auto output_desc_list, create_desc_list(*driver));

    TRY(auto channel_pair, driver->pci_ep_accept(input_desc_list.handle(), output_desc_list.handle()));

    (void)port;

    return PcieSession::create(driver, channel_pair.first, channel_pair.second, std::move(input_desc_list),
        std::move(output_desc_list), PcieSessionType::SERVER);
}

Expected<PcieSession> PcieSession::create(std::shared_ptr<HailoRTDriver> driver, vdma::ChannelId input_channel_id,
    vdma::ChannelId output_channel_id, vdma::DescriptorList &&input_desc_list, vdma::DescriptorList &&output_desc_list,
    PcieSessionType session_type)
{
    // TODO: HRT-14038 - remove this to support multiple connections. Until then, mark as used to allow ctrl+c handle
    CHECK_SUCCESS(driver->mark_as_used());

    TRY(auto interrupts_dispatcher, vdma::InterruptsDispatcher::create(*driver));
    TRY(auto transfer_launcher, vdma::TransferLauncher::create());

    auto create_channel = [&](vdma::ChannelId id, vdma::BoundaryChannel::Direction dir, vdma::DescriptorList &&desc_list) {
        return vdma::BoundaryChannel::create(*driver, id, dir, std::move(desc_list), *transfer_launcher,
            MAX_ONGOING_TRANSFERS);
    };

    TRY(auto input_channel, create_channel(input_channel_id, vdma::BoundaryChannel::Direction::H2D, std::move(input_desc_list)));
    TRY(auto output_channel, create_channel(output_channel_id, vdma::BoundaryChannel::Direction::D2H, std::move(output_desc_list)));

    CHECK_SUCCESS(interrupts_dispatcher->start(vdma::ChannelsGroup{input_channel, output_channel}));
    CHECK_SUCCESS(transfer_launcher->start());

    CHECK_SUCCESS(input_channel->activate());
    CHECK_SUCCESS(output_channel->activate());

    return PcieSession(std::move(driver), std::move(interrupts_dispatcher), std::move(transfer_launcher),
        std::move(input_channel), std::move(output_channel), session_type);
}

hailo_status PcieSession::write(const void *buffer, size_t size, std::chrono::milliseconds timeout)
{
    return launch_transfer_sync(*m_input, const_cast<void *>(buffer), size, timeout);
}

hailo_status PcieSession::read(void *buffer, size_t size, std::chrono::milliseconds timeout)
{
    return launch_transfer_sync(*m_output, buffer, size, timeout);
}

hailo_status PcieSession::write_async(const void *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    return launch_transfer_async(*m_input, const_cast<void *>(buffer), size, std::move(callback));
}

hailo_status PcieSession::read_async(void *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    return launch_transfer_async(*m_output, buffer, size, std::move(callback));
}

hailo_status PcieSession::close()
{
    hailo_status status = HAILO_SUCCESS; // Success orietnted

    // First, close all host resources, disallow new transfers
    m_input->deactivate();
    m_output->deactivate();

    auto stop_status = m_interrupts_dispatcher->stop();
    if (HAILO_SUCCESS != stop_status) {
        LOGGER__ERROR("Failed to stop interrupts dispatcher with status {}", stop_status);
        status = stop_status;
    }

    // Then, close the connection to ABORT any vDMA channel.
    stop_status = m_driver->close_connection(m_input->get_channel_id(), m_output->get_channel_id(), m_session_type);
    if (HAILO_SUCCESS != stop_status) {
        LOGGER__ERROR("Failed to close connection with status {}", stop_status);
        status = stop_status;
    }

    // Finally, cancel any pending transfer (must happen after the vDMA channel was aborted).
    m_input->cancel_pending_transfers();
    m_output->cancel_pending_transfers();

    stop_status = m_transfer_launcher->stop();
    if (HAILO_SUCCESS != stop_status) {
        LOGGER__ERROR("Failed to stop transfer launcher with status {}", stop_status);
        status = stop_status;
    }

    return status;
}

uint64_t PcieSession::max_transfer_size()
{
    // The max transfer size, is the size feet in MAX_SG_DESCS_COUNT -1.
    // We don't use the last 8 descritpors to max sure max_transfer_size is aligened to 4K
    return vdma::DEFAULT_SG_PAGE_SIZE * (MAX_SG_DESCS_COUNT - 8);
}

hailo_status PcieSession::launch_transfer_sync(vdma::BoundaryChannel &channel,
    void *buffer, size_t size, std::chrono::milliseconds timeout)
{
    std::mutex mutex;
    std::condition_variable cv;
    hailo_status transfer_status = HAILO_UNINITIALIZED;
    auto callback = [&](hailo_status status) mutable {
        {
            std::unique_lock<std::mutex> lock(mutex);
            assert(status != HAILO_UNINITIALIZED);
            transfer_status = status;
        }
        cv.notify_one();
    };

    auto status = launch_transfer_async(channel, buffer, size, std::move(callback));
    if (HAILO_STREAM_ABORT == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(mutex);
    CHECK(cv.wait_for(lock, timeout, [&] { return transfer_status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");
    return transfer_status;
}

hailo_status PcieSession::launch_transfer_async(vdma::BoundaryChannel &channel,
    void *buffer, size_t size, std::function<void(hailo_status)> &&callback)
{
    TransferRequest request{
        {TransferBuffer(MemoryView(buffer, size))},
        std::move(callback)
    };

    return channel.launch_transfer(std::move(request));
}

Expected<vdma::DescriptorList> PcieSession::create_desc_list(HailoRTDriver &driver)
{
    const bool circular = true;
    TRY(auto desc_list, vdma::DescriptorList::create(MAX_SG_DESCS_COUNT, vdma::DEFAULT_SG_PAGE_SIZE, circular, driver));
    return desc_list;
}

} /* namespace hailort */
