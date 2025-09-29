/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hef.hpp
 * @brief Hef parsing and configuration functions
 **/

#ifndef _HAILO_HEF_HPP_
#define _HAILO_HEF_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

#include <vector>
#include <memory>
#include <map>

/** hailort namespace */
namespace hailort
{

#define DEFAULT_NMS_NO_BURST_SIZE (1)
#define DEFAULT_ACTUAL_BATCH_SIZE (1)

/*! Hailo configure parameters per network_group. Analogical to hailo_configure_network_group_params_t */
struct ConfigureNetworkParams
{
    ConfigureNetworkParams() = default;
    ConfigureNetworkParams(const hailo_configure_network_group_params_t &params) : batch_size(params.batch_size),
        power_mode(params.power_mode), latency(params.latency), enable_kv_cache(params.enable_kv_cache)
    {
        for (size_t i = 0; i < params.stream_params_by_name_count; i++) {
            stream_params_by_name.insert(std::make_pair(std::string(params.stream_params_by_name[i].name),
                params.stream_params_by_name[i].stream_params));
        }
        for (size_t i = 0; i < params.network_params_by_name_count; i++) {
            network_params_by_name.insert(std::make_pair(std::string(params.network_params_by_name[i].name),
                params.network_params_by_name[i].network_params));
        }
    }

    bool operator==(const ConfigureNetworkParams &other) const;
    bool operator!=(const ConfigureNetworkParams &other) const;

    uint16_t batch_size;
    hailo_power_mode_t power_mode;
    hailo_latency_measurement_flags_t latency;
    bool enable_kv_cache;
    std::map<std::string, hailo_stream_parameters_t> stream_params_by_name;
    std::map<std::string, hailo_network_parameters_t> network_params_by_name;
};

/** @addtogroup group_type_definitions */
/*@{*/

/** Represents a mapping of network group name to its' params */
using NetworkGroupsParamsMap = std::map<std::string, ConfigureNetworkParams>;

/*@}*/

/*! HEF model that can be loaded to Hailo devices */
class HAILORTAPI Hef final
{
public:

    /**
     * Creates an Hef from a file.
     *
     * @param[in] hef_path            The path of the Hef file.
     * @return Upon success, returns Expected of Hef. Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<Hef> create(const std::string &hef_path);

    /**
     * Creates an Hef from a buffer.
     *
     * @param[in] hef_buffer          A buffer that contains the Hef content.
     * @return Upon success, returns Expected of Hef. Otherwise, returns Unexpected of ::hailo_status error.
     * @note During Hef creation, the buffer's content is copied to an internal buffer.
     */
    static Expected<Hef> create(const MemoryView &hef_buffer);

    /**
     * Creates an Hef from a buffer.
     *
     * @param[in] hef_buffer          A buffer that contains the Hef content.
     * @return Upon success, returns Expected of Hef. Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<Hef> create(std::shared_ptr<Buffer> hef_buffer);

    /**
     * Gets input streams informations.
     *
     * @param[in] name                    The name of the network or network_group which contains the input stream_infos.
     *                                    In case network group name is given, the function returns the input stream infos 
     *                                    of all the networks of the given network group.
     *                                    In case network name is given (provided by @a get_network_infos), 
     *                                    the function returns the input stream infos of the given network.
     *                                    If NULL is passed, the function returns the input stream infos of 
     *                                    all the networks of the first network group.
     * @return Upon success, returns a vector of ::hailo_stream_info_t, containing each stream's information.
     *         Otherwise, returns a ::hailo_status error.
     */
    Expected<std::vector<hailo_stream_info_t>> get_input_stream_infos(const std::string &name="") const;
    
    /**
     * Gets output streams informations.
     *
     * @param[in] name                    The name of the network or network_group which contains the output stream_infos.
     *                                    In case network group name is given, the function returns the output stream infos 
     *                                    of all the networks of the given network group.
     *                                    In case network name is given (provided by @a get_network_infos), 
     *                                    the function returns the output stream infos of the given network.
     *                                    If NULL is passed, the function returns the output stream infos of 
     *                                    all the networks of the first network group.
     * @return Upon success, returns a vector of ::hailo_stream_info_t, containing each stream's information.
     *         Otherwise, returns a ::hailo_status error.
     */
    Expected<std::vector<hailo_stream_info_t>> get_output_stream_infos(const std::string &name="") const;

