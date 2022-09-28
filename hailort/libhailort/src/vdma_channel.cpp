#include "vdma_channel.hpp"
#include "vdma_channel_regs.hpp"
#include "hw_consts.hpp"
#include "common/logger_macros.hpp"
#include "common/utils.hpp"
#include "microprofile.h"
#include "vdma/sg_buffer.hpp"
#include "vdma_descriptor_list.hpp"

#include "hailo/hailort_common.hpp"

#include <list>
#include <chrono>
#include <thread>

#include <iostream>

namespace hailort
{

#define FD_READ_SIZE (8)
#define MIN_TIMEOUT_DDR (1000)

/* PLDA descriptor control */
#define PCIE_DESCRIPTOR_CONTROL_CLR(src)\
    src = (src & (~(uint32_t)0xFF))
#define PCIE_DESCRIPTOR_CONTROL_SET_DESC_STATUS_REQ(src)\
    src = ((src) | 0x01)
#define PCIE_DESCRIPTOR_CONTROL_SET_DESC_STATUS_REQ_ERR(src)\
    src = ((src) | 0x02)
#define PCIE_DESCRIPTOR_CONTROL_SET_DESC_SET_IRQ_ON_ERROR(src)\
    src = ((src) | 0x04)
#define PCIE_DESCRIPTOR_CONTROL_SET_DESC_SET_IRQ_ON_AXI_DOMAIN(src)\
    src = ((src) | 0x10)


void VdmaChannel::State::lock()
{
#ifndef _MSC_VER
    int err = pthread_mutex_lock(&m_state_lock);
    if (0 != err) {
        LOGGER__ERROR("Failed destory vdma channel mutex, errno {}", err);
        assert(true);
    }
#else
    EnterCriticalSection(&m_state_lock);
#endif
}

void VdmaChannel::State::unlock()
{
#ifndef _MSC_VER
    int err = pthread_mutex_unlock(&m_state_lock);
    assert(0 == err);
    (void)err;
#else
    LeaveCriticalSection(&m_state_lock);
#endif
}

Expected<VdmaChannel> VdmaChannel::create(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver,
    uint16_t requested_desc_page_size, uint32_t stream_index, LatencyMeterPtr latency_meter, uint16_t transfers_per_axi_intr)
{
    CHECK_AS_EXPECTED(Direction::BOTH != direction, HAILO_INVALID_ARGUMENT);

    hailo_status status = HAILO_UNINITIALIZED;
    auto desc_page_size_value = driver.calc_desc_page_size(requested_desc_page_size);
    CHECK_AS_EXPECTED(is_powerof2(desc_page_size_value), HAILO_INVALID_ARGUMENT,
        "Descriptor page_size must be a power of two.");
    CHECK_AS_EXPECTED(channel_id.channel_index < VDMA_CHANNELS_PER_ENGINE, HAILO_INVALID_ARGUMENT,
        "Invalid DMA channel index {}", channel_id.channel_index);
    CHECK_AS_EXPECTED(channel_id.engine_index < driver.dma_engines_count(), HAILO_INVALID_ARGUMENT,
        "Invalid DMA engine index {}, max {}", channel_id.engine_index, driver.dma_engines_count());

    VdmaChannel object(channel_id, direction, driver, stream_index, latency_meter, desc_page_size_value, 
        transfers_per_axi_intr, status);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed creating VdmaChannel");
        return make_unexpected(status);
    }
    return object;
}

VdmaChannel::VdmaChannel(vdma::ChannelId channel_id, Direction direction, HailoRTDriver &driver, 
    uint32_t stream_index, LatencyMeterPtr latency_meter, uint16_t desc_page_size, uint16_t transfers_per_axi_intr, 
    hailo_status &status)
    : m_d2h_callback_thread(nullptr), m_channel_id(channel_id),
      m_direction(direction), m_driver(driver),
      m_host_registers(driver, channel_id, direction),
      m_device_registers(driver, channel_id, other_direction(direction)), m_desc_page_size(desc_page_size),
      m_stream_index(stream_index), m_latency_meter(latency_meter), m_channel_enabled(false), m_channel_is_active(false),
      m_transfers_per_axi_intr(transfers_per_axi_intr), m_pending_buffers_sizes(0), m_pending_num_avail_offset(0), m_is_waiting_for_channel_completion(false),
      m_is_aborted_by_internal_source(false)
{
    if (m_transfers_per_axi_intr == 0) {
        LOGGER__ERROR("Invalid transfers per axi interrupt");
        status = HAILO_INVALID_ARGUMENT;
        return;
    }

    auto channel_handle_memory = MmapBuffer<HailoRTDriver::VdmaChannelHandle>::create_shared_memory(sizeof(HailoRTDriver::VdmaChannelHandle));
    if (!channel_handle_memory) {
        LOGGER__ERROR("Failed allocating shared memory for channel, err = {}", channel_handle_memory.status());
        status = channel_handle_memory.status();
        return;
    }
    m_channel_handle = channel_handle_memory.release();
    *m_channel_handle = HailoRTDriver::INVALID_VDMA_CHANNEL_HANDLE;

    // The channel will become active after calling start_allocated_channel().
    // The driver cleans the channel's state, in case the last shutdown wasn't successful.
    m_channel_enabled = true;

    status = HAILO_SUCCESS;
}

VdmaChannel::~VdmaChannel()
{
    if (m_channel_enabled) {
        stop_channel();
        m_channel_enabled = false;
        if (Direction::H2D == m_direction) {
            m_can_write_buffer_cv.notify_all();
        } else {
            m_can_read_buffer_cv.notify_all();
        }
    }

    if (m_state) {
#ifndef _MSC_VER
        int err = pthread_mutex_destroy(&m_state->m_state_lock);
        if (0 != err) {
            LOGGER__ERROR("Failed destory vdma channel mutex, errno {}", err);
        }
#else
        DeleteCriticalSection(&m_state->m_state_lock);
#endif
    }
}

