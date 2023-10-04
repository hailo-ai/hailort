/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file async_infer_runner.cpp
 * @brief Implemention of the async HL infer
 **/

#include <iostream>

#include "common/utils.hpp"
#include "common/os_utils.hpp"
#include "hailo/event.hpp"
#include "hailo/hailort_defaults.hpp"
#include "hailo/hailort_common.hpp"
#include "net_flow/pipeline/async_infer_runner_internal.hpp"
#include "net_flow/pipeline/pipeline.hpp"
#include "net_flow/ops/op_metadata.hpp"

namespace hailort
{

Expected<AsyncPipeline> AsyncPipeline::create()
{
    return AsyncPipeline();
}

AsyncPipeline::AsyncPipeline() {}

void AsyncPipeline::add_element_to_pipeline(std::shared_ptr<PipelineElement> pipeline_element)
{
    m_pipeline_elements.push_back(pipeline_element);
}

void AsyncPipeline::set_async_hw_element(std::shared_ptr<AsyncHwElement> async_hw_element)
{
    m_async_hw_element = async_hw_element;
}

void AsyncPipeline::add_entry_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &input_name)
{
    assert(!contains(m_entry_elements, input_name));
    m_entry_elements[input_name] = pipeline_element;
}

void AsyncPipeline::add_last_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &output_name)
{
    assert(!contains(m_last_elements, output_name));
    m_last_elements[output_name] = pipeline_element;
}

void AsyncPipeline::set_build_params(ElementBuildParams &build_params)
{
    m_build_params = build_params;
}

const std::vector<std::shared_ptr<PipelineElement>>& AsyncPipeline::get_pipeline() const
{
    return m_pipeline_elements;
}

const std::unordered_map<std::string, std::shared_ptr<PipelineElement>>& AsyncPipeline::get_entry_elements() const
{
    return m_entry_elements;
}

const std::unordered_map<std::string, std::shared_ptr<PipelineElement>>& AsyncPipeline::get_last_elements() const
{
    return m_last_elements;
}

const std::shared_ptr<AsyncHwElement> AsyncPipeline::get_async_hw_element()
{
    return m_async_hw_element;
}

const ElementBuildParams AsyncPipeline::get_build_params()
{
    return m_build_params;
}

Expected<std::shared_ptr<AsyncInferRunnerInternal>> AsyncInferRunnerInternal::create(ConfiguredNetworkGroupBase &net_group,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_format_t> &outputs_formats)
{
    auto async_infer_runner = AsyncInferRunnerImpl::create(net_group, inputs_formats, outputs_formats);
    CHECK_EXPECTED(async_infer_runner);

    auto async_infer_runner_ptr = std::shared_ptr<AsyncInferRunnerInternal>(async_infer_runner.release());
    CHECK_NOT_NULL_AS_EXPECTED(async_infer_runner_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return async_infer_runner_ptr;
}

AsyncInferRunnerInternal::AsyncInferRunnerInternal() :
    m_pipeline_status(make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS))
{}

Expected<std::shared_ptr<AsyncInferRunnerImpl>> AsyncInferRunnerImpl::create(ConfiguredNetworkGroupBase &net_group,
    const std::unordered_map<std::string, hailo_format_t> &inputs_formats, const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
    const uint32_t timeout)
{
    auto async_pipeline_expected = create_pipeline(net_group, inputs_formats, outputs_formats, timeout);
    CHECK_EXPECTED(async_pipeline_expected);

    auto async_infer_runner_ptr = make_shared_nothrow<AsyncInferRunnerImpl>(async_pipeline_expected.release());
    CHECK_NOT_NULL_AS_EXPECTED(async_infer_runner_ptr, HAILO_OUT_OF_HOST_MEMORY);

    auto status = async_infer_runner_ptr->start_pipeline();
    CHECK_SUCCESS_AS_EXPECTED(status);

    return async_infer_runner_ptr;
}

AsyncInferRunnerImpl::AsyncInferRunnerImpl(AsyncPipeline &&async_pipeline) :
    AsyncInferRunnerInternal(),
    m_async_pipeline(std::move(async_pipeline)),
    m_is_activated(false),
    m_is_aborted(false)
{}

AsyncInferRunnerImpl::~AsyncInferRunnerImpl()
{
    (void)stop_pipeline();
}

hailo_status AsyncInferRunnerImpl::stop_pipeline()
{
    hailo_status status = HAILO_SUCCESS;
    if (m_is_activated) {
        m_is_activated = false;
        for (auto &entry_element : m_async_pipeline.get_entry_elements()) {
            status = entry_element.second->deactivate();
            if (HAILO_SUCCESS != status) {
                LOGGER__WARNING("Failed deactivate of element {} status {}", entry_element.second->name(), status);
            }

            auto should_clear_abort = (!m_is_aborted);
            status = entry_element.second->post_deactivate(should_clear_abort);
            if (HAILO_SUCCESS != status) {
                LOGGER__WARNING("Failed post deactivate of element {} status {}", entry_element.second->name(), status);
            }
        }
    }
    return status;
}

hailo_status AsyncInferRunnerImpl::start_pipeline()
{
    hailo_status status = HAILO_SUCCESS;
    for (auto &entry_element : m_async_pipeline.get_entry_elements()) {
        status = entry_element.second->activate();
        CHECK_SUCCESS(status);
    }

    return status;
}

hailo_status AsyncInferRunnerImpl::async_infer()
{
    hailo_status status = m_async_pipeline.get_build_params().pipeline_status->load();
    CHECK(HAILO_SUCCESS == status, HAILO_INVALID_OPERATION, "Can't handle infer request since Pipeline status is {}.", status);

    for (auto &last_element : m_async_pipeline.get_last_elements()) {
        auto buffers_are_full = last_element.second->are_buffer_pools_full();
        CHECK_EXPECTED_AS_STATUS(buffers_are_full);
        if (buffers_are_full.release()) {
            LOGGER__ERROR("Can't handle infer request since queue is full.");
            return HAILO_QUEUE_IS_FULL;
        }
    }

    for (auto &last_element : m_async_pipeline.get_last_elements()) {
        assert(contains(m_output_buffers, last_element.first));
        auto output_buffer = m_output_buffers.at(last_element.first);
        auto read_done = m_read_dones.at(last_element.first);
        // TODO: handle the non-recoverable case where one buffer is enqueued succesfully and the second isn't (HRT-11783)
        status = last_element.second->enqueue_execution_buffer(output_buffer, read_done);
        CHECK_SUCCESS(status);
    }

    for (auto &entry_element : m_async_pipeline.get_entry_elements()) {
        assert(contains(m_input_buffers, entry_element.first));
        auto input_buffer = m_input_buffers.at(entry_element.first);
        auto write_done = m_write_dones.at(entry_element.first);
        entry_element.second->sinks()[0].run_push_async(PipelineBuffer(input_buffer, write_done));
    }
    return HAILO_SUCCESS;
}