    /**
     * Gets all streams informations.
     *
     * @param[in] name                    The name of the network or network_group which contains the stream_infos.
     *                                    In case network group name is given, the function returns all stream infos 
     *                                    of all the networks of the given network group.
     *                                    In case network name is given (provided by @a get_network_infos), 
     *                                    the function returns all stream infos of the given network.
     *                                    If NULL is passed, the function returns all the stream infos of 
     *                                    all the networks of the first network group.
     * @return Upon success, returns Expected of a vector of ::hailo_stream_info_t, containing each stream's information.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &name="") const;

    /**
     * Gets stream's information from it's name.
     *
     * @param[in] stream_name         The name of the stream as presented in the Hef.
     * @param[in] stream_direction    Indicates the stream direction.
     * @param[in] net_group_name      The name of the network_group which contains the stream's information.
     *                                If not passed, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected of ::hailo_stream_info_t. Otherwise, returns Unexpected of ::hailo_status error.
     * 
     */
    Expected<hailo_stream_info_t> get_stream_info_by_name(const std::string &stream_name,
        hailo_stream_direction_t stream_direction, const std::string &net_group_name="") const;

    /**
     * Gets input virtual streams infos.
     *
     * @param[in] name                    The name of the network or network_group which contains the input virtual stream_infos.
     *                                    In case network group name is given, the function returns the input virtual stream infos 
     *                                    of all the networks of the given network group.
     *                                    In case network name is given (provided by @a get_network_infos), 
     *                                    the function returns the input virtual stream infos of the given network.
     *                                    If NULL is passed, the function returns the input virtual stream infos of 
     *                                    all the networks of the first network group.
     * @return Upon success, returns Expected of a vector of ::hailo_vstream_info_t.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &name="") const;

    /**
     * Gets output virtual streams infos.
     *
     * @param[in] name                    The name of the network or network_group which contains the output virtual stream_infos.
     *                                    In case network group name is given, the function returns the output virtual stream infos 
     *                                    of all the networks of the given network group.
     *                                    In case network name is given (provided by @a get_network_infos), 
     *                                    the function returns the output virtual stream infos of the given network.
     *                                    If NULL is passed, the function returns the output virtual stream infos of 
     *                                    all the networks of the first network group.
     * 
     * @return Upon success, returns Expected of a vector of ::hailo_vstream_info_t.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &name="") const;

    /**
     * Gets all virtual streams infos.
     *
     * @param[in] name                    The name of the network or network_group which contains the virtual stream_infos.
     *                                    In case network group name is given, the function returns all virtual stream infos 
     *                                    of all the networks of the given network group.
     *                                    In case network name is given (provided by @a get_network_infos), 
     *                                    the function returns all virtual stream infos of the given network.
     *                                    If NULL is passed, the function returns all the virtual stream infos of 
     *                                    all the networks of the first network group.
     * 
     * @return Upon success, returns Expected of a vector of ::hailo_vstream_info_t.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &name="") const;

    /**
     * Gets sorted output vstreams names.
     *
     * @param[in] net_group_name      The name of the network_group which contains the streams information.
     *                                If not passed, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected of a sorted vector of output vstreams names.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<std::string>> get_sorted_output_names(const std::string &net_group_name="") const;

    /**
     * Gets the number of low-level input streams.
     *
     * @param[in] net_group_name      The name of the network_group which contains the streams information.
     *                                If not passed, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected containing the number of low-level input streams.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<size_t> get_number_of_input_streams(const std::string &net_group_name="") const;

    /**
     * Gets the number of low-level output streams.
     *
     * @param[in] net_group_name      The name of the network_group which contains the streams information.
     *                                If not passed, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected containing the number of low-level output streams.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<size_t> get_number_of_output_streams(const std::string &net_group_name="") const;

    /**
     * Gets bottleneck FPS.
     *
     * @param[in] net_group_name      The name of the network_group which contains the information.
     *                                If not passed, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected containing the bottleneck FPS number.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<float64_t> get_bottleneck_fps(const std::string &net_group_name="") const;

    /**
     * Get device Architecture HEF was compiled for.
     *
     * @return Upon success, returns Expected containing the device architecture the HEF was compiled for.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     *
     * @deprecated Use get_compatible_device_archs instead.
     */
    Expected<hailo_device_architecture_t> get_hef_device_arch() const
        DEPRECATED("Hef::get_hef_device_arch is deprecated. Use get_compatible_device_archs instead");

