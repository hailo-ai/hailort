/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file interrupts_dispatcher.hpp
 * @brief Manages a thread that is waiting for channel interrupts.
 **/

#ifndef _HAILO_VDMA_INTERRUPTS_DISPATCHER_HPP_
#define _HAILO_VDMA_INTERRUPTS_DISPATCHER_HPP_

#include "vdma/driver/hailort_driver.hpp"
#include "vdma/channel/channels_group.hpp"

#include <thread>
#include <functional>
#include <condition_variable>

namespace hailort {
namespace vdma {

/// When needed, creates thread (or threads) that waits for interrupts on all channels.
class InterruptsDispatcher final {
public:
    // The actual irq process callback, should run quickly (blocks the interrupts thread).
    using ProcessIrqCallback = std::function<void(IrqData &&irq_data)>;

    static Expected<std::unique_ptr<InterruptsDispatcher>> create(std::reference_wrapper<HailoRTDriver> driver);
    explicit InterruptsDispatcher(std::reference_wrapper<HailoRTDriver> driver);
    ~InterruptsDispatcher();

    InterruptsDispatcher(const InterruptsDispatcher &) = delete;
    InterruptsDispatcher &operator=(const InterruptsDispatcher &) = delete;
    InterruptsDispatcher(InterruptsDispatcher &&) = delete;
    InterruptsDispatcher &operator=(InterruptsDispatcher &&) = delete;

    hailo_status start(const ChannelsBitmap &channels_bitmap, bool enable_timestamp_measure,
        const ProcessIrqCallback &process_irq);
    hailo_status start(const ChannelsGroup &channels_group);
    hailo_status stop();

private:

    void wait_interrupts();
    void signal_thread_quit();

    struct WaitContext {
        ChannelsBitmap bitmap;
        ProcessIrqCallback process_irq;
    };

    enum class ThreadState {
        // The interrupts thread is actually waiting for interrupts
        active,

        // The interrupts thread is done waiting for interrupts, it is waiting to be active.
        not_active,
    };

    std::mutex m_mutex;
    std::condition_variable m_cond;

    const std::reference_wrapper<HailoRTDriver> m_driver;

    ThreadState m_thread_state = ThreadState::not_active;
    // When m_wait_context is not nullptr, the thread should start waiting for interrupts.
    std::unique_ptr<WaitContext> m_wait_context;

    // m_should_quit is used to quit the thread (called on destruction)
    bool m_should_quit = false;
    std::thread m_interrupts_thread;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_VDMA_INTERRUPTS_DISPATCHER_HPP_ */
