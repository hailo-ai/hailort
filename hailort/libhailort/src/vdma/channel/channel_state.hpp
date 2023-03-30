/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file channel_state.hpp
 * @brief Current state of Vdma Channel
 *
 * <doc>
 **/

#ifndef _HAILO_VDMA_CHANNEL_STATE_HPP_
#define _HAILO_VDMA_CHANNEL_STATE_HPP_

#include "hailo/hailort.h"
#include "os/hailort_driver.hpp"
#include "common/circular_buffer.hpp"
#include "hailo/dma_mapped_buffer.hpp"
#include "hailo/stream.hpp"

#include <array>
#include <condition_variable>

#ifndef _MSC_VER
#include <sys/mman.h>
#endif


namespace hailort {
namespace vdma {

struct PendingBuffer {
    uint32_t last_desc;
    uint32_t latency_measure_desc;
    TransferDoneCallback on_transfer_done;
    std::shared_ptr<DmaMappedBuffer> buffer;
    void *opaque;
};

class ChannelBase;
class BoundaryChannel;
class AsyncChannel;
class BufferedChannel;


#ifndef _MSC_VER
// Special mutex and condition variable objects that can be shared between forked processes (Not needed on windows, 
// because there is no fork).
class RecursiveSharedMutex final {
public:
    RecursiveSharedMutex();
    ~RecursiveSharedMutex();

    RecursiveSharedMutex(const RecursiveSharedMutex &) = delete;
    RecursiveSharedMutex &operator=(const RecursiveSharedMutex &) = delete;
    RecursiveSharedMutex(RecursiveSharedMutex &&) = delete;
    RecursiveSharedMutex &operator=(RecursiveSharedMutex &&) = delete;

    void lock();
    void unlock();

    pthread_mutex_t *native_handle()
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

class SharedConditionVariable final {
public:

    SharedConditionVariable();
    ~SharedConditionVariable();

    SharedConditionVariable(const SharedConditionVariable &) = delete;
    SharedConditionVariable &operator=(const SharedConditionVariable &) = delete;
    SharedConditionVariable(SharedConditionVariable &&) = delete;
    SharedConditionVariable &operator=(SharedConditionVariable &&) = delete;

    bool wait_for(std::unique_lock<RecursiveSharedMutex> &lock, std::chrono::milliseconds timeout, std::function<bool()> condition);
    void notify_one();
    void notify_all();

private:
    pthread_cond_t m_cond;
};
#else /* _MSC_VER */
using RecursiveSharedMutex = std::recursive_mutex;
using SharedConditionVariable = std::condition_variable_any;
#endif

class VdmaChannelState final
{
public:
    static Expected<std::unique_ptr<VdmaChannelState>> create(uint32_t descs_count, bool measure_latency);

    VdmaChannelState(uint32_t descs_count, bool measure_latency);
    VdmaChannelState(const VdmaChannelState &other) = delete;
    VdmaChannelState(VdmaChannelState &&other) = delete;
    ~VdmaChannelState() = default;

    void reset_counters();
    void reset_previous_state_counters();
    // Each transfer on the channel is logged by a PendingBuffer:
    // - first_desc/last_desc - first and last descriptors of the transfer
    // - direction - transfer's direction
    // - on_transfer_done - callback to be called once the transfer is complete (i.e. when an interrupt is received on last_desc)
    // - buffer - points to the vdma mapped buffer being transferred (may be null)
    // - opaque - context to be transferred to the callback (may be null)
    void add_pending_buffer(uint32_t first_desc, uint32_t last_desc, HailoRTDriver::DmaDirection direction,
        const TransferDoneCallback &on_transfer_done, std::shared_ptr<DmaMappedBuffer> buffer = nullptr, void *opaque = nullptr);

    RecursiveSharedMutex &mutex()
    {
        return m_state_lock;
    }

    SharedConditionVariable &transfer_buffer_cv()
    {
        return m_can_transfer_buffer_cv;
    }

#ifndef _MSC_VER
    // The VdmaChannelState must remain in a shared memory scope, so we implement the new/delete operators (only on
    // non-windows machines).
    void* operator new(std::size_t size) = delete;
    void* operator new(std::size_t size, const std::nothrow_t&) throw() {
        // Map a shared memory region into the virtual memory of the process
        void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            return nullptr;
        }
        return ptr;
    }

    // Custom operator delete function that unmaps the shared memory region
    void operator delete(void* ptr, std::size_t size) {
        munmap(ptr, size);
    }
#endif /* _MSC_VER */

    friend class ChannelBase;
    friend class BoundaryChannel;
    friend class AsyncChannel;
    friend class BufferedChannel;

private:
    RecursiveSharedMutex m_state_lock;
    SharedConditionVariable m_can_transfer_buffer_cv;

    bool m_is_channel_activated;

    // On pending buffer with must use std::array because it relays on the shared memory (and std::vector uses new malloc)
    CircularArray<PendingBuffer, std::array<PendingBuffer, PENDING_BUFFERS_SIZE>> m_pending_buffers;
    // TODO: describe why we must have our own num_available and num_proc.
    // it's not just for efficiency but its critical to avoid a potential bug - see Avigail email.
    // TODO: Consider C11 stdatomic
    circbuf_t m_descs;
    // m_d2h_read_desc_index and m_d2h_read_desc_index_abs are the index of the first desc containing frames to be
    // copied to the user ("ready" frames in a D2H buffered channel). m_d2h_read_desc_index is relative to the
    // first desc in the desc list, whereas m_d2h_read_desc_index_abs is relative to the start of the vdma buffer.
    int m_d2h_read_desc_index;
    int m_d2h_read_desc_index_abs;
    bool m_is_aborted;
    // Points to the tail of the desc list when the channel is stopped (starts at zero)
    int m_previous_tail;
    int m_desc_list_delta;
    // Contains the last num_processed of the last interrupt (only used on latency measurement)
    uint16_t m_last_timestamp_num_processed;
    size_t m_accumulated_transfers;
};

} /* namespace hailort */
} /* namespace hailort */

#endif /* _HAILO_VDMA_CHANNEL_STATE_HPP_ */