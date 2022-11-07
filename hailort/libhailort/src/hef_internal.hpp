/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hef_internal.hpp
 * @brief Internal definition of Hef class Impl
 **/

#ifndef _HEF_INTERNAL_HPP_
#define _HEF_INTERNAL_HPP_

// https://github.com/protocolbuffers/protobuf/tree/master/cmake#notes-on-compiler-warnings
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4127)
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include "hef.pb.h"
#if defined(_MSC_VER)
#pragma warning( pop ) 
#else
#pragma GCC diagnostic pop
#endif

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/hef.hpp"
#include "hailo/network_group.hpp"
#include "context_switch/active_network_group_holder.hpp"
#include "layer_info.hpp"
#include "hailort_defaults.hpp"
#include "control_protocol.h"

#include "control_protocol.hpp"

#include <functional>
#include <bitset>
#include <memory>

extern "C" {
#include "md5.h"
}

namespace hailort
{

class ResourcesManager;
class ConfigBuffer;
class VdmaConfigActivatedNetworkGroup;
using VdmaConfigActiveAppHolder = ActiveNetworkGroupHolder<VdmaConfigActivatedNetworkGroup>;
using ProtoHEFNetworkGroupPtr = std::shared_ptr<ProtoHEFNetworkGroup>;

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t hef_proto_length;
    uint32_t reserved;
    MD5_SUM_t expected_md5;
} hef__header_t;
#pragma pack(pop)

typedef enum {
    HEF__FORMAT__TF_RGB = 0,
    HEF__FORMAT__FRAMES,
    HEF__FORMAT__FLAT,
    HEF__FORMAT__FCR,
    HEF__FORMAT__BAYER_RGB,
    HEF__FORMAT__ARGMAX,
    HEF__FORMAT__NMS,
    HEF__FORMAT__F8CR,
} HEF__net_io_formatter_type_t;


using ConfigBufferInfoMap = std::unordered_map<uint8_t, std::vector<uint32_t>>;
const static uint32_t SUPPORTED_EXTENSIONS_BITSET_SIZE = 1000;
static const std::vector<ProtoHEFExtensionType> SUPPORTED_EXTENSIONS = {
    ABBALE, 
    POSTED_WRITES, 
    DDR, 
    PADDED_DDR_BUFFERS, 
    IS_MULTI_CONTEXTS, 
    COMPRESSED_PARAMS, 
    TRANSPOSE_COMPONENT,
    IS_NMS_MULTI_CONTEXT,
    OFFLOAD_ARGMAX,
    KO_RUN_ASAP // Extention added in platform 4.8 release
};

struct HefParsingInfo
{
    ConfigBufferInfoMap cfg_infos_preliminary_config;
    // cfg_count_per_context[ctxt_index][cfg_index]
    std::vector<ConfigBufferInfoMap> cfg_infos_per_context;
    // TODO: add information about boundary channels here, so they can be allocated in init time instead of activation time
    // TODO: Save this struct inside network_group class
};

struct NetworkGroupSupportedFeatures {
    bool padded_ddr_buffers;
    bool multi_network_support;
    bool multi_context;
    bool preliminary_run_asap;
};

static uint32_t PARTIAL_CLUSTERS_LAYOUT_IGNORE = static_cast<uint32_t>(-1);

static inline bool is_h2d_boundary_info_layer(const ProtoHEFEdgeLayer& layer)
{
    return ((ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__HOST_TO_DEVICE == layer.direction()) &&
        (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY ==
            layer.context_switch_info().edge_connection_type()) &&
        (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type()));
}

static inline bool is_d2h_boundary_info_layer(const ProtoHEFEdgeLayer& layer)
{
    return ((ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST == layer.direction()) &&
        (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY ==
            layer.context_switch_info().edge_connection_type()) &&
        (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type()));
}

static inline bool is_h2d_boundary_mux_layer(const ProtoHEFEdgeLayer& layer)
{
    return ((ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__HOST_TO_DEVICE == layer.direction()) &&
        (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY ==
            layer.context_switch_info().edge_connection_type()) &&
        (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__MUX == layer.edge_layer_type()));
}

static inline bool is_d2h_boundary_mux_layer(const ProtoHEFEdgeLayer& layer)
{
    return ((ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST == layer.direction()) &&
        (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY ==
            layer.context_switch_info().edge_connection_type()) &&
        (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__MUX == layer.edge_layer_type()));
}

// TODO: Fix the circular dependency (with HRT-2899, InputStream/OutputStream related code will move elsewhere)
class InputStreamBase;
class OutputStreamBase;

// Forward declerations
struct WriteMemoryInfo;
class Device;
class VdmaConfigNetworkGroup;
class VdmaDevice;
class HailoRTDriver;

