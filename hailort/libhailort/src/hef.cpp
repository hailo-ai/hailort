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
#include "common/string_utils.hpp"

#include "pcie_device.hpp"
#include "context_switch/multi_context/vdma_config_manager.hpp"
#include "context_switch/single_context/hcp_config_network_group.hpp"
#include "byte_order.h"
#include "common/logger_macros.hpp"
#include "common/file_utils.hpp"
#include "layer_info.hpp"
#include "control.hpp"
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
#define DEFAULT_BATCH_SIZE (1)

static const uint8_t ENABLE_LCU_CONTROL_WORD[4] = {1, 0, 0, 0};

#pragma pack(push, 1)
typedef struct {
    uint32_t words_count;
    uint32_t address;
} CcwHeader;
#pragma pack(pop)

bool ConfigureNetworkParams::operator==(const ConfigureNetworkParams &other) const
{
    for (auto &name_param_pair : network_params_by_name) {
        if ((other.network_params_by_name.find(name_param_pair.first) == other.network_params_by_name.end()) ||
                (name_param_pair.second.batch_size != other.network_params_by_name.at(name_param_pair.first).batch_size) ) {
            return false;
        }
    }
    return (batch_size == other.batch_size) && (power_mode == other.power_mode) && (latency == other.latency);
}

bool ConfigureNetworkParams::operator!=(const ConfigureNetworkParams &other) const
{
    return !(*this == other);
}


// Note: Can't add the definition in the header. This will lead to the following error:
//       /usr/include/c++/7/bits/unique_ptr.h: In instantiation of 'void std::default_delete<_Tp>::operator()(_Tp*) const [with _Tp = Hef::Impl]':
//       /usr/include/c++/7/bits/unique_ptr.h:263:17:   required from 'std::unique_ptr<_Tp, _Dp>::~unique_ptr() [with _Tp = Hef::Impl; _Dp = std::default_delete<Hef::Impl>]'
//       /local/users/projects/platform-sw/hailort/libhailort/src/../include/hailo/hef.hpp:61:7:   required from 'Expected<T>::~Expected() [with T = Hef]'
//       /local/users/projects/platform-sw/hailort/hailortcli/run_command.cpp:705:51:   required from here
//       /usr/include/c++/7/bits/unique_ptr.h:76:22: error: invalid application of 'sizeof' to incomplete type 'Hef::Impl'
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

void Hef::Impl::init_md5(MD5_SUM_t &calculated_md5)
{
    memcpy(m_md5, calculated_md5, sizeof(m_md5));
}

hailo_status Hef::Impl::parse_hef_file(const std::string &hef_path)
{
#ifdef HAILO_SUPPORT_MULTI_PROCESS
    auto hef_buffer = read_binary_file(hef_path);
    CHECK_EXPECTED_AS_STATUS(hef_buffer);
    m_hef_buffer = hef_buffer.release();
#endif // HAILO_SUPPORT_MULTI_PROCESS

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

    init_md5(calculated_md5);

    ProtoHEFHef hef_message;
    auto rb = hef_message.ParseFromIstream(&hef_file);
    CHECK(rb, HAILO_INVALID_HEF, "Failed parsing HEF file");
    status = transfer_protobuf_field_ownership(hef_message);
    CHECK_SUCCESS(status);

    fill_core_ops();

    status = fill_networks_metadata();
    CHECK_SUCCESS(status);

    // Must be called after fill_networks_metadata
    status = validate_hef_extensions();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::parse_hef_memview(const MemoryView &hef_memview)
{
#ifdef HAILO_SUPPORT_MULTI_PROCESS
    auto hef_buffer = Buffer::create(hef_memview.data(), hef_memview.size());
    CHECK_EXPECTED_AS_STATUS(hef_buffer);
    m_hef_buffer = hef_buffer.release();
#endif // HAILO_SUPPORT_MULTI_PROCESS

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

    init_md5(calculated_md5);

    ProtoHEFHef hef_message;
    auto rb = hef_message.ParseFromArray(proto_buffer, static_cast<int>(proto_size));
    CHECK(rb, HAILO_INVALID_HEF, "Failed parsing HEF buffer");
    status = transfer_protobuf_field_ownership(hef_message);
    CHECK_SUCCESS(status);

    fill_core_ops();

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

    NetworkGroupMetadataPerArch metadata;
    uint32_t partial_clusters_layout_bitmap = 0;

    for (auto &network_group : m_groups) {
        auto network_group_name = HefUtils::get_network_group_name(*network_group, m_supported_features);
        // TODO: keep metadata per core_op (HRT-8639)
        const auto &core_ops = m_core_ops_per_group[network_group_name];
        assert(core_ops.size() == 1);
        const auto &core_op = core_ops[0];
        if (ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L == get_device_arch()) {
            if (m_supported_features.hailo_net_flow) {
                for (auto &partial_core_op : core_op.partial_core_ops) {
                    partial_clusters_layout_bitmap = partial_core_op->layout.partial_clusters_layout_bitmap();
                    auto metadata_per_arch = create_metadata_per_arch(*(partial_core_op->core_op));
                    CHECK_EXPECTED_AS_STATUS(metadata_per_arch);
                    auto &&arch_metadata = metadata_per_arch.release();
                    auto expected_net_flow_ops = create_network_group_ops(*network_group, arch_metadata);
                    CHECK_EXPECTED_AS_STATUS(expected_net_flow_ops);
                    m_post_process_ops_per_group.insert({arch_metadata.network_group_name(), expected_net_flow_ops.value()});
                    metadata.add_metadata(arch_metadata, partial_clusters_layout_bitmap);
                }
            } else {
                for (auto &partial_network_group : network_group->partial_network_groups()) {
                    partial_clusters_layout_bitmap = partial_network_group.layout().partial_clusters_layout_bitmap();
                    ProtoHEFCoreOpMock partial_core_op{
                        partial_network_group.network_group().network_group_metadata(),
                        partial_network_group.network_group().preliminary_config(),
                        partial_network_group.network_group().contexts(),
                        partial_network_group.network_group().sorted_outputs_order(),
                        partial_network_group.network_group().fused_layers_metadata(),
                        partial_network_group.network_group().networks_names(),
                        {}
                    };
                    auto metadata_per_arch = create_metadata_per_arch(partial_core_op);
                    CHECK_EXPECTED_AS_STATUS(metadata_per_arch);
                    auto &&arch_metadata = metadata_per_arch.release();
                    std::vector<std::shared_ptr<hailort::NetFlowElement>> empty_ops;
                    m_post_process_ops_per_group.insert({arch_metadata.network_group_name(), empty_ops});
                    metadata.add_metadata(arch_metadata, partial_clusters_layout_bitmap);
                }
            }
        } else {
            partial_clusters_layout_bitmap = PARTIAL_CLUSTERS_LAYOUT_IGNORE;
            auto metadata_per_arch = create_metadata_per_arch(core_op);
            CHECK_EXPECTED_AS_STATUS(metadata_per_arch);
            auto &&arch_metadata = metadata_per_arch.release();
            auto expected_net_flow_ops = create_network_group_ops(*network_group, arch_metadata);
            CHECK_EXPECTED_AS_STATUS(expected_net_flow_ops);
            m_post_process_ops_per_group.insert({arch_metadata.network_group_name(), expected_net_flow_ops.value()});
            metadata.add_metadata(arch_metadata, partial_clusters_layout_bitmap);
        }
        CHECK(!contains(m_network_group_metadata_per_arch, network_group_name),
            HAILO_INVALID_OPERATION, "Network group with the name {} is already configured on the device", network_group_name);
        m_network_group_metadata_per_arch.emplace(network_group_name, metadata);        
    }
    return HAILO_SUCCESS;
}

static Expected<std::vector<ConfigChannelInfo>> parse_config_channels_info(const ProtoHEFCoreOpMock &core_op)
{
    const auto &metadata = core_op.network_group_metadata;
    // Backwards compatibility for HEFs without the cfg_channels_count field
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(metadata.cfg_channels_count()),
        HAILO_INVALID_HEF, "Invalid cfg channels count");
    const uint8_t cfg_channels_count = (0 == metadata.cfg_channels_count()) ?
        1 : static_cast<uint8_t>(metadata.cfg_channels_count());


    std::vector<ConfigChannelInfo> config_channels_info;
    config_channels_info.reserve(cfg_channels_count);
    const auto &cfg_channels_config = metadata.cfg_channels_config();
    for (uint8_t config_stream_index = 0; config_stream_index < cfg_channels_count; config_stream_index++) {
        auto cfg_info = std::find_if(cfg_channels_config.begin(), cfg_channels_config.end(),
            [config_stream_index](const auto &cfg_info)
            {
                return cfg_info.cfg_channel_index() == config_stream_index;
            });

        if (cfg_info != cfg_channels_config.end()) {
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(cfg_info->engine_id()), HAILO_INVALID_HEF, "Invalid dma engine index");
            config_channels_info.emplace_back(ConfigChannelInfo{static_cast<uint8_t>(cfg_info->engine_id())});
        }
        else {
            // Not found - can happen on old HEF or hailo8. In those case we want to use the default engine
            config_channels_info.emplace_back(ConfigChannelInfo{vdma::DEFAULT_ENGINE_INDEX});
        }
    }

    return config_channels_info;
}