void AsyncInferRunnerImpl::add_element_to_pipeline(std::shared_ptr<PipelineElement> pipeline_element)
{
    m_async_pipeline.add_element_to_pipeline(pipeline_element);
}

void AsyncInferRunnerImpl::add_entry_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &input_name)
{
    m_async_pipeline.add_entry_element(pipeline_element, input_name);
}

void AsyncInferRunnerImpl::add_last_element(std::shared_ptr<PipelineElement> pipeline_element, const std::string &output_name)
{
    m_async_pipeline.add_last_element(pipeline_element, output_name);
}

std::unordered_map<std::string, std::shared_ptr<PipelineElement>> AsyncInferRunnerImpl::get_entry_elements()
{
    return m_async_pipeline.get_entry_elements();
}

std::unordered_map<std::string, std::shared_ptr<PipelineElement>> AsyncInferRunnerImpl::get_last_elements()
{
    return m_async_pipeline.get_last_elements();
}

void AsyncInferRunnerImpl::set_input(const std::string &input_name, MemoryView &&input_buffer, TransferDoneCallbackAsyncInfer &write_done)
{
    m_input_buffers[input_name] = std::move(input_buffer);
    m_write_dones[input_name] = write_done;
}

void AsyncInferRunnerImpl::set_output(const std::string &output_name, MemoryView &&output_buffer, TransferDoneCallbackAsyncInfer &read_done)
{
    m_output_buffers[output_name] = std::move(output_buffer);
    m_read_dones[output_name] = read_done;
}

Expected<size_t> AsyncInferRunnerImpl::get_min_buffer_pool_size(ConfiguredNetworkGroupBase &net_group)
{
    uint32_t buffer_pool_size = UINT32_MAX;

    auto input_streams = net_group.get_input_streams();
    for (const auto &input_stream : input_streams) {
        auto async_max_queue_size = input_stream.get().get_async_max_queue_size();
        CHECK_EXPECTED(async_max_queue_size);
        if (buffer_pool_size > async_max_queue_size.value()) {
            buffer_pool_size = static_cast<uint32_t>(async_max_queue_size.value());
        }
    }

    auto output_streams = net_group.get_output_streams();
    for (const auto &output_stream : output_streams) {
        auto async_max_queue_size = output_stream.get().get_async_max_queue_size();
        CHECK_EXPECTED(async_max_queue_size);
        if (buffer_pool_size > async_max_queue_size.value()) {
            buffer_pool_size = static_cast<uint32_t>(async_max_queue_size.value());
        }
    }

    return buffer_pool_size;
}

Expected<std::unordered_map<std::string, hailo_format_t>> AsyncInferRunnerImpl::expand_auto_input_formats(ConfiguredNetworkGroupBase &net_group,
    const std::unordered_map<std::string, hailo_format_t> &inputs_formats)
{
    std::unordered_map<std::string, hailo_format_t> expanded_input_format;
    for (auto &input_format : inputs_formats) {
        auto input_streams_names = net_group.get_stream_names_from_vstream_name(input_format.first);
        CHECK_EXPECTED(input_streams_names);

        // TODO: Taking data from the first ll stream will not work in multi-planar work
        auto shared_stream_ptr = net_group.get_shared_input_stream_by_name(input_streams_names.value()[0]);
        CHECK_EXPECTED(shared_stream_ptr);

        expanded_input_format[input_format.first] = HailoRTDefaults::expand_auto_format(input_format.second,
            shared_stream_ptr.value()->get_info().format);
    }
    return expanded_input_format;
}

Expected<std::unordered_map<std::string, hailo_format_t>> AsyncInferRunnerImpl::expand_auto_output_formats(ConfiguredNetworkGroupBase &net_group,
    const std::unordered_map<std::string, hailo_format_t> &outputs_formats)
{
    std::unordered_map<std::string, hailo_format_t> expanded_output_format;
    for (auto &output_format : outputs_formats) {
        auto output_streams_names = net_group.get_stream_names_from_vstream_name(output_format.first);
        CHECK_EXPECTED(output_streams_names);

        // TODO: Taking data from the first ll stream will not work in multi-planar work
        auto shared_stream_ptr = net_group.get_shared_output_stream_by_name(output_streams_names.value()[0]);
        CHECK_EXPECTED(shared_stream_ptr);

        expanded_output_format[output_format.first] = HailoRTDefaults::expand_auto_format(output_format.second,
            shared_stream_ptr.value()->get_info().format);
    }
    return expanded_output_format;
}

Expected<std::unordered_map<std::string, std::shared_ptr<InputStream>>> AsyncInferRunnerImpl::get_input_streams_from_net_group(ConfiguredNetworkGroupBase &net_group,
    const std::unordered_map<std::string, hailo_format_t> &inputs_formats)
{
    std::unordered_map<std::string, std::shared_ptr<InputStream>> input_streams;
    for (auto &input_format : inputs_formats) {
        auto input_streams_names = net_group.get_stream_names_from_vstream_name(input_format.first);
        CHECK_EXPECTED(input_streams_names);

        for (auto &input_stream_name : input_streams_names.release()) {
            auto shared_stream_ptr = net_group.get_shared_input_stream_by_name(input_stream_name);
            CHECK_EXPECTED(shared_stream_ptr);

            input_streams[input_stream_name] = shared_stream_ptr.release();
        }
    }
    return input_streams;
}

Expected<std::unordered_map<std::string, std::shared_ptr<OutputStream>>> AsyncInferRunnerImpl::get_output_streams_from_net_group(ConfiguredNetworkGroupBase &net_group,
    const std::unordered_map<std::string, hailo_format_t> &outputs_formats)
{
    std::unordered_map<std::string, std::shared_ptr<OutputStream>> output_streams;
    for (auto &output_format : outputs_formats) {
        auto output_streams_names = net_group.get_stream_names_from_vstream_name(output_format.first);
        CHECK_EXPECTED(output_streams_names);

        for (auto &output_stream_name : output_streams_names.release()) {
            auto shared_stream_ptr = net_group.get_shared_output_stream_by_name(output_stream_name);
            CHECK_EXPECTED(shared_stream_ptr);

            output_streams[output_stream_name] = shared_stream_ptr.release();
        }
    }
    return output_streams;
}

