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
#include "hailo/hailort_defaults.hpp"

#include "hef/core_op_metadata.hpp"
#include "hef/layer_info.hpp"
#include "hef/context_switch_actions.hpp"
#include "net_flow/ops/op.hpp"
#include "net_flow/pipeline/pipeline.hpp"
#include "core_op/core_op.hpp"
#include "device_common/control_protocol.hpp"

#include "control_protocol.h"
#include <functional>
#include <bitset>
#include <memory>

extern "C" {
#include "md5.h"
}


namespace hailort
{

class CoreOpMetadata;
class CoreOp;
using ProtoHEFNetworkGroupPtr = std::shared_ptr<ProtoHEFNetworkGroup>;

struct ProtoHEFCoreOpMock;
struct ProtoHEFPartialCoreOpMock {
    ProtoHEFPartialCoreOpMock(std::shared_ptr<ProtoHEFCoreOpMock> core_op, const ProtoHEFPhysicalLayout &layout)
        : core_op(core_op)
        , layout(layout)
    {}

    ProtoHEFPartialCoreOpMock(const ProtoHEFPartialCoreOpMock &partial_core_op)
        : core_op(partial_core_op.core_op)
        , layout(partial_core_op.layout)
    {}

    std::shared_ptr<ProtoHEFCoreOpMock> core_op;
    const ProtoHEFPhysicalLayout &layout;
};

struct ProtoHEFCoreOpMock {
    ProtoHEFCoreOpMock(
        const ProtoHEFNetworkGroupMetadata &network_group_metadata,
        const ProtoHEFPreliminaryConfig &preliminary_config,
        const google::protobuf::RepeatedPtrField<ProtoHEFContext> &contexts,
        const google::protobuf::RepeatedPtrField<std::string> &sorted_outputs_order,
        const ProtoHEFFusedLayersMetadata &fused_layers_metadata,
        const google::protobuf::RepeatedPtrField<std::string> &networks_names,
        const std::vector<std::shared_ptr<ProtoHEFPartialCoreOpMock>> &partial_core_ops)
    : network_group_metadata(network_group_metadata),
      preliminary_config(preliminary_config),
      contexts(contexts),
      sorted_outputs_order(sorted_outputs_order),
      fused_layers_metadata(fused_layers_metadata),
      networks_names(networks_names),
      partial_core_ops(partial_core_ops)
    {}

    ProtoHEFCoreOpMock(const ProtoHEFCoreOpMock &core_op)
        : network_group_metadata(core_op.network_group_metadata),
        preliminary_config(core_op.preliminary_config),
        contexts(core_op.contexts),
        sorted_outputs_order(core_op.sorted_outputs_order),
        fused_layers_metadata(core_op.fused_layers_metadata),
        networks_names(core_op.networks_names),
        partial_core_ops(core_op.partial_core_ops)
    {}

