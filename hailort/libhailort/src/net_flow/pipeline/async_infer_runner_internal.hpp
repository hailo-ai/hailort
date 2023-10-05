/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_infer_runner_internal.hpp
 * @brief Implemention of the async HL infer
 **/

#ifndef _HAILO_ASYNC_INFER_RUNNER_INTERNAL_HPP_
#define _HAILO_ASYNC_INFER_RUNNER_INTERNAL_HPP_

#include "network_group/network_group_internal.hpp"
#include "net_flow/pipeline/pipeline.hpp"
#include "net_flow/pipeline/vstream_internal.hpp"
#include "net_flow/ops/argmax_post_process.hpp"
#include "net_flow/ops/softmax_post_process.hpp"
#include "net_flow/ops/yolox_post_process.hpp"
#include "net_flow/ops/ssd_post_process.hpp"
#include "net_flow/ops/op.hpp"

namespace hailort
{
class AsyncPipeline
{
public:
    static Expected<AsyncPipeline> create();
    AsyncPipeline &operator=(const AsyncPipeline &) = delete;

    virtual ~AsyncPipeline() = default;

    void add_element_to_pipeline(std::shared_ptr<PipelineElement> pipeline_element);
    void set_async_hw_element(std::shared_ptr<AsyncHwElement> async_hw_element);
    void add_entry_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &input_name);
    void add_last_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &output_name);
    void set_build_params(ElementBuildParams &build_params);

    const std::vector<std::shared_ptr<PipelineElement>>& get_pipeline() const;
    const std::unordered_map<std::string, std::shared_ptr<PipelineElement>>& get_entry_elements() const;
    const std::unordered_map<std::string, std::shared_ptr<PipelineElement>>& get_last_elements() const;
    const std::shared_ptr<AsyncHwElement> get_async_hw_element();
    const ElementBuildParams get_build_params();

private:
    AsyncPipeline();

    std::vector<std::shared_ptr<PipelineElement>> m_pipeline_elements;
    std::shared_ptr<AsyncHwElement> m_async_hw_element;
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> m_entry_elements;
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> m_last_elements;
    ElementBuildParams m_build_params;
};

class AsyncInferRunnerInternal
{
public:
    static Expected<std::shared_ptr<AsyncInferRunnerInternal>> create(ConfiguredNetworkGroupBase &net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_format_t> &outputs_formats);
    AsyncInferRunnerInternal(AsyncInferRunnerInternal &&other) noexcept = default;
    AsyncInferRunnerInternal &operator=(AsyncInferRunnerInternal &&other) noexcept = default;
    virtual ~AsyncInferRunnerInternal() = default;

    virtual hailo_status async_infer() = 0;
    virtual std::string get_pipeline_description() const = 0;
    virtual std::vector<std::shared_ptr<PipelineElement>> get_pipeline() const = 0;

protected:
    AsyncInferRunnerInternal();
    std::shared_ptr<std::atomic<hailo_status>> m_pipeline_status;

};


class AsyncInferRunnerImpl : public AsyncInferRunnerInternal
{
public:
    static Expected<std::shared_ptr<AsyncInferRunnerImpl>> create(ConfiguredNetworkGroupBase &net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
        const uint32_t timeout = HAILO_DEFAULT_VSTREAM_TIMEOUT_MS);
    AsyncInferRunnerImpl(AsyncInferRunnerImpl &&) = delete;
    AsyncInferRunnerImpl(const AsyncInferRunnerImpl &) = delete;
    AsyncInferRunnerImpl &operator=(AsyncInferRunnerImpl &&) = delete;
    AsyncInferRunnerImpl &operator=(const AsyncInferRunnerImpl &) = delete;
    virtual ~AsyncInferRunnerImpl();
    AsyncInferRunnerImpl(AsyncPipeline &&async_pipeline);

    virtual hailo_status async_infer() override;

    // TODO: consider removing the methods below (needed for unit testing)
    void add_element_to_pipeline(std::shared_ptr<PipelineElement> pipeline_element);
    void add_entry_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &input_name);
    void add_last_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &output_name);

    void set_input(const std::string &input_name, MemoryView &&input_buffer, TransferDoneCallbackAsyncInfer &write_done);
    void set_output(const std::string &output_name, MemoryView &&output_buffer, TransferDoneCallbackAsyncInfer &read_done);

    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> get_entry_elements();
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> get_last_elements();

    virtual std::vector<std::shared_ptr<PipelineElement>> get_pipeline() const override;
    virtual std::string get_pipeline_description() const override;

    static Expected<size_t> get_min_buffer_pool_size(ConfiguredNetworkGroupBase &net_group);