VdmaChannel::VdmaChannel(VdmaChannel &&other) noexcept:
 m_d2h_callback_thread(std::move(other.m_d2h_callback_thread)),
 m_channel_id(std::move(other.m_channel_id)),
 m_direction(other.m_direction),
 m_driver(other.m_driver),
 m_host_registers(std::move(other.m_host_registers)),
 m_device_registers(std::move(other.m_device_registers)),
 m_desc_page_size(other.m_desc_page_size),
 m_buffer(std::move(other.m_buffer)),
 m_stream_index(std::move(other.m_stream_index)),
 m_latency_meter(std::move(other.m_latency_meter)),
 m_state(std::move(other.m_state)),
 m_channel_handle(std::move(other.m_channel_handle)),
 m_channel_enabled(std::exchange(other.m_channel_enabled, false)),
 m_channel_is_active(std::exchange(other.m_channel_is_active, false)),
 m_transfers_per_axi_intr(std::move(other.m_transfers_per_axi_intr)),
 m_pending_buffers_sizes(std::move(other.m_pending_buffers_sizes)),
 m_pending_num_avail_offset(std::move(other.m_pending_num_avail_offset)),
 m_is_waiting_for_channel_completion(other.m_is_waiting_for_channel_completion.exchange(false)),
 m_is_aborted_by_internal_source(other.m_is_aborted_by_internal_source.exchange(false))
{}

hailo_status VdmaChannel::stop_channel()
{
    {
        std::unique_lock<std::mutex> lock(m_is_active_flag_mutex);
        m_channel_is_active = false;
    }

    if (!m_state) {
        const auto status = unregister_fw_controlled_channel();
        CHECK_SUCCESS(status, "Failed to disable channel {}", m_channel_id);
    } else {
        std::unique_lock<State> state_guard(*m_state);
        const auto status = unregister_fw_controlled_channel();
        CHECK_SUCCESS(status, "Failed to disable channel {}", m_channel_id);

        if (Direction::D2H == m_direction) {
            unregister_for_d2h_interrupts(state_guard);
        } else {
            // For H2D channels we reset counters as we want to allow writes to the start of the buffer while the channel is stopped
            reset_internal_counters();
        }
    }
    return HAILO_SUCCESS;
}

uint16_t VdmaChannel::get_page_size() 
{
    return m_desc_page_size;
}

Expected<CONTROL_PROTOCOL__host_buffer_info_t> VdmaChannel::get_boundary_buffer_info(uint32_t transfer_size)
{
    CHECK_AS_EXPECTED(m_buffer, HAILO_INVALID_OPERATION, "Cannot get host buffer before buffer is allocated");
    return m_buffer->get_host_buffer_info(transfer_size);
}

hailo_status VdmaChannel::abort()
{
    m_is_aborted_by_internal_source = true;
    if (Direction::H2D == m_direction) {
        m_can_write_buffer_cv.notify_all();
    } else {
        m_can_read_buffer_cv.notify_all();
    }
    return m_driver.vdma_channel_abort(m_channel_id, *m_channel_handle);
}

hailo_status VdmaChannel::clear_abort()
{
    auto status = m_driver.vdma_channel_clear_abort(m_channel_id, *m_channel_handle);
    m_is_aborted_by_internal_source = false;
    return status;
}

size_t VdmaChannel::get_transfers_count_in_buffer(size_t transfer_size)
{
    const auto descs_in_transfer = m_buffer->descriptors_in_buffer(transfer_size);
    const auto descs_count = CB_SIZE(m_state->m_descs);
    return (descs_count - 1) / descs_in_transfer;
}

size_t VdmaChannel::get_buffer_size() const
{
    assert(m_buffer);
    return m_buffer->size();
}

Expected<size_t> VdmaChannel::get_h2d_pending_frames_count()
{
    return m_pending_buffers_sizes.size();
}

Expected<size_t> VdmaChannel::get_d2h_pending_descs_count()
{
    assert(m_state);

    std::lock_guard<State> state_guard(*m_state);

    int num_proc = CB_TAIL(m_state->m_descs);
    int desc_num_ready = CB_PROG(m_state->m_descs, num_proc, m_state->m_d2h_read_desc_index);

    return desc_num_ready;
}

hailo_status VdmaChannel::prepare_d2h_pending_descriptors(uint32_t transfer_size)
{
    assert(m_buffer);

    auto transfers_count_in_buffer = get_transfers_count_in_buffer(transfer_size);
    auto transfers_count = std::min(transfers_count_in_buffer,
        static_cast<size_t>(CB_SIZE(m_state->m_buffers) - 1));

    // on D2H no need for interrupt of first descriptor
    const auto first_desc_interrupts_domain = VdmaInterruptsDomain::NONE;
    for (uint32_t i = 0; i < transfers_count; i++) {
        /* Provide FW interrupt only in the end of the last transfer in the batch */
        auto last_desc_interrutps_domain = 
            (static_cast<uint32_t>(m_transfers_per_axi_intr - 1) == (i % m_transfers_per_axi_intr)) ? 
                VdmaInterruptsDomain::BOTH : VdmaInterruptsDomain::HOST;
        auto status = prepare_descriptors(transfer_size, first_desc_interrupts_domain, last_desc_interrutps_domain);
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__INFO("preparing descriptors failed because channel is not activated");
            return status;
        }
        CHECK_SUCCESS(status, "Failed prepare desc status={}", status);
    }

    /* We assume each output transfer is in the same size */
    m_state->m_accumulated_transfers += ((m_state->m_accumulated_transfers + transfers_count) % m_transfers_per_axi_intr);

    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::allocate_resources(uint32_t descs_count)
{
    // TODO (HRT-3762) : Move channel's state to driver to avoid using shared memory
    auto state = MmapBuffer<VdmaChannel::State>::create_shared_memory(sizeof(VdmaChannel::State));
    CHECK_EXPECTED_AS_STATUS(state, "Failed to allocate channel's resources");

#ifndef _MSC_VER
    // Make sharable mutex
    pthread_mutexattr_t mutex_attrs{};
    int err = pthread_mutexattr_init(&mutex_attrs);
    CHECK(0 == err, HAILO_INTERNAL_FAILURE, "pthread_mutexattr_init failed with {}", err);

    err = pthread_mutexattr_setpshared(&mutex_attrs, PTHREAD_PROCESS_SHARED);
    if (0 != err) {
        (void)pthread_mutexattr_destroy(&mutex_attrs);
        LOGGER__ERROR("pthread_mutexattr_setpshared failed with {}", err);
        return HAILO_INTERNAL_FAILURE;
    }

    err = pthread_mutex_init(&state.value()->m_state_lock, &mutex_attrs);
    if (0 != pthread_mutexattr_destroy(&mutex_attrs)) {
        LOGGER__ERROR("pthread_mutexattr_destroy failed");
        // continue
    }
    CHECK(0 == err, HAILO_INTERNAL_FAILURE, "Mutex init failed with {}", err);
#else
    InitializeCriticalSection(&state.value()->m_state_lock);
#endif

    m_state = state.release();
    m_pending_buffers_sizes = CircularArray<size_t>(descs_count);

    // If measuring latency, max_active_transfer is limited to 16 (see hailort_driver.hpp doc for further information)
    int pending_buffers_size = (nullptr == m_latency_meter) ? static_cast<int>(m_state->m_pending_buffers.size()) :
        (static_cast<int>(m_state->m_pending_buffers.size()) / 2);

    if (MAX_DESCS_COUNT < descs_count) {
        LOGGER__ERROR("Vdma channel descs_count mustn't be larger than {}", MAX_DESCS_COUNT);
        return HAILO_INVALID_ARGUMENT;
    }

    CB_INIT(m_state->m_descs, descs_count);
    CB_INIT(m_state->m_buffers, pending_buffers_size);

    // Allocate descriptor list (host side)
    auto status = allocate_buffer(descs_count * m_desc_page_size);
    CHECK_SUCCESS(status, "Failed to allocate vDMA buffer for channel transfer! status={}", status);

    clear_descriptor_list();

    return HAILO_SUCCESS;
}