Expected<NetworkGroupMetadata> Hef::Impl::create_metadata_per_arch(const ProtoHEFCoreOpMock &core_op)
{
    auto preliminary_context = HefUtils::parse_preliminary_context(core_op.preliminary_config, m_supported_features);
    CHECK_EXPECTED(preliminary_context);

    auto dynamic_contexts = HefUtils::parse_dynamic_contexts(core_op, m_supported_features);
    CHECK_EXPECTED(dynamic_contexts);

    auto config_channels_info = parse_config_channels_info(core_op);
    CHECK_EXPECTED(config_channels_info);

    auto sorted_output_names = HefUtils::get_sorted_output_names(core_op);
    CHECK_EXPECTED(sorted_output_names);

    std::vector<std::string> sorted_network_names;
    if (m_supported_features.multi_network_support) {
        sorted_network_names.reserve(core_op.networks_names.size());
        for (auto &partial_network_name : core_op.networks_names) {
            auto network_name = HefUtils::get_network_name(core_op, partial_network_name);
            sorted_network_names.push_back(network_name);
        }
    } else {
        sorted_network_names.push_back(HailoRTDefaults::get_network_name(core_op.network_group_metadata.network_group_name()));
    }

    NetworkGroupMetadata metadata_per_arch(core_op.network_group_metadata.network_group_name(),
        preliminary_context.release(), dynamic_contexts.release(), config_channels_info.release(),
        sorted_output_names.release(), m_supported_features, sorted_network_names);
    return metadata_per_arch;
}

void Hef::Impl::fill_core_ops()
{
    if (m_supported_features.hailo_net_flow) {
        for (const auto &net_group : m_groups) {
            auto core_op_iter = std::find_if(net_group->ops().begin(), net_group->ops().end(),
                [](auto &op) {
                    return op.op_case() == ProtoHEFOp::kCoreOp;
                });
            assert(core_op_iter != m_groups[0]->ops().end());
            std::vector<std::shared_ptr<ProtoHEFPartialCoreOpMock>> partial_core_ops;
            partial_core_ops.reserve(core_op_iter->core_op().partial_core_ops().size());
            for (auto &partial_core_op : core_op_iter->core_op().partial_core_ops()) {
                ProtoHEFCoreOpMock core_op{
                    partial_core_op.core_op().network_group_metadata(),
                    partial_core_op.core_op().preliminary_config(),
                    partial_core_op.core_op().contexts(),
                    partial_core_op.core_op().sorted_outputs_order(),
                    partial_core_op.core_op().fused_layers_metadata(),
                    partial_core_op.core_op().networks_names(),
                    {}
                };
                ProtoHEFPartialCoreOpMock partial_core_op_mock{
                    std::make_shared<ProtoHEFCoreOpMock>(core_op),
                    partial_core_op.layout()
                };
                partial_core_ops.push_back(std::make_shared<ProtoHEFPartialCoreOpMock>(partial_core_op_mock));
            }
            ProtoHEFCoreOpMock core_op{
                core_op_iter->core_op().network_group_metadata(),
                core_op_iter->core_op().preliminary_config(),
                core_op_iter->core_op().contexts(),
                core_op_iter->core_op().sorted_outputs_order(),
                core_op_iter->core_op().fused_layers_metadata(),
                core_op_iter->core_op().networks_names(),
                partial_core_ops
            };
            auto net_group_name = HefUtils::get_network_group_name(*net_group, m_supported_features);
            m_core_ops_per_group[net_group_name].push_back(std::move(core_op));
        }
    } else {
        for (const auto &net_group : m_groups) {
            std::vector<std::shared_ptr<ProtoHEFPartialCoreOpMock>> partial_core_ops;
            partial_core_ops.reserve(net_group->partial_network_groups().size());
            for (auto &partial_network_group : net_group->partial_network_groups()) {
                ProtoHEFCoreOpMock core_op{
                    partial_network_group.network_group().network_group_metadata(),
                    partial_network_group.network_group().preliminary_config(),
                    partial_network_group.network_group().contexts(),
                    partial_network_group.network_group().sorted_outputs_order(),
                    partial_network_group.network_group().fused_layers_metadata(),
                    partial_network_group.network_group().networks_names(),
                    {}
                };
                ProtoHEFPartialCoreOpMock partial_core_op{
                    std::make_shared<ProtoHEFCoreOpMock>(core_op),
                    partial_network_group.layout()
                };
                partial_core_ops.push_back(std::make_shared<ProtoHEFPartialCoreOpMock>(partial_core_op));
            }
            ProtoHEFCoreOpMock core_op{
                net_group->network_group_metadata(),
                net_group->preliminary_config(),
                net_group->contexts(),
                net_group->sorted_outputs_order(),
                net_group->fused_layers_metadata(),
                net_group->networks_names(),
                partial_core_ops
            };
            auto net_group_name = HefUtils::get_network_group_name(*net_group, m_supported_features);
            m_core_ops_per_group[net_group_name].push_back(std::move(core_op));
        }
    }
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

    m_supported_features = get_supported_features(m_header, m_hef_extensions, m_included_features,
        m_hef_optional_extensions);

    return HAILO_SUCCESS;
}

#ifdef HAILO_SUPPORT_MULTI_PROCESS
const MemoryView Hef::Impl::get_hef_memview()
{
    return MemoryView(m_hef_buffer);
}
#endif // HAILO_SUPPORT_MULTI_PROCESS

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

SupportedFeatures Hef::Impl::get_supported_features(const ProtoHEFHeader &header,
        const std::vector<ProtoHEFExtension> &hef_extensions, const ProtoHEFIncludedFeatures &included_features,
        const std::vector<ProtoHEFOptionalExtension> &hef_optional_extensions)
{
    SupportedFeatures supported_features{};
    supported_features.padded_ddr_buffers = check_hef_extension(ProtoHEFExtensionType::PADDED_DDR_BUFFERS,
        header, hef_extensions, included_features);
    supported_features.multi_network_support = check_hef_optional_extension(ProtoHEFExtensionType::MULTI_NETWORK_VARIABLE_BATCH_SIZE,
        header, hef_optional_extensions);
    supported_features.multi_context = check_hef_extension(ProtoHEFExtensionType::IS_MULTI_CONTEXTS,
        header, hef_extensions, included_features);
    supported_features.preliminary_run_asap = check_hef_extension(ProtoHEFExtensionType::KO_RUN_ASAP,
        header, hef_extensions, included_features);
    supported_features.hailo_net_flow = check_hef_extension(ProtoHEFExtensionType::HAILO_NET_FLOW,
        header, hef_extensions, included_features);

    return supported_features;
}

