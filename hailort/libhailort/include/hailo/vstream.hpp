/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream.hpp
 * @brief Virtual Stream
 **/

#ifndef _HAILO_VSTREAM_HPP_
#define _HAILO_VSTREAM_HPP_

#include "hailo/transform.hpp"
#include "hailo/stream.hpp"
#include "hailo/network_group.hpp"
#include "hailo/runtime_statistics.hpp"

namespace hailort
{

class OutputVStreamInternal;
class InputVStreamInternal;
class SinkElement;
class PipelineElement;

class HAILORTAPI InputVStream
{
public:
    static Expected<InputVStream> create(const hailo_vstream_info_t &vstream_info,
        const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
        std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, EventPtr network_group_activated_event,
        AccumulatorPtr pipeline_latency_accumulator);
    InputVStream(InputVStream &&other) noexcept = default;
    InputVStream &operator=(InputVStream &&other) noexcept = default;
    virtual ~InputVStream() = default;

    /**
     * Writes @a buffer to hailo device.
     *
     * @param[in] buffer            The buffer containing the data to be sent to device.
     *                              The buffer's format can be obtained by get_user_buffer_format(),
     *                              and the buffer's shape can be obtained by calling get_info().shape.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status write(const MemoryView &buffer);

    /**
     * Flushes the vstream pipeline buffers. This will block until the vstream pipeline is clear.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status flush();

    /**
     * Clears the vstreams' pipeline buffers.
     *
     * @param[in] vstreams            The vstreams to be cleared.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    static hailo_status clear(std::vector<InputVStream> &vstreams);

    /**
     * Clears the vstreams' pipeline buffers.
     *
     * @param[in] vstreams            The vstreams to be cleared.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    static hailo_status clear(std::vector<std::reference_wrapper<InputVStream>> &vstreams);

    /**
     * Aborts vstream until its resumed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status abort();

    /**
     * Resumes vstream after it was aborted.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status resume();

    /**
     * @return the size of a virtual stream's frame on the host side in bytes.
     * @note The size could be affected by the format type - using UINT16, or by the data not being quantized yet.
     */
    size_t get_frame_size() const;

    /**
     * @return ::hailo_vstream_info_t object containing the vstream info.
     */
    const hailo_vstream_info_t &get_info() const;

    /**
     * @return ::hailo_format_t object containing the user buffer format.
     */
    const hailo_format_t &get_user_buffer_format() const;

    /**
     * @return the virtual stream's name.
     */
    std::string name() const;

    /**
     * @return the virtual stream's network name.
     */
    std::string network_name() const;

    /**
     * Gets a reference to a map between pipeline element names to their respective fps accumulators.
     * These accumulators measure the net throughput of each pipeline element. This means that the effects
     * of queuing in the vstream pipeline (between elements) are not accounted for by these accumulators.
     * 
     * @return A const reference to a map between pipeline element names to their respective fps accumulators.
     * @note FPS accumulators are created for pipeline elements, if the vstream is created with the flag
     *       ::HAILO_PIPELINE_ELEM_STATS_MEASURE_FPS set under the @a pipeline_elements_stats_flags field of ::hailo_vstream_params_t.
     */
    const std::map<std::string, AccumulatorPtr> &get_fps_accumulators() const;
    
    /**
     * Gets a reference to a map between pipeline element names to their respective latency accumulators.
     * These accumulators measure the net latency of each pipeline element. This means that the effects
     * of queuing in the vstream pipeline (between elements) are not accounted for by these accumulators.
     * 
     * @return A const reference to a map between pipeline element names to their respective latency accumulators.
     * @note Latency accumulators are created for pipeline elements, if the vstream is created with the flag
     *       ::HAILO_PIPELINE_ELEM_STATS_MEASURE_LATENCY set under the @a pipeline_elements_stats_flags field of ::hailo_vstream_params_t.
     */
    const std::map<std::string, AccumulatorPtr> &get_latency_accumulators() const;

    /**
     * Gets a reference to a map between pipeline element names to their respective queue size accumulators.
     * These accumulators measure the number of free buffers in the queue, right before a buffer is removed
     * from the queue to be used.
     * 
     * @return A const reference to a map between pipeline element names to their respective queue size accumulators.
     * @note Queue size accumulators are created for pipeline elements, if the vstream is created with the flag
     *       ::HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE set under the @a pipeline_elements_stats_flags field of ::hailo_vstream_params_t.
     */
    const std::map<std::string, std::vector<AccumulatorPtr>> &get_queue_size_accumulators() const;
    
