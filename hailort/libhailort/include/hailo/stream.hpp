/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file stream.hpp
 * @brief Input/Output streaming to the device
 **/

#ifndef _HAILO_STREAM_HPP_
#define _HAILO_STREAM_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/event.hpp"

#include <memory>
#include <chrono>
#include <atomic>
#include <functional>


/** hailort namespace */
namespace hailort
{

#define INVALID_CORE_OP_HANDLE (UINT32_MAX)
using device_id_t = std::string;
using vdevice_core_op_handle_t = uint32_t;

/*! Input (host to device) stream representation */
class HAILORTAPI InputStream
{
public:
    virtual ~InputStream() = default;

    InputStream(const InputStream&) = delete;
    InputStream& operator=(const InputStream&) = delete;

    /** Context passed to the \ref TransferDoneCallback after the async operation is done or has failed. */
    struct CompletionInfo
    {
        /**
         * Status of the async transfer.
         * - ::HAILO_SUCCESS - When transfer is complete successfully.
         * - ::HAILO_STREAM_ABORT - The transfer was canceled (can happen after network deactivation).
         * - Any other ::hailo_status on unexpected errors.
         */
        hailo_status status;

        union {
            const void *buffer_addr;    /* Points to the transferred buffer. */
            int dmabuf_fd;              /* File descriptor to dmabuf*/
        };
        size_t buffer_size;             /* Size of the transferred buffer. */

        CompletionInfo(hailo_status status, const uint8_t *addr, size_t size):
            status(status), buffer_addr(static_cast<const void*>(addr)), buffer_size(size) {}
        
        CompletionInfo(hailo_status status, int fd, size_t size):
            status(status), dmabuf_fd(fd), buffer_size(size) {}
    };

    /** Async transfer complete callback prototype. */
    using TransferDoneCallback = std::function<void(const CompletionInfo &completion_info)>;

    /**
     * Set new timeout value to the input stream
     *
     * @param[in] timeout      The new timeout value to be set in milliseconds.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) = 0;

    /**
     * @return The input stream's timeout in milliseconds.
     */
    virtual std::chrono::milliseconds get_timeout() const = 0;

    /**
     * @return The input stream's interface.
     */
    virtual hailo_stream_interface_t get_interface() const = 0;

    /**
     * Aborting the stream.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function is deprecated. One should use ConfiguredNetworkGroup::shutdown()
     */
    virtual hailo_status abort()
        DEPRECATED("InputStream::abort is deprecated. One should use ConfiguredNetworkGroup::shutdown()") = 0;

    /**
     * Clearing the aborted state of the stream.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function is deprecated. To reuse network after shutdown, reconfigure it.
     */
    virtual hailo_status clear_abort()
        DEPRECATED("InputStream::clear_abort() is deprecated. To reuse network after shutdown, reconfigure it") = 0;

    /**
     * Writes all pending data to the underlying stream.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status flush();

    /**
     * @returns a pointer for network group activated event.
     */
    EventPtr &get_network_group_activated_event()
        DEPRECATED("'InputStream::get_network_group_activated_event' is deprecated.");

    /**
     * @returns whether the stream is managed by the model scheduler.
     */
    virtual bool is_scheduled() = 0;

    /**
     * Writes the entire buffer to the stream without transformations.
     *
     * @param[in] buffer    The buffer to be written.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
     *
     * @note @a buffer is expected to be in the format dictated by this.stream_info.format
     * @note @a buffer.size() is expected to be get_frame_size().
     */
    virtual hailo_status write(const MemoryView &buffer) = 0;

    /**
     * Writes the entire buffer to the stream without transformations.
     *
     * @param[in] buffer    The buffer to be written.
     * @param[in] size      The size of the buffer given.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
     *
     * @note @a buffer is expected to be in the format dictated by this.stream_info.format
     * @note @a size is expected to be get_frame_size().
     */
    virtual hailo_status write(const void *buffer, size_t size) = 0;

