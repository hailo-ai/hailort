/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file transfer_launcher.cpp
 * @brief Manages a thread that launches non-bound async vdma read/writes
 **/

#include "transfer_launcher.hpp"
#include "common/utils.hpp"
#include "common/os_utils.hpp"

namespace hailort {
namespace vdma {

Expected<std::unique_ptr<TransferLauncher>> TransferLauncher::create()
{
    auto thread = make_unique_nothrow<TransferLauncher>();
    CHECK_NOT_NULL_AS_EXPECTED(thread, HAILO_OUT_OF_HOST_MEMORY);
    return thread;
}

TransferLauncher::TransferLauncher() :
    m_mutex(),
    m_cond(),
    m_queue(),
    m_should_quit(false),
    m_thread_active(false),
    m_worker_thread([this] { worker_thread(); })
{}

TransferLauncher::~TransferLauncher()
{
    const auto status = stop();
    if (status != HAILO_SUCCESS) {
        LOGGER__ERROR("Failed stopping transfer launcher thread on destructor");
    }

    if (m_worker_thread.joinable()) {
        signal_thread_quit();
        m_worker_thread.join();
    }
}

hailo_status TransferLauncher::enqueue_transfer(Transfer &&transfer)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.emplace(std::move(transfer));
    }

    m_cond.notify_one();
    return HAILO_SUCCESS;
}

hailo_status TransferLauncher::start()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        CHECK(!m_thread_active, HAILO_INVALID_OPERATION, "Transfer launcher thread already running");

        m_thread_active = true;
    }
    m_cond.notify_one();

    return HAILO_SUCCESS;
}

hailo_status TransferLauncher::stop()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (!m_thread_active) {
        // Already stopped
        return HAILO_SUCCESS;
    }

    m_thread_active = false;

    while (!m_queue.empty()) {
        // No need signal that the transfer was aborted, it'll be done in BoundaryChannel::cancel_pending_transfers
        m_queue.pop();
    }

    // TODO: Keep stop flow used in interrupt thread? (HRT-13110)
    //       E.g. look for comment "The wait is needed because otherwise, on a fast stop()..."

    return HAILO_SUCCESS;
}

void TransferLauncher::worker_thread()
{
    OsUtils::set_current_thread_name("TRANSFR_LNCH");

    while (true) {
        Transfer transfer;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait(lock, [this] { return m_should_quit || (!m_queue.empty() && m_thread_active); });
            if (m_should_quit) {
                return;
            }

            // There's work to do
            transfer = std::move(m_queue.front());
            m_queue.pop();
        }
        transfer();
    }
}

void TransferLauncher::signal_thread_quit()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_should_quit = true;
    }
    m_cond.notify_all();
}

} /* namespace vdma */
} /* namespace hailort */
