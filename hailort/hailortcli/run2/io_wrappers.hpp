/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
**/
/**
 * @file io_wrappers.hpp
 * @brief Wrappers for Input/Output Stream/VStream. Manages buffer allocation, framerate throttle, latency meter and
 * more.
 **/

#ifndef _HAILO_IO_WRAPPERS_HPP_
#define _HAILO_IO_WRAPPERS_HPP_

#include "network_live_track.hpp"

#include "common/file_utils.hpp"
#include "common/latency_meter.hpp"

#include <chrono>
#include <string>

using namespace hailort;

constexpr uint32_t UNLIMITED_FRAMERATE = 0;

#ifndef HAILO_EMULATOR
constexpr std::chrono::milliseconds HAILORTCLI_DEFAULT_TIMEOUT(HAILO_DEFAULT_VSTREAM_TIMEOUT_MS);
#else /* ifndef HAILO_EMULATOR */
constexpr std::chrono::milliseconds HAILORTCLI_DEFAULT_TIMEOUT(HAILO_DEFAULT_VSTREAM_TIMEOUT_MS * 100);
#endif /* ifndef HAILO_EMULATOR */


class FramerateThrottle final
{
public:
    FramerateThrottle(uint32_t framerate);
    ~FramerateThrottle() = default;
    void throttle();

private:
    const uint32_t m_framerate;
    const std::chrono::duration<double> m_framerate_interval;
    decltype(std::chrono::steady_clock::now()) m_last_write_time;
};

// Wrapper for InputStream or InputVStream objects.
template<typename Writer>
class WriterWrapper final
{
public:
    template<typename WriterParams>
    static Expected<std::shared_ptr<WriterWrapper>> create(Writer &writer, const WriterParams &params,
        const LatencyMeterPtr &overall_latency_meter, uint32_t framerate)
    {
        auto dataset = create_dataset(writer, params);
        CHECK_EXPECTED(dataset);

        std::shared_ptr<WriterWrapper> wrapper(
            new (std::nothrow) WriterWrapper(writer, dataset.release(), overall_latency_meter, framerate));
        CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);

        return wrapper;
    }

    Writer &get() { return m_writer.get(); }
    Writer &get() const { return m_writer.get(); }

    hailo_status write()
    {
        before_write_start();
        auto status = get().write(MemoryView(*next_buffer()));
        if (HAILO_SUCCESS != status) {
            return status;
        }

        m_framerate_throttle.throttle();
        return HAILO_SUCCESS;
    }

    hailo_status wait_for_async_ready()
    {
        return get().wait_for_async_ready(m_dataset[0]->size(), HAILORTCLI_DEFAULT_TIMEOUT);
    }

    hailo_status write_async(typename Writer::TransferDoneCallback callback)
    {
        before_write_start();
        // We can use the same buffer for multiple writes simultaneously. That is OK since we don't modify the buffers.
        auto status = get().write_async(MemoryView(*next_buffer()), callback);
        if (HAILO_SUCCESS != status) {
            return status;
        }

        m_framerate_throttle.throttle();
        return HAILO_SUCCESS;
    }

