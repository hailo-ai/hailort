/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file pcie_session.cpp
 **/

#include "pcie_session.hpp"
#include "vdma/channel/channels_group.hpp"

namespace hailort
{

Expected<PcieSession> PcieSession::connect(std::shared_ptr<HailoRTDriver> driver, pcie_connection_port_t port)
{
    TRY(auto input_desc_list, create_desc_list(*driver));
    TRY(auto output_desc_list, create_desc_list(*driver));

    TRY(auto channel_pair, driver->soc_connect(port, input_desc_list.handle(), output_desc_list.handle()));

    return PcieSession::create(driver, channel_pair.first, channel_pair.second, std::move(input_desc_list),
        std::move(output_desc_list), PcieSessionType::CLIENT);
}

Expected<PcieSession> PcieSession::accept(std::shared_ptr<HailoRTDriver> driver, pcie_connection_port_t port)
{
    TRY(auto input_desc_list, create_desc_list(*driver));
    TRY(auto output_desc_list, create_desc_list(*driver));

    TRY(auto channel_pair, driver->pci_ep_accept(port, input_desc_list.handle(), output_desc_list.handle()));

    return PcieSession::create(driver, channel_pair.first, channel_pair.second, std::move(input_desc_list),
        std::move(output_desc_list), PcieSessionType::SERVER);
}

hailo_status PcieSession::listen(std::shared_ptr<HailoRTDriver> driver, pcie_connection_port_t port, uint8_t backlog_size)
{
    CHECK_SUCCESS(driver->pci_ep_listen(port, backlog_size),
        "Failed to listen on port 0x{:X}", port);
    return HAILO_SUCCESS;
}

Expected<PcieSession> PcieSession::create(std::shared_ptr<HailoRTDriver> driver, vdma::ChannelId input_channel_id,
    vdma::ChannelId output_channel_id, vdma::DescriptorList &&input_desc_list, vdma::DescriptorList &&output_desc_list,
    PcieSessionType session_type)
{
    TRY(auto interrupts_dispatcher, vdma::InterruptsDispatcher::create(*driver));
    TRY(auto transfer_launcher, vdma::TransferLauncher::create());

    auto create_channel = [&](vdma::ChannelId id, vdma::BoundaryChannel::Direction dir, vdma::DescriptorList &&desc_list) {
        // TODO: HRT-15701 : remove 4
        return vdma::BoundaryChannel::create(*driver, id, dir, std::move(desc_list), *transfer_launcher,
            4 * MAX_ONGOING_TRANSFERS, true);
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

bool PcieSession::is_write_ready(size_t transfer_size) const
{
    return m_input->is_ready(transfer_size);
}

hailo_status PcieSession::write(const void *buffer, size_t size, std::chrono::milliseconds timeout)
{
    return launch_transfer_sync(*m_input, const_cast<void *>(buffer), size, timeout, m_write_cb_params);
}

bool PcieSession::is_read_ready(size_t transfer_size) const
{
    return m_output->is_ready(transfer_size);
}

hailo_status PcieSession::read(void *buffer, size_t size, std::chrono::milliseconds timeout)
{
    return launch_transfer_sync(*m_output, buffer, size, timeout, m_read_cb_params);
}

hailo_status PcieSession::write_async(const void *buffer, size_t size, TransferDoneCallback &&callback)
{
    return write_async(to_request(const_cast<void *>(buffer), size, std::move(callback)));
}

hailo_status PcieSession::write_async(TransferRequest &&request)
{
    TransferDoneCallback callback = request.callback;
    std::vector<TransferBuffer> aligned_buffers{};
    aligned_buffers.reserve(request.transfer_buffers.size());

    for (auto &transfer_buf : request.transfer_buffers) {
        if (transfer_buf.is_aligned_for_dma()) {
            aligned_buffers.emplace_back(std::move(transfer_buf));
            continue;
        }

        TRY(auto bounce_buf, Buffer::create_shared(transfer_buf.size(), BufferStorageParams::create_dma()));
        transfer_buf.copy_to(MemoryView(*bounce_buf));
        aligned_buffers.emplace_back(MemoryView(*bounce_buf));
        callback = [bounce_buf=std::move(bounce_buf), callback=callback](hailo_status status)
        {
            // Note: We need this callback wrapper to keep the bounce-buffer alive.
            callback(status);
        };
    }

    return m_input->launch_transfer(TransferRequest(std::move(aligned_buffers), callback));
}

hailo_status PcieSession::read_async(void *buffer, size_t size, TransferDoneCallback &&callback)
{
    return read_async(to_request(buffer, size, std::move(callback)));
}

hailo_status PcieSession::read_async(TransferRequest &&request)
{
    TransferDoneCallback callback = request.callback;
    std::vector<TransferBuffer> aligned_buffers{};
    aligned_buffers.reserve(request.transfer_buffers.size());

    for (auto &transfer_buf : request.transfer_buffers) {
        if (transfer_buf.is_aligned_for_dma()) {
            aligned_buffers.emplace_back(std::move(transfer_buf));
            continue;
        }

        TRY(auto bounce_buf, Buffer::create_shared(transfer_buf.size(), BufferStorageParams::create_dma()));
        aligned_buffers.emplace_back(MemoryView(*bounce_buf));
        callback = [transfer_buf=transfer_buf,
                    bounce_buf=std::move(bounce_buf),
                    callback=callback](hailo_status status) mutable
        {
            transfer_buf.copy_from(MemoryView(*bounce_buf));
            callback(status);
        };
    }

    return m_output->launch_transfer(TransferRequest(std::move(aligned_buffers), callback));
}

hailo_status PcieSession::close()
{
    hailo_status status = HAILO_SUCCESS; // Success oriented
    if (m_should_close.exchange(false)) {
        LOGGER__TRACE("Closing session now");
    } else {
        return status;
    }

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

hailo_status PcieSession::launch_transfer_sync(vdma::BoundaryChannel &channel,
    void *buffer, size_t size, std::chrono::milliseconds timeout, CbParams &cb_params)
{
    cb_params.status = HAILO_UNINITIALIZED;
    auto callback = [&cb_params](hailo_status status) mutable {
        {
            std::unique_lock<std::mutex> lock(cb_params.mutex);
            assert(status != HAILO_UNINITIALIZED);
            cb_params.status = status;
        }
        cb_params.cv.notify_one();
    };

    auto status = channel.launch_transfer(to_request(buffer, size, callback));
    if (HAILO_STREAM_ABORT == status) {
        return status;
    }
    CHECK_SUCCESS(status);

    std::unique_lock<std::mutex> lock(cb_params.mutex);
    CHECK(cb_params.cv.wait_for(lock, timeout, [&] { return cb_params.status != HAILO_UNINITIALIZED; }),
        HAILO_TIMEOUT, "Timeout waiting for transfer completion");
    return cb_params.status;
}

Expected<vdma::DescriptorList> PcieSession::create_desc_list(HailoRTDriver &driver)
{
    const bool circular = true;
    const auto desc_params = driver.get_sg_desc_params();
    TRY(auto desc_list, vdma::DescriptorList::create(desc_params.max_descs_count, desc_params.default_page_size,
        circular, driver));
    return desc_list;
}

} /* namespace hailort */