hailo_status AsyncInferRunnerImpl::create_pre_async_hw_elements(ConfiguredNetworkGroupBase &net_group,
        std::unordered_map<std::string, std::shared_ptr<InputStream>> &input_streams,
        const std::unordered_map<std::string, hailo_format_t> &inputs_formats, AsyncPipeline &async_pipeline)
{
    bool is_dma_able = true;
    for (auto &input_stream_pair : input_streams) {
        auto input_stream = input_stream_pair.second;
        auto input_stream_name = input_stream_pair.first;
        auto input_stream_base = std::static_pointer_cast<InputStreamBase>(input_stream);
        auto input_stream_info = input_stream->get_info();
        auto vstream_names = net_group.get_vstream_names_from_stream_name(input_stream_name);
        CHECK_EXPECTED_AS_STATUS(vstream_names);

        auto sink_index = async_pipeline.get_async_hw_element()->get_sink_index_from_input_stream_name(input_stream_name);
        CHECK_EXPECTED_AS_STATUS(sink_index);

        auto should_transform = InputTransformContext::is_transformation_required(input_stream_info.shape,
            inputs_formats.at(input_stream_name), input_stream_info.hw_shape, input_stream_info.format,
            input_stream_base->get_quant_infos());
        CHECK_EXPECTED_AS_STATUS(should_transform);

        auto entry_queue_elem = add_push_queue_element(PipelineObject::create_element_name("EntryPushQueueElement", input_stream_info.name, input_stream_info.index),
            async_pipeline, nullptr);
        CHECK_EXPECTED_AS_STATUS(entry_queue_elem);

        if (should_transform.value()) {
            auto pre_infer_elem = PreInferElement::create(input_stream_info.shape, inputs_formats.at(input_stream_name),
                input_stream_info.hw_shape, input_stream_info.format, input_stream_base->get_quant_infos(),
                PipelineObject::create_element_name("PreInferElement", input_stream_info.name, input_stream_info.index),
                async_pipeline.get_build_params(), PipelineDirection::PUSH, is_dma_able);
            CHECK_EXPECTED_AS_STATUS(pre_infer_elem);
            async_pipeline.add_element_to_pipeline(pre_infer_elem.value());
            CHECK_SUCCESS(PipelinePad::link_pads(entry_queue_elem.value(), pre_infer_elem.value()));

            auto queue_elem = add_push_queue_element(PipelineObject::create_element_name("PushQueueElement", input_stream_info.name, input_stream_info.index),
                async_pipeline, pre_infer_elem.value());
            CHECK_EXPECTED_AS_STATUS(queue_elem);

            CHECK_SUCCESS(PipelinePad::link_pads(queue_elem.value(), async_pipeline.get_async_hw_element(), 0, sink_index.value()));
        } else {
            CHECK_SUCCESS(PipelinePad::link_pads(entry_queue_elem.value(), async_pipeline.get_async_hw_element(), 0, sink_index.value()));
        }

        for (auto &vstream_name : vstream_names.release()) {
            if (!contains(async_pipeline.get_entry_elements(), vstream_name)) {
                async_pipeline.add_entry_element(entry_queue_elem.release(), vstream_name);
            }
        }
    }
    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<PostInferElement>> AsyncInferRunnerImpl::add_post_infer_element(const hailo_format_t &output_format,
    const hailo_nms_info_t &nms_info, AsyncPipeline &async_pipeline, const hailo_3d_image_shape_t &src_image_shape,
    const hailo_format_t &src_format, const hailo_3d_image_shape_t &dst_image_shape, const std::vector<hailo_quant_info_t> &dst_quant_infos,
    bool is_last_copy_element, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index)
{
    auto queue_elem = add_push_queue_element(PipelineObject::create_element_name("PushQueueElement", final_elem->name(), static_cast<uint8_t>(final_elem_source_index)),
        async_pipeline, final_elem, final_elem_source_index);
    CHECK_EXPECTED(queue_elem);

    auto post_infer_elem = PostInferElement::create(src_image_shape, src_format, dst_image_shape, output_format,
        dst_quant_infos, nms_info, PipelineObject::create_element_name("PostInferElement",
        final_elem->name(), static_cast<uint8_t>(final_elem_source_index)), async_pipeline.get_build_params(),
        PipelineDirection::PUSH, is_last_copy_element);
    CHECK_EXPECTED(post_infer_elem);

    async_pipeline.add_element_to_pipeline(post_infer_elem.value());

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(queue_elem.value(), post_infer_elem.value()));
    return post_infer_elem.release();
}

Expected<std::shared_ptr<AsyncPushQueueElement>> AsyncInferRunnerImpl::add_push_queue_element(const std::string &queue_name, AsyncPipeline &async_pipeline,
    std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index)
{
    auto push_queue_elem = AsyncPushQueueElement::create(queue_name, async_pipeline.get_build_params(), PipelineDirection::PUSH);
    CHECK_EXPECTED(push_queue_elem);

    async_pipeline.add_element_to_pipeline(push_queue_elem.value());

    // final elem will be nullptr in case it's the first element in pipeline
    if (final_elem) {
        CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, push_queue_elem.value(), final_elem_source_index, 0));
    }

    return push_queue_elem.release();
}

Expected<std::shared_ptr<ConvertNmsToDetectionsElement>> AsyncInferRunnerImpl::add_nms_to_detections_convert_element(AsyncPipeline &async_pipeline,
    std::shared_ptr<OutputStream> output_stream, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    const bool is_last_copy_element, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_index)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    auto nms_to_detections_element = ConvertNmsToDetectionsElement::create(metadata->nms_info(),
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        async_pipeline.get_build_params(), PipelineDirection::PUSH, is_last_copy_element);
    CHECK_EXPECTED(nms_to_detections_element);

    async_pipeline.add_element_to_pipeline(nms_to_detections_element.value());

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, nms_to_detections_element.value(), final_elem_index, 0));
    return nms_to_detections_element.release();
}

Expected<std::shared_ptr<RemoveOverlappingBboxesElement>> AsyncInferRunnerImpl::add_remove_overlapping_bboxes_element(AsyncPipeline &async_pipeline,
    std::shared_ptr<OutputStream> output_stream, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    const bool is_last_copy_element, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_index)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    auto remove_overlapping_bboxes_element = RemoveOverlappingBboxesElement::create(metadata->nms_config(),
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        async_pipeline.get_build_params(), PipelineDirection::PUSH, is_last_copy_element);
    CHECK_EXPECTED(remove_overlapping_bboxes_element);

    async_pipeline.add_element_to_pipeline(remove_overlapping_bboxes_element.value());

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, remove_overlapping_bboxes_element.value(), final_elem_index, 0));
    return remove_overlapping_bboxes_element;
}

Expected<std::shared_ptr<FillNmsFormatElement>> AsyncInferRunnerImpl::add_fill_nms_format_element(AsyncPipeline &async_pipeline,
    std::shared_ptr<OutputStream> output_stream, const std::string &element_name, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    const hailo_format_t &output_format, const bool is_last_copy_element, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_index)
{
    auto metadata = std::dynamic_pointer_cast<net_flow::NmsOpMetadata>(op_metadata);
    assert(nullptr != metadata);

    auto fill_nms_format_element = FillNmsFormatElement::create(metadata->nms_info(), output_format, metadata->nms_config(),
        PipelineObject::create_element_name(element_name, output_stream->name(), output_stream->get_info().index),
        async_pipeline.get_build_params(), PipelineDirection::PUSH, is_last_copy_element);
    CHECK_EXPECTED(fill_nms_format_element);

    async_pipeline.add_element_to_pipeline(fill_nms_format_element.value());

    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, fill_nms_format_element.value(), final_elem_index, 0));
    return fill_nms_format_element;
}

