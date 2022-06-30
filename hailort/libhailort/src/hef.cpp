/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file hef.cpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#include "hailo/hailort.h"
#include "hailo/hef.hpp"
#include "hef_internal.hpp"
#include "hailo/stream.hpp"
#include "hailo/device.hpp"
#include "common/utils.hpp"
#include "hailo/hailort_common.hpp"
#include "hailort_defaults.hpp"

#include "hlpcie.hpp"
#include "pcie_device.hpp"
#include "context_switch/multi_context/vdma_config_manager.hpp"
#include "context_switch/single_context/hcp_config_manager.hpp"
#include "context_switch/single_context/hcp_config_network_group.hpp"
#include "byte_order.h"
#include "common/logger_macros.hpp"
#include "context_switch/hef_metadata.hpp"
#include "common/file_utils.hpp"
#include "layer_info.hpp"
#include "context_switch_defs.h"

#include <fstream>
#include <memory>
#include <limits>
#include <stdint.h>
#include <stdbool.h>
#include <set>
#include <algorithm>
#include <cstring>
#include <numeric>

namespace hailort
{

#define HEF__MD5_BUFFER_SIZE (1024)
#define CCW_BYTES_IN_WORD (4)
#define CCW_DATA_OFFSET (CCW_BYTES_IN_WORD * 2)
#define CCW_HEADER_SIZE (CCW_DATA_OFFSET)
#define DEFAULT_BATCH_SIZE (1)

static const uint8_t ENABLE_LCU_CONTROL_WORD[4] = {1, 0, 0, 0};

// Parse initial_l3 register from old hef
constexpr uint32_t HAILO8_INITIAL_L3_CUT_MASK = 0x0000007F;
constexpr uint32_t HAILO8_INITIAL_L3_OFFSET_MASK = 0x0007FF80L;
constexpr uint32_t HAILO8_INITIAL_L3_OFFSET_SHIFT = 7;
constexpr uint32_t HAILO8_INITIAL_L3_OFFSET_BYTES_GRANULARITY_SHIFT = 3;
constexpr uint64_t CCW_NOP = 0x0;

#pragma pack(push, 1)
typedef struct {
    uint32_t words_count;
    uint32_t address;
} CcwHeader;
#pragma pack(pop)

typedef struct {
    std::set<uint8_t> D2H_channels_in_use;
    std::set<uint8_t> H2D_channels_in_use;
} ContextSwitchChannelsParsingInfo;

// Note: Can't add the definition in the header. This will lead to the following error:
//       /usr/include/c++/7/bits/unique_ptr.h: In instantiation of ‘void std::default_delete<_Tp>::operator()(_Tp*) const [with _Tp = Hef::Impl]’:
//       /usr/include/c++/7/bits/unique_ptr.h:263:17:   required from ‘std::unique_ptr<_Tp, _Dp>::~unique_ptr() [with _Tp = Hef::Impl; _Dp = std::default_delete<Hef::Impl>]’
//       /local/users/projects/platform-sw/hailort/libhailort/src/../include/hailo/hef.hpp:61:7:   required from ‘Expected<T>::~Expected() [with T = Hef]’
//       /local/users/projects/platform-sw/hailort/hailortcli/run_command.cpp:705:51:   required from here
//       /usr/include/c++/7/bits/unique_ptr.h:76:22: error: invalid application of ‘sizeof’ to incomplete type ‘Hef::Impl’
//         static_assert(sizeof(_Tp)>0,
Hef::~Hef() = default;
Hef::Hef(Hef &&) = default;
Hef &Hef::operator=(Hef &&) = default;

Expected<Hef> Hef::create(const std::string &hef_path)
{
    auto impl = Hef::Impl::create(hef_path);
    CHECK_EXPECTED(impl);

    // TODO: can we do this without the copy ctor here (i.e. make the impl as a unique_ptr to begin with)
    return Hef(make_unique_nothrow<Impl>(impl.release()));
}

Expected<Hef> Hef::create(const MemoryView &hef_buffer)
{
    auto impl = Hef::Impl::create(hef_buffer);
    CHECK_EXPECTED(impl);

    // TODO: can we do this without the copy ctor here (i.e. make the impl as a unique_ptr to begin with)
    return Hef(make_unique_nothrow<Impl>(impl.release()));
}

Hef::Hef(std::unique_ptr<Impl> pimpl) :
    pimpl(std::move(pimpl))
{}

Expected<std::vector<hailo_stream_info_t>> Hef::get_input_stream_infos(const std::string &name)
{
    auto network_pair = pimpl->get_network_group_and_network_name(name);
    CHECK_EXPECTED(network_pair);

    return pimpl->get_input_stream_infos(network_pair.value().first, network_pair.value().second);
}

Expected<std::vector<hailo_stream_info_t>> Hef::get_output_stream_infos(const std::string &name)
{
    auto network_pair = pimpl->get_network_group_and_network_name(name);
    CHECK_EXPECTED(network_pair);

    return pimpl->get_output_stream_infos(network_pair.value().first, network_pair.value().second);
}

Expected<std::vector<hailo_stream_info_t>> Hef::get_all_stream_infos(const std::string &name)
{
    auto network_pair = pimpl->get_network_group_and_network_name(name);
    CHECK_EXPECTED(network_pair);

    return pimpl->get_all_stream_infos(network_pair.value().first, network_pair.value().second);
}

Expected<std::vector<hailo_network_info_t>> Hef::get_network_infos(const std::string &net_group_name)
{
    auto names_pair = pimpl->get_network_group_and_network_name(net_group_name);
    CHECK_EXPECTED(names_pair);
    return pimpl->get_network_infos(names_pair->first);
}

Expected<hailo_stream_info_t> Hef::get_stream_info_by_name(const std::string &stream_name,
    hailo_stream_direction_t stream_direction, const std::string &net_group_name)
{
    // Addressing the situation where net_group_name == ""
    auto net_group_name_pair = pimpl->get_network_group_and_network_name(net_group_name);
    CHECK_EXPECTED(net_group_name_pair);
    auto net_group_name_str = net_group_name_pair->first;

    return pimpl->get_stream_info_by_name(stream_name, stream_direction, net_group_name_str);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::get_input_vstream_infos(const std::string &name)
{
    auto network_pair = pimpl->get_network_group_and_network_name(name);
    CHECK_EXPECTED(network_pair);

    return pimpl->get_input_vstream_infos(network_pair.value().first, network_pair.value().second);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::get_output_vstream_infos(const std::string &name)
{
    auto network_pair = pimpl->get_network_group_and_network_name(name);
    CHECK_EXPECTED(network_pair);

    return pimpl->get_output_vstream_infos(network_pair.value().first, network_pair.value().second);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::get_all_vstream_infos(const std::string &name)
{
    auto network_pair = pimpl->get_network_group_and_network_name(name);
    CHECK_EXPECTED(network_pair);

    return pimpl->get_all_vstream_infos(network_pair.value().first, network_pair.value().second);
}

Expected<std::vector<std::string>> Hef::get_sorted_output_names(const std::string &net_group_name)
{
    // Addressing the situation where net_group_name == ""
    auto net_group_name_pair = pimpl->get_network_group_and_network_name(net_group_name);
    CHECK_EXPECTED(net_group_name_pair);
    auto net_group_name_str = net_group_name_pair->first;

    return pimpl->get_sorted_output_names(net_group_name_str);
}

Expected<size_t> Hef::get_number_of_input_streams(const std::string &net_group_name)
{
    // Addressing the situation where net_group_name == ""
    auto net_group_name_pair = pimpl->get_network_group_and_network_name(net_group_name);
    CHECK_EXPECTED(net_group_name_pair);
    auto net_group_name_str = net_group_name_pair->first;

    return pimpl->get_number_of_input_streams(net_group_name_str);
}

Expected<size_t> Hef::get_number_of_output_streams(const std::string &net_group_name)
{
    // Addressing the situation where net_group_name == ""
    auto net_group_name_pair = pimpl->get_network_group_and_network_name(net_group_name);
    CHECK_EXPECTED(net_group_name_pair);
    auto net_group_name_str = net_group_name_pair->first;

    return pimpl->get_number_of_output_streams(net_group_name_str);
}

Expected<float64_t> Hef::get_bottleneck_fps(const std::string &net_group_name)
{
    return pimpl->get_bottleneck_fps(net_group_name);
}

Expected<std::string> Hef::get_vstream_name_from_original_name(const std::string &original_name,
    const std::string &net_group_name)
{
    return pimpl->get_vstream_name_from_original_name(original_name, net_group_name);
}

Expected<std::vector<std::string>> Hef::get_original_names_from_vstream_name(const std::string &stream_name,
    const std::string &net_group_name)
{
    return pimpl->get_original_names_from_vstream_name(stream_name, net_group_name);
}

Expected<std::vector<std::string>> Hef::get_stream_names_from_vstream_name(const std::string &vstream_name,
    const std::string &net_group_name)
{
    auto network_group_name_pair = pimpl->get_network_group_and_network_name(net_group_name);
    CHECK_EXPECTED(network_group_name_pair);
    auto net_group_name_str = network_group_name_pair->first;

    return pimpl->get_stream_names_from_vstream_name(vstream_name, net_group_name_str);
}

Expected<std::vector<std::string>> Hef::get_vstream_names_from_stream_name(const std::string &stream_name,
    const std::string &net_group_name)
{
    auto network_group_name_pair = pimpl->get_network_group_and_network_name(net_group_name);
    CHECK_EXPECTED(network_group_name_pair);
    auto net_group_name_str = network_group_name_pair->first;

    return pimpl->get_vstream_names_from_stream_name(stream_name, net_group_name_str);
}

Expected<Hef::Impl> Hef::Impl::create(const std::string &hef_path)
{
    hailo_status status = HAILO_UNINITIALIZED;

    Impl hef(hef_path, status);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed creating HEF");
        return make_unexpected(status);
    }

    return hef;
}

Expected<Hef::Impl> Hef::Impl::create(const MemoryView &hef_buffer)
{
    hailo_status status = HAILO_UNINITIALIZED;

    Impl hef(hef_buffer, status);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed creating HEF");
        return make_unexpected(status);
    }

    return hef;
}

static hailo_status calc_istream_md5(std::ifstream &s, MD5_SUM_t &calculated_md5)
{
    char md5_buffer[HEF__MD5_BUFFER_SIZE] = {};
    MD5_CTX md5 = {};

    auto beg_pos = s.tellg();
    CHECK(-1 != beg_pos, HAILO_FILE_OPERATION_FAILURE, "ifstream::tellg() failed");

    MD5_Init(&md5);
    while (!s.eof()) {
        s.read(md5_buffer, HEF__MD5_BUFFER_SIZE);
        CHECK(!s.bad(), HAILO_FILE_OPERATION_FAILURE, "ifstream::read() failed");
        MD5_Update(&md5, &md5_buffer, static_cast<size_t>(s.gcount()));
    }
    MD5_Final(calculated_md5, &md5);

    s.clear();
    s.seekg(beg_pos, s.beg);
    CHECK(s.good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::validate_hef_header(const hef__header_t &header, MD5_SUM_t &calculated_md5, size_t proto_size)
{
    CHECK(HEADER_MAGIC == BYTE_ORDER__htonl(header.magic), HAILO_INVALID_HEF,
        "HEF magic does not match. detected magic - {:x}", header.magic);

    CHECK(HEADER_VERSION == BYTE_ORDER__htonl(header.version), HAILO_INVALID_HEF, "HEF version does not match");

    CHECK(proto_size == BYTE_ORDER__htonl(header.hef_proto_length), HAILO_INVALID_HEF,
        "HEF file length does not match");

    CHECK(0 == memcmp(&calculated_md5, &header.expected_md5, sizeof(MD5_SUM_t)), HAILO_INVALID_HEF,
        "HEF md5 does not match");

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::validate_hef_extensions()
{
    std::vector<std::string> unsupported_extensions;
    for (const auto &extension : m_hef_extensions) {
        if ((extension.type_index() >= m_supported_extensions_bitset.size()) || !m_supported_extensions_bitset.test(extension.type_index())) {
            unsupported_extensions.emplace_back(extension.name());
        }
    }

    CHECK(unsupported_extensions.empty(), HAILO_INVALID_HEF, "Failed opening non-compatible HEF with the following unsupported extensions: {}",
        std::accumulate(std::next(unsupported_extensions.begin()), unsupported_extensions.end(), unsupported_extensions[0], 
        [] (std::string a, std::string b) { return std::move(a) + ", " + b; }));

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::parse_hef_file(const std::string &hef_path)
{
    auto hef_file = std::ifstream(hef_path, std::ios::in | std::ios::binary);
    CHECK(hef_file.is_open(), HAILO_OPEN_FILE_FAILURE, "Failed to open HEF file \"{}\". errno: {}", hef_path, errno);

    hef__header_t header = {};
    hef_file.read((char*)&header, sizeof(header));
    CHECK(hef_file.good(), HAILO_FILE_OPERATION_FAILURE, "Failed reading HEF header");

    auto proto_size = get_istream_size(hef_file);
    CHECK_EXPECTED_AS_STATUS(proto_size);

    MD5_SUM_t calculated_md5 = {};
    auto status = calc_istream_md5(hef_file, calculated_md5);
    CHECK_SUCCESS(status);

    status = validate_hef_header(header, calculated_md5, proto_size.value());
    CHECK_SUCCESS(status);

    ProtoHEFHef hef_message;
    auto rb = hef_message.ParseFromIstream(&hef_file);
    CHECK(rb, HAILO_INVALID_HEF, "Failed parsing HEF file");
    status = transfer_protobuf_field_ownership(hef_message);
    CHECK_SUCCESS(status);

    status = fill_networks_metadata();
    CHECK_SUCCESS(status);

    // Must be called after fill_networks_metadata
    status = validate_hef_extensions();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::parse_hef_memview(const MemoryView &hef_memview)
{
    CHECK(hef_memview.size() >= sizeof(hef__header_t), HAILO_INVALID_HEF, "Invalid HEF header");
    const hef__header_t &header = reinterpret_cast<const hef__header_t&>(*hef_memview.data());

    auto proto_buffer = (hef_memview.data() + sizeof(header));
    auto proto_size = (hef_memview.size() - sizeof(header));

    MD5_CTX md5 = {};
    MD5_SUM_t calculated_md5 = {};
    MD5_Init(&md5);
    MD5_Update(&md5, proto_buffer, proto_size);
    MD5_Final(calculated_md5, &md5);

    auto status = validate_hef_header(header, calculated_md5, proto_size);
    CHECK_SUCCESS(status);

    ProtoHEFHef hef_message;
    auto rb = hef_message.ParseFromArray(proto_buffer, static_cast<int>(proto_size));
    CHECK(rb, HAILO_INVALID_HEF, "Failed parsing HEF buffer");
    status = transfer_protobuf_field_ownership(hef_message);
    CHECK_SUCCESS(status);

    status = fill_networks_metadata();
    CHECK_SUCCESS(status);

    // Must be called after fill_networks_metadata
    status = validate_hef_extensions();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::fill_networks_metadata()
{
    fill_extensions_bitset();
    auto supported_features = get_supported_features(m_header, m_hef_extensions, m_included_features,
        m_hef_optional_extensions);

    for (auto &network_group : m_groups) {

        CHECK(!contains(m_network_group_metadata,
            network_group->network_group_metadata().network_group_name()),
            HAILO_INVALID_OPERATION, "Network group with the name {} is already configured on the device",
            network_group->network_group_metadata().network_group_name());

        auto layer_infos = HefUtils::get_all_layers_info(*network_group, supported_features);
        CHECK_EXPECTED_AS_STATUS(layer_infos);

        auto sorted_output_names = HefUtils::get_sorted_output_names(*network_group);
        CHECK_EXPECTED_AS_STATUS(sorted_output_names);

        std::vector<std::string> sorted_network_names;
        if (supported_features.multi_network_support) {
            sorted_network_names.reserve(network_group->networks_names().size());
            for (auto &partial_network_name : network_group->networks_names()) {
                auto network_name = HefUtils::get_network_name(*network_group, partial_network_name);
                sorted_network_names.push_back(network_name);
            }
        } else {
            sorted_network_names.push_back(HailoRTDefaults::get_network_name(network_group->network_group_metadata().network_group_name()));
        }

        NetworkGroupMetadata metadata(network_group->network_group_metadata().network_group_name(),
            layer_infos.release(), sorted_output_names.release(), supported_features, sorted_network_names);
        m_network_group_metadata.emplace(
            network_group->network_group_metadata().network_group_name(), metadata);
    }

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::transfer_protobuf_field_ownership(ProtoHEFHef &hef_message)
{
    m_groups.reserve(hef_message.network_groups().size());
    while (!hef_message.network_groups().empty()) {
        // We pass the ownership from protobuf to shared_ptr (it'll call delete when the refcount drops to 0)
        // Note: Protobuf messages are allocated with new
        const auto network_group = hef_message.mutable_network_groups()->ReleaseLast();
        CHECK(nullptr != network_group, HAILO_INTERNAL_FAILURE, "Null network group found while parsing HEF; Unexpected");
        m_groups.emplace_back(network_group);
    }

    m_hef_extensions.reserve(hef_message.extensions().size());
    for (const auto &extension : hef_message.extensions()) {
        m_hef_extensions.emplace_back(extension);
    }

    m_header.CopyFrom(hef_message.header());
    m_included_features.CopyFrom(hef_message.included_features());

    m_hef_optional_extensions.reserve(hef_message.optional_extensions().size());
    for (const auto &optional_extension : hef_message.optional_extensions()) {
        m_hef_optional_extensions.emplace_back(optional_extension);
    }

    return HAILO_SUCCESS;
}

Hef::Impl::Impl(const std::string &hef_path, hailo_status &status)
{
    status = HAILO_UNINITIALIZED;
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    status = parse_hef_file(hef_path);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed parsing HEF file");
        return;
    }

    status = HAILO_SUCCESS;
}

Hef::Impl::Impl(const MemoryView &hef_memview, hailo_status &status)
{
    status = HAILO_UNINITIALIZED;
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    status = parse_hef_memview(hef_memview);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed parsing HEF buffer");
        return;
    }

    status = HAILO_SUCCESS;
}

void Hef::Impl::fill_extensions_bitset()
{
    for (auto extension : SUPPORTED_EXTENSIONS) {
        m_supported_extensions_bitset[extension] = 1;
    }
}

NetworkGroupSupportedFeatures Hef::Impl::get_supported_features(const ProtoHEFHeader &header,
        const std::vector<ProtoHEFExtension> &hef_extensions, const ProtoHEFIncludedFeatures &included_features,
        const std::vector<ProtoHEFOptionalExtension> &hef_optional_extensions)
{
    NetworkGroupSupportedFeatures supported_features{};
    supported_features.padded_ddr_buffers = check_hef_extension(ProtoHEFExtensionType::PADDED_DDR_BUFFERS, header,
        hef_extensions, included_features);
    supported_features.multi_network_support = check_hef_optional_extension(ProtoHEFExtensionType::MULTI_NETWORK_VARIABLE_BATCH_SIZE,
        header, hef_optional_extensions);
    supported_features.multi_context = check_hef_extension(ProtoHEFExtensionType::IS_MULTI_CONTEXTS, header,
        hef_extensions, included_features);
    supported_features.preliminary_run_asap = check_hef_extension(ProtoHEFExtensionType::PRELIMINARY_RUN_ASAP,
        header, hef_extensions, included_features);

    return supported_features;
}

hailo_status get_hw_padding_params(hailo_format_order_t format_order, uint32_t width, uint32_t features, uint32_t hw_data_bytes, 
    uint16_t &feature_padding_payload, uint16_t &periph_bytes_per_buffer)
{
    uint32_t feature_padding_payload_32bit = 0; 
    uint32_t periph_bytes_per_buffer_32bit = 0;

    // TODO: HRT-3278 dont assume core_buffers_per_frame == height    
    switch (format_order)
    {
    case HAILO_FORMAT_ORDER_NHCW:
    case HAILO_FORMAT_ORDER_NHW:
        feature_padding_payload_32bit = width * hw_data_bytes;
        periph_bytes_per_buffer_32bit = feature_padding_payload_32bit * features;
        break;
    case HAILO_FORMAT_ORDER_NHWC:
    case HAILO_FORMAT_ORDER_FCR:
    case HAILO_FORMAT_ORDER_F8CR:
    case HAILO_FORMAT_ORDER_NC:
    case HAILO_FORMAT_ORDER_BAYER_RGB:
    case HAILO_FORMAT_ORDER_12_BIT_BAYER_RGB:
    case HAILO_FORMAT_ORDER_RGB888:
        feature_padding_payload_32bit = features * hw_data_bytes;
        periph_bytes_per_buffer_32bit = feature_padding_payload_32bit * width;
        break;
    default:
        LOGGER__ERROR("unsupported format for HW padding");
        return HAILO_INTERNAL_FAILURE;
    }

    CHECK(IS_FIT_IN_UINT16(feature_padding_payload_32bit), HAILO_INVALID_HEF, 
        "frame width {} is too big", feature_padding_payload_32bit);
    CHECK(IS_FIT_IN_UINT16(periph_bytes_per_buffer_32bit), HAILO_INVALID_HEF,
        "unpadded bytes per buffer {} is too big", periph_bytes_per_buffer_32bit);

    feature_padding_payload = static_cast<uint16_t>(feature_padding_payload_32bit);
    periph_bytes_per_buffer = static_cast<uint16_t>(periph_bytes_per_buffer_32bit);

    return HAILO_SUCCESS;
}

Expected<CONTROL_PROTOCOL__nn_stream_config_t> HefConfigurator::parse_nn_stream_config(hailo_format_order_t format_order, uint32_t width, uint32_t features,
    uint32_t hw_data_bytes, uint16_t core_buffers_per_frame, uint16_t core_bytes_per_buffer, bool hw_padding_supported, bool is_ddr)
{
    CONTROL_PROTOCOL__nn_stream_config_t stream_config = {};

    stream_config.core_buffers_per_frame = core_buffers_per_frame;
    stream_config.core_bytes_per_buffer = core_bytes_per_buffer;

    /* For DDR buffering - core buffers is depended on the amount of buffers per PCIe interrupt. No HW padding required */
    if (is_ddr) {
        stream_config.core_buffers_per_frame = DDR_NUMBER_OF_ROWS_PER_INTERRUPT;
        stream_config.feature_padding_payload = 0;
        stream_config.periph_bytes_per_buffer = stream_config.core_bytes_per_buffer;
    } else {
        if (hw_padding_supported) {
            auto status = get_hw_padding_params(format_order, width, features, hw_data_bytes,
                stream_config.feature_padding_payload, stream_config.periph_bytes_per_buffer);
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else {
            stream_config.feature_padding_payload = 0;
            stream_config.periph_bytes_per_buffer = stream_config.core_bytes_per_buffer;
        }
        /* For now, no support for buffer padding */
        stream_config.buffer_padding_payload = 0;
        stream_config.buffer_padding = 0;
    }
    return stream_config;
}

Expected<CONTROL_PROTOCOL__nn_stream_config_t> HefConfigurator::parse_nn_stream_config(const ProtoHEFEdgeLayerBase &edge_layer,
    bool hw_padding_supported, const ProtoHEFEdgeConnectionType &edge_connection_type)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(edge_layer.core_bytes_per_buffer()), HAILO_INVALID_HEF,
        "core_bytes_per_buffer is too big");
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(edge_layer.core_buffers_per_frame()), HAILO_INVALID_HEF,
        "core_buffers_per_frame is too big");

    auto format_order_exp = HailoRTDefaults::get_device_format_order(edge_layer.format());
    CHECK_EXPECTED(format_order_exp);
    auto format_order = format_order_exp.release();
    auto is_ddr = ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__DDR == edge_connection_type;

    // Width and features only used in case hw_padding is supported. In that case, they represent the HW shape (without padding)
    return parse_nn_stream_config(format_order, edge_layer.width(), edge_layer.features(),
        edge_layer.data_bytes(), static_cast<uint16_t>(edge_layer.core_buffers_per_frame()),
        static_cast<uint16_t>(edge_layer.core_bytes_per_buffer()), hw_padding_supported, is_ddr);
}

Expected<CONTROL_PROTOCOL__nn_stream_config_t> HefConfigurator::parse_nn_stream_config(const LayerInfo &edge_layer, bool hw_padding_supported)
{
    auto is_ddr = false; // This function is called only on boundary layers, so no DDR
    return parse_nn_stream_config(edge_layer.format.order, edge_layer.hw_shape.width, edge_layer.hw_shape.features,
        edge_layer.hw_data_bytes, edge_layer.core_buffers_per_frame, edge_layer.core_bytes_per_buffer, hw_padding_supported,
        is_ddr);
}


bool HefConfigurator::is_hw_padding_supported(bool is_boundary, bool is_mux, hailo_format_order_t format_order,
    uint16_t core_buffers_per_frame, uint32_t height, uint32_t width, uint32_t features, uint32_t hw_data_bytes)
{
    if (!is_boundary || is_mux) {
        return false;
    }

    // TODO: HRT-4462 support more orders
    switch (format_order)
    {
    case HAILO_FORMAT_ORDER_NHCW:
        break;
    default:
        LOGGER__DEBUG("HW padding is not supported for format {} ", format_order);
        return false;
    }

    if (core_buffers_per_frame != height) {
        // TODO: HRT-3278
        LOGGER__DEBUG("HW padding is supported only on layers with core_buffers_per_frame == height");
        return false;
    }

    if (((width * features) % 8) != 0) {
        // TODO: HRT-963 support chunks
        LOGGER__DEBUG("HW padding is supported only when periph_bytes_per_buffer is a multiple of 8");
        return false;
    }

    if((width * features * hw_data_bytes) > 
        (HAILO8_INBOUND_DATA_STREAM_SIZE - 1)) {
        // TODO: HRT-4177
        LOGGER__DEBUG("HW padding is supported only on layers with features * width * data size > stream size");
        return false;
    }
    return true;
}

bool HefConfigurator::is_hw_padding_supported(const LayerInfo &layer_info)
{
    /* If the network is transposed, the width and height are swapped in LayerInfo c'tor, so need to swap it again for calculations */
    auto height = layer_info.shape.height;
    auto width = layer_info.shape.width;
    if (layer_info.format.flags & HAILO_FORMAT_FLAGS_TRANSPOSED) {
        std::swap(height, width);
    }

    auto is_boundary = true; // This function is called only on boundary layers
    return is_hw_padding_supported(is_boundary, layer_info.is_mux, layer_info.format.order,layer_info.core_buffers_per_frame,
        height, width, layer_info.shape.features, layer_info.hw_data_bytes);
}

bool HefConfigurator::is_hw_padding_supported(const ProtoHEFEdgeLayer &edge_layer)
{
    auto is_boundary = (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY == edge_layer.context_switch_info().edge_connection_type());
    auto is_mux = (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__MUX == edge_layer.edge_layer_type());
    auto edge_layer_base = edge_layer.layer_info().edge_layer_base();
    auto format_order_exp = HailoRTDefaults::get_device_format_order(edge_layer_base.format());
    if (!format_order_exp) {
        LOGGER__DEBUG("Failed to get format order. Not enabling hw padding");
        return false;
    }
    CHECK_EXPECTED_AS_STATUS(format_order_exp);
    auto format_order = format_order_exp.release();

    if (!IS_FIT_IN_UINT16(edge_layer_base.core_buffers_per_frame())) {
        LOGGER__DEBUG("Invalid core_buffers_per_frame. Not enabling hw padding");
        return false;
    }

    return is_hw_padding_supported(is_boundary, is_mux, format_order, static_cast<uint16_t>(edge_layer_base.core_buffers_per_frame()),
        edge_layer_base.height(), edge_layer_base.width(), edge_layer_base.features(), edge_layer_base.data_bytes());
}

Expected<std::vector<hailo_stream_info_t>> Hef::Impl::get_input_stream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);
    return network_group_metadata->get_input_stream_infos(network_name);
}

Expected<std::vector<hailo_stream_info_t>> Hef::Impl::get_output_stream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);
    return network_group_metadata->get_output_stream_infos(network_name);
}

Expected<std::vector<hailo_stream_info_t>> Hef::Impl::get_all_stream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);
    return network_group_metadata->get_all_stream_infos(network_name);
}

