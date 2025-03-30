/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include "../buffer_utils.hpp"

#include "common/file_utils.hpp"
#include "common/latency_meter.hpp"

#include "hailo/dma_mapped_buffer.hpp"

#include <chrono>
#include <string>

using namespace hailort;

constexpr uint32_t UNLIMITED_FRAMERATE = 0;
constexpr size_t   AMOUNT_OF_OUTPUT_BUFFERS_SYNC_API = 1;

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
// We use std::enable_from_this because on async api, we want to increase the ref count of this object until the
// callback is called. It can happen since network_group->shutdown() may be called after this object is being
// destructed.
template<typename Writer>
class WriterWrapper final : public std::enable_shared_from_this<WriterWrapper<Writer>>
{
public:
    template<typename WriterParams>
    static Expected<std::shared_ptr<WriterWrapper>> create(Writer &writer, const WriterParams &params,
        VDevice &vdevice, const LatencyMeterPtr &overall_latency_meter, uint32_t framerate, bool async_api)
    {
        TRY(auto dataset, create_dataset(writer, params));

        std::vector<DmaMappedBuffer> dataset_mapped_buffers;
        if (async_api) {
            TRY(dataset_mapped_buffers, dma_map_dataset(dataset, vdevice));
        }

        std::shared_ptr<WriterWrapper> wrapper(
            new (std::nothrow) WriterWrapper(writer, std::move(dataset), std::move(dataset_mapped_buffers),
                                             overall_latency_meter, framerate));
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

    template<typename CB>
    hailo_status write_async(CB &&callback)
    {
        before_write_start();
        auto self = std::enable_shared_from_this<WriterWrapper<Writer>>::shared_from_this();
        auto status = get().write_async(MemoryView(*next_buffer()),
            [self, original=callback](const typename Writer::CompletionInfo &completion_info) {
                (void)self; // Keeping self here so the buffer won't be deleted until the callback is called.
                original(completion_info.status);
            });
        if (HAILO_SUCCESS != status) {
            return status;
        }

        m_framerate_throttle.throttle();
        return HAILO_SUCCESS;
    }

private:
    WriterWrapper(Writer &writer, std::vector<BufferPtr> &&dataset, std::vector<DmaMappedBuffer> &&dataset_mapped_buffers,
                  const LatencyMeterPtr &overall_latency_meter, uint32_t framerate) :
        m_writer(std::ref(writer)),
        m_dataset(std::move(dataset)),
        m_dataset_mapped_buffers(std::move(dataset_mapped_buffers)),
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
            return create_random_dataset(writer.get_frame_size());
        } else {
            return create_dataset_from_input_file(params.input_file_path, writer.get_frame_size());
        }
    }

    static Expected<std::vector<BufferPtr>> create_random_dataset(size_t frame_size)
    {
        TRY(auto buffer,
            create_uniformed_buffer_shared(frame_size, BufferStorageParams::create_dma()));

        return std::vector<BufferPtr>{ buffer };
    }

    static Expected<std::vector<BufferPtr>> create_dataset_from_input_file(const std::string &file_path, size_t frame_size)
    {
        TRY(auto buffer, read_binary_file(file_path));
        CHECK_AS_EXPECTED(0 == (buffer.size() % frame_size), HAILO_INVALID_ARGUMENT,
            "Input file ({}) size {} must be a multiple of the frame size {}",
            file_path, buffer.size(), frame_size);

        std::vector<BufferPtr> dataset;
        const size_t frames_count = buffer.size() / frame_size;
        dataset.reserve(frames_count);
        for (size_t i = 0; i < frames_count; i++) {
            const auto offset = frame_size * i;
            TRY(auto frame_buffer,
                Buffer::create_shared(buffer.data() + offset, frame_size, BufferStorageParams::create_dma()));
            dataset.emplace_back(frame_buffer);
        }

        return dataset;
    }

    static Expected<std::vector<DmaMappedBuffer>> dma_map_dataset(const std::vector<BufferPtr> &dataset, VDevice &vdevice) {
        std::vector<DmaMappedBuffer> dataset_mapped_buffers;
        for (const auto &buffer : dataset) {
            TRY(auto mapped_buffer,
                DmaMappedBuffer::create(vdevice, buffer->data(), buffer->size(), HAILO_DMA_BUFFER_DIRECTION_H2D));
            dataset_mapped_buffers.emplace_back(std::move(mapped_buffer));
        }
        return dataset_mapped_buffers;
    }

    std::reference_wrapper<Writer> m_writer;

    std::vector<BufferPtr> m_dataset;
    std::vector<DmaMappedBuffer> m_dataset_mapped_buffers;
    size_t m_current_buffer_index = 0;

    LatencyMeterPtr m_overall_latency_meter;
    FramerateThrottle m_framerate_throttle;
};

template<typename Writer>
using WriterWrapperPtr = std::shared_ptr<WriterWrapper<Writer>>;

// Wrapper for OutputStream or OutputVStream objects.
// We use std::enable_from_this because on async api, we want to increase the ref count of this object until the
// callback is called. It can happen since network_group->shutdown() may be called after this object is being
// destructed.
template<typename Reader>
class ReaderWrapper final : public std::enable_shared_from_this<ReaderWrapper<Reader>>
{
public:

    // Function that gets the amount of output buffers needed for stream. Templated for both possible cases of Types that
    // ReaderWrapper can wrap - OutputStream and OutputVStream

