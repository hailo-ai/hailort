/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file inference_pipeline.hpp
 * @brief Pipeline used to run inference, builds all necessary flags
 **/

#ifndef _HAILO_INFERENCE_PIPELINE_HPP_
#define _HAILO_INFERENCE_PIPELINE_HPP_

#include "hailo/vstream.hpp"


/** hailort namespace */
namespace hailort
{

/*! Pipeline used to run inference */
// TODO: HRT-3157 - Fix doc after multi-network support.
class HAILORTAPI InferVStreams final
{
public:

    /**
     * Creates vstreams pipelines to be used later for inference by calling the InferVStreams::infer() function. 
     *
     * @param[in] net_group                    A ConfiguredNetworkGroup to run the inference on.
     * @param[in] input_params                 A mapping of input vstream name to its' params. Can be achieved by calling 
     *                                         ConfiguredNetworkGroup::make_input_vstream_params() or Hef::make_input_vstream_params
     *                                         functions.
     * @param[in] output_params                A mapping of output vstream name to its' params. Can be achieved by calling 
     *                                         ConfiguredNetworkGroup::make_output_vstream_params() or Hef::make_output_vstream_params()
     *                                         functions.
     * @return Upon success, returns Expected of InferVStreams. Otherwise, returns Unexpected of ::hailo_status error.
     * @note If at least one input/output of some network is present, all inputs and outputs of that network must also be present.
     */
    static Expected<InferVStreams> create(ConfiguredNetworkGroup &net_group,
        const std::map<std::string, hailo_vstream_params_t> &input_params,
        const std::map<std::string, hailo_vstream_params_t> &output_params);

    /**
     * Run inference on dataset @a input_data.
     *
     * @param[in] input_data                    A mapping of vstream name to MemoryView containing input dataset for inference.
     * @param[out] output_data                  A mapping of vstream name to MemoryView containing the inference output data. 
     * @param[in] frames_count                  The amount of inferred frames.
     * 
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note ConfiguredNetworkGroup must be activated before calling this function.
     * @note The size of each element in @a input_data and @a output_data must match the frame size
     *       of the matching vstream name multiplied by @a frames_count.
     * @note If at least one input/output of some network is present, all inputs and outputs of that network must also be present.
     */
    hailo_status infer(const std::map<std::string, MemoryView>& input_data,
                       std::map<std::string, MemoryView>& output_data, size_t frames_count);

    /**
     * Get InputVStream by name.
     *
     * @param[in] name      The vstream's name.
     * @return Upon success, returns Expected of InputVStream. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::reference_wrapper<InputVStream>> get_input_by_name(const std::string &name);

    /**
     * Get OutputVStream by name.
     *
     * @param[in] name      The vstream's name.
     * @return Upon success, returns Expected of OutputVStream. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::reference_wrapper<OutputVStream>> get_output_by_name(const std::string &name);

    /**
     * @return Returns a vector of all InputVStream%s.
     */
    std::vector<std::reference_wrapper<InputVStream>> get_input_vstreams();

    /**
     * @return Returns a vector of all OutputVStream%s.
     */
    std::vector<std::reference_wrapper<OutputVStream>> get_output_vstreams();

    /**
     * Set NMS score threshold, used for filtering out candidates. Any box with score<TH is suppressed.
     *
     * @param[in] threshold        NMS score threshold to set.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function will fail in cases where there is no output with NMS operations on the CPU.
     */
    hailo_status set_nms_score_threshold(float32_t threshold);

    /**
     * Set NMS intersection over union overlap Threshold,
     * used in the NMS iterative elimination process where potential duplicates of detected items are suppressed.
     *
     * @param[in] threshold        NMS IoU threshold to set.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function will fail in cases where there is no output with NMS operations on the CPU.
     */
    hailo_status set_nms_iou_threshold(float32_t threshold);

    /**
     * Set a limit for the maximum number of boxes per class.
     *
     * @param[in] max_proposals_per_class    NMS max proposals per class to set.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note This function must be called before starting inference!
     * This function will fail in cases where there is no output with NMS operations on the CPU.
     */
    hailo_status set_nms_max_proposals_per_class(uint32_t max_proposals_per_class);

    /**
     * Set maximum accumulated mask size for all the detections in a frame.
     *
     * Note: Used in order to change the output buffer frame size,
     * in cases where the output buffer is too small for all the segmentation detections.
     *
     * @param[in] max_accumulated_mask_size NMS max accumulated mask size.
     * @note This function must be called before starting inference!
     * This function will fail in cases where the output vstream has no NMS operations on the CPU.
     */
    hailo_status set_nms_max_accumulated_mask_size(uint32_t max_accumulated_mask_size);

    InferVStreams(const InferVStreams &other) = delete;
    InferVStreams &operator=(const InferVStreams &other) = delete;
    InferVStreams &operator=(InferVStreams &&other) = delete;
    InferVStreams(InferVStreams &&other) :
        m_inputs(std::move(other.m_inputs)),
        m_outputs(std::move(other.m_outputs)),
        m_is_multi_context(std::move(other.m_is_multi_context)),
        m_is_scheduled(std::move(other.m_is_scheduled)),
        m_network_name_to_input_count(std::move(other.m_network_name_to_input_count)),
        m_network_name_to_output_count(std::move(other.m_network_name_to_output_count)),
        m_batch_size(std::move(other.m_batch_size))
        {};
private:
    InferVStreams(std::vector<InputVStream> &&inputs, std::vector<OutputVStream> &&outputs, bool is_multi_context,
        bool is_scheduled, uint16_t batch_size);
    hailo_status verify_network_inputs_and_outputs(const std::map<std::string, MemoryView>& inputs_name_mem_view_map,
                                                   const std::map<std::string, MemoryView>& outputs_name_mem_view_map);
    hailo_status verify_memory_view_size(const std::map<std::string, MemoryView>& inputs_name_mem_view_map,
                                         const std::map<std::string, MemoryView>& outputs_name_mem_view_map,
                                         size_t frames_count);
    hailo_status verify_frames_count(size_t frames_count);

    std::vector<InputVStream> m_inputs;
    std::vector<OutputVStream> m_outputs;
    const bool m_is_multi_context;
    const bool m_is_scheduled;
    std::map<std::string, size_t> m_network_name_to_input_count;
    std::map<std::string, size_t> m_network_name_to_output_count;
    uint16_t m_batch_size;
};

} /* namespace hailort */

#endif /* _HAILO_INFERENCE_PIPELINE_HPP_ */