Expected<std::vector<hailo_network_info_t>> Hef::Impl::get_network_infos(const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);
    return network_group_metadata->get_network_infos();
}

Expected<hailo_stream_info_t> Hef::Impl::get_stream_info_by_name(const std::string &stream_name,
    hailo_stream_direction_t stream_direction, const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    if (HAILO_H2D_STREAM == stream_direction) {
        auto stream_infos = network_group_metadata->get_input_stream_infos();
        CHECK_EXPECTED(stream_infos);
        for (auto &stream_info : stream_infos.value()) {
            if (stream_name == stream_info.name) {
                return std::move(stream_info);
            }
        }
    } else {
        auto stream_infos = network_group_metadata->get_output_stream_infos();
        CHECK_EXPECTED(stream_infos);
        for (auto &stream_info : stream_infos.value()) {
            if (stream_name == stream_info.name) {
                return std::move(stream_info);
            }
        }
    }

    return make_unexpected(HAILO_NOT_FOUND);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::Impl::get_input_vstream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);
    return network_group_metadata->get_input_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::Impl::get_output_vstream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);
    return network_group_metadata->get_output_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::Impl::get_all_vstream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);
    return network_group_metadata->get_all_vstream_infos(network_name);
}

const std::vector<ProtoHEFNetworkGroupPtr>& Hef::Impl::network_groups() const
{
    return m_groups;
};

bool Hef::Impl::check_hef_extension(const ProtoHEFExtensionType &extension, const ProtoHEFHeader &header,
    const std::vector<ProtoHEFExtension> &hef_extensions, const ProtoHEFIncludedFeatures &included_features)
{
    if (header.version() > 0) {
        return std::find_if(hef_extensions.begin(), hef_extensions.end(),
            [extension] (const ProtoHEFExtension &extended_feature) { return ((ProtoHEFExtensionType)extended_feature.type_index()) == extension; }) != hef_extensions.end();
    }

    // ProtoHEFIncludedFeature is deprecated
    switch (extension) {
        case ProtoHEFExtensionType::ABBALE:
            return included_features.abbale();
        case ProtoHEFExtensionType::POSTED_WRITES:
            return included_features.posted_writes();
        case ProtoHEFExtensionType::DDR:
            return included_features.ddr();
        case ProtoHEFExtensionType::IS_MULTI_CONTEXTS:
            return (included_features.number_of_contexts() > 0);
        case ProtoHEFExtensionType::COMPRESSED_PARAMS:
            return included_features.compressed_params();
        case ProtoHEFExtensionType::TRANSPOSE_COMPONENT:
            return included_features.transpose_component();
        case ProtoHEFExtensionType::PADDED_DDR_BUFFERS:
            return included_features.padded_ddr_buffers();
        default:
            return false;
    }
}

bool Hef::Impl::check_hef_optional_extension(const ProtoHEFExtensionType &extension, const ProtoHEFHeader &header,
    const std::vector<ProtoHEFOptionalExtension> &hef_optional_extensions)
{
    if (header.version() > 0) {
        return std::find_if(hef_optional_extensions.begin(), hef_optional_extensions.end(),
            [extension] (const ProtoHEFOptionalExtension &extended_feature) { return ((ProtoHEFExtensionType)extended_feature.type_index()) == extension; }) != hef_optional_extensions.end();
    }

    /* optional extensions are only for m_header.version() > 0. 
       For lower version, those features are not supported */
    return false;
}

Expected<std::pair<std::string, std::string>> Hef::Impl::get_network_group_and_network_name(const std::string &name)
{
    std::string network_group_name;
    if (name.empty()) {
        // Name is not given - addressing all networks in the first network_group
        network_group_name = m_groups[0]->network_group_metadata().network_group_name();
        LOGGER__INFO("No name was given. Addressing all networks of default network_group: {}",
            network_group_name);
        auto network_name = HailoRTDefaults::get_network_name(network_group_name);
        return std::make_pair(network_group_name, network_name);
    } else {
        for (const auto &network_group : m_groups) {
            network_group_name = network_group->network_group_metadata().network_group_name();
            // Look for network_group with the given name
            if (name == network_group_name) {
                auto network_name = HailoRTDefaults::get_network_name(network_group_name);
                return std::make_pair(network_group_name, network_name);
            }
            // Look for network with the given name
            for (const auto &partial_network_name : network_group->networks_names()) {
                auto full_network_name = HefUtils::get_network_name(network_group_name, partial_network_name);
                if (name == full_network_name) {
                    return std::make_pair(network_group_name, full_network_name);
                }
            }
            // Handle case of deafult_network_name
            if (name == HailoRTDefaults::get_network_name(network_group_name)) {
                return std::make_pair(network_group_name, name);
            }
        }
    }

    LOGGER__ERROR("Failed to find network or network_group with the name {}",
        name);
    return make_unexpected(HAILO_NOT_FOUND);
}

Expected<ProtoHEFNetworkGroupPtr> Hef::Impl::get_net_group_by_name(const std::string &net_group_name)
{
    if ("" == net_group_name) {
        LOGGER__INFO("No network_group name was given. Addressing default network_group: {}",
            m_groups[0]->network_group_metadata().network_group_name());
        return Expected<ProtoHEFNetworkGroupPtr>(m_groups[0]);
    }
    for (auto const &net_group : m_groups) {
        CHECK_AS_EXPECTED(nullptr != net_group, HAILO_INTERNAL_FAILURE, "null netwrok group");
        if (net_group_name == net_group->network_group_metadata().network_group_name()) {
            return Expected<ProtoHEFNetworkGroupPtr>(net_group);
        }
    }
    LOGGER__ERROR("HEF does not contain network_group with name {}", net_group_name);
    return make_unexpected(HAILO_NOT_FOUND);
}

Expected<size_t> Hef::Impl::get_number_of_input_streams(const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    auto input_layer_infos = network_group_metadata->get_input_layer_infos();
    CHECK_EXPECTED(input_layer_infos);
    return input_layer_infos->size();
}

Expected<size_t> Hef::Impl::get_number_of_output_streams(const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    auto output_layer_infos = network_group_metadata->get_output_layer_infos();
    CHECK_EXPECTED(output_layer_infos);
    return output_layer_infos->size();
}

