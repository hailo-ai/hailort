/**
 * Copyright (c) 2024 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file transfer_launcher.hpp
 * @brief Manages a thread that launches non-bound async vdma read/writes
 **/

#ifndef _HAILO_TRANSFER_LAUNCHER_HPP_
#define _HAILO_TRANSFER_LAUNCHER_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>

namespace hailort {
namespace vdma {

class TransferLauncher final
{
public:
    using Transfer = std::function<void()>;

    static Expected<std::unique_ptr<TransferLauncher>> create();
    TransferLauncher();
    ~TransferLauncher();

    TransferLauncher(TransferLauncher &&) = delete;
    TransferLauncher(const TransferLauncher &) = delete;
    TransferLauncher &operator=(TransferLauncher &&) = delete;
    TransferLauncher &operator=(const TransferLauncher &) = delete;

    hailo_status enqueue_transfer(Transfer &&transfer);
    hailo_status start();
    hailo_status stop();

private:
    void worker_thread();
    void signal_thread_quit();

    std::mutex m_mutex;
    std::condition_variable m_cond;
    // TODO: use SpscQueue (HRT-10554)
    std::queue<Transfer> m_queue;
    // m_should_quit is used to quit the thread (called on destruction)
    bool m_should_quit;
    bool m_thread_active;
    std::thread m_worker_thread;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_TRANSFER_LAUNCHER_HPP_ */