    /**
     * Gets a shared_ptr to the vstream's latency accumulator. This accumulator measures the time it takes for a frame to pass
     * through an entire vstream pipeline. Specifically:
     * * For InputVStream%s: The time it takes a frame from the call to InputVStream::write, until the frame is written to the HW.
     * * For OutputVStream%s: The time it takes a frame from being read from the HW, until it's returned to the user via OutputVStream::read.
     * 
     * @return A shared pointer to the vstream's latency accumulator.
     * @note A pipeline-wide latency accumulator is created for the vstream, if the vstream is created with the flag
     *       ::HAILO_VSTREAM_STATS_MEASURE_LATENCY set under the @a vstream_stats_flags field of ::hailo_vstream_params_t.
     */
    AccumulatorPtr get_pipeline_latency_accumulator() const;
    
    /**
     * @return A const reference to the @a PipelineElement%s that this vstream is comprised of.
     */
    const std::vector<std::shared_ptr<PipelineElement>> &get_pipeline() const;

protected:
    explicit InputVStream(std::shared_ptr<InputVStreamInternal> vstream);
    std::string get_pipeline_description() const;

    hailo_status start_vstream();
    hailo_status stop_vstream();
    hailo_status stop_and_clear();

    std::shared_ptr<InputVStreamInternal> m_vstream;

    friend class VStreamsBuilderUtils;
};

class HAILORTAPI OutputVStream
{
public:
    static Expected<OutputVStream> create(
        const hailo_vstream_info_t &vstream_info, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
        EventPtr network_group_activated_event, AccumulatorPtr pipeline_latency_accumulator);
    OutputVStream(OutputVStream &&other) noexcept = default;
    OutputVStream &operator=(OutputVStream &&other) noexcept = default;
    virtual ~OutputVStream() = default;

    /**
     * Reads data from hailo device into @a buffer.
     *
     * @param[in] buffer            The buffer to read data into.
     *                              The buffer's format can be obtained by get_user_buffer_format(), 
     *                              and the buffer's shape can be obtained by calling get_info().shape.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status read(MemoryView buffer);

    /**
     * Clears the vstreams' pipeline buffers.
     *
     * @param[in] vstreams            The vstreams to be cleared.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    static hailo_status clear(std::vector<OutputVStream> &vstreams);

    /**
     * Clears the vstreams' pipeline buffers.
     *
     * @param[in] vstreams            The vstreams to be cleared.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    static hailo_status clear(std::vector<std::reference_wrapper<OutputVStream>> &vstreams);

    /**
     * Aborts vstream until its resumed.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status abort();

    /**
     * Resumes vstream after it was aborted.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    hailo_status resume();

    /**
     * @return the size of a virtual stream's frame on the host side in bytes.
     * @note The size could be affected by the format type - using UINT16, or by the data not being quantized yet.
     */
    size_t get_frame_size() const;

    /**
     * @return ::hailo_vstream_info_t object containing the vstream info.
     */
    const hailo_vstream_info_t &get_info() const;

    /**
     * @return ::hailo_format_t object containing the user buffer format.
     */
    const hailo_format_t &get_user_buffer_format() const;

    /**
     * @return the virtual stream's name.
     */
    std::string name() const;

    /**
     * @return the virtual stream's network name.
     */
    std::string network_name() const;

    /**
     * Gets a reference to a map between pipeline element names to their respective fps accumulators.
     * These accumulators measure the net throughput of each pipeline element. This means that the effects
     * of queuing in the vstream pipeline (between elements) are not accounted for by these accumulators.
     * 
     * @return A const reference to a map between pipeline element names to their respective fps accumulators.
     * @note FPS accumulators are created for pipeline elements, if the vstream is created with the flag
     *       ::HAILO_PIPELINE_ELEM_STATS_MEASURE_FPS set under the @a pipeline_elements_stats_flags field of ::hailo_vstream_params_t.
     */
    const std::map<std::string, AccumulatorPtr> &get_fps_accumulators() const;
    
    /**
     * Gets a reference to a map between pipeline element names to their respective latency accumulators.
     * These accumulators measure the net latency of each pipeline element. This means that the effects
     * of queuing in the vstream pipeline (between elements) are not accounted for by these accumulators.
     * 
     * @return A const reference to a map between pipeline element names to their respective latency accumulators.
     * @note Latency accumulators are created for pipeline elements, if the vstream is created with the flag
     *       ::HAILO_PIPELINE_ELEM_STATS_MEASURE_LATENCY set under the @a pipeline_elements_stats_flags field of ::hailo_vstream_params_t.
     */
    const std::map<std::string, AccumulatorPtr> &get_latency_accumulators() const;

