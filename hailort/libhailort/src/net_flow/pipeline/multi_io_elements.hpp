/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file multi_io_elements.hpp
 * @brief all multiple inputs/outputs elements in the pipeline.
 **/

#ifndef _HAILO_MULTI_IO_ELEMENTS_HPP_
#define _HAILO_MULTI_IO_ELEMENTS_HPP_

#include "net_flow/ops_metadata/yolov5_seg_op_metadata.hpp"

namespace hailort
{

class BaseMuxElement : public PipelineElementInternal
{
public:
    virtual ~BaseMuxElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async_multi(std::vector<PipelineBuffer> &&buffers) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

protected:
    BaseMuxElement(size_t sink_count, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual hailo_status execute_terminate(hailo_status error_status) override;
    virtual Expected<PipelineBuffer> action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional) = 0;
    virtual std::vector<PipelinePad*> execution_pads() override;

    PipelinePad &next_pad_downstream()
    {
        return *m_sources[0].next();
    }

    std::chrono::milliseconds m_timeout;

private:
    std::unordered_map<std::string, uint32_t> m_sink_name_to_index;
    std::vector<PipelinePad*> m_next_pads;
};

class NmsPostProcessMuxElement : public BaseMuxElement
{
public:
    static Expected<std::shared_ptr<NmsPostProcessMuxElement>> create(std::shared_ptr<net_flow::Op> nms_op,
        const std::string &name, std::chrono::milliseconds timeout,
        hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<NmsPostProcessMuxElement>> create(std::shared_ptr<net_flow::Op> nms_op,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<NmsPostProcessMuxElement>> create(std::shared_ptr<net_flow::Op> nms_op,
        const std::string &name, const hailo_vstream_params_t &vstream_params,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    NmsPostProcessMuxElement(std::shared_ptr<net_flow::Op> nms_op, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
        std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual std::string description() const override;

    void add_sink_name(const std::string &name, uint32_t index)
    {
        m_sinks_names[index] = name;
    }

    std::shared_ptr<net_flow::Op> get_op() { return m_nms_op; }

    virtual hailo_status set_nms_score_threshold(float32_t threshold)
    {
        auto nms_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(get_op()->metadata());
        assert(nullptr != nms_metadata);
        nms_metadata->nms_config().nms_score_th = threshold;

        return HAILO_SUCCESS;
    }

    virtual hailo_status set_nms_iou_threshold(float32_t threshold)
    {
        auto nms_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(get_op()->metadata());
        assert(nullptr != nms_metadata);
        nms_metadata->nms_config().nms_iou_th = threshold;

        return HAILO_SUCCESS;
    }

    virtual hailo_status set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
    {
        auto nms_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(get_op()->metadata());
        assert(nullptr != nms_metadata);
        nms_metadata->nms_config().max_proposals_per_class = max_proposals_per_class;

        return HAILO_SUCCESS;
    }

    virtual hailo_status set_nms_max_proposals_total(uint32_t max_proposals_total)
    {
        auto nms_metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(get_op()->metadata());
        assert(nullptr != nms_metadata);
        nms_metadata->nms_config().max_proposals_total = max_proposals_total;

        return HAILO_SUCCESS;
    }

    virtual hailo_status set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size)
    {
        auto yolov5seg_metadata = std::dynamic_pointer_cast<net_flow::Yolov5SegOpMetadata>(get_op()->metadata());
        assert(nullptr != yolov5seg_metadata);
        yolov5seg_metadata->yolov5seg_config().max_accumulated_mask_size = max_accumulated_mask_size;

        return HAILO_SUCCESS;
    }

protected:
    virtual Expected<PipelineBuffer> action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional) override;

private:
    std::shared_ptr<net_flow::Op> m_nms_op;
    std::vector<std::string> m_sinks_names;
};

class NmsMuxElement : public BaseMuxElement
{
public:
    static Expected<std::shared_ptr<NmsMuxElement>> create(const std::vector<hailo_nms_info_t> &nms_infos,
        const std::string &name, std::chrono::milliseconds timeout, hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL, std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<NmsMuxElement>> create(const std::vector<hailo_nms_info_t> &nms_infos, const std::string &name,
        const hailo_vstream_params_t &vstream_params, std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<NmsMuxElement>> create(const std::vector<hailo_nms_info_t> &nms_infos,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    NmsMuxElement(const std::vector<hailo_nms_info_t> &nms_infos, const hailo_nms_info_t &fused_nms_info, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    const hailo_nms_info_t &get_fused_nms_info() const;

protected:
    virtual Expected<PipelineBuffer> action(std::vector<PipelineBuffer> &&inputs, PipelineBuffer &&optional) override;

private:
    std::vector<hailo_nms_info_t> m_nms_infos;
    hailo_nms_info_t m_fused_nms_info;
};

class BaseDemuxElement : public PipelineElementInternal
{
public:
    virtual ~BaseDemuxElement() = default;

    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;
    hailo_status set_timeout(std::chrono::milliseconds timeout);

    virtual Expected<uint32_t> get_source_index_from_source_name(const std::string &source_name) override;

protected:
    BaseDemuxElement(size_t source_count, const std::string &name, std::chrono::milliseconds timeout,
        DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);
    virtual hailo_status execute_activate() override;
    virtual hailo_status execute_deactivate() override;
    virtual hailo_status execute_post_deactivate(bool should_clear_abort) override;
    virtual hailo_status execute_abort() override;
    virtual Expected<std::vector<PipelineBuffer>> action(PipelineBuffer &&input) = 0;
    virtual std::vector<PipelinePad*> execution_pads() override;

    std::chrono::milliseconds m_timeout;

private:
    bool were_all_srcs_arrived();

    std::atomic_bool m_is_activated;
    std::atomic_bool m_was_stream_aborted;
    std::unordered_map<std::string, uint32_t> m_source_name_to_index;
    std::vector<bool> m_was_source_called;
    std::vector<PipelineBuffer> m_buffers_for_action;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<PipelinePad*> m_next_pads;
};

class TransformDemuxElement : public BaseDemuxElement
{
public:
    static Expected<std::shared_ptr<TransformDemuxElement>> create(std::shared_ptr<OutputDemuxer> demuxer,
        const std::string &name, std::chrono::milliseconds timeout, hailo_pipeline_elem_stats_flags_t elem_flags,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    static Expected<std::shared_ptr<TransformDemuxElement>> create(std::shared_ptr<OutputDemuxer> demuxer,
        const std::string &name, const ElementBuildParams &build_params, PipelineDirection pipeline_direction = PipelineDirection::PULL,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    TransformDemuxElement(std::shared_ptr<OutputDemuxer> demuxer, const std::string &name,
        std::chrono::milliseconds timeout, DurationCollector &&duration_collector, std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status,
        PipelineDirection pipeline_direction, std::shared_ptr<AsyncPipeline> async_pipeline);

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
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status, hailo_format_order_t order,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);

    PixBufferElement(const std::string &name, std::chrono::milliseconds timeout, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, hailo_format_order_t order,
        std::shared_ptr<AsyncPipeline> async_pipeline);

protected:
    virtual Expected<std::vector<PipelineBuffer>> action(PipelineBuffer &&input);
    hailo_format_order_t m_order;
};

// Note: This element does infer - it sends writes to HW and reads the outputs
class AsyncHwElement : public PipelineElementInternal
{
public:
    static Expected<std::shared_ptr<AsyncHwElement>> create(const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos,
        std::chrono::milliseconds timeout, hailo_pipeline_elem_stats_flags_t elem_flags, const std::string &name,
        std::shared_ptr<std::atomic<hailo_status>> pipeline_status,
        std::shared_ptr<ConfiguredNetworkGroup> net_group, PipelineDirection pipeline_direction = PipelineDirection::PUSH,
        std::shared_ptr<AsyncPipeline> async_pipeline = nullptr);
    AsyncHwElement(const std::unordered_map<std::string, hailo_stream_info_t> &named_stream_infos, std::chrono::milliseconds timeout,
        const std::string &name, DurationCollector &&duration_collector,
        std::shared_ptr<std::atomic<hailo_status>> &&pipeline_status, PipelineDirection pipeline_direction,
        std::shared_ptr<AsyncPipeline> async_pipeline, std::shared_ptr<ConfiguredNetworkGroup> net_group,
        const size_t max_ongoing_transfers, hailo_status &status);
    virtual ~AsyncHwElement() = default;

    virtual void run_push_async(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    void action();
    virtual hailo_status run_push(PipelineBuffer &&buffer, const PipelinePad &sink) override;
    virtual Expected<PipelineBuffer> run_pull(PipelineBuffer &&optional, const PipelinePad &source) override;

    Expected<uint32_t> get_source_index_from_output_stream_name(const std::string &output_stream_name);
    Expected<uint8_t> get_sink_index_from_input_stream_name(const std::string &input_stream_name);
    virtual Expected<uint32_t> get_source_index_from_source_name(const std::string &source_name) override;

    std::vector<PipelineBufferPoolPtr> get_hw_interacted_buffer_pools_h2d();
    std::vector<PipelineBufferPoolPtr> get_hw_interacted_buffer_pools_d2h();

protected:
    virtual std::vector<PipelinePad*> execution_pads() override;
    virtual hailo_status execute_terminate(hailo_status error_status) override;

private:
    void handle_error_in_hw_async_elem(hailo_status error_status);

    std::chrono::milliseconds m_timeout;
    std::weak_ptr<ConfiguredNetworkGroup> m_net_group;
    size_t m_max_ongoing_transfers;

    std::unordered_map<std::string, std::string> m_sink_name_to_stream_name;
    std::unordered_map<std::string, std::string> m_source_name_to_stream_name;
    std::unordered_map<std::string, PipelineBuffer> m_input_buffers;
    std::mutex m_mutex;
    std::unordered_map<std::string, uint32_t> m_source_name_to_index;
    std::unordered_map<std::string, uint32_t> m_sink_name_to_index;
    BarrierPtr m_barrier;
};



} /* namespace hailort */

#endif /* _HAILO_MULTI_IO_ELEMENTS_HPP_ */
