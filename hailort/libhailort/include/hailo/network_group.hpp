/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file network_group.hpp
 * @brief A module describing the base classes for ConfiguredNetworkGroup and ActivatedNetworkGroup 
 **/

#ifndef _HAILO_NETWORK_GROUP_HPP_
#define _HAILO_NETWORK_GROUP_HPP_

#include "hailo/stream.hpp"
#include "hailo/runtime_statistics.hpp"
#include "hailo/hef.hpp"

#include <string>
#include <map>
#include <unordered_map>
#include <mutex>
#include <condition_variable>

/** hailort namespace */
namespace hailort
{

namespace net_flow {
class OpMetadata;
using PostProcessOpMetadataPtr = std::shared_ptr<OpMetadata>;
}

enum class BufferType
{
    UNINITIALIZED,
    VIEW,
    PIX_BUFFER,
    DMA_BUFFER,
};

struct BufferRepresentation {
    BufferType buffer_type;
    union {
        MemoryView view;
        hailo_dma_buffer_t dma_buffer;
    };
};

using NamedBuffersCallbacks = std::unordered_map<std::string, std::pair<BufferRepresentation, std::function<void(hailo_status)>>>;

class InputVStream;
class OutputVStream;
struct LayerInfo;


/** @addtogroup group_type_definitions */
/*@{*/

/** Represents a vector of InputStream */
using InputStreamRefVector = std::vector<std::reference_wrapper<InputStream>>;

/** Represents a vector of OutputStream */
using OutputStreamRefVector = std::vector<std::reference_wrapper<OutputStream>>;

/** Represents a mapping of vstream name to its' params */
using NameToVStreamParamsMap = std::unordered_map<std::string, hailo_vstream_params_t>;

/** Represents a vector of pairs of OutputStream and NameToVStreamParamsMap */
using OutputStreamWithParamsVector = std::vector<std::pair<std::shared_ptr<OutputStream>, NameToVStreamParamsMap>>;

/** Latency measurement result info */
struct LatencyMeasurementResult {
    std::chrono::nanoseconds avg_hw_latency;
};

struct HwInferResults {
    uint16_t batch_count;
    size_t total_transfer_size;
    size_t total_frames_passed;
    float32_t time_sec;
    float32_t fps;
    float32_t BW_Gbps;
};
/*@}*/

using src_context_t = uint16_t;
using src_stream_index_t = uint8_t;
using IntermediateBufferKey = std::pair<src_context_t, src_stream_index_t>;

/*! Activated network_group that can be used to send/receive data */
class HAILORTAPI ActivatedNetworkGroup
{
public:
    virtual ~ActivatedNetworkGroup() = default;
    ActivatedNetworkGroup(const ActivatedNetworkGroup &other) = delete;
    ActivatedNetworkGroup &operator=(const ActivatedNetworkGroup &other) = delete;
    ActivatedNetworkGroup &operator=(ActivatedNetworkGroup &&other) = delete;
    ActivatedNetworkGroup(ActivatedNetworkGroup &&other) noexcept = default;

    /**
     * @return The network group name.
     */
    virtual const std::string& get_network_group_name() const = 0;

    virtual Expected<Buffer> get_intermediate_buffer(const IntermediateBufferKey &key) = 0;

    /**
     * @return The number of invalid frames.
     */
    virtual uint32_t get_invalid_frames_count() = 0;

protected:
    ActivatedNetworkGroup() = default;
};

/*! Loaded network_group that can be activated */
class HAILORTAPI ConfiguredNetworkGroup
{
public:
    virtual ~ConfiguredNetworkGroup() = default;
    ConfiguredNetworkGroup(const ConfiguredNetworkGroup &other) = delete;
    ConfiguredNetworkGroup &operator=(const ConfiguredNetworkGroup &other) = delete;
    ConfiguredNetworkGroup &operator=(ConfiguredNetworkGroup &&other) = delete;
    ConfiguredNetworkGroup(ConfiguredNetworkGroup &&other) noexcept = delete;

    /**
     * @return The network group name.
     */
    virtual const std::string& get_network_group_name() const
        DEPRECATED("'get_network_group_name' is deprecated. One should use 'name()'.") = 0;

    /**
     * @return The network group name.
     */
    virtual const std::string& name() const = 0;