Expected<std::vector<std::shared_ptr<NetFlowElement>>> Hef::Impl::create_network_group_ops(const ProtoHEFNetworkGroup &network_group_proto,
    NetworkGroupMetadata &network_group_meta_data) const
{
    std::vector<std::shared_ptr<NetFlowElement>> result;
    if (!m_supported_features.hailo_net_flow) {
        return result;
    }
    auto output_layer_infos = network_group_meta_data.get_output_layer_infos();
    std::map<size_t, LayerInfo> pad_index_to_streams_info;
    for (auto &output_layer_info : output_layer_infos) {
        if (output_layer_info.pad_index != INVALID_PAD_INDEX) {
            pad_index_to_streams_info.insert({output_layer_info.pad_index, output_layer_info});
        }
    }
    std::map<size_t, size_t> input_to_output_pads;
    for (auto &pad_edge : network_group_proto.pad_edges()) {
        input_to_output_pads.insert({pad_edge.dst(), pad_edge.src()});
    }
    for (auto &op_proto : network_group_proto.ops()) {
        switch (op_proto.op_case()) {
            case ProtoHEFOp::kCoreOp: {
                break;
            }
            case ProtoHEFOp::kNmsOp: {
                NetFlowYoloNmsElement nms_op{};
                nms_op.type = NetFlowElement::Type::YoloNmsOp;
                nms_op.name = "YOLO_NMS";
                nms_op.nms_score_th = (float32_t)op_proto.nms_op().nms_score_th();
                nms_op.nms_iou_th = (float32_t)op_proto.nms_op().nms_iou_th();
                nms_op.max_proposals_per_class = op_proto.nms_op().max_proposals_per_class();
                nms_op.classes = op_proto.nms_op().classes();
                nms_op.background_removal = op_proto.nms_op().background_removal();
                nms_op.background_removal_index = op_proto.nms_op().background_removal_index();
                nms_op.image_height = (float32_t)op_proto.nms_op().yolo_nms_op().image_height();
                nms_op.image_width = (float32_t)op_proto.nms_op().yolo_nms_op().image_width();
                nms_op.input_division_factor = op_proto.nms_op().yolo_nms_op().input_division_factor();
                if (!nms_op.input_division_factor) {
                    nms_op.input_division_factor = 1;
                }
                nms_op.bbox_decoders.reserve(op_proto.nms_op().yolo_nms_op().bbox_decoders().size());
                for (auto &bbox_proto : op_proto.nms_op().yolo_nms_op().bbox_decoders()) {
                    YoloBboxDecoder yolo_bbox_decoder;
                    for (auto h : bbox_proto.h()) {
                        yolo_bbox_decoder.h.push_back(h);
                    }
                    for (auto w : bbox_proto.w()) {
                        yolo_bbox_decoder.w.push_back(w);
                    }
                    yolo_bbox_decoder.stride = bbox_proto.stride();
                    yolo_bbox_decoder.stream_name = pad_index_to_streams_info[input_to_output_pads[bbox_proto.pad_index()]].name;
                    nms_op.bbox_decoders.push_back(yolo_bbox_decoder);
                }
                std::set<uint32_t> input_pads;
                std::transform(op_proto.input_pads().begin(), op_proto.input_pads().end(), std::inserter(input_pads, input_pads.begin()),
                    [](auto &pad) {
                        return pad.index();
                    });
                for (auto &input_pad : op_proto.input_pads()) {
                    CHECK_AS_EXPECTED(input_to_output_pads.count(input_pad.index()), HAILO_INVALID_HEF,
                        "NMS op is not connected to core op");
                    auto output_pad_index = input_to_output_pads[input_pad.index()];
                    CHECK_AS_EXPECTED(pad_index_to_streams_info.count(output_pad_index), HAILO_INVALID_HEF,
                        "Pad {} of post-process {} is not connected to any core output stream",
                            input_pad.index(), op_proto.name());
                    const auto &op_input_stream = pad_index_to_streams_info[output_pad_index];
                    nms_op.input_pads.push_back(NetFlowPad{input_pad.name(), op_input_stream.format, op_input_stream.quant_info, 0});
                    nms_op.input_streams.insert(op_input_stream.name);
                }
                hailo_format_t format;
                format.type = HAILO_FORMAT_TYPE_FLOAT32;
                format.order = HAILO_FORMAT_ORDER_HAILO_NMS;
                format.flags = HAILO_FORMAT_FLAGS_QUANTIZED;
                assert(op_proto.output_pads().size() == 1);
                auto proto_output_pad = op_proto.output_pads()[0];
                nms_op.output_pads.push_back(NetFlowPad{proto_output_pad.name(), format, hailo_quant_info_t(), nms_op.classes});
                result.push_back(std::shared_ptr<NetFlowElement>(std::make_shared<NetFlowYoloNmsElement>(nms_op)));

                // Fill meta-data output vstream info
                auto net_group_name = HefUtils::get_network_group_name(network_group_proto, m_supported_features);
                auto network_name = HailoRTDefaults::get_network_name(net_group_name);
                hailo_vstream_info_t net_flow_output_vstream_info{};
                strncpy(net_flow_output_vstream_info.name, proto_output_pad.name().c_str(), proto_output_pad.name().length() + 1);
                strncpy(net_flow_output_vstream_info.network_name, network_name.c_str(), network_name.length() + 1);
                net_flow_output_vstream_info.direction = HAILO_D2H_STREAM;
                net_flow_output_vstream_info.format = format;
                net_flow_output_vstream_info.nms_shape.number_of_classes = nms_op.classes;
                net_flow_output_vstream_info.nms_shape.max_bboxes_per_class = nms_op.max_proposals_per_class;
                network_group_meta_data.add_output_vstream_info(net_flow_output_vstream_info);
                break;
            }
            default: {
                LOGGER__ERROR("Unsupported Net-Flow Op");
                return make_unexpected(HAILO_INTERNAL_FAILURE);
            }
        }
    }
    return result;
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
    stream_config.periph_buffers_per_frame = core_buffers_per_frame; // periph buffers per frame is the same (even if
                                                                     // for hw padding each buffer is smaller).


    /* For DDR buffering - core buffers is depended on the amount of buffers per PCIe interrupt. No HW padding required */
    if (is_ddr) {
        stream_config.core_buffers_per_frame = 1;
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
    // TODO HRT-7177 - pass interface to layer info instead of re-calculated Layer info from stream_internal.hpp
    // After passing stream interface, there is no need for this function. Just use CONTROL_PROTOCOL__nn_stream_config_t from layer info. 
    auto is_ddr = false; // This function is called only on boundary layers, so no DDR
    return parse_nn_stream_config(edge_layer.format.order, edge_layer.hw_shape.width, edge_layer.hw_shape.features,
        edge_layer.hw_data_bytes, edge_layer.nn_stream_config.core_buffers_per_frame, 
        edge_layer.nn_stream_config.core_bytes_per_buffer, hw_padding_supported, is_ddr);
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

    if ((width * features * hw_data_bytes) > 
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
    return is_hw_padding_supported(is_boundary, layer_info.is_mux, layer_info.format.order,
        layer_info.nn_stream_config.core_buffers_per_frame, height, width, 
        layer_info.shape.features, layer_info.hw_data_bytes);
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

    if (!IS_FIT_IN_UINT16(edge_layer_base.core_buffers_per_frame())) {
        LOGGER__DEBUG("Invalid core_buffers_per_frame. Not enabling hw padding");
        return false;
    }

    auto format_order = format_order_exp.release();
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

const std::vector<ProtoHEFCoreOpMock>& Hef::Impl::core_ops(const std::string &net_group_name) const
{
    assert(contains(m_core_ops_per_group, net_group_name));
    return m_core_ops_per_group.at(net_group_name);
};

const std::vector<std::shared_ptr<hailort::NetFlowElement>> Hef::Impl::post_process_ops(const std::string &net_group_name) const
{
    assert(contains(m_post_process_ops_per_group, net_group_name));
    return m_post_process_ops_per_group.at(net_group_name);
}

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
            return included_features.is_multi_context();
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
        network_group_name = (ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L == get_device_arch()) ?
            m_groups[0]->partial_network_groups(0).network_group().network_group_metadata().network_group_name()
            : m_groups[0]->network_group_metadata().network_group_name();
        LOGGER__INFO("No name was given. Addressing all networks of default network_group: {}",
            network_group_name);
        auto network_name = HailoRTDefaults::get_network_name(network_group_name);
        return std::make_pair(network_group_name, network_name);
    } else {
        const ProtoHEFNetworkGroup *network_group_ptr = nullptr;
        for (const auto &network_group : m_groups) {
            // TODO: Handle new HEFs
            network_group_ptr = (ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L == get_device_arch()) ?
                &network_group->partial_network_groups(0).network_group()
                : network_group.get();
            network_group_name = network_group_ptr->network_group_metadata().network_group_name();

            // Look for network_group with the given name
            if (name == network_group_name) {
                auto network_name = HailoRTDefaults::get_network_name(network_group_name);
                return std::make_pair(network_group_name, network_name);
            }
            // Look for network with the given name
            for (const auto &partial_network_name : network_group_ptr->networks_names()) {
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

// TODO: core_ops names?
Expected<std::shared_ptr<ProtoHEFCoreOpMock>> Hef::Impl::get_core_op_by_net_group_name(const std::string &net_group_name)
{
    if ("" == net_group_name) {
        auto network_group_ptr = m_groups[0];
        auto network_group_name = HefUtils::get_network_group_name(*network_group_ptr, m_supported_features);
        LOGGER__INFO("No network_group name was given. Addressing default network_group: {}", network_group_name);
        const auto &core_op = m_core_ops_per_group[network_group_name][0];
        if (ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L == get_device_arch()) {
            auto partial_core_op = core_op.partial_core_ops[0];
            return std::make_shared<ProtoHEFCoreOpMock>(*(partial_core_op->core_op));
        }
        return std::make_shared<ProtoHEFCoreOpMock>(core_op);
    }
    CHECK_AS_EXPECTED(contains(m_core_ops_per_group, net_group_name), HAILO_NOT_FOUND,
        "HEF does not contain network_group with name {}", net_group_name);
    const auto &core_op = m_core_ops_per_group[net_group_name][0];
    if (ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L == get_device_arch()) {
        auto partial_core_op = core_op.partial_core_ops[0];
        return std::make_shared<ProtoHEFCoreOpMock>(*(partial_core_op->core_op));
    }
    return std::make_shared<ProtoHEFCoreOpMock>(core_op);
}

Expected<size_t> Hef::Impl::get_number_of_input_streams(const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    auto input_layer_infos = network_group_metadata->get_input_layer_infos();
    return input_layer_infos.size();
}

Expected<size_t> Hef::Impl::get_number_of_output_streams(const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    auto output_layer_infos = network_group_metadata->get_output_layer_infos();
    return output_layer_infos.size();
}

static Expected<LayerType> get_layer_type(const ProtoHEFEdgeConnectionType &edge_connection_type)
{
    switch (edge_connection_type) {
    case PROTO__EDGE_CONNECTION_TYPE__BOUNDARY:
        return LayerType::BOUNDARY;
    case PROTO__EDGE_CONNECTION_TYPE__INTERMEDIATE:
        return LayerType::INTER_CONTEXT;
    case PROTO__EDGE_CONNECTION_TYPE__DDR:
        return LayerType::DDR;
    default:
        LOGGER__ERROR("Not supported edge connection type {}", edge_connection_type);
        return make_unexpected(HAILO_INVALID_HEF);
    }
}

hailo_status HefUtils::fill_layer_info_with_base_info(const ProtoHEFEdgeLayerBase &base_info, 
    const ProtoHEFEdgeConnectionType &edge_connection_type, const ProtoHEFNetworkGroupMetadata &network_group_proto, 
    bool hw_padding_supported, bool transposed, const uint8_t context_index, const uint8_t network_index,
    LayerInfo &layer_info)
{
    auto format_order_exp = HailoRTDefaults::get_device_format_order(base_info.format());
    CHECK_EXPECTED_AS_STATUS(format_order_exp);

    auto format_oder = format_order_exp.release();

    auto layer_type = get_layer_type(edge_connection_type);
    CHECK_EXPECTED_AS_STATUS(layer_type);
    layer_info.type = layer_type.value();

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
    layer_info.nn_stream_config = nn_stream_config.release();
    layer_info.network_index = network_index;
    layer_info.context_index = context_index;

    CHECK(IS_FIT_IN_UINT8(base_info.sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", base_info.sys_index());
    layer_info.stream_index = static_cast<uint8_t>(base_info.sys_index());
    CHECK(IS_FIT_IN_UINT8(base_info.engine_id()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid engine_id: {}.", base_info.engine_id());
    layer_info.dma_engine_index = static_cast<uint8_t>(base_info.engine_id());

    if (HAILO_FORMAT_ORDER_HAILO_NMS == layer_info.format.order) {
        auto expected_nms_info = parse_proto_nms_info(base_info.additional_info().nms_info());
        CHECK_EXPECTED_AS_STATUS(expected_nms_info);
        layer_info.nms_info = expected_nms_info.release();
    }

    layer_info.max_shmifo_size = base_info.max_shmifo_size();

    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_layer_info(const ProtoHEFEdgeLayerInfo &info, 
    const ProtoHEFEdgeConnectionType &edge_connection_type,
    const ProtoHEFCoreOpMock &core_op, hailo_stream_direction_t direction,
    bool hw_padding_supported, const uint8_t context_index, const std::string &partial_network_name, 
    uint8_t network_index, LayerInfo &layer_info)
{
    auto status = fill_layer_info_with_base_info(info.edge_layer_base(), edge_connection_type, core_op.network_group_metadata,
        hw_padding_supported, info.transposed(), context_index, network_index, layer_info);
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

    layer_info.network_name = HefUtils::get_network_name(core_op, partial_network_name);
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

    layer_info.is_defused_nms = core_op.fused_layers_metadata.network_has_fused_layers() &&
        (HAILO_FORMAT_ORDER_HAILO_NMS == layer_info.format.order) && layer_info.nms_info.is_defused;

    if (layer_info.is_defused_nms) {
        for (const auto &fused_layer : core_op.fused_layers_metadata.fused_layers()) {
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
    const ProtoHEFCoreOpMock &core_op, hailo_stream_direction_t direction,
    bool hw_padding_supported, const uint8_t context_index, const std::string &partial_network_name, 
    uint8_t network_index, LayerInfo &layer_info)
{
    const bool transposed = false;
    auto status = fill_layer_info_with_base_info(info.edge_layer_base(), edge_connection_type, core_op.network_group_metadata,
        hw_padding_supported, transposed, context_index, network_index, layer_info);
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

    layer_info.network_name = HefUtils::get_network_name(core_op, partial_network_name);
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
                status = fill_layer_info(info.predecessors(i).layer_info(), edge_connection_type, core_op,
                    direction, hw_padding_supported, context_index, partial_network_name, network_index, temp_layer);
                if (HAILO_SUCCESS != status) {
                    return status;
                }
                layer_info.predecessor.push_back(temp_layer);
                break;
            case ProtoHefEdge::kLayerMux:
                status = fill_mux_info(info.predecessors(i).layer_mux(), edge_connection_type, core_op,
                    direction, hw_padding_supported, context_index, partial_network_name, network_index, temp_layer);
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

hailo_status HefUtils::fill_boundary_layers_info(
    const ProtoHEFCoreOpMock &core_op,
    const uint8_t context_index,
    const ProtoHEFEdgeLayer &layer,
    const SupportedFeatures &supported_features,
    ContextMetadata &context_metadata)
{
    auto layer_info = get_boundary_layer_info(core_op, context_index, layer, supported_features);
    CHECK_EXPECTED_AS_STATUS(layer_info);
    
    context_metadata.add_boundary_layer(layer_info.release());

    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_inter_context_layers_info(
    const ProtoHEFCoreOpMock &core_op,
    const uint8_t context_index,
    const ProtoHEFEdgeLayer &layer,
    const SupportedFeatures &supported_features,
    ContextMetadata &context_metadata)
{
    auto layer_info = get_inter_context_layer_info(core_op, context_index, layer, supported_features);
    CHECK_EXPECTED_AS_STATUS(layer_info);

    context_metadata.add_inter_context_layer(layer_info.release());
    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_ddr_layers_info(
    const ProtoHEFCoreOpMock &core_op,
    const uint8_t context_index,
    const ProtoHEFEdgeLayer &layer,
    const SupportedFeatures &supported_features,
    ContextMetadata &context_metadata)
{
    auto layer_info = get_ddr_layer_info(core_op, context_index, layer, supported_features);
    CHECK_EXPECTED_AS_STATUS(layer_info);

    context_metadata.add_ddr_layer(layer_info.release());
    return HAILO_SUCCESS;
}

hailo_status HefUtils::check_ddr_pairs_match(
    const std::vector<LayerInfo> &context_ddr_input_layers,
    const std::vector<LayerInfo> &context_ddr_output_layers,
    const uint8_t context_index)
{
    CHECK(context_ddr_input_layers.size() == context_ddr_output_layers.size(), HAILO_INVALID_HEF,
        "DDR pairs must be equal in size for context {}" ,context_index);

    for (auto const &ddr_output_layer : context_ddr_output_layers) {
        auto matching_input_stream = ddr_output_layer.connected_context_info.stream_index;
        bool found_mathing_layer = false;
        for (auto const &ddr_input_layer : context_ddr_input_layers) {
            if (ddr_input_layer.stream_index == matching_input_stream) {
                CHECK(!found_mathing_layer, HAILO_INVALID_HEF, "Found multiple input DDR streams for single ddr output stream");
                found_mathing_layer = true;
                CHECK(ddr_output_layer.nn_stream_config.core_bytes_per_buffer == ddr_input_layer.nn_stream_config.core_bytes_per_buffer,
                    HAILO_INVALID_HEF, "both sides for DDR pair must have the same core_bytes_per_buffer.\n"
                    "context index {}.  Output stream index - {} output side core_bytes_per_buffer - {}." 
                    "input stream index {}.input size core_bytes_per_buffer - {}",
                    context_index, ddr_output_layer.stream_index, ddr_output_layer.nn_stream_config.core_bytes_per_buffer, 
                    ddr_input_layer.stream_index, ddr_input_layer.nn_stream_config.core_bytes_per_buffer);
                CHECK(ddr_output_layer.ddr_info.total_buffers_per_frame == ddr_input_layer.ddr_info.total_buffers_per_frame,
                    HAILO_INVALID_HEF, "both sides for DDR pair must have the same total_buffers_per_frame.\n"
                    "context index {}. Output stream index - {} output side total_buffers_per_frame - {}."
                    "input stream index {}. input size total_buffers_per_frame - {}",
                    context_index, ddr_output_layer.stream_index, ddr_output_layer.ddr_info.total_buffers_per_frame, 
                    ddr_input_layer.stream_index, ddr_input_layer.ddr_info.total_buffers_per_frame);
            }
        }
        CHECK(found_mathing_layer, HAILO_INVALID_HEF, "didn't find any match for context {} output stream {}", context_index, ddr_output_layer.stream_index);
    }

    return HAILO_SUCCESS;
}

static Expected<ContextSwitchConfigActionPtr> parse_trigger_action(const ProtoHEFTrigger &trigger_proto)
{
    switch (trigger_proto.trigger_case()) {
    case ProtoHEFTrigger::kTriggerLcu:
    {
        const auto cluster_index = trigger_proto.trigger_lcu().cluster_index();
        const auto lcu_index = trigger_proto.trigger_lcu().lcu_index();
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(cluster_index), HAILO_INVALID_HEF,
            "Failed to parse HEF. Invalid cluster_index: {}.", cluster_index);
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(lcu_index), HAILO_INVALID_HEF,
            "Failed to parse HEF. Invalid lcu_index: {}.", lcu_index);
        return WaitForLcuAction::create(static_cast<uint8_t>(cluster_index), static_cast<uint8_t>(lcu_index));
    }
    case ProtoHEFTrigger::kTriggerAllDataWasSent:
    {
        const auto stream_index = trigger_proto.trigger_all_data_was_sent().shmifo_index();
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(stream_index), HAILO_INVALID_HEF,
            "Failed to parse HEF. Invalid stream_index: {}.", stream_index);
        return  WaitOutputTransferDoneAction::create(static_cast<uint8_t>(stream_index));
    }
    case ProtoHEFTrigger::kTriggerDmaIdle:
    {
        const auto stream_index = trigger_proto.trigger_dma_idle().shmifo_index();
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(stream_index), HAILO_INVALID_HEF,
            "Failed to parse HEF. Invalid stream_index: {}.", stream_index);
        return WaitDmaIdleAction::create(static_cast<uint8_t>(stream_index));
    }
    case ProtoHEFTrigger::kTriggerNms:
    {
        const auto aggregator_index = trigger_proto.trigger_nms().aggregator_index();
        const auto pred_cluster_ob_index = trigger_proto.trigger_nms().pred_cluster_ob_index();
        const auto pred_cluster_ob_cluster_index = trigger_proto.trigger_nms().pred_cluster_ob_cluster_index();
        const auto pred_cluster_ob_interface = trigger_proto.trigger_nms().pred_cluster_ob_interface();
        const auto succ_prepost_ob_index = trigger_proto.trigger_nms().succ_prepost_ob_index();
        const auto succ_prepost_ob_interface = trigger_proto.trigger_nms().succ_prepost_ob_interface();
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

        return WaitNmsIdleAction::create(static_cast<uint8_t>(aggregator_index),
            static_cast<uint8_t>(pred_cluster_ob_index), static_cast<uint8_t>(pred_cluster_ob_cluster_index),
            static_cast<uint8_t>(pred_cluster_ob_interface), static_cast<uint8_t>(succ_prepost_ob_index),
            static_cast<uint8_t>(succ_prepost_ob_interface));
    }
    case ProtoHEFTrigger::kTriggerAllDataWasReceived:
    {
        LOGGER__ERROR("kTriggerAllDataWasReceived trigger is not supported");
        return make_unexpected(HAILO_NOT_IMPLEMENTED);
    }
    case ProtoHEFTrigger::kTriggerNone:
    {
        return NoneAction::create();
    }
    default:
        LOGGER__ERROR("Unsupported trigger given {}", trigger_proto.trigger_case());
        return make_unexpected(HAILO_INVALID_HEF);
    }
}

// Parse initial_l3 register from old hef
constexpr uint32_t HAILO8_INITIAL_L3_CUT_MASK = 0x0000007F;
constexpr uint32_t HAILO8_INITIAL_L3_OFFSET_MASK = 0x0007FF80L;
constexpr uint32_t HAILO8_INITIAL_L3_OFFSET_SHIFT = 7;
constexpr uint32_t HAILO8_INITIAL_L3_OFFSET_BYTES_GRANULARITY_SHIFT = 3;


static std::pair<uint8_t, uint16_t> old_hef_parse_initial_l3(uint32_t initial_l3)
{
    // parse initial l3 as written in hailo8 initial_l3 format -
    //      7 bits of initial_l3_cut
    //      12 bits of initial_l3_offset, offset in 256 bits (8 bytes) granularity.
    const uint8_t initial_l3_cut = static_cast<uint8_t>(initial_l3 & HAILO8_INITIAL_L3_CUT_MASK);
    const uint32_t initial_l3_offset_256 = (initial_l3 & HAILO8_INITIAL_L3_OFFSET_MASK) >> HAILO8_INITIAL_L3_OFFSET_SHIFT;
    const uint16_t initial_l3_offset = static_cast<uint16_t>(initial_l3_offset_256 << HAILO8_INITIAL_L3_OFFSET_BYTES_GRANULARITY_SHIFT);
    return std::make_pair(initial_l3_cut, initial_l3_offset);
}

static Expected<ContextSwitchConfigActionPtr> parse_action(const ProtoHEFAction &proto_action,
    const SupportedFeatures &supported_features)
{
    switch (proto_action.action_case()) {
        case ProtoHEFAction::kWriteDataCcw:\
        {
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.write_data_ccw().cfg_channel_index()), HAILO_INVALID_HEF,
                "Invalid cfg channel index");
            const auto config_stream_index = static_cast<uint8_t>(proto_action.write_data_ccw().cfg_channel_index());

            auto data = Buffer::create(
                reinterpret_cast<const uint8_t*>(proto_action.write_data_ccw().data().data()),
                proto_action.write_data_ccw().data().length());
            CHECK_EXPECTED(data);

            return WriteDataCcwAction::create(data.release(), config_stream_index);
        }
        case ProtoHEFAction::kDisableLcu:
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.disable_lcu().cluster_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid cluster_index: {}.", proto_action.disable_lcu().cluster_index());
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.disable_lcu().lcu_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid lcu_index: {}", proto_action.disable_lcu().lcu_index());
            return DisableLcuAction::create(static_cast<uint8_t>(proto_action.disable_lcu().cluster_index()),
                static_cast<uint8_t>(proto_action.disable_lcu().lcu_index()));
        case ProtoHEFAction::kEnableLcu:
        {
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.enable_lcu().cluster_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid cluster_index: {}.", proto_action.enable_lcu().cluster_index());
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.enable_lcu().lcu_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid lcu_index: {}.", proto_action.enable_lcu().lcu_index());
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(proto_action.enable_lcu().lcu_kernel_done_address()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid lcu_kernel_done_address: {}.", proto_action.enable_lcu().lcu_kernel_done_address());

            auto support_multi_networks = supported_features.multi_network_support;
            auto network_index = static_cast<uint8_t>((support_multi_networks) ? proto_action.enable_lcu().network_index() : 0);

            const auto cluster_index = static_cast<uint8_t>(proto_action.enable_lcu().cluster_index());
            const auto lcu_index = static_cast<uint8_t>(proto_action.enable_lcu().lcu_index());
            const auto kernel_done_address = static_cast<uint16_t>(proto_action.enable_lcu().lcu_kernel_done_address());
            const auto kernel_done_count = static_cast<uint32_t>(proto_action.enable_lcu().lcu_kernel_done_count());

            return EnableLcuAction::create(cluster_index, lcu_index, network_index, kernel_done_address,
                kernel_done_count);
        }
        case ProtoHEFAction::kEnableSequencer:
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

            return EnableSequencerAction::create(
                static_cast<uint8_t>(proto_action.enable_sequencer().cluster_index()),
                initial_l3_cut, initial_l3_offset,
                proto_action.enable_sequencer().active_apu_bitmap(),
                proto_action.enable_sequencer().active_ia_bitmap(),
                proto_action.enable_sequencer().active_sc_bitmap(),
                proto_action.enable_sequencer().active_l2_bitmap(),
                l2_offset_0,
                l2_offset_1);
        }
        case ProtoHEFAction::kNone:
            return NoneAction::create();

        case ProtoHEFAction::kWaitForSeqeuncer:
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.wait_for_seqeuncer().cluster_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid cluster_index: {}.", proto_action.wait_for_seqeuncer().cluster_index());

            return WaitForSequencerAction::create(
                static_cast<uint8_t>(proto_action.wait_for_seqeuncer().cluster_index()));

        case ProtoHEFAction::kAllowInputDataflow:
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.allow_input_dataflow().sys_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid sys_index: {}.", proto_action.allow_input_dataflow().sys_index());
            return AllowInputDataflowAction::create(
                static_cast<uint8_t>(proto_action.allow_input_dataflow().sys_index()));

        case ProtoHEFAction::kWaitForModuleConfigDone:
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.wait_for_module_config_done().index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid index: {}", proto_action.wait_for_module_config_done().index());
            return WaitForModuleConfigDoneAction::create(
                static_cast<uint8_t>(proto_action.wait_for_module_config_done().index()));

        case ProtoHEFAction::kEnableNms:
        {
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.enable_nms().nms_unit_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid nms_unit_index: {}.", proto_action.enable_nms().nms_unit_index());
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.enable_nms().network_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid network_index: {}.", proto_action.enable_nms().network_index());

            auto support_multi_networks = supported_features.multi_network_support;
            auto network_index = static_cast<uint8_t>((support_multi_networks) ? proto_action.enable_nms().network_index() : 0);

            const auto nms_unit_index = static_cast<uint8_t>(proto_action.enable_nms().nms_unit_index());

            return EnableNmsAction::create(network_index, nms_unit_index);
        }

        default:
            LOGGER__ERROR("Action {} not implemented", proto_action.action_case());
            break;
    }

    // Default case
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

static Expected<ContextSwitchOperation> parse_operation(const ProtoHEFOperation &operation_proto,
    const SupportedFeatures &supported_features)
{
    std::vector<ContextSwitchConfigActionPtr> actions;
    actions.reserve(operation_proto.actions_size() + 1); // +1 for the trigger action

    auto trigger_action = parse_trigger_action(operation_proto.trigger());
    CHECK_EXPECTED(trigger_action);
    actions.emplace_back(trigger_action.release());

    actions.reserve(operation_proto.actions_size());
    for (const auto &proto_action : operation_proto.actions()) {
        auto action = parse_action(proto_action, supported_features);
        CHECK_EXPECTED(action);
        actions.emplace_back(action.release());
    }

    return ContextSwitchOperation(std::move(actions));
}

static Expected<std::vector<ContextSwitchOperation>> parse_operations(
    const google::protobuf::RepeatedPtrField<ProtoHEFOperation> &operations_proto,
    const SupportedFeatures &supported_features)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(operations_proto.size()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid operations_count: {}.", operations_proto.size());
    std::vector<ContextSwitchOperation> operations;
    operations.reserve(operations_proto.size());
    for (const auto &operation_proto : operations_proto) {
        auto operation = parse_operation(operation_proto, supported_features);
        CHECK_EXPECTED(operation);
        operations.emplace_back(operation.release());
    }
    return operations;
}

static hailo_status update_parsing_info(uint8_t cfg_index, uint32_t data_length, ConfigBufferInfoMap &results)
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

static Expected<ConfigBufferInfoMap> get_config_buffer_info(
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

Expected<PreliminaryContextMetadata> HefUtils::parse_preliminary_context(const ProtoHEFPreliminaryConfig &preliminary_proto,
    const SupportedFeatures &supported_features)
{
    auto operations = parse_operations(preliminary_proto.operation(), supported_features);
    CHECK_EXPECTED(operations);

    auto config_buffer_infos = get_config_buffer_info(preliminary_proto.operation());
    CHECK_EXPECTED(config_buffer_infos);

    return PreliminaryContextMetadata(operations.release(), config_buffer_infos.release());
}

Expected<ContextMetadata> HefUtils::parse_single_dynamic_context(const ProtoHEFCoreOpMock &core_op,
    const ProtoHEFContext &context_proto, uint8_t context_index, const SupportedFeatures &supported_features)
{
    auto operations = parse_operations(context_proto.operations(), supported_features);
    CHECK_EXPECTED(operations);

    auto config_buffer_infos = get_config_buffer_info(context_proto.operations());
    CHECK_EXPECTED(config_buffer_infos);

    ContextMetadata context_metadata(operations.release(), config_buffer_infos.release());

    for (const auto &edge_layer : context_proto.metadata().edge_layers()) { 
        if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY ==
                edge_layer.context_switch_info().edge_connection_type()) {
            auto status = fill_boundary_layers_info(core_op, context_index, edge_layer,
                supported_features, context_metadata);
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__INTERMEDIATE ==
                edge_layer.context_switch_info().edge_connection_type()) {
            auto status = fill_inter_context_layers_info(core_op, context_index, edge_layer,
                supported_features, context_metadata);
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__DDR ==
                edge_layer.context_switch_info().edge_connection_type()) {
            auto status = fill_ddr_layers_info(core_op, context_index, edge_layer,
                supported_features, context_metadata);
            CHECK_SUCCESS_AS_EXPECTED(status);
        }
    }

    auto status = check_ddr_pairs_match(context_metadata.get_ddr_input_layers(), context_metadata.get_ddr_output_layers(),
        context_index);
    CHECK_SUCCESS_AS_EXPECTED(status);


    return context_metadata;
}

static hailo_status validate_unique_boundary_names(const std::vector<ContextMetadata> &contexts_metadata)
{
    std::unordered_set<std::string> names;
    for (const auto &context_metadata : contexts_metadata) {
        for (const auto &layer_info : context_metadata.get_boundary_input_layers()) {
            CHECK(names.find(layer_info.name) == names.end(), HAILO_INVALID_HEF,
                "Layer name should be unique. name '{}' appears more than once", layer_info.name);
            names.insert(layer_info.name);
        }

        for (const auto &layer_info : context_metadata.get_boundary_output_layers()) {
            CHECK(names.find(layer_info.name) == names.end(), HAILO_INVALID_HEF,
                "Layer name should be unique. name '{}' appears more than once", layer_info.name);
            names.insert(layer_info.name);
        }
    }
    return HAILO_SUCCESS;
}

Expected<std::vector<ContextMetadata>> HefUtils::parse_dynamic_contexts(const ProtoHEFCoreOpMock &core_op, const SupportedFeatures &supported_features)
{
    std::vector<ContextMetadata> contexts_metadata;
    for (uint8_t context_index = 0; context_index < core_op.contexts.size(); context_index++) {
        auto &context_proto = core_op.contexts[context_index];
        auto context_metadata = parse_single_dynamic_context(core_op, context_proto, context_index, supported_features);
        CHECK_EXPECTED(context_metadata);
        contexts_metadata.emplace_back(context_metadata.release());
    }

    const auto status = validate_unique_boundary_names(contexts_metadata);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return contexts_metadata;
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

Expected<LayerInfo> HefUtils::get_boundary_layer_info(const ProtoHEFCoreOpMock &core_op,
    const uint8_t context_index, const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features)
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
    auto partial_network_name = HefUtils::get_partial_network_name_by_index(core_op, network_index, supported_features);
    CHECK_EXPECTED(partial_network_name);
    const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(layer);
    if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type()) {
        // TODO: return LayerInfo
        auto status = fill_layer_info(layer.layer_info(), layer.context_switch_info().edge_connection_type(),
            core_op, direction, hw_padding_supported, context_index, partial_network_name.value(), network_index, result);
        CHECK_SUCCESS_AS_EXPECTED(status);
    } else if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__MUX == layer.edge_layer_type()) {
        // TODO: return LayerInfo
        auto status = fill_mux_info(layer.layer_mux(), layer.context_switch_info().edge_connection_type(), 
            core_op, direction, hw_padding_supported, context_index, partial_network_name.value(), network_index, result);
        CHECK_SUCCESS_AS_EXPECTED(status);
    } else {
        LOGGER__ERROR("Invalid layer type");
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }

    result.direction = (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST ==
            layer.direction()) ? HAILO_D2H_STREAM : HAILO_H2D_STREAM;

    if (layer.has_pad_index()) {
        result.pad_index = layer.pad_index();
    }

    return result;
}

static Expected<ConnectedContextInfo> parse_connected_context_info(
    const ProtoHEFConnectedContextInfo &connected_context_proto)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(connected_context_proto.sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid connected_sys_index: {}.", connected_context_proto.sys_index());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(connected_context_proto.engine_id()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid engine_id: {}. in connected_contexts", connected_context_proto.engine_id());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(connected_context_proto.index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid connected_context_index: {}.", connected_context_proto.index());

    ConnectedContextInfo connected_context{};
    connected_context.context_index = static_cast<uint8_t>(connected_context_proto.index());
    connected_context.stream_index = static_cast<uint8_t>(connected_context_proto.sys_index());
    connected_context.dma_engine_index = static_cast<uint8_t>(connected_context_proto.engine_id());
    return connected_context;
}

Expected<LayerInfo> HefUtils::get_inter_context_layer_info(const ProtoHEFCoreOpMock &core_op,
    const uint8_t context_index, const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features)
{
    LayerInfo result = {};
    CHECK_AS_EXPECTED(PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type(), HAILO_INVALID_HEF, "Inter-context layer can't be mux.");

    result.type = LayerType::INTER_CONTEXT;
    auto support_multi_networks = supported_features.multi_network_support;
    result.network_index = static_cast<uint8_t>((support_multi_networks) ? layer.network_index() : 0);
    auto partial_network_name = HefUtils::get_partial_network_name_by_index(core_op, result.network_index, supported_features);
    CHECK_EXPECTED(partial_network_name);    
    result.network_name = HefUtils::get_network_name(core_op, partial_network_name.release());
    result.context_index = context_index;
    const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(layer);
    result.name = layer.layer_info().name();
    auto nn_stream_config_exp = HefConfigurator::parse_nn_stream_config(layer.layer_info().edge_layer_base(), 
        hw_padding_supported, layer.context_switch_info().edge_connection_type());
    CHECK_EXPECTED(nn_stream_config_exp);
    result.nn_stream_config = nn_stream_config_exp.release();
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", layer.layer_info().edge_layer_base().sys_index());
    result.stream_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().sys_index());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().engine_id()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid engine_id: {}.", layer.layer_info().edge_layer_base().engine_id());
    result.dma_engine_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().engine_id());

    result.max_shmifo_size = layer.layer_info().edge_layer_base().max_shmifo_size();

    result.direction = (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST ==
            layer.direction()) ? HAILO_D2H_STREAM : HAILO_H2D_STREAM;

    // HRT-7201 - The system supports one src and multiple dstinations. Right now we're saving only one dstination
    CHECK_AS_EXPECTED(layer.context_switch_info().connected_contexts_size() >= 1, HAILO_INVALID_HEF,
        "Inter context layer info must contain connected_context");
    auto connected_context = parse_connected_context_info(layer.context_switch_info().connected_contexts(0));
    CHECK_EXPECTED(connected_context);
    result.connected_context_info = connected_context.release();

    return result;
}

Expected<LayerInfo> HefUtils::get_ddr_layer_info(const ProtoHEFCoreOpMock &core_op,
    const uint8_t context_index, const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features)
{
    LayerInfo result = {};
    CHECK_AS_EXPECTED(PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type(), HAILO_INVALID_HEF, "DDR layer can't be mux.");

    result.type = LayerType::DDR;

    auto support_multi_networks = supported_features.multi_network_support;
    result.network_index = static_cast<uint8_t>((support_multi_networks) ? layer.network_index() : 0);
    auto partial_network_name = HefUtils::get_partial_network_name_by_index(core_op, result.network_index, supported_features);
    CHECK_EXPECTED(partial_network_name);
    result.network_name = HefUtils::get_network_name(core_op, partial_network_name.release());
    result.context_index = context_index;
    const bool hw_padding_supported = HefConfigurator::is_hw_padding_supported(layer);
    result.name = layer.layer_info().name();
    auto nn_stream_config_exp = HefConfigurator::parse_nn_stream_config(layer.layer_info().edge_layer_base(), 
        hw_padding_supported, layer.context_switch_info().edge_connection_type());
    CHECK_EXPECTED(nn_stream_config_exp);
    result.nn_stream_config = nn_stream_config_exp.release();
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", layer.layer_info().edge_layer_base().sys_index());
    result.stream_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().sys_index());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().engine_id()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid engine_id: {}.", layer.layer_info().edge_layer_base().engine_id());
    result.dma_engine_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().engine_id());
    result.max_shmifo_size = layer.layer_info().edge_layer_base().max_shmifo_size();

    CHECK_AS_EXPECTED(layer.context_switch_info().connected_contexts_size() == 1, HAILO_INVALID_HEF,
        "Only single connected context is supported on DDR channels");
    auto connected_context = parse_connected_context_info(layer.context_switch_info().connected_contexts(0));
    CHECK_EXPECTED(connected_context);
    CHECK_AS_EXPECTED(context_index == connected_context->context_index,
        HAILO_INVALID_HEF, "for ddr layer, connected_context_index must be same to the edge layer's context");
    result.connected_context_info = connected_context.release();

    result.direction = (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST ==
            layer.direction()) ? HAILO_D2H_STREAM : HAILO_H2D_STREAM;

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(layer.layer_info().edge_layer_base().core_buffers_per_frame()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid core_buffers_per_frame: {}.", layer.layer_info().edge_layer_base().core_buffers_per_frame());
    result.ddr_info.total_buffers_per_frame = static_cast<uint16_t>(layer.layer_info().edge_layer_base().core_buffers_per_frame());

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(layer.context_switch_info().buffers()), HAILO_INVALID_HEF, 
        "calculated number of transfers for DDR buffer is out of UINT16_T range");
    result.ddr_info.min_buffered_rows = static_cast<uint16_t>(layer.context_switch_info().buffers());

    return result;
}

Expected<std::vector<std::string>> HefUtils::get_sorted_output_names(const ProtoHEFCoreOpMock &core_op)
{
    if (core_op.fused_layers_metadata.network_has_fused_layers()) {
        return std::vector<std::string>(std::begin(core_op.fused_layers_metadata.updated_sorted_output_names()),
            std::end(core_op.fused_layers_metadata.updated_sorted_output_names()));
    } else if (0 != core_op.sorted_outputs_order.size()) {
        // For backwards compatibility before we've added updated_sorted_output_names
        return std::vector<std::string>(std::begin(core_op.sorted_outputs_order),
            std::end(core_op.sorted_outputs_order));
    } else {
        // For backwards compatibility before we've added this field
        uint32_t number_of_contexts = core_op.contexts.size();
        const auto& context_metadata = core_op.contexts[number_of_contexts - 1].metadata();

        CHECK_AS_EXPECTED(0 < context_metadata.sorted_outputs_order_size(), HAILO_INVALID_HEF,
            "Sorted output names is not set up in the HEF.");

        return std::vector<std::string>(std::begin(context_metadata.sorted_outputs_order()),
            std::end(context_metadata.sorted_outputs_order()));
    }
}

Expected<std::string> HefUtils::get_partial_network_name_by_index(const ProtoHEFCoreOpMock &core_op, uint8_t network_index,
    const SupportedFeatures &supported_features)
{
    if (supported_features.multi_network_support) {
        CHECK_AS_EXPECTED(network_index < core_op.networks_names.size(), HAILO_INVALID_ARGUMENT,
            "Requested name for network_index={}, however there are only {} networks in the network group",
            network_index, core_op.networks_names.size());
        return std::string(core_op.networks_names[network_index]);
    } else {
        auto partial_network_name = core_op.network_group_metadata.network_group_name();
        return partial_network_name;
    }
}

std::string HefUtils::get_network_group_name(const ProtoHEFNetworkGroup &net_group, const SupportedFeatures &/*supported_features*/)
{
    if (!net_group.partial_network_groups().empty()) {
        return net_group.partial_network_groups(0).network_group().network_group_metadata().network_group_name();
    }
    return net_group.network_group_metadata().network_group_name();
}

std::string HefUtils::get_network_name(const std::string &net_group_name, const std::string &partial_network_name)
{
    return net_group_name + HAILO_DEFAULT_NETWORK_NAME_QUALIFIER + partial_network_name;
}

std::string HefUtils::get_network_name(const ProtoHEFCoreOpMock &core_op, const std::string &partial_network_name)
{
    return HefUtils::get_network_name(core_op.network_group_metadata.network_group_name(), partial_network_name);
}

Expected<std::shared_ptr<ProtoHEFCoreOpMock>> Hef::Impl::get_core_op_per_arch(const ProtoHEFCoreOpMock &core_op,
    ProtoHEFHwArch hef_arch, hailo_device_architecture_t device_arch, uint32_t partial_clusters_layout_bitmap)
{
    if (ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L == hef_arch) {
        // Hailo8 can work with Hailo8L configurations. in that case we choose one of the configurations
        for (auto &partial_core_op : core_op.partial_core_ops) {
            if (partial_clusters_layout_bitmap == partial_core_op->layout.partial_clusters_layout_bitmap()
                    || (HAILO_ARCH_HAILO8 == device_arch)) {
                return std::make_shared<ProtoHEFCoreOpMock>(*(partial_core_op->core_op));
            }
        }
        LOGGER__ERROR("There is no matching partial_clusters_layout_bitmap configuration in the given HEF");
        return make_unexpected(HAILO_INVALID_HEF);
    } else {
        return std::make_shared<ProtoHEFCoreOpMock>(core_op);
    }
}

Expected<std::vector<std::string>> Hef::Impl::get_sorted_output_names(const std::string &net_group_name)
{
    if (m_supported_features.hailo_net_flow) {
        std::vector<std::string> res;
        for (const auto &net_group : m_groups) {
            auto curr_name = HefUtils::get_network_group_name(*net_group, m_supported_features);
            if (curr_name == net_group_name) {
                res.reserve(net_group->sorted_outputs_order().size());
                for (auto &name : net_group->sorted_outputs_order()) {
                    res.push_back(name);
                }
                return res;
            }
        }
        LOGGER__ERROR("Did not find network group of name {}", net_group_name);
        return make_unexpected(HAILO_INVALID_HEF);
    }
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
    auto core_op = get_core_op_by_net_group_name(net_group_name);
    CHECK_EXPECTED(core_op);
    return core_op.value()->network_group_metadata.bottleneck_fps();
}

bool Hef::Impl::contains_ddr_layers(const ProtoHEFCoreOpMock& core_op)
{
    for (auto &context : core_op.contexts) {
        for (auto &layer : context.metadata().edge_layers()) {
            if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__DDR ==
                layer.context_switch_info().edge_connection_type()) {
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

Expected<std::vector<std::string>> Hef::Impl::get_vstream_names_from_stream_name(const std::string &stream_name,
    const std::string &net_group_name)
{
    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    return network_group_metadata->get_vstream_names_from_stream_name(stream_name);
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
    auto core_op = get_core_op_by_net_group_name(net_group_name);
    CHECK_EXPECTED(core_op);

    std::string results;

    for (const auto &context : core_op.value()->contexts) {
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
    auto copre_op = get_core_op_by_net_group_name(net_group_name);
    CHECK_EXPECTED(copre_op);

    std::vector<std::string> results;

    for (const auto &context : copre_op.value()->contexts) {
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

hailo_status Hef::Impl::validate_core_op_unique_layer_names(const ProtoHEFCoreOpMock &core_op)
{
    std::set<std::string> edge_layer_names;
    std::string layer_name;
    for (auto &context : core_op.contexts) {
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

std::string Hef::hash() const
{
    const auto &md5 = pimpl->md5();
    const bool LOWERCASE = false;
    return StringUtils::to_hex_string(md5, MD5_DIGEST_LENGTH, LOWERCASE);
}

std::vector<std::string> Hef::Impl::get_network_groups_names()
{
    std::vector<std::string> results;
    results.reserve(m_groups.size());

    for (const auto &net_group : m_groups) {
        auto &network_group_name = (ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L == get_device_arch()) ?
            net_group->partial_network_groups(0).network_group().network_group_metadata().network_group_name()
            : net_group->network_group_metadata().network_group_name();
        results.push_back(network_group_name);
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
    results.reserve(m_core_ops_per_group.size());

    for (const auto &group_name_to_core_op : m_core_ops_per_group) {
        const auto &core_op = group_name_to_core_op.second[0];
        hailo_network_group_info_t info = {};
        auto &network_group_name = (ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L == get_device_arch()) ?
            core_op.partial_core_ops[0]->core_op->network_group_metadata.network_group_name()
            : core_op.network_group_metadata.network_group_name();
        CHECK_AS_EXPECTED(HAILO_MAX_NETWORK_GROUP_NAME_SIZE >= (network_group_name.length() + 1), HAILO_INTERNAL_FAILURE,
            "The network group '{}' has a too long name (max is HAILO_MAX_NETWORK_GROUP_NAME_SIZE)", network_group_name);
        strncpy(info.name, network_group_name.c_str(), network_group_name.length() + 1);
        info.is_multi_context = (1 < core_op.contexts.size());
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
    for (auto &input_layer : network_group_metadata->get_input_layer_infos()) {
        auto params = HailoRTDefaults::get_stream_parameters(stream_interface, HAILO_H2D_STREAM);
        CHECK_EXPECTED(params);
        results.emplace(std::make_pair(input_layer.name, params.release()));
    }
    for (auto &output_layer : network_group_metadata->get_output_layer_infos()) {
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
    auto core_op = get_core_op_by_net_group_name(net_group_name);
    CHECK_EXPECTED(core_op);

    auto network_group_metadata = get_network_group_metadata(net_group_name);
    CHECK_EXPECTED(network_group_metadata);

    std::map<std::string, hailo_network_parameters_t> results;

    if (network_group_metadata->supported_features().multi_network_support) {
        CHECK_AS_EXPECTED((core_op.value()->networks_names.size() != 0), HAILO_INTERNAL_FAILURE, 
        "Hef support multiple networks, but no networks found in the proto");
        for (const auto &partial_network_name : core_op.value()->networks_names) {
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
    for (auto &input_layer : network_group_metadata->get_input_layer_infos()) {
        hailo_stream_parameters_t params = {};
        params.direction = HAILO_H2D_STREAM;
        params.stream_interface = HAILO_STREAM_INTERFACE_MIPI;
        params.mipi_input_params = mipi_params;
        results.emplace(std::make_pair(input_layer.name, params));
    }
    for (auto &output_layer : network_group_metadata->get_output_layer_infos()) {
        auto params = HailoRTDefaults::get_stream_parameters(output_interface, HAILO_D2H_STREAM);
        CHECK_EXPECTED(params);
        results.emplace(std::make_pair(output_layer.name, params.release()));
    }

    return results;
}

} /* namespace hailort */