    const ProtoHEFNetworkGroupMetadata &network_group_metadata;
    const ProtoHEFPreliminaryConfig &preliminary_config;
    const google::protobuf::RepeatedPtrField<ProtoHEFContext> &contexts;
    const google::protobuf::RepeatedPtrField<std::string> &sorted_outputs_order;
    const ProtoHEFFusedLayersMetadata &fused_layers_metadata;
    const google::protobuf::RepeatedPtrField<std::string> &networks_names;
    std::vector<std::shared_ptr<ProtoHEFPartialCoreOpMock>> partial_core_ops;
};

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

typedef enum {
    HAILO_NET_FLOW_OP_TYPE_NMS       = 0,
    HAILO_NET_FLOW_OP_TYPE_ARGMAX    = 1,
    HAILO_NET_FLOW_OP_TYPE_SOFTMAX   = 2,

    /** Max enum value to maintain ABI Integrity */
    HAILO_NET_FLOW_OP_TYPE_MAX_ENUM          = HAILO_MAX_ENUM
} hailo_net_flow_op_type_t;

struct NetFlowElement
{
    std::string name;
    std::shared_ptr<net_flow::Op> op;
    std::set<std::string> input_streams;
    hailo_nms_info_t nms_info;
    hailo_net_flow_op_type_t op_type;
    hailo_vstream_info_t output_vstream_info; // Should be vector?
};

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
    KO_RUN_ASAP,
    HAILO_NET_FLOW,
    HAILO_NET_FLOW_YOLO_NMS, // Extention added in platform 4.12 release
    HAILO_NET_FLOW_SSD_NMS, // Extention added in platform 4.14 release
    WRITE_DATA_BY_TYPE, // Extention added in platform 4.14 release
    NMS_OUTPUT_BURST, // Extention added in platform 4.14 release
    DUAL_DIRECTION_STREAM_INDEX, // Extention added in platform 4.14 release
    HAILO_NET_FLOW_ARGMAX, // Extention added in platform 4.14 release
    HAILO_NET_FLOW_SOFTMAX, // Extention added in platform 4.14 release
    ALIGNED_FORMAT_TYPE, // Extention added in platform 4.14 release
    HAILO_NET_FLOW_YOLOX_NMS, // Extention added in platform 4.14 release
    OUTPUT_SCALE_PER_FEATURE, // Extension added in platform 4.14 release
    PERIPH_CALCULATION_IN_HAILORT, // Extension added in platform 4.14 release
};

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
class VdmaConfigCoreOp;
class VdmaDevice;
class HailoRTDriver;


class Hef::Impl final
{
public:
    static const uint32_t HEADER_MAGIC = 0x01484546;
    static const uint32_t HEADER_VERSION = 0;

    static Expected<Impl> create(const std::string &hef_path);
    static Expected<Impl> create(const MemoryView &hef_buffer);

    const std::vector<ProtoHEFNetworkGroupPtr>& network_groups() const;
    const std::vector<ProtoHEFCoreOpMock>& core_ops(const std::string &net_group_name) const;
    const NetworkGroupMetadata network_group_metadata(const std::string &net_group_name) const;

    Expected<std::pair<std::string, std::string>> get_network_group_and_network_name(const std::string &name);

    Expected<std::shared_ptr<ProtoHEFCoreOpMock>> get_core_op_by_net_group_name(const std::string &net_group_name="");
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
    static bool contains_ddr_layers(const ProtoHEFCoreOpMock &core_op);
    static hailo_status validate_core_op_unique_layer_names(const ProtoHEFCoreOpMock &core_op);
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

    Expected<ConfigureNetworkParams> create_configure_params(hailo_stream_interface_t stream_interface, const std::string &network_group_name);
    Expected<ConfigureNetworkParams> create_configure_params_mipi_input(hailo_stream_interface_t output_interface,
        const hailo_mipi_input_stream_params_t &mipi_params, const std::string &network_group_name);

    static Expected<std::vector<WriteMemoryInfo>> create_single_context_core_op_config(
        const ProtoHEFPreliminaryConfig& proto_config);

