/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
    m_interrupts_thread([this] { wait_interrupts(); })
{}

InterruptsDispatcher::~InterruptsDispatcher()
{
    if (m_wait_context != nullptr) {
        auto status = stop();
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Failed stopping interrupts dispatcher on destructor");
        }
    }

    if (m_interrupts_thread.joinable()) {
        signal_thread_quit();
        m_interrupts_thread.join();
    }
}

hailo_status InterruptsDispatcher::start(const ChannelsBitmap &channels_bitmap, bool enable_timestamp_measure,
    const ProcessIrqCallback &process_irq)
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        CHECK(m_wait_context == nullptr, HAILO_INVALID_OPERATION, "Interrupt thread already running");

        auto wait_context = make_unique_nothrow<WaitContext>(WaitContext{channels_bitmap, process_irq});
        CHECK_NOT_NULL(wait_context, HAILO_OUT_OF_HOST_MEMORY);
        m_wait_context = std::move(wait_context);

        auto status = m_driver.get().vdma_enable_channels(m_wait_context->bitmap, enable_timestamp_measure);
        CHECK_SUCCESS(status, "Failed to enable vdma channels");
    }
    m_cond.notify_one();

    return HAILO_SUCCESS;
}

hailo_status InterruptsDispatcher::start(const ChannelsGroup &channels_group)
{
    return start(channels_group.bitmap(), channels_group.should_measure_timestamp(),
        [channels_group=channels_group](IrqData &&irq_data) mutable {
            channels_group.process_interrupts(std::move(irq_data));
        });
}

hailo_status InterruptsDispatcher::stop()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    if (!m_wait_context) {
        // Already stopped
        return HAILO_SUCCESS;
    }

    // Nullify wait context so the thread will pause
    const auto bitmap = m_wait_context->bitmap;
    m_wait_context = nullptr;

    // Calling disable interrupts will cause the vdma_interrupts_wait to return.
    auto status = m_driver.get().vdma_disable_channels(bitmap);
    CHECK_SUCCESS(status, "Failed to disable vdma interrupts");

    // Needs to make sure that the interrupts thread is disabled.
    // The wait is needed because otherwise, on a fast stop() and start(), the next start() may accept
    // interrupts from previous run.
    m_cond.wait(lock, [&]{ return m_thread_state == ThreadState::not_active; });

    return HAILO_SUCCESS;
}

void InterruptsDispatcher::wait_interrupts()
{
    OsUtils::set_current_thread_name("CHANNEL_INTR");

    std::unique_lock<std::mutex> lock(m_mutex);
    while (true) {

        m_thread_state = ThreadState::not_active;
        m_cond.notify_one(); // Wake up stop()

        m_cond.wait(lock, [&]{ return m_should_quit || (m_wait_context != nullptr); });
        if (m_should_quit) {
            break;
        }

        m_thread_state = ThreadState::active;
        auto wait_context = *m_wait_context;

        // vdma_interrupts_wait is a blocking function that returns in this scenarios:
        //   1. We got a new interrupts, irq_data will be passed to the process_irq callback
        //   2. vdma_disable_channels will be called, vdma_interrupts_wait will return with an empty list.
        //   3. Other error returns - shouldn't really happen, we exit the interrupt thread.
        lock.unlock();
        auto irq_data = m_driver.get().vdma_interrupts_wait(wait_context.bitmap);
        lock.lock();

        if (!irq_data.has_value()) {
            LOGGER__ERROR("Interrupt thread exit with {}", irq_data.status());
            break;
        }

        if (irq_data->channels_count > 0) {
            wait_context.process_irq(irq_data.release());
        }
    }
}

void InterruptsDispatcher::signal_thread_quit()
{
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        assert(m_thread_state == ThreadState::not_active);
        m_should_quit = true;
    }
    m_cond.notify_one();
}

} /* namespace vdma */
} /* namespace hailort */