class NetworkGroupMetadata {
public:
    NetworkGroupMetadata() = default;
    NetworkGroupMetadata(const std::string &network_group_name,
        std::vector<std::vector<LayerInfo>> &&boundary_input_layers,
        std::vector<std::vector<LayerInfo>> &&boundary_output_layers,
        std::vector<std::vector<InterContextLayerInfo>> &&inter_context_input_layers,
        std::vector<std::vector<InterContextLayerInfo>> &&inter_context_output_layers,
        std::vector<std::vector<DdrLayerInfo>> &&ddr_input_layers,
        std::vector<std::vector<DdrLayerInfo>> &&ddr_output_layers,
        std::vector<std::string> &&sorted_output_names,
        NetworkGroupSupportedFeatures &supported_features, 
        const std::vector<std::string> &sorted_network_names);

    Expected<std::vector<LayerInfo>> get_input_layer_infos(const std::string &network_name = "") const;
    Expected<std::vector<LayerInfo>> get_output_layer_infos(const std::string &network_name = "") const;
    Expected<std::vector<LayerInfo>> get_all_layer_infos(const std::string &network_name = "") const;
    Expected<LayerInfo> get_layer_info_by_stream_name(const std::string &stream_name) const;
    
    std::vector<LayerInfo> get_boundary_input_layer_infos(const uint8_t context_index) const;
    std::vector<LayerInfo> get_boundary_output_layer_infos(const uint8_t context_index) const;
    std::vector<InterContextLayerInfo> get_inter_context_input_layer_infos(const uint8_t context_index) const;
    std::vector<InterContextLayerInfo> get_inter_context_output_layer_infos(const uint8_t context_index) const;
    std::vector<DdrLayerInfo> get_ddr_input_layer_infos(const uint8_t context_index) const;
    std::vector<DdrLayerInfo> get_ddr_output_layer_infos(const uint8_t context_index) const;

    Expected<std::vector<hailo_stream_info_t>> get_input_stream_infos(const std::string &network_name = "") const;
    Expected<std::vector<hailo_stream_info_t>> get_output_stream_infos(const std::string &network_name = "") const;
    Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name = "") const;

    Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &network_name = "") const;
    Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &network_name = "") const;
    Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &network_name = "") const;

    Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name) const;
    Expected<std::vector<std::string>> get_stream_names_from_vstream_name(const std::string &vstream_name) const;

    Expected<std::vector<hailo_network_info_t>> get_network_infos() const;

    const std::string &network_group_name() const
    {
        return m_network_group_name;
    }

    const std::string default_network_name() const
    {
        return HailoRTDefaults::get_network_name(m_network_group_name);
    }

    const std::vector<std::string> get_sorted_output_names() const
    {
        return m_sorted_output_names;
    }

    const NetworkGroupSupportedFeatures &supported_features() const
    {
        return m_supported_features;
    }

    const std::vector<std::string> get_network_names() const
    {
        return m_sorted_network_names;
    }

private:
    std::vector<hailo_stream_info_t> convert_layer_infos_to_stream_infos(const std::vector<LayerInfo> &layer_infos) const;
    std::vector<hailo_vstream_info_t> convert_layer_infos_to_vstream_infos(const std::vector<LayerInfo> &layer_infos) const;

    // vector of edge layers Per context
    std::vector<std::vector<LayerInfo>> m_boundary_input_layers;
    std::vector<std::vector<LayerInfo>> m_boundary_output_layers;
    std::vector<std::vector<InterContextLayerInfo>> m_inter_context_input_layers;
    std::vector<std::vector<InterContextLayerInfo>> m_inter_context_output_layers;
    std::vector<std::vector<DdrLayerInfo>> m_ddr_input_layers;
    std::vector<std::vector<DdrLayerInfo>> m_ddr_output_layers;

    std::string m_network_group_name;
    std::vector<std::string> m_sorted_output_names;
    NetworkGroupSupportedFeatures m_supported_features;
    std::vector<std::string> m_sorted_network_names;
};

class NetworkGroupMetadataPerArch
{
public:
    NetworkGroupMetadataPerArch() = default;
    Expected<NetworkGroupMetadata> get_metadata(uint32_t partial_clusters_layout_bitmap)
    {
        if (PARTIAL_CLUSTERS_LAYOUT_IGNORE == partial_clusters_layout_bitmap) {
            // Passing PARTIAL_CLUSTERS_LAYOUT_IGNORE is magic for getting one of the metadata
            assert(0 != m_metadata_per_arch.size());
            auto result = m_metadata_per_arch.begin()->second;
            return result;
        }
        if (contains(m_metadata_per_arch, partial_clusters_layout_bitmap)) {
            auto result = m_metadata_per_arch[partial_clusters_layout_bitmap];
            return result;
        }
        LOGGER__ERROR("NetworkGroupMetadataPerArch does not contain metadata for partial_clusters_layout_bitmap {}", partial_clusters_layout_bitmap);
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
    void add_metadata(const NetworkGroupMetadata &metadata, uint32_t partial_clusters_layout_bitmap)
    {
        m_metadata_per_arch[partial_clusters_layout_bitmap] = metadata;
    }
private:
    std::map<uint32_t, NetworkGroupMetadata> m_metadata_per_arch;
};

class Hef::Impl final
{
public:
    static const uint32_t HEADER_MAGIC = 0x01484546;
    static const uint32_t HEADER_VERSION = 0;