hailo_status HefUtils::fill_layer_info_with_base_info(const ProtoHEFEdgeLayerBase &base_info, 
    const ProtoHEFEdgeConnectionType &edge_connection_type, const ProtoHEFNetworkGroupMetadata &network_group_proto, 
    bool hw_padding_supported, bool transposed, LayerInfo &layer_info)
{
    auto format_order_exp = HailoRTDefaults::get_device_format_order(base_info.format());
    CHECK_EXPECTED_AS_STATUS(format_order_exp);

    auto format_oder = format_order_exp.release();

    if (HEF__FORMAT__NMS != base_info.format()) {
        layer_info.shape.height = base_info.height();
        layer_info.shape.width = base_info.width();
        layer_info.shape.features = base_info.features();
    } else {
        layer_info.shape.height = static_cast<uint32_t>(base_info.additional_info().nms_info().number_of_classes());
        layer_info.shape.width = HailoRTCommon::BBOX_PARAMS;
        layer_info.shape.features = static_cast<uint32_t>(base_info.additional_info().nms_info().max_output_size() *
            base_info.additional_info().nms_info().input_division_factor());
    }

    if (hw_padding_supported) {
        layer_info.hw_shape.height = base_info.height();
        layer_info.hw_shape.width = base_info.width();
        layer_info.hw_shape.features = base_info.features();
    }
    else {
        layer_info.hw_shape.height = base_info.padded_height();
        layer_info.hw_shape.width = base_info.padded_width();
        layer_info.hw_shape.features = base_info.padded_features();
    }
    layer_info.hw_data_bytes = base_info.data_bytes();

    // TODO: remove duplications with stream info parse
    layer_info.format.order = format_oder;
    layer_info.format.flags = HAILO_FORMAT_FLAGS_QUANTIZED;

    // The check network_group_proto.transposed_net() is for supporting backward compatability for old hefs
    if ((network_group_proto.transposed_net() || transposed) && (layer_info.format.order != HAILO_FORMAT_ORDER_NC))  {
        std::swap(layer_info.shape.height, layer_info.shape.width);
        layer_info.format.flags |= HAILO_FORMAT_FLAGS_TRANSPOSED;
    }

    if (base_info.host_argmax()) {
        layer_info.format.flags |= HAILO_FORMAT_FLAGS_HOST_ARGMAX;
        layer_info.shape.features = 1;
    }

    auto type = HailoRTCommon::get_format_type(layer_info.hw_data_bytes);
    CHECK_EXPECTED_AS_STATUS(type);
    layer_info.format.type = type.value();

    auto nn_stream_config = HefConfigurator::parse_nn_stream_config(base_info, hw_padding_supported, 
        edge_connection_type);
    CHECK_EXPECTED_AS_STATUS(nn_stream_config, "Failed parse nn stream config");
    layer_info.core_bytes_per_buffer = nn_stream_config->core_bytes_per_buffer;
    layer_info.core_buffers_per_frame = nn_stream_config->core_buffers_per_frame;

    if (!IS_FIT_IN_UINT8(base_info.sys_index())) {
        LOGGER__ERROR("Failed to parse HEF. Invalid sys_index {}.", base_info.sys_index());
        return HAILO_INVALID_HEF;
    }
    layer_info.index = static_cast<uint8_t>(base_info.sys_index());

    if (HAILO_FORMAT_ORDER_HAILO_NMS == layer_info.format.order) {
        auto expected_nms_info = parse_proto_nms_info(base_info.additional_info().nms_info());
        CHECK_EXPECTED_AS_STATUS(expected_nms_info);
        layer_info.nms_info = expected_nms_info.release();
    }

    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_layer_info(const ProtoHEFEdgeLayerInfo &info, 
    const ProtoHEFEdgeConnectionType &edge_connection_type, 
    const ProtoHEFNetworkGroup &net_group, hailo_stream_direction_t direction,
    bool hw_padding_supported, const std::string &partial_network_name, LayerInfo &layer_info)
{
    auto status = fill_layer_info_with_base_info(info.edge_layer_base(), edge_connection_type, net_group.network_group_metadata(),
        hw_padding_supported, info.transposed(), layer_info);
    CHECK_SUCCESS(status);

    if (HAILO_MAX_STREAM_NAME_SIZE < (info.name().length() + 1)) {
        LOGGER__ERROR("The edge layer '{}' has a too long name (max is HAILO_MAX_STREAM_NAME_SIZE)", info.name());
        return HAILO_INTERNAL_FAILURE;
    }
    if (HAILO_MAX_NETWORK_NAME_SIZE < (partial_network_name.length() + 1)) {
        LOGGER__ERROR("The network '{}' has a too long name (max is HAILO_MAX_NETWORK_NAME_SIZE)", partial_network_name);
        return HAILO_INTERNAL_FAILURE;
    }
    layer_info.name = info.name();

    layer_info.network_name = HefUtils::get_network_name(net_group, partial_network_name);
    layer_info.is_mux = false;
    layer_info.direction = direction;
    layer_info.quant_info.limvals_max = info.numeric_info().limvals_max();
    layer_info.quant_info.limvals_min = info.numeric_info().limvals_min();
    layer_info.quant_info.qp_scale = info.numeric_info().qp_scale();
    layer_info.quant_info.qp_zp = info.numeric_info().qp_zp();
    // Simulation info
    assert (1 == info.edge_layer_base().buffer_indices_size());
    layer_info.buffer_indices.cluster_index = info.edge_layer_base().buffer_indices(0).cluster_index();
    layer_info.buffer_indices.index = info.edge_layer_base().buffer_indices(0).index();

    layer_info.is_defused_nms = net_group.has_fused_layers_metadata() &&
        (HAILO_FORMAT_ORDER_HAILO_NMS == layer_info.format.order) && layer_info.nms_info.is_defused;

    if (layer_info.is_defused_nms) {
        for (const auto &fused_layer : net_group.fused_layers_metadata().fused_layers()) {
            if (fused_layer.layer_info().name() == layer_info.nms_info.defuse_info.original_name) {
                // This creates a new LayerInfo for the fused layer *for each defused layer*, even though they all share the same fused layer.
                // TODO Make it so all defused layer reference the same LayerInfo of the fused layer.
                LayerInfo fused_layer_info = {};
                status = fill_fused_nms_info(fused_layer, fused_layer_info, layer_info.quant_info, layer_info.network_name);
                CHECK_SUCCESS(status);
                layer_info.fused_nms_layer.push_back(fused_layer_info);
                break;
            }
        }
        CHECK(0 != layer_info.fused_nms_layer.size(), HAILO_NOT_FOUND, "Could not find the fused layer {}", layer_info.nms_info.defuse_info.original_name);
    }

    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_fused_nms_info(const ProtoHEFEdgeLayerFused &info, LayerInfo &layer_info,
    hailo_quant_info_t &defuse_quant_info, const std::string &network_name)
{
    auto base_info = info.layer_info().edge_layer_base();
    auto format_order_exp = HailoRTDefaults::get_device_format_order(base_info.format());
    CHECK_EXPECTED_AS_STATUS(format_order_exp);
    layer_info.format.order = format_order_exp.release();
    layer_info.format.flags = HAILO_FORMAT_FLAGS_QUANTIZED;

    layer_info.shape.height = static_cast<uint32_t>(info.nms_info().number_of_classes());
    layer_info.shape.width = HailoRTCommon::BBOX_PARAMS;
    layer_info.shape.features = static_cast<uint32_t>(info.nms_info().max_output_size() *
        info.nms_info().input_division_factor());

    layer_info.hw_data_bytes = base_info.data_bytes();

    auto type = HailoRTCommon::get_format_type(layer_info.hw_data_bytes);
    CHECK_EXPECTED_AS_STATUS(type);
    layer_info.format.type = type.value();

    auto expected_nms_info = parse_proto_nms_info(info.nms_info());
    CHECK_EXPECTED_AS_STATUS(expected_nms_info);
    layer_info.nms_info = expected_nms_info.release();

    if (HAILO_MAX_STREAM_NAME_SIZE < (info.layer_info().name().length() + 1)) {
        LOGGER__ERROR("The edge layer '{}' has a too long name (max is HAILO_MAX_STREAM_NAME_SIZE)", info.layer_info().name());
        return HAILO_INTERNAL_FAILURE;
    }
    layer_info.name = info.layer_info().name();
    layer_info.network_name = network_name;
    layer_info.is_mux = false;
    layer_info.direction = HAILO_D2H_STREAM;
    // Due to bug in SDK quant info of fused layer is empty, so we use the quant info of  the defused layer
    layer_info.quant_info = defuse_quant_info;

    // Simulation info
    assert (1 == info.layer_info().edge_layer_base().buffer_indices_size());
    layer_info.buffer_indices.cluster_index = info.layer_info().edge_layer_base().buffer_indices(0).cluster_index();
    layer_info.buffer_indices.index = info.layer_info().edge_layer_base().buffer_indices(0).index();

    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_mux_info(const ProtoHEFEdgeLayerMux &info,
    const ProtoHEFEdgeConnectionType &edge_connection_type, 
    const ProtoHEFNetworkGroup &net_group, hailo_stream_direction_t direction,
    bool hw_padding_supported, const std::string &partial_network_name, LayerInfo &layer_info)
{
    const bool transposed = false;
    auto status = fill_layer_info_with_base_info(info.edge_layer_base(), edge_connection_type, net_group.network_group_metadata(),
        hw_padding_supported, transposed, layer_info);
    CHECK_SUCCESS(status);

    if (HAILO_MAX_STREAM_NAME_SIZE < (info.name().length() + 1)) {
        LOGGER__ERROR("The edge layer '{}' has a too long name (max is HAILO_MAX_STREAM_NAME_SIZE)", info.name());
        return HAILO_INTERNAL_FAILURE;
    }
    if (HAILO_MAX_NETWORK_NAME_SIZE < (partial_network_name.length() + 1)) {
        LOGGER__ERROR("The network '{}' has a too long name (max is HAILO_MAX_NETWORK_NAME_SIZE)", partial_network_name);
        return HAILO_INTERNAL_FAILURE;
    }
    layer_info.name = info.name();

    layer_info.network_name = HefUtils::get_network_name(net_group, partial_network_name);
    layer_info.is_mux = true;
    layer_info.predecessor.reserve(info.mux_data().number_of_predecessors());
    layer_info.height_gcd = info.mux_data().height_gcd();
    layer_info.height_ratios.reserve(info.mux_data().height_ratios_list_len());
    for (const auto &height_ratio : info.mux_data().height_ratios_list()) {
        layer_info.height_ratios.emplace_back(height_ratio);
    }
    // Simulation info
    assert (1 == info.edge_layer_base().buffer_indices_size());
    layer_info.buffer_indices.cluster_index = info.edge_layer_base().buffer_indices(0).cluster_index();
    layer_info.buffer_indices.index = info.edge_layer_base().buffer_indices(0).index();

    for (uint32_t i = 0; i < info.mux_data().number_of_predecessors(); i++) {
        LayerInfo temp_layer = {};
        switch (info.predecessors(i).edge_case()) {
            case ProtoHefEdge::kLayerInfo:
                status = fill_layer_info(info.predecessors(i).layer_info(), edge_connection_type, net_group,
                    direction, hw_padding_supported, partial_network_name, temp_layer);
                if (HAILO_SUCCESS != status) {
                    return status;
                }
                layer_info.predecessor.push_back(temp_layer);
                break;
            case ProtoHefEdge::kLayerMux:
                status = fill_mux_info(info.predecessors(i).layer_mux(), edge_connection_type, net_group,
                    direction, hw_padding_supported, partial_network_name, temp_layer);
                if (HAILO_SUCCESS != status) {
                    return status;
                }
                layer_info.predecessor.push_back(temp_layer);
                break;
            default:
                LOGGER__ERROR("Invalid layer type");
                return HAILO_INTERNAL_FAILURE;
                break;
        }
    }

    return HAILO_SUCCESS;
}

Expected<std::vector<LayerInfo>> HefUtils::get_all_layers_info(const ProtoHEFNetworkGroup &network_group_proto,
    const NetworkGroupSupportedFeatures &supported_features)
{
    std::vector<LayerInfo> layers_info;
    for (uint8_t context_index = 0; context_index < network_group_proto.contexts_size(); context_index++) {
        auto &context_metadata = network_group_proto.contexts(context_index).metadata();
        for (int i = 0; i < context_metadata.edge_layers_size(); i++) {
            // We parse only boundary layers for user usage
            if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY !=
                context_metadata.edge_layers(i).context_switch_info().edge_connection_type()) {
                    continue;
                }
            auto layer_info = get_layer_info(network_group_proto, context_metadata.edge_layers(i), supported_features);
            CHECK_EXPECTED(layer_info);

            // Validate unique layer names
            for (const auto &layer : layers_info) {
                CHECK_AS_EXPECTED((layer.name != layer_info->name), HAILO_INVALID_HEF,
                    "Layer name should be unique. name '{}' appears more than once", layer_info->name);
            }

            layers_info.emplace_back(layer_info.release());
        }
    }
    return layers_info;
}

Expected<hailo_nms_info_t> HefUtils::parse_proto_nms_info(const ProtoHEFNmsInfo &proto_nms_info)
{
    hailo_nms_info_t nms_info = {};
    nms_info.number_of_classes = static_cast<uint32_t>(proto_nms_info.number_of_classes());
    nms_info.bbox_size = static_cast<uint32_t>(proto_nms_info.bbox_size());
    nms_info.max_bboxes_per_class = static_cast<uint32_t>(proto_nms_info.max_output_size());
    nms_info.chunks_per_frame = static_cast<uint32_t>(proto_nms_info.input_division_factor());
    if (nms_info.chunks_per_frame == 0) {
        // Old hef, use default value 1
        nms_info.chunks_per_frame = 1;
    }
    nms_info.is_defused = static_cast<bool>(proto_nms_info.is_defused());
    nms_info.defuse_info.class_group_index =
        static_cast<uint32_t>(proto_nms_info.defuse_info().class_group_index());

    CHECK_AS_EXPECTED(nms_info.defuse_info.class_group_index < HailoRTCommon::MAX_DEFUSED_LAYER_COUNT,
        HAILO_INVALID_HEF, "class_group_index from HEF is bigger than {}!", HailoRTCommon::MAX_DEFUSED_LAYER_COUNT);

    const std::string &original_name = proto_nms_info.defuse_info().original_name();
    CHECK_AS_EXPECTED(HAILO_MAX_STREAM_NAME_SIZE >= (original_name.length() + 1), HAILO_INTERNAL_FAILURE,
        "original_name field '{}' has a too long name (max is HAILO_MAX_STREAM_NAME_SIZE including the null terminated character)",
        original_name);
    strncpy(nms_info.defuse_info.original_name, original_name.c_str(), original_name.length() + 1);
    return nms_info;
}

Expected<LayerInfo> HefUtils::get_layer_info(const ProtoHEFNetworkGroup &net_group, const ProtoHEFEdgeLayer &layer,
    const NetworkGroupSupportedFeatures &supported_features)
{
    // We parse only boundary layers for user usage
    CHECK_AS_EXPECTED(
        ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY == layer.context_switch_info().edge_connection_type(),
        HAILO_INTERNAL_FAILURE, "get_layer_info can be called only on boundary layers");

    LayerInfo result = {};
    const auto direction = 
        (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST == layer.direction()) ?
        HAILO_D2H_STREAM : HAILO_H2D_STREAM;
    auto support_multi_networks = supported_features.multi_network_support;
    auto network_index = static_cast<uint8_t>((support_multi_networks) ? layer.network_index() : 0);
    auto partial_network_name = HefUtils::get_partial_network_name_by_index(net_group, network_index, supported_features);
    CHECK_EXPECTED(partial_network_name);
    const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(layer);
    if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type()) {
        // TODO: return LayerInfo
        auto status = fill_layer_info(layer.layer_info(), layer.context_switch_info().edge_connection_type(),
            net_group, direction, hw_padding_supported, partial_network_name.value(), result);
        CHECK_SUCCESS_AS_EXPECTED(status);
    } else if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__MUX == layer.edge_layer_type()) {
        // TODO: return LayerInfo
        auto status = fill_mux_info(layer.layer_mux(), layer.context_switch_info().edge_connection_type(), 
            net_group, direction, hw_padding_supported, partial_network_name.value(), result);
        CHECK_SUCCESS_AS_EXPECTED(status);
    } else {
        LOGGER__ERROR("Invalid layer type");
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    result.direction = (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST ==
            layer.direction()) ? HAILO_D2H_STREAM : HAILO_H2D_STREAM;

    return result;
}

Expected<std::vector<std::string>> HefUtils::get_sorted_output_names(const ProtoHEFNetworkGroup &net_group)
{
    if (net_group.fused_layers_metadata().network_has_fused_layers()) {
        return std::vector<std::string>(std::begin(net_group.fused_layers_metadata().updated_sorted_output_names()),
            std::end(net_group.fused_layers_metadata().updated_sorted_output_names()));
    } else if (0 != net_group.sorted_outputs_order_size()) {
        // For backwards compatibility before we've added updated_sorted_output_names
        return std::vector<std::string>(std::begin(net_group.sorted_outputs_order()),
            std::end(net_group.sorted_outputs_order()));
    } else {
        // For backwards compatibility before we've added this field
        uint32_t number_of_contexts = net_group.contexts_size();
        const auto& context_metadata = net_group.contexts(number_of_contexts - 1).metadata();

        CHECK_AS_EXPECTED(0 < context_metadata.sorted_outputs_order_size(), HAILO_INVALID_HEF,
            "Sorted output names is not set up in the HEF.");

        return std::vector<std::string>(std::begin(context_metadata.sorted_outputs_order()),
            std::end(context_metadata.sorted_outputs_order()));
    }
}

/* VdmaConfigNetworkGroup funcs */

inline std::pair<uint8_t, uint16_t> old_hef_parse_initial_l3(uint32_t initial_l3)
{
    // parse initial l3 as written in hailo8 initial_l3 format -
    //      7 bits of initial_l3_cut
    //      12 bits of initial_l3_offset, offset in 256 bits (8 bytes) granularity.
    const uint8_t initial_l3_cut = static_cast<uint8_t>(initial_l3 & HAILO8_INITIAL_L3_CUT_MASK);
    const uint32_t initial_l3_offset_256 = (initial_l3 & HAILO8_INITIAL_L3_OFFSET_MASK) >> HAILO8_INITIAL_L3_OFFSET_SHIFT;
    const uint16_t initial_l3_offset = static_cast<uint16_t>(initial_l3_offset_256 << HAILO8_INITIAL_L3_OFFSET_BYTES_GRANULARITY_SHIFT);
    return std::make_pair(initial_l3_cut, initial_l3_offset);
}

static uint32_t get_initial_credit_size(const ProtoHEFEdgeLayer &edge_layer_info)
{
    // On old HEFs the max shmifo is not defined, so 0 wil be returned and the firmware will choose the default value.
    return edge_layer_info.layer_info().edge_layer_base().max_shmifo_size();
} 

static hailo_status fill_boundary_input_layer(CONTROL_PROTOCOL__context_switch_context_info_t *context_info, 
    uint8_t **context_meta_data_head_pointer, uint8_t stream_index, const ProtoHEFEdgeLayer &edge_layer_info,
    const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    ContextSwitchChannelsParsingInfo &channels_parsing_info, ResourcesManager &resources_manager,
    const std::string &layer_name, const std::string &network_name, uint8_t network_index, 
    uint32_t frame_credits_in_bytes)
{
    auto channel_index = resources_manager.get_available_channel_index(
        channels_parsing_info.H2D_channels_in_use, ChannelInfo::Type::BOUNDARY, VdmaChannel::Direction::H2D, layer_name);
    CHECK_EXPECTED_AS_STATUS(channel_index);

    // Mark the channel info with the stream_index
    auto channel_info = resources_manager.get_channel_info(channel_index.value());
    CHECK_EXPECTED_AS_STATUS(channel_info);
    channel_info->get().set_pcie_stream_index(stream_index);

    auto vdma_channel = resources_manager.create_boundary_vdma_channel(channel_index.value(), frame_credits_in_bytes,
        network_name, layer_name, VdmaChannel::Direction::H2D);
    CHECK_EXPECTED_AS_STATUS(vdma_channel);

    // Lock the channel for further use in this net_group
    channels_parsing_info.H2D_channels_in_use.insert(channel_index.value());

    LOGGER__DEBUG("Boundary input stream: {} h2d_pcie_channel: {}.", stream_index, channel_index.value());

    /* Update metadata */
    const uint32_t initial_credit_size = get_initial_credit_size(edge_layer_info);
    auto status = HEF_METADATA__add_network_boundary_input_edge_layer(context_info, 
        context_meta_data_head_pointer, stream_index, channel_index.value(), network_index, nn_stream_config,
        vdma_channel.value()->get_page_size(), initial_credit_size);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status fill_inter_context_input_layer(CONTROL_PROTOCOL__context_switch_context_info_t *context_info, 
        uint8_t **context_meta_data_head_pointer, ResourcesManager &resources_manager, uint8_t dst_context, 
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
        const ProtoHEFEdgeLayer *edge_layer_info, ContextSwitchChannelsParsingInfo &channels_parsing_info,
        std::set<uint8_t> &channels_to_unlock,
        uint8_t network_index)
{
    uint8_t src_context = 0;
    uint8_t src_stream_index = 0;

    // Used to log layer info - debug only
    (void)dst_context;

    if (!(IS_FIT_IN_UINT8(edge_layer_info->context_switch_info().connected_context_index()))) {
        LOGGER__ERROR("Failed to parse HEF. Invalid connected_context index: {}.",
            edge_layer_info->context_switch_info().connected_context_index());
        return HAILO_INVALID_HEF;
    }
    src_context = static_cast<uint8_t>(edge_layer_info->context_switch_info().connected_context_index());

    if(!(IS_FIT_IN_UINT8(edge_layer_info->context_switch_info().connected_sys_index()))) {
        LOGGER__ERROR("Failed to parse HEF. Invalid connected_sys index: {}.",
            edge_layer_info->context_switch_info().connected_sys_index());
        return HAILO_INVALID_HEF;
    }
    src_stream_index = static_cast<uint8_t>(edge_layer_info->context_switch_info().connected_sys_index());

    /* Find next available h2d_channel, and mark it to unlock at the end of the context */
    auto h2d_channel_index = resources_manager.get_available_channel_index(channels_parsing_info.H2D_channels_in_use,
        ChannelInfo::Type::INTER_CONTEXT, VdmaChannel::Direction::H2D);
    CHECK_EXPECTED_AS_STATUS(h2d_channel_index);

    channels_parsing_info.H2D_channels_in_use.insert(h2d_channel_index.value());
    channels_to_unlock.insert(h2d_channel_index.value());

    /* Get inter context buffer previously created */
    auto intermediate_buffer_key = std::make_pair(src_context, src_stream_index);
    auto intermediate_buffer_exp = resources_manager.get_intermediate_buffer(intermediate_buffer_key);
    CHECK_EXPECTED_AS_STATUS(intermediate_buffer_exp, "Failed to find intermediate buffer for src context {}, src_stream_index {}",
        src_context, src_stream_index);
    auto &intermediate_buffer = intermediate_buffer_exp->get();

    LOGGER__DEBUG("Intermediate input stream {}, src_context:{}, dst_context: {}, h2d_pcie_channel {}.",
        stream_index, src_context, dst_context, h2d_channel_index.value());

    /* Update metadata */
    const uint32_t initial_credit_size = get_initial_credit_size(*edge_layer_info);
    return HEF_METADATA__add_inter_context_input_edge_layer(context_info, context_meta_data_head_pointer,
        stream_index, h2d_channel_index.value(), network_index, nn_stream_config,
        intermediate_buffer.get_host_buffer_info(), initial_credit_size);
}

static hailo_status fill_boundary_output_layer(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
        uint8_t **context_meta_data_head_pointer, ResourcesManager &resources_manager, uint8_t stream_index,
        const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, uint32_t frame_credits_in_bytes,
        ContextSwitchChannelsParsingInfo &channels_parsing_info, const std::string &layer_name, uint8_t network_index, 
        const std::string &network_name)
{
   auto channel_index = resources_manager.get_available_channel_index(channels_parsing_info.D2H_channels_in_use,
       ChannelInfo::Type::BOUNDARY, VdmaChannel::Direction::D2H, layer_name);
    CHECK_EXPECTED_AS_STATUS(channel_index);

    // Mark the channel info with the stream_index
    auto channel_info = resources_manager.get_channel_info(channel_index.value());
    CHECK_EXPECTED_AS_STATUS(channel_info);
    channel_info->get().set_pcie_stream_index(stream_index);


    auto vdma_channel = resources_manager.create_boundary_vdma_channel(channel_index.value(), frame_credits_in_bytes,
        network_name, layer_name, VdmaChannel::Direction::D2H);
    CHECK_EXPECTED_AS_STATUS(vdma_channel);
    auto page_size = vdma_channel.value()->get_page_size();

    // Lock the channel for further use in this net_group
    channels_parsing_info.D2H_channels_in_use.insert(channel_index.value());

    LOGGER__DEBUG("Boundary output stream: {} d2h_pcie_channel: {}.", stream_index, channel_index.value());

    /* Update metadata */
    auto status = HEF_METADATA__add_network_boundary_output_edge_layer(context_info, 
            context_meta_data_head_pointer, stream_index, channel_index.value(), network_index,
            nn_stream_config, frame_credits_in_bytes, page_size);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status fill_inter_context_output_layer(CONTROL_PROTOCOL__context_switch_context_info_t *context_info, 
        uint8_t **context_meta_data_head_pointer, ResourcesManager &resources_manager, uint8_t src_context,
        uint8_t stream_index, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config, uint32_t frame_credits_in_bytes,
        const ProtoHEFEdgeLayer *edge_layer_info, ContextSwitchChannelsParsingInfo &channels_parsing_info,
        std::set<uint8_t> &channels_to_unlock, uint8_t network_index, const std::string &network_name)
{
    std::vector<uint8_t> connected_h2d_channels;

    CHECK(IS_FIT_IN_UINT8(edge_layer_info->context_switch_info().connected_context_index()),
        HAILO_INVALID_HEF, "Failed to parse HEF. Invalid connected_context index: {}.",
        edge_layer_info->context_switch_info().connected_context_index());

    /* Find next available d2h_channel, and mark it to unlock at the end of the context */
    auto d2h_channel_index = resources_manager.get_available_channel_index(
        channels_parsing_info.D2H_channels_in_use, ChannelInfo::Type::INTER_CONTEXT, VdmaChannel::Direction::D2H);
    CHECK_EXPECTED_AS_STATUS(d2h_channel_index);

    channels_parsing_info.D2H_channels_in_use.insert(d2h_channel_index.value());
    channels_to_unlock.insert(d2h_channel_index.value());

    auto intermediate_buffer_exp = resources_manager.create_inter_context_buffer(frame_credits_in_bytes,
        stream_index, src_context, network_name);
    CHECK_EXPECTED_AS_STATUS(intermediate_buffer_exp);
    auto &intermediate_buffer = intermediate_buffer_exp->get();

    LOGGER__DEBUG("Intermediate output stream {}, src_context:{}, d2h_pcie_channel {}.",
        stream_index, src_context, d2h_channel_index.value());

    /* Update metadata */
    auto status = HEF_METADATA__add_inter_context_output_edge_layer(context_info, context_meta_data_head_pointer,
        stream_index, d2h_channel_index.value(), network_index, nn_stream_config, intermediate_buffer.get_host_buffer_info());
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

static hailo_status fill_ddr_layer_multi_context(CONTROL_PROTOCOL__context_switch_context_info_t *context_switch_info,
    uint8_t **context_meta_data_head_pointer, ResourcesManager &resources_manager, uint8_t context_index,
    const ProtoHEFEdgeLayer &edge_layer_proto, const CONTROL_PROTOCOL__nn_stream_config_t &nn_stream_config,
    hailo_stream_direction_t direction, ContextSwitchChannelsParsingInfo &channels_parsing_info, uint32_t frame_credits_in_bytes,
    std::set<uint8_t> &channels_to_unlock, uint8_t network_index)
{
    /* Find out if the connected layer has already been parsed */
    uint8_t channel_index = 0;

    CHECK(resources_manager.get_supported_features().padded_ddr_buffers, HAILO_INVALID_HEF,
        "Failed opening non-compatible HEF that uses the following deprecated features: host-managed DDR buffers." 
        "Please re-compile the HEF using a newer Dataflow Compiler version (v3.11.0 or newer)");
    
    if (HAILO_H2D_STREAM == direction) {
        auto channel_index_expected = resources_manager.get_available_channel_index(channels_parsing_info.H2D_channels_in_use,
            ChannelInfo::Type::DDR, VdmaChannel::Direction::H2D);
        CHECK_EXPECTED_AS_STATUS(channel_index_expected);
        channel_index = channel_index_expected.value();
        channels_parsing_info.H2D_channels_in_use.insert(channel_index);
        channels_to_unlock.insert(channel_index);
    } else if (HAILO_D2H_STREAM == direction) {
        auto channel_index_expected = resources_manager.get_available_channel_index(channels_parsing_info.D2H_channels_in_use,
            ChannelInfo::Type::DDR, VdmaChannel::Direction::D2H);
        CHECK_EXPECTED_AS_STATUS(channel_index_expected);
        channel_index = channel_index_expected.value();
        channels_parsing_info.D2H_channels_in_use.insert(channel_index);
        channels_to_unlock.insert(channel_index);
    } else {
        LOGGER__ERROR("Invalid layer direction");
        return HAILO_INVALID_ARGUMENT;
    }

    for (auto &ddr_info : resources_manager.ddr_infos()) {
        if (HAILO_H2D_STREAM == direction) {
            /* we have the info already, just validate saved info, add ch_index, and return success! */
            if ((ddr_info.row_size == edge_layer_proto.layer_info().edge_layer_base().core_bytes_per_buffer())
                && (ddr_info.context_index == context_index)
                && (ddr_info.d2h_stream_index == edge_layer_proto.context_switch_info().connected_sys_index())
                && (ddr_info.h2d_stream_index == static_cast<uint8_t>(edge_layer_proto.layer_info().edge_layer_base().sys_index()))) {
                ddr_info.h2d_channel_index = channel_index;
                LOGGER__DEBUG("DDR layer: input stream_index: {}, output stream_index: {}, h2d_pcie_channel {}, d2h_pcie_channel: {}.",
                    ddr_info.h2d_stream_index, ddr_info.d2h_stream_index, ddr_info.h2d_channel_index, ddr_info.d2h_channel_index);
                const uint32_t initial_credit_size = get_initial_credit_size(edge_layer_proto);
                return HEF_METADATA__add_ddr_buffer_input_edge_layer(context_switch_info,
                    context_meta_data_head_pointer, ddr_info.h2d_stream_index, ddr_info.h2d_channel_index, network_index,
                    nn_stream_config, ddr_info.intermediate_buffer->dma_address(), ddr_info.intermediate_buffer->depth(),
                    initial_credit_size);
            }
        } else if (HAILO_D2H_STREAM == direction) {
            /* we have the info already, just validate saved info, add ch_index, and return success! */
            if ((ddr_info.row_size == edge_layer_proto.layer_info().edge_layer_base().core_bytes_per_buffer())
                && (ddr_info.context_index == context_index)
                && (ddr_info.h2d_stream_index == edge_layer_proto.context_switch_info().connected_sys_index())
                && (ddr_info.d2h_stream_index == static_cast<uint8_t>(edge_layer_proto.layer_info().edge_layer_base().sys_index()))) {
                ddr_info.d2h_channel_index = channel_index;
                LOGGER__DEBUG("DDR layer: input stream_index: {}, output stream_index: {}, h2d_pcie_channel {}, d2h_pcie_channel: {}.",
                    ddr_info.h2d_stream_index, ddr_info.d2h_stream_index, ddr_info.h2d_channel_index, ddr_info.d2h_channel_index);
                return HEF_METADATA__add_ddr_buffer_output_edge_layer(context_switch_info,
                    context_meta_data_head_pointer, ddr_info.d2h_stream_index, ddr_info.d2h_channel_index, network_index,
                    nn_stream_config, frame_credits_in_bytes, ddr_info.intermediate_buffer->dma_address(), 
                    ddr_info.intermediate_buffer->desc_page_size(), ddr_info.intermediate_buffer->depth(),
                    ddr_info.min_buffered_rows);
            }
        } else {
            LOGGER__ERROR("Invalid layer direction");
            return HAILO_INVALID_ARGUMENT;
        }
    }

    /* Allocate resources and prepare ddr_info */
    DdrChannelsInfo local_info = {};
    local_info.context_index = context_index;
    CHECK(IS_FIT_IN_UINT8(edge_layer_proto.context_switch_info().connected_sys_index()),
        HAILO_INVALID_HEF, "Failed to parse HEF. Invalid connected_sys_index: {}.",
        edge_layer_proto.context_switch_info().connected_sys_index());
    auto connected_sys_index = static_cast<uint8_t>(edge_layer_proto.context_switch_info().connected_sys_index());
    if (HAILO_H2D_STREAM == direction) {
        local_info.h2d_channel_index = channel_index;
        local_info.h2d_stream_index = static_cast<uint8_t>(edge_layer_proto.layer_info().edge_layer_base().sys_index());
        local_info.d2h_stream_index = connected_sys_index;
    } else if (HAILO_D2H_STREAM == direction) {
        local_info.h2d_stream_index = connected_sys_index;
        local_info.d2h_channel_index = channel_index;
        local_info.d2h_stream_index = static_cast<uint8_t>(edge_layer_proto.layer_info().edge_layer_base().sys_index());
    } else {
        LOGGER__ERROR("Invalid layer direction");
        return HAILO_INVALID_ARGUMENT;
    }

    local_info.row_size = static_cast<uint16_t>(edge_layer_proto.layer_info().edge_layer_base().core_bytes_per_buffer());
    // We count on local_info.min_buffered_rows to be aligned to DDR_NUMBER_OF_ROWS_PER_INTERRUPT in the ddr threads
    local_info.min_buffered_rows = edge_layer_proto.context_switch_info().buffers();
    auto leftover = (local_info.min_buffered_rows % DDR_NUMBER_OF_ROWS_PER_INTERRUPT);
    if (0 != leftover) {
        local_info.min_buffered_rows += (DDR_NUMBER_OF_ROWS_PER_INTERRUPT - leftover);
    }

    /* Create descs list */
    auto ddr_buffer = resources_manager.create_ddr_buffer(local_info, context_index);
    CHECK_EXPECTED_AS_STATUS(ddr_buffer);

    local_info.intermediate_buffer = &ddr_buffer->get();

    CHECK(0 == (DEFAULT_DESC_PAGE_SIZE % local_info.intermediate_buffer->desc_page_size()), HAILO_INTERNAL_FAILURE,
        "In padded DDR buffers, desc list page size must be dividor of {}", DEFAULT_DESC_PAGE_SIZE);
    CHECK(0 == (local_info.row_size % local_info.intermediate_buffer->desc_page_size()), HAILO_INTERNAL_FAILURE, 
        "If HEF supports padded DDR buffers, row size must be a multiple of descriptor page size");
    local_info.descriptors_per_frame = (local_info.row_size / local_info.intermediate_buffer->desc_page_size()) * 
        edge_layer_proto.layer_info().edge_layer_base().core_buffers_per_frame();

    auto programed_descs = ddr_buffer->get().program_ddr();
    CHECK_EXPECTED_AS_STATUS(programed_descs);
    local_info.initial_programed_descs = programed_descs.release();
    local_info.desc_list_size_mask = static_cast<uint32_t>(local_info.intermediate_buffer->descs_count() - 1);

    // Add layer to metadata
    if (HAILO_H2D_STREAM == direction) {
        const uint32_t initial_credit_size = get_initial_credit_size(edge_layer_proto);
        auto status = HEF_METADATA__add_ddr_buffer_input_edge_layer(context_switch_info,
            context_meta_data_head_pointer, local_info.h2d_stream_index, local_info.h2d_channel_index, network_index,
            nn_stream_config, local_info.intermediate_buffer->dma_address(), local_info.intermediate_buffer->depth(),
            initial_credit_size);
        CHECK_SUCCESS(status);
    } else if (HAILO_D2H_STREAM == direction) {
        auto status = HEF_METADATA__add_ddr_buffer_output_edge_layer(context_switch_info,
            context_meta_data_head_pointer, local_info.d2h_stream_index, local_info.d2h_channel_index,
            network_index, nn_stream_config, frame_credits_in_bytes, local_info.intermediate_buffer->dma_address(), 
            local_info.intermediate_buffer->desc_page_size(), local_info.intermediate_buffer->depth(),
            local_info.min_buffered_rows);
        CHECK_SUCCESS(status);
    } else {
        LOGGER__ERROR("Invalid layer direction");
        return HAILO_INVALID_ARGUMENT;
    }

    resources_manager.ddr_infos().push_back(local_info);
    return HAILO_SUCCESS;
}

Expected<std::string> HefUtils::get_partial_network_name_by_index(const ProtoHEFNetworkGroup &network_group_proto, uint8_t network_index, 
    const NetworkGroupSupportedFeatures &supported_features)
{
    if (supported_features.multi_network_support) {
        CHECK_AS_EXPECTED(network_index < network_group_proto.networks_names_size(), HAILO_INVALID_ARGUMENT,
            "Requested name for network_index={}, however there are only {} networks in the network group",
            network_index, network_group_proto.networks_names_size());
        return std::string(network_group_proto.networks_names(network_index));
    } else {
        auto partial_network_name = network_group_proto.network_group_metadata().network_group_name();
        return partial_network_name;
    }
}

std::string HefUtils::get_network_name(const std::string &net_group_name, const std::string &partial_network_name)
{
    return net_group_name + HAILO_DEFAULT_NETWORK_NAME_QUALIFIER + partial_network_name;
}

std::string HefUtils::get_network_name(const ProtoHEFNetworkGroup &net_group, const std::string &partial_network_name)
{
    return HefUtils::get_network_name(net_group.network_group_metadata().network_group_name(), partial_network_name);
}

static hailo_status parse_and_fill_h2d_layer_multi_context(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer, const ProtoHEFEdgeLayer &edge_layer_proto,
    ContextSwitchChannelsParsingInfo &channels_parsing_info, ResourcesManager &resources_manager,
    uint8_t context_index, std::set<uint8_t> &channels_to_unlock, const std::string &network_name,
    uint8_t network_index)
{
    uint8_t stream_index = 0;
    uint32_t frame_credits_in_bytes = 0;

    CHECK(ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__INFO == edge_layer_proto.edge_layer_type(), HAILO_INVALID_HEF,
        "H2D layers must be info_layer");

    CHECK(IS_FIT_IN_UINT8(edge_layer_proto.layer_info().edge_layer_base().sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", edge_layer_proto.layer_info().edge_layer_base().sys_index());
    stream_index = static_cast<uint8_t>(edge_layer_proto.layer_info().edge_layer_base().sys_index());

    const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(edge_layer_proto);

    auto nn_stream_config = HefConfigurator::parse_nn_stream_config(edge_layer_proto.layer_info().edge_layer_base(), hw_padding_supported,
        edge_layer_proto.context_switch_info().edge_connection_type());
    CHECK_EXPECTED_AS_STATUS(nn_stream_config, "Failed parse nn stream config");
    auto layer_name = edge_layer_proto.layer_info().name();

    /* credits work on periph bytes */
    frame_credits_in_bytes = (nn_stream_config->periph_bytes_per_buffer * nn_stream_config->core_buffers_per_frame);

    switch (edge_layer_proto.context_switch_info().edge_connection_type()) {
        case ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY:
            return fill_boundary_input_layer(context_info, context_meta_data_head_pointer, stream_index,
                edge_layer_proto, *nn_stream_config, channels_parsing_info, resources_manager, layer_name,
                network_name, network_index, frame_credits_in_bytes);

        case ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__INTERMEDIATE:
            return fill_inter_context_input_layer(context_info, context_meta_data_head_pointer, resources_manager,
                context_index, stream_index, *nn_stream_config, &edge_layer_proto, channels_parsing_info,
                channels_to_unlock, network_index);

        case ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__DDR:
            return fill_ddr_layer_multi_context(context_info, context_meta_data_head_pointer, resources_manager, context_index,
                edge_layer_proto, *nn_stream_config, HAILO_H2D_STREAM, channels_parsing_info, frame_credits_in_bytes, 
                channels_to_unlock, network_index);

        default:
            LOGGER__ERROR("Invalid edge connection type");
            return HAILO_INTERNAL_FAILURE;
    }
}

static hailo_status parse_and_fill_d2h_layer_multi_context(
    CONTROL_PROTOCOL__context_switch_context_info_t *context_info, 
    uint8_t **context_meta_data_head_pointer, const ProtoHEFEdgeLayer &edge_layer_proto,
    ContextSwitchChannelsParsingInfo &channels_parsing_info, ResourcesManager &resources_manager,
    uint8_t context_index, std::set<uint8_t> &channels_to_unlock, const std::string &network_name,
    uint8_t network_index)
{
    uint8_t stream_index = 0;
    uint32_t frame_credits_in_bytes = 0;
    bool is_mux = false;
    std::string layer_name;
    CONTROL_PROTOCOL__nn_stream_config_t nn_stream_config = {};

    const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(edge_layer_proto);

    if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__INFO == edge_layer_proto.edge_layer_type()) {
        CHECK(IS_FIT_IN_UINT8(edge_layer_proto.layer_info().edge_layer_base().sys_index()), HAILO_INVALID_HEF,
            "Failed to parse HEF. Invalid sys_index: {}.", edge_layer_proto.layer_info().edge_layer_base().sys_index());
        stream_index = static_cast<uint8_t>(edge_layer_proto.layer_info().edge_layer_base().sys_index());
        auto nn_stream_config_expected = HefConfigurator::parse_nn_stream_config(edge_layer_proto.layer_info().edge_layer_base(), 
            hw_padding_supported, edge_layer_proto.context_switch_info().edge_connection_type());
        CHECK_EXPECTED_AS_STATUS(nn_stream_config_expected, "Failed parse nn stream config");
        nn_stream_config = nn_stream_config_expected.release();
        frame_credits_in_bytes = (nn_stream_config.periph_bytes_per_buffer * nn_stream_config.core_buffers_per_frame);
        is_mux = false;
        layer_name = edge_layer_proto.layer_info().name();
    } else if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__MUX == edge_layer_proto.edge_layer_type()) {
        CHECK(IS_FIT_IN_UINT8(edge_layer_proto.layer_mux().edge_layer_base().sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", edge_layer_proto.layer_mux().edge_layer_base().sys_index());
        stream_index = static_cast<uint8_t>(edge_layer_proto.layer_mux().edge_layer_base().sys_index());
        auto nn_stream_config_expected = HefConfigurator::parse_nn_stream_config(edge_layer_proto.layer_mux().edge_layer_base(), 
            hw_padding_supported, edge_layer_proto.context_switch_info().edge_connection_type());
        CHECK_EXPECTED_AS_STATUS(nn_stream_config_expected, "Failed parse nn stream config");
        nn_stream_config = nn_stream_config_expected.release();
        frame_credits_in_bytes = (nn_stream_config.periph_bytes_per_buffer * nn_stream_config.core_buffers_per_frame);
        is_mux = true;
        layer_name = edge_layer_proto.layer_mux().name();
    } else {
        LOGGER__ERROR("Invalid layer type.");
        return HAILO_INVALID_HEF;
    }

    switch (edge_layer_proto.context_switch_info().edge_connection_type()) {
        case ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY:
            return fill_boundary_output_layer(context_info, context_meta_data_head_pointer, resources_manager,
                stream_index, nn_stream_config, frame_credits_in_bytes, channels_parsing_info, layer_name,
                network_index, network_name);

        case ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__INTERMEDIATE:
            CHECK(!is_mux, HAILO_INVALID_HEF, "Inter-context layer can't be mux.");
            return fill_inter_context_output_layer(context_info, context_meta_data_head_pointer, resources_manager,
                context_index, stream_index, nn_stream_config, frame_credits_in_bytes,
                &edge_layer_proto, channels_parsing_info, channels_to_unlock, network_index, network_name);

        case ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__DDR:
            return fill_ddr_layer_multi_context(context_info, context_meta_data_head_pointer, resources_manager, context_index,
                edge_layer_proto, nn_stream_config, HAILO_D2H_STREAM, channels_parsing_info, frame_credits_in_bytes, 
                channels_to_unlock, network_index);

        default:
            LOGGER__ERROR("Invalid edge connection type");
            return HAILO_INTERNAL_FAILURE;
    }
}

static hailo_status fill_ddr_buffers_info(CONTROL_PROTOCOL__context_switch_context_info_t *context_info, 
    uint8_t **context_meta_data_head_pointer, ResourcesManager &resources_manager, uint8_t context_index)
{
    const CONTROL_PROTOCOL__TRIGGER_t none_trigger = HEF_METADATA__build_none_trigger();
    bool found_ddr_pair_for_context = false;
    // See: HRT-5373
    static const bool NOT_REPEATED = false;

    for (auto& ddr_info : resources_manager.ddr_infos()) {
        if (context_index == ddr_info.context_index) {
            /* Any action must have a trigger */
            auto status = HEF_METADATA__add_trigger_to_trigger_group(context_info, context_meta_data_head_pointer, 
                &none_trigger);
            CHECK_SUCCESS(status, "failed to add NONE trigger before ddr buffer pair infos");
            /* Add ddr pair info action */
            status = HEF_METADATA__add_ddr_pair_info(context_info, context_meta_data_head_pointer, 
                ddr_info.h2d_channel_index, ddr_info.d2h_channel_index, ddr_info.descriptors_per_frame, 
                ddr_info.initial_programed_descs, NOT_REPEATED);
            CHECK_SUCCESS(status,"failed to add ddr pair info");
            found_ddr_pair_for_context = true;
        }
    }

    if (found_ddr_pair_for_context) {
        /* No need to add NONE trigger. This action can be inside the last none trigger of the last DDR pair */
        auto status = HEF_METADATA__add_ddr_buffering_start(context_info, context_meta_data_head_pointer, NOT_REPEATED);
        CHECK_SUCCESS(status,"failed to add ddr buffer start action");
    }

    return HAILO_SUCCESS;
}

static hailo_status parse_and_fill_edge_layers_mapping(ProtoHEFNetworkGroupPtr network_group_proto, 
        CONTROL_PROTOCOL__context_switch_context_info_t *context_info, 
        uint8_t **context_meta_data_head_pointer, const ProtoHEFContextMetadata *context_metadata, 
        ContextSwitchChannelsParsingInfo &channels_parsing_info, ResourcesManager &resources_manager, uint8_t context_index)
{
    hailo_status status = HAILO_UNINITIALIZED;
    // We use those sets to unlock resources at the end of each context parsing to prevent reuse within the same context
    std::set<uint8_t> channels_to_unlock = {};

    const auto number_of_edge_layers = context_metadata->edge_layers_size();
    CHECK(0 < number_of_edge_layers, HAILO_INVALID_HEF, "No edge layers in this context");
    CHECK(IS_FIT_IN_UINT8(number_of_edge_layers), HAILO_INVALID_HEF, 
        "Failed to parse HEF. Invalid edge_layers_size: {}.", number_of_edge_layers);

    for (const auto &edge_layer_proto : context_metadata->edge_layers()) {
        auto support_multi_networks = resources_manager.get_supported_features().multi_network_support;
        auto network_index = static_cast<uint8_t>((support_multi_networks) ? edge_layer_proto.network_index() : 0);
        auto partial_network_name = HefUtils::get_partial_network_name_by_index(*network_group_proto, network_index,
            resources_manager.get_supported_features());
        CHECK_EXPECTED_AS_STATUS(partial_network_name);
        auto network_name = HefUtils::get_network_name(*network_group_proto, partial_network_name.value());

        if (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__HOST_TO_DEVICE == edge_layer_proto.direction()) {
            status = parse_and_fill_h2d_layer_multi_context(context_info, context_meta_data_head_pointer,
                edge_layer_proto, channels_parsing_info, resources_manager, context_index, channels_to_unlock,
                network_name, network_index);
            CHECK_SUCCESS(status);
        } else if (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST == edge_layer_proto.direction()) {
            status = parse_and_fill_d2h_layer_multi_context(context_info, context_meta_data_head_pointer, 
                edge_layer_proto, channels_parsing_info, resources_manager, context_index, channels_to_unlock,
                network_name, network_index);
            CHECK_SUCCESS(status);
        } else {
            LOGGER__ERROR("Invalid argument: stream_direction");
            return HAILO_INVALID_ARGUMENT;
        }
    }

    /* UN-Lock resources at the end of the context - 
        d2h inter-context and DDR buffer channels, h2d inter-context and DDR buffer channels */
    for (auto& channel_index : channels_to_unlock) {
        if (contains(channels_parsing_info.D2H_channels_in_use, channel_index)) {
            channels_parsing_info.D2H_channels_in_use.erase(channel_index);
        } else if (contains(channels_parsing_info.H2D_channels_in_use, channel_index)) {
            channels_parsing_info.H2D_channels_in_use.erase(channel_index);
        } else {
            LOGGER__ERROR("channel_index {} was marked for unlocking, but is not marked as in use in context {}",
                channel_index, context_index);
            return HAILO_INTERNAL_FAILURE;
        }
    }

    status = fill_ddr_buffers_info(context_info, context_meta_data_head_pointer, resources_manager, context_index);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

// Returns pairs of form [start, end] (inclusive) of repeated 'ContextSwitchConfigAction's in the given vector
static std::vector<std::pair<uint32_t, uint32_t>> get_repreated_actions_boundary_indices(
    const std::vector<ContextSwitchConfigActionPtr> &actions)
{
    const uint32_t num_actions = static_cast<uint32_t>(actions.size());

    std::vector<std::pair<uint32_t, uint32_t>> repeated_indexes;
    uint32_t start_index = 0;
    while (start_index < num_actions) {
        auto end_index = start_index + 1;
        do
        {
            if (end_index == num_actions) {
                break;
            }
            if (actions[start_index]->get_type() != actions[end_index]->get_type()) {
                break;
            }
            end_index++;
        } while (true);
        

        repeated_indexes.emplace_back(start_index, end_index - 1);
        start_index = end_index;
    }

    return repeated_indexes;
}

// Returns a map from start indexes of repeated actions to the size of the chunk (number of repeated actions)
static std::map<uint32_t, uint8_t> get_start_indexes_of_repeated_actions(
    const std::vector<ContextSwitchConfigActionPtr> &actions,
    const std::vector<std::pair<uint32_t, uint32_t>> &repeated_indexes,
    // TODO: get this from HardCoded config (HRT-5352)
    const std::set<ContextSwitchConfigAction::Type> &action_types_denylist = {})
{
    std::map<uint32_t, uint8_t> result;
    for (const auto &index_pair : repeated_indexes) {
        if (!actions[index_pair.first]->supports_repeated_block()) {
            continue;
        }

        if (contains(action_types_denylist, actions[index_pair.first]->get_type())) {
            continue;
        }

        // TODO: Move merge calculation to HRT-5352
        // Merge calculation (see also - CONTEXT_SWITCH_DEFS__repeated_action_header_t in common/include/context_switch_defs.h):
        // * Assume there are x repeated actions that can be merged
        // * Let a := sizeof(action_to_be_merged) [without CONTEXT_SWITCH_DEFS__common_action_header_t]
        // * sizeof(CONTEXT_SWITCH_DEFS__common_action_header_t) is 5
        // * sizeof(CONTEXT_SWITCH_DEFS__repeated_action_header_t) is 3
        // Then:
        // * original_size = x * (5 + a) = 5x + ax
        // * new_size = 5 + 3 + ax = 8 + ax
        // * new_size < original_size <=> 8 + ax < 5x + ax <=> 8 < 5x <=> 1.6 < x
        // Hence we merge for x >= 2
        static_assert(sizeof(CONTEXT_SWITCH_DEFS__common_action_header_t) == 5,
            "Merge calculation assumes that 'sizeof(CONTEXT_SWITCH_DEFS__common_action_header_t) == 5'");
        static_assert(sizeof(CONTEXT_SWITCH_DEFS__repeated_action_header_t) == 3,
            "Merge calculation assumes that 'sizeof(CONTEXT_SWITCH_DEFS__repeated_action_header_t) == 3'");
        static const uint32_t MIN_REQUIRED_FOR_MERGING = 2;

        uint32_t start_index = index_pair.first;
        const uint32_t end_index = index_pair.second;
        while (start_index < end_index) {
            const auto curr_chunk_size = static_cast<uint8_t>(std::min(
                static_cast<uint32_t>(std::numeric_limits<uint8_t>::max()),
                end_index - start_index + 1));
            if (curr_chunk_size < MIN_REQUIRED_FOR_MERGING) {
                break;
            }

            result.emplace(start_index, curr_chunk_size);

            start_index += curr_chunk_size;
        }
    }

    return result;
}

static std::set<uint32_t> get_end_indexes_of_action_type(
    const std::vector<ContextSwitchConfigActionPtr> &actions,
    const std::vector<std::pair<uint32_t, uint32_t>> &repeated_indexes,
    const ContextSwitchConfigAction::Type &required_action_type)
{
    std::set<uint32_t> result;
    for (const auto &index_pair : repeated_indexes) {
        const auto curr_action_type = actions[index_pair.first]->get_type();
        if (required_action_type != curr_action_type) {
            continue;
        }

        result.insert(index_pair.second);
    }

    return result;
}

static hailo_status proccess_write_ccw_action(const ContextSwitchConfigActionPtr &configuration_action,
    std::vector<ConfigBuffer> &config_resources, std::set<uint8_t> &pending_cfg_ch_buffer,
    std::vector<uint16_t> &total_ccw_bursts, const std::set<uint32_t> &end_indexes_of_write_ccw_actions, 
    const uint32_t &action_index, const bool support_pre_fetch, 
    std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    // Add the config channel index of the current WriteDataCcwAction
    const auto cfg_channel_index = configuration_action->get_proto_action().write_data_ccw().cfg_channel_index();
    CHECK(IS_FIT_IN_UINT8(cfg_channel_index), HAILO_INVALID_HEF, 
        "Failed to parse HEF. Invalid cfg_channel_index: {}.", cfg_channel_index);
    pending_cfg_ch_buffer.insert(static_cast<uint8_t>(cfg_channel_index));
            
    /* TODO - get CCW headers from proto (need to add it into the proto) */
    //const auto ccw_bursts = configuration_action->get_proto_action().write_data_ccw().ccw_bursts();
    const uint16_t ccw_bursts = 1;
    auto accum_ccw_bursts = total_ccw_bursts[cfg_channel_index] + ccw_bursts;
    CHECK(IS_FIT_IN_UINT16(accum_ccw_bursts), HAILO_INTERNAL_FAILURE,
        "Failed to parse HEF. action fetch ccw burst supports only to 2^16 bursts.");
    total_ccw_bursts[cfg_channel_index] = static_cast<uint16_t>(accum_ccw_bursts);

    // At the end of a consecutive group of WriteDataCcwActions, we program the
    // descriptors for all the config channels used.
    if (contains(end_indexes_of_write_ccw_actions, action_index)) {
        /* Add the last CCW write into the buffer */
        processed_configuration_actions.emplace_back(configuration_action);

        CHECK(total_ccw_bursts.size() == config_resources.size(), HAILO_INTERNAL_FAILURE, "Invalid cfg channels count");
        for (const auto cfg_channel : pending_cfg_ch_buffer) {
            CHECK(cfg_channel < config_resources.size(), HAILO_INTERNAL_FAILURE, "Invalid cfg channel index");

            auto fetch_config_action = support_pre_fetch ?
                AddCcwBurstAction::create(cfg_channel, total_ccw_bursts[cfg_channel]) :
                CreateConfigDescAndFetchAction::create(cfg_channel, config_resources[cfg_channel]);
            CHECK_EXPECTED_AS_STATUS(fetch_config_action);
            processed_configuration_actions.emplace_back(fetch_config_action.release());
        }

        /* Cleanups */
        pending_cfg_ch_buffer.clear();
        for (uint8_t cleanup_ch_index = 0; cleanup_ch_index < total_ccw_bursts.size(); cleanup_ch_index++) {
            total_ccw_bursts[cleanup_ch_index] = 0;
        }
    } else {
        processed_configuration_actions.emplace_back(configuration_action);
    }

    return HAILO_SUCCESS;
}

static hailo_status proccess_trigger_new_data_input_action(const ContextSwitchConfigActionPtr &configuration_action,
    const std::set<uint32_t> &end_indexes_of_trigger_new_data_input_actions, 
    const uint32_t &action_index, std::vector<ContextSwitchConfigActionPtr> &processed_configuration_actions)
{
    // At the end of a consecutive group of TriggerNewDataFromDataInput actions, we can trigger the BurstCreditsTask
    // in the FW, via StartBurstCreditsTaskAction.
    processed_configuration_actions.emplace_back(configuration_action);
    if (contains(end_indexes_of_trigger_new_data_input_actions, action_index)) {
        auto start_burst_credits_task_action = StartBurstCreditsTaskAction::create();
        CHECK_EXPECTED_AS_STATUS(start_burst_credits_task_action);
        processed_configuration_actions.emplace_back(start_burst_credits_task_action.release());
    }

    return HAILO_SUCCESS;
}

// Adds context switch configuration actions that don't appear in the HEF:
// * If groups of consecutive actions can be "merged" as repeated actions (saving room the FW's
//   action list) a RepeatedHeaderAction is placed before the relevant actions.
//   See also: CONTROL_PROTOCOL__REPEATED_ACTION_t's documnetion in control_protocol.h.
// * At the end of each consecutive group of WriteDataCcwAction, a CreateConfigDescAndFetchAction is added.
// * At the end of each consecutive group of TriggerNewDataFromDataInput, a StartBurstCreditsTaskAction is added.
static Expected<std::vector<ContextSwitchConfigActionPtr>> process_configuration_actions(
    std::vector<ContextSwitchConfigActionPtr> &input_configuration_actions,
    std::vector<ConfigBuffer> &config_resources, const bool support_pre_fetch)
{
    std::vector<ContextSwitchConfigActionPtr> processed_configuration_actions;
    
    std::set<uint8_t> pending_cfg_ch_buffer;
    std::vector<uint16_t> total_ccw_bursts(config_resources.size(), 0);

    const auto repeated_indexes = get_repreated_actions_boundary_indices(input_configuration_actions);
    const auto start_indexes_of_repeated_actions = get_start_indexes_of_repeated_actions(
        input_configuration_actions, repeated_indexes);
    const auto end_indexes_of_write_ccws = get_end_indexes_of_action_type(input_configuration_actions,
        repeated_indexes, ContextSwitchConfigAction::Type::WriteDataCcw);
    const auto end_indexes_of_trigger_new_data_from_inputs = get_end_indexes_of_action_type(
        input_configuration_actions, repeated_indexes, ContextSwitchConfigAction::Type::TriggerNewDataFromDataInput);
    for (uint32_t action_index = 0; action_index < input_configuration_actions.size(); action_index++) {
        // A group of actions can be "merged" as repeated actions.
        // Hence we add a RepeatedHeaderAction and mark all the actions in this group as "reapted"
        if (contains(start_indexes_of_repeated_actions, action_index)) {
            const auto num_repeates = start_indexes_of_repeated_actions.at(action_index);
            auto create_repeated_action = RepeatedHeaderAction::create(
                input_configuration_actions[action_index]->get_action_list_type(), num_repeates);
            CHECK_EXPECTED(create_repeated_action);
            processed_configuration_actions.emplace_back(create_repeated_action.release());
            for (uint32_t repeated_offset = 0; repeated_offset < num_repeates; repeated_offset++) {
                input_configuration_actions[action_index + repeated_offset]->set_is_in_repeated_block(true);
            }
        }

        // Add the current action
        const auto &configuration_action = input_configuration_actions[action_index];
        if (ContextSwitchConfigAction::Type::WriteDataCcw == configuration_action->get_type()) {
            auto status = proccess_write_ccw_action(configuration_action, config_resources, pending_cfg_ch_buffer,
                total_ccw_bursts, end_indexes_of_write_ccws, action_index, support_pre_fetch, processed_configuration_actions);
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else if (ContextSwitchConfigAction::Type::TriggerNewDataFromDataInput == configuration_action->get_type()) {
            auto status = proccess_trigger_new_data_input_action(configuration_action, end_indexes_of_trigger_new_data_from_inputs,
                action_index, processed_configuration_actions);
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else {
            // Add the current action
            processed_configuration_actions.emplace_back(configuration_action);
        }
    }

    return processed_configuration_actions;
}

static bool is_mercury_device_type(const ProtoHEFHwArch &hw_arch) 
{
    /* TODO - HRT-5067 - use one hw_arch for mercury */    
    return (PROTO__HW_ARCH__MERCURY == hw_arch) || (PROTO__HW_ARCH__GINGER == hw_arch) ||
        (PROTO__HW_ARCH__LAVENDER == hw_arch);
}

static hailo_status parse_actions_in_operation(const ProtoHEFOperation &operation,
    Device &device, const ProtoHEFHwArch &hw_arch, std::vector<ConfigBuffer> &config_resources, 
    const ResourcesManager &resources_manager, ProtoHEFNetworkGroupPtr network_group_proto, 
    CONTROL_PROTOCOL__context_switch_context_info_t &context_info, uint8_t **context_meta_data_head_pointer)
{
    auto support_pre_fetch = is_mercury_device_type(hw_arch);

    // First, the context switch configuration actions from the HEF are added in their order of
    // appearance (which is chronological).
    std::vector<ContextSwitchConfigActionPtr> configuration_actions;
    for (const auto &proto_action : operation.actions()) {
        auto configuration_action = ContextSwitchConfigAction::create(proto_action, device, config_resources,
            resources_manager, *network_group_proto, support_pre_fetch);
        CHECK_EXPECTED_AS_STATUS(configuration_action);
        configuration_actions.emplace_back(configuration_action.release());
    }

    // Next, we process the actions from the HEF, adding 'CreateConfigDescAndFetchAction's and 'RepeatedHeaderAction's.
    // The resulting vector contains the configuration actions to be executed in chronological order.
    const auto processed_configuration_actions = process_configuration_actions(configuration_actions, config_resources, 
        support_pre_fetch);
    CHECK_EXPECTED_AS_STATUS(processed_configuration_actions);

    // Finally, we execute the context switch configuration actions.
    for (const auto &configuration_action : processed_configuration_actions.value()) {
        const auto status = configuration_action->execute(&context_info, context_meta_data_head_pointer);
        CHECK_SUCCESS(status);
    }

    return HAILO_SUCCESS;
}

static hailo_status fill_context_recepies_for_multi_context(ProtoHEFNetworkGroupPtr network_group_proto, 
    const ProtoHEFHwArch &hw_arch, CONTROL_PROTOCOL__context_switch_context_info_t &context_info, 
    ResourcesManager &resources_manager, uint8_t context_index, const ProtoHEFContext &proto_context, 
    Device &device, ContextSwitchChannelsParsingInfo &channels_parsing_info)
{
    hailo_status status = HAILO_UNINITIALIZED;
    uint8_t *context_meta_data_head_pointer = context_info.context_network_data;

    // Add edge layers mapping
    status = parse_and_fill_edge_layers_mapping(network_group_proto, &context_info, &context_meta_data_head_pointer, 
        &proto_context.metadata(), channels_parsing_info, resources_manager, context_index);
    CHECK_SUCCESS(status);

    CHECK(IS_FIT_IN_UINT8(proto_context.operations_size()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid operations_count: {}.", proto_context.operations_size());

    context_info.context_stream_remap_data.should_use_stream_remap = static_cast<uint8_t>(proto_context.metadata().shmiglue_info().should_use_shmiglue());

    // Parse context
    for (const auto &operation : proto_context.operations()) {
        const auto operation_trigger = ContextSwitchTrigger::create(operation.trigger());
        CHECK_EXPECTED_AS_STATUS(operation_trigger);
        operation_trigger->add_to_trigger_group(&context_info, &context_meta_data_head_pointer);

        status = parse_actions_in_operation(operation, device, hw_arch, 
            resources_manager.dynamic_config(context_index), resources_manager, network_group_proto, context_info, 
            &context_meta_data_head_pointer);
        CHECK_SUCCESS(status);
    }

    // update context_network_data_length per context, and dynamic_contexts_descriptors count in main header
    context_info.context_network_data_length =
        static_cast<uint32_t>(context_meta_data_head_pointer - context_info.context_network_data);

    return HAILO_SUCCESS;
}

static hailo_status fill_preliminary_config_recepies_for_multi_context(const ProtoHEFHwArch &hw_arch,
    CONTROL_PROTOCOL__context_switch_context_info_t &context_info, ResourcesManager &resources_manager,
    ContextSwitchChannelsParsingInfo &channels_parsing_info, ProtoHEFNetworkGroupPtr network_group_proto,
    const ProtoHEFPreliminaryConfig &proto_preliminary_config, Device &device)
{
    uint8_t *context_meta_data_head_pointer = context_info.context_network_data;

    CHECK(IS_FIT_IN_UINT8(proto_preliminary_config.operation_size()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid operations_count: {}.", proto_preliminary_config.operation_size());

    if (resources_manager.get_supported_features().preliminary_run_asap) {
        // Add edge layers mapping (only preliminary_run_asap networks have edge layers in the preliminary context)
        static const auto PRELIMINARY_CONTEXT_INDEX = 0;
        auto status = parse_and_fill_edge_layers_mapping(network_group_proto, &context_info, &context_meta_data_head_pointer, 
            &(network_group_proto->contexts(PRELIMINARY_CONTEXT_INDEX).metadata()), channels_parsing_info, resources_manager,
            PRELIMINARY_CONTEXT_INDEX);
        CHECK_SUCCESS(status);
    }

    // Parse preliminary config
    for (const auto &operation_proto : proto_preliminary_config.operation()) {
        const auto operation_trigger = ContextSwitchTrigger::create(operation_proto.trigger());
        CHECK_EXPECTED_AS_STATUS(operation_trigger);
        operation_trigger->add_to_trigger_group(&context_info, &context_meta_data_head_pointer);

        const auto status = parse_actions_in_operation(operation_proto, device, hw_arch, 
            resources_manager.preliminary_config(), resources_manager, network_group_proto, context_info, 
            &context_meta_data_head_pointer);
        CHECK_SUCCESS(status);
    }

    // Update context_network_data_length per context, and preliminary_context_descriptors count in main header
    context_info.context_network_data_length =
        static_cast<uint32_t>(context_meta_data_head_pointer - context_info.context_network_data);

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<ResourcesManager>> Hef::Impl::create_resources_manager(
    ProtoHEFNetworkGroupPtr network_group_proto, uint8_t net_group_index,
    VdmaDevice &device, HailoRTDriver &driver, const ConfigureNetworkParams &config_params, 
    std::shared_ptr<NetworkGroupMetadata> network_group_metadata,
    const ProtoHEFHwArch &hw_arch)
{
    CHECK_ARG_NOT_NULL_AS_EXPECTED(network_group_proto);

    CHECK(network_group_proto->contexts_size() <= MAX_CONTEXTS_COUNT, make_unexpected(HAILO_INVALID_HEF),
        "App '{}' contains more contexts than allowed ({} > {})", network_group_proto->network_group_metadata().network_group_name(),
        network_group_proto->contexts_size(), MAX_CONTEXTS_COUNT);
    
    for (auto &network_params : config_params.network_params_by_name) {
        CHECK(HAILO_MAX_BATCH_SIZE >= network_params.second.batch_size, make_unexpected(HAILO_INVALID_ARGUMENT),
            "Given batch size ({}) for network group {}, network {} is bigger than max allowed ({})", network_params.second.batch_size,
            network_group_proto->network_group_metadata().network_group_name(), network_params.first, HAILO_MAX_BATCH_SIZE);
    }

    auto parsing_info = Hef::Impl::get_parsing_info(network_group_proto);
    CHECK_EXPECTED(parsing_info);

    auto resources_manager = ResourcesManager::create(device, driver, config_params, network_group_proto, network_group_metadata,
        parsing_info.release(), net_group_index);
    CHECK_EXPECTED(resources_manager);

    auto preliminary_context = resources_manager->add_new_context();
    CHECK_EXPECTED(preliminary_context);

    // TODO: Support sharing of ContextSwitchChannelsParsingInfo between fill_preliminary_config_recepies_for_multi_context
    //       and fill_context_recepies_for_multi_context (HRT-6683).
    //       When running in preliminary_run_asap mode the channels used by the first dynamic context and by the preliminary
    //       context are the same channels. We need to add the sane edge_layers for both contexts (look for calls to 
    //       hef_metadata__add_edge_layer_header) so that the needed actions will be added to the action list. However,
    //       due to the logic in ResourceManager::get_available_channel_index's blacklist channels that should be the same
    //       will get different indexes. Need to refactor the logic of that function and the entire channel index allocation logic.
    ContextSwitchChannelsParsingInfo channels_parsing_info_preliminary{};
    auto status = fill_preliminary_config_recepies_for_multi_context(hw_arch, preliminary_context.value().get(),
        resources_manager.value(), channels_parsing_info_preliminary, network_group_proto, network_group_proto->preliminary_config(),
        device);
    CHECK_SUCCESS_AS_EXPECTED(status);
    resources_manager->update_preliminary_config_buffer_info();

    ContextSwitchChannelsParsingInfo channels_parsing_info_dynamic{};
    for (uint8_t context_index = 0; context_index < network_group_proto->contexts_size(); ++context_index) {
        auto new_context = resources_manager->add_new_context();
        CHECK_EXPECTED(new_context);

        status = fill_context_recepies_for_multi_context(network_group_proto, hw_arch, new_context.value().get(), resources_manager.value(),
            context_index, network_group_proto->contexts(context_index), device, channels_parsing_info_dynamic);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }
    resources_manager->update_dynamic_contexts_buffer_info();

    status = resources_manager->create_internal_vdma_channels();
    CHECK_SUCCESS_AS_EXPECTED(status);

    auto resources_manager_ptr = make_shared_nothrow<ResourcesManager>(resources_manager.release());
    CHECK_NOT_NULL_AS_EXPECTED(resources_manager_ptr, HAILO_OUT_OF_HOST_MEMORY);

    return resources_manager_ptr;
}

Expected<std::vector<std::string>> Hef::Impl::get_sorted_output_names(const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    auto res = network_group_metadata->get_sorted_output_names();
    return res;
}

static Expected<WriteMemoryInfo> parse_ccw_buffer(const std::string &ccw_buffer)
{
    WriteMemoryInfo write_memory_info = {};
    CHECK_AS_EXPECTED(ccw_buffer.size() > CCW_DATA_OFFSET, HAILO_INVALID_HEF, "ccw buffer is too small");
    CcwHeader *header = (CcwHeader*)(ccw_buffer.data());

    uint32_t words_count = header->words_count + 1;
    auto data_length = words_count * CCW_BYTES_IN_WORD;
    write_memory_info.address = header->address;

    // Validation for ccw size
    size_t expected_ccw_data_length = (ccw_buffer.length() - CCW_DATA_OFFSET);
    if (0 != (words_count % 2)) {
        expected_ccw_data_length -= CCW_BYTES_IN_WORD;
    }
    CHECK_AS_EXPECTED(data_length == expected_ccw_data_length, HAILO_INVALID_HEF,
        "Invalid ccw buffer was parsed from HEF");

    auto data_buff = Buffer::create(reinterpret_cast<const uint8_t*>(ccw_buffer.data() + CCW_DATA_OFFSET), data_length);
    CHECK_EXPECTED(data_buff);
    write_memory_info.data = data_buff.release();

    return write_memory_info;
}

/* HcpConfigNetworkGroup funcs */

Expected<std::vector<WriteMemoryInfo>> Hef::Impl::create_single_context_network_group_config(const ProtoHEFPreliminaryConfig& proto_config)
{
    std::vector<WriteMemoryInfo> config_buffers;

    for (const auto &operation : proto_config.operation()) {
        switch (operation.trigger().trigger_case()) {
            case ProtoHEFTrigger::kTriggerNone: {
                break;
            }
            default: {
                LOGGER__ERROR("Triggers different from 'ProtoHEFTriggerNone' are not supported");
                return make_unexpected(HAILO_INTERNAL_FAILURE);
            }
        }

        for (const auto &action : operation.actions()) {
            switch (action.action_case()) {
                case ProtoHEFAction::kNone: {
                    break;
                }
                case ProtoHEFAction::kWriteData: {
                    WriteMemoryInfo write_memory_info = {};
                    write_memory_info.address = static_cast<uint32_t>(action.write_data().address());
                    auto data_buff = Buffer::create(
                        reinterpret_cast<const uint8_t*>(action.write_data().data().data()),
                        action.write_data().data().length());
                    CHECK_EXPECTED(data_buff);
                    write_memory_info.data = data_buff.release();
                    config_buffers.emplace_back(std::move(write_memory_info));
                    break;
                }
                case ProtoHEFAction::kWriteDataCcw: {
                    auto config_buffer = parse_ccw_buffer(action.write_data_ccw().data());
                    CHECK_EXPECTED(config_buffer);
                    config_buffers.emplace_back(config_buffer.release());
                    break;
                }
                case ProtoHEFAction::kDisableLcu: {
                    // We ignore this action. the lcu_disable will happen in the nn_core reset before configuring specific network_group
                    break;
                }
                case ProtoHEFAction::kEnableLcu: {
                    WriteMemoryInfo write_memory_info = {};
                    write_memory_info.address = action.enable_lcu().lcu_enable_address();
                    auto data_buff = Buffer::create(ENABLE_LCU_CONTROL_WORD, sizeof(ENABLE_LCU_CONTROL_WORD));
                    CHECK_EXPECTED(data_buff);
                    write_memory_info.data = data_buff.release();
                    config_buffers.emplace_back(std::move(write_memory_info));
                    break;
                }
                case ProtoHEFAction::kAllowInputDataflow: {
                case ProtoHEFAction::kWaitForModuleConfigDone:
                    // We ignore the 'wait_for_interrupt' actions. After writing the configurations we can be sure everything is configured and dont need to wait for interrupts
                    break;
                }
                case ProtoHEFAction::kWaitForSeqeuncer: {
                case ProtoHEFAction::kEnableSequencer:
                    LOGGER__ERROR("Parsing error. Sequencer related actions are not supported over Ethernet. "
                        "If you use the Ethernet interface, please disable the Sequencer in the Dataflow Compiler (SDK) and then re-create the HEF. "
                        "Disabling the Sequencer is done using the hef_param command in the model script (ALLS file). "
                        "See the Dataflow Compiler user guide for more information.");
                    return make_unexpected(HAILO_INVALID_HEF);
                }
                default: {
                    LOGGER__ERROR("Invalid action");
                    return make_unexpected(HAILO_INTERNAL_FAILURE);
                }
            }
        }
    }

    return config_buffers;
}

ProtoHEFHwArch Hef::Impl::get_device_arch()
{
    return m_header.hw_arch();
}

Expected<float64_t> Hef::Impl::get_bottleneck_fps(const std::string &net_group_name)
{
    auto net_group = get_net_group_by_name(net_group_name);
    CHECK_EXPECTED(net_group);
    return net_group.value()->network_group_metadata().bottleneck_fps();
}

bool Hef::Impl::contains_ddr_layers(const ProtoHEFNetworkGroup& net_group)
{
    for (auto &context : net_group.contexts()) {
        for (auto &layer : context.metadata().edge_layers()) {
            if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__DDR ==
                layer.context_switch_info().edge_connection_type()) {
                return true;
            }
        }
    }
    return false;
}

bool is_edge_under_mux(const LayerInfo &info, const std::string &edge_name)
{
    if (!info.is_mux) {
        return edge_name == info.name;
    }
    for (const auto &pred : info.predecessor) {
        if (info.is_mux) {
            if (is_edge_under_mux(pred, edge_name)) {
                return true;
            }
        } else {
            if (edge_name == pred.name) {
                return true;
            }
        }
    }
    return false;
}

Expected<std::vector<std::string>> Hef::Impl::get_stream_names_from_vstream_name(const std::string &vstream_name,
    const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    return network_group_metadata->get_stream_names_from_vstream_name(vstream_name);
}

void get_demuxes_names_impl(const LayerInfo &info, std::vector<std::string> &res)
{
    if (!info.is_mux) {
        res.push_back(info.name);
    } else {
        for (auto &pred : info.predecessor) {
            get_demuxes_names_impl(pred, res);
        }
    }
}

std::vector<std::string> get_demuxes_names(const LayerInfo &info)
{
    std::vector<std::string> res;
    get_demuxes_names_impl(info, res);
    return res;
}

Expected<std::vector<std::string>> Hef::Impl::get_vstream_names_from_stream_name(const std::string &stream_name,
    const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    return network_group_metadata->get_vstream_names_from_stream_name(stream_name);
}

Expected<CONTROL_PROTOCOL__TRIGGER_t> ContextSwitchTrigger::serialize(const ProtoHEFTrigger &proto_trigger)
{
    switch (proto_trigger.trigger_case()) {
        case ProtoHEFTrigger::kTriggerNone:
            return HEF_METADATA__build_none_trigger();
        case ProtoHEFTrigger::kTriggerAllDataWasReceived:
        {
            const auto stream_index = proto_trigger.trigger_all_data_was_received().shmifo_index();
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(stream_index), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid stream_index: {}.", stream_index);
            return HEF_METADATA__build_input_stream_trigger(static_cast<uint8_t>(stream_index));
        }
        case ProtoHEFTrigger::kTriggerAllDataWasSent:
        {
            const auto stream_index = proto_trigger.trigger_all_data_was_sent().shmifo_index();
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(stream_index), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid stream_index: {}.", stream_index);
            return HEF_METADATA__build_output_stream_trigger(static_cast<uint8_t>(stream_index));
        }
        case ProtoHEFTrigger::kTriggerLcu:
        {
            const auto cluster_index = proto_trigger.trigger_lcu().cluster_index();
            const auto lcu_index = proto_trigger.trigger_lcu().lcu_index();
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(cluster_index), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid cluster_index: {}.", cluster_index);
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(lcu_index), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid lcu_index: {}.", lcu_index);
            return HEF_METADATA__build_lcu_trigger(static_cast<uint8_t>(cluster_index),
                static_cast<uint8_t>(lcu_index));
        }
        case ProtoHEFTrigger::kTriggerNms:
        {
            const auto aggregator_index = proto_trigger.trigger_nms().aggregator_index();
            const auto pred_cluster_ob_index = proto_trigger.trigger_nms().pred_cluster_ob_index();
            const auto pred_cluster_ob_cluster_index = proto_trigger.trigger_nms().pred_cluster_ob_cluster_index();
            const auto pred_cluster_ob_interface = proto_trigger.trigger_nms().pred_cluster_ob_interface();
            const auto succ_prepost_ob_index = proto_trigger.trigger_nms().succ_prepost_ob_index();
            const auto succ_prepost_ob_interface = proto_trigger.trigger_nms().succ_prepost_ob_interface();
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(aggregator_index), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid aggregator_index: {}.", aggregator_index);
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(pred_cluster_ob_index), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid pred_cluster_ob_index: {}.", pred_cluster_ob_index);
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(pred_cluster_ob_cluster_index), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid pred_cluster_ob_cluster_index: {}.", pred_cluster_ob_cluster_index);
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(pred_cluster_ob_interface), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid pred_cluster_ob_interface: {}.", pred_cluster_ob_interface);
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(succ_prepost_ob_index), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid succ_prepost_ob_index: {}.", succ_prepost_ob_index);
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(succ_prepost_ob_interface), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid succ_prepost_ob_interface: {}.", succ_prepost_ob_interface);
            
            return HEF_METADATA__build_nms_trigger(static_cast<uint8_t>(aggregator_index),
                static_cast<uint8_t>(pred_cluster_ob_index), static_cast<uint8_t>(pred_cluster_ob_cluster_index),
                static_cast<uint8_t>(pred_cluster_ob_interface), static_cast<uint8_t>(succ_prepost_ob_index),
                static_cast<uint8_t>(succ_prepost_ob_interface));
        }
        case ProtoHEFTrigger::kTriggerDmaIdle:
        {
            const auto stream_index = proto_trigger.trigger_dma_idle().shmifo_index();
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(stream_index), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid stream_index: {}.", stream_index);
            return HEF_METADATA__build_dma_idle_trigger(static_cast<uint8_t>(stream_index));
        }
        default:
            LOGGER__ERROR("Invalid trigger");
            break;
    }

    // Deafult case
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

Expected<ContextSwitchTrigger> ContextSwitchTrigger::create(const ProtoHEFTrigger &proto_trigger)
{
    auto serialized_trigger = serialize(proto_trigger);
    CHECK_EXPECTED(serialized_trigger);
    return ContextSwitchTrigger(serialized_trigger.release());
}

ContextSwitchTrigger::ContextSwitchTrigger(CONTROL_PROTOCOL__TRIGGER_t &&serialized_trigger) :
    m_serialized_trigger(std::move(serialized_trigger))
{}

CONTROL_PROTOCOL__TRIGGER_t ContextSwitchTrigger::get_serialized() const
{
    return m_serialized_trigger;
}

hailo_status ContextSwitchTrigger::add_to_trigger_group(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer) const
{
    return HEF_METADATA__add_trigger_to_trigger_group(context_info, context_meta_data_head_pointer,
        &m_serialized_trigger);
}

Expected<ContextSwitchConfigActionPtr> ContextSwitchConfigAction::create(const ProtoHEFAction &proto_action, Device &device,
    std::vector<ConfigBuffer> &config, const ResourcesManager &resources_manager, const ProtoHEFNetworkGroup &net_group,
    bool support_pre_fetch)
{
    switch (proto_action.action_case()) {
        case ProtoHEFAction::kWriteDataCcw:
            return WriteDataCcwAction::create(proto_action, config, support_pre_fetch);

        case ProtoHEFAction::kWriteData:
            return WriteDataAction::create(proto_action, device);

        case ProtoHEFAction::kDisableLcu:
            return DisableLcuAction::create(proto_action);

        case ProtoHEFAction::kEnableLcu:
            return EnableLcuAction::create(proto_action, resources_manager, net_group);

        case ProtoHEFAction::kEnableSequencer:
            return EnableSequencerAction::create(proto_action);
        
        case ProtoHEFAction::kNone:
            return NoneAction::create();

        case ProtoHEFAction::kWaitForSeqeuncer:
            return WaitForSeqeuncerAction::create(proto_action);

        case ProtoHEFAction::kWriteCompressedData:
            LOGGER__ERROR("Action 'WriteCompressedData' is not supported");
            return make_unexpected(HAILO_NOT_IMPLEMENTED);

        case ProtoHEFAction::kAllowInputDataflow:
            return AllowInputDataflowAction::create(proto_action);

        case ProtoHEFAction::kWaitForModuleConfigDone:
            return WaitForModuleConfigDoneAction::create(proto_action);

        default:
            LOGGER__ERROR("Invalid action");
            break;
    }

    // Default case
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

ContextSwitchConfigAction::ContextSwitchConfigAction(Type type) :
    ContextSwitchConfigAction(type, CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_COUNT, ProtoHEFAction::default_instance())
{}

ContextSwitchConfigAction::ContextSwitchConfigAction(Type type, const ProtoHEFAction& proto_action) :
    ContextSwitchConfigAction(type, CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_COUNT, proto_action)
{}

ContextSwitchConfigAction::ContextSwitchConfigAction(Type type, CONTROL_PROTOCOL__ACTION_TYPE_t action_list_type) :
    ContextSwitchConfigAction(type, action_list_type, ProtoHEFAction::default_instance())
{}

ContextSwitchConfigAction::ContextSwitchConfigAction(Type type,
                                                     CONTROL_PROTOCOL__ACTION_TYPE_t action_list_type,
                                                     const ProtoHEFAction& proto_action) :
    m_type(type),
    m_action_list_type(action_list_type),
    m_is_in_repeated_block(false),
    m_proto_action(proto_action)
{}

ContextSwitchConfigAction::Type ContextSwitchConfigAction::get_type() const
{
    return m_type;
}

CONTROL_PROTOCOL__ACTION_TYPE_t ContextSwitchConfigAction::get_action_list_type() const
{
    return m_action_list_type;
}

const ProtoHEFAction &ContextSwitchConfigAction::get_proto_action() const
{
    return m_proto_action;
}

void ContextSwitchConfigAction::set_is_in_repeated_block(bool is_repeated)
{
    m_is_in_repeated_block = is_repeated;
}

Expected<ContextSwitchConfigActionPtr> NoneAction::create()
{
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) NoneAction());
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

NoneAction::NoneAction() :
    ContextSwitchConfigAction(Type::None)
{}

hailo_status NoneAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *, uint8_t **)
{
    // Do nothing
    return HAILO_SUCCESS;
}

bool NoneAction::supports_repeated_block() const
{
    // None actions are ignored and aren't written to the FW's action list. Hence they can't be part of a repeated block.
    return false;
}

Expected<ContextSwitchConfigActionPtr> WriteDataAction::create(const ProtoHEFAction& proto_action, Device &device)
{
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) WriteDataAction(proto_action, device));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

WriteDataAction::WriteDataAction(const ProtoHEFAction& proto_action, Device &device) :
    ContextSwitchConfigAction(Type::WriteData, proto_action),
    m_device(device)
{}

hailo_status WriteDataAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *, uint8_t **)
{
    return m_device.write_memory(static_cast<uint32_t>(m_proto_action.write_data().address()),
        MemoryView::create_const(m_proto_action.write_data().data().data(), m_proto_action.write_data().data().length()));
}

bool WriteDataAction::supports_repeated_block() const
{
    // WriteDataActions aren't written to the FW's action list. Hence they can't be part of a repeated block.
    return false;
}

Expected<ContextSwitchConfigActionPtr> WriteDataCcwAction::create(const ProtoHEFAction& proto_action,
    std::vector<ConfigBuffer> &config, bool support_pre_fetch)
{
    // Add buffer to cfg_ch buffer without making descriptors (saving offset as the total data so far)
    CHECK_AS_EXPECTED(proto_action.write_data_ccw().cfg_channel_index() < config.size(), HAILO_INVALID_HEF,
        "cfg_channel index {} is bigger than config channels count {}",
        proto_action.write_data_ccw().cfg_channel_index(), config.size());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.write_data_ccw().cfg_channel_index()), HAILO_INVALID_HEF,
        "Invalid cfg channel index");
    
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) WriteDataCcwAction(proto_action,
        config[proto_action.write_data_ccw().cfg_channel_index()], support_pre_fetch));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

WriteDataCcwAction::WriteDataCcwAction(const ProtoHEFAction& proto_action, ConfigBuffer &config,
    bool support_pre_fetch) :
    ContextSwitchConfigAction(Type::WriteDataCcw, proto_action),
    m_config(config),
    m_support_pre_fetch(support_pre_fetch)
{}

bool WriteDataCcwAction::is_last_ccw_write()
{
    auto write_size = m_proto_action.write_data_ccw().data().length();
    /* If last operation in context - Add nops to fill the buffer */
    if ((write_size + m_config.get_current_buffer_size()) != m_config.get_total_cfg_size()) {
        return false;
    }

    return true;
}

hailo_status WriteDataCcwAction::pad_with_nops()
{
    auto page_size = m_config.desc_page_size();
    auto buffer_size = m_config.get_total_cfg_size();
    auto buffer_residue = buffer_size % page_size;
    if (0 != buffer_residue % CCW_HEADER_SIZE) {
        LOGGER__ERROR("CFG channel buffer size must be a multiple of CCW header size ({})", CCW_HEADER_SIZE);
        return HAILO_INTERNAL_FAILURE;
    }
    /* If buffer does not fit info descriptor, the host must pad the buffer with CCW NOPs. */
    auto nop_count = (buffer_residue == 0) ? 0 : ((page_size - buffer_residue) / CCW_HEADER_SIZE);
    for (uint8_t nop_index = 0; nop_index < nop_count; nop_index++) {
        /* Generate nop tranaction.
           CCW of all zeros (64’h0) should be treated as NOP – ignore CCW and expect CCW in next 64b word. 
           When CSM recognize it is a NOP it pops it from the channel FIFO without forward any address/data/command, 
           does not contribute to CRC calculations but return credits to the peripheral as usual. */
        m_config.write(reinterpret_cast<const void *>(&CCW_NOP), sizeof(CCW_NOP));
    }

    return HAILO_SUCCESS;

}

hailo_status WriteDataCcwAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *, uint8_t **)
{
    const bool is_last_write = is_last_ccw_write();
    if (m_support_pre_fetch && is_last_write) {
        auto status = pad_with_nops();
        CHECK_SUCCESS(status);
    }

    auto status = m_config.write(m_proto_action.write_data_ccw().data().data(),
        m_proto_action.write_data_ccw().data().length());
    CHECK_SUCCESS(status);

    if (m_support_pre_fetch && is_last_write) {
        auto desc_count = m_config.program_descriptors();
        CHECK_EXPECTED_AS_STATUS(desc_count);
    }

    return HAILO_SUCCESS;
}

bool WriteDataCcwAction::supports_repeated_block() const
{
    // WriteDataCcwActions aren't written to the FW's action list. Hence they can't be part of a repeated block.
    return false;
}

Expected<ContextSwitchConfigActionPtr> AddCcwBurstAction::create(uint8_t channel_index, uint16_t ccw_bursts)
{
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) AddCcwBurstAction(channel_index, ccw_bursts));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

AddCcwBurstAction::AddCcwBurstAction(uint8_t channel_index, uint16_t ccw_bursts) :
    ContextSwitchConfigAction(Type::AddCcwBurst),
    m_channel_index(channel_index),
    m_ccw_bursts(ccw_bursts)
{}

hailo_status AddCcwBurstAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    return HEF_METADATA__add_ccw_bursts_action(context_info, context_meta_data_head_pointer,
        m_ccw_bursts, m_channel_index, m_is_in_repeated_block);
}

bool AddCcwBurstAction::supports_repeated_block() const
{
    return false;
}

Expected<ContextSwitchConfigActionPtr> CreateConfigDescAndFetchAction::create(uint8_t channel_index, ConfigBuffer &config_buffer)
{
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) CreateConfigDescAndFetchAction(channel_index, config_buffer));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

CreateConfigDescAndFetchAction::CreateConfigDescAndFetchAction(uint8_t channel_index, ConfigBuffer &config_buffer) :
    ContextSwitchConfigAction(Type::CreateDescForCcw),
    m_channel_index(channel_index),
    m_config_buffer(config_buffer)
{}

hailo_status CreateConfigDescAndFetchAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    const auto desc_count = m_config_buffer.program_descriptors();
    CHECK_EXPECTED_AS_STATUS(desc_count);

    CHECK(IS_FIT_IN_UINT16(desc_count.value()), HAILO_INVALID_OPERATION,
        "On cfg with continuous mode, max descriptors size must fit in uint16_t");
    return HEF_METADATA__add_read_vdma_action(context_info, context_meta_data_head_pointer,
        static_cast<uint16_t>(desc_count.value()), m_channel_index, m_is_in_repeated_block);
}

bool CreateConfigDescAndFetchAction::supports_repeated_block() const
{
    // TODO: Each CreateConfigDescAndFetchAction may contain multiple HEF_METADATA__add_read_vdma_action.
    //       They could be part of a repated block, but the curent logic in the hef module assumes that
    //       only one context switch action is written to the fw per ContextSwitchConfigAction instance.
    //       Hence this isn't supported.
    return false;
}

Expected<ContextSwitchConfigActionPtr> StartBurstCreditsTaskAction::create()
{
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) StartBurstCreditsTaskAction());
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

StartBurstCreditsTaskAction::StartBurstCreditsTaskAction() :
    ContextSwitchConfigAction(Type::StartBurstCreditsTask)
{}

hailo_status StartBurstCreditsTaskAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    return HEF_METADATA__burst_credits_task_start(context_info, context_meta_data_head_pointer,
        m_is_in_repeated_block);
}

bool StartBurstCreditsTaskAction::supports_repeated_block() const
{
    // We don't support repeated blocks for this action, since only one is added per group of consecutive
    // TriggerNewDataFromDataInput actions.
    return false;
}

Expected<ContextSwitchConfigActionPtr> RepeatedHeaderAction::create(
    CONTROL_PROTOCOL__ACTION_TYPE_t sub_action_type, uint8_t num_actions)
{
    CHECK_AS_EXPECTED(sub_action_type != CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ADD_REPEATED, HAILO_INVALID_HEF,
        "Invalid repeated sub-action type (can't have sub-action with type CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ADD_REPEATED)");
    CHECK_AS_EXPECTED(sub_action_type != CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_COUNT, HAILO_INVALID_HEF,
        "Invalid repeated sub-action type (can't have sub-action with type CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_COUNT)");
    CHECK_AS_EXPECTED(num_actions != 0, HAILO_INVALID_HEF, "Invalid sub-action count (must be greater than zero)");
    
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) RepeatedHeaderAction(
        sub_action_type, num_actions));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

