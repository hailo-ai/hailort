/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vstream.hpp
 * @brief Virtual Stream.
 *        Hence, the hierarchy is as follows:
 *        ------------------------------------------------------------------------------------------------
 *        |                                         BaseVStream                                          |  (Internal "interface")
 *        |                   ___________________________|___________________________                    |
 *        |                  /                                                       \                   |
 *        |            InputVStreamInternal                                OutputVStreamInternal         |  (Base classes)
 *        |               /                  \                               /           \               |
 *        |  InputVStreamImpl           InputVStreamClient    OuputVStreamImpl     OutputVStreamClient   |  (Actual implementations)
 *        ------------------------------------------------------------------------------------------------
 *  -- InputVStream                                 (External 'interface')
 *     |
 *     |__ std::share_ptr<InputVStreamInternal>
 *
 *  -- OutputVStream                                (External 'interface')
 *     |
 *     |__ std::share_ptr<OutputVStreamInternal>
 **/

#ifndef _HAILO_VSTREAM_INTERNAL_HPP_
#define _HAILO_VSTREAM_INTERNAL_HPP_

#include "hailo/expected.hpp"
#include "hailo/transform.hpp"
#include "hailo/stream.hpp"

#include "hef/hef_internal.hpp"
#include "net_flow/pipeline/pipeline.hpp"
#include "net_flow/ops/yolov5_post_process.hpp"
#include "network_group/network_group_internal.hpp"

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#include "service/hailort_rpc_client.hpp"
#endif // HAILO_SUPPORT_MULTI_PROCESS


namespace hailort
{

/*! Virtual stream base class */
class BaseVStream
{
public:
    BaseVStream(BaseVStream &&other) noexcept;
    BaseVStream& operator=(BaseVStream &&other) noexcept;
    virtual ~BaseVStream() = default;

    virtual size_t get_frame_size() const;
    virtual const hailo_vstream_info_t &get_info() const;
    virtual const std::vector<hailo_quant_info_t> &get_quant_infos() const;
    virtual const hailo_format_t &get_user_buffer_format() const;
    virtual std::string name() const;
    virtual std::string network_name() const;
    virtual const std::map<std::string, AccumulatorPtr> &get_fps_accumulators() const;
    virtual const std::map<std::string, AccumulatorPtr> &get_latency_accumulators() const;
    virtual const std::map<std::string, std::vector<AccumulatorPtr>> &get_queue_size_accumulators() const;
    virtual AccumulatorPtr get_pipeline_latency_accumulator() const;
    virtual const std::vector<std::shared_ptr<PipelineElement>> &get_pipeline() const;

    virtual hailo_status abort();
    virtual hailo_status resume();
    virtual hailo_status start_vstream();
    virtual hailo_status stop_vstream();
    virtual hailo_status stop_and_clear();

    virtual bool is_aborted() { return m_is_aborted; };

    virtual hailo_status before_fork();
    virtual hailo_status after_fork_in_parent();
    virtual hailo_status after_fork_in_child();

protected:
    BaseVStream(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr &&core_op_activated_event, hailo_status &output_status);
    BaseVStream() = default;

    virtual std::string get_pipeline_description() const = 0;

    hailo_vstream_info_t m_vstream_info;
    std::vector<hailo_quant_info_t> m_quant_infos;
    hailo_vstream_params_t m_vstream_params;
    bool m_measure_pipeline_latency;
    std::shared_ptr<PipelineElement> m_entry_element;
    std::vector<std::shared_ptr<PipelineElement>> m_pipeline;
    volatile bool m_is_activated;
    volatile bool m_is_aborted;
    std::shared_ptr<std::atomic<hailo_status>> m_pipeline_status;
    EventPtr m_shutdown_event;
    EventPtr m_core_op_activated_event;
    std::map<std::string, AccumulatorPtr> m_fps_accumulators;
    std::map<std::string, AccumulatorPtr> m_latency_accumulators;
    std::map<std::string, std::vector<AccumulatorPtr>> m_queue_size_accumulators;
    AccumulatorPtr m_pipeline_latency_accumulator;
};

/*! Input virtual stream, used to stream data to device */
class InputVStreamInternal : public BaseVStream
{
public:
    static Expected<std::shared_ptr<InputVStreamInternal>> create(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos,
        const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
        std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, EventPtr core_op_activated_event,
        AccumulatorPtr pipeline_latency_accumulator);
    InputVStreamInternal(InputVStreamInternal &&other) noexcept = default;
    InputVStreamInternal &operator=(InputVStreamInternal &&other) noexcept = default;
    virtual ~InputVStreamInternal() = default;