    /**
     * Waits until the stream is ready to launch a new write_async() operation. Each stream contains some limited sized
     * queue for ongoing transfers. Calling get_async_max_queue_size() will return the queue size for current stream.
     *
     * @param[in] transfer_size     Must be get_frame_size().
     * @param[in] timeout           Amount of time to wait until the stream is ready in milliseconds.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *           - If @a timeout has passed and the stream is not ready, returns ::HAILO_TIMEOUT.
     *           - In any other error case, returns ::hailo_status error.
     */
    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout);

    /**
     * Returns the maximum amount of frames that can be simultaneously written to the stream (by write_async() calls)
     * before any one of the write operations is complete, as signified by @a user_callback being called.
     *
     * @return Upon success, returns Expected of a the queue size.
     *   Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<size_t> get_async_max_queue_size() const;

    /**
     * Writes the contents of @a buffer to the stream asynchronously, initiating a deferred operation that will be
     * completed later.
     * - If the function call succeeds (i.e., write_async() returns ::HAILO_SUCCESS), the deferred operation has been
     *   initiated. Until @a user_callback is called, the user cannot change or delete @a buffer.
     * - If the function call fails (i.e., write_async() returns a status other than ::HAILO_SUCCESS), the deferred
     *   operation will not be initiated and @a user_callback will not be invoked. The user is free to change or delete
     *   @a buffer.
     * - @a user_callback is triggered upon successful completion or failure of the deferred operation. The callback
     *   receives a \ref CompletionInfo object containing a pointer to the transferred buffer (@a buffer_addr) and the
     *   transfer status (@a status).
     *
     * @param[in] buffer            The buffer to be written.
     *                              The buffer must be aligned to the system page size.
     * @param[in] user_callback     The callback that will be called when the transfer is complete
     *                              or has failed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *           - If the stream queue is full, returns ::HAILO_QUEUE_IS_FULL. In this case please wait
     *             until @a user_callback is called on previous writes, or call wait_for_async_ready().
     *             The size of the queue can be determined by calling get_async_max_queue_size().
     *           - In any other error case, returns a ::hailo_status error.
     *
     * @note @a user_callback should run as quickly as possible.
     * @note The buffer's format comes from the @a format field inside get_info() and the shape comes from
     *       the @a hw_shape field inside get_info().
     * @note The address provided must be aligned to the system's page size, and the rest of the page should not be in
     *       use by any other part of the program to ensure proper functioning of the DMA operation. Memory for the
     *       provided address can be allocated using `mmap` on Unix-like systems or `VirtualAlloc` on Windows.
     * @note Pre-mapping @a buffer to DMA via `Device::dma_map()` may improve performance, if @a buffer is used for
     *       multiple async transfers.
     */
    virtual hailo_status write_async(const MemoryView &buffer, const TransferDoneCallback &user_callback) = 0;

    /**
     * Writes the contents of @a buffer to the stream asynchronously, initiating a deferred operation that will be
     * completed later.
     * - If the function call succeeds (i.e., write_async() returns ::HAILO_SUCCESS), the deferred operation has been
     *   initiated. Until @a user_callback is called, the user cannot change or delete @a buffer.
     * - If the function call fails (i.e., write_async() returns a status other than ::HAILO_SUCCESS), the deferred
     *   operation will not be initiated and @a user_callback will not be invoked. The user is free to change or delete
     *   @a buffer.
     * - @a user_callback is triggered upon successful completion or failure of the deferred operation. The callback
     *   receives a \ref CompletionInfo object containing a pointer to the transferred buffer (@a buffer_addr) and the
     *   transfer status (@a status).
     *
     * @param[in] buffer            The buffer to be written.
     *                              The buffer must be aligned to the system page size.
     * @param[in] size              The size of the given buffer, expected to be get_frame_size().
     * @param[in] user_callback     The callback that will be called when the transfer is complete
     *                              or has failed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *           - If the stream queue is full, returns ::HAILO_QUEUE_IS_FULL. In this case please wait
     *             until @a user_callback is called on previous writes, or call wait_for_async_ready().
     *             The size of the queue can be determined by calling get_async_max_queue_size().
     *           - In any other error case, returns a ::hailo_status error.
     *
     * @note @a user_callback should run as quickly as possible.
     * @note The buffer's format comes from the @a format field inside get_info() and the shape comes from
     *       the @a hw_shape field inside get_info().
     * @note The address provided must be aligned to the system's page size, and the rest of the page should not be in
     *       use by any other part of the program to ensure proper functioning of the DMA operation. Memory for the
     *       provided address can be allocated using `mmap` on Unix-like systems or `VirtualAlloc` on Windows.
     * @note Pre-mapping @a buffer to DMA via `Device::dma_map()` may improve performance, if @a buffer is used for
     *       multiple async transfers.
     */
    virtual hailo_status write_async(const void *buffer, size_t size, const TransferDoneCallback &user_callback) = 0;