RepeatedHeaderAction::RepeatedHeaderAction(CONTROL_PROTOCOL__ACTION_TYPE_t sub_action_type,
                                           uint8_t num_actions) :
    ContextSwitchConfigAction(Type::AddRrepeated, CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ADD_REPEATED),
    m_sub_action_type(sub_action_type),
    m_num_actions(num_actions)
{}

hailo_status RepeatedHeaderAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    return HEF_METADATA__add_repeated_header_action(context_info, context_meta_data_head_pointer,
        m_sub_action_type, m_num_actions);
}

bool RepeatedHeaderAction::supports_repeated_block() const
{
    // RepeatedHeaderActions can't be part of a repated block themselves
    return false;
}

Expected<ContextSwitchConfigActionPtr> DisableLcuAction::create(const ProtoHEFAction& proto_action)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.disable_lcu().cluster_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid cluster_index: {}.", proto_action.disable_lcu().cluster_index());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.disable_lcu().lcu_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid lcu_index: {}", proto_action.disable_lcu().lcu_index());
    
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) DisableLcuAction(proto_action));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

DisableLcuAction::DisableLcuAction(const ProtoHEFAction& proto_action) :
    ContextSwitchConfigAction(Type::DisableLcu, CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_DISABLE_LCU, proto_action)
{}