    virtual hailo_status write(const MemoryView &buffer) = 0;
    virtual hailo_status write(const hailo_pix_buffer_t &buffer) = 0;
    virtual hailo_status flush() = 0;
    virtual bool is_multi_planar() const = 0;

    virtual std::string get_pipeline_description() const override;

protected:
    InputVStreamInternal(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr &&core_op_activated_event, hailo_status &output_status);
    InputVStreamInternal() = default;
};

/*! Output virtual stream, used to read data from device */
class OutputVStreamInternal : public BaseVStream
{
public:
    static Expected<std::shared_ptr<OutputVStreamInternal>> create(
        const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
        EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator);
    OutputVStreamInternal(OutputVStreamInternal &&other) noexcept = default;
    OutputVStreamInternal &operator=(OutputVStreamInternal &&other) noexcept = default;
    virtual ~OutputVStreamInternal() = default;


    virtual hailo_status read(MemoryView buffer) = 0;
    virtual std::string get_pipeline_description() const override;

    virtual hailo_status set_nms_score_threshold(float32_t threshold) = 0;
    virtual hailo_status set_nms_iou_threshold(float32_t threshold) = 0;
    virtual hailo_status set_nms_max_proposals_per_class(uint32_t max_proposals_per_class) = 0;

protected:
    OutputVStreamInternal(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr core_op_activated_event, hailo_status &output_status);
    OutputVStreamInternal() = default;
};

class InputVStreamImpl : public InputVStreamInternal
{
public:
    static Expected<std::shared_ptr<InputVStreamImpl>> create(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos,
        const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
        std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, EventPtr core_op_activated_event,
        AccumulatorPtr pipeline_latency_accumulator);
    InputVStreamImpl(InputVStreamImpl &&) noexcept = default;
    InputVStreamImpl(const InputVStreamImpl &) = delete;
    InputVStreamImpl &operator=(InputVStreamImpl &&) noexcept = default;
    InputVStreamImpl &operator=(const InputVStreamImpl &) = delete;
    virtual ~InputVStreamImpl();

    virtual hailo_status write(const MemoryView &buffer) override;
    virtual hailo_status write(const hailo_pix_buffer_t &buffer) override;
    virtual hailo_status flush() override;
    virtual bool is_multi_planar() const override;

private:
    InputVStreamImpl(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr core_op_activated_event, hailo_status &output_status);

    bool m_is_multi_planar;
};

class OutputVStreamImpl : public OutputVStreamInternal
{
public:
    static Expected<std::shared_ptr<OutputVStreamImpl>> create(
        const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event,
        EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator);
    OutputVStreamImpl(OutputVStreamImpl &&) noexcept = default;
    OutputVStreamImpl(const OutputVStreamImpl &) = delete;
    OutputVStreamImpl &operator=(OutputVStreamImpl &&) noexcept = default;
    OutputVStreamImpl &operator=(const OutputVStreamImpl &) = delete;
    virtual ~OutputVStreamImpl();

    virtual hailo_status read(MemoryView buffer) override;

    void set_on_vstream_cant_read_callback(std::function<void()> callback)
    {
        m_cant_read_callback = callback;
    }

    void set_on_vstream_can_read_callback(std::function<void()> callback)
    {
        m_can_read_callback = callback;
    }

    virtual hailo_status set_nms_score_threshold(float32_t threshold) override;
    virtual hailo_status set_nms_iou_threshold(float32_t threshold) override;
    virtual hailo_status set_nms_max_proposals_per_class(uint32_t max_proposals_per_class) override;

private:
    OutputVStreamImpl(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr shutdown_event, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr core_op_activated_event, hailo_status &output_status);

    Expected<std::shared_ptr<net_flow::NmsOpMetadata>> get_nms_metadata_from_pipeline() const;

