/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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
#include <map>
#include <chrono>
#include <atomic>

namespace hailort
{

// Forward declaration
struct LayerInfo;

/*! Input (host to device) stream representation */
class HAILORTAPI InputStream
{
public:
    virtual ~InputStream() = default;

    InputStream(const InputStream&) = delete;
    InputStream& operator=(const InputStream&) = delete;

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
     */
    virtual hailo_status abort() = 0;

    /**
     * Clearing the aborted state of the stream.
     * 
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status clear_abort() = 0;

    /**
     * Writes all pending data to the underlying stream.
     * 
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status flush();

    /**
     * @returns a pointer for network group activated event.
     */
    virtual EventPtr &get_network_group_activated_event() = 0;

    /**
     * @returns whether the stream is managed by a network group scheduler.
     */
    virtual bool is_scheduled() = 0;

    /**
     * Writes the entire buffer to the stream without transformations
     *
     * @param[in] buffer    The buffer to be written.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
     * @note @a buffer is expected to be in the format dictated by this.stream_info.format
     * @note @a size is expected to be a product of this.stream_info.hw_frame_size (i.e. more than one frame may be written)
     */
    virtual hailo_status write(const MemoryView &buffer);

    /**
     * @returns A ::hailo_stream_info_t object containing the stream's info.
     */
    const hailo_stream_info_t &get_info() const
    {
        return m_stream_info;
    }

    /**
     * @returns the stream's hw frame size in bytes.
     */
    virtual inline size_t get_frame_size() const
    {
        return m_stream_info.hw_frame_size;
    }

    /**
     * @returns the stream's name.
     */
    std::string name() const
    {
        return m_stream_info.name;
    }

    /**
     * @returns the stream's description containing it's name and index.
     */
    virtual std::string to_string() const;

protected:
    InputStream() = default;
    InputStream(InputStream &&) = default;

    // Note: Implement sync_write_all_raw_buffer_no_transform_impl for the actual stream interaction in sub classes
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) = 0;

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) = 0;
    virtual hailo_status deactivate_stream() = 0;

    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) = 0;

    hailo_stream_info_t m_stream_info;
    uint8_t m_dataflow_manager_id;

private:
    friend class HefConfigurator;
    friend class ActivatedNetworkGroupBase;
};

/*! Output (device to host) stream representation */
class HAILORTAPI OutputStream
{
public:
    virtual ~OutputStream() = default;

    OutputStream(const OutputStream&) = delete;
    OutputStream& operator=(const OutputStream&) = delete;

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
     */
    virtual hailo_status abort() = 0;

    /**
     * Clearing the abort flag of the stream.
     * 
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status clear_abort() = 0;

    /**
     * @returns a pointer for network group activated event.
     */
    virtual EventPtr &get_network_group_activated_event() = 0;

    /**
     * @returns whether the stream is managed by a network group scheduler.
     */
    virtual bool is_scheduled() = 0;
    
    /**
     * @returns the stream's info.
     */
    const hailo_stream_info_t &get_info() const
    {
        return m_stream_info;
    }

    /**
     * @returns the stream's hw frame size.
     */
    virtual inline size_t get_frame_size() const
    {
        return m_stream_info.hw_frame_size;
    }

    /**
     * @returns the stream's name.
     */
    std::string name() const
    {
        return m_stream_info.name;
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
     * @param[out] buffer   A pointer to a buffer that receives the data read from the stream.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns an ::hailo_status error.
     * @note Upon return, @a buffer is expected to be in the format dictated by this.stream_info.format
     * @note @a size is expected to be a product of this.stream_info.hw_frame_size (i.e. more than one frame may be read)
     */
    virtual hailo_status read(MemoryView buffer);

protected:
    OutputStream() = default;
    OutputStream(OutputStream&&);

    virtual hailo_status activate_stream(uint16_t dynamic_batch_size) = 0;
    virtual hailo_status deactivate_stream() = 0;
    virtual hailo_status read_all(MemoryView &buffer) = 0;

    virtual Expected<size_t> sync_read_raw_buffer(MemoryView &buffer) = 0;

    hailo_stream_info_t m_stream_info;
    uint8_t m_dataflow_manager_id;
    std::atomic<uint32_t> m_invalid_frames_count;

private:
    virtual const LayerInfo& get_layer_info() = 0;
    hailo_status read_nms(void *buffer, size_t offset, size_t size);
    void increase_invalid_frames_count(uint32_t value);

    friend class HefConfigurator;
    friend class ActivatedNetworkGroupBase;
    friend class HwReadElement;
    friend class OutputDemuxer;
};

} /* namespace hailort */

#endif /* _HAILO_STREAM_HPP_ */