    static Expected<std::shared_ptr<ProtoHEFCoreOpMock>> get_core_op_per_arch(const ProtoHEFCoreOpMock &core_op,
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
    // Also adds information to CoreOpMetadata
    // TODO: When supporting multiple core ops in same netflow - Change metadata param to a map of core_ops_metadata.
    Expected<std::vector<std::shared_ptr<NetFlowElement>>> create_net_flow_ops(const ProtoHEFNetworkGroup &network_group_proto,
        CoreOpMetadata &core_op_metadata, const ProtoHEFHwArch &hef_arch) const;

    // TODO: Should return map of NG's core_ops metadata?
    Expected<CoreOpMetadataPtr> get_core_op_metadata(const std::string &network_group_name, uint32_t partial_clusters_layout_bitmap = PARTIAL_CLUSTERS_LAYOUT_IGNORE);

    Expected<std::string> get_description(bool stream_infos, bool vstream_infos, hailo_device_architecture_t device_arch);

    const MD5_SUM_t &md5() const
    {
        return m_md5;
    }

    static hailo_status update_network_batch_size(ConfigureNetworkParams &network_group_config_params)
    {
        static_assert(HAILO_DEFAULT_BATCH_SIZE == 0, "Invalid HAILO_DEFAULT_BATCH_SIZE");

        auto single_network_default_batch = (HAILO_DEFAULT_BATCH_SIZE == network_group_config_params.batch_size);
        auto multi_network_default_batch = true;
        /* Batch size overide logic - if user modifies network group batch size
        and not the network batch size,  */

        for (auto const &network_params : network_group_config_params.network_params_by_name) {
            if (HAILO_DEFAULT_BATCH_SIZE != network_params.second.batch_size) {
                multi_network_default_batch = false;
            }
        }

        CHECK((single_network_default_batch || multi_network_default_batch), HAILO_INVALID_OPERATION, 
            "User provided batch size for network group and for network as well. User is adviced to work with network's batch size only");

        if (!single_network_default_batch && multi_network_default_batch) {
            /* In case user works with network group, overide the network batch size.*/
            for (auto &network_params : network_group_config_params.network_params_by_name) {
                network_params.second.batch_size = network_group_config_params.batch_size;
            }
        }

        return HAILO_SUCCESS;
    }

    // TODO: HRT-8875 -  Change to validate all core ops under same ng or use this func in the new configure API and use this to validate each op when configured.
    hailo_status validate_boundary_streams_were_created(const std::string &network_group_name, std::shared_ptr<CoreOp> core_op);

#ifdef HAILO_SUPPORT_MULTI_PROCESS
    const MemoryView get_hef_memview();
#endif // HAILO_SUPPORT_MULTI_PROCESS

private:
    Impl(const std::string &hef_path, hailo_status &status);
    Impl(const MemoryView &hef_memview, hailo_status &status);

    hailo_status parse_hef_file(const std::string &hef_path);
    hailo_status parse_hef_memview(const MemoryView &hef_memview);
    hailo_status transfer_protobuf_field_ownership(ProtoHEFHef &hef_message);
    void fill_core_ops();
    hailo_status fill_networks_metadata();
    void fill_extensions_bitset();
    void init_md5(MD5_SUM_t &calculated_md5);

    static bool check_hef_extension(const ProtoHEFExtensionType &extension, const ProtoHEFHeader &header,
        const std::vector<ProtoHEFExtension> &hef_extensions, const ProtoHEFIncludedFeatures &included_features);
    // Note: If the network group is found, i.e has_value() is true on the returned object, then the underlying pointer is not null
    static bool check_hef_optional_extension(const ProtoHEFExtensionType &extension, const ProtoHEFHeader &header,
        const std::vector<ProtoHEFOptionalExtension> &hef_optional_extensions);
    static SupportedFeatures get_supported_features(const ProtoHEFHeader &header,
        const std::vector<ProtoHEFExtension> &hef_extensions, const ProtoHEFIncludedFeatures &included_features,
        const std::vector<ProtoHEFOptionalExtension> &hef_optional_extensions);

    hailo_status validate_hef_extensions();
    static hailo_status validate_hef_header(const hef__header_t &header, MD5_SUM_t &calculated_md5, size_t proto_size);

    Expected<std::map<std::string, hailo_format_t>> get_inputs_vstream_names_and_format_info(
        const std::string &net_group_name, const std::string &network_name);
    Expected<std::map<std::string, hailo_format_t>> get_outputs_vstream_names_and_format_info(
        const std::string &net_group_name, const std::string &network_name);

    static Expected<std::string> get_vstream_name_from_original_name_mux(const std::string &original_name, const ProtoHefEdge &layer);
    static Expected<std::vector<std::string>> get_original_names_from_vstream_name_mux(const std::string &vstream_name, const ProtoHefEdge &layer);

    Expected<CoreOpMetadataPtr> create_metadata_per_arch(const ProtoHEFCoreOpMock &core_op, const std::vector<std::string> &sorted_network_names); // TODO: Remove sorted_network_names
    Expected<std::vector<std::string>> get_stream_infos_description(const std::string &network_group_name, const std::string &network_name);
    Expected<std::vector<std::string>> get_vstream_infos_description(const std::string &network_group_name, const std::string &network_name);
    Expected<std::vector<std::string>> get_post_processes_infos_description(const std::string &network_group_name);

    // Hef information
    ProtoHEFHeader m_header;
    ProtoHEFIncludedFeatures m_included_features;
    SupportedFeatures m_supported_features;
    std::vector<ProtoHEFNetworkGroupPtr> m_groups;
    std::map<std::string, std::vector<ProtoHEFCoreOpMock>> m_core_ops_per_group;
    std::map<std::string, std::vector<std::shared_ptr<NetFlowElement>>> m_post_process_ops_per_group;
    std::vector<ProtoHEFExtension> m_hef_extensions;
    std::vector<ProtoHEFOptionalExtension> m_hef_optional_extensions;
    std::bitset<SUPPORTED_EXTENSIONS_BITSET_SIZE> m_supported_extensions_bitset;
    MD5_SUM_t m_md5;

#ifdef HAILO_SUPPORT_MULTI_PROCESS
    Buffer m_hef_buffer;
#endif // HAILO_SUPPORT_MULTI_PROCESS

    std::map<std::string, NetworkGroupMetadata> m_network_group_metadata; // Key is NG name
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

    static Expected<uint32_t> max_periph_bytes_value(const hailo_device_architecture_t hw_arch);
    static Expected<uint32_t> max_periph_bytes_value(const hailo_stream_interface_t interface);

    static bool is_hw_padding_supported(const ProtoHEFEdgeLayer &edge_layer, const uint32_t max_periph_bytes_value);
    static bool is_hw_padding_supported(const LayerInfo &layer_info, const uint32_t max_periph_bytes_value);
private:
    static Expected<CONTROL_PROTOCOL__nn_stream_config_t> parse_nn_stream_config(hailo_format_order_t format_order,
        uint32_t width, uint32_t features, uint32_t hw_data_bytes, uint16_t core_buffers_per_frame,
        uint16_t core_bytes_per_buffer, bool hw_padding_supported, bool is_ddr, uint16_t periph_buffers_per_frame,
        uint16_t periph_bytes_per_buffer);

    static bool is_hw_padding_supported(bool is_boundary, bool is_mux, hailo_format_order_t format_order,
        uint16_t core_buffers_per_frame, uint32_t height, uint32_t width, uint32_t features, uint32_t hw_data_bytes,
        const uint32_t max_periph_bytes_value);
};

class HefUtils final
{
public:
    HefUtils() = delete;