    std::function<void()> m_cant_read_callback;
    std::function<void()> m_can_read_callback;
};

#ifdef HAILO_SUPPORT_MULTI_PROCESS
class InputVStreamClient : public InputVStreamInternal
{
public:
    static Expected<std::shared_ptr<InputVStreamClient>> create(VStreamIdentifier &&identifier);
    InputVStreamClient(InputVStreamClient &&) noexcept = default;
    InputVStreamClient(const InputVStreamClient &) = delete;
    InputVStreamClient &operator=(InputVStreamClient &&) noexcept = default;
    InputVStreamClient &operator=(const InputVStreamClient &) = delete;
    virtual ~InputVStreamClient();

    virtual hailo_status write(const MemoryView &buffer) override;
    virtual hailo_status write(const hailo_pix_buffer_t &buffer) override;
    virtual hailo_status flush() override;
    virtual bool is_multi_planar() const override;

    virtual hailo_status abort() override;
    virtual hailo_status resume() override;
    virtual size_t get_frame_size() const override;
    virtual const hailo_vstream_info_t &get_info() const override;
    virtual const hailo_format_t &get_user_buffer_format() const override;
    virtual std::string name() const override;
    virtual std::string network_name() const override;
    virtual const std::map<std::string, AccumulatorPtr> &get_fps_accumulators() const override;
    virtual const std::map<std::string, AccumulatorPtr> &get_latency_accumulators() const override;
    virtual const std::map<std::string, std::vector<AccumulatorPtr>> &get_queue_size_accumulators() const override;
    virtual AccumulatorPtr get_pipeline_latency_accumulator() const override;
    virtual const std::vector<std::shared_ptr<PipelineElement>> &get_pipeline() const override;
    virtual hailo_status before_fork() override;
    virtual hailo_status after_fork_in_parent() override;
    virtual hailo_status after_fork_in_child() override;
    virtual hailo_status stop_and_clear() override;
    virtual hailo_status start_vstream() override;
    virtual bool is_aborted() override;

private:
    InputVStreamClient(std::unique_ptr<HailoRtRpcClient> client, VStreamIdentifier &&identifier, hailo_format_t &&user_buffer_format,
        hailo_vstream_info_t &&info);
    hailo_status create_client();

    std::unique_ptr<HailoRtRpcClient> m_client;
    VStreamIdentifier m_identifier;
    hailo_format_t m_user_buffer_format;
    hailo_vstream_info_t m_info;
};

class OutputVStreamClient : public OutputVStreamInternal
{
public:
    static Expected<std::shared_ptr<OutputVStreamClient>> create(const VStreamIdentifier &&identifier);
    OutputVStreamClient(OutputVStreamClient &&) noexcept = default;
    OutputVStreamClient(const OutputVStreamClient &) = delete;
    OutputVStreamClient &operator=(OutputVStreamClient &&) noexcept = default;
    OutputVStreamClient &operator=(const OutputVStreamClient &) = delete;
    virtual ~OutputVStreamClient();

    virtual hailo_status read(MemoryView buffer);

    virtual hailo_status abort() override;
    virtual hailo_status resume() override;
    virtual size_t get_frame_size() const override;
    virtual const hailo_vstream_info_t &get_info() const override;
    virtual const hailo_format_t &get_user_buffer_format() const override;
    virtual std::string name() const override;
    virtual std::string network_name() const override;
    virtual const std::map<std::string, AccumulatorPtr> &get_fps_accumulators() const override;
    virtual const std::map<std::string, AccumulatorPtr> &get_latency_accumulators() const override;
    virtual const std::map<std::string, std::vector<AccumulatorPtr>> &get_queue_size_accumulators() const override;
    virtual AccumulatorPtr get_pipeline_latency_accumulator() const override;
    virtual const std::vector<std::shared_ptr<PipelineElement>> &get_pipeline() const override;
    virtual hailo_status before_fork() override;
    virtual hailo_status after_fork_in_parent() override;
    virtual hailo_status after_fork_in_child() override;
    virtual hailo_status stop_and_clear() override;
    virtual hailo_status start_vstream() override;
    virtual bool is_aborted() override;