    /**
     * Get all device architectures that the HEF is compatible with.
     *
     * @return Upon success, returns Expected of a vector of hailo_device_architecture_t.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<hailo_device_architecture_t>> get_compatible_device_archs() const;

    /**
     * Get string of device architecture HEF was compiled for.
     *
     * @param[in] arch     hailo_device_architecture_t representing the device architecture of the HEF
     * @return Upon success, returns string representing the device architecture the HEF was compiled for.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::string> device_arch_to_string(const hailo_device_architecture_t arch);

    /**
     * Gets all stream names under the given vstream name
     *
     * @param[in] vstream_name        The name of the vstream.
     * @param[in] net_group_name      The name of the network_group which contains the streams information.
     *                                If not passed, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected of a vector of all stream names linked to the provided vstream.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<std::string>> get_stream_names_from_vstream_name(const std::string &vstream_name,
        const std::string &net_group_name="") const;

    /**
     * Get all vstream names under the given stream name
     *
     * @param[in] stream_name         The name of the low-level stream.
     * @param[in] net_group_name      The name of the network_group which contains the streams information.
     *                                If not passed, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected of a vector of all stream names linked to the provided vstream.
     * @return Upon success, returns  of a vector of all vstream names linked to the provided stream.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name,
        const std::string &net_group_name="") const;

    /**
     * Gets vstream name from original layer name.
     *
     * @param[in] original_name       The original layer name as presented in the Hef.
     * @param[in] net_group_name      The name of the network_group which contains the streams information.
     *                                If not passed, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected containing the vstream's name.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::string> get_vstream_name_from_original_name(const std::string &original_name,
        const std::string &net_group_name="") const;

    /**
     * Gets original names from vstream name.
     *
     * @param[in] vstream_name        The name of the vstream as presented in the Hef.
     * @param[in] net_group_name      The name of the network_group which contains the streams information.
     *                                If not passed, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected of a vector of all original names linked to the provided vstream.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<std::string>> get_original_names_from_vstream_name(const std::string &vstream_name,
        const std::string &net_group_name="") const;

    /**
     * Gets all network groups names in the Hef.
     *
     * @return Returns a vector of all network groups names.
     */
    std::vector<std::string> get_network_groups_names() const;