    static Expected<Impl> create(const std::string &hef_path);
    static Expected<Impl> create(const MemoryView &hef_buffer);

    const std::vector<ProtoHEFNetworkGroupPtr>& network_groups() const;

    Expected<std::pair<std::string, std::string>> get_network_group_and_network_name(const std::string &name);

    Expected<ProtoHEFNetworkGroupPtr> get_net_group_by_name(const std::string &net_group_name="");
    Expected<std::vector<hailo_network_info_t>> get_network_infos(const std::string &net_group_name="");

    Expected<std::vector<hailo_stream_info_t>> get_input_stream_infos(const std::string &net_group_name="",
        const std::string &network_name="");
    Expected<std::vector<hailo_stream_info_t>> get_output_stream_infos(const std::string &net_group_name="",
        const std::string &network_name="");
    Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &net_group_name="",
        const std::string &network_name="");
    Expected<hailo_stream_info_t> get_stream_info_by_name(const std::string &stream_name,
        hailo_stream_direction_t stream_direction, const std::string &net_group_name="");

    Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &net_group_name="",
        const std::string &network_name="");
    Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &net_group_name="",
        const std::string &network_name="");
    Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &net_group_name="",
        const std::string &network_name="");
    Expected<std::vector<std::string>> get_sorted_output_names(const std::string &net_group_name="");
    Expected<size_t> get_number_of_input_streams(const std::string &net_group_name="");
    Expected<size_t> get_number_of_output_streams(const std::string &net_group_name="");
    ProtoHEFHwArch get_device_arch();
    Expected<float64_t> get_bottleneck_fps(const std::string &net_group_name="");
    static bool contains_ddr_layers(const ProtoHEFNetworkGroup& net_group);
    static hailo_status validate_net_group_unique_layer_names(const ProtoHEFNetworkGroup &net_group);
    Expected<std::vector<hailo_vstream_info_t>> get_network_input_vstream_infos(const std::string &net_group_name="",
        const std::string &network_name="");

    Expected<std::vector<std::string>> get_stream_names_from_vstream_name(const std::string &vstream_name,
        const std::string &net_group_name="");
    Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name,
        const std::string &net_group_name="");

    Expected<std::string> get_vstream_name_from_original_name(const std::string &original_name,
        const std::string &net_group_name="");
    Expected<std::vector<std::string>> get_original_names_from_vstream_name(const std::string &stream_name,
        const std::string &net_group_name="");

    std::vector<std::string> get_network_groups_names();
    Expected<std::vector<hailo_network_group_info_t>> get_network_groups_infos();

    Expected<ConfigureNetworkParams> create_configure_params(hailo_stream_interface_t stream_interface, const std::string &network_gorup_name);
    Expected<ConfigureNetworkParams> create_configure_params_mipi_input(hailo_stream_interface_t output_interface,
        const hailo_mipi_input_stream_params_t &mipi_params, const std::string &network_gorup_name);

    static Expected<std::vector<WriteMemoryInfo>> create_single_context_network_group_config(
        const ProtoHEFPreliminaryConfig& proto_config);

    /* TODO HRT-5067 - work with hailo_device_architecture_t instead of ProtoHEFHwArch */
    static Expected<std::shared_ptr<ResourcesManager>> create_resources_manager(
        const ProtoHEFNetworkGroup &network_group_proto, uint8_t net_group_index,
        VdmaDevice &device, HailoRTDriver &driver, const ConfigureNetworkParams &network_group_params,
        std::shared_ptr<NetworkGroupMetadata> network_group_metadata,
        const ProtoHEFHwArch &hw_arch);

    static ExpectedRef<const ProtoHEFNetworkGroup> get_net_group_per_arch(const ProtoHEFNetworkGroup &base_net_group,
        ProtoHEFHwArch hef_arch, hailo_device_architecture_t device_arch, uint32_t partial_clusters_layout_bitmap);

    Expected<std::map<std::string, hailo_stream_parameters_t>> create_stream_parameters_by_name(
        const std::string &net_group_name, hailo_stream_interface_t stream_interface);

    Expected<std::map<std::string, hailo_network_parameters_t>> create_network_parameters_by_name(
        const std::string &net_group_name);

    Expected<std::map<std::string,hailo_stream_parameters_t>> create_stream_parameters_by_name_mipi_input(
        const std::string &net_group_name, hailo_stream_interface_t output_interface,
        const hailo_mipi_input_stream_params_t &mipi_params);

    Expected<std::map<std::string, hailo_vstream_params_t>> make_input_vstream_params(
        const std::string &net_group_name, const std::string &network_name, bool quantized, 
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size);
    hailo_status fill_missing_input_vstream_params_with_default(const std::string &net_group_name,
        const std::string &network_name, std::map<std::string, hailo_vstream_params_t> &input_vstreams_params,
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size);
    Expected<std::map<std::string, hailo_vstream_params_t>> make_output_vstream_params(
        const std::string &net_group_name, const std::string &network_name, bool quantized, 
        hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size);
    hailo_status fill_missing_output_vstream_params_with_default(const std::string &net_group_name,
        const std::string &network_name, std::map<std::string, hailo_vstream_params_t> &output_vstream_params,
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size);
    static hailo_status fill_missing_vstream_params_with_default(std::map<std::string, hailo_vstream_params_t> &vstream_params,
        std::vector<hailo_vstream_info_t> &name_to_format_info, bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms,
        uint32_t queue_size);

    Expected<NetworkGroupMetadata> get_network_group_metadata(const std::string &network_group_name, uint32_t partial_clusters_layout_bitmap = PARTIAL_CLUSTERS_LAYOUT_IGNORE)
    {
        CHECK_AS_EXPECTED(contains(m_network_group_metadata_per_arch, network_group_name), HAILO_NOT_FOUND,
            "Network group with name {} wasn't found", network_group_name);
        auto metadata_per_arch = m_network_group_metadata_per_arch.at(network_group_name);
        auto metadata = metadata_per_arch.get_metadata(partial_clusters_layout_bitmap);
        return metadata;
    }

    const MD5_SUM_t &md5() const
    {
        return m_md5;
    }