    virtual hailo_status set_nms_score_threshold(float32_t threshold) override;
    virtual hailo_status set_nms_iou_threshold(float32_t threshold) override;
    virtual hailo_status set_nms_max_proposals_per_class(uint32_t max_proposals_per_class) override;

private:
    OutputVStreamClient(std::unique_ptr<HailoRtRpcClient> client, const VStreamIdentifier &&identifier, hailo_format_t &&user_buffer_format,
        hailo_vstream_info_t &&info);

    hailo_status create_client();

    std::unique_ptr<HailoRtRpcClient> m_client;
    VStreamIdentifier m_identifier;
    hailo_format_t m_user_buffer_format;
    hailo_vstream_info_t m_info;
};
#endif // HAILO_SUPPORT_MULTI_PROCESS

class PreInferElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<PreInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
        const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH, bool is_dma_able = false);
    static Expected<std::shared_ptr<PreInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos, const std::string &name,
        const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH, bool is_dma_able = false);
    static Expected<std::shared_ptr<PreInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_infos,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PUSH, bool is_dma_able = false);
    PreInferElement(std::unique_ptr<InputTransformContext> &&transform_context, BufferPoolPtr buffer_pool,
        const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction);
    virtual ~PreInferElement() = default;

    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::unique_ptr<InputTransformContext> m_transform_context;
};

class RemoveOverlappingBboxesElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> create(
        const net_flow::NmsPostProcessConfig nms_config, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
        size_t buffer_pool_size, PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> create(const net_flow::NmsPostProcessConfig nms_config,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL, 
        bool is_last_copy_element = false);
    RemoveOverlappingBboxesElement(const net_flow::NmsPostProcessConfig &&nms_config, const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, BufferPoolPtr buffer_pool, std::chrono::milliseconds timeout,
        PipelineDirection pipeline_direction);
    virtual ~RemoveOverlappingBboxesElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    net_flow::NmsPostProcessConfig m_nms_config;
};

class PostInferElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<PostInferElement>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
        const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
        size_t buffer_pool_size, PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<PostInferElement>> create(const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format, const std::vector<hailo_quant_info_t> &dst_quant_info, const hailo_nms_info_t &nms_info,
        const std::string &name, const hailo_vstream_params_t &vstream_params, std::shared_ptr<std::atomic<hailo_status>> pipeline_status, EventPtr shutdown_event,
        PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<PostInferElement>> create(const hailo_3d_image_shape_t &src_image_shape,
        const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const hailo_format_t &dst_format,
        const std::vector<hailo_quant_info_t> &dst_quant_infos, const hailo_nms_info_t &nms_info, const std::string &name,
        const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    PostInferElement(std::unique_ptr<OutputTransformContext> &&transform_context, const std::string &name,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, BufferPoolPtr buffer_pool,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction = PipelineDirection::PULL);
    virtual ~PostInferElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::unique_ptr<OutputTransformContext> m_transform_context;
};

class ConvertNmsToDetectionsElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> create(const hailo_nms_info_t &nms_info, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
        size_t buffer_pool_size, PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> create(
        const hailo_nms_info_t &nms_info, const std::string &name, const ElementBuildParams &build_params,
        PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    ConvertNmsToDetectionsElement(const hailo_nms_info_t &&nms_info, const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, BufferPoolPtr buffer_pool, std::chrono::milliseconds timeout,
        PipelineDirection pipeline_direction);
    virtual ~ConvertNmsToDetectionsElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    hailo_nms_info_t m_nms_info;
};

class FillNmsFormatElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<FillNmsFormatElement>> create(const hailo_nms_info_t nms_info,
        const hailo_format_t &dst_format, const net_flow::NmsPostProcessConfig nms_config, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
        size_t buffer_pool_size, PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<FillNmsFormatElement>> create(const hailo_nms_info_t nms_info,
        const hailo_format_t &dst_format, const net_flow::NmsPostProcessConfig nms_config, const std::string &name,
        const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    FillNmsFormatElement(const net_flow::NmsPostProcessConfig &&nms_config, const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, BufferPoolPtr buffer_pool, std::chrono::milliseconds timeout,
        PipelineDirection pipeline_direction);
    virtual ~FillNmsFormatElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    net_flow::NmsPostProcessConfig m_nms_config;
};

class ArgmaxPostProcessElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<ArgmaxPostProcessElement>> create(std::shared_ptr<net_flow::Op> argmax_op,
        const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, size_t buffer_pool_size, std::chrono::milliseconds timeout,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        bool is_last_copy_element = false);
    static Expected<std::shared_ptr<ArgmaxPostProcessElement>> create(std::shared_ptr<net_flow::Op> argmax_op,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        bool is_last_copy_element = false);
    ArgmaxPostProcessElement(std::shared_ptr<net_flow::Op> argmax_op, const std::string &name,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        std::chrono::milliseconds timeout, BufferPoolPtr buffer_pool, PipelineDirection pipeline_direction = PipelineDirection::PULL);
    virtual ~ArgmaxPostProcessElement() = default;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;
    
protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::shared_ptr<net_flow::Op> m_argmax_op;
};

class SoftmaxPostProcessElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<SoftmaxPostProcessElement>> create(std::shared_ptr<net_flow::Op> softmax_op,
        const std::string &name, hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, size_t buffer_pool_size, std::chrono::milliseconds timeout,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
        PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<SoftmaxPostProcessElement>> create(std::shared_ptr<net_flow::Op> softmax_op,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        bool is_last_copy_element = false);
    SoftmaxPostProcessElement(std::shared_ptr<net_flow::Op> softmax_op, const std::string &name,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        std::chrono::milliseconds timeout, BufferPoolPtr buffer_pool, PipelineDirection pipeline_direction = PipelineDirection::PULL);
    virtual ~SoftmaxPostProcessElement() = default;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual PipelinePad &next_pad() override;
    virtual std::string description() const override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;

private:
    std::shared_ptr<net_flow::Op> m_softmax_op;
};

class NmsPostProcessMuxElement : public BaseMuxElement
{
public:
    static Expected<std::shared_ptr<NmsPostProcessMuxElement>> create(std::shared_ptr<net_flow::Op> nms_op,
        const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size,
        hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<NmsPostProcessMuxElement>> create(std::shared_ptr<net_flow::Op> nms_op,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<NmsPostProcessMuxElement>> create(std::shared_ptr<net_flow::Op> nms_op,
        const std::string &name, const hailo_vstream_params_t &vstream_params,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    NmsPostProcessMuxElement(std::shared_ptr<net_flow::Op> nms_op, BufferPoolPtr &&pool, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction);

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;
    void add_sink_name(const std::string &name) // TODO: remove this (HRT-8875)
    {
        m_sinks_names.push_back(name);
    }

    std::shared_ptr<net_flow::Op> get_op() { return m_nms_op; }

protected:
    virtual Expected<PipelineBuffer> action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional) override;

private:
    std::shared_ptr<net_flow::Op> m_nms_op;
    std::vector<std::string> m_sinks_names; // TODO: remove this (HRT-8875)
};

class NmsMuxElement : public BaseMuxElement
{
public:
    static Expected<std::shared_ptr<NmsMuxElement>> create(const std::vector<hailo_nms_info_t> &nms_infos,
        const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<NmsMuxElement>> create(const std::vector<hailo_nms_info_t> &nms_infos, const std::string &name,
        const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL, bool is_last_copy_element = false);
    static Expected<std::shared_ptr<NmsMuxElement>> create(const std::vector<hailo_nms_info_t> &nms_infos,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        bool is_last_copy_element = false);
    NmsMuxElement(const std::vector<hailo_nms_info_t> &nms_infos, const hailo_nms_info_t &fused_nms_info, BufferPoolPtr &&pool, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL);
    const hailo_nms_info_t &get_fused_nms_info() const;

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

protected:
    virtual Expected<PipelineBuffer> action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional) override;

private:
    std::vector<hailo_nms_info_t> m_nms_infos;
    hailo_nms_info_t m_fused_nms_info;
};

class TransformDemuxElement : public BaseDemuxElement
{
public:
    static Expected<std::shared_ptr<TransformDemuxElement>> create(std::shared_ptr<OutputDemuxer> demuxer,
        const std::string &name, std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL);
    static Expected<std::shared_ptr<TransformDemuxElement>> create(std::shared_ptr<OutputDemuxer> demuxer,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL);
    TransformDemuxElement(std::shared_ptr<OutputDemuxer> demuxer, std::vector<BufferPoolPtr> &&pools, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineDirection pipeline_direction);

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

protected:
    virtual Expected<std::vector<PipelineBuffer>> action(PipelineBuffer &&input) override;

private:
    std::shared_ptr<OutputDemuxer> m_demuxer;
};

class PixBufferElement : public BaseDemuxElement
{
public:
    static Expected<std::shared_ptr<PixBufferElement>> create(const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, size_t sources_count, hailo_format_order_t order);

    PixBufferElement(const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, size_t sources_count, hailo_format_order_t order);

protected:
    virtual Expected<std::vector<PipelineBuffer>> action(PipelineBuffer &&input);
    hailo_format_order_t m_order;
};

class HwReadElement : public SourceElement
{
public:
    static Expected<std::shared_ptr<HwReadElement>> create(std::shared_ptr<OutputStream> stream, const std::string &name, std::chrono::milliseconds timeout,
        size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags, hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction = PipelineDirection::PULL);
    HwReadElement(std::shared_ptr<OutputStream> stream, BufferPoolPtr buffer_pool, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineDirection pipeline_direction);
    virtual ~HwReadElement() = default;

    virtual std::vector<AccumulatorPtr> get_queue_size_accumulators() override;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_clear() override;
    virtual hailo_status execute_flush() override;
    virtual hailo_status execute_abort() override;
    virtual hailo_status execute_clear_abort() override;
    virtual hailo_status execute_wait_for_finish() override;
    uint32_t get_invalid_frames_count();
    virtual std::string description() const override;

private:
    std::shared_ptr<OutputStream> m_stream;
    BufferPoolPtr m_pool;
    std::chrono::milliseconds m_timeout;
    EventPtr m_shutdown_event;
    WaitOrShutdown m_activation_wait_or_shutdown;
};

class HwWriteElement : public SinkElement
{
public:
    static Expected<std::shared_ptr<HwWriteElement>> create(std::shared_ptr<InputStream> stream, const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH);
    HwWriteElement(std::shared_ptr<InputStream> stream, const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr got_flush_event, PipelineDirection pipeline_direction);
    virtual ~HwWriteElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_clear() override;
    virtual hailo_status execute_flush() override;
    virtual hailo_status execute_abort() override;
    virtual hailo_status execute_clear_abort() override;
    virtual hailo_status execute_wait_for_finish() override;
    virtual std::string description() const override;

private:
    std::shared_ptr<InputStream> m_stream;
    EventPtr m_got_flush_event;
};

class LastAsyncElement : public SinkElement
{
public:
    static Expected<std::shared_ptr<LastAsyncElement>> create(const std::string &name,
        hailo_pipeline_elem_stats_flags_t elem_flags, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH);
    static Expected<std::shared_ptr<LastAsyncElement>> create(const std::string &name,
        const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PUSH);
    LastAsyncElement(const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineDirection pipeline_direction);
    virtual ~LastAsyncElement() = default;

    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    virtual std::string description() const override;
    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_wait_for_finish() override;

    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> are_buffer_pools_full() override;
    virtual hailo_status fill_buffer_pools(bool is_dma_able) override;
};

// Note: This element does infer - it sends writes to HW and reads the outputs
class AsyncHwElement : public PipelineElement
{
public:
    static Expected<std::shared_ptr<AsyncHwElement>> create(const std::vector<std::shared_ptr<InputStream>> &input_streams,
        const std::vector<std::shared_ptr<OutputStream>> &output_streams, std::chrono::milliseconds timeout, size_t buffer_pool_size, hailo_pipeline_elem_stats_flags_t elem_flags,
        hailo_vstream_stats_flags_t vstream_flags, EventPtr shutdown_event, const std::string &name, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PUSH, bool is_last_copy_element = false);
    AsyncHwElement(const std::vector<std::shared_ptr<InputStream>> &input_streams, const std::vector<std::shared_ptr<OutputStream>> &output_streams,
        std::chrono::milliseconds timeout, std::unordered_map<std::string, BufferPoolPtr> &&output_streams_pools, const std::string &name,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction);
    virtual ~AsyncHwElement() = default;

    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

    virtual hailo_status enqueue_execution_buffer(MemoryView mem_view, const TransferDoneCallbackAsyncInfer &exec_done, const std::string &source_name) override;
    virtual Expected<bool> are_buffer_pools_full() override;
    virtual hailo_status fill_buffer_pools(bool is_dma_able) override;

    Expected<uint32_t> get_source_index_from_output_stream_name(const std::string &output_stream_name);
    Expected<uint32_t> get_sink_index_from_input_stream_name(const std::string &input_stream_name);

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;

private:
    void read_async_on_all_streams();
    void handle_error_in_hw_async_elem(hailo_status error_status);
    bool has_all_sinks_arrived();

    std::chrono::milliseconds m_timeout;
    std::unordered_map<std::string, BufferPoolPtr> m_output_streams_pools;
    std::unordered_map<std::string, std::shared_ptr<InputStream>> m_sink_name_to_input;
    std::unordered_map<std::string, std::shared_ptr<OutputStream>> m_source_name_to_output;
    std::unordered_map<std::string, bool> m_sink_has_arrived;
    std::unordered_map<std::string, PipelineBuffer> m_input_buffers;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::unordered_map<std::string, uint32_t> m_source_name_to_index;
    std::unordered_map<std::string, uint32_t> m_sink_name_to_index;
};

class CopyBufferElement : public FilterElement
{
public:
    static Expected<std::shared_ptr<CopyBufferElement>> create(const std::string &name, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction = PipelineDirection::PULL);
    CopyBufferElement(const std::string &name, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::chrono::milliseconds timeout, PipelineDirection pipeline_direction);
    virtual ~CopyBufferElement() = default;
    virtual PipelinePad &next_pad() override;

protected:
    virtual Expected<PipelineBuffer> action(PipelineBuffer &&input, PipelineBuffer &&optional) override;
};

class VStreamsBuilderUtils
{
public:
    static Expected<std::vector<InputVStream>> create_inputs(std::vector<std::shared_ptr<InputStream>> input_streams, const hailo_vstream_info_t &input_vstream_infos,
        const hailo_vstream_params_t &vstreams_params);
    static Expected<std::vector<OutputVStream>> create_outputs(std::shared_ptr<OutputStream> output_stream,
        NameToVStreamParamsMap &vstreams_params_map, const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);
    static InputVStream create_input(std::shared_ptr<InputVStreamInternal> input_vstream);
    static OutputVStream create_output(std::shared_ptr<OutputVStreamInternal> output_vstream);
    static Expected<std::vector<OutputVStream>> create_output_nms(OutputStreamPtrVector &output_streams,
        hailo_vstream_params_t vstreams_params,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);
    static Expected<std::vector<OutputVStream>> create_output_vstreams_from_streams(const OutputStreamWithParamsVector &all_output_streams,
        OutputStreamPtrVector &output_streams, const hailo_vstream_params_t &vstream_params,
        const std::unordered_map<std::string, net_flow::PostProcessOpMetadataPtr> &post_process_ops,
        const std::unordered_map<std::string, std::string> &op_inputs_to_op_name, const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos_map);
    static Expected<std::vector<OutputVStream>> create_output_post_process_nms(OutputStreamPtrVector &output_streams,
        hailo_vstream_params_t vstreams_params,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos,
        const std::shared_ptr<hailort::net_flow::Op> &nms_op);
    static Expected<std::shared_ptr<HwReadElement>> add_hw_read_element(std::shared_ptr<OutputStream> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, EventPtr &shutdown_event, size_t buffer_pool_size,
        const hailo_pipeline_elem_stats_flags_t &hw_read_element_stats_flags, const hailo_vstream_stats_flags_t &hw_read_stream_stats_flags);

    static Expected<std::shared_ptr<PullQueueElement>> add_pull_queue_element(std::shared_ptr<OutputStream> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, EventPtr &shutdown_event, const hailo_vstream_params_t &vstream_params);

    // Move all post-processes related elements to a dedicated model - HRT-11512
    static Expected<std::shared_ptr<ArgmaxPostProcessElement>> add_argmax_element(std::shared_ptr<OutputStream> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &argmax_op,
        size_t buffer_pool_size, std::chrono::milliseconds timeout, const hailo_vstream_stats_flags_t &vstream_flags,
        EventPtr &shutdown_event);

    static Expected<std::shared_ptr<SoftmaxPostProcessElement>> add_softmax_element(std::shared_ptr<OutputStream> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &softmax_op,
        size_t buffer_pool_size, std::chrono::milliseconds timeout, const hailo_vstream_stats_flags_t &vstream_flags,
        EventPtr &shutdown_event);

    static Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> add_nms_to_detections_convert_element(std::shared_ptr<OutputStream> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements, const std::string &element_name,
        hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata, size_t buffer_pool_size, std::chrono::milliseconds timeout,
        const hailo_vstream_stats_flags_t &vstream_flags, EventPtr &shutdown_event);

    static Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> add_remove_overlapping_bboxes_element(std::shared_ptr<OutputStream> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata,
        size_t buffer_pool_size, std::chrono::milliseconds timeout, const hailo_vstream_stats_flags_t &vstream_flags, EventPtr &shutdown_event);

    static Expected<std::shared_ptr<FillNmsFormatElement>> add_fill_nms_format_element(std::shared_ptr<OutputStream> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, hailo_vstream_params_t &vstream_params, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata,
        size_t buffer_pool_size, std::chrono::milliseconds timeout, const hailo_vstream_stats_flags_t &vstream_flags, EventPtr &shutdown_event);

    static Expected<std::shared_ptr<UserBufferQueueElement>> add_user_buffer_queue_element(std::shared_ptr<OutputStream> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, EventPtr &shutdown_event, const hailo_vstream_params_t &vstream_params);

    static Expected<std::shared_ptr<PostInferElement>> add_post_infer_element(std::shared_ptr<OutputStream> &output_stream,
        std::shared_ptr<std::atomic<hailo_status>> &pipeline_status, std::vector<std::shared_ptr<PipelineElement>> &elements,
        const std::string &element_name, const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event);

    static hailo_status add_demux(std::shared_ptr<OutputStream> output_stream, NameToVStreamParamsMap &vstreams_params_map,
        std::vector<std::shared_ptr<PipelineElement>> &&elements, std::vector<OutputVStream> &vstreams,
        std::shared_ptr<HwReadElement> hw_read_elem, EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);

    static hailo_status handle_pix_buffer_splitter_flow(std::vector<std::shared_ptr<InputStream>> streams,
        const hailo_vstream_info_t &vstream_info, std::vector<std::shared_ptr<PipelineElement>> &&base_elements,
        std::vector<InputVStream> &vstreams, const hailo_vstream_params_t &vstream_params, EventPtr shutdown_event,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, EventPtr &core_op_activated_event,
        AccumulatorPtr accumaltor);

    static hailo_status add_nms_fuse(OutputStreamPtrVector &output_streams, hailo_vstream_params_t &vstreams_params,
        std::vector<std::shared_ptr<PipelineElement>> &elements, std::vector<OutputVStream> &vstreams,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos);

    static hailo_status add_nms_post_process(OutputStreamPtrVector &output_streams, hailo_vstream_params_t &vstreams_params,
        std::vector<std::shared_ptr<PipelineElement>> &elements, std::vector<OutputVStream> &vstreams,
        EventPtr shutdown_event, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        const std::map<std::string, hailo_vstream_info_t> &output_vstream_infos,
        const std::shared_ptr<hailort::net_flow::Op> &nms_op);

    static Expected<AccumulatorPtr> create_pipeline_latency_accumulator(const hailo_vstream_params_t &vstreams_params);

private:
    static Expected<std::vector<OutputVStream>> create_output_post_process_argmax(std::shared_ptr<OutputStream> output_stream,
        const NameToVStreamParamsMap &vstreams_params_map, const hailo_vstream_info_t &output_vstream_info,
        const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata);
    static Expected<std::vector<OutputVStream>> create_output_post_process_softmax(std::shared_ptr<OutputStream> output_stream,
        const NameToVStreamParamsMap &vstreams_params_map, const hailo_vstream_info_t &output_vstream_info,
        const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata);
    static Expected<std::vector<OutputVStream>> create_output_post_process_iou(std::shared_ptr<OutputStream> output_stream,
        hailo_vstream_params_t vstream_params, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata);
};

} /* namespace hailort */

#endif /* _HAILO_VSTREAM_INTERNAL_HPP_ */