    /**
     * Writes the contents of the dmabuf associated with the fd @a dmabuf_fd to the stream asynchronously, initiating a deferred operation that will be
     * completed later.
     * - If the function call succeeds (i.e., write_async() returns ::HAILO_SUCCESS), the deferred operation has been
     *   initiated. Until @a user_callback is called, the user cannot change or delete @a dmabuf_fd.
     * - If the function call fails (i.e., write_async() returns a status other than ::HAILO_SUCCESS), the deferred
     *   operation will not be initiated and @a user_callback will not be invoked. The user is free to change or delete
     *   @a dmabuf_fd.
     * - @a user_callback is triggered upon successful completion or failure of the deferred operation. The callback
     *   receives a \ref CompletionInfo object containing a fd representing the transferred buffer (@a dmabuf_fd) and the
     *   transfer status (@a status).
     *
     * @param[in] dmabuf_fd         The File descriptor to the dmabuf
     * @param[in] size              The size of the given buffer, expected to be get_frame_size().
     * @param[in] user_callback     The callback that will be called when the transfer is complete
     *                              or has failed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *           - If the stream queue is full, returns ::HAILO_QUEUE_IS_FULL. In this case please wait
     *             until @a user_callback is called on previous writes, or call wait_for_async_ready().
     *             The size of the queue can be determined by calling get_async_max_queue_size().
     *           - In any other error case, returns a ::hailo_status error.
     *
     * @note @a user_callback should run as quickly as possible.
     * @note The dmabuf fd must be a linux-system dmabuf fd - otherwise behavior will fail.
     * @note This API of write_async is currently experimental.
     **/
    virtual hailo_status write_async(int dmabuf_fd, size_t size, const TransferDoneCallback &user_callback) = 0;

    /**
     * @returns A ::hailo_stream_info_t object containing the stream's info.
     */
    virtual const hailo_stream_info_t &get_info() const
    {
        return m_stream_info;
    }

    /**
     * @returns the quant_infos - quant info per feature.
     */
    virtual const std::vector<hailo_quant_info_t> &get_quant_infos() const
    {
        return m_quant_infos;
    }

    /**
     * @returns the stream's hw frame size in bytes.
     */
    virtual inline size_t get_frame_size() const
    {
        return get_info().hw_frame_size;
    }

    /**
     * @returns the stream's name.
     */
    std::string name() const
    {
        return get_info().name;
    }

    /**
     * @returns the stream's description containing it's name and index.
     */
    virtual std::string to_string() const;

    // get_network_group_activated_event is same as this function
    virtual EventPtr &get_core_op_activated_event() = 0;

protected:
    InputStream() = default;
    InputStream(InputStream &&) = delete;

    hailo_stream_info_t m_stream_info;
    std::vector<hailo_quant_info_t> m_quant_infos;
    uint8_t m_dataflow_manager_id;
};

/*! Output (device to host) stream representation */
class HAILORTAPI OutputStream
{
public:
    virtual ~OutputStream() = default;