void VdmaChannel::reset_internal_counters()
{
    assert(m_state);
    CB_RESET(m_state->m_descs);
    CB_RESET(m_state->m_buffers);
    m_state->m_d2h_read_desc_index = 0;
    m_state->m_last_timestamp_num_processed = 0;
    m_state->m_accumulated_transfers = 0;
}

hailo_status VdmaChannel::start_allocated_channel(uint32_t transfer_size)
{
    /* descriptor buffer must be allocated */
    assert(m_buffer);
    assert(m_state);
    std::lock_guard<State> state_guard(*m_state);
    reset_internal_counters();

    auto status = start_channel();
    CHECK_SUCCESS(status, "failed to start channel {}", m_channel_id);

    if ((Direction::D2H == m_direction) && (transfer_size != 0)) {
        status = prepare_d2h_pending_descriptors(transfer_size);
        if (HAILO_SUCCESS != status) {
            stop_channel();
        }
        return status;
    }
    m_channel_is_active = true;

    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::register_fw_controlled_channel()
{
    return register_channel_to_driver(HailoRTDriver::INVALID_DRIVER_BUFFER_HANDLE_VALUE);
}

hailo_status VdmaChannel::register_for_d2h_interrupts(const std::function<void(uint32_t)> &callback)
{
    // This function has to be called after channel is started
    assert(!((m_d2h_callback_thread) && m_d2h_callback_thread->joinable()));
    m_d2h_callback_thread = make_unique_nothrow<std::thread>([this, callback]() {
        wait_d2h_callback(callback);
    });
    CHECK_NOT_NULL(m_d2h_callback_thread, HAILO_OUT_OF_HOST_MEMORY);

    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::unregister_for_d2h_interrupts(std::unique_lock<State> &lock)
{
    // This function has to be called after channel is stopped (after unregister_fw_controlled_channel is called)
    if ((m_d2h_callback_thread) && (m_d2h_callback_thread->joinable())) {
        // Let the channel finish processing interrupts
        lock.unlock();
        m_d2h_callback_thread->join();
        lock.lock();
    }
    return HAILO_SUCCESS;
}

void VdmaChannel::wait_d2h_callback(const std::function<void(uint32_t)> &callback)
{
    assert(Direction::D2H == m_direction);
    if(!m_buffer) {
        LOGGER__ERROR("Wait called without allocating buffers");
        return;
    }
    while (true) {
        auto status = wait_for_channel_completion(HAILO_INFINITE_TIMEOUT, callback);
         if (HAILO_SUCCESS == status || (HAILO_STREAM_INTERNAL_ABORT == status)) {
            // Ignore HAILO_STREAM_INTERNAL_ABORT as we want to keep waiting for interrupts until channel is stopped
            continue;
        } else if (HAILO_STREAM_NOT_ACTIVATED == status) {
            // Finish gracefully
            return;
        } else {
            LOGGER__ERROR("wait_d2h_callback failed with status={}", status);
            return;
        }
    }
}

hailo_status VdmaChannel::wait(size_t buffer_size, std::chrono::milliseconds timeout)
{
    if (!m_buffer) {
        LOGGER__ERROR("Wait called without allocating buffers");
        return HAILO_INVALID_OPERATION;
    }

    CHECK(buffer_size < m_buffer->size(), HAILO_INVALID_ARGUMENT,
        "Requested transfer size ({}) must be smaller than ({})", buffer_size, m_buffer->size());

    if ((Direction::D2H == m_direction) && ((m_d2h_callback_thread) && (m_d2h_callback_thread->joinable()))) {
        std::unique_lock<State> state_guard(*m_state);
        hailo_status status = HAILO_SUCCESS; // Best effort
        bool was_successful = m_can_read_buffer_cv.wait_for(state_guard, timeout, [this, buffer_size, &status] () {
            if ((!m_channel_enabled) || (m_is_aborted_by_internal_source)) {
                status = HAILO_STREAM_INTERNAL_ABORT;
                return true; // return true so that the wait will finish
            }
            return is_ready_for_transfer_d2h(buffer_size);
        });
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            LOGGER__INFO("wait_for in d2h wait was aborted!");
            return HAILO_STREAM_INTERNAL_ABORT;
        }
        CHECK(was_successful, HAILO_TIMEOUT);
        return HAILO_SUCCESS;
    }
    auto is_ready_for_transfer = (Direction::H2D == m_direction) ?
        std::bind(&VdmaChannel::is_ready_for_transfer_h2d, this, buffer_size) :
        std::bind(&VdmaChannel::is_ready_for_transfer_d2h, this, buffer_size);
    return wait_for_condition(is_ready_for_transfer, timeout);
}

hailo_status VdmaChannel::transfer(void *buf, size_t count)
{
    CHECK((nullptr != buf) && (0 < count), HAILO_INVALID_ARGUMENT);
    CHECK(nullptr != m_buffer, HAILO_INVALID_OPERATION, "Transfer called without allocating buffers");

    hailo_status status = HAILO_UNINITIALIZED;
    assert(m_state);
    std::lock_guard<State> state_guard(*m_state);

    if (Direction::H2D == m_direction) {
        status = transfer_h2d(buf, count);
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__INFO("Transfer failed because Channel {} is not activated", m_channel_id);
            return HAILO_STREAM_NOT_ACTIVATED;
        } 
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Transfer failed for channel {}", m_channel_id);
            return status;
        }
        return HAILO_SUCCESS;
    } else {
        status = transfer_d2h(buf, count);
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__INFO("Transfer failed because Channel {} is not activated", m_channel_id);
            return HAILO_STREAM_NOT_ACTIVATED;
        } 
        if (HAILO_SUCCESS != status) {
            LOGGER__ERROR("Transfer failed for channel {} status {}", m_channel_id, status);
            return status;
        }
        return HAILO_SUCCESS;
    }

    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::write_buffer_impl(const MemoryView &buffer)
{
    CHECK(nullptr != m_buffer, HAILO_INVALID_OPERATION, "Transfer called without allocating buffers");

    size_t desired_desc_num = m_buffer->descriptors_in_buffer(buffer.size());
    uint32_t desc_avail = (get_num_available() + m_pending_num_avail_offset) & m_state->m_descs.size_mask;

    assert(CB_AVAIL(m_state->m_descs, desc_avail, CB_TAIL(m_state->m_descs)) >= static_cast<uint16_t>(desired_desc_num));

    /* Copy buffer into the PLDA data struct */
    auto offset = desc_avail * m_desc_page_size;
    auto status = m_buffer->write_cyclic(buffer.data(), buffer.size(), offset);
    CHECK_SUCCESS(status);

    m_pending_num_avail_offset = static_cast<uint16_t>(m_pending_num_avail_offset + desired_desc_num);    

    CHECK(!m_pending_buffers_sizes.full(), HAILO_INVALID_OPERATION, "Cannot add more pending buffers!");
    m_pending_buffers_sizes.push_back(buffer.size());
    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::write_buffer(const MemoryView &buffer, std::chrono::milliseconds timeout)
{
    assert(m_state);
    std::unique_lock<State> state_guard(*m_state);

    size_t desired_desc_num = m_buffer->descriptors_in_buffer(buffer.size());
    hailo_status channel_completion_status = HAILO_SUCCESS;
    bool was_successful = m_can_write_buffer_cv.wait_for(state_guard, timeout, [this, desired_desc_num, timeout, &state_guard,
            &channel_completion_status] () {
        if ((!m_channel_enabled) || (m_is_aborted_by_internal_source)) {
            return true;
        }

        // TODO (HRT-7252): Clean this code
        while (true) {
            int buffers_head = CB_HEAD(m_state->m_buffers);
            int buffers_tail = CB_TAIL(m_state->m_buffers);
            if (CB_AVAIL(m_state->m_buffers, buffers_head, buffers_tail)) {
                break;
            }

            if (HailoRTDriver::INVALID_VDMA_CHANNEL_HANDLE == *m_channel_handle) {
                return false;
            }

            state_guard.unlock();
            channel_completion_status = wait_for_channel_completion(timeout);
            if (HAILO_SUCCESS != channel_completion_status) {
                LOGGER__INFO("wait_for_channel_completion failed with status={}", channel_completion_status);
                return true;
            }
            state_guard.lock();
        }

        uint32_t desc_avail = (get_num_available() + m_pending_num_avail_offset) & m_state->m_descs.size_mask;
        int num_free = CB_AVAIL(m_state->m_descs, desc_avail, CB_TAIL(m_state->m_descs));
        return (num_free >= static_cast<uint16_t>(desired_desc_num));
    });
    if ((!m_channel_enabled) || (m_is_aborted_by_internal_source) || (HAILO_STREAM_INTERNAL_ABORT == channel_completion_status)) {
        LOGGER__INFO("wait_for in write_buffer was aborted!");
        return HAILO_STREAM_INTERNAL_ABORT;
    }
    CHECK(was_successful, HAILO_TIMEOUT, "Waiting for descriptors in write_buffer has reached a timeout!");
    CHECK_SUCCESS(channel_completion_status);

    return write_buffer_impl(buffer);
}

hailo_status VdmaChannel::send_pending_buffer_impl()
{
    CHECK(!m_pending_buffers_sizes.empty(), HAILO_INVALID_OPERATION, "There are no pending buffers to send!");
    assert(m_buffer);

    // For h2d, only the host need to get transfer done interrupts
    VdmaInterruptsDomain last_desc_interrupts_domain = VdmaInterruptsDomain::HOST;
    // If we measure latency, we need interrupt on the first descriptor
    VdmaInterruptsDomain first_desc_interrupts_domain = (m_latency_meter != nullptr) ?
        VdmaInterruptsDomain::HOST : VdmaInterruptsDomain::NONE;

    auto status = prepare_descriptors(m_pending_buffers_sizes.front(), first_desc_interrupts_domain, last_desc_interrupts_domain);
    if (HAILO_STREAM_NOT_ACTIVATED == status) {
        LOGGER__INFO("sending pending buffer failed because stream is not activated");
        // Stream was aborted during transfer - reset pending buffers
        m_pending_num_avail_offset = 0;
        while (m_pending_buffers_sizes.size() > 0) {
            m_pending_buffers_sizes.pop_front();
        }
        return status;
    }
    CHECK_SUCCESS(status);

    m_state->m_accumulated_transfers = (m_state->m_accumulated_transfers + 1) % m_transfers_per_axi_intr;

    size_t desired_desc_num = m_buffer->descriptors_in_buffer(m_pending_buffers_sizes.front());
    m_pending_num_avail_offset = static_cast<uint16_t>(m_pending_num_avail_offset - desired_desc_num);

    m_pending_buffers_sizes.pop_front();

    return HAILO_SUCCESS;
}

Expected<PendingBufferState> VdmaChannel::send_pending_buffer()
{
    size_t next_buffer_desc_num = 0;
    {
        assert(m_state);
        assert(m_buffer);
        std::lock_guard<State> state_guard(*m_state);

        // Save before calling send_pending_buffer_impl because we pop from m_pending_buffers_sizes there
        next_buffer_desc_num = m_buffer->descriptors_in_buffer(m_pending_buffers_sizes.front());

        auto status = send_pending_buffer_impl();
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__INFO("stream is not activated");
            return make_unexpected(HAILO_STREAM_NOT_ACTIVATED);
        } else {
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
    }
    m_can_write_buffer_cv.notify_one();

    return PendingBufferState(*this, next_buffer_desc_num);
}

hailo_status PendingBufferState::finish(std::chrono::milliseconds timeout, std::unique_lock<std::mutex> &lock)
{
    unlock_guard<std::unique_lock<std::mutex>> unlock(lock);

    while (true) {
        {
            std::lock_guard<VdmaChannel::State> state_guard(*m_vdma_channel.m_state);

            // Make sure that only one thread is waiting for channel completion
            if (m_vdma_channel.m_is_waiting_for_channel_completion) {
                break;
            }

            // When all pending buffers have been sent but no buffers were processed yet (there are no free descriptors) we want to wait
            // for channel completion to make free room to the next buffers
            // TODO: This assumes the next buffer is the same size as the current one, so consider moving this to the write_buffer function
            int num_free = CB_AVAIL(m_vdma_channel.m_state->m_descs, m_vdma_channel.get_num_available(), CB_TAIL(m_vdma_channel.m_state->m_descs));

            // We use m_next_buffer_desc_num to check if the next buffer has enough descriptors
            bool should_free_descs = (0 == m_vdma_channel.m_pending_num_avail_offset) && (num_free < static_cast<uint16_t>(m_next_buffer_desc_num));
            m_vdma_channel.m_is_waiting_for_channel_completion = should_free_descs;
            if (!should_free_descs) {
                break;
            }
        }

        auto status = m_vdma_channel.wait_for_channel_completion(timeout);
        if (HAILO_STREAM_INTERNAL_ABORT == status) {
            LOGGER__INFO("wait_for_channel_completion has failed with status=HAILO_STREAM_INTERNAL_ABORT");
            return status;
        }
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::flush(const std::chrono::milliseconds &timeout)
{
    assert(m_state);

    if (Direction::D2H == m_direction) {
        // We are not buffering user data
        return HAILO_SUCCESS;
    }

    if (!m_buffer) {
        LOGGER__ERROR("VdmaChannel::flush is called on a channel without allocated resources");
        return HAILO_INVALID_OPERATION;
    }

    return wait_for_condition([this] { return CB_HEAD(m_state->m_buffers) == CB_TAIL(m_state->m_buffers); }, timeout);
}

hailo_status VdmaChannel::transfer_h2d(void *buf, size_t count)
{
    auto status = write_buffer_impl(MemoryView(buf, count));
    CHECK_SUCCESS(status);

    status = send_pending_buffer_impl();
    if (HAILO_STREAM_NOT_ACTIVATED == status) {
        return status;
    } else {
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::transfer_d2h(void *buf, size_t count)
{
    hailo_status status = HAILO_UNINITIALIZED;
    /* Provide FW interrupt only in the end of the last transfer in the batch */
    VdmaInterruptsDomain first_desc_interrupts_domain = VdmaInterruptsDomain::NONE;
    VdmaInterruptsDomain last_desc_interrupts_domain = (m_state->m_accumulated_transfers + 1 == m_transfers_per_axi_intr) ? 
        VdmaInterruptsDomain::BOTH : VdmaInterruptsDomain::HOST;
 
    assert(m_state);
    assert(m_buffer);

    auto desired_desc_num = m_buffer->descriptors_in_buffer(count);
    assert(desired_desc_num <= MAX_DESCS_COUNT);
    int desc_num = static_cast<int>(desired_desc_num);

    int num_processes = CB_TAIL(m_state->m_descs);
    int num_ready = CB_PROG(m_state->m_descs, num_processes, m_state->m_d2h_read_desc_index);
    if (num_ready < desc_num) {
        return HAILO_OUT_OF_DESCRIPTORS;
    }

    size_t offset = m_state->m_d2h_read_desc_index * m_desc_page_size;
    status = m_buffer->read_cyclic(buf, count, offset);
    if (status != HAILO_SUCCESS) {
        return status;
    }

    m_state->m_d2h_read_desc_index = (m_state->m_d2h_read_desc_index + desc_num) & m_state->m_descs.size_mask;

    // prepare descriptors for next recv
    if (*m_channel_handle != HailoRTDriver::INVALID_VDMA_CHANNEL_HANDLE) {
        status = prepare_descriptors(count, first_desc_interrupts_domain, last_desc_interrupts_domain);
        if (HAILO_STREAM_NOT_ACTIVATED == status) {
            LOGGER__INFO("transfer d2h failed because stream is not activated");
            return status;
        }
        CHECK_SUCCESS(status);
    }

    m_state->m_accumulated_transfers = (m_state->m_accumulated_transfers + 1) % m_transfers_per_axi_intr;

    return HAILO_SUCCESS;
}

uint16_t VdmaChannel::get_num_available()
{
    assert(m_state);

    uint16_t num_available = (uint16_t)CB_HEAD(m_state->m_descs);

#ifndef NDEBUG
    // Validate synchronization with HW
    auto hw_num_avail = m_host_registers.get_num_available();
    assert(hw_num_avail);
    // On case of channel aborted, the num_available is set to 0 (so we don't accept sync)

    auto is_aborted_exp = is_aborted();
    assert(is_aborted_exp);

    if ((HailoRTDriver::INVALID_VDMA_CHANNEL_HANDLE != *m_channel_handle) && !is_aborted_exp.value()) {
        assert(hw_num_avail.value() == num_available);
    }
#endif
    return num_available;
}

Expected<uint16_t> VdmaChannel::get_hw_num_processed()
{
    assert(m_state);

    auto hw_num_processed = m_host_registers.get_num_processed();
    CHECK_EXPECTED(hw_num_processed, "Fail to read vdma num processed register");

    // Although the hw_num_processed should be a number between 0 and m_descs.size-1, if
    // m_desc.size < 0x10000 (the maximum desc size), the actual hw_num_processed is a number
    // between 1 and m_descs.size. Therefore the value can be m_descs.size, in this case we change it
    // to zero.
    return static_cast<uint16_t>(hw_num_processed.value() & m_state->m_descs.size_mask);
}

hailo_status VdmaChannel::set_num_avail_value(uint16_t new_value)
{
    // TODO - HRT-7885 : add check in driver
    CHECK(*m_channel_handle != HailoRTDriver::INVALID_VDMA_CHANNEL_HANDLE, HAILO_STREAM_NOT_ACTIVATED,
        "Error, can't set num available when stream is not activated");

    auto status = m_host_registers.set_num_available(new_value);
    CHECK_SUCCESS(status, "Fail to write vdma num available register");

#ifndef NDEBUG
    // Validate synchronization with HW
    auto hw_num_avail = m_host_registers.get_num_available();
    assert(hw_num_avail);
    assert(hw_num_avail.value() == new_value);
#endif
    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::set_transfers_per_axi_intr(uint16_t transfers_per_axi_intr)
{
    CHECK(0 != transfers_per_axi_intr, HAILO_INVALID_ARGUMENT, "Invalid transfers per axi interrupt");
    m_transfers_per_axi_intr = transfers_per_axi_intr;
    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::inc_num_available(uint16_t value)
{
    assert(m_state);

    //TODO: validate that count is added.
    int num_available = get_num_available();
    int num_processed = CB_TAIL(m_state->m_descs);
    int num_free = CB_AVAIL(m_state->m_descs, num_available, num_processed);
    if (value > num_free) {
        return HAILO_OUT_OF_DESCRIPTORS;
    }

    CB_ENQUEUE(m_state->m_descs, value);
    num_available = (num_available + value) & m_state->m_descs.size_mask;
    return set_num_avail_value(static_cast<uint16_t>(num_available));
}

void VdmaChannel::add_pending_buffer(uint32_t first_desc, uint32_t last_desc)
{
    assert(m_state);

    int head = CB_HEAD(m_state->m_buffers);
    int tail = CB_TAIL(m_state->m_buffers);
    if (!CB_AVAIL(m_state->m_buffers, head, tail)) {
        LOGGER__ERROR("no avail space");
    }
    m_state->m_pending_buffers[head].last_desc = last_desc;
    m_state->m_pending_buffers[head].latency_measure_desc = (m_direction == Direction::H2D) ? first_desc : last_desc;
    CB_ENQUEUE(m_state->m_buffers, 1);
}

VdmaChannel::Direction VdmaChannel::other_direction(Direction direction)
{
    return (Direction::H2D == direction) ? Direction::D2H : Direction::H2D;
}

hailo_status VdmaChannel::unregister_fw_controlled_channel()
{
    assert(m_channel_handle);
    if (HailoRTDriver::INVALID_VDMA_CHANNEL_HANDLE != *m_channel_handle) {
        auto status = m_driver.vdma_channel_disable(m_channel_id, *m_channel_handle);
        *m_channel_handle = HailoRTDriver::INVALID_VDMA_CHANNEL_HANDLE;
        CHECK_SUCCESS(status, "Failed to disable channel {}", m_channel_id);
    }
    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::register_channel_to_driver(uintptr_t desc_list_handle)
{
    const bool measure_latency = (nullptr != m_latency_meter);
    const uintptr_t desc_handle = desc_list_handle;
    auto channel_handle = m_driver.vdma_channel_enable(m_channel_id, m_direction, desc_handle, measure_latency);
    CHECK_EXPECTED_AS_STATUS(channel_handle, "Failed to enable channel {}", m_channel_id);

    *m_channel_handle = channel_handle.release();
    return HAILO_SUCCESS;
}

hailo_status VdmaChannel::start_channel()
{
    auto is_aborted_exp = is_aborted();
    assert(is_aborted_exp);
    assert(is_aborted_exp.value());

    auto status = register_channel_to_driver(m_buffer->get_desc_list()->get().handle());
    CHECK_SUCCESS(status, "Failed to enable channel {}", m_channel_id);

    m_channel_is_active = true;

    return HAILO_SUCCESS;
}

// TODO - HRT-6984 - move function inside desc list class as part of the ctor
void VdmaChannel::clear_descriptor_list()
{
    assert(m_buffer);

    size_t desc_number = m_buffer->descs_count();
    size_t page_size = m_buffer->desc_page_size();
    auto desc_list = m_buffer->get_desc_list();

    // Config Descriptors value in SG-List Host side
    for (uint32_t j = 0; j < desc_number; j++) {
        VdmaDescriptor &descInfo = (desc_list->get())[j];
        descInfo.PageSize_DescControl = static_cast<uint32_t>((page_size << 8) + 0x2);
        descInfo.RemainingPageSize_Status = 0x0;
    }
}

hailo_status VdmaChannel::allocate_buffer(const uint32_t buffer_size)
{
    assert((buffer_size % m_desc_page_size) == 0);
    uint32_t desc_count = buffer_size / m_desc_page_size;

    if (m_buffer) {
        LOGGER__ERROR("m_buffer is not NULL");
        return HAILO_INVALID_OPERATION;
    }

    auto buffer = vdma::SgBuffer::create(m_driver, desc_count, m_desc_page_size, m_direction,
        m_channel_id.channel_index);
    CHECK_EXPECTED_AS_STATUS(buffer);

    m_buffer = make_unique_nothrow<vdma::SgBuffer>(buffer.release());
    CHECK_NOT_NULL(m_buffer, HAILO_OUT_OF_HOST_MEMORY);

    return HAILO_SUCCESS;
}

uint32_t VdmaChannel::calculate_buffer_size(const HailoRTDriver &driver, uint32_t transfer_size,
    uint32_t transfers_count, uint16_t requested_desc_page_size) {
    auto desc_page_size = driver.calc_desc_page_size(requested_desc_page_size);
    uint32_t descs_per_transfer = VdmaDescriptorList::descriptors_in_buffer(transfer_size, desc_page_size);
    uint32_t descs_count = descs_per_transfer * transfers_count;

    if (descs_count > MAX_DESCS_COUNT) {
        descs_count = MAX_DESCS_COUNT;
    }
    else if (descs_count < MIN_DESCS_COUNT) {
        descs_count = MIN_DESCS_COUNT;
    }

    return descs_count * desc_page_size;
}

hailo_status VdmaChannel::trigger_channel_completion(uint16_t hw_num_processed, const std::function<void(uint32_t)> &callback)
{
    // NOTE: right now, we can retake the 'completion' descriptor for a new transfer before handling the interrupt.
    //      we should have our own pointers indicating whats free instead of reading from HW.
    // TODO: consider calculating the last descriptor using the src_desc_avail and src_desc_proc instead of using
    // status?
    // TODO: we might free a pending buffer which we didn't get an interrupt for yet. we should still handle this
    // situation correctly.

    assert(m_state);
    assert(m_buffer);
    std::lock_guard<State> state_guard(*m_state);

    int processed_no = 0;
    int head = CB_HEAD(m_state->m_buffers);
    int tail = CB_TAIL(m_state->m_buffers);
    int prog = CB_PROG(m_state->m_buffers, head, tail);
    int last_tail = -1;

    auto channel_error = m_host_registers.get_channel_error();
    CHECK_EXPECTED_AS_STATUS(channel_error, "Fail to read vdma channel error register");
    CHECK(0 == channel_error.value(), HAILO_INTERNAL_FAILURE, "Vdma channel {} in error state {}", m_channel_id,
        channel_error.value());

    uint16_t last_num_processed = static_cast<uint16_t>(CB_TAIL(m_state->m_descs));

    for (; prog > 0; prog--) {
        uint16_t last_desc_index = static_cast<uint16_t>(m_state->m_pending_buffers[tail].last_desc);
        // Transfer is complete if its last descriptor is in [last_num_processed, hw_num_processed) or
        // the the buffer is empty (hw_num_processed == get_num_available())
        bool is_complete = is_desc_between(last_num_processed, hw_num_processed, last_desc_index) || (hw_num_processed == get_num_available());

#ifndef NDEBUG
        auto status = (m_buffer->get_desc_list()->get())[last_desc_index].RemainingPageSize_Status & 0xFF;
        // Verify if a DMA Descriptor error occurred.
        if (status & 0x2) {
            LOGGER__ERROR("Error while processing descriptor {} of DMA {} on board {}.", last_desc_index, m_channel_id,
                m_driver.dev_path());
            return HAILO_INTERNAL_FAILURE;
        }

        // status is read after hw_num_processed, so we want is_complete -> (status == 1).
        assert(!is_complete || ((status & 0x1) == 1));
#endif

        if (!is_complete) {
            break;
        }

        processed_no++;
        last_tail = tail;
        tail = ((tail + 1) & m_state->m_buffers.size_mask);
    }

    if (0 < processed_no) {
        // TODO: use a different macro instead?
        _CB_SET(m_state->m_descs.tail, (m_state->m_pending_buffers[last_tail].last_desc + 1) & m_state->m_descs.size_mask);
        CB_DEQUEUE(m_state->m_buffers, processed_no);

        if (Direction::H2D == m_direction) {
            m_can_write_buffer_cv.notify_one();
        } else {
            m_can_read_buffer_cv.notify_one();
        }
        callback(processed_no);
    }

    m_is_waiting_for_channel_completion = false;
    return HAILO_SUCCESS;
}

bool VdmaChannel::is_ready_for_transfer_h2d(size_t buffer_size)
{   
    assert(m_state);
    assert(m_buffer);

    size_t desired_desc_num = m_buffer->descriptors_in_buffer(buffer_size);
    assert(desired_desc_num <= MAX_DESCS_COUNT);
    int desc_num = static_cast<int>(desired_desc_num);

    int buffers_head = CB_HEAD(m_state->m_buffers);
    int buffers_tail = CB_TAIL(m_state->m_buffers);
    if (!CB_AVAIL(m_state->m_buffers, buffers_head, buffers_tail)) {
        return false;
    }

    int num_available = get_num_available();
    int num_processed = CB_TAIL(m_state->m_descs);

    if (desc_num == m_state->m_descs.size) {
        // Special case when the checking if the buffer is empty
        return num_available == num_processed; 
    }

    int num_free = CB_AVAIL(m_state->m_descs, num_available, num_processed);
    if (num_free < desc_num) {
        return false;
    }

    return true;
}

bool VdmaChannel::is_ready_for_transfer_d2h(size_t buffer_size)
{
    assert(m_state);
    assert(m_buffer);

    size_t desired_desc_num = m_buffer->descriptors_in_buffer(buffer_size);
    assert(desired_desc_num <= MAX_DESCS_COUNT);
    int desc_num = static_cast<int>(desired_desc_num);

    int buffers_head = CB_HEAD(m_state->m_buffers);
    int buffers_tail = CB_TAIL(m_state->m_buffers);
    if (!CB_AVAIL(m_state->m_buffers, buffers_head, buffers_tail)) {
        return false;
    }

    int num_processed = CB_TAIL(m_state->m_descs);
    int num_ready = CB_PROG(m_state->m_descs, num_processed, m_state->m_d2h_read_desc_index);
    if (num_ready < desc_num) {
        return false;
    }
    return true;
}

hailo_status VdmaChannel::prepare_descriptors(size_t transfer_size, VdmaInterruptsDomain first_desc_interrupts_domain,
    VdmaInterruptsDomain last_desc_interrupts_domain)
{
    MICROPROFILE_SCOPEI("vDMA Channel", "Trigger vDMA", 0);

    assert(m_buffer);
    assert(m_state);
    auto desc_info = m_buffer->get_desc_list();

    /* calculate desired descriptors for the buffer */
    size_t desired_desc_num = m_buffer->descriptors_in_buffer(transfer_size);
    assert(desired_desc_num <= MAX_DESCS_COUNT);
    uint16_t desc_num = static_cast<uint16_t>(desired_desc_num);

    int num_available = get_num_available();
    int num_processed = CB_TAIL(m_state->m_descs);
    int num_free = CB_AVAIL(m_state->m_descs, num_available, num_processed);
    if (num_free < desc_num) {
        return HAILO_OUT_OF_DESCRIPTORS;
    }

    auto actual_desc_count = desc_info->get().program_descriptors(transfer_size, first_desc_interrupts_domain,
        last_desc_interrupts_domain, num_available, true);
    if (!actual_desc_count) {
        LOGGER__ERROR("Failed to program desc_list for channel {}", m_channel_id);
        return actual_desc_count.status();
    }
    assert (actual_desc_count.value() == desc_num);
    int last_desc_avail = ((num_available + desc_num - 1) & m_state->m_descs.size_mask);

    add_pending_buffer(num_available, last_desc_avail);
    return inc_num_available(desc_num);
}

uint32_t VdmaChannel::calculate_descriptors_count(uint32_t buffer_size)
{
    return VdmaDescriptorList::calculate_descriptors_count(buffer_size, 1, m_desc_page_size);
}

bool VdmaChannel::is_desc_between(uint16_t begin, uint16_t end, uint16_t desc)
{
    if (begin == end) {
        // There is nothing between
        return false;
    }
    if (begin < end) {
        // desc needs to be in [begin, end)
        return (begin <= desc) && (desc < end);
    }
    else {
        // desc needs to be in [0, end) or [begin, m_state->m_descs.size()-1]
        return (desc < end) || (begin <= desc);
    }
}

Expected<bool> VdmaChannel::is_aborted()
{
    // Checking if either src side or dst side of the channel are aborted
    auto host_control = m_host_registers.get_control();
    CHECK_EXPECTED(host_control, "Fail to read vdma control register");
    if (vdma_channel_control_is_aborted(host_control.value()) ||
        vdma_channel_control_is_paused(host_control.value())) {
        return true;
    }

    auto device_control = m_device_registers.get_control();
    CHECK_EXPECTED(device_control, "Fail to read vdma control register");
    if (vdma_channel_control_is_aborted(device_control.value()) ||
        vdma_channel_control_is_paused(device_control.value())) {
        return true;
    }

    return false;
}

hailo_status VdmaChannel::wait_for_condition(std::function<bool()> condition, std::chrono::milliseconds timeout)
{
    auto start_time = std::chrono::steady_clock::now();
    std::chrono::milliseconds time_elapsed(0);
    while (timeout > time_elapsed) {
        if (condition()) {
            return HAILO_SUCCESS;
        }

        auto status = wait_for_channel_completion(timeout);
        if (HAILO_SUCCESS != status) {
            return status;
        }

        time_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time);
    }

    return condition() ? HAILO_SUCCESS : HAILO_TIMEOUT;
}

hailo_status VdmaChannel::wait_for_channel_completion(std::chrono::milliseconds timeout, const std::function<void(uint32_t)> &callback)
{
    auto hw_num_processed = wait_interrupts(timeout);
    if ((hw_num_processed.status() == HAILO_TIMEOUT) ||
        (hw_num_processed &&  hw_num_processed.value() == 0)) {
        // We need to check for channel abort in this 2 cases:
        //  1. TIMEOUT - maybe the timeout is a result of channel aborted.
        //  2. hw_num_processed == 0 - In this case we receive an interrupt, but the channel may be
        //     aborted. When the channel is aborted, num processed is set to 0.
        auto is_aborted_exp = is_aborted();
        CHECK_EXPECTED_AS_STATUS(is_aborted_exp);
        if (is_aborted_exp.value()) {
            std::unique_lock<std::mutex> lock(m_is_active_flag_mutex);
            if (!m_channel_is_active) {
                return HAILO_STREAM_NOT_ACTIVATED;
            }

            LOGGER__CRITICAL("Channel {} was aborted by an external source!", m_channel_id);
            return HAILO_STREAM_ABORTED;
        }
    }
    if ((HAILO_STREAM_INTERNAL_ABORT == hw_num_processed.status()) ||
        (HAILO_STREAM_NOT_ACTIVATED == hw_num_processed.status())) {
        return hw_num_processed.status();
    }
    CHECK_EXPECTED_AS_STATUS(hw_num_processed);

    auto status = trigger_channel_completion(hw_num_processed.value(), callback);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

Expected<uint16_t> VdmaChannel::wait_interrupts(std::chrono::milliseconds timeout)
{
    assert(m_state);

    auto irq_data = m_driver.wait_channel_interrupts(m_channel_id, *m_channel_handle, timeout);
    if ((HAILO_STREAM_INTERNAL_ABORT == irq_data.status()) ||
        (HAILO_STREAM_NOT_ACTIVATED == irq_data.status())) {
        LOGGER__INFO("Wait channel interrupts was aborted!");
        return make_unexpected(irq_data.status());
    }
    CHECK_EXPECTED(irq_data);

    if (m_latency_meter == nullptr) {
        return get_hw_num_processed();
    }
    else {
        // Fixing desc num_processed (it may be equal to m_state->m_descs.size, in this case we will make it zero)
        for (size_t i = 0; i < irq_data->count; i++) {
            irq_data->timestamp_list[i].desc_num_processed = static_cast<uint16_t>(
                irq_data->timestamp_list[i].desc_num_processed & m_state->m_descs.size_mask);
        }
        return update_latency_meter(irq_data.value());
    }
}

Expected<uint16_t> VdmaChannel::update_latency_meter(const ChannelInterruptTimestampList &timestamp_list)
{
    assert(m_state);

    uint16_t last_num_processed = m_state->m_last_timestamp_num_processed;
 
    if (timestamp_list.count == 0) {
        // TODO: handle this in the driver level.
        return last_num_processed;
    }

    // TODO: now we have more iterations than we need. We know that the pending buffers + the timestamp list
    // are ordered. If pending_buffer[i] is not in any of the timestamps_list[0, 1, ... k], then also pending_buffer[i+1,i+2,...]
    // not in those timestamps

    int head = CB_HEAD(m_state->m_buffers);
    int tail = CB_TAIL(m_state->m_buffers);
    int prog = CB_PROG(m_state->m_buffers, head, tail);

    for (; prog > 0; prog--, tail = ((tail + 1) & m_state->m_buffers.size_mask)) {
        uint16_t latency_desc = static_cast<uint16_t>(m_state->m_pending_buffers[tail].latency_measure_desc);
        for (size_t i = 0; i < timestamp_list.count; i++) {
            const auto &irq_timestamp = timestamp_list.timestamp_list[i];
            if (is_desc_between(last_num_processed, irq_timestamp.desc_num_processed, latency_desc)) {
                if (m_direction == Direction::H2D) {
                    m_latency_meter->add_start_sample(irq_timestamp.timestamp);
                }
                else {
                    m_latency_meter->add_end_sample(m_stream_index, irq_timestamp.timestamp);
                }
                break;
            }
        }
    }

    m_state->m_last_timestamp_num_processed = timestamp_list.timestamp_list[timestamp_list.count-1].desc_num_processed;
    return std::move(static_cast<uint16_t>(m_state->m_last_timestamp_num_processed));
}

} /* namespace hailort */