hailo_status DisableLcuAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    return HEF_METADATA__add_disable_lcu_action(context_info, context_meta_data_head_pointer,
        static_cast<uint8_t>(m_proto_action.disable_lcu().cluster_index()),
        static_cast<uint8_t>(m_proto_action.disable_lcu().lcu_index()), m_is_in_repeated_block);
}

bool DisableLcuAction::supports_repeated_block() const
{
    return true;
}

Expected<ContextSwitchConfigActionPtr> EnableLcuAction::create(const ProtoHEFAction& proto_action,
    const ResourcesManager &resources_manager, const ProtoHEFNetworkGroup &net_group)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.enable_lcu().cluster_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid cluster_index: {}.", proto_action.enable_lcu().cluster_index());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.enable_lcu().lcu_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid lcu_index: {}.", proto_action.enable_lcu().lcu_index());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(proto_action.enable_lcu().lcu_kernel_done_address()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid lcu_kernel_done_address: {}.", proto_action.enable_lcu().lcu_kernel_done_address());

    auto support_multi_networks = resources_manager.get_supported_features().multi_network_support;
    auto network_index = static_cast<uint8_t>((support_multi_networks) ? proto_action.enable_lcu().network_index() : 0);
    auto partial_network_name = HefUtils::get_partial_network_name_by_index(net_group, network_index,
        resources_manager.get_supported_features());
    CHECK_EXPECTED(partial_network_name);

    auto network_name = HefUtils::get_network_name(net_group, partial_network_name.value());

    auto batch_size = resources_manager.get_network_batch_size(network_name);
    CHECK_EXPECTED(batch_size);
    const auto kernel_done_address = static_cast<uint16_t>(proto_action.enable_lcu().lcu_kernel_done_address());
    const auto kernel_done_count = static_cast<uint32_t>(proto_action.enable_lcu().lcu_kernel_done_count());
    const auto is_default = (CONTEXT_SWITCH_DEFS__ENABLE_LCU_DEFAULT_KERNEL_ADDRESS == kernel_done_address) &&
        (CONTEXT_SWITCH_DEFS__ENABLE_LCU_DEFAULT_KERNEL_COUNT == kernel_done_count);
    
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) EnableLcuAction(proto_action, is_default, network_index));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