    OutputStream(const OutputStream&) = delete;
    OutputStream& operator=(const OutputStream&) = delete;

    /** Context passed to the \ref TransferDoneCallback after the async operation is done or has failed. */
    struct CompletionInfo
    {
        public:
        /**
         * Status of the async transfer.
         * - ::HAILO_SUCCESS - When transfer is complete successfully.
         * - ::HAILO_STREAM_ABORT - The transfer was canceled (can happen after network deactivation).
         * - Any other ::hailo_status on unexpected errors.
         */
        hailo_status status;

        union {
            void *buffer_addr;          /* Points to the transferred buffer. */
            int dmabuf_fd;              /* File descriptor to dmabuf*/
        };
        
        size_t buffer_size;             /* Size of the transferred buffer. */

        CompletionInfo(hailo_status status, uint8_t *addr, size_t size):
            status(status), buffer_addr(static_cast<void*>(addr)), buffer_size(size) {}
        
        CompletionInfo(hailo_status status, int fd, size_t size):
            status(status), dmabuf_fd(fd), buffer_size(size) {}
    };

    /** Async transfer complete callback prototype. */
    using TransferDoneCallback = std::function<void(const CompletionInfo &completion_info)>;

    /**
     * Set new timeout value to the output stream
     *
     * @param[in] timeout      The new timeout value to be set in milliseconds.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) = 0;

    /**
     * @return returns the output stream's timeout in milliseconds.
     */
    virtual std::chrono::milliseconds get_timeout() const = 0;

    /**
     * @return returns the output stream's interface.
     */
    virtual hailo_stream_interface_t get_interface() const = 0;

    /**
     * Aborting the stream.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function is deprecated. One should use ConfiguredNetworkGroup::shutdown()
     */
    virtual hailo_status abort()
        DEPRECATED("OutputStream::abort is deprecated. One should use ConfiguredNetworkGroup::shutdown()")= 0;

    /**
     * Clearing the abort flag of the stream.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function is deprecated. To reuse network after shutdown, reconfigure it.
     */
    virtual hailo_status clear_abort()
        DEPRECATED("OutputStream::clear_abort is deprecated. To reuse network after shutdown, reconfigure it") = 0;

    /**
     * @returns a pointer for network group activated event.
     */
    EventPtr &get_network_group_activated_event()
        DEPRECATED("'OutputStream::get_network_group_activated_event' is deprecated.");

    /**
     * @returns whether the stream is managed by the model scheduler.
     */
    virtual bool is_scheduled() = 0;

    /**
     * @returns the stream's info.
     */
    virtual const hailo_stream_info_t &get_info() const
    {
        return m_stream_info;
    }

    /**
     * @returns the quant_infos - quant info per feature.
     */
    virtual const std::vector<hailo_quant_info_t> &get_quant_infos() const
    {
        return m_quant_infos;
    }

    /**
     * @returns the stream's hw frame size.
     */
    virtual inline size_t get_frame_size() const
    {
        return get_info().hw_frame_size;
    }

    /**
     * @returns the stream's name.
     */
    std::string name() const
    {
        return get_info().name;
    }

    /**
     * @returns the stream's description containing it's name and index.
     */
    virtual std::string to_string() const;

    /**
     * @returns the number of invalid frames received by the stream.
     */
    uint32_t get_invalid_frames_count() const;

    /**
     * Reads the entire buffer from the stream without transformations
     *
     * @param[in] buffer            A buffer that receives the data read from the stream.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
     * @note Upon return, @a buffer is expected to be in the format dictated by this.get_info().format
     * @note @a size is expected to be get_frame_size().
     */
    virtual hailo_status read(MemoryView buffer) = 0;