    // In async create amount of output buffers equal to async_max_queue_size - we do this because in async mode we want
    // each stream to have its own buffer. (Otherwise can cause bugs in NMS Async mode.)
    static Expected<size_t> get_amount_of_output_buffers(OutputStream &output_stream, bool async_api)
    {
        if (async_api) {
            return output_stream.get_async_max_queue_size();
        } else {
            return static_cast<size_t>(AMOUNT_OF_OUTPUT_BUFFERS_SYNC_API);
        }
    }

    // Vstreams will always be sync hence 1 output buffer is enough.
    static Expected<size_t> get_amount_of_output_buffers(OutputVStream &output_vstream, bool async_api)
    {
        (void) output_vstream;
        (void) async_api;
        return static_cast<size_t>(AMOUNT_OF_OUTPUT_BUFFERS_SYNC_API);
    }

    static Expected<std::shared_ptr<ReaderWrapper>> create(Reader &reader, VDevice &vdevice,
        const LatencyMeterPtr &overall_latency_meter, std::shared_ptr<NetworkLiveTrack> net_live_track, bool async_api)
    {
        TRY(const auto amount_of_output_buffers, get_amount_of_output_buffers(reader,async_api));

        TRY(auto output_buffers, create_output_buffers(reader, amount_of_output_buffers));

        std::vector<DmaMappedBuffer> dma_mapped_buffers;
        if (async_api) {
            TRY(dma_mapped_buffers, dma_map_output_buffers(vdevice, amount_of_output_buffers, output_buffers));
        }

        std::shared_ptr<ReaderWrapper> wrapper(
            new (std::nothrow) ReaderWrapper(reader, std::move(output_buffers), std::move(dma_mapped_buffers),
            overall_latency_meter, net_live_track));
        CHECK_NOT_NULL_AS_EXPECTED(wrapper, HAILO_OUT_OF_HOST_MEMORY);

        return wrapper;
    }

    Reader &get() { return m_reader.get(); }
    Reader &get() const { return m_reader.get(); }

    hailo_status read()
    {
        auto status = get().read(MemoryView(*next_buffer()));
        if (HAILO_SUCCESS != status) {
            return status;
        }

        on_read_done();
        return HAILO_SUCCESS;
    }

    hailo_status wait_for_async_ready()
    {
        return get().wait_for_async_ready(m_buffer[0]->size(), HAILORTCLI_DEFAULT_TIMEOUT);
    }

    template<typename CB>
    hailo_status read_async(CB &&callback)
    {
        auto self = std::enable_shared_from_this<ReaderWrapper<Reader>>::shared_from_this();
        return get().read_async(MemoryView(*next_buffer()),
            [self, original=callback](const typename Reader::CompletionInfo &completion_info) {
                original(completion_info.status);
                if (completion_info.status == HAILO_SUCCESS) {
                    self->on_read_done();
                }
            });
    }

    void set_net_live_track(std::shared_ptr<NetworkLiveTrack> net_live_track)
    {
        m_net_live_track = net_live_track;
    }

private:
    ReaderWrapper(Reader &reader, std::vector<BufferPtr> &&buffer, std::vector<DmaMappedBuffer> &&mapped_buffer_ptr,
                  const LatencyMeterPtr &overall_latency_meter, std::shared_ptr<NetworkLiveTrack> net_live_track) :
        m_reader(std::ref(reader)),
        m_buffer(std::move(buffer)),
        m_mapped_buffer_ptr(std::move(mapped_buffer_ptr)),
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
    std::vector<BufferPtr> m_buffer;
    std::vector<DmaMappedBuffer> m_mapped_buffer_ptr;
    LatencyMeterPtr m_overall_latency_meter;
    std::shared_ptr<NetworkLiveTrack> m_net_live_track;
    size_t m_current_buffer_index = 0;

    static Expected<std::vector<BufferPtr>> create_output_buffers(Reader &reader, size_t amount_of_output_buffers)
    {
        std::vector<BufferPtr> output_buffers;
        output_buffers.reserve(amount_of_output_buffers);

        for (size_t i = 0; i < amount_of_output_buffers; i++) {
            TRY(auto buffer, Buffer::create_shared(reader.get_frame_size(), BufferStorageParams::create_dma()));
            output_buffers.emplace_back(std::move(buffer));
        }

        return output_buffers;
    }

    static Expected<std::vector<DmaMappedBuffer>>dma_map_output_buffers(VDevice &vdevice, size_t amount_of_output_buffers,
        const std::vector<BufferPtr> &output_buffers)
    {
        std::vector<DmaMappedBuffer> mapped_output_buffers;
        mapped_output_buffers.reserve(amount_of_output_buffers);
        
        for (const auto& output_buffer : output_buffers) {
            TRY(auto mapped_buffer,
                DmaMappedBuffer::create(vdevice, output_buffer->data(), output_buffer->size(), HAILO_DMA_BUFFER_DIRECTION_D2H));
            mapped_output_buffers.emplace_back(std::move(mapped_buffer));
        }

        return mapped_output_buffers;
    }

    size_t next_buffer_index()
    {
        const auto index = m_current_buffer_index;
        m_current_buffer_index = (m_current_buffer_index + 1) % m_buffer.size();
        return index;
    }

    BufferPtr next_buffer()
    {
        return m_buffer[next_buffer_index()];
    }
};

template<typename Reader>
using ReaderWrapperPtr = std::shared_ptr<ReaderWrapper<Reader>>;

#endif /* _HAILO_IO_WRAPPERS_HPP_ */