private:
    WriterWrapper(Writer &writer, std::vector<BufferPtr> &&dataset, const LatencyMeterPtr &overall_latency_meter,
                  uint32_t framerate) :
        m_writer(std::ref(writer)),
        m_dataset(std::move(dataset)),
        m_overall_latency_meter(overall_latency_meter),
        m_framerate_throttle(framerate)
    {}

    void before_write_start()
    {
        if (m_overall_latency_meter) {
            m_overall_latency_meter->add_start_sample(std::chrono::steady_clock::now().time_since_epoch());
        }
    }

    size_t next_buffer_index()
    {
        const auto index = m_current_buffer_index;
        m_current_buffer_index = (m_current_buffer_index + 1) % m_dataset.size();
        return index;
    }

    BufferPtr next_buffer()
    {
        return m_dataset[next_buffer_index()];
    }

    template<typename WriterParams>
    static Expected<std::vector<BufferPtr>> create_dataset(Writer &writer, const WriterParams &params)
    {
        if (params.input_file_path.empty()) {
            return create_constant_dataset(writer.get_frame_size());
        } else {
            return create_dataset_from_input_file(params.input_file_path, writer.get_frame_size());
        }
    }

    static Expected<std::vector<BufferPtr>> create_constant_dataset(size_t frame_size)
    {
        const uint8_t const_byte = 0xAB;
        auto constant_buffer = Buffer::create_shared(frame_size, const_byte, BufferStorageParams::create_dma());
        CHECK_EXPECTED(constant_buffer);

        return std::vector<BufferPtr>{constant_buffer.release()};
    }

    static Expected<std::vector<BufferPtr>> create_dataset_from_input_file(const std::string &file_path, size_t frame_size)
    {
        auto buffer = read_binary_file(file_path);
        CHECK_EXPECTED(buffer);
        CHECK_AS_EXPECTED(0 == (buffer->size() % frame_size), HAILO_INVALID_ARGUMENT,
            "Input file ({}) size {} must be a multiple of the frame size {}",
            file_path, buffer->size(), frame_size);

        auto buffer_ptr = make_shared_nothrow<Buffer>(buffer.release());
        CHECK_NOT_NULL_AS_EXPECTED(buffer_ptr, HAILO_OUT_OF_HOST_MEMORY);

        std::vector<BufferPtr> dataset;
        const size_t frames_count = buffer->size() / frame_size;
        dataset.reserve(frames_count);
        for (size_t i = 0; i < frames_count; i++) {
            const auto offset = frame_size * i;
            auto frame_buffer = Buffer::create_shared(buffer->data() + offset, frame_size, BufferStorageParams::create_dma());
            CHECK_EXPECTED(frame_buffer);
            dataset.emplace_back(frame_buffer.release());
        }

        return dataset;
    }

    std::reference_wrapper<Writer> m_writer;

    std::vector<BufferPtr> m_dataset;
    size_t m_current_buffer_index = 0;

    LatencyMeterPtr m_overall_latency_meter;
    FramerateThrottle m_framerate_throttle;
};

template<typename Writer>
using WriterWrapperPtr = std::shared_ptr<WriterWrapper<Writer>>;

// Wrapper for OutputStream or OutputVStream objects.
// We use std::enable_from_this because on async api the callback is using `this`. We want to increase the reference
// count until the callback is over.
template<typename Reader>
class ReaderWrapper final : public std::enable_shared_from_this<ReaderWrapper<Reader>>
{
public:
    static Expected<std::shared_ptr<ReaderWrapper>> create(Reader &reader, const LatencyMeterPtr &overall_latency_meter,
        std::shared_ptr<NetworkLiveTrack> net_live_track)
    {
        auto buffer = Buffer::create_shared(reader.get_frame_size(), BufferStorageParams::create_dma());
        CHECK_EXPECTED(buffer);

        std::shared_ptr<ReaderWrapper> wrapper(
            new (std::nothrow) ReaderWrapper(reader, buffer.release(), overall_latency_meter, net_live_track));
        CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);

        return wrapper;
    }

    Reader &get() { return m_reader.get(); }
    Reader &get() const { return m_reader.get(); }

    hailo_status read()
    {
        auto status = get().read(MemoryView(*m_buffer));
        if (HAILO_SUCCESS != status) {
            return status;
        }

        on_read_done();
        return HAILO_SUCCESS;
    }

    hailo_status wait_for_async_ready()
    {
        return get().wait_for_async_ready(m_buffer->size(), HAILORTCLI_DEFAULT_TIMEOUT);
    }

    hailo_status read_async(typename Reader::TransferDoneCallback callback)
    {
        auto self = std::enable_shared_from_this<ReaderWrapper<Reader>>::shared_from_this();
        return get().read_async(MemoryView(*m_buffer),
            [self, original=callback](const typename Reader::CompletionInfo &completion_info) {
                original(completion_info);
                if (completion_info.status == HAILO_SUCCESS) {
                    self->on_read_done();
                }
            });
    }

private:
    ReaderWrapper(Reader &reader, BufferPtr &&buffer, const LatencyMeterPtr &overall_latency_meter,
                  std::shared_ptr<NetworkLiveTrack> net_live_track) :
        m_reader(std::ref(reader)),
        m_buffer(std::move(buffer)),
        m_overall_latency_meter(overall_latency_meter),
        m_net_live_track(net_live_track)
    {}

    void on_read_done()
    {
        if (m_overall_latency_meter) {
            m_overall_latency_meter->add_end_sample(get().name(), std::chrono::steady_clock::now().time_since_epoch());
        }

        if (m_net_live_track) {
            m_net_live_track->progress();
        }
    }

    std::reference_wrapper<Reader> m_reader;
    BufferPtr m_buffer;
    LatencyMeterPtr m_overall_latency_meter;
    std::shared_ptr<NetworkLiveTrack> m_net_live_track;
};

template<typename Reader>
using ReaderWrapperPtr = std::shared_ptr<ReaderWrapper<Reader>>;

#endif /* _HAILO_IO_WRAPPERS_HPP_ */