    /**
     * Reads the entire buffer from the stream without transformations
     *
     * @param[in] buffer   A pointer to a buffer that receives the data read from the stream.
     * @param[in] size     The size of the given buffer.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
     *
     * @note Upon return, @a buffer is expected to be in the format dictated by this.get_info().format
     * @note @a size is expected to be get_frame_size().
     */
    virtual hailo_status read(void *buffer, size_t size) = 0;

    /**
     * Waits until the stream is ready to launch a new read_async() operation. Each stream contains some limited sized
     * queue for ongoing transfers. Calling get_async_max_queue_size() will return the queue size for current stream.
     *
     * @param[in] transfer_size     Must be get_frame_size().
     * @param[in] timeout           Amount of time to wait until the stream is ready in milliseconds.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *           - If @a timeout has passed and the stream is not ready, returns ::HAILO_TIMEOUT.
     *           - In any other error case, returns ::hailo_status error.
     */
    virtual hailo_status wait_for_async_ready(size_t transfer_size, std::chrono::milliseconds timeout);

    /**
     * Returns the maximum amount of frames that can be simultaneously read from the stream (by read_async() calls)
     * before any one of the read operations is complete, as signified  by @a user_callback being called.
     *
     * @return Upon success, returns Expected of a the queue size.
     *   Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<size_t> get_async_max_queue_size() const;

    /**
     * Reads into @a buffer from the stream asynchronously, initiating a deferred operation that will be completed
     * later.
     * - If the function call succeeds (i.e., read_async() returns ::HAILO_SUCCESS), the deferred operation has been
     *   initiated. Until @a user_callback is called, the user cannot change or delete @a buffer.
     * - If the function call fails (i.e., read_async() returns a status other than ::HAILO_SUCCESS), the deferred
     *   operation will not be initiated and @a user_callback will not be invoked. The user is free to change or
     *   delete @a buffer.
     * - @a user_callback is triggered upon successful completion or failure of the deferred operation.
     *   The callback receives a \ref CompletionInfo object containing a pointer to the transferred buffer
     *   (@a buffer_addr) and the transfer status (@a status). If the operation has completed successfully, the contents
     *   of @a buffer will have been updated by the read operation.
     *
     * @param[in] buffer        The buffer to be read into.
     *                          The buffer must be aligned to the system page size.
     * @param[in] user_callback The callback that will be called when the transfer is complete or has failed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *         - If the stream queue is full, returns ::HAILO_QUEUE_IS_FULL.
     *           In this case, please wait until @a user_callback is called on previous
     *           reads, or call wait_for_async_ready(). The size of the queue can be
     *           determined by calling get_async_max_queue_size().
     *         - In any other error case, returns a ::hailo_status error.
     * @note @a user_callback should execute as quickly as possible.
     * @note The buffer's format is determined by the @a format field inside get_info(),
     *       and the shape is determined by the @a hw_shape field inside get_info().
     * @note The address provided must be aligned to the system's page size, and the rest of the page should not be in
     *       use by any other part of the program to ensure proper functioning of the DMA operation. Memory for the
     *       provided address can be allocated using `mmap` on Unix-like systems or `VirtualAlloc` on Windows.
     * @note Pre-mapping @a buffer to DMA via `Device::dma_map()` may improve performance, if @a buffer is used for
     *       multiple async transfers.
     */
    virtual hailo_status read_async(MemoryView buffer, const TransferDoneCallback &user_callback) = 0;