    /**
     * Gets the stream's default interface.
     * 
     * @return Upon success, returns Expected of ::hailo_stream_interface_t.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() = 0;

    /**
     * Gets all input streams with the given @a interface.
     * 
     * @param[in] stream_interface         A ::hailo_stream_interface_t indicating which InputStream to return.
     * @return Upon success, returns a vector of InputStream.
     */
    virtual std::vector<std::reference_wrapper<InputStream>> get_input_streams_by_interface(hailo_stream_interface_t stream_interface) = 0;

    /**
     * Gets all output streams with the given @a interface.
     * 
     * @param[in] stream_interface         A ::hailo_stream_interface_t indicating which OutputStream to return.
     * @return Upon success, returns a vector of OutputStream.
     */
    virtual std::vector<std::reference_wrapper<OutputStream>> get_output_streams_by_interface(hailo_stream_interface_t stream_interface) = 0;

    /**
     * Gets input stream by stream name.
     *
     * @param[in] name                  The name of the input stream to retrieve.
     * @return Upon success, returns ExpectedRef of InputStream.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual ExpectedRef<InputStream> get_input_stream_by_name(const std::string &name) = 0;

    /**
     * Gets output stream by stream name.
     *
     * @param[in] name                  The name of the input stream to retrieve.
     * @return Upon success, returns ExpectedRef of OutputStream.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual ExpectedRef<OutputStream> get_output_stream_by_name(const std::string &name) = 0;

    /**
     *
     * @param[in]  network_name             Network name of the requested input streams.
     *                                      If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of vector InputStream.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<InputStreamRefVector> get_input_streams_by_network(const std::string &network_name="") = 0;

    /**
     *
     * @param[in]  network_name             Network name of the requested output streams.
     *                                      If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of vector OutputStream.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<OutputStreamRefVector> get_output_streams_by_network(const std::string &network_name="") = 0;

    /**
     * @return All input streams.
     */
    virtual InputStreamRefVector get_input_streams() = 0;

    /**
     * @return All output streams.
     */
    virtual OutputStreamRefVector get_output_streams() = 0;

    /**
     *
     * @param[in]  network_name             Network name of the requested latency measurement.
     *                                      If not passed, all the networks in the network group will be addressed,
     *                                      and the resulted measurement is avarage latency of all networks.
     * @return Upon success, returns Expected of LatencyMeasurementResult object containing the output latency result.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<LatencyMeasurementResult> get_latency_measurement(const std::string &network_name="") = 0;

    /**
     * Gets output streams and their vstream params from vstreams names.
     *
     * @param[in] outputs_params            Map of output vstream name and params.
     * @return Upon success, returns Expected of OutputStreamWithParamsVector.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     * @note The output streams returned here are streams that corresponds to the names in outputs_params.
     * If outputs_params contains a demux edge name, then all its predecessors names must be in outputs_params as well
     * and the return value will contain the stream that corresponds to those edges.
     */
    virtual Expected<OutputStreamWithParamsVector> get_output_streams_from_vstream_names(
        const std::map<std::string, hailo_vstream_params_t> &outputs_params) = 0;

    /**
     * Activates hailo device inner-resources for inference.

     * @return Upon success, returns Expected of a pointer to ActivatedNetworkGroup object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::unique_ptr<ActivatedNetworkGroup>> activate();

    /**
     * Activates hailo device inner-resources for inference.
     *
     * @param[in] network_group_params         Parameters for the activation.
     * @return Upon success, returns Expected of a pointer to ActivatedNetworkGroup object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate(const hailo_activate_network_group_params_t &network_group_params) = 0;

    /**
     * Block until network group is activated, or until timeout is passed.
     *
     * @param[in] timeout                 The timeout in milliseconds. If @a timeout is zero, the function returns immediately.
     *                                    If @a timeout is HAILO_INFINITE, the function returns only when the event is
     *                                    signaled.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     */
    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout) = 0;

    /**
     * Shutdown the network group. Makes sure all ongoing async operations are canceled. All async callbacks
     * of transfers that have not been completed will be called with status ::HAILO_STREAM_ABORT.
     * Any resources attached to the network group may be released after function returns.
     *
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     *
     * @note Calling this function is optional, and it is used to shutdown network group while there is still ongoing
     *       inference.
     */
    virtual hailo_status shutdown() = 0;