protected:
    static Expected<AsyncPipeline> create_pipeline(ConfiguredNetworkGroupBase &net_group, const std::unordered_map<std::string, hailo_format_t> &inputs_formats,
        const std::unordered_map<std::string, hailo_format_t> &outputs_formats, const uint32_t timeout);

    hailo_status start_pipeline();
    hailo_status stop_pipeline();

    static Expected<std::unordered_map<std::string, std::shared_ptr<InputStream>>> get_input_streams_from_net_group(ConfiguredNetworkGroupBase &net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats);
    static Expected<std::unordered_map<std::string, std::shared_ptr<OutputStream>>> get_output_streams_from_net_group(ConfiguredNetworkGroupBase &net_group,
        const std::unordered_map<std::string, hailo_format_t> &outputs_formats);
    static Expected<std::unordered_map<std::string, hailo_format_t>> expand_auto_input_formats(ConfiguredNetworkGroupBase &net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats);
    static Expected<std::unordered_map<std::string, hailo_format_t>> expand_auto_output_formats(ConfiguredNetworkGroupBase &net_group,
        const std::unordered_map<std::string, hailo_format_t> &outputs_formats);
    static Expected<std::pair<std::string, hailo_format_t>> get_output_format_from_edge_info_name(std::string edge_info_name,
        const std::unordered_map<std::string, hailo_format_t> &outputs_formats);

    static hailo_status create_pre_async_hw_elements(ConfiguredNetworkGroupBase &net_group,
        std::unordered_map<std::string, std::shared_ptr<InputStream>> &input_streams,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, AsyncPipeline &async_pipeline);
    static hailo_status create_post_async_hw_elements(ConfiguredNetworkGroupBase &net_group,
        const std::unordered_map<std::string, hailo_format_t> &expanded_outputs_formats, std::unordered_map<std::string, hailo_format_t> &original_outputs_formats,
        AsyncPipeline &async_pipeline);

    static hailo_status add_argmax_flow(AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
        const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata);
    static hailo_status add_softmax_flow(AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
        const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata);
    static hailo_status add_ops_flows(AsyncPipeline &async_pipeline,
        const std::pair<std::string, hailo_format_t> &output_format, net_flow::PostProcessOpMetadataPtr &op_metadata,
        OutputStreamPtrVector &output_streams, const std::vector<hailo_vstream_info_t> &vstreams_infos);
    static hailo_status add_output_demux_flow(std::shared_ptr<OutputStreamBase> &output_stream,
        AsyncPipeline &async_pipeline, const std::unordered_map<std::string, hailo_format_t> &outputs_formats);
    static hailo_status add_nms_fuse_flow(OutputStreamPtrVector &output_streams, const std::pair<std::string, hailo_format_t> &output_format,
        AsyncPipeline &async_pipeline);
    static hailo_status add_nms_flow(AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
        const std::pair<std::string, hailo_format_t> &output_format, const std::shared_ptr<hailort::net_flow::Op> &nms_op,
        const hailo_vstream_info_t &vstream_info);
    static hailo_status add_iou_flow(AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
        const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata);
    static hailo_status add_nms_flows(AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
        const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &op_metadata,
        const std::vector<hailo_vstream_info_t> &vstreams_infos);


    static Expected<std::shared_ptr<PostInferElement>> add_post_infer_element(const hailo_format_t &output_format, const hailo_nms_info_t &nms_info,
        AsyncPipeline &async_pipeline, const hailo_3d_image_shape_t &src_image_shape, const hailo_format_t &src_format,
        const hailo_3d_image_shape_t &dst_image_shape, const std::vector<hailo_quant_info_t> &dst_quant_infos, bool is_last_copy_element,
        std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<LastAsyncElement>> add_last_async_element(AsyncPipeline &async_pipeline,
        const std::string &output_format_name, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<AsyncPushQueueElement>> add_push_queue_element(const std::string &queue_name, AsyncPipeline &async_pipeline,
        std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> add_nms_to_detections_convert_element(AsyncPipeline &async_pipeline,
        std::shared_ptr<OutputStream> output_stream, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
        const bool is_last_copy_element, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> add_remove_overlapping_bboxes_element(AsyncPipeline &async_pipeline,
        std::shared_ptr<OutputStream> output_stream, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
        const bool is_last_copy_element, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);
    static Expected<std::shared_ptr<FillNmsFormatElement>> add_fill_nms_format_element(AsyncPipeline &async_pipeline,
        std::shared_ptr<OutputStream> output_stream, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
        const hailo_format_t &output_format, const bool is_last_copy_element, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);

    static hailo_status finalize_output_flow(std::shared_ptr<OutputStreamBase> &output_stream_base,
        const std::pair<std::string, hailo_format_t> &output_format, const hailo_nms_info_t &nms_info, const bool is_dma_able,
        AsyncPipeline &async_pipeline, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index = 0);

    AsyncPipeline m_async_pipeline;
    std::unordered_map<std::string, MemoryView> m_input_buffers;
    std::unordered_map<std::string, TransferDoneCallbackAsyncInfer> m_write_dones;
    std::unordered_map<std::string, MemoryView> m_output_buffers;
    std::unordered_map<std::string, TransferDoneCallbackAsyncInfer> m_read_dones;
    volatile bool m_is_activated;
    volatile bool m_is_aborted;
};

} /* namespace hailort */

#endif /* _HAILO_ASYNC_INFER_RUNNER_INTERNAL_HPP_ */
