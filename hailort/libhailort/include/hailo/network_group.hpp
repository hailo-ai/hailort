/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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

#include <string>
#include <map>
#include <unordered_map>
#include <thread>
#include <utility>

namespace hailort
{

/** @addtogroup group_type_definitions */
/*@{*/

/** Represents a vector of InputStream */
using InputStreamRefVector = std::vector<std::reference_wrapper<InputStream>>;

/** Represents a vector of OutputStream */
using OutputStreamRefVector = std::vector<std::reference_wrapper<OutputStream>>;

/** Represents a mapping of vstream name to its' params */
using NameToVStreamParamsMap = std::unordered_map<std::string, hailo_vstream_params_t>;

/** Represents a vector of pairs of OutputStream and NameToVStreamParamsMap */
using OutputStreamWithParamsVector = std::vector<std::pair<std::reference_wrapper<OutputStream>, NameToVStreamParamsMap>>;

/** Latency measurement result info */
struct LatencyMeasurementResult {
    std::chrono::nanoseconds avg_hw_latency;
};
/*@}*/

using src_context_t = uint8_t;
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
    ConfiguredNetworkGroup(ConfiguredNetworkGroup &&other) noexcept = default;

    /**
     * @return The network group name.
     */
    virtual const std::string &get_network_group_name() const = 0;

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
     * Activates hailo device inner-resources for context_switch inference.

     * @return Upon success, returns Expected of a pointer to ActivatedNetworkGroup object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::unique_ptr<ActivatedNetworkGroup>> activate();

    /**
     * Activates hailo device inner-resources for context_switch inference.
     *
     * @param[in] network_group_params         Parameters for the activation.
     * @return Upon success, returns Expected of a pointer to ActivatedNetworkGroup object.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate(
        const hailo_activate_network_group_params_t &network_group_params) = 0;

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
     * Creates input virtual stream params.
     *
     * @param[in]  quantized                Whether the data fed into the chip is already quantized. True means
     *                                      the data is already quantized. False means it's HailoRT's responsibility
     *                                      to quantize (scale) the data.
     * @param[in]  format_type              The default format type for all input virtual streams.
     * @param[in]  timeout_ms               The default timeout in milliseconds for all input virtual streams.
     * @param[in]  queue_size               The default queue size for all input virtual streams.
     * @param[in]  network_name             Network name of the requested virtual stream params.
     *                                      If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of a map of name to vstream params.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_input_vstream_params(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") = 0;

    /**
     * Creates output virtual stream params.
     *
     * @param[in]  quantized                Whether the data fed into the chip is already quantized. True means
     *                                      the data is already quantized. False means it's HailoRT's responsibility
     *                                      to quantize (scale) the data.
     * @param[in]  format_type              The default format type for all output virtual streams.
     * @param[in]  timeout_ms               The default timeout in milliseconds for all output virtual streams.
     * @param[in]  queue_size               The default queue size for all output virtual streams.
     * @param[in]  network_name             Network name of the requested virtual stream params.
     *                                      If not passed, all the networks in the network group will be addressed.
     * @return Upon success, returns Expected of a map of name to vstream params.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_output_vstream_params(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="") = 0;

    /**
     * Creates output virtual stream params. The groups are splitted with respect to their low-level streams.
     *
     * @param[in]  quantized                Whether the data fed into the chip is already quantized. True means
     *                                      the data is already quantized. False means it's HailoRT's responsibility
     *                                      to quantize (scale) the data.
     * @param[in]  format_type              The default format type for all output virtual streams.
     * @param[in]  timeout_ms               The default timeout in milliseconds for all output virtual streams.
     * @param[in]  queue_size               The default queue size for all output virtual streams.
     * @return Upon success, returns Expected of a vector of maps, mapping name to vstream params, where each map represents a params group.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    virtual Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> make_output_vstream_params_groups(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size) = 0;

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

    virtual AccumulatorPtr get_activation_time_accumulator() const = 0;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const = 0;

protected:
    ConfiguredNetworkGroup() = default;

private:
    friend class ActivatedNetworkGroup;
};
using ConfiguredNetworkGroupVector = std::vector<std::shared_ptr<ConfiguredNetworkGroup>>;

} /* namespace hailort */

#endif /* _HAILO_NETWORK_GROUP_HPP_ */