#ifdef HAILO_SUPPORT_MULTI_PROCESS
    const MemoryView get_hef_memview();
#endif // HAILO_SUPPORT_MULTI_PROCESS

private:
    Impl(const std::string &hef_path, hailo_status &status);
    Impl(const MemoryView &hef_memview, hailo_status &status);

    hailo_status parse_hef_file(const std::string &hef_path);
    hailo_status parse_hef_memview(const MemoryView &hef_memview);
    hailo_status transfer_protobuf_field_ownership(ProtoHEFHef &hef_message);
    hailo_status fill_networks_metadata();
    void fill_extensions_bitset();
    void init_md5(MD5_SUM_t &calculated_md5);

    static bool check_hef_extension(const ProtoHEFExtensionType &extension, const ProtoHEFHeader &header,
        const std::vector<ProtoHEFExtension> &hef_extensions, const ProtoHEFIncludedFeatures &included_features);
    // Note: If the network group is found, i.e has_value() is true on the returned object, then the underlying pointer is not null
    static bool check_hef_optional_extension(const ProtoHEFExtensionType &extension, const ProtoHEFHeader &header,
        const std::vector<ProtoHEFOptionalExtension> &hef_optional_extensions);
    static NetworkGroupSupportedFeatures get_supported_features(const ProtoHEFHeader &header,
        const std::vector<ProtoHEFExtension> &hef_extensions, const ProtoHEFIncludedFeatures &included_features,
        const std::vector<ProtoHEFOptionalExtension> &hef_optional_extensions);

    hailo_status validate_hef_extensions();
    static hailo_status validate_hef_header(const hef__header_t &header, MD5_SUM_t &calculated_md5, size_t proto_size);
    static Expected<HefParsingInfo> get_parsing_info(const ProtoHEFNetworkGroup &net_group);

    Expected<std::map<std::string, hailo_format_t>> get_inputs_vstream_names_and_format_info(
        const std::string &net_group_name, const std::string &network_name);
    Expected<std::map<std::string, hailo_format_t>> get_outputs_vstream_names_and_format_info(
        const std::string &net_group_name, const std::string &network_name);

    static Expected<std::string> get_vstream_name_from_original_name_mux(const std::string &original_name, const ProtoHefEdge &layer);
    static Expected<std::vector<std::string>> get_original_names_from_vstream_name_mux(const std::string &vstream_name, const ProtoHefEdge &layer);

    Expected<NetworkGroupMetadata> create_metadata_per_arch(const ProtoHEFNetworkGroup &network_group, NetworkGroupSupportedFeatures &supported_features);

    // Hef information
    ProtoHEFHeader m_header;
    ProtoHEFIncludedFeatures m_included_features;
    std::vector<ProtoHEFNetworkGroupPtr> m_groups;
    std::vector<ProtoHEFExtension> m_hef_extensions;
    std::vector<ProtoHEFOptionalExtension> m_hef_optional_extensions;
    std::bitset<SUPPORTED_EXTENSIONS_BITSET_SIZE> m_supported_extensions_bitset;
    MD5_SUM_t m_md5;

#ifdef HAILO_SUPPORT_MULTI_PROCESS
    Buffer m_hef_buffer;
#endif // HAILO_SUPPORT_MULTI_PROCESS

    // NetworkGroups information
    std::map<std::string, NetworkGroupMetadataPerArch> m_network_group_metadata_per_arch;
};

// TODO: Make this part of a namespace? (HRT-2881)
/* TODO: Create LayerInfo for all layers in the HEF (including inter-context and DDR), and use it for parsing additional info without proto dependency
    After this will be done, this class should move to layer_info.hpp */
class HefConfigurator final
{
public:
    HefConfigurator() = delete;

