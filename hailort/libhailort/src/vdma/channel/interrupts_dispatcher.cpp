/**
 * Copyright (c) 2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file interrupts_dispatcher.cpp
 * @brief Manages a thread that is waiting for channel interrupts.
 **/

#include "interrupts_dispatcher.hpp"
#include "hailo/hailort_common.hpp"
#include "common/os_utils.hpp"

namespace hailort {
namespace vdma {

Expected<std::unique_ptr<InterruptsDispatcher>> InterruptsDispatcher::create(std::reference_wrapper<HailoRTDriver> driver)
{
    auto thread = make_unique_nothrow<InterruptsDispatcher>(driver);
    CHECK_NOT_NULL_AS_EXPECTED(thread, HAILO_OUT_OF_HOST_MEMORY);
    return thread;
}

InterruptsDispatcher::InterruptsDispatcher(std::reference_wrapper<HailoRTDriver> driver) :
    m_driver(driver),
    m_is_running(false),
    m_channels_bitmap()
{}

InterruptsDispatcher::~InterruptsDispatcher()
{
    if (m_is_running) {
        stop();
    }
}

hailo_status InterruptsDispatcher::start(const ChannelsBitmap &channels_bitmap, bool enable_timestamp_measure,
    const ProcessIrqCallback &process_irq)
{
    CHECK(!m_is_running, HAILO_INVALID_OPERATION, "Interrupt thread already running");
    assert(m_channel_threads.empty());
    assert(m_channels_bitmap == ChannelsBitmap{});

    m_channels_bitmap = channels_bitmap;

    auto status = m_driver.get().vdma_interrupts_enable(m_channels_bitmap, enable_timestamp_measure);
    CHECK_SUCCESS(status, "Failed to enable vdma interrupts");

    // Setting m_is_running will allow the threads to run
    m_is_running = true;
    m_channel_threads.emplace_back([this, process_irq]() {
        // m_channels_bitmap may be changed by InterruptsDispatcher::stop. To avoid wait for 0 channels,
        // we use copy of m_channels_bitmap.
        ChannelsBitmap channels_bitmap_local = m_channels_bitmap;
        wait_interrupts(channels_bitmap_local, process_irq);
    });

    return HAILO_SUCCESS;
}

hailo_status InterruptsDispatcher::stop()
{
    CHECK(m_is_running, HAILO_INVALID_OPERATION, "Interrupts thread not started");
    assert(!m_channel_threads.empty());
    assert(m_channels_bitmap != ChannelsBitmap{});

    // Signal threads to stop execution
    m_is_running = false;

    // Calling disable interrupts will cause the vdma_interrupts_wait to return.
    auto status = m_driver.get().vdma_interrupts_disable(m_channels_bitmap);
    CHECK_SUCCESS(status, "Failed to disable vdma interrupts");

    m_channels_bitmap = ChannelsBitmap{};
    for (auto &thread : m_channel_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_channel_threads.clear();

    return HAILO_SUCCESS;
}

void InterruptsDispatcher::wait_interrupts(const ChannelsBitmap &channels_bitmap, const ProcessIrqCallback &process_irq)
{
    OsUtils::set_current_thread_name("CHANNEL_INTR");
    while (m_is_running) {
        // vdma_interrupts_wait is a blocking function that returns in this scenarios:
        //   1. We got a new interrupts, irq_data will be passed to the process_irq callback
        //   2. vdma_interrupts_disable will be called, vdma_interrupts_wait will return with an empty list.
        //   3. Other error returns - shouldn't really happen, we exit the interrupt thread.
        auto irq_data = m_driver.get().vdma_interrupts_wait(channels_bitmap);
        if (!irq_data.has_value()) {
            LOGGER__ERROR("Interrupt thread exit with {}", irq_data.status());
            break;
        }

        if (irq_data->channels_count > 0) {
            process_irq(irq_data.release());
        }
    }
}

} /* namespace vdma */
} /* namespace hailort */