CONTROL_PROTOCOL__ACTION_TYPE_t EnableLcuAction::get_enable_lcu_action_type(bool is_default)
{
    return is_default ? static_cast<CONTROL_PROTOCOL__ACTION_TYPE_t>(CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ENABLE_LCU_DEFAULT) :
        static_cast<CONTROL_PROTOCOL__ACTION_TYPE_t>(CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_ENABLE_LCU_NON_DEFAULT);
}

ContextSwitchConfigAction::Type EnableLcuAction::get_enable_lcu_type(bool is_default)
{
    return is_default ? Type::EnableLcuDefault : Type::EnableLcuNonDefault;
}

EnableLcuAction::EnableLcuAction(const ProtoHEFAction& proto_action, bool is_default, uint8_t network_index) :
    ContextSwitchConfigAction(get_enable_lcu_type(is_default), get_enable_lcu_action_type(is_default), proto_action),
    m_network_index(network_index),
    m_is_default(is_default)
{}

hailo_status EnableLcuAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    const auto cluster_index = static_cast<uint8_t>(m_proto_action.enable_lcu().cluster_index());
    const auto lcu_index = static_cast<uint8_t>(m_proto_action.enable_lcu().lcu_index());
    if (m_is_default) {
        return HEF_METADATA__add_enable_lcu_default_action(context_info, context_meta_data_head_pointer,
            cluster_index, lcu_index, m_network_index, m_is_in_repeated_block);
    } else {
        const auto kernel_done_address = static_cast<uint16_t>(m_proto_action.enable_lcu().lcu_kernel_done_address());
        const auto kernel_done_count = static_cast<uint32_t>(m_proto_action.enable_lcu().lcu_kernel_done_count());
        return HEF_METADATA__add_enable_lcu_non_default_action(context_info, context_meta_data_head_pointer,
            cluster_index, lcu_index, kernel_done_address, kernel_done_count, m_network_index, m_is_in_repeated_block);
    }
}