    /**
     * Gets a reference to a map between pipeline element names to their respective queue size accumulators.
     * These accumulators measure the number of free buffers in the queue, right before a buffer is removed
     * from the queue to be used.
     * 
     * @return A const reference to a map between pipeline element names to their respective queue size accumulators.
     * @note Queue size accumulators are created for pipeline elements, if the vstream is created with the flag
     *       ::HAILO_PIPELINE_ELEM_STATS_MEASURE_QUEUE_SIZE set under the @a pipeline_elements_stats_flags field of ::hailo_vstream_params_t.
     */
    const std::map<std::string, std::vector<AccumulatorPtr>> &get_queue_size_accumulators() const;
    
    /**
     * Gets a shared_ptr to the vstream's latency accumulator. This accumulator measures the time it takes for a frame to pass
     * through an entire vstream pipeline. Specifically:
     * * For InputVStream%s: The time it takes a frame from the call to InputVStream::write, until the frame is written to the HW.
     * * For OutputVStream%s: The time it takes a frame from being read from the HW, until it's returned to the user via OutputVStream::read.
     * 
     * @return A shared pointer to the vstream's latency accumulator.
     * @note A pipeline-wide latency accumulator is created for the vstream, if the vstream is created with the flag
     *       ::HAILO_VSTREAM_STATS_MEASURE_LATENCY set under the @a vstream_stats_flags field of ::hailo_vstream_params_t.
     */
    AccumulatorPtr get_pipeline_latency_accumulator() const;
    
    /**
     * @return A const reference to the @a PipelineElement%s that this vstream is comprised of.
     */
    const std::vector<std::shared_ptr<PipelineElement>> &get_pipeline() const;

protected:
    explicit OutputVStream(std::shared_ptr<OutputVStreamInternal> vstream);
    std::string get_pipeline_description() const;

    hailo_status start_vstream();
    hailo_status stop_vstream();
    hailo_status stop_and_clear();

    std::shared_ptr<OutputVStreamInternal> m_vstream;

    friend class VStreamsBuilderUtils;
    friend class VdmaConfigNetworkGroup;
};

/*! Contains the virtual streams creation functions */
class HAILORTAPI VStreamsBuilder
{
public:
    VStreamsBuilder() = delete;

    /**
     * Creates input virtual streams and output virtual streams.
     *
     * @param[in] net_group            Configured network group that owns the streams.
     * @param[in] quantized            Default quantized parameter for all virtual streams.
     *                                 For input vstreams indicates whether the data fed into the chip is already quantized.
     *                                 True means the data is already quantized.
     *                                 False means it's HailoRT's responsibility to quantize (scale) the data.
     *                                 For output vstreams indicates whether the data returned from the device should be quantized.
     *                                 True means that the data returned to the user is still quantized.
     *                                 False means it's HailoRT's responsibility to de-quantize (rescale) the data.
     * @param[in] format_type          The default format type for all virtual streams.
     * @param[in] network_name         Request to create vstreams of specific network inside the configured network group.
     *                                 If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of a pair of input vstreams and output vstreams.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> create_vstreams(
        ConfiguredNetworkGroup &net_group, bool quantized, hailo_format_type_t format_type,
        const std::string &network_name="");

    /**
     * Creates input virtual streams and output virtual streams.
     *
     * @param[in] net_group            Configured network group that owns the streams.
     * @param[in] vstreams_params      A ::hailo_vstream_params_t containing default params for all virtual streams.
     * @param[in] network_name         Request to create vstreams of specific network inside the configured network group.
     *                                 If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of a vector of input virtual streams.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::pair<std::vector<InputVStream>, std::vector<OutputVStream>>> create_vstreams(
        ConfiguredNetworkGroup &net_group, const hailo_vstream_params_t &vstreams_params,
        const std::string &network_name="");

    /**
     * Creates input virtual streams.
     *
     * @param[in] net_group            Configured network group that owns the streams.
     * @param[in] inputs_params        Map of input vstreams <name, params> to create input vstreams from.
     * @return Upon success, returns Expected of a vector of input virtual streams.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::vector<InputVStream>> create_input_vstreams(ConfiguredNetworkGroup &net_group,
        const std::map<std::string, hailo_vstream_params_t> &inputs_params);

    /**
     * Creates output virtual streams.
     *
     * @param[in] net_group            Configured network group that owns the streams.
     * @param[in] outputs_params       Map of output vstreams <name, params> to create output vstreams from.
     * @return Upon success, returns Expected of a vector of output virtual streams.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note If not creating all output vstreams together, one should make sure all vstreams from the same group are created together.
     *       See ConfiguredNetworkGroup::make_output_vstream_params_groups
     */
    static Expected<std::vector<OutputVStream>> create_output_vstreams(ConfiguredNetworkGroup &net_group,
        const std::map<std::string, hailo_vstream_params_t> &outputs_params);
};

} /* namespace hailort */

#endif /* _HAILO_VSTREAM_HPP_ */