Expected<std::shared_ptr<LastAsyncElement>> AsyncInferRunnerImpl::add_last_async_element(AsyncPipeline &async_pipeline,
    const std::string &output_format_name, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index)
{
    auto last_async_element = LastAsyncElement::create(PipelineObject::create_element_name("LastAsyncElement",
        final_elem->name(), static_cast<uint8_t>(final_elem_source_index)), async_pipeline.get_build_params());
    CHECK_EXPECTED(last_async_element);

    async_pipeline.add_element_to_pipeline(last_async_element.value());
    CHECK_SUCCESS_AS_EXPECTED(PipelinePad::link_pads(final_elem, last_async_element.value(), final_elem_source_index, 0));

    async_pipeline.add_last_element(last_async_element.value(), output_format_name);

    return last_async_element.release();
}

Expected<std::pair<std::string, hailo_format_t>> AsyncInferRunnerImpl::get_output_format_from_edge_info_name(std::string edge_info_name,
    const std::unordered_map<std::string, hailo_format_t> &outputs_formats)
{
    for (auto &output_format : outputs_formats) {
        if (output_format.first == edge_info_name) {
            return std::pair<std::string, hailo_format_t>(output_format);
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

hailo_status AsyncInferRunnerImpl::add_output_demux_flow(std::shared_ptr<OutputStreamBase> &output_stream,
    AsyncPipeline &async_pipeline, const std::unordered_map<std::string, hailo_format_t> &outputs_formats)
{
    const bool is_dma_able_hw_async = true;
    auto status = async_pipeline.get_async_hw_element()->fill_buffer_pools(is_dma_able_hw_async);
    CHECK_SUCCESS(status);

    auto expected_demuxer = OutputDemuxer::create(*output_stream);
    CHECK_EXPECTED_AS_STATUS(expected_demuxer);

    std::shared_ptr<OutputDemuxer> demuxer_ptr = expected_demuxer.release();
    CHECK_ARG_NOT_NULL(demuxer_ptr);

    status = output_stream->set_timeout(HAILO_INFINITE_TIMEOUT);
    CHECK_SUCCESS(status);

    auto demux_elem = TransformDemuxElement::create(demuxer_ptr,
        PipelineObject::create_element_name("TransformDemuxElement", output_stream->name(), output_stream->get_info().index),
        async_pipeline.get_build_params(), PipelineDirection::PUSH);
    CHECK_EXPECTED_AS_STATUS(demux_elem);
    async_pipeline.add_element_to_pipeline(demux_elem.value());

    auto output_index = async_pipeline.get_async_hw_element()->get_source_index_from_output_stream_name(output_stream->name());
    CHECK_EXPECTED_AS_STATUS(output_index);
    CHECK_SUCCESS(PipelinePad::link_pads(async_pipeline.get_async_hw_element(), demux_elem.value(), output_index.value(), 0));

    uint8_t i = 0;
    for (auto &edge_info : demuxer_ptr->get_edges_stream_info()) {
        auto output_format_expected = get_output_format_from_edge_info_name(edge_info.name, outputs_formats);
        CHECK_EXPECTED_AS_STATUS(output_format_expected);

        auto demux_queue_elem = add_push_queue_element(PipelineObject::create_element_name("PushQueueElement_demux", edge_info.name, i), async_pipeline,
            demux_elem.value(), i);
        CHECK_EXPECTED_AS_STATUS(demux_queue_elem);

        auto should_transform = OutputTransformContext::is_transformation_required(edge_info.hw_shape, 
            edge_info.format, edge_info.shape, output_format_expected.value().second, std::vector<hailo_quant_info_t>{edge_info.quant_info}); // TODO: Get quant vector (HRT-11077)
        CHECK_EXPECTED_AS_STATUS(should_transform);

        if (should_transform.value()) {
            status = demux_elem.value()->fill_buffer_pool(false, i);
            CHECK_SUCCESS(status);

            auto post_infer_elem = add_post_infer_element(output_format_expected.value().second, edge_info.nms_info,
                async_pipeline, edge_info.hw_shape, edge_info.format, edge_info.shape, {edge_info.quant_info}, true, demux_queue_elem.value());
            CHECK_EXPECTED_AS_STATUS(post_infer_elem);

            auto last_async_element = add_last_async_element(async_pipeline, output_format_expected.value().first, post_infer_elem.value());
            CHECK_EXPECTED_AS_STATUS(last_async_element);
        } else {
            auto last_async_element = add_last_async_element(async_pipeline, output_format_expected.value().first, demux_queue_elem.value());
            CHECK_EXPECTED_AS_STATUS(last_async_element);
        }
        i++;
    }
    return HAILO_SUCCESS;
}

// TODO: remove this function as part of HRT-11667
hailo_status AsyncInferRunnerImpl::finalize_output_flow(std::shared_ptr<OutputStreamBase> &output_stream_base,
    const std::pair<std::string, hailo_format_t> &output_format, const hailo_nms_info_t &nms_info, const bool is_dma_able,
    AsyncPipeline &async_pipeline, std::shared_ptr<PipelineElement> final_elem, const uint32_t final_elem_source_index)
{
    auto stream_info = output_stream_base->get_info();
    auto stream_quant_infos = output_stream_base->get_quant_infos();
    auto should_transform = OutputTransformContext::is_transformation_required(stream_info.hw_shape,
        stream_info.format, stream_info.shape, output_format.second, stream_quant_infos);
    CHECK_EXPECTED_AS_STATUS(should_transform);

    if (should_transform.value()) {
        hailo_status status = final_elem->fill_buffer_pools(is_dma_able);
        CHECK_SUCCESS(status);

        auto post_infer_elem = add_post_infer_element(output_format.second, nms_info, async_pipeline,
            stream_info.hw_shape, stream_info.format, stream_info.shape, stream_quant_infos, true, final_elem, final_elem_source_index);
        CHECK_EXPECTED_AS_STATUS(post_infer_elem);

        auto last_async_element = add_last_async_element(async_pipeline, output_format.first, post_infer_elem.value());
        CHECK_EXPECTED_AS_STATUS(last_async_element);
    } else {
        auto last_async_element = add_last_async_element(async_pipeline, output_format.first, final_elem, final_elem_source_index);
        CHECK_EXPECTED_AS_STATUS(last_async_element);
    }
    return HAILO_SUCCESS;
}

hailo_status AsyncInferRunnerImpl::add_nms_fuse_flow(OutputStreamPtrVector &output_streams,
    const std::pair<std::string, hailo_format_t> &output_format, AsyncPipeline &async_pipeline)
{
    const bool is_dma_able_hw_async = true;
    auto status = async_pipeline.get_async_hw_element()->fill_buffer_pools(is_dma_able_hw_async);
    CHECK_SUCCESS(status);

    std::vector<hailo_nms_info_t> nms_infos;
    nms_infos.reserve(output_streams.size());
    for (const auto &out_stream : output_streams) {
        CHECK(out_stream->get_info().nms_info.defuse_info.class_group_index <= output_streams.size(),
            HAILO_INVALID_ARGUMENT, "Not all defused nms outputs were grouped correctly!");
        nms_infos.emplace_back(out_stream->get_info().nms_info);
    }

    // To get the fused layer name and src stream format, we use the stream info of one of the defuses
    auto first_defused_stream_info = output_streams[0]->get_info();
    auto fused_layer_name = first_defused_stream_info.nms_info.defuse_info.original_name;

    bool is_last_copy_element = true;
    auto nms_elem = NmsMuxElement::create(nms_infos, PipelineObject::create_element_name("NmsMuxElement", fused_layer_name, 0),
        async_pipeline.get_build_params(), PipelineDirection::PUSH, is_last_copy_element);
    CHECK_EXPECTED_AS_STATUS(nms_elem);

    async_pipeline.add_element_to_pipeline(nms_elem.value());

    uint32_t i = 0;
    for (auto &output_stream :  output_streams) {
        const auto &curr_stream_info = output_stream->get_info();
        output_stream->set_timeout(HAILO_INFINITE_TIMEOUT);

        auto output_index = async_pipeline.get_async_hw_element()->get_source_index_from_output_stream_name(output_stream->name());
        CHECK_EXPECTED_AS_STATUS(output_index);

        auto queue_elem = add_push_queue_element(PipelineObject::create_element_name("PushQueueElement_nms_source", curr_stream_info.name, curr_stream_info.index),
            async_pipeline, async_pipeline.get_async_hw_element(), output_index.value());
        CHECK_EXPECTED_AS_STATUS(queue_elem);

        CHECK_SUCCESS(PipelinePad::link_pads(queue_elem.value(), nms_elem.value(), 0, i));
        i++;
    }

    auto output_stream_base = std::static_pointer_cast<OutputStreamBase>(output_streams[0]);
    auto fused_layer_nms_info = nms_elem.value()->get_fused_nms_info();
    const bool is_dma_able_nms_mux = false;
    const uint32_t final_elem_source_index = 0;
    status = finalize_output_flow(output_stream_base, output_format, fused_layer_nms_info,
        is_dma_able_nms_mux, async_pipeline, nms_elem.value(), final_elem_source_index);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status AsyncInferRunnerImpl::add_softmax_flow(AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
    const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &softmax_op_metadata)
{
    assert(output_streams.size() == 1);
    auto output_stream_base = std::static_pointer_cast<OutputStreamBase>(output_streams[0]);
    auto hw_async_elem_index = async_pipeline.get_async_hw_element()->get_source_index_from_output_stream_name(output_stream_base->name());
    CHECK_EXPECTED_AS_STATUS(hw_async_elem_index);

    auto op_input_format = softmax_op_metadata->inputs_metadata().begin()->second.format;
    auto output_format_expanded = net_flow::SoftmaxOpMetadata::expand_output_format_autos(output_format.second, op_input_format);

    auto stream_info = output_stream_base->get_info();
    auto stream_quant_infos = output_stream_base->get_quant_infos();
    auto post_infer_elem = add_post_infer_element(output_format_expanded, {}, async_pipeline, stream_info.hw_shape, stream_info.format,
        stream_info.shape, output_stream_base->get_quant_infos(), false, async_pipeline.get_async_hw_element(), hw_async_elem_index.value());
    CHECK_EXPECTED_AS_STATUS(post_infer_elem);

    auto queue_elem = add_push_queue_element(PipelineObject::create_element_name("PushQueueElement_softmax", async_pipeline.get_async_hw_element()->name(),
        static_cast<uint8_t>(hw_async_elem_index.value())), async_pipeline, post_infer_elem.value());
    CHECK_EXPECTED_AS_STATUS(queue_elem);

    // Updating metadata according to user request
    // Currently softmax only supports inputs to be float32 and order NHWC or NC
    auto updated_inputs_metadata = softmax_op_metadata.get()->inputs_metadata();
    updated_inputs_metadata.begin()->second.format = output_format_expanded;
    auto updated_outputs_metadata = softmax_op_metadata.get()->outputs_metadata();
    updated_outputs_metadata.begin()->second.format = output_format_expanded;
    auto metadata = std::dynamic_pointer_cast<net_flow::SoftmaxOpMetadata>(softmax_op_metadata);
    assert(nullptr != metadata);
    metadata->set_outputs_metadata(updated_outputs_metadata);
    metadata->set_inputs_metadata(updated_inputs_metadata);
    CHECK_SUCCESS(metadata->validate_format_info());

    auto op_expected = net_flow::SoftmaxPostProcessOp::create(metadata);
    CHECK_EXPECTED_AS_STATUS(op_expected);

    auto softmax_op = op_expected.release();
    auto softmax_element = SoftmaxPostProcessElement::create(softmax_op,
        PipelineObject::create_element_name("SoftmaxPostProcessElement", output_stream_base->name(), stream_info.index),
        async_pipeline.get_build_params(), PipelineDirection::PUSH, true);
    CHECK_EXPECTED_AS_STATUS(softmax_element);

    async_pipeline.add_element_to_pipeline(softmax_element.value());
    CHECK_SUCCESS(PipelinePad::link_pads(queue_elem.value(), softmax_element.value()));

    auto last_async_element = add_last_async_element(async_pipeline, output_format.first, softmax_element.value());
    CHECK_EXPECTED_AS_STATUS(last_async_element);

    return HAILO_SUCCESS;
}

hailo_status AsyncInferRunnerImpl::add_argmax_flow(AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
    const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &argmax_op_metadata)
{
    assert(output_streams.size() == 1);
    auto output_stream_base = std::static_pointer_cast<OutputStreamBase>(output_streams[0]);
    auto hw_async_elem_index = async_pipeline.get_async_hw_element()->get_source_index_from_output_stream_name(output_stream_base->name());
    CHECK_EXPECTED_AS_STATUS(hw_async_elem_index);

    auto queue_elem = add_push_queue_element(PipelineObject::create_element_name("PushQueueElement_argmax", async_pipeline.get_async_hw_element()->name(),
        static_cast<uint8_t>(hw_async_elem_index.value())), async_pipeline, async_pipeline.get_async_hw_element());
    CHECK_EXPECTED_AS_STATUS(queue_elem);

    // Updating metadata according to user request
    auto op_input_format = argmax_op_metadata->inputs_metadata().begin()->second.format;
    auto updated_outputs_metadata = argmax_op_metadata.get()->outputs_metadata();
    updated_outputs_metadata.begin()->second.format = net_flow::ArgmaxOpMetadata::expand_output_format_autos(output_format.second, op_input_format);;
    auto metadata = std::dynamic_pointer_cast<net_flow::ArgmaxOpMetadata>(argmax_op_metadata);
    assert(nullptr != metadata);
    metadata->set_outputs_metadata(updated_outputs_metadata);
    CHECK_SUCCESS(metadata->validate_format_info());

    auto op_expected = net_flow::ArgmaxPostProcessOp::create(metadata);
    CHECK_EXPECTED_AS_STATUS(op_expected);
    auto argmax_op = op_expected.release();
    bool is_last_copy_element = true;

    auto argmax_element = ArgmaxPostProcessElement::create(argmax_op,
        PipelineObject::create_element_name("ArgmaxPostProcessElement", output_stream_base->name(), output_stream_base->get_info().index),
        async_pipeline.get_build_params(), PipelineDirection::PUSH, is_last_copy_element);
    CHECK_EXPECTED_AS_STATUS(argmax_element);

    async_pipeline.add_element_to_pipeline(argmax_element.value());
    CHECK_SUCCESS(PipelinePad::link_pads(queue_elem.value(), argmax_element.value()));

    auto last_async_element = add_last_async_element(async_pipeline, output_format.first, argmax_element.value());
    CHECK_EXPECTED_AS_STATUS(last_async_element);

    return HAILO_SUCCESS;
}

hailo_status AsyncInferRunnerImpl::add_nms_flow(AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
    const std::pair<std::string, hailo_format_t> &output_format, const std::shared_ptr<hailort::net_flow::Op> &nms_op,
    const hailo_vstream_info_t &vstream_info)
{
    auto first_stream_info = output_streams[0]->get_info();
    CHECK(output_format.second.type == HAILO_FORMAT_TYPE_FLOAT32, HAILO_INVALID_ARGUMENT,
        "NMS output format type must be HAILO_FORMAT_TYPE_FLOAT32");
    CHECK(HailoRTCommon::is_nms(output_format.second.order), HAILO_INVALID_ARGUMENT,
        "NMS output format order must be HAILO_FORMAT_ORDER_HAILO_NMS or HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK");

    std::unordered_map<std::string, net_flow::BufferMetaData> inputs_metadata;
    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    for (uint32_t i = 0; i < output_streams.size(); ++i) {
        const auto &curr_stream_info = output_streams[i]->get_info();
        net_flow::BufferMetaData input_metadata = {
            curr_stream_info.shape,
            curr_stream_info.hw_shape,
            curr_stream_info.format,
            curr_stream_info.quant_info
        };
        inputs_metadata.insert({curr_stream_info.name, input_metadata});
    }

    assert(nms_op->outputs_metadata().size() == 1);

    net_flow::BufferMetaData output_metadata = {
        vstream_info.shape,
        vstream_info.shape,
        vstream_info.format,
        vstream_info.quant_info
    };
    outputs_metadata.insert({nms_op->outputs_metadata().begin()->first, output_metadata});

    auto nms_elem = NmsPostProcessMuxElement::create(nms_op, PipelineObject::create_element_name("NmsPostProcessMuxElement", nms_op->get_name(), 0),
        async_pipeline.get_build_params(), PipelineDirection::PUSH, true);
    CHECK_EXPECTED_AS_STATUS(nms_elem);

    async_pipeline.add_element_to_pipeline(nms_elem.value());

    hailo_format_t nms_src_format;
    nms_src_format.flags = HAILO_FORMAT_FLAGS_QUANTIZED;
    nms_src_format.order = HAILO_FORMAT_ORDER_NHCW;
    nms_src_format.type = first_stream_info.format.type;

    for (uint32_t i = 0; i < output_streams.size(); ++i) {
        const auto &curr_stream_info = output_streams[i]->get_info();
        output_streams[i]->set_timeout(HAILO_INFINITE_TIMEOUT); // TODO: Check with Salem/Kimel if can be removed

        auto output_stream_base = std::static_pointer_cast<OutputStreamBase>(output_streams[i]);
        auto should_transform = OutputTransformContext::is_transformation_required(curr_stream_info.hw_shape, curr_stream_info.format,
            curr_stream_info.hw_shape, nms_src_format, output_stream_base->get_quant_infos());
        CHECK_EXPECTED_AS_STATUS(should_transform);

        CHECK(!(should_transform.value()), HAILO_INVALID_ARGUMENT, "Unexpected transformation required for {}", curr_stream_info.name);

        auto source_id = async_pipeline.get_async_hw_element()->get_source_index_from_output_stream_name(output_stream_base->name());
        CHECK_EXPECTED_AS_STATUS(source_id);

        auto nms_source_queue_elem = add_push_queue_element(PipelineObject::create_element_name("PullQueueElement_nms_source", curr_stream_info.name, curr_stream_info.index),
            async_pipeline, async_pipeline.get_async_hw_element(), source_id.value());
        CHECK_EXPECTED_AS_STATUS(nms_source_queue_elem);

        CHECK_SUCCESS(PipelinePad::link_pads(nms_source_queue_elem.value(), nms_elem.value(), 0, i));
        nms_elem.value()->add_sink_name(curr_stream_info.name);
    }
    auto last_async_element = add_last_async_element(async_pipeline, output_format.first, nms_elem.value());
    CHECK_EXPECTED_AS_STATUS(last_async_element);

    return HAILO_SUCCESS;
}

hailo_status AsyncInferRunnerImpl::add_iou_flow( AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
    const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &iou_op_metadata)
{
    assert(output_streams.size() == 1);
    auto output_stream = output_streams[0];

    auto output_index = async_pipeline.get_async_hw_element()->get_source_index_from_output_stream_name(output_stream->name());
        CHECK_EXPECTED_AS_STATUS(output_index);

    auto hw_read_queue_element = add_push_queue_element(PipelineObject::create_element_name("PushQueueElement_hw_read", output_stream->name(), output_stream->get_info().index),
        async_pipeline, async_pipeline.get_async_hw_element() , output_index.value());
    CHECK_EXPECTED_AS_STATUS(hw_read_queue_element);

    auto &stream_info = output_stream->get_info();
    auto &stream_quant_infos = output_stream->get_quant_infos();

    auto post_infer_element = add_post_infer_element(output_format.second, stream_info.nms_info,
        async_pipeline, stream_info.hw_shape, stream_info.format, stream_info.shape, stream_quant_infos, false, hw_read_queue_element.value());
    CHECK_EXPECTED_AS_STATUS(post_infer_element);

    auto pre_nms_convert_queue_element = add_push_queue_element(PipelineObject::create_element_name("PullQueueElement_pre_nms_convert", output_stream->name(), output_stream->get_info().index),
        async_pipeline, post_infer_element.value());
    CHECK_EXPECTED_AS_STATUS(pre_nms_convert_queue_element);

    auto nms_to_detections_element = add_nms_to_detections_convert_element(async_pipeline, output_stream, "NmsFormatToDetectionsElement", iou_op_metadata,
        false, pre_nms_convert_queue_element.value());
    CHECK_EXPECTED_AS_STATUS(nms_to_detections_element);

    auto pre_remove_overlapping_bboxes_element_queue_element = add_push_queue_element(PipelineObject::create_element_name("PullQueueElement_pre_bboxes_removing", output_stream->name(), output_stream->get_info().index),
        async_pipeline, nms_to_detections_element.value());
    CHECK_EXPECTED_AS_STATUS(pre_remove_overlapping_bboxes_element_queue_element);

    auto remove_overlapping_bboxes_element = add_remove_overlapping_bboxes_element(async_pipeline, output_stream, "RemoveOverlappingBboxesElement", iou_op_metadata,
        false, pre_remove_overlapping_bboxes_element_queue_element.value());
    CHECK_EXPECTED_AS_STATUS(remove_overlapping_bboxes_element);

    auto pre_fill_nms_format_element_queue_element = add_push_queue_element(PipelineObject::create_element_name("PullQueueElement_pre_fill_nms_format", output_stream->name(), output_stream->get_info().index),
        async_pipeline, remove_overlapping_bboxes_element.value());
    CHECK_EXPECTED_AS_STATUS(pre_fill_nms_format_element_queue_element);

    auto fill_nms_format_element = add_fill_nms_format_element(async_pipeline, output_stream, "FillNmsFormatElement", iou_op_metadata,
        output_format.second, true, pre_fill_nms_format_element_queue_element.value());
    CHECK_EXPECTED_AS_STATUS(fill_nms_format_element);

    auto last_async_element = add_last_async_element(async_pipeline, output_format.first, fill_nms_format_element.value());
    CHECK_EXPECTED_AS_STATUS(last_async_element);

    return HAILO_SUCCESS;
}

hailo_status AsyncInferRunnerImpl::add_nms_flows(AsyncPipeline &async_pipeline, OutputStreamPtrVector &output_streams,
    const std::pair<std::string, hailo_format_t> &output_format, const net_flow::PostProcessOpMetadataPtr &op_metadata,
    const std::vector<hailo_vstream_info_t> &vstreams_infos)
{
    assert(1 <= op_metadata->outputs_metadata().size());
    auto updated_outputs_metadata = op_metadata->outputs_metadata();
    std::pair<std::string, hailo_format_t> expanded_output_format = {output_format.first,
        net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(output_format.second, op_metadata->type())};
    updated_outputs_metadata.begin()->second.format = expanded_output_format.second;

    if (HAILO_FORMAT_FLAGS_QUANTIZED & updated_outputs_metadata.begin()->second.format.flags) {
        updated_outputs_metadata.begin()->second.format.flags &= ~HAILO_FORMAT_FLAGS_QUANTIZED;
        // TODO: Delete override when changing CLI default flags
        // TODO: check with Kimel/Salem of this warning is still needed
        LOGGER__WARNING("The output_vstream {} format flag is marked as quantized, which is not supported with {}. "
            "flag has been automatically set to False.", updated_outputs_metadata.begin()->first, op_metadata->get_name());
    }

    op_metadata->set_outputs_metadata(updated_outputs_metadata);
    CHECK_SUCCESS(op_metadata->validate_format_info());
    std::shared_ptr<hailort::net_flow::Op> op;

    switch (op_metadata->type()) {
    case net_flow::OperationType::IOU:
        return add_iou_flow(async_pipeline, output_streams, expanded_output_format, op_metadata);

    case net_flow::OperationType::YOLOX:
    {
        auto metadata = std::dynamic_pointer_cast<net_flow::YoloxOpMetadata>(op_metadata);
        assert(nullptr != metadata);
        auto op_expected = net_flow::YOLOXPostProcessOp::create(metadata);
        CHECK_EXPECTED_AS_STATUS(op_expected);
        op = op_expected.release();
        break;
    }
    case net_flow::OperationType::YOLOV5:
    {
        auto metadata = std::dynamic_pointer_cast<net_flow::Yolov5OpMetadata>(op_metadata);
        assert(nullptr != metadata);
        auto op_expected = net_flow::YOLOv5PostProcessOp::create(metadata);
        CHECK_EXPECTED_AS_STATUS(op_expected);
        op = op_expected.release();
        break;
    }
    case net_flow::OperationType::SSD:
    {
        auto metadata = std::dynamic_pointer_cast<net_flow::SSDOpMetadata>(op_metadata);
        assert(nullptr != metadata);
        auto op_expected = net_flow::SSDPostProcessOp::create(metadata);
        CHECK_EXPECTED_AS_STATUS(op_expected);
        op = op_expected.release();
        break;
    }
    default:
        break;
    }
    hailo_vstream_info_t output_vstream_info;
    for (auto &current_output_vstream_info : vstreams_infos) {
        if (current_output_vstream_info.name == op->outputs_metadata().begin()->first) {
            output_vstream_info = current_output_vstream_info;
        }
    }
    return add_nms_flow(async_pipeline, output_streams, expanded_output_format, op, output_vstream_info);
}

hailo_status AsyncInferRunnerImpl::add_ops_flows(AsyncPipeline &async_pipeline,
    const std::pair<std::string, hailo_format_t> &output_format, net_flow::PostProcessOpMetadataPtr &op_metadata,
    OutputStreamPtrVector &output_streams, const std::vector<hailo_vstream_info_t> &vstreams_infos)
{
    const bool is_dma_able_hw_async = true;
    auto status = async_pipeline.get_async_hw_element()->fill_buffer_pools(is_dma_able_hw_async);
    CHECK_SUCCESS(status);

    switch (op_metadata->type()) {
    case net_flow::OperationType::YOLOX:
    case net_flow::OperationType::SSD:
    case net_flow::OperationType::YOLOV5:
    case net_flow::OperationType::IOU:
    // TODO: add support for YOLOV5SEG
        return add_nms_flows(async_pipeline, output_streams, output_format, op_metadata, vstreams_infos);

    case net_flow::OperationType::ARGMAX:
        return add_argmax_flow(async_pipeline, output_streams, output_format, op_metadata);

    case net_flow::OperationType::SOFTMAX:
        return add_softmax_flow(async_pipeline, output_streams, output_format, op_metadata);

    default:
        LOGGER__ERROR("op type {} of op {} is not in any of the supported post process OP types", net_flow::OpMetadata::get_operation_type_str(op_metadata->type()), op_metadata->get_name());
        return HAILO_INVALID_OPERATION;
    }
}

hailo_status AsyncInferRunnerImpl::create_post_async_hw_elements(ConfiguredNetworkGroupBase &net_group,
        const std::unordered_map<std::string, hailo_format_t> &expanded_outputs_formats, std::unordered_map<std::string, hailo_format_t> &original_outputs_formats,
        AsyncPipeline &async_pipeline)
{
    // streams_added is a vector which holds all stream names which vstreams connected to them were already added (for demux cases)
    std::vector<std::string> streams_added;

    // Building DBs that connect output_vstreams, output_streams and ops.
    // Note: Assuming each post process op has a unique output streams.
    //       In other words, not possible for an output stream to be connected to more than one op
    std::unordered_map<std::string, net_flow::PostProcessOpMetadataPtr> post_process_metadata;
    std::unordered_map<stream_name_t, op_name_t> op_inputs_to_op_name;
    for (auto &metadata : net_group.get_ops_metadata().release()) {
        post_process_metadata.insert({metadata->get_name(), metadata});
        for (auto &input_name : metadata->get_input_names()) {
            op_inputs_to_op_name.insert({input_name, metadata->get_name()});
        }
    }

    for (auto &output_format : expanded_outputs_formats) {
        auto output_streams_expected = net_group.get_output_streams_by_vstream_name(output_format.first);
        CHECK_EXPECTED_AS_STATUS(output_streams_expected);

        auto first_stream_info = output_streams_expected.value()[0]->get_info();
        if (contains(streams_added, static_cast<std::string>(first_stream_info.name))) {
            continue;
        }
        for (auto &output_stream : output_streams_expected.value()) {
            streams_added.push_back(output_stream->get_info().name);
        }

        if (contains(op_inputs_to_op_name, static_cast<std::string>(first_stream_info.name))) {
            auto &op_name = op_inputs_to_op_name.at(first_stream_info.name);
            auto &op_metadata = post_process_metadata.at(op_name);

            auto output_vstreams_infos = net_group.get_output_vstream_infos();
            CHECK_EXPECTED_AS_STATUS(output_vstreams_infos);

            std::pair<std::string, hailo_format_t> original_output_format = {output_format.first, original_outputs_formats.at(output_format.first)};

            hailo_status status = add_ops_flows(async_pipeline, original_output_format,
                op_metadata, output_streams_expected.value(), output_vstreams_infos.value());
            CHECK_SUCCESS(status);

        } else if ((HAILO_FORMAT_ORDER_HAILO_NMS == first_stream_info.format.order) &&
            (first_stream_info.nms_info.is_defused)) {
            // Case defuse NMS
            hailo_status status = add_nms_fuse_flow(output_streams_expected.value(), output_format, async_pipeline);
            CHECK_SUCCESS(status);
        } else if (first_stream_info.is_mux) {
            // case demux in output from NN core (only one output stream is currently suppored)
            hailo_status status = add_output_demux_flow(output_streams_expected.value()[0], async_pipeline, expanded_outputs_formats);
            CHECK_SUCCESS(status);
        } else {
            // case simple and single output from NN core to user (and transformation at best)
            auto output_stream_base = std::static_pointer_cast<OutputStreamBase>(output_streams_expected.value()[0]);
            const bool is_dma_able = true;
            auto final_elem_source_index = async_pipeline.get_async_hw_element()->get_source_index_from_output_stream_name(output_stream_base->name());
            CHECK_EXPECTED_AS_STATUS(final_elem_source_index);

            hailo_status status = finalize_output_flow(output_stream_base, output_format, {}, is_dma_able, async_pipeline,
                async_pipeline.get_async_hw_element(), final_elem_source_index.value());
            CHECK_SUCCESS(status);
        }
    }
    return HAILO_SUCCESS;
}

Expected<AsyncPipeline> AsyncInferRunnerImpl::create_pipeline(ConfiguredNetworkGroupBase &net_group,
    const std::unordered_map<std::string, hailo_format_t> &inputs_formats,
    const std::unordered_map<std::string, hailo_format_t> &outputs_formats,
    const uint32_t timeout)
{
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> entry_elements;
    std::unordered_map<std::string, std::shared_ptr<PipelineElement>> last_elements;

    ElementBuildParams build_params;

    // buffer_pool_size should be the minimum of the maximum queue size of all LL streams (input and output)
    auto buffer_pool_size_expected = get_min_buffer_pool_size(net_group);
    CHECK_EXPECTED(buffer_pool_size_expected);
    build_params.buffer_pool_size = buffer_pool_size_expected.release();
    build_params.elem_stats_flags = HAILO_PIPELINE_ELEM_STATS_NONE;
    build_params.vstream_stats_flags = HAILO_VSTREAM_STATS_NONE;

    auto async_pipeline_expected = AsyncPipeline::create();
    CHECK_EXPECTED(async_pipeline_expected);
    auto async_pipeline = async_pipeline_expected.release();

    auto input_streams_expected = get_input_streams_from_net_group(net_group, inputs_formats);
    CHECK_EXPECTED(input_streams_expected);

    auto input_expanded_format = expand_auto_input_formats(net_group, inputs_formats);
    CHECK_EXPECTED(input_expanded_format);

    std::vector<std::shared_ptr<InputStream>> input_streams_list;
    input_streams_list.reserve(input_streams_expected.value().size());
    for (auto &input_stream : input_streams_expected.value()) {
        input_streams_list.push_back(input_stream.second);
    }

    auto output_streams_expected = get_output_streams_from_net_group(net_group, outputs_formats);
    CHECK_EXPECTED(output_streams_expected);

    auto output_expanded_format = expand_auto_output_formats(net_group, outputs_formats);
    CHECK_EXPECTED(output_expanded_format);

    auto outputs_original_formats = outputs_formats;  // The original formats is needed for specific format expanding (required for PP OPs, like argmax)

    std::vector<std::shared_ptr<OutputStream>> output_streams_list;
    output_streams_list.reserve(output_streams_expected.value().size());
    for (auto &output_stream : output_streams_expected.value()) {
        output_streams_list.push_back(output_stream.second);
    }

    auto shutdown_event_expected = Event::create_shared(Event::State::not_signalled);
    CHECK_EXPECTED(shutdown_event_expected);

    build_params.shutdown_event = shutdown_event_expected.release();
    build_params.pipeline_status = make_shared_nothrow<std::atomic<hailo_status>>(HAILO_SUCCESS);
    CHECK_ARG_NOT_NULL_AS_EXPECTED(build_params.pipeline_status);
    build_params.timeout = std::chrono::milliseconds(timeout);

    async_pipeline.set_build_params(build_params);

    // all elements in async pipeline start as last elements, and in the end of this func all non-last-copy elements will be added buffers
    bool is_last_copy_element = true;

    auto async_hw_elem = AsyncHwElement::create(input_streams_list, output_streams_list, build_params.timeout,
        build_params.buffer_pool_size, build_params.elem_stats_flags,
        build_params.vstream_stats_flags, build_params.shutdown_event,
        "AsyncHwElement", build_params.pipeline_status, PipelineDirection::PUSH, is_last_copy_element);
    CHECK_EXPECTED(async_hw_elem);
    async_pipeline.add_element_to_pipeline(async_hw_elem.value());
    async_pipeline.set_async_hw_element(async_hw_elem.release());

    // TODO: HRT-11759
    hailo_status status = create_pre_async_hw_elements(net_group, input_streams_expected.value(), input_expanded_format.value(),
        async_pipeline);
    CHECK_SUCCESS_AS_EXPECTED(status);

    status = create_post_async_hw_elements(net_group, output_expanded_format.value(), outputs_original_formats, async_pipeline);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return async_pipeline;
}

std::vector<std::shared_ptr<PipelineElement>> AsyncInferRunnerImpl::get_pipeline() const
{
    return m_async_pipeline.get_pipeline();
}

std::string AsyncInferRunnerImpl::get_pipeline_description() const
{
    std::stringstream pipeline_str;
    pipeline_str << "Async infer pipeline description:\n";
    for (const auto &element : get_pipeline()) {
	    pipeline_str << " >> " << element->description();
    }
    return pipeline_str.str();
}

} /* namespace hailort */