    static Expected<CONTROL_PROTOCOL__nn_stream_config_t> parse_nn_stream_config(const ProtoHEFEdgeLayerBase &edge_layer,
        bool hw_padding_supported, const ProtoHEFEdgeConnectionType &edge_connection_type);
    static Expected<CONTROL_PROTOCOL__nn_stream_config_t> parse_nn_stream_config(const LayerInfo &edge_layer,
        bool hw_padding_supported);

    static bool is_hw_padding_supported(const ProtoHEFEdgeLayer &edge_layer);
    static bool is_hw_padding_supported(const LayerInfo &layer_info);
private:
    static Expected<CONTROL_PROTOCOL__nn_stream_config_t> parse_nn_stream_config(hailo_format_order_t format_order,
        uint32_t width, uint32_t features, uint32_t hw_data_bytes, uint16_t core_buffers_per_frame,
        uint16_t core_bytes_per_buffer, bool hw_padding_supported, bool is_ddr);

    static bool is_hw_padding_supported(bool is_boundary, bool is_mux, hailo_format_order_t format_order,
        uint16_t core_buffers_per_frame, uint32_t height, uint32_t width, uint32_t features, uint32_t hw_data_bytes);
};

class HefUtils final
{
public:
    HefUtils() = delete;

    static hailo_status fill_boundary_layers_info(
        const ProtoHEFNetworkGroup &network_group_proto,
        const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer,
        const NetworkGroupSupportedFeatures &supported_features,
        std::vector<std::string> &layer_name,
        std::vector<LayerInfo> &context_boundary_input_layers,
        std::vector<LayerInfo> &context_boundary_output_layers);
    static Expected<InterContextLayerInfo> get_inter_context_layer_info(
        const ProtoHEFNetworkGroup &net_group, const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer, const NetworkGroupSupportedFeatures &supported_features);
    static hailo_status fill_inter_context_layers_info(
        const ProtoHEFNetworkGroup &network_group_proto,
        const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer,
        const NetworkGroupSupportedFeatures &supported_features,
        std::vector<InterContextLayerInfo> &context_inter_context_input_layers,
        std::vector<InterContextLayerInfo> &context_inter_context_output_layers);
    static Expected<DdrLayerInfo> get_ddr_layer_info(
        const ProtoHEFNetworkGroup &net_group, const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer, const NetworkGroupSupportedFeatures &supported_features);
    static hailo_status fill_ddr_layers_info(
        const ProtoHEFNetworkGroup &network_group_proto,
        const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer,
        const NetworkGroupSupportedFeatures &supported_features,
        std::vector<DdrLayerInfo> &context_ddr_input_layers,
        std::vector<DdrLayerInfo> &context_ddr_output_layers);
    static hailo_status check_ddr_pairs_match(
        const std::vector<DdrLayerInfo> &context_ddr_input_layers,
        const std::vector<DdrLayerInfo> &context_ddr_output_layers,
        const uint8_t context_index);
    static hailo_status get_all_layers_info(const ProtoHEFNetworkGroup &network_group_proto,
        const NetworkGroupSupportedFeatures &supported_features,
        std::vector<std::vector<LayerInfo>> &boundary_input_layers,
        std::vector<std::vector<LayerInfo>> &boundary_output_layers,
        std::vector<std::vector<InterContextLayerInfo>> &inter_context_input_layers,
        std::vector<std::vector<InterContextLayerInfo>> &inter_context_output_layers,
        std::vector<std::vector<DdrLayerInfo>> &ddr_input_layers,
        std::vector<std::vector<DdrLayerInfo>> &ddr_output_layers);
    static Expected<hailo_nms_info_t> parse_proto_nms_info(const ProtoHEFNmsInfo &proto_nms_info);
    static Expected<LayerInfo> get_boundary_layer_info(const ProtoHEFNetworkGroup &net_group, const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer, const NetworkGroupSupportedFeatures &supported_features);
    static Expected<std::vector<std::string>> get_sorted_output_names(const ProtoHEFNetworkGroup &net_group);
    
    static Expected<std::string> get_partial_network_name_by_index(const ProtoHEFNetworkGroup &network_group_proto, uint8_t network_index, 
        const NetworkGroupSupportedFeatures &supported_features);

    static Expected<std::vector<hailo_network_info_t>> get_network_infos(const ProtoHEFNetworkGroup &net_group,
        const std::string &net_group_name, const NetworkGroupSupportedFeatures &supported_features);