    /**
     * Reads into @a buffer from the stream asynchronously, initiating a deferred operation that will be completed
     * later.
     * - If the function call succeeds (i.e., read_async() returns ::HAILO_SUCCESS), the deferred operation has been
     *   initiated. Until @a user_callback is called, the user cannot change or delete @a buffer.
     * - If the function call fails (i.e., read_async() returns a status other than ::HAILO_SUCCESS), the deferred
     *   operation will not be initiated and @a user_callback will not be invoked. The user is free to change or
     *   delete @a buffer.
     * - @a user_callback is triggered upon successful completion or failure of the deferred operation.
     *   The callback receives a \ref CompletionInfo object containing a pointer to the transferred buffer
     *   (@a buffer_addr) and the transfer status (@a status). If the operation has completed successfully, the contents
     *   of @a buffer will have been updated by the read operation.
     *
     * @param[in] buffer        The buffer to be read into.
     *                          The buffer must be aligned to the system page size.
     * @param[in] size          The size of the given buffer, expected to be get_frame_size().
     * @param[in] user_callback The callback that will be called when the transfer is complete or has failed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *         - If the stream queue is full, returns ::HAILO_QUEUE_IS_FULL.
     *           In this case, please wait until @a user_callback is called on previous
     *           reads, or call wait_for_async_ready(). The size of the queue can be
     *           determined by calling get_async_max_queue_size().
     *         - In any other error case, returns a ::hailo_status error.
     * @note @a user_callback should execute as quickly as possible.
     * @note The buffer's format is determined by the @a format field inside get_info(),
     *       and the shape is determined by the @a hw_shape field inside get_info()
     * @note The address provided must be aligned to the system's page size, and the rest of the page should not be in
     *       use by any other part of the program to ensure proper functioning of the DMA operation. Memory for the
     *       provided address can be allocated using `mmap` on Unix-like systems or `VirtualAlloc` on Windows.
     * @note Pre-mapping @a buffer to DMA via `Device::dma_map()` may improve performance, if @a buffer is used for
     *       multiple async transfers.
     */
    virtual hailo_status read_async(void *buffer, size_t size, const TransferDoneCallback &user_callback) = 0;

    /**
     * Reads into dmabuf represented by fd @a dmabuf_fd from the stream asynchronously, initiating a deferred operation that will be completed
     * later.
     * - If the function call succeeds (i.e., read_async() returns ::HAILO_SUCCESS), the deferred operation has been
     *   initiated. Until @a user_callback is called, the user cannot change or delete @a dmabuf_fd.
     * - If the function call fails (i.e., read_async() returns a status other than ::HAILO_SUCCESS), the deferred
     *   operation will not be initiated and @a user_callback will not be invoked. The user is free to change or
     *   delete @a dmabuf_fd.
     * - @a user_callback is triggered upon successful completion or failure of the deferred operation.
     *   The callback receives a \ref CompletionInfo object containing a fd representing the transferred buffer
     *   (@a dmabuf_fd) and the transfer status (@a status). If the operation has completed successfully, the contents
     *   of the dmabuf will have been updated by the read operation.
     *
     * @param[in] dmabuf_fd     The File descriptor to the dmabuf
     * @param[in] size          The size of the given buffer, expected to be get_frame_size().
     * @param[in] user_callback The callback that will be called when the transfer is complete or has failed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise:
     *         - If the stream queue is full, returns ::HAILO_QUEUE_IS_FULL.
     *           In this case, please wait until @a user_callback is called on previous
     *           reads, or call wait_for_async_ready(). The size of the queue can be
     *           determined by calling get_async_max_queue_size().
     *         - In any other error case, returns a ::hailo_status error.
     * @note @a user_callback should execute as quickly as possible.
     * @note The dmabuf fd must be a linux-system dmabuf fd - otherwise behavior will fail.
     * @note This API of read_async is currently experimental.
     */
    virtual hailo_status read_async(int dmabuf_fd, size_t size, const TransferDoneCallback &user_callback) = 0;

    // get_network_group_activated_event is same as this function
    virtual EventPtr &get_core_op_activated_event() = 0;
protected:
    OutputStream() = default;
    OutputStream(OutputStream&&) = delete;

    hailo_stream_info_t m_stream_info;
    std::vector<hailo_quant_info_t> m_quant_infos;
    uint8_t m_dataflow_manager_id;
    std::atomic<uint32_t> m_invalid_frames_count;

private:
    void increase_invalid_frames_count(uint32_t value);

    friend class HwReadElement;
};

} /* namespace hailort */

#endif /* _HAILO_STREAM_HPP_ */