bool EnableLcuAction::supports_repeated_block() const
{
    return true;
}

Expected<ContextSwitchConfigActionPtr> EnableSequencerAction::create(const ProtoHEFAction& proto_action)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.enable_sequencer().cluster_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid cluster_index: {}.", proto_action.enable_sequencer().cluster_index());

    // TODO: Remove when impolemeted in the hef.proto
    uint64_t l2_offset_0 = 0;
    uint64_t l2_offset_1 = 0;
    // TODO: Change the CONTEXT_SWITCH__add_enable_sequencer_proto_action func to receive 4 'l2_offset' params
    l2_offset_0 |= (uint64_t)(proto_action.enable_sequencer().l2_write_0());
    l2_offset_0 |= ((uint64_t)(proto_action.enable_sequencer().l2_write_1()) << 32);
    l2_offset_1 |= (uint64_t)(proto_action.enable_sequencer().l2_write_2());
    l2_offset_1 |= ((uint64_t)(proto_action.enable_sequencer().l2_write_3()) << 32);

    uint8_t initial_l3_cut = 0;
    uint16_t initial_l3_offset = 0;
    if (proto_action.enable_sequencer().initial_l3_info().includes_initial_l3_info()) {
        const auto &initial_l3_info = proto_action.enable_sequencer().initial_l3_info();
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(initial_l3_info.initial_l3_index()), HAILO_INVALID_HEF,
            "Initial l3 cut {} is out of range", initial_l3_info.initial_l3_index());
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(initial_l3_info.initial_l3_offset()), HAILO_INVALID_HEF,
            "Initial l3 offset {} is out of range", initial_l3_info.initial_l3_offset());
        initial_l3_cut = static_cast<uint8_t>(initial_l3_info.initial_l3_index());
        initial_l3_offset = static_cast<uint16_t>(initial_l3_info.initial_l3_offset());
    }
    else {
        // Legacy mode should work only on hailo8
        std::tie(initial_l3_cut, initial_l3_offset) = old_hef_parse_initial_l3(proto_action.enable_sequencer().initial_l3_legacy());
    }

    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) EnableSequencerAction(
        proto_action,
        initial_l3_cut,
        initial_l3_offset,
        l2_offset_0,
        l2_offset_1
    ));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

EnableSequencerAction::EnableSequencerAction(const ProtoHEFAction& proto_action, uint8_t initial_l3_cut, uint16_t initial_l3_offset,
                                             uint64_t l2_offset_0, uint64_t l2_offset_1) :
    ContextSwitchConfigAction(Type::TriggerSequencer, CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_TRIGGER_SEQUENCER, proto_action),
    m_initial_l3_cut(initial_l3_cut),
    m_initial_l3_offset(initial_l3_offset),
    m_l2_offset_0(l2_offset_0),
    m_l2_offset_1(l2_offset_1)
{}

hailo_status EnableSequencerAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    const auto cluster_index = static_cast<uint8_t>(m_proto_action.enable_sequencer().cluster_index());
    const auto active_apu_bitmap = static_cast<uint32_t>(m_proto_action.enable_sequencer().active_apu_bitmap());
    const auto active_ia_bitmap = static_cast<uint32_t>(m_proto_action.enable_sequencer().active_ia_bitmap());
    const auto active_sc_bitmap = static_cast<uint64_t>(m_proto_action.enable_sequencer().active_sc_bitmap());
    const auto active_l2_bitmap = static_cast<uint64_t>(m_proto_action.enable_sequencer().active_l2_bitmap());
    return HEF_METADATA__add_enable_sequencer_action(context_info, context_meta_data_head_pointer, 
        cluster_index, m_initial_l3_cut, m_initial_l3_offset, active_apu_bitmap, active_ia_bitmap,
        active_sc_bitmap, active_l2_bitmap, m_l2_offset_0, m_l2_offset_1, m_is_in_repeated_block);
}

bool EnableSequencerAction::supports_repeated_block() const
{
    return true;
}

Expected<ContextSwitchConfigActionPtr> WaitForSeqeuncerAction::create(const ProtoHEFAction& proto_action)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.wait_for_seqeuncer().cluster_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid cluster_index: {}.", proto_action.wait_for_seqeuncer().cluster_index());
    
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) WaitForSeqeuncerAction(proto_action));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

WaitForSeqeuncerAction::WaitForSeqeuncerAction(const ProtoHEFAction& proto_action) :
    ContextSwitchConfigAction(Type::WaitForSequencerDone, CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_WAIT_FOR_SEQUENCER_DONE, proto_action)
{}

hailo_status WaitForSeqeuncerAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    return HEF_METADATA__add_wait_for_sequencer_action(context_info, context_meta_data_head_pointer,
        static_cast<uint8_t>(m_proto_action.wait_for_seqeuncer().cluster_index()), m_is_in_repeated_block);
}

bool WaitForSeqeuncerAction::supports_repeated_block() const
{
    // Wait actions shouldn't be repeated (for easier debugging)
    return false;
}

Expected<ContextSwitchConfigActionPtr> AllowInputDataflowAction::create(const ProtoHEFAction& proto_action)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.allow_input_dataflow().sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", proto_action.allow_input_dataflow().sys_index());
    
    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) AllowInputDataflowAction(proto_action));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

ContextSwitchConfigAction::Type AllowInputDataflowAction::get_input_dataflow_action_type(const ProtoHEFAction& proto_action)
{
    if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__DDR == proto_action.allow_input_dataflow().connection_type()) {
        return Type::TriggerNewDataFromDataInputDdr;
    }
    return Type::TriggerNewDataFromDataInput;
}

AllowInputDataflowAction::AllowInputDataflowAction(const ProtoHEFAction& proto_action) :
    ContextSwitchConfigAction(get_input_dataflow_action_type(proto_action),
                              CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_TRIGGER_NEW_DATA_FROM_DATA_INPUT, proto_action)
{}

hailo_status AllowInputDataflowAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    // DDR threads are implemented on HailoRT so no FW action is required
    if (Type::TriggerNewDataFromDataInputDdr == m_type) {
        return HAILO_SUCCESS;
    }

    return HEF_METADATA__add_fetch_new_data_action(context_info, context_meta_data_head_pointer,
        static_cast<uint8_t>(m_proto_action.allow_input_dataflow().sys_index()), m_is_in_repeated_block);
}

bool AllowInputDataflowAction::supports_repeated_block() const
{
    // DDR threads are implemented on HailoRT so no FW action is required. Hence they can't be part of a repeated block.
    if (Type::TriggerNewDataFromDataInputDdr == m_type) {
        return false;
    }

    return true;
}

Expected<ContextSwitchConfigActionPtr> WaitForModuleConfigDoneAction::create(const ProtoHEFAction& proto_action)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.wait_for_module_config_done().index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid index: {}", proto_action.wait_for_module_config_done().index());

    auto result = ContextSwitchConfigActionPtr(new (std::nothrow) WaitForModuleConfigDoneAction(proto_action));
    CHECK_AS_EXPECTED((nullptr != result), HAILO_OUT_OF_HOST_MEMORY);
    return result;
}

WaitForModuleConfigDoneAction::WaitForModuleConfigDoneAction(const ProtoHEFAction& proto_action) :
    ContextSwitchConfigAction(Type::WaitForModuleConfigDone, CONTROL_PROTOCOL__CONTEXT_SWITCH_ACTION_WAIT_FOR_MODULE_CONFIG_DONE, proto_action)
{}

hailo_status WaitForModuleConfigDoneAction::execute(CONTROL_PROTOCOL__context_switch_context_info_t *context_info,
    uint8_t **context_meta_data_head_pointer)
{
    return HEF_METADATA__add_wait_for_module_config_done_action(context_info, context_meta_data_head_pointer,
        static_cast<uint8_t>(m_proto_action.wait_for_module_config_done().index()), m_is_in_repeated_block);
}

bool WaitForModuleConfigDoneAction::supports_repeated_block() const
{
    // Wait actions shouldn't be repeated (for easier debugging)
    return false;
}