    static hailo_status fill_boundary_layers_info(
        const ProtoHEFCoreOpMock &core_op,
        const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer,
        const SupportedFeatures &supported_features,
        ContextMetadata &context_metadata,
        const ProtoHEFHwArch &hef_arch);
    static Expected<LayerInfo> get_inter_context_layer_info(
        const ProtoHEFCoreOpMock &core_op, const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features, const ProtoHEFHwArch &hef_arch);
    static hailo_status fill_inter_context_layers_info(
        const ProtoHEFCoreOpMock &core_op,
        const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer,
        const SupportedFeatures &supported_features,
        ContextMetadata &context_metadata, const ProtoHEFHwArch &hef_arch);
    static Expected<LayerInfo> get_ddr_layer_info(
        const ProtoHEFCoreOpMock &core_op, const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features, const ProtoHEFHwArch &hef_arch);
    static hailo_status fill_ddr_layers_info(
        const ProtoHEFCoreOpMock &core_op,
        const uint8_t context_index,
        const ProtoHEFEdgeLayer &layer,
        const SupportedFeatures &supported_features,
        ContextMetadata &context_metadata, const ProtoHEFHwArch &hef_arch);
    static hailo_status check_ddr_pairs_match(
        const std::vector<LayerInfo> &context_ddr_input_layers,
        const std::vector<LayerInfo> &context_ddr_output_layers,
        const uint8_t context_index);
    static Expected<ContextMetadata> parse_preliminary_context(const ProtoHEFPreliminaryConfig &preliminary_proto,
        const SupportedFeatures &supported_features);
    static Expected<ContextMetadata> parse_single_dynamic_context(const ProtoHEFCoreOpMock &core_op,
        const ProtoHEFContext &context_proto, uint8_t context_index, const SupportedFeatures &supported_features,
        const ProtoHEFHwArch &hef_arch);
    static Expected<std::vector<ContextMetadata>> parse_dynamic_contexts(const ProtoHEFCoreOpMock &core_op,
        const SupportedFeatures &supported_features, const ProtoHEFHwArch &hef_arch);
    static Expected<hailo_nms_info_t> parse_proto_nms_info(const ProtoHEFNmsInfo &proto_nms_info,
        const bool burst_mode_enabled, const ProtoHEFHwArch &hef_arch);
    static Expected<LayerInfo> get_boundary_layer_info(const ProtoHEFCoreOpMock &core_op,
        const uint8_t context_index, const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features,
        const ProtoHEFHwArch &hef_arch);
    