    /**
     * Creates input virtual stream params.
     *
     * @param[in]  unused                   Unused.
     * @param[in]  format_type              The default format type for all input virtual streams.
     * @param[in]  timeout_ms               The default timeout in milliseconds for all input virtual streams.
     * @param[in]  queue_size               The default queue size for all input virtual streams.
     * @param[in]  network_name             Network name of the requested virtual stream params.
     *                                      If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of a map of name to vstream params.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_input_vstream_params(
        bool unused, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") = 0;

    /**
     * Creates output virtual stream params.
     *
     * @param[in]  unused                   Unused.
     * @param[in]  format_type              The default format type for all output virtual streams.
     * @param[in]  timeout_ms               The default timeout in milliseconds for all output virtual streams.
     * @param[in]  queue_size               The default queue size for all output virtual streams.
     * @param[in]  network_name             Network name of the requested virtual stream params.
     *                                      If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of a map of name to vstream params.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_output_vstream_params(
        bool unused, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") = 0;

    /**
     * Creates output virtual stream params. The groups are splitted with respect to their low-level streams.
     *
     * @param[in]  unused                   Unused.
     * @param[in]  format_type              The default format type for all output virtual streams.
     * @param[in]  timeout_ms               The default timeout in milliseconds for all output virtual streams.
     * @param[in]  queue_size               The default queue size for all output virtual streams.
     * @return Upon success, returns Expected of a vector of maps, mapping name to vstream params, where each map represents a params group.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> make_output_vstream_params_groups(
        bool unused, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size) = 0;

    /**
     * Gets output virtual stream groups for given network_group. The groups are splitted with respect to their low-level streams.
     *
     * @return Upon success, returns Expected of a map of vstream name to group index.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<std::vector<std::string>>> get_output_vstream_groups() = 0;

    /**
     * Gets all network infos of the configured network group.
     *
     * @return Upon success, returns Expected of a vector of all network infos of the configured network group.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<hailo_network_info_t>> get_network_infos() const = 0;

    /**
     * @param[in]  network_name        Network name of the requested stream infos.
     *                                 If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of all stream infos of the configured network group.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name="") const = 0;

    /**
     * @param[in]  network_name        Network name of the requested virtual stream infos.
     *                                 If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of all input vstreams infos of the configured network group.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &network_name="") const = 0;

    /**
     * @param[in]  network_name        Network name of the requested virtual stream infos.
     *                                 If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of all output vstreams infos of the configured network group.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &network_name="") const = 0;

    /**
     * @param[in]  network_name        Network name of the requested virtual stream infos.
     *                                 If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of all vstreams infos of the configured network group.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &network_name="") const = 0;

    /**
     * @returns whether the network group is managed by the model scheduler.
     */
    virtual bool is_scheduled() const = 0;