    static std::string get_network_name(const ProtoHEFNetworkGroup &net_group, const std::string &partial_network_name);
    static std::string get_network_name(const std::string &net_group_name, const std::string &partial_network_name);

private:
    static hailo_status fill_layer_info_with_base_info(const ProtoHEFEdgeLayerBase &base_info,
        const ProtoHEFEdgeConnectionType &edge_connection_type,
        const ProtoHEFNetworkGroupMetadata &network_group_proto, bool hw_padding_supported, bool transposed, 
        const uint8_t context_index, const uint8_t network_index, LayerInfo &layer_info);
    static hailo_status fill_layer_info(const ProtoHEFEdgeLayerInfo &info,
        const ProtoHEFEdgeConnectionType &edge_connection_type,
        const ProtoHEFNetworkGroup &net_group, hailo_stream_direction_t direction,
        bool hw_padding_supported, const uint8_t context_index, const std::string &partial_network_name, 
        uint8_t network_index, LayerInfo &layer_info);
    static hailo_status fill_fused_nms_info(const ProtoHEFEdgeLayerFused &info,
            LayerInfo &layer_info, hailo_quant_info_t &defuse_quant_info, const std::string &network_name);
    static hailo_status fill_mux_info(const ProtoHEFEdgeLayerMux &info,
        const ProtoHEFEdgeConnectionType &edge_connection_type,
        const ProtoHEFNetworkGroup &net_group, hailo_stream_direction_t direction,
        bool hw_padding_supported, const uint8_t context_index, const std::string &partial_network_name, 
        uint8_t network_index, LayerInfo &layer_info);
};

class ContextSwitchTrigger final
{
public:
    static Expected<ContextSwitchTrigger> create_none_trigger();
    static Expected<ContextSwitchTrigger> create(const ProtoHEFTrigger &proto_trigger);
    ContextSwitchTrigger(ContextSwitchTrigger &&) = default;
    ContextSwitchTrigger(const ContextSwitchTrigger &) = delete;
    ContextSwitchTrigger &operator=(ContextSwitchTrigger &&) = delete;
    ContextSwitchTrigger &operator=(const ContextSwitchTrigger &) = delete;
    ~ContextSwitchTrigger() = default;

    CONTROL_PROTOCOL__TRIGGER_t get_serialized() const;
    hailo_status add_to_trigger_group(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) const;

private:
    static Expected<CONTROL_PROTOCOL__TRIGGER_t> serialize(const ProtoHEFTrigger &proto_trigger);
    ContextSwitchTrigger(CONTROL_PROTOCOL__TRIGGER_t &&serialized_trigger);

    const CONTROL_PROTOCOL__TRIGGER_t m_serialized_trigger;
};

class ContextSwitchConfigAction;
using ContextSwitchConfigActionPtr = std::shared_ptr<ContextSwitchConfigAction>;
class ContextSwitchConfigAction
{
public:
    enum class Type
    {
        None,
        WriteData,
        WriteDataCcw,
        AddCcwBurst,
        CreateDescForCcw,
        ReadVdma,
        TriggerSequencer,
        WaitForSequencerDone,
        TriggerNewDataFromDataInput,
        TriggerNewDataFromDataInputDdr,
        EnableLcuNonDefault,
        EnableLcuDefault,
        DisableLcu,
        WaitForModuleConfigDone,
        DdrPairInfo,
        StartDdrBufferingTask,
        AddRrepeated,
        StartBurstCreditsTask,
        EdgeLayerActivationActionsPositionMarker
    };

    static Expected<ContextSwitchConfigActionPtr> create(const ProtoHEFAction &proto_action, Device &device,
        std::vector<ConfigBuffer> &config, const ResourcesManager &resources_manager, const ProtoHEFNetworkGroup &net_group,
        bool support_pre_fetch);

    ContextSwitchConfigAction(ContextSwitchConfigAction &&) = default;
    ContextSwitchConfigAction(const ContextSwitchConfigAction &) = delete;
    ContextSwitchConfigAction &operator=(ContextSwitchConfigAction &&) = delete;
    ContextSwitchConfigAction &operator=(const ContextSwitchConfigAction &) = delete;
    virtual ~ContextSwitchConfigAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) = 0;
    virtual bool supports_repeated_block() const = 0;
    void set_is_in_repeated_block(bool is_repeated);
    Type get_type() const;
    CONTROL_PROTOCOL__ACTION_TYPE_t get_action_list_type() const;
    const ProtoHEFAction &get_proto_action() const;

protected:
    ContextSwitchConfigAction(Type type);
    ContextSwitchConfigAction(Type type, const ProtoHEFAction& proto_action);
    ContextSwitchConfigAction(Type type, CONTROL_PROTOCOL__ACTION_TYPE_t action_list_type);
    ContextSwitchConfigAction(Type type, CONTROL_PROTOCOL__ACTION_TYPE_t action_list_type, const ProtoHEFAction& proto_action);

    const Type m_type;
    const CONTROL_PROTOCOL__ACTION_TYPE_t m_action_list_type;
    bool m_is_in_repeated_block;
    // Note: This references the action in the hef Protobuf
    const ProtoHEFAction &m_proto_action;
};

class NoneAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();
    NoneAction(NoneAction &&) = default;
    NoneAction(const NoneAction &) = delete;
    NoneAction &operator=(NoneAction &&) = delete;
    NoneAction &operator=(const NoneAction &) = delete;
    virtual ~NoneAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    NoneAction();
};

class WriteDataAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const ProtoHEFAction& proto_action, Device &device);
    WriteDataAction(WriteDataAction &&) = default;
    WriteDataAction(const WriteDataAction &) = delete;
    WriteDataAction &operator=(WriteDataAction &&) = delete;
    WriteDataAction &operator=(const WriteDataAction &) = delete;
    virtual ~WriteDataAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    WriteDataAction(const ProtoHEFAction& proto_action, Device &device);

    Device &m_device;
};

class WriteDataCcwAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const ProtoHEFAction& proto_action,
        std::vector<ConfigBuffer> &config, bool support_pre_fetch);
    WriteDataCcwAction(WriteDataCcwAction &&) = default;
    WriteDataCcwAction(const WriteDataCcwAction &) = delete;
    WriteDataCcwAction &operator=(WriteDataCcwAction &&) = delete;
    WriteDataCcwAction &operator=(const WriteDataCcwAction &) = delete;
    virtual ~WriteDataCcwAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    WriteDataCcwAction(const ProtoHEFAction& proto_action, ConfigBuffer &config, 
        bool support_pre_fetch);

    bool is_last_ccw_write();
    hailo_status pad_with_nops();
    
    ConfigBuffer &m_config;
    const bool m_support_pre_fetch;
};

class AddCcwBurstAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t config_stream_index, uint16_t ccw_bursts);

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    AddCcwBurstAction(uint8_t config_stream_index, uint16_t ccw_bursts);

    const uint8_t m_config_stream_index;
    const uint16_t m_ccw_bursts;
};

class CreateConfigDescAndFetchAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(uint8_t config_stream_index, ConfigBuffer &config_buffer);

    CreateConfigDescAndFetchAction(CreateConfigDescAndFetchAction &&) = default;
    CreateConfigDescAndFetchAction(const CreateConfigDescAndFetchAction &) = delete;
    CreateConfigDescAndFetchAction &operator=(CreateConfigDescAndFetchAction &&) = delete;
    CreateConfigDescAndFetchAction &operator=(const CreateConfigDescAndFetchAction &) = delete;
    virtual ~CreateConfigDescAndFetchAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    CreateConfigDescAndFetchAction(uint8_t config_stream_index, ConfigBuffer &config_buffer);

    const uint8_t m_config_stream_index;
    ConfigBuffer &m_config_buffer;
};

class StartBurstCreditsTaskAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();

    StartBurstCreditsTaskAction(StartBurstCreditsTaskAction &&) = default;
    StartBurstCreditsTaskAction(const StartBurstCreditsTaskAction &) = delete;
    StartBurstCreditsTaskAction &operator=(StartBurstCreditsTaskAction &&) = delete;
    StartBurstCreditsTaskAction &operator=(const StartBurstCreditsTaskAction &) = delete;
    virtual ~StartBurstCreditsTaskAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    StartBurstCreditsTaskAction();
};

class RepeatedHeaderAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(CONTROL_PROTOCOL__ACTION_TYPE_t sub_action_type, uint8_t num_actions);
    RepeatedHeaderAction(RepeatedHeaderAction &&) = default;
    RepeatedHeaderAction(const RepeatedHeaderAction &) = delete;
    RepeatedHeaderAction &operator=(RepeatedHeaderAction &&) = delete;
    RepeatedHeaderAction &operator=(const RepeatedHeaderAction &) = delete;
    virtual ~RepeatedHeaderAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    RepeatedHeaderAction(CONTROL_PROTOCOL__ACTION_TYPE_t sub_action_type, uint8_t num_actions);

    const CONTROL_PROTOCOL__ACTION_TYPE_t m_sub_action_type;
    const uint8_t m_num_actions;
};

class DisableLcuAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const ProtoHEFAction& proto_action);
    DisableLcuAction(DisableLcuAction &&) = default;
    DisableLcuAction(const DisableLcuAction &) = delete;
    DisableLcuAction &operator=(DisableLcuAction &&) = delete;
    DisableLcuAction &operator=(const DisableLcuAction &) = delete;
    virtual ~DisableLcuAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    DisableLcuAction(const ProtoHEFAction& proto_action);
};

class EnableLcuAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const ProtoHEFAction& proto_action,
        const ResourcesManager &resources_manager, const ProtoHEFNetworkGroup &net_group);
    EnableLcuAction(EnableLcuAction &&) = default;
    EnableLcuAction(const EnableLcuAction &) = delete;
    EnableLcuAction &operator=(EnableLcuAction &&) = delete;
    EnableLcuAction &operator=(const EnableLcuAction &) = delete;
    virtual ~EnableLcuAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    static CONTROL_PROTOCOL__ACTION_TYPE_t get_enable_lcu_action_type(bool is_default);
    static Type get_enable_lcu_type(bool is_default);

    EnableLcuAction(const ProtoHEFAction& proto_action, bool is_default, uint8_t network_index);
    const uint8_t m_network_index;
    const bool m_is_default;
};

class EnableSequencerAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const ProtoHEFAction& proto_action);
    EnableSequencerAction(EnableSequencerAction &&) = default;
    EnableSequencerAction(const EnableSequencerAction &) = delete;
    EnableSequencerAction &operator=(EnableSequencerAction &&) = delete;
    EnableSequencerAction &operator=(const EnableSequencerAction &) = delete;
    virtual ~EnableSequencerAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    EnableSequencerAction(const ProtoHEFAction& proto_action, uint8_t initial_l3_cut,
        uint16_t initial_l3_offset, uint64_t l2_offset_0, uint64_t l2_offset_1);

    const uint8_t m_initial_l3_cut;
    const uint16_t m_initial_l3_offset;
    const uint64_t m_l2_offset_0;
    const uint64_t m_l2_offset_1;
};

class WaitForSeqeuncerAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const ProtoHEFAction& proto_action);
    WaitForSeqeuncerAction(WaitForSeqeuncerAction &&) = default;
    WaitForSeqeuncerAction(const WaitForSeqeuncerAction &) = delete;
    WaitForSeqeuncerAction &operator=(WaitForSeqeuncerAction &&) = delete;
    WaitForSeqeuncerAction &operator=(const WaitForSeqeuncerAction &) = delete;
    virtual ~WaitForSeqeuncerAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    WaitForSeqeuncerAction(const ProtoHEFAction& proto_action);
};

class AllowInputDataflowAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const ProtoHEFAction& proto_action);
    AllowInputDataflowAction(AllowInputDataflowAction &&) = default;
    AllowInputDataflowAction(const AllowInputDataflowAction &) = delete;
    AllowInputDataflowAction &operator=(AllowInputDataflowAction &&) = delete;
    AllowInputDataflowAction &operator=(const AllowInputDataflowAction &) = delete;
    virtual ~AllowInputDataflowAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    static ContextSwitchConfigAction::Type get_input_dataflow_action_type(const ProtoHEFAction& proto_action);

    AllowInputDataflowAction(const ProtoHEFAction& proto_action);
};

class WaitForModuleConfigDoneAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const ProtoHEFAction& proto_action);
    WaitForModuleConfigDoneAction(WaitForModuleConfigDoneAction &&) = default;
    WaitForModuleConfigDoneAction(const WaitForModuleConfigDoneAction &) = delete;
    WaitForModuleConfigDoneAction &operator=(WaitForModuleConfigDoneAction &&) = delete;
    WaitForModuleConfigDoneAction &operator=(const WaitForModuleConfigDoneAction &) = delete;
    virtual ~WaitForModuleConfigDoneAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    WaitForModuleConfigDoneAction(const ProtoHEFAction& proto_action);
};

class DdrPairInfoAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create(const vdma::ChannelId &h2d_channel_id,
        const vdma::ChannelId &d2h_channel_id, uint32_t descriptors_per_frame, uint16_t descs_count);
    DdrPairInfoAction(DdrPairInfoAction &&) = default;
    DdrPairInfoAction(const DdrPairInfoAction &) = delete;
    DdrPairInfoAction &operator=(DdrPairInfoAction &&) = delete;
    DdrPairInfoAction &operator=(const DdrPairInfoAction &) = delete;
    virtual ~DdrPairInfoAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    DdrPairInfoAction(const vdma::ChannelId &h2d_channel_id, const vdma::ChannelId &d2h_channel_id,
        uint32_t descriptors_per_frame, uint16_t descs_count);

    const vdma::ChannelId m_h2d_channel_id;
    const vdma::ChannelId m_d2h_channel_id;
    const uint32_t m_descriptors_per_frame;
    const uint16_t m_descs_count;
};

class StartDdrBufferingTaskAction : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();
    StartDdrBufferingTaskAction(StartDdrBufferingTaskAction &&) = default;
    StartDdrBufferingTaskAction(const StartDdrBufferingTaskAction &) = delete;
    StartDdrBufferingTaskAction &operator=(StartDdrBufferingTaskAction &&) = delete;
    StartDdrBufferingTaskAction &operator=(const StartDdrBufferingTaskAction &) = delete;
    virtual ~StartDdrBufferingTaskAction() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    StartDdrBufferingTaskAction();
};

class EdgeLayerActivationActionsPositionMarker : public ContextSwitchConfigAction
{
public:
    static Expected<ContextSwitchConfigActionPtr> create();
    EdgeLayerActivationActionsPositionMarker(EdgeLayerActivationActionsPositionMarker &&) = default;
    EdgeLayerActivationActionsPositionMarker(const EdgeLayerActivationActionsPositionMarker &) = delete;
    EdgeLayerActivationActionsPositionMarker &operator=(EdgeLayerActivationActionsPositionMarker &&) = delete;
    EdgeLayerActivationActionsPositionMarker &operator=(const EdgeLayerActivationActionsPositionMarker &) = delete;
    virtual ~EdgeLayerActivationActionsPositionMarker() = default;

    virtual hailo_status execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer) override;
    virtual bool supports_repeated_block() const override;

private:
    EdgeLayerActivationActionsPositionMarker();
};

} /* namespace hailort */

#endif /* _HEF_INTERNAL_HPP_ */
