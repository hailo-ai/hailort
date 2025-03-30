/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file inference_pipeline.cpp
 * @brief Implemention of inference pipeline
 **/

#include "hailo/inference_pipeline.hpp"
#include "hailo/hailort_defaults.hpp"

#include "common/async_thread.hpp"

#include "net_flow/pipeline/vstream_internal.hpp"
#include "network_group/network_group_internal.hpp"
#include "core_op/resource_manager/resource_manager.hpp"

#include <sstream>


namespace hailort
{

InferVStreams::InferVStreams(std::vector<InputVStream> &&inputs, std::vector<OutputVStream> &&outputs, bool is_multi_context,
    bool is_scheduled, uint16_t batch_size) :
    m_inputs(std::move(inputs)),
    m_outputs(std::move(outputs)),
    m_is_multi_context(is_multi_context),
    m_is_scheduled(is_scheduled),
    m_batch_size(batch_size)
{
    for (auto &input : m_inputs) {
        if (contains(m_network_name_to_input_count, input.network_name())) {
            ++m_network_name_to_input_count[input.network_name()];
        } else {
            m_network_name_to_input_count.emplace(input.network_name(), 1);
        }
    }
    for (auto &output : m_outputs) {
        if (contains(m_network_name_to_output_count, output.network_name())) {
            ++m_network_name_to_output_count[output.network_name()];
        } else {
            m_network_name_to_output_count.emplace(output.network_name(), 1);
        }
    }
}

hailo_status InferVStreams::verify_network_inputs_and_outputs(const std::map<std::string, MemoryView>& inputs_name_mem_view_map,
                                                   const std::map<std::string, MemoryView>& outputs_name_mem_view_map)
{
    std::map<std::string, std::pair<size_t, size_t>> input_output_count_per_network;

    for (const auto &input_name_to_memview : inputs_name_mem_view_map) {
        auto input_vstream = get_input_by_name(input_name_to_memview.first);
        CHECK_EXPECTED_AS_STATUS(input_vstream);
        auto network_name = input_vstream->get().network_name();
        if (contains(input_output_count_per_network, network_name)) {
            ++input_output_count_per_network[network_name].first;
        } else {
            input_output_count_per_network.emplace(network_name, std::pair<size_t, size_t>(1, 0));
        }
    }
    for (const auto &output_name_to_memview : outputs_name_mem_view_map) {
        auto output_vstream = get_output_by_name(output_name_to_memview.first);
        CHECK_EXPECTED_AS_STATUS(output_vstream);
        auto network_name = output_vstream->get().network_name();
        if (contains(input_output_count_per_network, network_name)) {
            ++input_output_count_per_network[network_name].second;
        } else {
            input_output_count_per_network.emplace(network_name, std::pair<size_t, size_t>(0, 1));
        }
    }
    CHECK(!m_is_multi_context || (input_output_count_per_network.size() == m_network_name_to_input_count.size()), HAILO_INVALID_ARGUMENT,
        "For multi-context network groups, inference is only supported on all available networks");

    for (const auto &network_to_input_output_count : input_output_count_per_network) {
        CHECK(network_to_input_output_count.second.first == m_network_name_to_input_count[network_to_input_output_count.first],
            HAILO_INVALID_ARGUMENT, "Not all inputs have been provided for network {}", network_to_input_output_count.first);
        CHECK(network_to_input_output_count.second.second == m_network_name_to_output_count[network_to_input_output_count.first],
            HAILO_INVALID_ARGUMENT, "Not all outputs have been provided for network {}", network_to_input_output_count.first);
    }
    return HAILO_SUCCESS;
}

static hailo_status verify_vstream_params_in_vstream_infos(const std::map<std::string, hailo_vstream_params_t> &params,
    const std::vector<hailo_vstream_info_t> &vstream_infos)
{
    for (const auto &name_to_param : params) {
        const auto &name = name_to_param.first;
        bool found = false;
        for (const auto &vstream_info : vstream_infos) {
            if (vstream_info.name == name) {
                found = true;
                break;
            }
        }
        CHECK(found, HAILO_NOT_FOUND, "Could not find vstream {}", name);
    }
    return HAILO_SUCCESS;
}

Expected<InferVStreams> InferVStreams::create(ConfiguredNetworkGroup &net_group,
        const std::map<std::string, hailo_vstream_params_t> &input_params,
        const std::map<std::string, hailo_vstream_params_t> &output_params)
{
    auto network_infos = net_group.get_network_infos();
    CHECK_EXPECTED(network_infos);

    auto is_multi_context = net_group.is_multi_context();
    std::map<std::string, std::pair<size_t, size_t>> input_param_count_per_network;
    size_t total_inputs_found = 0;
    size_t total_outputs_found = 0;

    uint16_t batch_size = 0;
    if (is_multi_context) {
        const auto &config_params = net_group.get_config_params();
        batch_size = config_params.batch_size;

        if (HAILO_DEFAULT_BATCH_SIZE == batch_size) {
            uint16_t network_batch_size = config_params.network_params_by_name.begin()->second.batch_size;
            for (const auto &name_params_pair : config_params.network_params_by_name) {
                CHECK_AS_EXPECTED(network_batch_size == name_params_pair.second.batch_size, HAILO_INVALID_ARGUMENT,
                    "Batch size of each network must be the same!");
            }

            batch_size = network_batch_size;
        }
    }

    if (HAILO_DEFAULT_BATCH_SIZE == batch_size) {
        batch_size = DEFAULT_ACTUAL_BATCH_SIZE;
    }

    for (const auto &network_info : network_infos.value()) {
        auto input_vstream_infos_per_network = net_group.get_input_vstream_infos(network_info.name);
        CHECK_EXPECTED(input_vstream_infos_per_network);

        size_t input_counter = 0;
        for (const auto &vstream_info : input_vstream_infos_per_network.value()) {
            if (contains(input_params, std::string(vstream_info.name))) {
                ++input_counter;
                ++total_inputs_found;
            }
        }

        auto output_vstream_infos_per_network = net_group.get_output_vstream_infos(network_info.name);
        CHECK_EXPECTED(output_vstream_infos_per_network);

        size_t output_counter = 0;
        for (const auto &vstream_info : output_vstream_infos_per_network.value()) {
            if (contains(output_params, std::string(vstream_info.name))) {
                ++output_counter;
                ++total_outputs_found;
            }
        }

        if ((0 != input_counter) || (0 != output_counter)) {
            CHECK_AS_EXPECTED(input_counter == input_vstream_infos_per_network->size(), HAILO_INVALID_ARGUMENT,
                "Found only partial inputs for network {}", network_info.name);
            CHECK_AS_EXPECTED(output_counter == output_vstream_infos_per_network->size(), HAILO_INVALID_ARGUMENT,
                "Found only partial outputs for network {}", network_info.name);
        } else {
            CHECK_AS_EXPECTED(!is_multi_context, HAILO_INVALID_ARGUMENT,
                "For multi-context network groups, the pipeline must be created for all available networks");
        }
    }

    if (total_inputs_found != input_params.size()) {
        auto all_input_vstream_infos = net_group.get_input_vstream_infos();
        CHECK_EXPECTED(all_input_vstream_infos);

        auto status = verify_vstream_params_in_vstream_infos(input_params, all_input_vstream_infos.release());
        CHECK_SUCCESS_AS_EXPECTED(status);
    }
    if (total_outputs_found != output_params.size()) {
        auto all_output_vstream_infos = net_group.get_output_vstream_infos();
        CHECK_EXPECTED(all_output_vstream_infos);

        auto status = verify_vstream_params_in_vstream_infos(output_params, all_output_vstream_infos.release());
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    auto input_vstreams = VStreamsBuilder::create_input_vstreams(net_group, input_params);
    CHECK_EXPECTED(input_vstreams);

    auto output_vstreams = VStreamsBuilder::create_output_vstreams(net_group, output_params);
    CHECK_EXPECTED(output_vstreams);

    return InferVStreams(input_vstreams.release(), output_vstreams.release(), is_multi_context, net_group.is_scheduled(),
        batch_size);
}

hailo_status InferVStreams::infer(const std::map<std::string, MemoryView>& input_data,
    std::map<std::string, MemoryView>& output_data, size_t frames_count)
{
    auto status = verify_network_inputs_and_outputs(input_data, output_data);
    CHECK_SUCCESS(status);

    status = verify_memory_view_size(input_data, output_data, frames_count);
    CHECK_SUCCESS(status);

    status = verify_frames_count(frames_count);
    CHECK_SUCCESS(status);

    std::vector<AsyncThreadPtr<hailo_status>> results;

    // Launch async read/writes
    for (auto &input_name_to_data_pair : input_data) {
        auto input_vstream_exp = get_input_by_name(input_name_to_data_pair.first);
        CHECK_EXPECTED_AS_STATUS(input_vstream_exp);
        auto &input_vstream = input_vstream_exp.release().get();
        results.emplace_back(std::make_unique<AsyncThread<hailo_status>>(
            [&input_vstream, &input_name_to_data_pair, frames_count]() -> hailo_status {
                const auto &input_buffer = input_name_to_data_pair.second;
                for (uint32_t i = 0; i < frames_count; i++) {
                    const size_t offset = i * input_vstream.get_frame_size();
                    auto status = input_vstream.write(MemoryView::create_const(
                        input_buffer.data() + offset,
                        input_vstream.get_frame_size()));
                    if (HAILO_STREAM_ABORT == status) {
                        LOGGER__DEBUG("Input stream was aborted!");
                        return status;
                    }
                    CHECK_SUCCESS(status);
                }
                return HAILO_SUCCESS;
            }
        ));
    }
    for (auto &output_name_to_data_pair : output_data) {
        auto output_vstream_exp = get_output_by_name(output_name_to_data_pair.first);
        CHECK_EXPECTED_AS_STATUS(output_vstream_exp);
        auto &output_vstream = output_vstream_exp.release().get();
        results.emplace_back(std::make_unique<AsyncThread<hailo_status>>(
            [&output_vstream, &output_name_to_data_pair, frames_count]() {
                for (size_t i = 0; i < frames_count; i++) {
                    auto status = output_vstream.read(MemoryView(output_name_to_data_pair.second.data() + i * output_vstream.get_frame_size(), output_vstream.get_frame_size()));
                    if (HAILO_SUCCESS != status) {
                        return status;
                    }
                }
                return HAILO_SUCCESS;
            }
        ));
    }

    // Wait for all results
    auto error_status = HAILO_SUCCESS;
    for (auto& result : results) {
        status = result->get();
        if (HAILO_STREAM_ABORT == status) {
            continue;
        }
        if (HAILO_SUCCESS != status) {
            error_status = status;
            LOGGER__ERROR("Failed waiting for threads with status {}", error_status);
        }
    }
    if (HAILO_SUCCESS != error_status) {
        return error_status;
    }

    return HAILO_SUCCESS;
}

hailo_status InferVStreams::verify_memory_view_size(const std::map<std::string, MemoryView>& inputs_name_mem_view_map,
    const std::map<std::string, MemoryView>& outputs_name_mem_view_map, size_t frames_count)
{
    for (const auto &input_name_to_memview : inputs_name_mem_view_map) {
        auto input_vstream_exp = get_input_by_name(input_name_to_memview.first);
        CHECK_EXPECTED_AS_STATUS(input_vstream_exp);
        auto &input_vstream = input_vstream_exp.release().get();
        CHECK(frames_count * input_vstream.get_frame_size() == input_name_to_memview.second.size(), HAILO_INVALID_ARGUMENT,
            "Memory size of vstream {} does not match the frame count! (Expected {}, got {})",
            input_vstream.name(), frames_count * input_vstream.get_frame_size(), input_name_to_memview.second.size());
    }
    for (const auto &output_name_to_memview : outputs_name_mem_view_map) {
        auto output_vstream_exp = get_output_by_name(output_name_to_memview.first);
        CHECK_EXPECTED_AS_STATUS(output_vstream_exp);
        auto &output_vstream = output_vstream_exp.release().get();
        CHECK(frames_count * output_vstream.get_frame_size() == output_name_to_memview.second.size(), HAILO_INVALID_ARGUMENT,
            "Memory size of vstream {} does not match the frame count! (Expected {}, got {})",
            output_vstream.name(), frames_count * output_vstream.get_frame_size(), output_name_to_memview.second.size());
    }

    return HAILO_SUCCESS;
}

hailo_status InferVStreams::verify_frames_count(size_t frames_count)
{
    if (m_is_multi_context && !m_is_scheduled) {
        CHECK((frames_count % m_batch_size) == 0, HAILO_INVALID_ARGUMENT,
            "On the case of multi-context without the model scheduler, frames count must be a multiplier of the batch size! ({} % {} != 0)",
            frames_count, m_batch_size);
    }
    return HAILO_SUCCESS;
}

Expected<std::reference_wrapper<InputVStream>> InferVStreams::get_input_by_name(const std::string &name)
{
    for (auto &input_vstream : m_inputs) {
        if (input_vstream.name() == name) {
            return std::ref(input_vstream);
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

Expected<std::reference_wrapper<OutputVStream>> InferVStreams::get_output_by_name(const std::string &name)
{
    for (auto &ouput_vstream : m_outputs) {
        if (ouput_vstream.name() == name) {
            return std::ref(ouput_vstream);
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

std::vector<std::reference_wrapper<InputVStream>> InferVStreams::get_input_vstreams()
{
    std::vector<std::reference_wrapper<InputVStream>> vsterams_refs;
    for (auto &input_vstream : m_inputs) {
        vsterams_refs.push_back(std::ref(input_vstream));
    }
    return vsterams_refs;
}

std::vector<std::reference_wrapper<OutputVStream>> InferVStreams::get_output_vstreams()
{
    std::vector<std::reference_wrapper<OutputVStream>> vsterams_refs;
    for (auto &ouput_vstream : m_outputs) {
        vsterams_refs.push_back(std::ref(ouput_vstream));
    }
    return vsterams_refs;
}

hailo_status InferVStreams::set_nms_score_threshold(float32_t threshold)
{
    // Check that we have NMS outputs in the model
    auto has_nms_output = std::any_of(m_outputs.begin(), m_outputs.end(), [](const auto &vs)
    {
        return HailoRTCommon::is_nms(vs.get_info());
    });
    CHECK(has_nms_output, HAILO_INVALID_OPERATION, "'set_nms_score_threshold()' is called, but there is no NMS output in this model.");

    for (auto &ouput_vstream : m_outputs) {
        if (HailoRTCommon::is_nms(ouput_vstream.get_info())) {
            CHECK_SUCCESS(ouput_vstream.set_nms_score_threshold(threshold));
        }
    }

    return HAILO_SUCCESS;
}

hailo_status InferVStreams::set_nms_iou_threshold(float32_t threshold)
{
    // Check that we have NMS outputs in the model
    auto has_nms_output = std::any_of(m_outputs.begin(), m_outputs.end(), [](const auto &vs)
    {
        return HailoRTCommon::is_nms(vs.get_info());
    });
    CHECK(has_nms_output, HAILO_INVALID_OPERATION, "'set_nms_iou_threshold()' is called, but there is no NMS output in this model.");

    for (auto &ouput_vstream : m_outputs) {
        if (HailoRTCommon::is_nms(ouput_vstream.get_info())) {
            CHECK_SUCCESS(ouput_vstream.set_nms_iou_threshold(threshold));
        }
    }

    return HAILO_SUCCESS;
}

hailo_status InferVStreams::set_nms_max_proposals_per_class(uint32_t max_proposals_per_class)
{
    // Check that we have NMS outputs in the model
    auto has_nms_by_class_output = std::any_of(m_outputs.begin(), m_outputs.end(), [](const auto &vs)
    {
        return ((HailoRTCommon::is_nms(vs.get_info())) && (HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE != vs.get_info().format.order));
    });
    CHECK(has_nms_by_class_output, HAILO_INVALID_OPERATION, "'set_nms_max_proposals_per_class()' is called, but there is no NMS ordered by class output in this model.");

    for (auto &ouput_vstream : m_outputs) {
        if (HailoRTCommon::is_nms(ouput_vstream.get_info())) {
            CHECK_SUCCESS(ouput_vstream.set_nms_max_proposals_per_class(max_proposals_per_class));
        }
    }

    return HAILO_SUCCESS;
}

hailo_status InferVStreams::set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size)
{
    auto has_mask_output = false;
    for (auto &ouput_vstream : m_outputs) {
        if (HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK == ouput_vstream.get_info().format.order) {
            has_mask_output = true;
            CHECK_SUCCESS(ouput_vstream.set_nms_max_accumulated_mask_size(max_accumulated_mask_size));
        }
    }
    CHECK(has_mask_output, HAILO_INVALID_OPERATION,
        "'set_nms_max_accumulated_mask_size()' is called, but there is no NMS WITH BYTE MASK output in this model.");

    return HAILO_SUCCESS;
}

} /* namespace hailort */