Expected<std::string> Hef::Impl::get_vstream_name_from_original_name_mux(const std::string &original_name, const ProtoHefEdge &layer)
{
    switch (layer.edge_case()) {
        case ProtoHefEdge::kLayerInfo:
            for (const auto &name : layer.layer_info().original_names()) {
                if (original_name == name) {
                    return std::string(layer.layer_info().name());
                }
            }
            return make_unexpected(HAILO_NOT_FOUND);
        case ProtoHefEdge::kLayerMux:
            for (const auto &pred : layer.layer_mux().predecessors()) {
                auto res = get_vstream_name_from_original_name_mux(original_name, pred);
                if (res) {
                    return std::move(res.value());
                }
            }
            return make_unexpected(HAILO_NOT_FOUND);
        default:
            LOGGER__ERROR("Invalid layer type");
            return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

Expected<std::string> Hef::Impl::get_vstream_name_from_original_name(const std::string &original_name,
    const std::string &net_group_name)
{
    auto net_group = get_net_group_by_name(net_group_name);
    CHECK_EXPECTED(net_group);

    std::string results;

    for (const auto &context : net_group.value()->contexts()) {
        for (const auto &layer_info : context.metadata().edge_layers()) {
            if ((is_h2d_boundary_info_layer(layer_info)) || (is_d2h_boundary_info_layer(layer_info))) {
                for (auto &name : layer_info.layer_info().original_names()) {
                    if (original_name == name) {
                        CHECK_AS_EXPECTED(results.empty(), HAILO_INVALID_HEF, "Original name {} appears more than once in the HEF.", original_name);
                        results = std::string(layer_info.layer_info().name());
                    }
                }
            } else if(is_d2h_boundary_mux_layer(layer_info)) {
                for (auto &pred : layer_info.layer_mux().predecessors()) {
                    auto stream_name = get_vstream_name_from_original_name_mux(original_name, pred);
                    if (stream_name) {
                        CHECK_AS_EXPECTED(results.empty(), HAILO_INVALID_HEF, "Original name {} appears more than once in the HEF.", original_name);
                        results = stream_name.value();
                    }
                }
            }
        }
    }
    CHECK_AS_EXPECTED(!results.empty(), HAILO_NOT_FOUND);
    return results;
}

Expected<std::vector<std::string>> Hef::Impl::get_original_names_from_vstream_name_mux(const std::string &vstream_name, const ProtoHefEdge &layer)
{
    switch (layer.edge_case()) {
    case ProtoHefEdge::kLayerInfo:
    {
        if (vstream_name == layer.layer_info().name()) {
            std::vector<std::string> results;
            for (const auto &name : layer.layer_info().original_names()) {
                results.push_back(name);
            }
            return results;
        }
        return make_unexpected(HAILO_NOT_FOUND);
    }
    case ProtoHefEdge::kLayerMux:
        for (const auto &pred : layer.layer_mux().predecessors()) {
            auto res = get_original_names_from_vstream_name_mux(vstream_name, pred);
            if (res) {
                return std::move(res.value());
            }
        }
        return make_unexpected(HAILO_NOT_FOUND);
    default:
        LOGGER__ERROR("Invalid layer type");
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

Expected<std::vector<std::string>> Hef::Impl::get_original_names_from_vstream_name(const std::string &vstream_name,
    const std::string &net_group_name)
{
    auto net_group = get_net_group_by_name(net_group_name);
    CHECK_EXPECTED(net_group);

    std::vector<std::string> results;

    for (const auto &context : net_group.value()->contexts()) {
        for (const auto &layer_info : context.metadata().edge_layers()) {
            if ((is_h2d_boundary_info_layer(layer_info)) || (is_d2h_boundary_info_layer(layer_info))) {
                if (vstream_name == layer_info.layer_info().name()) {
                    for (const auto &name : layer_info.layer_info().original_names()) {
                        results.push_back(name);
                    }
                    return results;
                }
            } else if(is_d2h_boundary_mux_layer(layer_info)) {
                for (const auto &pred : layer_info.layer_mux().predecessors()) {
                    auto names = get_original_names_from_vstream_name_mux(vstream_name, pred);
                    if (names) {
                        return std::move(names.value());
                    }
                }
            }
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

hailo_status Hef::Impl::validate_net_group_unique_layer_names(ProtoHEFNetworkGroupPtr net_group)
{
    CHECK_ARG_NOT_NULL(net_group);

    std::set<std::string> edge_layer_names;
    std::string layer_name;
    for (auto &context : net_group->contexts()) {
        for (auto &layer : context.metadata().edge_layers()) {
            // TODO: remove check for boundary layer after fix will be pushed in SDK
            if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY ==
                layer.context_switch_info().edge_connection_type()) {
                if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type()) {
                    layer_name = layer.layer_info().name();
                } else if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__MUX == layer.edge_layer_type()) {
                    layer_name = layer.layer_mux().name();
                } else {
                    LOGGER__ERROR("Invalid layer type.");
                    return HAILO_INVALID_HEF;
                }
                CHECK(!contains(edge_layer_names, layer_name), HAILO_INVALID_HEF,
                    "layer_name should be unique. {} appears more than once in the given network_group.",
                    layer_name);
                edge_layer_names.insert(layer_name);
            }
        }
    }
    return HAILO_SUCCESS;
}

hailo_status update_parsing_info(uint8_t cfg_index, uint32_t data_length, ConfigBufferInfoMap &results)
{
    CHECK(cfg_index < CONTROL_PROTOCOL__MAX_CFG_CHANNELS, HAILO_INVALID_HEF, "Invalid cfg_index");

    if (contains(results, cfg_index)) {
        results.at(cfg_index).push_back(data_length);
        return HAILO_SUCCESS;
    }

    // If we got here, the current cfg_index's info is parsed for the first time
    results.emplace(cfg_index, std::vector<uint32_t>(1, data_length));
    return HAILO_SUCCESS;
}

Expected<ConfigBufferInfoMap> get_config_buffer_info(
    const google::protobuf::RepeatedPtrField<ProtoHEFOperation> &operations)
{
    auto status = HAILO_UNINITIALIZED;
    ConfigBufferInfoMap results;

    for (const auto &operation : operations) {
        for (const auto &action : operation.actions()) {
            if (ProtoHEFAction::kWriteDataCcw == action.action_case()) {
                CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(action.write_data_ccw().cfg_channel_index()), HAILO_INVALID_HEF,
                    "Invalid cfg index {}", action.write_data_ccw().cfg_channel_index());
                status = update_parsing_info(static_cast<uint8_t>(action.write_data_ccw().cfg_channel_index()),
                    static_cast<uint32_t>(action.write_data_ccw().data().length()), results);
                CHECK_SUCCESS_AS_EXPECTED(status);
            }
        }
    }
    return results;
}

Expected<HefParsingInfo> Hef::Impl::get_parsing_info(ProtoHEFNetworkGroupPtr net_group)
{
    assert(nullptr != net_group);

    // Parse preliminary config
    auto preliminary_config_buffer_infos = get_config_buffer_info(net_group->preliminary_config().operation());
    CHECK_EXPECTED(preliminary_config_buffer_infos);

    HefParsingInfo parsing_info;
    parsing_info.cfg_infos_preliminary_config = preliminary_config_buffer_infos.release();

    // Parse dynamic contexts
    for (const auto &context : net_group->contexts()) {
        auto dynamic_ctxt_config_buffer_infos = get_config_buffer_info(context.operations());
        CHECK_EXPECTED(dynamic_ctxt_config_buffer_infos);

        parsing_info.cfg_infos_per_context.emplace_back(dynamic_ctxt_config_buffer_infos.release());
    }

    return parsing_info;
}

std::vector<std::string> Hef::get_network_groups_names()
{
    return pimpl->get_network_groups_names();
}

Expected<NetworkGroupsParamsMap> Hef::create_configure_params(hailo_stream_interface_t stream_interface)
{
    NetworkGroupsParamsMap results;
    for (const auto &name : pimpl->get_network_groups_names()) {
        auto params = create_configure_params(stream_interface, name);
        CHECK_EXPECTED(params);
        results.emplace(std::make_pair(name, params.release()));
    }
    return results;
}

Expected<ConfigureNetworkParams> Hef::create_configure_params(hailo_stream_interface_t stream_interface, const std::string &network_group_name)
{
    return pimpl->create_configure_params(stream_interface, network_group_name);
}

Expected<NetworkGroupsParamsMap> Hef::create_configure_params_mipi_input(hailo_stream_interface_t output_interface,
    const hailo_mipi_input_stream_params_t &mipi_params)
{
    NetworkGroupsParamsMap results;
    for (const auto &name : pimpl->get_network_groups_names()) {
        auto params = create_configure_params_mipi_input(output_interface, mipi_params, name);
        CHECK_EXPECTED(params);
        results.emplace(std::make_pair(name, params.release()));
    }
    return results;
}


Expected<ConfigureNetworkParams> Hef::create_configure_params_mipi_input(hailo_stream_interface_t output_interface,
    const hailo_mipi_input_stream_params_t &mipi_params, const std::string &network_group_name)
{
    return pimpl->create_configure_params_mipi_input(output_interface, mipi_params, network_group_name);
}

std::vector<std::string> Hef::Impl::get_network_groups_names()
{
    std::vector<std::string> results;
    results.reserve(m_groups.size());

    for (const auto &net_group : m_groups) {
        results.push_back(net_group->network_group_metadata().network_group_name());
    }
    return results;
}

Expected<std::vector<hailo_network_group_info_t>> Hef::get_network_groups_infos()
{
    return pimpl->get_network_groups_infos();
}

Expected<std::vector<hailo_network_group_info_t>> Hef::Impl::get_network_groups_infos()
{
    std::vector<hailo_network_group_info_t> results;
    results.reserve(m_groups.size());

    for (const auto &net_group : m_groups) {
        hailo_network_group_info_t info = {};
        auto &network_group_name = net_group->network_group_metadata().network_group_name();
        CHECK_AS_EXPECTED(HAILO_MAX_NETWORK_GROUP_NAME_SIZE >= (network_group_name.length() + 1), HAILO_INTERNAL_FAILURE,
            "The network group '{}' has a too long name (max is HAILO_MAX_NETWORK_GROUP_NAME_SIZE)", network_group_name);
        strncpy(info.name, network_group_name.c_str(), network_group_name.length() + 1);
        info.is_multi_context = (1 < net_group->contexts_size());
        results.push_back(info);
    }
    return results;
}

Expected<std::map<std::string, hailo_vstream_params_t>> Hef::make_input_vstream_params(
    const std::string &name, bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms,
    uint32_t queue_size)
{
    auto network_pair = pimpl->get_network_group_and_network_name(name);
    CHECK_EXPECTED(network_pair);
    
    return pimpl->make_input_vstream_params(network_pair.value().first, network_pair.value().second, quantized, format_type, 
        timeout_ms, queue_size);
}

Expected<std::map<std::string, hailo_vstream_params_t>> Hef::Impl::make_input_vstream_params(
    const std::string &net_group_name, const std::string &network_name, bool quantized, 
    hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    std::map<std::string, hailo_vstream_params_t> input_vstreams_params;
    auto status = fill_missing_input_vstream_params_with_default(net_group_name,
        network_name, input_vstreams_params, quantized, format_type, timeout_ms, queue_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return input_vstreams_params;
}

Expected<std::map<std::string, hailo_vstream_params_t>> Hef::make_output_vstream_params(
    const std::string &name, bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms,
    uint32_t queue_size)
{
    auto network_pair = pimpl->get_network_group_and_network_name(name);
    CHECK_EXPECTED(network_pair);

    return pimpl->make_output_vstream_params(network_pair.value().first, network_pair.value().second, quantized, format_type, 
        timeout_ms, queue_size);
}

Expected<std::map<std::string, hailo_vstream_params_t>> Hef::Impl::make_output_vstream_params(
    const std::string &net_group_name, const std::string &network_name, bool quantized, 
    hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    std::map<std::string, hailo_vstream_params_t> output_vstreams_params;
    auto status = fill_missing_output_vstream_params_with_default(net_group_name,
        network_name, output_vstreams_params, quantized, format_type, timeout_ms, queue_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return output_vstreams_params;
}

hailo_status Hef::Impl::fill_missing_input_vstream_params_with_default(const std::string &net_group_name,
    const std::string &network_name, std::map<std::string, hailo_vstream_params_t> &input_vstreams_params,
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED_AS_STATUS(network_group_metadata);
    auto input_vstream_infos = network_group_metadata->get_input_vstream_infos(network_name);
    CHECK_EXPECTED_AS_STATUS(input_vstream_infos);

    return fill_missing_vstream_params_with_default(input_vstreams_params, input_vstream_infos.value(),
        quantized, format_type, timeout_ms, queue_size);
}

hailo_status Hef::Impl::fill_missing_output_vstream_params_with_default(const std::string &net_group_name,
    const std::string &network_name, std::map<std::string, hailo_vstream_params_t> &output_vstream_params,
    bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED_AS_STATUS(network_group_metadata);
    auto output_vstream_infos =  network_group_metadata->get_output_vstream_infos(network_name);
    CHECK_EXPECTED_AS_STATUS(output_vstream_infos);

    return fill_missing_vstream_params_with_default(output_vstream_params, output_vstream_infos.value(),
        quantized, format_type, timeout_ms, queue_size);
}

hailo_status Hef::Impl::fill_missing_vstream_params_with_default(std::map<std::string, hailo_vstream_params_t> &vstream_params,
    std::vector<hailo_vstream_info_t> &vstream_infos, bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms,
    uint32_t queue_size)
{
    hailo_format_flags_t flags = static_cast<hailo_format_flags_t>(HAILO_FORMAT_FLAGS_NONE);
    if (quantized) {
        flags = static_cast<hailo_format_flags_t>(flags | HAILO_FORMAT_FLAGS_QUANTIZED);
    }
    for (const auto &vstream_info : vstream_infos) {
        std::string vstream_name(vstream_info.name);
        if (contains(vstream_params, vstream_name)) {
            continue;
        }
        hailo_vstream_params_t params{};
        params.user_buffer_format.order = HAILO_FORMAT_ORDER_AUTO;
        params.user_buffer_format.type = format_type;
        params.user_buffer_format.flags = flags;
        params.timeout_ms = timeout_ms;
        params.queue_size = queue_size;
        vstream_params.insert(std::make_pair(vstream_name, params));
    }
    return HAILO_SUCCESS;
}

Expected<ConfigureNetworkParams> Hef::Impl::create_configure_params(hailo_stream_interface_t stream_interface, const std::string &network_group_name)
{
    auto params = HailoRTDefaults::get_configure_params();
    auto stream_params_by_name = create_stream_parameters_by_name(network_group_name, stream_interface);
    CHECK_EXPECTED(stream_params_by_name);
    params.stream_params_by_name = stream_params_by_name.release();
    auto network_params_by_name = create_network_parameters_by_name(network_group_name);
    CHECK_EXPECTED(network_params_by_name);
    params.network_params_by_name = network_params_by_name.release();

    return params;
}

Expected<ConfigureNetworkParams> Hef::Impl::create_configure_params_mipi_input(hailo_stream_interface_t output_interface,
    const hailo_mipi_input_stream_params_t &mipi_params, const std::string &network_group_name)
{
    auto params = HailoRTDefaults::get_configure_params();
    auto stream_params_by_name = create_stream_parameters_by_name_mipi_input(network_group_name, output_interface, mipi_params);
    CHECK_EXPECTED(stream_params_by_name);
    params.stream_params_by_name = stream_params_by_name.release();
    auto network_params_by_name = create_network_parameters_by_name(network_group_name);
    CHECK_EXPECTED(network_params_by_name);
    params.network_params_by_name = network_params_by_name.release();

    return params;
}

Expected<std::map<std::string, hailo_stream_parameters_t>> Hef::create_stream_parameters_by_name(
    const std::string &net_group_name, hailo_stream_interface_t stream_interface)
{
    auto network_group_name_pair = pimpl->get_network_group_and_network_name(net_group_name);
    CHECK_EXPECTED(network_group_name_pair);
    auto net_group_name_str = network_group_name_pair->first;

    return pimpl->create_stream_parameters_by_name(net_group_name_str, stream_interface);
}

Expected<std::map<std::string, hailo_stream_parameters_t>> Hef::Impl::create_stream_parameters_by_name(
    const std::string &net_group_name, hailo_stream_interface_t stream_interface)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    std::map<std::string, hailo_stream_parameters_t> results;
    auto input_layers_info = network_group_metadata->get_input_layer_infos();
    CHECK_EXPECTED(input_layers_info);
    for (auto &input_layer : input_layers_info.value()) {
        auto params = HailoRTDefaults::get_stream_parameters(stream_interface, HAILO_H2D_STREAM);
        CHECK_EXPECTED(params);
        results.emplace(std::make_pair(input_layer.name, params.release()));
    }
    auto output_layers_info = network_group_metadata->get_output_layer_infos();
    CHECK_EXPECTED(output_layers_info);
    for (auto &output_layer : output_layers_info.value()) {
        auto params = HailoRTDefaults::get_stream_parameters(stream_interface, HAILO_D2H_STREAM);
        CHECK_EXPECTED(params);
        results.emplace(std::make_pair(output_layer.name, params.release()));
    }

    return results;
}

Expected<std::map<std::string, hailo_network_parameters_t>> Hef::create_network_parameters_by_name(
    const std::string &net_group_name)
{
    return pimpl->create_network_parameters_by_name(net_group_name);
}

Expected<std::map<std::string, hailo_network_parameters_t>> Hef::Impl::create_network_parameters_by_name(
    const std::string &net_group_name)
{
    auto net_group = get_net_group_by_name(net_group_name);
    CHECK_EXPECTED(net_group);

    auto network_gorup_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_gorup_metadata);

    std::map<std::string, hailo_network_parameters_t> results;

    if (network_gorup_metadata->supported_features().multi_network_support) {
        CHECK_AS_EXPECTED((net_group.value()->networks_names_size() != 0), HAILO_INTERNAL_FAILURE, 
        "Hef support multiple networks, but no networks found in the proto");
        for (const auto &partial_network_name : net_group.value()->networks_names()) {
            auto network_name = HefUtils::get_network_name(net_group_name, partial_network_name);
            auto params = HailoRTDefaults::get_network_parameters();
            results.emplace(std::make_pair(network_name, params));
        }
    } else {
        /* For hefs without the "networks_names" field, build default network name with default params */
        auto params = HailoRTDefaults::get_network_parameters();
        auto network_name = HailoRTDefaults::get_network_name(net_group_name);
        results.emplace(std::make_pair(network_name, params));
    }

    return results;
}

Expected<std::map<std::string, hailo_stream_parameters_t>> Hef::create_stream_parameters_by_name_mipi_input(
    const std::string &net_group_name, hailo_stream_interface_t output_interface,
    const hailo_mipi_input_stream_params_t &mipi_params)
{
    auto network_group_name_pair = pimpl->get_network_group_and_network_name(net_group_name);
    CHECK_EXPECTED(network_group_name_pair);
    auto net_group_name_str = network_group_name_pair->first;

    return pimpl->create_stream_parameters_by_name_mipi_input(net_group_name_str, output_interface, mipi_params);
}

Expected<std::map<std::string, hailo_stream_parameters_t>> Hef::Impl::create_stream_parameters_by_name_mipi_input(
    const std::string &net_group_name, hailo_stream_interface_t output_interface,
    const hailo_mipi_input_stream_params_t &mipi_params)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    std::map<std::string, hailo_stream_parameters_t> results;
    auto input_layers_info = network_group_metadata->get_input_layer_infos();
    CHECK_EXPECTED(input_layers_info);
    for (auto &input_layer : input_layers_info.value()) {
        hailo_stream_parameters_t params = {};
        params.direction = HAILO_H2D_STREAM;
        params.stream_interface = HAILO_STREAM_INTERFACE_MIPI;
        params.mipi_input_params = mipi_params;
        results.emplace(std::make_pair(input_layer.name, params));
    }
    auto output_layers_info = network_group_metadata->get_output_layer_infos();
    CHECK_EXPECTED(output_layers_info);
    for (auto &output_layer : output_layers_info.value()) {
        auto params = HailoRTDefaults::get_stream_parameters(output_interface, HAILO_D2H_STREAM);
        CHECK_EXPECTED(params);
        results.emplace(std::make_pair(output_layer.name, params.release()));
    }

    return results;
}

NetworkGroupMetadata::NetworkGroupMetadata(const std::string &network_group_name, std::vector<LayerInfo> &&layer_infos,
    std::vector<std::string> &&sorted_output_names, NetworkGroupSupportedFeatures &supported_features,
    const std::vector<std::string> &sorted_network_names)
    : m_network_group_name(network_group_name), m_sorted_output_names(std::move(sorted_output_names)), m_supported_features(supported_features),
        m_sorted_network_names(sorted_network_names)
{
    for (auto &layer_info : layer_infos) {
        if (HAILO_H2D_STREAM == layer_info.direction) {
            m_input_layer_infos[layer_info.network_name].push_back(layer_info);
        } else {
            m_output_layer_infos[layer_info.network_name].push_back(layer_info);
        }
    }
}

Expected<LayerInfo> NetworkGroupMetadata::get_layer_info_by_stream_name(const std::string &stream_name) const
{
    auto layer_infos = get_all_layer_infos();
    CHECK_EXPECTED(layer_infos);
    for (auto layer_info : layer_infos.release()) {
        if (layer_info.name == stream_name) {
            return layer_info;
        }
    }
    LOGGER__ERROR("Failed to find layer with name {}", stream_name);
    return make_unexpected(HAILO_NOT_FOUND);    
}

Expected<std::vector<LayerInfo>> NetworkGroupMetadata::get_input_layer_infos(const std::string &network_name) const
{
    CHECK_AS_EXPECTED((network_name.empty()) || (network_name == default_network_name()) || contains(m_input_layer_infos, network_name),
        HAILO_NOT_FOUND, "Network name {} is not found in networks metadata", network_name);
    std::vector<LayerInfo> res;
    for (auto &layer_infos_pair : m_input_layer_infos) {
        if ((network_name == layer_infos_pair.first) || (network_name.empty()) || (network_name == default_network_name())) {
            res.insert(res.end(), layer_infos_pair.second.begin(), layer_infos_pair.second.end());
        }
    }
    return res;
}

Expected<std::vector<LayerInfo>> NetworkGroupMetadata::get_output_layer_infos(const std::string &network_name) const
{
    CHECK_AS_EXPECTED((network_name.empty()) || (network_name == default_network_name()) || contains(m_output_layer_infos, network_name),
        HAILO_NOT_FOUND, "Network name {} is not found in networks metadata", network_name);
    std::vector<LayerInfo> res;
    for (auto &layer_infos_pair : m_output_layer_infos) {
        if ((network_name == layer_infos_pair.first) || (network_name.empty()) || (network_name == default_network_name())) {
            res.insert(res.end(), layer_infos_pair.second.begin(), layer_infos_pair.second.end());
        }
    }
    return res;
}

Expected<std::vector<LayerInfo>> NetworkGroupMetadata::get_all_layer_infos(const std::string &network_name) const
{
    auto input_layer_infos = get_input_layer_infos(network_name);
    CHECK_EXPECTED(input_layer_infos);

    auto output_layer_infos = get_output_layer_infos(network_name);
    CHECK_EXPECTED(output_layer_infos);

    std::vector<LayerInfo> res;
    res.reserve(input_layer_infos->size() + output_layer_infos->size());
    res.insert(res.end(), input_layer_infos->begin(), input_layer_infos->end());
    res.insert(res.end(), output_layer_infos->begin(), output_layer_infos->end());

    return res;
}

Expected<std::vector<hailo_stream_info_t>> NetworkGroupMetadata::get_input_stream_infos(const std::string &network_name) const
{
    auto input_layer_infos = get_input_layer_infos(network_name);
    CHECK_EXPECTED(input_layer_infos);

    return convert_layer_infos_to_stream_infos(input_layer_infos.value());
}

Expected<std::vector<hailo_stream_info_t>> NetworkGroupMetadata::get_output_stream_infos(const std::string &network_name) const
{
    auto output_layer_infos = get_output_layer_infos(network_name);
    CHECK_EXPECTED(output_layer_infos);

    return convert_layer_infos_to_stream_infos(output_layer_infos.value());
}

Expected<std::vector<hailo_stream_info_t>> NetworkGroupMetadata::get_all_stream_infos(const std::string &network_name) const
{
    auto input_stream_infos = get_input_stream_infos(network_name);
    CHECK_EXPECTED(input_stream_infos);

    auto output_stream_infos = get_output_stream_infos(network_name);
    CHECK_EXPECTED(output_stream_infos);

    std::vector<hailo_stream_info_t> res;
    res.reserve(input_stream_infos->size() + output_stream_infos->size());
    res.insert(res.end(), input_stream_infos->begin(), input_stream_infos->end());
    res.insert(res.end(), output_stream_infos->begin(), output_stream_infos->end());

    return res;
}

Expected<std::vector<hailo_vstream_info_t>> NetworkGroupMetadata::get_input_vstream_infos(const std::string &network_name) const
{
    auto input_layer_infos = get_input_layer_infos(network_name);
    CHECK_EXPECTED(input_layer_infos);

    return convert_layer_infos_to_vstream_infos(input_layer_infos.value());
}

Expected<std::vector<hailo_vstream_info_t>> NetworkGroupMetadata::get_output_vstream_infos(const std::string &network_name) const
{
    auto output_layer_infos = get_output_layer_infos(network_name);
    CHECK_EXPECTED(output_layer_infos);
    
    auto res = convert_layer_infos_to_vstream_infos(output_layer_infos.value());

    hailo_status status = HAILO_SUCCESS;
    std::sort(res.begin(), res.end(),
        [this, &status](const auto &info1, const auto &info2)
    {
        const auto index1 = std::find(m_sorted_output_names.begin(), m_sorted_output_names.end(), std::string(info1.name));
        const auto index2 = std::find(m_sorted_output_names.begin(), m_sorted_output_names.end(), std::string(info2.name));

        if (m_sorted_output_names.end() == index1) {
            LOGGER__ERROR("Stream {} not found in sorted output names", info1.name);
            status = HAILO_INTERNAL_FAILURE;
            return false;
        }

        if (m_sorted_output_names.end() == index2) {
            LOGGER__ERROR("Stream {} not found in sorted output names", info2.name);
            status = HAILO_INTERNAL_FAILURE;
            return false;
        }

        return index1 < index2;
    });
    CHECK_SUCCESS_AS_EXPECTED(status);

    return res;
}

Expected<std::vector<hailo_vstream_info_t>> NetworkGroupMetadata::get_all_vstream_infos(const std::string &network_name) const
{
    auto input_vstream_infos = get_input_vstream_infos(network_name);
    CHECK_EXPECTED(input_vstream_infos);

    auto output_vstream_infos = get_output_vstream_infos(network_name);
    CHECK_EXPECTED(output_vstream_infos);

    std::vector<hailo_vstream_info_t> res;
    res.reserve(input_vstream_infos->size() + output_vstream_infos->size());
    res.insert(res.end(), input_vstream_infos->begin(), input_vstream_infos->end());
    res.insert(res.end(), output_vstream_infos->begin(), output_vstream_infos->end());

    return res;
}

Expected<std::vector<std::string>> NetworkGroupMetadata::get_vstream_names_from_stream_name(const std::string &stream_name) const
{
    std::vector<std::string> results;
    auto all_layer_infos =  get_all_layer_infos();
    CHECK_EXPECTED(all_layer_infos);

    for (auto &layer_info : all_layer_infos.value()) {
        if (stream_name == layer_info.name) {
            if (layer_info.is_defused_nms) {
                return std::vector<std::string> (1, layer_info.fused_nms_layer[0].name);
            } else if (layer_info.is_mux) {
                return get_demuxes_names(layer_info);
            } else {
                return std::vector<std::string> (1, layer_info.name);
            }
        }
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

Expected<std::vector<std::string>> NetworkGroupMetadata::get_stream_names_from_vstream_name(const std::string &vstream_name) const
{
    std::vector<std::string> results;
    auto all_layer_infos =  get_all_layer_infos();
    CHECK_EXPECTED(all_layer_infos);

    for (auto &layer_info : all_layer_infos.value()) {
        if (layer_info.is_mux) {
            if (is_edge_under_mux(layer_info, vstream_name)) {
                // vstream_name is a demux of the layer info
                results.push_back(layer_info.name);
            }
        } else if (layer_info.is_defused_nms) {
            if (vstream_name == layer_info.fused_nms_layer[0].name) {
                // vstream_name is the fused-layer of the layer info
                results.push_back(layer_info.name);
            }
        } else if (vstream_name == layer_info.name) {
            // vstream_name is a regular stream
            results.push_back(layer_info.name);
        }
    }
    CHECK_AS_EXPECTED(0 < results.size(), HAILO_NOT_FOUND);
    return results;
}

std::vector<hailo_stream_info_t> NetworkGroupMetadata::convert_layer_infos_to_stream_infos(const std::vector<LayerInfo> &layer_infos) const
{
    std::vector<hailo_stream_info_t> res;
    for (auto &layer_info : layer_infos) {
        res.push_back(LayerInfoUtils::get_stream_info_from_layer_info(layer_info));
    }
    return res;
}

std::vector<hailo_vstream_info_t> NetworkGroupMetadata::convert_layer_infos_to_vstream_infos(const std::vector<LayerInfo> &layer_infos) const
{
    std::vector<hailo_vstream_info_t> res;
    for (auto &layer_info : layer_infos) {
        auto vstream_infos = LayerInfoUtils::get_vstream_infos_from_layer_info(layer_info);
        for (const auto &vstream_info : vstream_infos) {
            // In case of fused nms layers, several LayerInfos will contain data about the same fused layer
            if (!LayerInfoUtils::vstream_info_already_in_vector(res, vstream_info.name)) {
                res.push_back(vstream_info);
            }
        }
    }
    return res;
}

Expected<std::vector<hailo_network_info_t>> NetworkGroupMetadata::get_network_infos() const
{
    std::vector<hailo_network_info_t> network_infos;
    auto net_group_name = network_group_name();
    network_infos.reserve(m_sorted_network_names.size());
    for (auto const &network_name : m_sorted_network_names) {
        hailo_network_info_t network_info = {};
        CHECK_AS_EXPECTED(HAILO_MAX_NETWORK_NAME_SIZE >= (network_name.length() + 1), HAILO_INTERNAL_FAILURE,
            "The network '{}' has a too long name (max is HAILO_MAX_NETWORK_NAME_SIZE)", network_name);  
        memcpy(network_info.name, network_name.c_str(), network_name.length() + 1);

        network_infos.push_back(network_info);
    }

    return network_infos;
}

} /* namespace hailort */
