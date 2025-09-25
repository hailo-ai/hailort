/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include "stream_common/stream_internal.hpp"

#include "common/barrier.hpp"

#include "net_flow/pipeline/pipeline.hpp"
#include "net_flow/pipeline/filter_elements.hpp"
#include "net_flow/pipeline/queue_elements.hpp"
#include "net_flow/pipeline/edge_elements.hpp"
#include "net_flow/pipeline/multi_io_elements.hpp"
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
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, AccumulatorPtr pipeline_latency_accumulator,
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
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr core_op_activated_event,
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
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, AccumulatorPtr pipeline_latency_accumulator,
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
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator);
    OutputVStreamInternal(OutputVStreamInternal &&other) noexcept = default;
    OutputVStreamInternal &operator=(OutputVStreamInternal &&other) noexcept = default;
    virtual ~OutputVStreamInternal() = default;

    hailo_status clear();

    virtual hailo_status read(MemoryView buffer) = 0;
    virtual std::string get_pipeline_description() const override;

    virtual hailo_status set_nms_score_threshold(float32_t threshold) = 0;
    virtual hailo_status set_nms_iou_threshold(float32_t threshold) = 0;
    virtual hailo_status set_nms_max_proposals_per_class(uint32_t max_proposals_per_class) = 0;
    virtual hailo_status set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size) = 0;

protected:
    OutputVStreamInternal(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr core_op_activated_event, hailo_status &output_status);
    OutputVStreamInternal() = default;
};

class InputVStreamImpl : public InputVStreamInternal
{
public:
    static Expected<std::shared_ptr<InputVStreamImpl>> create(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos,
        const hailo_vstream_params_t &vstream_params, std::shared_ptr<PipelineElement> pipeline_entry,
        std::shared_ptr<SinkElement> pipeline_exit, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, EventPtr core_op_activated_event,
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
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr core_op_activated_event, hailo_status &output_status);

    bool m_is_multi_planar;
};

class OutputVStreamImpl : public OutputVStreamInternal
{
public:
    static Expected<std::shared_ptr<OutputVStreamImpl>> create(
        const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        EventPtr core_op_activated_event, AccumulatorPtr pipeline_latency_accumulator);
    OutputVStreamImpl(OutputVStreamImpl &&) noexcept = default;
    OutputVStreamImpl(const OutputVStreamImpl &) = delete;
    OutputVStreamImpl &operator=(OutputVStreamImpl &&) noexcept = default;
    OutputVStreamImpl &operator=(const OutputVStreamImpl &) = delete;
    virtual ~OutputVStreamImpl();

    virtual hailo_status read(MemoryView buffer) override;

    virtual hailo_status set_nms_score_threshold(float32_t threshold) override;
    virtual hailo_status set_nms_iou_threshold(float32_t threshold) override;
    virtual hailo_status set_nms_max_proposals_per_class(uint32_t max_proposals_per_class) override;
    virtual hailo_status set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size) override;

private:
    OutputVStreamImpl(const hailo_vstream_info_t &vstream_info, const std::vector<hailo_quant_info_t> &quant_infos, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<PipelineElement> pipeline_entry, std::vector<std::shared_ptr<PipelineElement>> &&pipeline,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, AccumulatorPtr pipeline_latency_accumulator,
        EventPtr core_op_activated_event, hailo_status &output_status);
};

#ifdef HAILO_SUPPORT_MULTI_PROCESS
class InputVStreamClient : public InputVStreamInternal
{
public:
    static Expected<std::shared_ptr<InputVStreamClient>> create(VStreamIdentifier &&identifier, const std::chrono::milliseconds &timeout);
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
        hailo_vstream_info_t &&info, const std::chrono::milliseconds &timeout);
    hailo_status create_client();

    std::unique_ptr<HailoRtRpcClient> m_client;
    VStreamIdentifier m_identifier;
    hailo_format_t m_user_buffer_format;
    hailo_vstream_info_t m_info;
    const std::chrono::milliseconds m_timeout;
};

class OutputVStreamClient : public OutputVStreamInternal
{
public:
    static Expected<std::shared_ptr<OutputVStreamClient>> create(const VStreamIdentifier &&identifier, const std::chrono::milliseconds &timeout);
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
    virtual hailo_status set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size) override;

private:
    OutputVStreamClient(std::unique_ptr<HailoRtRpcClient> client, const VStreamIdentifier &&identifier, hailo_format_t &&user_buffer_format,
        hailo_vstream_info_t &&info, const std::chrono::milliseconds &timeout);

    hailo_status create_client();

    std::unique_ptr<HailoRtRpcClient> m_client;
    VStreamIdentifier m_identifier;
    hailo_format_t m_user_buffer_format;
    hailo_vstream_info_t m_info;
    const std::chrono::milliseconds m_timeout;
};
#endif // HAILO_SUPPORT_MULTI_PROCESS

} /* namespace hailort */

#endif /* _HAILO_VSTREAM_INTERNAL_HPP_ */