    /**
     * Gets all network groups infos in the Hef.
     *
     * @return Upon success, returns Expected of a vector of ::hailo_network_group_info_t.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<hailo_network_group_info_t>> get_network_groups_infos() const;

    /**
     * Creates the default configure params for the Hef. The user can modify the given params before
     * configuring the network.
     * 
     * @param[in] stream_interface     Stream interface to use on the network.
     * @return Upon success, returns Expected of NetworkGroupsParamsMap. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<NetworkGroupsParamsMap> create_configure_params(hailo_stream_interface_t stream_interface);

    /**
     * Creates the default configure params for the Hef. The user can modify the given params before
     * configuring the network.
     * 
     * @param[in] stream_interface    Stream interface to use on the network.
     * @param[in] network_group_name  Name of network_group to make configure params for.
     * @return Upon success, returns Expected of ConfigureNetworkParams. Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<ConfigureNetworkParams> create_configure_params(hailo_stream_interface_t stream_interface, const std::string &network_group_name);

    /**
     * Creates streams params with default values.
     *
     * @param[in] net_group_name    The name of the network_group for which to create the stream parameters for.
     *                              If an empty string is given, the first network_group in the Hef will be addressed.
     * @param[in] stream_interface  A ::hailo_stream_interface_t indicating which ::hailo_stream_parameters_t to
     *                              create for the output streams.
     * @return Upon success, returns Expected of a map of stream name to stream params.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::map<std::string, hailo_stream_parameters_t>> create_stream_parameters_by_name(
        const std::string &net_group_name, hailo_stream_interface_t stream_interface);

    /**
     * Creates networks params with default values.
     *
     * @param[in] net_group_name    The name of the network_group for which to create the network parameters for.
     *                              If an empty string is given, the first network_group in the Hef will be addressed.
     * @return Upon success, returns Expected of a map of network name to network params.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::map<std::string, hailo_network_parameters_t>> create_network_parameters_by_name(
        const std::string &net_group_name);

    /**
     * Creates input virtual stream params for a given network_group.
     *
     * @param[in] name              The name of the network or network_group which user wishes to create input virtual stream params for.
     *                              In case network group name is given, the function returns the input virtual stream params 
     *                              of all the networks of the given network group.
     *                              In case network name is given (provided by @a get_network_infos), 
     *                              the function returns the input virtual stream params of the given network.
     *                              If NULL is passed, the function returns the input virtual stream params of 
     *                              all the networks of the first network group.
     * @param[in] unused            Unused.
     * @param[in] format_type       The default format type for all input virtual streams.
     * @param[in] timeout_ms        The default timeout in milliseconds for all input virtual streams.
     * @param[in] queue_size        The default queue size for all input virtual streams.
     * @return Upon success, returns Expected of a map of input virtual stream name to params.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::map<std::string, hailo_vstream_params_t>> make_input_vstream_params(
        const std::string &name, bool unused, hailo_format_type_t format_type,
        uint32_t timeout_ms, uint32_t queue_size);

    /**
     * Creates output virtual stream params for a given network_group.
     *
     * @param[in] name              The name of the network or network_group which user wishes to create output virtual stream params for.
     *                              In case network group name is given, the function returns the output virtual stream params 
     *                              of all the networks of the given network group.
     *                              In case network name is given (provided by @a get_network_infos), 
     *                              the function returns the output virtual stream params of the given network.
     *                              If NULL is passed, the function returns the output virtual stream params of
     *                              all the networks of the first network group.
     * @param[in] unused            Unused.
     * @param[in] format_type       The default format type for all output virtual streams.
     * @param[in] timeout_ms        The default timeout in milliseconds for all output virtual streams.
     * @param[in] queue_size        The default queue size for all output virtual streams.
     * @return Upon success, returns Expected of a map of output virtual stream name to params.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::map<std::string, hailo_vstream_params_t>> make_output_vstream_params(
        const std::string &name, bool unused, hailo_format_type_t format_type,
        uint32_t timeout_ms, uint32_t queue_size);

    /**
     * Gets all networks informations.
     *
     * @param[in] net_group_name      The name of the network_group which contains the network information.
     *                                If NULL is passed, the function returns the network infos of 
     *                                all the networks of the first network group.
     * @return Upon success, returns Expected of a vector of ::hailo_network_info_t, containing each networks's information.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    Expected<std::vector<hailo_network_info_t>> get_network_infos(const std::string &net_group_name="") const;

    /**
     * Returns a unique hash for the specific Hef.
     * 
     * @return A unique string hash for the Hef. Hefs created based on identical files will return identical hashes.
     */
    std::string hash() const;

    /**
     * Returns a unique hash for the specific Hef file.
     *
     * @param[in] hef_path    The path of the Hef file.
     * @return A unique string hash for the Hef file. Hefs created based on identical files will return identical hashes.
     *         Otherwise, returns Unexpected of ::hailo_status error.
     */
    static Expected<std::string> hash(const std::string &hef_path);

    Expected<std::string> get_description(bool stream_infos, bool vstream_infos) const;
    Expected<MemoryView> get_external_resources(const std::string &resource_name) const;
    std::vector<std::string> get_external_resource_names() const;
    void set_memory_footprint_optimization(bool should_optimize); // Best effort optimization to reduce memcpy

    static Expected<std::map<std::string, BufferPtr>> extract_hef_external_resources(const std::string &file_path);

    ~Hef();
    Hef(Hef &&);
    Hef &operator=(Hef &&);
    Hef(const Hef &) = default;
    Hef &operator=(const Hef &) = delete;

private:
    friend class DeviceBase;
    friend class VdmaDevice;
    friend class InputStream;
    friend class OutputStream;
    friend class PyhailortInternal;
    friend class ConfiguredNetworkGroupBase;
    friend class CoreOp;
    friend class VDeviceBase;
    friend class InferModelBase;
    friend class MemoryRequirementsCalculator;
    friend class ContextResources;
    friend class ResourcesManagerBuilder;

    class Impl;
    Hef(std::shared_ptr<Impl> pimpl);
    std::shared_ptr<Impl> pimpl;
};

} /* namespace hailort */

#endif /* _HAILO_HEF_HPP_ */
