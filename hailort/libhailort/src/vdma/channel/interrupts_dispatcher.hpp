/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file interrupts_dispatcher.hpp
 * @brief Manages a thread that is waiting for channel interrupts.
 **/

#ifndef _HAILO_VDMA_INTERRUPTS_DISPATCHER_HPP_
#define _HAILO_VDMA_INTERRUPTS_DISPATCHER_HPP_

#include "os/hailort_driver.hpp"
#include <thread>
#include <functional>

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

    // TODO: HRT-9590 remove interrupt_thread_per_channel, use it by default
    hailo_status start(const ChannelsBitmap &channels_bitmap, bool enable_timestamp_measure,
        const ProcessIrqCallback &process_irq);
    hailo_status stop();

private:

    void wait_interrupts(const ChannelsBitmap &channels_bitmap, const ProcessIrqCallback &process_irq);

    const std::reference_wrapper<HailoRTDriver> m_driver;
    std::atomic<bool> m_is_running;
    ChannelsBitmap m_channels_bitmap;
    std::vector<std::thread> m_channel_threads;
};

} /* namespace vdma */
} /* namespace hailort */

#endif /* _HAILO_VDMA_INTERRUPTS_DISPATCHER_HPP_ */