    static Expected<std::string> get_partial_network_name_by_index(const ProtoHEFCoreOpMock &core_op, uint8_t network_index, const SupportedFeatures &supported_features);

    static std::string get_network_group_name(const ProtoHEFNetworkGroup &net_group, const SupportedFeatures &supported_features);
    static std::string get_network_name(const ProtoHEFCoreOpMock &core_op, const std::string &partial_network_name);
    static std::string get_network_name(const std::string &net_group_name, const std::string &partial_network_name);

private:
    static hailo_status fill_layer_info_with_base_info(const ProtoHEFEdgeLayerBase &base_info,
        const ProtoHEFEdgeConnectionType &edge_connection_type,
        const ProtoHEFNetworkGroupMetadata &network_group_proto, bool hw_padding_supported, bool transposed, 
        const uint8_t context_index, const uint8_t network_index, LayerInfo &layer_info,
        const SupportedFeatures &supported_features, const ProtoHEFHwArch &hef_arch);
    static hailo_status fill_layer_info(const ProtoHEFEdgeLayerInfo &info,
        const ProtoHEFEdgeConnectionType &edge_connection_type,
        const ProtoHEFCoreOpMock &core_op, hailo_stream_direction_t direction,
        bool hw_padding_supported, const uint8_t context_index, const std::string &partial_network_name, 
        uint8_t network_index, LayerInfo &layer_info, const SupportedFeatures &supported_features,
        const ProtoHEFHwArch &hef_arch);
    static hailo_status fill_fused_nms_info(const ProtoHEFEdgeLayerFused &info,
            LayerInfo &layer_info, hailo_quant_info_t &defuse_quant_info, const std::string &network_name,
            const bool burst_mode_enabled, const ProtoHEFHwArch &hef_arch);
    static hailo_status fill_mux_info(const ProtoHEFEdgeLayerMux &info,
        const ProtoHEFEdgeConnectionType &edge_connection_type,
        const ProtoHEFCoreOpMock &core_op, hailo_stream_direction_t direction,
        bool hw_padding_supported, const uint8_t context_index, const std::string &partial_network_name, 
        uint8_t network_index, LayerInfo &layer_info, const SupportedFeatures &supported_features,
        const ProtoHEFHwArch &hef_arch);
};

} /* namespace hailort */

#endif /* _HEF_INTERNAL_HPP_ */