    /**
     * Sets the maximum time period that may pass before receiving run time from the scheduler.
     * This will occur providing at least one send request has been sent, there is no minimum requirement for send
     *  requests, (e.g. threshold - see set_scheduler_threshold()).
     *
     * @param[in]  timeout              Timeout in milliseconds.
     * @param[in]  network_name         Network name for which to set the timeout.
     *                                  If not passed, the timeout will be set for all the networks in the network group.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note The new time period will be measured after the previous time the scheduler allocated run time to this network group.
     * @note Using this function is only allowed when scheduling_algorithm is not ::HAILO_SCHEDULING_ALGORITHM_NONE.
     * @note The default timeout is 0ms.
     * @note Currently, setting the timeout for a specific network is not supported.
     */
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name="") = 0;

    /**
     * Sets the minimum number of send requests required before the network is considered ready to get run time from the scheduler.
     *
     * @param[in]  threshold            Threshold in number of frames.
     * @param[in]  network_name         Network name for which to set the threshold.
     *                                  If not passed, the threshold will be set for all the networks in the network group.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note Using this function is only allowed when scheduling_algorithm is not ::HAILO_SCHEDULING_ALGORITHM_NONE.
     * @note The default threshold is 1.
     * @note If at least one send request has been sent, but the threshold is not reached within a set time period (e.g. timeout - see
     *  hailo_set_scheduler_timeout()), the scheduler will consider the network ready regardless.
     * @note Currently, setting the threshold for a specific network is not supported.
     */
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name="") = 0;

    /**
     * Sets the priority of the network.
     * When the network group scheduler will choose the next network, networks with higher priority will be prioritized in the selection.
     * bigger number represent higher priority.
     *
     * @param[in]  priority             Priority as a number between HAILO_SCHEDULER_PRIORITY_MIN - HAILO_SCHEDULER_PRIORITY_MAX.
     * @param[in]  network_name         Network name for which to set the Priority.
     *                                  If not passed, the priority will be set for all the networks in the network group.
     * @return Upon success, returns ::HAILO_SUCCESS. Otherwise, returns a ::hailo_status error.
     * @note Using this function is only allowed when scheduling_algorithm is not ::HAILO_SCHEDULING_ALGORITHM_NONE.
     * @note The default priority is HAILO_SCHEDULER_PRIORITY_NORMAL.
     * @note Currently, setting the priority for a specific network is not supported.
     */
    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name="") = 0;

    /**
     * @return Is the network group multi-context or not.
     */
    virtual bool is_multi_context() const = 0;

    /**
     * @return The configuration parameters this network group was initialized with.
     */
    virtual const ConfigureNetworkParams get_config_params() const = 0;

    virtual AccumulatorPtr get_activation_time_accumulator() const = 0;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const = 0;

    virtual Expected<std::vector<InputVStream>> create_input_vstreams(const std::map<std::string, hailo_vstream_params_t> &inputs_params) = 0;
    virtual Expected<std::vector<OutputVStream>> create_output_vstreams(const std::map<std::string, hailo_vstream_params_t> &outputs_params) = 0;
    virtual Expected<size_t> infer_queue_size() const = 0;

    virtual Expected<HwInferResults> run_hw_infer_estimator() = 0;

    virtual Expected<std::vector<std::string>> get_sorted_output_names() = 0;
    virtual Expected<std::vector<std::string>> get_stream_names_from_vstream_name(const std::string &vstream_name) = 0;
    virtual Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name) = 0;

    static Expected<std::shared_ptr<ConfiguredNetworkGroup>> duplicate_network_group_client(uint32_t ng_handle, uint32_t vdevice_handle,
        const std::string &network_group_name);
    virtual Expected<uint32_t> get_client_handle() const;
    virtual Expected<uint32_t> get_vdevice_client_handle() const;

    virtual hailo_status before_fork();
    virtual hailo_status after_fork_in_parent();
    virtual hailo_status after_fork_in_child();

    virtual hailo_status infer_async(const NamedBuffersCallbacks &named_buffers_callbacks,
        const std::function<void(hailo_status)> &infer_request_done_cb) = 0;
    virtual Expected<std::vector<net_flow::PostProcessOpMetadataPtr>> get_ops_metadata() = 0;
    virtual Expected<std::unique_ptr<LayerInfo>> get_layer_info(const std::string &stream_name) = 0;
    hailo_status wait_for_ongoing_callbacks_count_under(size_t threshold);
    void decrease_ongoing_callbacks();
    void increase_ongoing_callbacks();

    virtual hailo_status set_nms_score_threshold(const std::string &edge_name, float32_t nms_score_threshold) = 0;
    virtual hailo_status set_nms_iou_threshold(const std::string &edge_name, float32_t iou_threshold) = 0;
    virtual hailo_status set_nms_max_bboxes_per_class(const std::string &edge_name, uint32_t max_bboxes_per_class) = 0;
    virtual hailo_status set_nms_max_bboxes_total(const std::string &edge_name, uint32_t max_bboxes_total) = 0;
    virtual hailo_status set_nms_max_accumulated_mask_size(const std::string &edge_name, uint32_t max_accumulated_mask_size) = 0;

    virtual hailo_status init_cache(uint32_t read_offset) = 0;
    virtual hailo_status update_cache_offset(int32_t offset_delta_entries) = 0;

    virtual Expected<std::vector<uint32_t>> get_cache_ids() const = 0;
    virtual Expected<Buffer> read_cache_buffer(uint32_t cache_id) = 0;
    virtual hailo_status write_cache_buffer(uint32_t cache_id, MemoryView buffer) = 0;

protected:
    ConfiguredNetworkGroup();

    std::mutex m_infer_requests_mutex;
    std::atomic_size_t m_ongoing_transfers;
    std::condition_variable m_cv;
private:
    friend class ActivatedNetworkGroup;
    friend class AsyncAsyncPipelineBuilder;
};
using ConfiguredNetworkGroupVector = std::vector<std::shared_ptr<ConfiguredNetworkGroup>>;

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_HPP_ */
