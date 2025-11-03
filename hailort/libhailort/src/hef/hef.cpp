/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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
#include "hailo/stream.hpp"
#include "hailo/device.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/hailort_defaults.hpp"
#include "hailo/quantization.hpp"

#include "common/utils.hpp"
#include "common/utils.hpp"
#include "common/logger_macros.hpp"

#include "net_flow/ops/nms_post_process.hpp"
#include "net_flow/ops/yolov5_post_process.hpp"
#include "net_flow/ops/yolov5_bbox_only_post_process.hpp"
#include "net_flow/ops/yolox_post_process.hpp"
#include "net_flow/ops/ssd_post_process.hpp"
#include "net_flow/ops/argmax_post_process.hpp"
#include "net_flow/ops/softmax_post_process.hpp"
#include "net_flow/ops/yolov5_seg_post_process.hpp"
#include "net_flow/ops/yolov8_post_process.hpp"
#include "net_flow/ops/yolov8_bbox_only_post_process.hpp"
#include "hef/hef_internal.hpp"
#include "vdma/pcie/pcie_device.hpp"
#include "vdma/vdma_config_manager.hpp"
#include "hef/layer_info.hpp"
#include "device_common/control.hpp"
#include "utils/profiler/tracer_macros.hpp"

#include "byte_order.h"
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
#include <google/protobuf/io/zero_copy_stream_impl.h>


namespace hailort
{

#define HEF__MD5_BUFFER_SIZE (1024)
#define DEFAULT_BATCH_SIZE (1)
#define SKIP_SPACE_COMMA_CHARACTERS (2)
#define ALIGNED_TO_4_BYTES (4)
#define MIN_SLEEP_TIME_USEC (1000)
constexpr uint8_t DEFAULT_DIVISION_FACTOR = 1;

#define TAB ("    ")

static std::string add_tabs(uint8_t count)
{
    // Each TAB counts as 4 spaces
    std::string res = "";
    for (uint8_t i = 0; i < count; i++) {
        res = res + TAB;
    }
    return res;
}

static std::string get_shape_str(const hailo_stream_info_t &stream_info)
{
    switch (stream_info.format.order)
    {
    case HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP:
        return HailoRTCommon::get_format_type_str(stream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(stream_info.format.order) +
            "(maximum frame size: " + std::to_string(HailoRTCommon::get_nms_hw_frame_size(stream_info.nms_info)) + ")";

    case HAILO_FORMAT_ORDER_NC:
        return HailoRTCommon::get_format_type_str(stream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(stream_info.format.order) +
            "(" + std::to_string(stream_info.hw_shape.features) + ")";
    case HAILO_FORMAT_ORDER_NHW:
        return HailoRTCommon::get_format_type_str(stream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(stream_info.format.order) +
            "(" + std::to_string(stream_info.hw_shape.height) + "x" + std::to_string(stream_info.hw_shape.width) + ")";
    default:
        return HailoRTCommon::get_format_type_str(stream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(stream_info.format.order) +
            "(" + std::to_string(stream_info.hw_shape.height) + "x" + std::to_string(stream_info.hw_shape.width) +
            "x" + std::to_string(stream_info.hw_shape.features) + ")";
    }
}

static std::string get_shape_str(const hailo_vstream_info_t &vstream_info)
{
    switch (vstream_info.format.order)
    {
    case HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS:
        return HailoRTCommon::get_format_type_str(vstream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(vstream_info.format.order) +
            "(number of classes: " + std::to_string(vstream_info.nms_shape.number_of_classes) +
            ", maximum bounding boxes per class: " + std::to_string(vstream_info.nms_shape.max_bboxes_per_class) +
            ", maximum frame size: " + std::to_string(HailoRTCommon::get_nms_host_frame_size(vstream_info.nms_shape, vstream_info.format)) + ")";
    case HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE:
    case HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK:
        return HailoRTCommon::get_format_type_str(vstream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(vstream_info.format.order) +
            "(number of classes: " + std::to_string(vstream_info.nms_shape.number_of_classes) +
            ", maximum bounding boxes total: " + std::to_string(vstream_info.nms_shape.max_bboxes_total) +
            ", maximum frame size: " + std::to_string(HailoRTCommon::get_nms_host_frame_size(vstream_info.nms_shape, vstream_info.format)) + ")";
    case HAILO_FORMAT_ORDER_NC:
        return HailoRTCommon::get_format_type_str(vstream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(vstream_info.format.order) +
            "(" + std::to_string(vstream_info.shape.features) + ")";
    case HAILO_FORMAT_ORDER_NHW:
        return HailoRTCommon::get_format_type_str(vstream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(vstream_info.format.order) +
            "(" +std::to_string(vstream_info.shape.height) + "x" + std::to_string(vstream_info.shape.width) + ")";
    default:
        return HailoRTCommon::get_format_type_str(vstream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(vstream_info.format.order) +
            "(" + std::to_string(vstream_info.shape.height) + "x" + std::to_string(vstream_info.shape.width) + "x" +
            std::to_string(vstream_info.shape.features) + ")";
    }
}

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
    TRY(auto impl, Hef::Impl::create(hef_path));

    auto impl_ptr = make_shared_nothrow<Impl>(std::move(impl));
    CHECK_NOT_NULL_AS_EXPECTED(impl_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return Hef(std::move(impl_ptr));
}

Expected<Hef> Hef::create(const MemoryView &hef_buffer)
{
    TRY(auto hef_shared_buffer, Buffer::create_shared(hef_buffer.data(), hef_buffer.size(),
        BufferStorageParams::create_dma()));
    
    TRY(auto impl, Hef::Impl::create(hef_shared_buffer));

    auto impl_ptr = make_shared_nothrow<Impl>(std::move(impl));
    CHECK_NOT_NULL_AS_EXPECTED(impl_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return Hef(std::move(impl_ptr));
}

Expected<Hef> Hef::create(std::shared_ptr<Buffer> hef_buffer)
{
    TRY(auto impl, Hef::Impl::create(hef_buffer));

    auto impl_ptr = make_shared_nothrow<Impl>(std::move(impl));
    CHECK_NOT_NULL_AS_EXPECTED(impl_ptr, HAILO_OUT_OF_HOST_MEMORY);
    return Hef(std::move(impl_ptr));
}

Hef::Hef(std::shared_ptr<Impl> pimpl) :
    pimpl(std::move(pimpl))
{}

Expected<std::vector<hailo_stream_info_t>> Hef::get_input_stream_infos(const std::string &name) const
{
    TRY(const auto network_pair, pimpl->get_network_group_and_network_name(name));
    return pimpl->get_input_stream_infos(network_pair.first, network_pair.second);
}

Expected<std::vector<hailo_stream_info_t>> Hef::get_output_stream_infos(const std::string &name) const
{
    TRY(const auto network_pair, pimpl->get_network_group_and_network_name(name));
    return pimpl->get_output_stream_infos(network_pair.first, network_pair.second);
}

Expected<std::vector<hailo_stream_info_t>> Hef::get_all_stream_infos(const std::string &name) const
{
    TRY(const auto network_pair, pimpl->get_network_group_and_network_name(name));
    return pimpl->get_all_stream_infos(network_pair.first, network_pair.second);
}

Expected<std::vector<hailo_network_info_t>> Hef::get_network_infos(const std::string &net_group_name) const
{
    TRY(const auto names_pair, pimpl->get_network_group_and_network_name(net_group_name));
    return pimpl->get_network_infos(names_pair.first);
}

Expected<hailo_stream_info_t> Hef::get_stream_info_by_name(const std::string &stream_name,
    hailo_stream_direction_t stream_direction, const std::string &net_group_name) const
{
    // Addressing the situation where net_group_name == ""
    TRY(const auto net_group_name_pair, pimpl->get_network_group_and_network_name(net_group_name));
    const auto &net_group_name_str = net_group_name_pair.first;

    return pimpl->get_stream_info_by_name(stream_name, stream_direction, net_group_name_str);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::get_input_vstream_infos(const std::string &name) const
{
    TRY(const auto network_pair, pimpl->get_network_group_and_network_name(name));
    return pimpl->get_input_vstream_infos(network_pair.first, network_pair.second);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::get_output_vstream_infos(const std::string &name) const
{
    TRY(const auto network_pair, pimpl->get_network_group_and_network_name(name));
    return pimpl->get_output_vstream_infos(network_pair.first, network_pair.second);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::get_all_vstream_infos(const std::string &name) const
{
    TRY(const auto network_pair, pimpl->get_network_group_and_network_name(name));
    return pimpl->get_all_vstream_infos(network_pair.first, network_pair.second);
}

Expected<std::vector<std::string>> Hef::get_sorted_output_names(const std::string &net_group_name) const
{
    // Addressing the situation where net_group_name == ""
    TRY(const auto net_group_name_pair, pimpl->get_network_group_and_network_name(net_group_name));
    const auto &net_group_name_str = net_group_name_pair.first;
    return pimpl->get_sorted_output_names(net_group_name_str);
}

Expected<size_t> Hef::get_number_of_input_streams(const std::string &net_group_name) const
{
    // Addressing the situation where net_group_name == ""
    TRY(const auto net_group_name_pair, pimpl->get_network_group_and_network_name(net_group_name));
    const auto &net_group_name_str = net_group_name_pair.first;
    return pimpl->get_number_of_input_streams(net_group_name_str);
}

Expected<size_t> Hef::get_number_of_output_streams(const std::string &net_group_name) const
{
    // Addressing the situation where net_group_name == ""
    TRY(const auto net_group_name_pair, pimpl->get_network_group_and_network_name(net_group_name));
    const auto &net_group_name_str = net_group_name_pair.first;
    return pimpl->get_number_of_output_streams(net_group_name_str);
}

Expected<float64_t> Hef::get_bottleneck_fps(const std::string &net_group_name) const
{
    return pimpl->get_bottleneck_fps(net_group_name);
}


Expected<hailo_device_architecture_t> Hef::get_hef_device_arch() const
{
    TRY(auto compatible_archs, get_compatible_device_archs());
    return Expected<hailo_device_architecture_t>{compatible_archs.at(0)};
}

Expected<std::vector<hailo_device_architecture_t>> Hef::get_compatible_device_archs() const
{
    return DeviceBase::hef_arch_to_device_compatible_archs(static_cast<HEFHwArch>(pimpl->get_device_arch()));
}

Expected<std::string> Hef::device_arch_to_string(const hailo_device_architecture_t arch)
{
    return HailoRTCommon::get_device_arch_str(arch);
}

Expected<std::string> Hef::get_vstream_name_from_original_name(const std::string &original_name,
    const std::string &net_group_name) const
{
    return pimpl->get_vstream_name_from_original_name(original_name, net_group_name);
}

Expected<std::vector<std::string>> Hef::get_original_names_from_vstream_name(const std::string &stream_name,
    const std::string &net_group_name) const
{
    return pimpl->get_original_names_from_vstream_name(stream_name, net_group_name);
}

Expected<std::vector<std::string>> Hef::get_stream_names_from_vstream_name(const std::string &vstream_name,
    const std::string &net_group_name) const
{
    TRY(const auto network_group_name_pair, pimpl->get_network_group_and_network_name(net_group_name));
    const auto &net_group_name_str = network_group_name_pair.first;
    return pimpl->get_stream_names_from_vstream_name(vstream_name, net_group_name_str);
}

Expected<std::vector<std::string>> Hef::get_vstream_names_from_stream_name(const std::string &stream_name,
    const std::string &net_group_name) const
{
    TRY(const auto network_group_name_pair, pimpl->get_network_group_and_network_name(net_group_name));
    const auto &net_group_name_str = network_group_name_pair.first;
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

Expected<Hef::Impl> Hef::Impl::create(std::shared_ptr<Buffer> hef_buffer)
{
    hailo_status status = HAILO_UNINITIALIZED;

    Impl hef(hef_buffer, status);
    if (HAILO_SUCCESS != status) {
        LOGGER__ERROR("Failed creating HEF");
        return make_unexpected(status);
    }

    return hef;
}

Expected<size_t> calc_hef_residue_size(std::shared_ptr<SeekableBytesReader> hef_reader, uint32_t version)
{
    TRY(auto total_size, hef_reader->get_size());
    switch (version) {
    case HEADER_VERSION_0:
        return total_size - HEF_HEADER_SIZE_V0;
    case HEADER_VERSION_1:
        return total_size - HEF_HEADER_SIZE_V1;
    case HEADER_VERSION_2:
        return total_size - HEF_HEADER_SIZE_V2;
    case HEADER_VERSION_3:
        return total_size - HEF_HEADER_SIZE_V3;
    default:
        LOGGER__ERROR("Unsupported hef version {}", version);
        return make_unexpected(HAILO_HEF_NOT_SUPPORTED);
    }
}

static hailo_status calc_buffer_md5(const uint8_t *buffer, const size_t buffer_size, MD5_SUM_t &calculated_md5)
{
    MD5_CTX md5 = {};
    MD5_Init(&md5);
    MD5_Update(&md5, buffer, buffer_size);
    MD5_Final(calculated_md5, &md5);

    return HAILO_SUCCESS;
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
        MD5_Update(&md5, &md5_buffer, s.gcount());
    }
    MD5_Final(calculated_md5, &md5);
    s.clear();
    s.seekg(beg_pos, s.beg);
    CHECK(s.good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::seekg() failed");
    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::validate_hef_header(const hef__header_t &header, MD5_SUM_t &calculated_md5, size_t hef_file_residue_size)
{
    CHECK(HEADER_MAGIC == header.magic, HAILO_HEF_NOT_SUPPORTED,
        "HEF magic does not match. detected magic - {:x}", header.magic);

    CHECK((HEADER_VERSION_0 == header.version) , HAILO_INTERNAL_FAILURE,
        "HEF version does not match. Should be {} but detected {}", HEADER_VERSION_0, header.version);

    CHECK(hef_file_residue_size == header.hef_proto_size, HAILO_HEF_FILE_CORRUPTED,
        "HEF file length does not match");

    CHECK(0 == memcmp(&calculated_md5, &header.distinct.v0.expected_md5, sizeof(MD5_SUM_t)), HAILO_HEF_FILE_CORRUPTED,
        "HEF md5 does not match");

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::validate_hef_header(const hef__header_t &header, const uint32_t &crc_32, size_t hef_file_residue_size)
{
    CHECK(HEADER_MAGIC == header.magic, HAILO_HEF_NOT_SUPPORTED,
        "HEF magic does not match. Should be {:x} but detected magic - {:x}", HEADER_MAGIC, header.magic);

    CHECK((HEADER_VERSION_1 == header.version), HAILO_INTERNAL_FAILURE,
        "HEF version does not match. Should be {} but detected {}", HEADER_VERSION_1, header.version);

    CHECK(hef_file_residue_size == header.hef_proto_size + header.distinct.v1.ccws_size, HAILO_HEF_FILE_CORRUPTED,
        "HEF file length does not match");

    CHECK(0 == memcmp(&crc_32, &header.distinct.v1.crc, sizeof(crc_32)), HAILO_HEF_FILE_CORRUPTED,
        "HEF crc does not match");

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::validate_hef_header(const hef__header_t &header, const uint64_t &calculated_xxh3_64bits, size_t hef_file_residue_size)
{
    CHECK(HEADER_MAGIC == header.magic, HAILO_HEF_NOT_SUPPORTED,
        "HEF magic does not match. Should be {:x} but detected magic - {:x}", HEADER_MAGIC, header.magic);

    uint64_t non_proto_size = 0;
    uint64_t xxh3_64bits_from_hef = 0;
    if (HEADER_VERSION_2 == header.version) {
        non_proto_size = header.distinct.v2.ccws_size;
        xxh3_64bits_from_hef = header.distinct.v2.xxh3_64bits;
    } else if (HEADER_VERSION_3 == header.version) {
        non_proto_size = header.distinct.v3.ccws_size_with_padding + header.distinct.v3.additional_info_size;
        xxh3_64bits_from_hef = header.distinct.v3.xxh3_64bits;
    } else {
        LOGGER__ERROR("Invalid HEF version");
        return HAILO_HEF_NOT_SUPPORTED;
    }
    CHECK(hef_file_residue_size == header.hef_proto_size + non_proto_size, HAILO_HEF_FILE_CORRUPTED,
       "HEF file length does not match");

    CHECK(0 == memcmp(&calculated_xxh3_64bits, &xxh3_64bits_from_hef, sizeof(calculated_xxh3_64bits)), HAILO_HEF_FILE_CORRUPTED,
        "HEF xxhash does not match, calculated: {}, expected: {}", calculated_xxh3_64bits, xxh3_64bits_from_hef);

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

    CHECK(unsupported_extensions.empty(), HAILO_HEF_NOT_SUPPORTED, "Failed opening non-compatible HEF with the following unsupported extensions: {}",
        std::accumulate(std::next(unsupported_extensions.begin()), unsupported_extensions.end(), unsupported_extensions[0], 
        [] (std::string a, std::string b) { return std::move(a) + ", " + b; }));

    CHECK_AS_EXPECTED(m_supported_features.periph_calculation_in_hailort, HAILO_HEF_NOT_SUPPORTED,
        "Hef has periph_calculation_in_hailort feature disabled - this HEF is outdated and no longer supported. Please update HEF");

    return HAILO_SUCCESS;
}

void Hef::Impl::init_md5(MD5_SUM_t &calculated_md5)
{
    memcpy(m_md5, calculated_md5, sizeof(m_md5));
}

void Hef::Impl::init_crc(uint32_t crc_32)
{
    memcpy(&m_crc, &crc_32, sizeof(crc_32));
}

void Hef::Impl::init_hef_version(uint32_t version)
{
    m_hef_version = version;
}

Expected<hef__header_t> Hef::Impl::parse_hef_header_before_distinct(std::shared_ptr<SeekableBytesReader> hef_reader)
{
    hef__header_t hef_header = {};
    auto status = hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header), HEF_COMMON_SIZE);
    CHECK_SUCCESS_AS_EXPECTED(status);

    hef_header.magic = BYTE_ORDER__htonl(hef_header.magic);
    hef_header.version = BYTE_ORDER__htonl(hef_header.version);
    hef_header.hef_proto_size = BYTE_ORDER__htonl(hef_header.hef_proto_size);

    return hef_header;
}

hailo_status Hef::Impl::fill_v1_hef_header(hef__header_t &hef_header, std::shared_ptr<SeekableBytesReader> hef_reader)
{
    auto status = hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header.distinct), sizeof(hef__header_distinct_t::v1));
    CHECK_SUCCESS(status);

    hef_header.distinct.v1.ccws_size = BYTE_ORDER__htonll(hef_header.distinct.v1.ccws_size);
    hef_header.distinct.v1.crc = BYTE_ORDER__htonl(hef_header.distinct.v1.crc);

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::fill_v2_hef_header(hef__header_t &hef_header, std::shared_ptr<SeekableBytesReader> hef_reader)
{
    auto status = hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header.distinct), sizeof(hef__header_distinct_t::v2));
    CHECK_SUCCESS(status);

    hef_header.distinct.v2.ccws_size = BYTE_ORDER__htonll(hef_header.distinct.v2.ccws_size);
    hef_header.distinct.v2.xxh3_64bits = BYTE_ORDER__htonll(hef_header.distinct.v2.xxh3_64bits);

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::fill_v3_hef_header(hef__header_t &hef_header, std::shared_ptr<SeekableBytesReader> hef_reader)
{
    auto status = hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header.distinct), sizeof(hef__header_distinct_t::v3));
    CHECK_SUCCESS(status);

    hef_header.distinct.v3.ccws_size_with_padding = BYTE_ORDER__htonll(hef_header.distinct.v3.ccws_size_with_padding);
    hef_header.distinct.v3.xxh3_64bits = BYTE_ORDER__htonll(hef_header.distinct.v3.xxh3_64bits);
    hef_header.distinct.v3.hef_padding_size = BYTE_ORDER__htonl(hef_header.distinct.v3.hef_padding_size);
    hef_header.distinct.v3.additional_info_size = BYTE_ORDER__htonll(hef_header.distinct.v3.additional_info_size);
    hef_header.distinct.v3.proto_xxh3_64bits = BYTE_ORDER__htonll(hef_header.distinct.v3.proto_xxh3_64bits);

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::fill_core_ops_and_networks_metadata(uint32_t hef_version, std::shared_ptr<SeekableBytesReader> hef_reader, size_t ccws_offset)
{
    fill_core_ops();

    auto status = fill_networks_metadata(hef_version, hef_reader, ccws_offset);
    CHECK_SUCCESS(status);

    // Must be called after fill_networks_metadata
    status = validate_hef_extensions();
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::parse_hef_file(const std::string &hef_path)
{
    TRY(auto hef_reader, SeekableBytesReader::create_reader(hef_path));
    auto status = hef_reader->open();
    CHECK_SUCCESS(status);
    m_hef_reader = hef_reader;
    TRY(auto hef_header, parse_hef_header_before_distinct(hef_reader));
    init_hef_version(hef_header.version);
    m_offset_zero_point = 0; // Not relevant for HEADER_VERSION_0
    switch (hef_header.version) {
    case HEADER_VERSION_0: {
        status = hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header.distinct), sizeof(hef__header_distinct_t::v0));
        CHECK_SUCCESS(status);
        MD5_SUM_t calculated_md5 = {};
        status = calc_istream_md5(*hef_reader->get_fstream(), calculated_md5);
        CHECK_SUCCESS(status);
        TRY(const auto hef_file_residue_size, hef_reader->calculate_remaining_size());
        status = validate_hef_header(hef_header, calculated_md5, hef_file_residue_size);
        CHECK_SUCCESS(status);
        init_md5(calculated_md5);
        break;
    }
    case HEADER_VERSION_1: {
        status = fill_v1_hef_header(hef_header, hef_reader);
        CHECK_SUCCESS(status);
        m_offset_zero_point = HEF_HEADER_SIZE_V1 + hef_header.hef_proto_size;
        TRY(auto calculated_residue_size, calc_hef_residue_size(hef_reader, hef_header.version));
        TRY(auto calculated_crc, CRC32::calc_crc_on_stream(hef_reader->get_fstream(), calculated_residue_size));
        status = validate_hef_header(hef_header, calculated_crc, calculated_residue_size);
        CHECK_SUCCESS(status);
        init_crc(calculated_crc);
        break;
    }
    case HEADER_VERSION_2: {
        status = fill_v2_hef_header(hef_header, hef_reader);
        CHECK_SUCCESS(status);
        m_offset_zero_point = HEF_HEADER_SIZE_V2 + hef_header.hef_proto_size;
        TRY(auto calculated_residue_size, calc_hef_residue_size(hef_reader, hef_header.version));
        TRY(auto calculated_xxh3_64bits, Xxhash::calc_xxh3_on_stream(hef_reader->get_fstream(), calculated_residue_size));
        status = validate_hef_header(hef_header, calculated_xxh3_64bits, calculated_residue_size);
        CHECK_SUCCESS(status);
        m_xxh3_64bits = calculated_xxh3_64bits;
        break;
    }
    case HEADER_VERSION_3: {
        status = fill_v3_hef_header(hef_header, hef_reader);
        CHECK_SUCCESS(status);
        m_ccws_section_size = hef_header.distinct.v3.ccws_size_with_padding - hef_header.distinct.v3.hef_padding_size;
        m_offset_zero_point = HEF_HEADER_SIZE_V3 + hef_header.hef_proto_size + hef_header.distinct.v3.hef_padding_size;
        if (0 != hef_header.distinct.v3.proto_xxh3_64bits) {
            // If proto_xxh3_64bits is populated check only it, and let the rest of the HEF be validated later (CCW - on FW, external resources - hef parsing)
            TRY(auto hef_proto_checksum, Xxhash::calc_xxh3_on_stream(hef_reader->get_fstream(), hef_header.hef_proto_size));
            CHECK(hef_header.distinct.v3.proto_xxh3_64bits == hef_proto_checksum, HAILO_HEF_FILE_CORRUPTED, "HEF proto xxhash does not match");
            m_xxh3_64bits = hef_header.distinct.v3.xxh3_64bits;
        } else {
            TRY(auto calculated_residue_size, calc_hef_residue_size(hef_reader, hef_header.version));
            TRY(auto calculated_xxh3_64bits, Xxhash::calc_xxh3_on_stream(hef_reader->get_fstream(), calculated_residue_size));
            status = validate_hef_header(hef_header, calculated_xxh3_64bits, calculated_residue_size);
            CHECK_SUCCESS(status);
            m_xxh3_64bits = calculated_xxh3_64bits;
        }
        break;
    }
    default:
        LOGGER__ERROR("Unsupported hef version {}", hef_header.version);
        return HAILO_HEF_NOT_SUPPORTED;
    }
    ProtoHEFHef hef_message;
    google::protobuf::io::IstreamInputStream zero_copy_input(hef_reader->get_fstream().get());
    auto rb = hef_message.ParseFromBoundedZeroCopyStream(&zero_copy_input, hef_header.hef_proto_size); // This line corrupts the file
    CHECK(rb, HAILO_HEF_FILE_CORRUPTED, "Failed parsing HEF file");
    hef_reader->get_fstream()->clear(); // The call to ParseFromBoundedZeroCopyStream might corrupt the file, so we need to clear it's error flags
    // TODO: Remove this reset after stopping support for V0 (in the new format (V1), the file is not corrupted after parsing the protobuf message).
    status = transfer_protobuf_field_ownership(hef_message);
    CHECK_SUCCESS(status);
    status = fill_core_ops_and_networks_metadata(hef_header.version, hef_reader, m_offset_zero_point);
    CHECK_SUCCESS(status);

    status = hef_reader->close();
    CHECK_SUCCESS(status);
    TRACE(HefLoadedTrace, hef_path, m_header.sdk_version_str(), m_md5);
    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::parse_hef_memview_internal(const size_t proto_size, const uint8_t *proto_buffer, const uint32_t hef_version,
    std::shared_ptr<SeekableBytesReader> hef_reader, size_t ccws_offset)
{
    ProtoHEFHef hef_message;
    auto rb = hef_message.ParseFromArray(proto_buffer, static_cast<int>(proto_size));
    CHECK(rb, HAILO_HEF_FILE_CORRUPTED, "Failed parsing HEF buffer");
    auto status = transfer_protobuf_field_ownership(hef_message);
    CHECK_SUCCESS(status);

    status = fill_core_ops_and_networks_metadata(hef_version, hef_reader, ccws_offset);
    CHECK_SUCCESS(status);

    return HAILO_SUCCESS;
}

hailo_status Hef::Impl::parse_hef_memview(const MemoryView &hef_memview)
{
    TRY(auto hef_reader, SeekableBytesReader::create_reader(hef_memview));
    m_hef_reader = hef_reader;

    TRY(auto hef_header, parse_hef_header_before_distinct(hef_reader));
    init_hef_version(hef_header.version);

    CHECK(hef_memview.size() >= sizeof(hef__header_t), HAILO_HEF_FILE_CORRUPTED, "Invalid HEF header");

    m_offset_zero_point = 0; // Not relevant for HEADER_VERSION_0

    switch (hef_header.version) {
    case HEADER_VERSION_0: {
        auto status = hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header.distinct), sizeof(hef__header_distinct_t::v0));
        CHECK_SUCCESS(status);

        auto proto_buffer = (hef_memview.data() + HEF_HEADER_SIZE_V0);
        auto proto_size = (hef_memview.size() - HEF_HEADER_SIZE_V0);

        MD5_SUM_t calculated_md5 = {};
        status = calc_buffer_md5(proto_buffer, proto_size, calculated_md5);
        CHECK_SUCCESS(status);

        status = validate_hef_header(hef_header, calculated_md5, proto_size);
        CHECK_SUCCESS(status);

        init_md5(calculated_md5);

        return parse_hef_memview_internal(proto_size, proto_buffer, hef_header.version, hef_reader, m_offset_zero_point);
    }
    case HEADER_VERSION_1: {
        auto status = fill_v1_hef_header(hef_header, hef_reader);
        CHECK_SUCCESS(status);

        auto proto_and_ccw_buffer = hef_memview.data() + HEF_HEADER_SIZE_V1;
        auto proto_size = hef_memview.size() - HEF_HEADER_SIZE_V1 - hef_header.distinct.v1.ccws_size;

        m_offset_zero_point = HEF_HEADER_SIZE_V1 + hef_header.hef_proto_size;

        TRY(auto proto_and_ccws_size, calc_hef_residue_size(hef_reader, hef_header.version));
        auto proto_and_ccws_buffer = MemoryView::create_const(hef_memview.data() + HEF_HEADER_SIZE_V1, proto_and_ccws_size);
        TRY(auto calculated_crc, CRC32::calc_crc_on_buffer(proto_and_ccws_buffer));

        status = validate_hef_header(hef_header, calculated_crc, proto_and_ccws_size);
        CHECK_SUCCESS(status);

        init_crc(calculated_crc);

        return parse_hef_memview_internal(static_cast<size_t>(proto_size), proto_and_ccw_buffer, hef_header.version, hef_reader, m_offset_zero_point);
    }
    case HEADER_VERSION_2: {
        auto status = fill_v2_hef_header(hef_header, hef_reader);
        CHECK_SUCCESS(status);

        auto proto_and_ccw_buffer = hef_memview.data() + HEF_HEADER_SIZE_V2;
        auto proto_size = hef_memview.size() - HEF_HEADER_SIZE_V2 - hef_header.distinct.v2.ccws_size;

        m_offset_zero_point = HEF_HEADER_SIZE_V2 + hef_header.hef_proto_size;

        TRY(auto proto_and_ccws_size, calc_hef_residue_size(hef_reader, hef_header.version));
        auto proto_and_ccws_buffer = MemoryView::create_const(hef_memview.data() + HEF_HEADER_SIZE_V2, proto_and_ccws_size);
        TRY(auto calculated_xxh3_64bits, Xxhash::calc_xxh3_on_buffer(proto_and_ccws_buffer));

        status = validate_hef_header(hef_header, calculated_xxh3_64bits, proto_and_ccws_size);
        CHECK_SUCCESS(status);
        m_xxh3_64bits = calculated_xxh3_64bits;

        return parse_hef_memview_internal(static_cast<size_t>(proto_size), proto_and_ccw_buffer, hef_header.version, hef_reader, m_offset_zero_point);
    }
    case HEADER_VERSION_3:
    {
        auto status = fill_v3_hef_header(hef_header, hef_reader);
        CHECK_SUCCESS(status);

        auto proto_and_ccw_buffer = hef_memview.data() + HEF_HEADER_SIZE_V3;
        auto proto_size = hef_header.hef_proto_size;

        CHECK(hef_header.distinct.v3.ccws_size_with_padding >= hef_header.distinct.v3.hef_padding_size, HAILO_HEF_FILE_CORRUPTED,
            "Invalid HEF - ccws size is smaller than padding size");
        m_ccws_section_size = hef_header.distinct.v3.ccws_size_with_padding - hef_header.distinct.v3.hef_padding_size;
        m_offset_zero_point = HEF_HEADER_SIZE_V3 + hef_header.hef_proto_size + hef_header.distinct.v3.hef_padding_size;

        if (0 != hef_header.distinct.v3.proto_xxh3_64bits) {
            // If proto_xxh3_64bits is populated check only it, and let the rest of the HEF be validated later (CCW - on FW, external resources - hef parsing)
            auto proto_buffer = MemoryView::create_const(hef_memview.data() + HEF_HEADER_SIZE_V3, hef_header.hef_proto_size);
            TRY(auto hef_proto_checksum, Xxhash::calc_xxh3_on_buffer(proto_buffer));
            CHECK(hef_header.distinct.v3.proto_xxh3_64bits == hef_proto_checksum, HAILO_HEF_FILE_CORRUPTED, "HEF proto xxhash does not match");
            m_xxh3_64bits = hef_header.distinct.v3.xxh3_64bits;
        } else {
            TRY(auto proto_and_ccws_size, calc_hef_residue_size(hef_reader, hef_header.version));
            auto proto_and_ccws_buffer = MemoryView::create_const(hef_memview.data() + HEF_HEADER_SIZE_V3, proto_and_ccws_size);
            TRY(auto calculated_xxh3_64bits, Xxhash::calc_xxh3_on_buffer(proto_and_ccws_buffer));

            status = validate_hef_header(hef_header, calculated_xxh3_64bits, proto_and_ccws_size);
            CHECK_SUCCESS(status);
            m_xxh3_64bits = calculated_xxh3_64bits;
        }
        return parse_hef_memview_internal(static_cast<size_t>(proto_size), proto_and_ccw_buffer, hef_header.version, hef_reader, m_offset_zero_point);
    }
    default:
        LOGGER__ERROR("Unsupported hef version {}", hef_header.version);
        return HAILO_HEF_NOT_SUPPORTED;
    }
}


bool is_multi_layout(const ProtoHEFHwArch &hw_arch) {
    return (hw_arch == ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L) || (hw_arch == ProtoHEFHwArch::PROTO__HW_ARCH__HAILO15M);
}

hailo_status Hef::Impl::fill_networks_metadata(uint32_t hef_version, std::shared_ptr<SeekableBytesReader> hef_reader, size_t ccws_offset)
{
    fill_extensions_bitset();

    CoreOpMetadataPerArch core_op_metadata;
    uint32_t partial_clusters_layout_bitmap = 0;

    for (auto &network_group : m_groups) {
        // Prepare core_op_metadata
        auto network_group_name = HefUtils::get_network_group_name(*network_group, m_supported_features);
        // TODO: keep metadata per core_op (HRT-9551)
        const auto &core_ops = m_core_ops_per_group[network_group_name];
        assert(core_ops.size() == 1);
        const auto &core_op = core_ops[0];

        // TODO: Clean this code after hef.proto refactor
        std::vector<std::string> sorted_network_names;
        if (m_supported_features.multi_network_support) {
            if (0 != network_group->networks_names_size()) {
                sorted_network_names.reserve(core_op.networks_names.size());
                for (auto &partial_network_name : core_op.networks_names) {
                    auto network_name = HefUtils::get_network_name(network_group_name, partial_network_name);
                    sorted_network_names.push_back(network_name);
                }
            } else if (0 != network_group->partial_network_groups_size()) {
                sorted_network_names.reserve(network_group->partial_network_groups().begin()->network_group().networks_names_size());
                for (auto &partial_network_name : network_group->partial_network_groups().begin()->network_group().networks_names()) {
                    auto network_name = HefUtils::get_network_name(network_group_name, partial_network_name);
                    sorted_network_names.push_back(network_name);
                }
            }
        }
        if (sorted_network_names.empty()) {
            sorted_network_names.push_back(HailoRTDefaults::get_network_name(network_group_name));
        }

        if (is_multi_layout(get_device_arch())) {
            if (m_supported_features.hailo_net_flow) {
                for (auto &partial_core_op : core_op.partial_core_ops) {
                    partial_clusters_layout_bitmap = partial_core_op->layout.partial_clusters_layout_bitmap();
                    TRY(const auto metadata_per_arch,
                        create_metadata_per_arch(*(partial_core_op->core_op), sorted_network_names, hef_version, hef_reader, ccws_offset));

                    TRY(const auto ops_metadata, create_ops_metadata(*network_group, *metadata_per_arch));
                    m_post_process_ops_metadata_per_group.insert({metadata_per_arch->core_op_name(), ops_metadata});
                    core_op_metadata.add_metadata(metadata_per_arch, partial_clusters_layout_bitmap);
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

                    TRY(const auto metadata_per_arch, create_metadata_per_arch(partial_core_op, sorted_network_names, hef_version, hef_reader, ccws_offset));

                    std::vector<net_flow::PostProcessOpMetadataPtr> empty_metadata_ops;
                    m_post_process_ops_metadata_per_group.insert({metadata_per_arch->core_op_name(), empty_metadata_ops});
                    core_op_metadata.add_metadata(metadata_per_arch, partial_clusters_layout_bitmap);
                }
            }
        } else {
            partial_clusters_layout_bitmap = PARTIAL_CLUSTERS_LAYOUT_IGNORE;
            TRY(const auto metadata_per_arch, create_metadata_per_arch(core_op, sorted_network_names, hef_version, hef_reader, ccws_offset));

            TRY(auto ops_metadata, create_ops_metadata(*network_group, *metadata_per_arch));
            m_post_process_ops_metadata_per_group.insert({metadata_per_arch->core_op_name(), ops_metadata});
            core_op_metadata.add_metadata(metadata_per_arch, partial_clusters_layout_bitmap);
        }

        // Taking the full-layout's name (name is same across all layouts)
        TRY(const auto metadata, core_op_metadata.get_metadata(PARTIAL_CLUSTERS_LAYOUT_IGNORE));
        auto core_op_name = metadata->core_op_name();
        std::map<std::string, CoreOpMetadataPerArch> core_op_metadata_map;
        core_op_metadata_map[core_op_name] = core_op_metadata;
        // Prepare network_group_metadata
        CHECK(!contains(m_network_group_metadata, network_group_name),
            HAILO_INVALID_OPERATION, "Network group with the name {} is already configured on the device", network_group_name);

        // TODO: Clean this code after hef.proto refactor
        std::vector<std::string> sorted_output_names;
        if (core_op.fused_layers_metadata.network_has_fused_layers()) {
            // If the model has fused layers, updated sorted_output_names is under the fused layer metadata
            for (auto &name : core_op.fused_layers_metadata.updated_sorted_output_names()) {
                sorted_output_names.push_back(name);
            }
        } else if(!m_supported_features.hailo_net_flow && (0 != network_group->partial_network_groups_size()) &&
            (network_group->partial_network_groups().begin()->network_group().sorted_outputs_order_size())) {
            // If the model doesnt support net_flow, its possible that sorted output names will be under the partial_network_groups metadata
            for (auto &name : network_group->partial_network_groups().begin()->network_group().sorted_outputs_order()) {
                sorted_output_names.push_back(name);
            }
        } else if (0 != network_group->sorted_outputs_order_size()) {
            // Most cases should fall here - either net_flow is supported, or network_group->sorted_outputs_order() has values
            const auto &names = IS_PP_DISABLED() ? network_group->fused_layers_metadata().updated_sorted_output_names() :
                network_group->sorted_outputs_order();
            for (const auto &name : names) {
                sorted_output_names.push_back(name);
            }
        } else {
            // For very old HEFs, sorted_output_names might be in the last context's metadata
            uint32_t number_of_contexts = core_op.contexts.size();
            const auto& context_metadata = core_op.contexts[number_of_contexts - 1].metadata();
            CHECK(0 < context_metadata.sorted_outputs_order_size(), HAILO_INVALID_HEF,
                "Sorted output names is not set up in the HEF.");
            for (auto &name : context_metadata.sorted_outputs_order()) {
                sorted_output_names.push_back(name);
            }
        }

        std::vector<net_flow::PostProcessOpMetadataPtr> empty_ops_metadata;
        auto &ops_metadata = IS_PP_DISABLED() ? empty_ops_metadata : m_post_process_ops_metadata_per_group.at(network_group_name);
        TRY(auto network_group_metadata, NetworkGroupMetadata::create(network_group_name, std::move(core_op_metadata_map),
            sorted_output_names, m_supported_features, sorted_network_names, ops_metadata));
        m_network_group_metadata.emplace(network_group_name, std::move(network_group_metadata));
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

Expected<CoreOpMetadataPtr> Hef::Impl::create_metadata_per_arch(const ProtoHEFCoreOpMock &core_op, const std::vector<std::string> &sorted_network_names,
    uint32_t hef_version, std::shared_ptr<SeekableBytesReader> hef_reader, size_t ccws_offset)
{
    // TODO: validate that there's a read+write layer for each cache + no cache_id is only read or written without the
    //       other. They can be across different contexts (HRT-13655)
    TRY(auto preliminary_context, HefUtils::parse_preliminary_context(core_op.preliminary_config, m_supported_features, hef_version, hef_reader, ccws_offset));
    TRY_V(auto dynamic_contexts, HefUtils::parse_dynamic_contexts(core_op, m_supported_features, get_device_arch(), hef_version, hef_reader, ccws_offset));
    TRY(auto config_channels_info,  parse_config_channels_info(core_op));

    // If const input layer is found in the preliminary context, or first dynamic context we can't use fast batch switch
    const auto can_fast_batch_switch =
        !(preliminary_context.const_input_layer_found() || dynamic_contexts[0].const_input_layer_found());

    // Currently, CoreOp name is the same as network_group_name, thats why we init it with it.
    // TODO: HRT-9551 - Change it when supporting multi core ops.
    auto metadata_per_arch = make_shared_nothrow<CoreOpMetadata>(core_op.network_group_metadata.network_group_name(),
        std::move(preliminary_context), std::move(dynamic_contexts), std::move(config_channels_info),
        m_supported_features, sorted_network_names, can_fast_batch_switch);
    CHECK_NOT_NULL_AS_EXPECTED(metadata_per_arch, HAILO_OUT_OF_HOST_MEMORY);
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

    for (const auto &external_resouce : hef_message.external_resources()) {
        ExternalResourceInfo external_resource_info{external_resouce.name(), external_resouce.size(), external_resouce.offset(), external_resouce.xxhash()};
        m_hef_external_resources.emplace(external_resouce.name(), external_resource_info);
    }

    return HAILO_SUCCESS;
}

Expected<std::shared_ptr<Buffer>> Hef::Impl::get_hef_as_buffer()
{
    if (m_hef_buffer) {
        auto ptr = m_hef_buffer;
        return ptr;
    }

    auto hef_reader = get_hef_reader();
    CHECK_SUCCESS_AS_EXPECTED(hef_reader->open());
    TRY(auto size, hef_reader->get_size());
    TRY(auto buffer_ptr, Buffer::create_shared(size, BufferStorageParams::create_dma()));

    CHECK_SUCCESS(hef_reader->read(buffer_ptr->data(), size));
    CHECK_SUCCESS(hef_reader->close());
    return buffer_ptr;
}

Hef::Impl::Impl(const std::string &hef_path, hailo_status &status) : m_zero_copy_config_over_descs(false)
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

Hef::Impl::Impl(std::shared_ptr<Buffer> hef_buffer, hailo_status &status) : m_zero_copy_config_over_descs(false)
{
    status = HAILO_UNINITIALIZED;
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    m_hef_buffer = hef_buffer;

    status = parse_hef_memview(MemoryView(*m_hef_buffer));
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
    supported_features.dual_direction_stream_index = check_hef_extension(ProtoHEFExtensionType::DUAL_DIRECTION_STREAM_INDEX,
        header, hef_extensions, included_features);
    supported_features.nms_burst_mode = check_hef_extension(ProtoHEFExtensionType::NMS_OUTPUT_BURST,
        header, hef_extensions, included_features);
    supported_features.output_scale_by_feature = check_hef_extension(ProtoHEFExtensionType::OUTPUT_SCALE_PER_FEATURE,
        header, hef_extensions, included_features);
    supported_features.periph_calculation_in_hailort = check_hef_extension(ProtoHEFExtensionType::PERIPH_CALCULATION_IN_HAILORT,
        header, hef_extensions, included_features);
    supported_features.core_hw_padding_config_in_dfc = check_hef_optional_extension(ProtoHEFExtensionType::HW_PADDING,
        header, hef_optional_extensions);
    supported_features.batch_register_config = check_hef_extension(ProtoHEFExtensionType::BATCH_REGISTER_CONFIG,
        header, hef_extensions, included_features);
    supported_features.shared_config = check_hef_extension(ProtoHEFExtensionType::SHARED_CONFIG,
        header, hef_extensions, included_features);
    supported_features.strict_versioning = check_hef_extension(ProtoHEFExtensionType::STRICT_RUNTIME_VERSIONING,
        header, hef_extensions, included_features);
    supported_features.split_allow_input_action = check_hef_extension(ProtoHEFExtensionType::ENABLE_CONFIG_CHANNELS,
        header, hef_extensions, included_features);

    return supported_features;
}

net_flow::NmsPostProcessConfig create_post_process_nms_config(const ProtoHEFOp &op_proto)
{
    net_flow::NmsPostProcessConfig nms_config{};
    nms_config.nms_score_th = (float32_t)op_proto.nms_op().nms_score_th();
    nms_config.nms_iou_th = (float32_t)op_proto.nms_op().nms_iou_th();
    nms_config.max_proposals_per_class = op_proto.nms_op().max_proposals_per_class();
    nms_config.number_of_classes = op_proto.nms_op().classes();
    nms_config.max_proposals_total = nms_config.max_proposals_per_class * nms_config.number_of_classes;
    nms_config.background_removal = op_proto.nms_op().background_removal();
    nms_config.background_removal_index = op_proto.nms_op().background_removal_index();
    nms_config.bbox_only = op_proto.nms_op().bbox_decoding_only();

    return nms_config;
}

Expected<net_flow::YoloPostProcessConfig> create_yolov5_config(const google::protobuf::RepeatedPtrField<ProtoHEFYoloBboxDecoder> &bbox_decoders,
    double image_height, double image_width, const std::map<size_t, LayerInfo> &pad_index_to_streams_info)
{
    net_flow::YoloPostProcessConfig yolo_config{};
    yolo_config.image_height = static_cast<float32_t>(image_height);
    yolo_config.image_width = static_cast<float32_t>(image_width);
    for (auto &bbox_proto : bbox_decoders) {
        std::vector<int> bbox_anchors;
        CHECK_AS_EXPECTED((bbox_proto.h().size() == bbox_proto.w().size()), HAILO_INVALID_HEF,
            "YOLOv5 height anchors count {} doesn't mach the width anchors count {}", bbox_proto.h().size(), bbox_proto.w().size());
        for (int i = 0; i < bbox_proto.h().size(); ++i) {
            bbox_anchors.push_back(bbox_proto.w()[i]);
            bbox_anchors.push_back(bbox_proto.h()[i]);
        }
        assert(contains(pad_index_to_streams_info, static_cast<size_t>(bbox_proto.pad_index())));
        yolo_config.anchors.insert({pad_index_to_streams_info.at(bbox_proto.pad_index()).name, bbox_anchors});
    }

    return yolo_config;
}

Expected<std::unordered_map<std::string, net_flow::BufferMetaData>> create_inputs_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads)
{
    std::unordered_map<std::string, net_flow::BufferMetaData> inputs_metadata;
    for (auto &input_pad : op_proto.input_pads()) {
        CHECK_AS_EXPECTED(contains(input_to_output_pads, static_cast<size_t>(input_pad.index())), HAILO_INVALID_HEF,
            "NMS op is not connected to core op");
        auto output_pad_index = input_to_output_pads.at(input_pad.index());
        CHECK_AS_EXPECTED(contains(pad_index_to_streams_info, output_pad_index), HAILO_INVALID_HEF,
            "Pad {} of post-process {} is not connected to any core output stream",
                input_pad.index(), op_proto.name());
        const auto &op_input_stream = pad_index_to_streams_info.at(output_pad_index);
        net_flow::BufferMetaData input_metadata{};
        input_metadata.format = op_input_stream.format;
        input_metadata.quant_info = op_input_stream.quant_info;
        input_metadata.shape = op_input_stream.shape;
        input_metadata.padded_shape = op_input_stream.hw_shape;
        inputs_metadata.insert({op_input_stream.name, input_metadata});
    }

    return inputs_metadata;
}

uint32_t compute_num_of_proposals(const std::unordered_map<std::string, net_flow::BufferMetaData> &inputs_metadatas, std::map<std::string, 
    std::vector<int>> &anchors)
{
    uint32_t num_of_proposals = 0;
    for (const auto &input_metadata_pair : inputs_metadatas) {
        auto &name = input_metadata_pair.first;
        auto &input_metadata = input_metadata_pair.second;
        assert(contains(anchors, name));
        auto &layer_anchors = anchors.at(name);
        auto num_of_anchors = net_flow::YOLOv5PostProcessOp::get_num_of_anchors(layer_anchors);
        num_of_proposals += static_cast<uint32_t>(num_of_anchors * input_metadata.shape.height * input_metadata.shape.width);
    }
    return num_of_proposals;
}

Expected<net_flow::PostProcessOpMetadataPtr> create_yolov5_op_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads,
    const std::string &network_name)
{
    auto nms_config = create_post_process_nms_config(op_proto);

    TRY(auto yolo_config, create_yolov5_config(op_proto.nms_op().yolo_nms_op().bbox_decoders(),
        op_proto.nms_op().yolo_nms_op().image_height(), op_proto.nms_op().yolo_nms_op().image_width(), pad_index_to_streams_info));
    TRY(auto inputs_metadata, create_inputs_metadata(op_proto, pad_index_to_streams_info, input_to_output_pads));

    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    net_flow::BufferMetaData output_metadata{};
    output_metadata.format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(
        { HAILO_FORMAT_TYPE_AUTO, HAILO_FORMAT_ORDER_AUTO, HAILO_FORMAT_FLAGS_NONE }, net_flow::OperationType::YOLOV5,
        nms_config.bbox_only);

    outputs_metadata.insert({op_proto.output_pads()[0].name(), output_metadata});

    return net_flow::Yolov5OpMetadata::create(inputs_metadata, outputs_metadata, nms_config, yolo_config,
        network_name);
}

Expected<net_flow::PostProcessOpMetadataPtr> create_yolov5_bbox_only_op_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads,
    const std::string &network_name)
{
    auto nms_config = create_post_process_nms_config(op_proto);

    TRY(auto yolo_v5_config, create_yolov5_config(op_proto.nms_op().yolo_nms_op().bbox_decoders(),
        op_proto.nms_op().yolo_nms_op().image_height(), op_proto.nms_op().yolo_nms_op().image_width(), pad_index_to_streams_info));
    TRY(auto inputs_metadata, create_inputs_metadata(op_proto, pad_index_to_streams_info, input_to_output_pads));

    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    net_flow::BufferMetaData output_metadata{};
    uint32_t num_of_proposals = compute_num_of_proposals(inputs_metadata, yolo_v5_config.anchors);
    output_metadata.shape = {1, num_of_proposals, YOLOV5_BBOX_NUM_OF_VALUES + op_proto.nms_op().classes()};

    output_metadata.format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(
        { HAILO_FORMAT_TYPE_AUTO, HAILO_FORMAT_ORDER_AUTO, HAILO_FORMAT_FLAGS_NONE }, net_flow::OperationType::YOLOV5, nms_config.bbox_only);
    outputs_metadata.insert({op_proto.output_pads()[0].name(), output_metadata});

    return net_flow::Yolov5BboxOnlyOpMetadata::create(inputs_metadata, outputs_metadata, nms_config, yolo_v5_config,
        network_name);
}

Expected<net_flow::PostProcessOpMetadataPtr> create_yolov5_seg_op_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads,
    const std::string &network_name)
{
    auto nms_config = create_post_process_nms_config(op_proto);

    TRY(auto yolov5_config, create_yolov5_config(op_proto.nms_op().yolo_seg_op().bbox_decoders(),
        op_proto.nms_op().yolo_seg_op().image_height(), op_proto.nms_op().yolo_seg_op().image_width(), pad_index_to_streams_info));
    TRY(auto inputs_metadata, create_inputs_metadata(op_proto, pad_index_to_streams_info, input_to_output_pads));

    auto proto_layer_name = op_proto.nms_op().yolo_seg_op().proto_info().proto_layer();
    CHECK_AS_EXPECTED(contains(inputs_metadata, proto_layer_name), HAILO_INVALID_HEF);

    const uint32_t SIZE_FACTOR = 2;
    net_flow::YoloV5SegPostProcessConfig yolov5_seg_config = {};
    yolov5_seg_config.mask_threshold = static_cast<float32_t>(op_proto.nms_op().yolo_seg_op().mask_threshold());
    yolov5_seg_config.max_accumulated_mask_size = static_cast<uint32_t>(
        yolov5_config.image_height * yolov5_config.image_width * SIZE_FACTOR);
    yolov5_seg_config.proto_layer_name = proto_layer_name;

    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    net_flow::BufferMetaData output_metadata{};
    output_metadata.format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type
        ({ HAILO_FORMAT_TYPE_AUTO, HAILO_FORMAT_ORDER_HAILO_NMS_WITH_BYTE_MASK, HAILO_FORMAT_FLAGS_NONE },
        net_flow::OperationType::YOLOV5SEG, nms_config.bbox_only);
    outputs_metadata.insert({op_proto.output_pads()[0].name(), output_metadata});

    return net_flow::Yolov5SegOpMetadata::create(inputs_metadata, outputs_metadata, nms_config, yolov5_config,
        yolov5_seg_config, network_name);
}

Expected<net_flow::Yolov8PostProcessConfig> create_yolov8_config(const ProtoHEFOp &op_proto, const std::map<size_t, LayerInfo> &pad_index_to_streams_info)
{
    net_flow::Yolov8PostProcessConfig yolov8_config{};
    yolov8_config.image_height = (float32_t)op_proto.nms_op().yolov8_nms_op().image_height();
    yolov8_config.image_width = (float32_t)op_proto.nms_op().yolov8_nms_op().image_width();

    for (auto &bbox_proto : op_proto.nms_op().yolov8_nms_op().bbox_decoders()) {
        assert(contains(pad_index_to_streams_info, static_cast<size_t>(bbox_proto.reg_pad_index())));
        auto reg_name = pad_index_to_streams_info.at(bbox_proto.reg_pad_index()).name;
        assert(contains(pad_index_to_streams_info, static_cast<size_t>(bbox_proto.cls_pad_index())));
        auto cls_name = pad_index_to_streams_info.at(bbox_proto.cls_pad_index()).name;
        yolov8_config.reg_to_cls_inputs.emplace_back(net_flow::Yolov8MatchingLayersNames{reg_name, cls_name, bbox_proto.stride()});
    }
    return yolov8_config;
}

Expected<net_flow::PostProcessOpMetadataPtr> create_yolov8_op_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads,
    const std::string &network_name)
{
    auto nms_config = create_post_process_nms_config(op_proto);
    TRY(auto yolov8_config, create_yolov8_config(op_proto, pad_index_to_streams_info));

    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    net_flow::BufferMetaData output_metadata{};
    output_metadata.format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(
        { HAILO_FORMAT_TYPE_AUTO, HAILO_FORMAT_ORDER_AUTO, HAILO_FORMAT_FLAGS_NONE }, net_flow::OperationType::YOLOV8, nms_config.bbox_only);
    outputs_metadata.insert({op_proto.output_pads()[0].name(), output_metadata});

    TRY(auto inputs_metadata, create_inputs_metadata(op_proto, pad_index_to_streams_info, input_to_output_pads));

    return net_flow::Yolov8OpMetadata::create(inputs_metadata, outputs_metadata, nms_config, yolov8_config,
        network_name);
}

uint32_t compute_yolov8_bbox_only_num_of_proposals(const std::unordered_map<std::string, net_flow::BufferMetaData> &inputs_metadatas)
{
    static const uint32_t YOLOV8_NUM_OF_OUTPUTS_PER_SHAPE = 2;
    uint32_t num_of_proposals = 0;
    for (const auto &input_metadata_pair : inputs_metadatas) {
        auto &input_metadata = input_metadata_pair.second;
        num_of_proposals += input_metadata.shape.height * input_metadata.shape.width;
    }
    return num_of_proposals / YOLOV8_NUM_OF_OUTPUTS_PER_SHAPE; // we want to count the proposals from each unique shape only once
}

Expected<net_flow::PostProcessOpMetadataPtr> create_yolov8_bbox_only_op_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads,
    const std::string &network_name)
{
    auto nms_config = create_post_process_nms_config(op_proto);
    TRY(auto yolov8_config, create_yolov8_config(op_proto, pad_index_to_streams_info));

    TRY(auto inputs_metadata, create_inputs_metadata(op_proto, pad_index_to_streams_info, input_to_output_pads));

    uint32_t num_of_proposals = compute_yolov8_bbox_only_num_of_proposals(inputs_metadata);

    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    net_flow::BufferMetaData output_metadata{};
    output_metadata.format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(
        { HAILO_FORMAT_TYPE_AUTO, HAILO_FORMAT_ORDER_AUTO, HAILO_FORMAT_FLAGS_NONE }, net_flow::OperationType::YOLOV8, nms_config.bbox_only);
    auto bbox_num_of_coordinates = static_cast<uint32_t>(sizeof(hailo_rectangle_t) / sizeof(float32_t));
    output_metadata.shape = {1, num_of_proposals, bbox_num_of_coordinates + op_proto.nms_op().classes()};
    outputs_metadata.insert({op_proto.output_pads()[0].name(), output_metadata});

    return net_flow::Yolov8BboxOnlyOpMetadata::create(inputs_metadata, outputs_metadata, nms_config, yolov8_config,
        network_name);
}

Expected<net_flow::PostProcessOpMetadataPtr> create_yolox_op_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads,
    const std::string &network_name)
{
    auto nms_config = create_post_process_nms_config(op_proto);

    net_flow::YoloxPostProcessConfig yolox_config{};
    yolox_config.image_height = (float32_t)op_proto.nms_op().yolox_nms_op().image_height();
    yolox_config.image_width = (float32_t)op_proto.nms_op().yolox_nms_op().image_width();

    std::unordered_map<std::string, net_flow::BufferMetaData> inputs_metadata;
    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    net_flow::BufferMetaData output_metadata{};
    output_metadata.format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(
        { HAILO_FORMAT_TYPE_AUTO, HAILO_FORMAT_ORDER_AUTO, HAILO_FORMAT_FLAGS_NONE }, net_flow::OperationType::YOLOX, nms_config.bbox_only);
    outputs_metadata.insert({op_proto.output_pads()[0].name(), output_metadata});

    for (const auto &bbox_proto : op_proto.nms_op().yolox_nms_op().bbox_decoders()) {
        assert(contains(pad_index_to_streams_info, static_cast<size_t>(bbox_proto.reg_pad_index())));
        auto reg_name = pad_index_to_streams_info.at(bbox_proto.reg_pad_index()).name;
        assert(contains(pad_index_to_streams_info, static_cast<size_t>(bbox_proto.cls_pad_index())));
        auto cls_name = pad_index_to_streams_info.at(bbox_proto.cls_pad_index()).name;
        assert(contains(pad_index_to_streams_info, static_cast<size_t>(bbox_proto.obj_pad_index())));
        auto obj_name = pad_index_to_streams_info.at(bbox_proto.obj_pad_index()).name;
        yolox_config.input_names.emplace_back(net_flow::YoloxMatchingLayersNames{reg_name, obj_name, cls_name});
    }

    for (const auto &input_pad : op_proto.input_pads()) {
        CHECK_AS_EXPECTED(contains(input_to_output_pads, static_cast<size_t>(input_pad.index())), HAILO_INVALID_HEF,
            "NMS op is not connected to core op");
        auto output_pad_index = input_to_output_pads.at(input_pad.index());
        CHECK_AS_EXPECTED(contains(pad_index_to_streams_info, output_pad_index), HAILO_INVALID_HEF,
            "Pad {} of post-process {} is not connected to any core output stream",
                input_pad.index(), op_proto.name());
        const auto &op_input_stream = pad_index_to_streams_info.at(output_pad_index);
        net_flow::BufferMetaData input_metadata{};
        input_metadata.format = op_input_stream.format;
        input_metadata.quant_info = op_input_stream.quant_info;
        input_metadata.shape = op_input_stream.shape;
        input_metadata.padded_shape = op_input_stream.hw_shape;
        inputs_metadata.insert({op_input_stream.name, input_metadata});
    }

    return net_flow::YoloxOpMetadata::create(inputs_metadata, outputs_metadata, nms_config, yolox_config,
        network_name);
}

Expected<net_flow::PostProcessOpMetadataPtr> create_ssd_op_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads,
    const std::string &network_name)
{
    auto nms_config = create_post_process_nms_config(op_proto);

    net_flow::SSDPostProcessConfig ssd_config{};
    ssd_config.image_height = (float32_t)op_proto.nms_op().ssd_nms_op().image_height();
    ssd_config.image_width = (float32_t)op_proto.nms_op().ssd_nms_op().image_width();
    ssd_config.centers_scale_factor = op_proto.nms_op().ssd_nms_op().centers_scale_factor();
    ssd_config.bbox_dimensions_scale_factor = op_proto.nms_op().ssd_nms_op().bbox_dimensions_scale_factor();
    ssd_config.ty_index = op_proto.nms_op().ssd_nms_op().ty();
    ssd_config.tx_index = op_proto.nms_op().ssd_nms_op().tx();
    ssd_config.th_index = op_proto.nms_op().ssd_nms_op().th();
    ssd_config.tw_index = op_proto.nms_op().ssd_nms_op().tw();

    if ((ssd_config.ty_index == 0) && (ssd_config.tx_index == 0) && (ssd_config.th_index == 0) && (ssd_config.tw_index == 0)) {
        ssd_config.ty_index = net_flow::SSDPostProcessOp::DEFAULT_Y_OFFSET_IDX;
        ssd_config.tx_index = net_flow::SSDPostProcessOp::DEFAULT_X_OFFSET_IDX;
        ssd_config.th_index = net_flow::SSDPostProcessOp::DEFAULT_H_OFFSET_IDX;
        ssd_config.tw_index = net_flow::SSDPostProcessOp::DEFAULT_W_OFFSET_IDX;
    }

    for (auto &bbox_proto : op_proto.nms_op().ssd_nms_op().bbox_decoders()) {
        std::vector<float32_t> bbox_anchors;
        assert(bbox_proto.h().size() == bbox_proto.w().size());
        for (int i = 0; i < bbox_proto.h().size(); ++i) {
            bbox_anchors.push_back(bbox_proto.w()[i]);
            bbox_anchors.push_back(bbox_proto.h()[i]);
        }
        assert(contains(pad_index_to_streams_info, static_cast<size_t>(bbox_proto.reg_pad_index())));
        auto reg_name = pad_index_to_streams_info.at(bbox_proto.reg_pad_index()).name;
        ssd_config.anchors.insert({reg_name, bbox_anchors});
        assert(contains(pad_index_to_streams_info, static_cast<size_t>(bbox_proto.cls_pad_index())));
        auto cls_name = pad_index_to_streams_info.at(bbox_proto.cls_pad_index()).name;
        ssd_config.anchors.insert({pad_index_to_streams_info.at(bbox_proto.cls_pad_index()).name, bbox_anchors});
        ssd_config.reg_to_cls_inputs.insert({reg_name, cls_name});
    }

    std::unordered_map<std::string, net_flow::BufferMetaData> inputs_metadata;
    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    net_flow::BufferMetaData output_metadata{};
    output_metadata.format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(
        { HAILO_FORMAT_TYPE_AUTO, HAILO_FORMAT_ORDER_AUTO, HAILO_FORMAT_FLAGS_NONE }, net_flow::OperationType::SSD, nms_config.bbox_only);
    outputs_metadata.insert({op_proto.output_pads()[0].name(), output_metadata});

    for (auto &input_pad : op_proto.input_pads()) {
        CHECK_AS_EXPECTED(contains(input_to_output_pads, static_cast<size_t>(input_pad.index())), HAILO_INVALID_HEF,
            "NMS op is not connected to core op");
        auto output_pad_index = input_to_output_pads.at(input_pad.index());
        CHECK_AS_EXPECTED(contains(pad_index_to_streams_info, output_pad_index), HAILO_INVALID_HEF,
            "Pad {} of post-process {} is not connected to any core output stream",
                input_pad.index(), op_proto.name());
        const auto &op_input_stream = pad_index_to_streams_info.at(output_pad_index);
        net_flow::BufferMetaData input_metadata{};
        input_metadata.format = op_input_stream.format;
        input_metadata.quant_info = op_input_stream.quant_info;
        input_metadata.shape = op_input_stream.shape;
        input_metadata.padded_shape = op_input_stream.hw_shape;
        inputs_metadata.insert({op_input_stream.name, input_metadata});
    }

    return net_flow::SSDOpMetadata::create(inputs_metadata, outputs_metadata, nms_config, ssd_config, network_name);
}

Expected<std::shared_ptr<net_flow::OpMetadata>> create_argmax_op_metadata(const LayerInfo &op_input_layer_info, const ProtoHEFPad &output_pad,
    const std::string &output_name, const bool &is_core_hw_padding_supported, const std::string &network_name)
{
    // create input meta
    std::unordered_map<std::string, hailort::net_flow::BufferMetaData> inputs_metadata;
    hailort::net_flow::BufferMetaData input_metadata{};
    input_metadata.shape = op_input_layer_info.shape;
    // If padding is done in HW, the padded shape is as the shape (TODO: Remove once HRT support hw_padding from DFC)
    if (is_core_hw_padding_supported) {
        input_metadata.padded_shape = input_metadata.shape;
    } else {
        input_metadata.padded_shape = op_input_layer_info.hw_shape;
    }

    input_metadata.format = op_input_layer_info.format;
    input_metadata.quant_info = op_input_layer_info.quant_info;
    inputs_metadata.insert({op_input_layer_info.name, input_metadata});

    // create output meta
    std::unordered_map<std::string, hailort::net_flow::BufferMetaData> outputs_metadata;
    hailort::net_flow::BufferMetaData output_metadata{};
    output_metadata.shape = {input_metadata.shape.height, input_metadata.shape.width, hailort::net_flow::ARGMAX_OUTPUT_FEATURES_SIZE};
    output_metadata.padded_shape = output_metadata.shape;   // padded_shape is the same as the output_shape in argmax op
    output_metadata.format.order = static_cast<hailo_format_order_t>(output_pad.format_order());
    output_metadata.format.type = static_cast<hailo_format_type_t>(output_pad.format_type());
    output_metadata.quant_info.qp_zp = output_pad.numeric_info().qp_zp();
    output_metadata.quant_info.qp_scale = output_pad.numeric_info().qp_scale();
    output_metadata.quant_info.limvals_min = output_pad.numeric_info().limvals_min();
    output_metadata.quant_info.limvals_max = output_pad.numeric_info().limvals_max();
    output_metadata.format.flags = HAILO_FORMAT_FLAGS_NONE;
    outputs_metadata.insert({output_name, output_metadata});

    return net_flow::ArgmaxOpMetadata::create(inputs_metadata, outputs_metadata, network_name);
}

Expected<net_flow::PostProcessOpMetadataPtr> create_iou_op_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads,
    const std::string &network_name)
{
    auto op_type = net_flow::OperationType::IOU;
    auto nms_config = create_post_process_nms_config(op_proto);

    std::unordered_map<std::string, net_flow::BufferMetaData> inputs_metadata;
    std::unordered_map<std::string, net_flow::BufferMetaData> outputs_metadata;
    net_flow::BufferMetaData output_metadata{};
    output_metadata.format = net_flow::NmsOpMetadata::expand_output_format_autos_by_op_type(
        { HAILO_FORMAT_TYPE_AUTO, HAILO_FORMAT_ORDER_AUTO, HAILO_FORMAT_FLAGS_NONE }, op_type, nms_config.bbox_only);
    outputs_metadata.insert({op_proto.output_pads()[0].name(), output_metadata});

    for (auto &input_pad : op_proto.input_pads()) {
        CHECK_AS_EXPECTED(contains(input_to_output_pads, static_cast<size_t>(input_pad.index())), HAILO_INVALID_HEF,
            "NMS op is not connected to core op");
        auto output_pad_index = input_to_output_pads.at(input_pad.index());
        CHECK_AS_EXPECTED(contains(pad_index_to_streams_info, output_pad_index), HAILO_INVALID_HEF,
            "Pad {} of post-process {} is not connected to any core output stream",
                input_pad.index(), op_proto.name());
        const auto &op_input_stream = pad_index_to_streams_info.at(output_pad_index);
        net_flow::BufferMetaData input_metadata{};
        input_metadata.format = op_input_stream.format;
        input_metadata.quant_info = op_input_stream.quant_info;
        input_metadata.shape = op_input_stream.shape;
        input_metadata.padded_shape = op_input_stream.hw_shape;
        inputs_metadata.insert({op_input_stream.name, input_metadata});
    }

    return net_flow::NmsOpMetadata::create(inputs_metadata, outputs_metadata, nms_config,network_name, op_type, "IoU-Post-Process");
}

Expected<std::shared_ptr<net_flow::OpMetadata>> create_softmax_op_metadata(const LayerInfo &op_input_layer_info, const ProtoHEFPad &output_pad,
    const std::string &output_name, const std::string &network_name)
{
    // create input meta
    std::unordered_map<std::string, hailort::net_flow::BufferMetaData> inputs_metadata;
    hailort::net_flow::BufferMetaData input_metadata{};
    input_metadata.shape = op_input_layer_info.shape;
    input_metadata.padded_shape = input_metadata.shape; // Since softmax is connected to transform context, shape and padded shape are the same

    input_metadata.format = op_input_layer_info.format;
    input_metadata.quant_info = op_input_layer_info.quant_info;
    inputs_metadata.insert({op_input_layer_info.name, input_metadata});

    // create output meta
    std::unordered_map<std::string, hailort::net_flow::BufferMetaData> outputs_metadata;
    hailort::net_flow::BufferMetaData output_metadata{};
    output_metadata.shape = input_metadata.shape;
    output_metadata.padded_shape = output_metadata.shape; // padded_shape is the same as the output_shape in softmax op
    output_metadata.format.order = static_cast<hailo_format_order_t>(output_pad.format_order());
    output_metadata.format.type = static_cast<hailo_format_type_t>(output_pad.format_type());
    output_metadata.quant_info.qp_zp = output_pad.numeric_info().qp_zp();
    output_metadata.quant_info.qp_scale = output_pad.numeric_info().qp_scale();
    output_metadata.quant_info.limvals_min = output_pad.numeric_info().limvals_min();
    output_metadata.quant_info.limvals_max = output_pad.numeric_info().limvals_max();
    output_metadata.format.flags = HAILO_FORMAT_FLAGS_NONE;
    outputs_metadata.insert({output_name, output_metadata});

    return net_flow::SoftmaxOpMetadata::create(inputs_metadata, outputs_metadata, network_name);
}

Expected<std::shared_ptr<net_flow::OpMetadata>> create_logits_op_metadata(const ProtoHEFOp &op_proto,
    const std::map<size_t, LayerInfo> &pad_index_to_streams_info, const std::map<size_t, size_t> &input_to_output_pads,
    const std::string &network_name, const bool is_core_hw_padding_config_in_dfc)
{
    // connect input_streams to net_flow element
    CHECK_AS_EXPECTED(op_proto.input_pads().size() == 1, HAILO_INVALID_HEF, "Logits op must have 1 input only");
    CHECK_AS_EXPECTED(op_proto.output_pads().size() == 1, HAILO_INVALID_HEF, "Logits op must have 1 output only");
    auto input_pad = op_proto.input_pads()[0];
    auto output_pad = op_proto.output_pads()[0];

    // Op's input_pad is fed by core's output_pad
    CHECK_AS_EXPECTED(contains(input_to_output_pads, static_cast<size_t>(input_pad.index())), HAILO_INVALID_HEF,
        "Logits op is not connected to core-op");
    auto output_pad_index = input_to_output_pads.at(input_pad.index());
    CHECK_AS_EXPECTED(contains(pad_index_to_streams_info, output_pad_index), HAILO_INVALID_HEF,
        "Pad {} of post-process {} is not connected to any core output stream", input_pad.index(), op_proto.name());

    // TODO: HRT-10603
    const auto &op_input_layer_info = pad_index_to_streams_info.at(output_pad_index);

    // TODO HRT-12099 - return invalid hef error when remove support for hefs with no max_shmifo size
    const auto max_periph_bytes = (0 == op_input_layer_info.max_shmifo_size) ? HAILO1X_PERIPH_BYTES_PER_BUFFER_MAX_SIZE :
        std::min(HAILO1X_PERIPH_BYTES_PER_BUFFER_MAX_SIZE, op_input_layer_info.max_shmifo_size);
    const auto is_core_hw_padding_supported = HefConfigurator::is_core_hw_padding_supported(op_input_layer_info,
        max_periph_bytes, is_core_hw_padding_config_in_dfc);

    switch (op_proto.logits_op().logits_type()) {
        case ProtoHEFLogitsType::PROTO_HEF_ARGMAX_TYPE: {
            return create_argmax_op_metadata(op_input_layer_info, output_pad, output_pad.name(),
                is_core_hw_padding_supported, network_name);
        }
        case ProtoHEFLogitsType::PROTO_HEF_SOFTMAX_TYPE: {
            return create_softmax_op_metadata(op_input_layer_info, output_pad, output_pad.name(), network_name);
        }
        default: {
            LOGGER__ERROR("Invalid Net-Flow Logits-Op {}", ProtoHEFLogitsType_Name(op_proto.logits_op().logits_type()));
            return make_unexpected(HAILO_INTERNAL_FAILURE);
        }
    }
}

Expected<std::vector<net_flow::PostProcessOpMetadataPtr>> Hef::Impl::create_ops_metadata(const ProtoHEFNetworkGroup &network_group_proto,
    CoreOpMetadata &core_op_metadata) const
{
    std::vector<net_flow::PostProcessOpMetadataPtr> result;
    if (!m_supported_features.hailo_net_flow) {
        return result;
    }
    auto output_layer_infos = core_op_metadata.get_output_layer_infos();
    std::map<size_t, LayerInfo> pad_index_to_streams_info;
    for (const LayerInfo &output_layer_info : output_layer_infos) {
        if (output_layer_info.pad_index != INVALID_PAD_INDEX) {
            pad_index_to_streams_info.insert({output_layer_info.pad_index, output_layer_info});
        }
    }
    std::map<size_t, size_t> input_to_output_pads;
    for (auto &pad_edge : network_group_proto.pad_edges()) {
        input_to_output_pads.insert({pad_edge.dst(), pad_edge.src()});
    }

    auto net_group_name = HefUtils::get_network_group_name(network_group_proto, m_supported_features);
    auto network_name = HailoRTDefaults::get_network_name(net_group_name);

    for (auto &op_proto : network_group_proto.ops()) {
        switch (op_proto.op_case()) {
            case ProtoHEFOp::kCoreOp: {
                break;
            }
            case ProtoHEFOp::kNmsOp: {
                for (auto &input_pad : op_proto.input_pads()) {
                    CHECK_AS_EXPECTED(contains(input_to_output_pads, static_cast<size_t>(input_pad.index())), HAILO_INVALID_HEF,
                        "NMS op is not connected to core-op");
                    auto output_pad_index = input_to_output_pads.at(input_pad.index());
                    CHECK_AS_EXPECTED(contains(pad_index_to_streams_info, output_pad_index), HAILO_INVALID_HEF,
                        "Pad {} of post-process {} is not connected to any core output stream",
                            input_pad.index(), op_proto.name());
                }

                net_flow::PostProcessOpMetadataPtr post_process_op_metadata;
                switch (op_proto.nms_op().nms_op_case()) {
                    case ProtoHEFNmsOp::kYoloNmsOp: {
                        if (op_proto.nms_op().bbox_decoding_only()) {
                            TRY(post_process_op_metadata, create_yolov5_bbox_only_op_metadata(op_proto, pad_index_to_streams_info,
                                input_to_output_pads, network_name));
                            break;
                        } else {
                            TRY(post_process_op_metadata, create_yolov5_op_metadata(op_proto, pad_index_to_streams_info,
                                input_to_output_pads, network_name));
                            break;
                        }
                    }
                    case ProtoHEFNmsOp::kYoloxNmsOp: {
                        TRY(post_process_op_metadata, create_yolox_op_metadata(op_proto, pad_index_to_streams_info,
                           input_to_output_pads, network_name));
                        break;
                    }
                    case ProtoHEFNmsOp::kSsdNmsOp: {
                        TRY(post_process_op_metadata, create_ssd_op_metadata(op_proto, pad_index_to_streams_info,
                           input_to_output_pads, network_name));
                        break;
                    }
                    case ProtoHEFNmsOp::kIouOp: {
                        TRY(post_process_op_metadata, create_iou_op_metadata(op_proto, pad_index_to_streams_info,
                           input_to_output_pads, network_name));
                        break;
                    }
                    case ProtoHEFNmsOp::kYoloSegOp: {
                        TRY(post_process_op_metadata, create_yolov5_seg_op_metadata(op_proto, pad_index_to_streams_info,
                           input_to_output_pads, network_name));
                        break;
                    }
                    case ProtoHEFNmsOp::kYolov8NmsOp: {
                        if (op_proto.nms_op().bbox_decoding_only()) {
                            TRY(post_process_op_metadata, create_yolov8_bbox_only_op_metadata(op_proto, pad_index_to_streams_info,
                                input_to_output_pads, network_name));
                            break;
                        } else {
                            TRY(post_process_op_metadata, create_yolov8_op_metadata(op_proto, pad_index_to_streams_info,
                                input_to_output_pads, network_name));
                            break;
                        }
                    }
                    default: {
                        LOGGER__ERROR("Unsupported Net-Flow NMS-Op");
                        return make_unexpected(HAILO_INTERNAL_FAILURE);
                    }
                }

                result.push_back(post_process_op_metadata);
                break;
            }
            case ProtoHEFOp::kLogitsOp: {
                TRY(auto post_process_op_metadata, create_logits_op_metadata(op_proto, pad_index_to_streams_info,
                    input_to_output_pads, network_name, m_supported_features.core_hw_padding_config_in_dfc));
                result.push_back(post_process_op_metadata);
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

Expected<CoreOpMetadataPtr> Hef::Impl::get_core_op_metadata(const std::string &network_group_name, uint32_t partial_clusters_layout_bitmap)
{
    CHECK_AS_EXPECTED(contains(m_network_group_metadata, network_group_name), HAILO_NOT_FOUND,
        "Network group with name {} wasn't found", network_group_name);
    auto &ng_metadata = m_network_group_metadata.at(network_group_name);
    CHECK_AS_EXPECTED(contains(ng_metadata.m_core_ops_metadata_per_arch, network_group_name), HAILO_NOT_FOUND,
        "Core-op with name {} wasn't found", network_group_name);
    auto metadata_per_arch = ng_metadata.m_core_ops_metadata_per_arch.at(network_group_name);
    auto metadata = metadata_per_arch.get_metadata(partial_clusters_layout_bitmap);
    return metadata;
}

hailo_status Hef::Impl::validate_boundary_streams_were_created(const std::string &network_group_name, std::shared_ptr<CoreOp> core_op)
{
    TRY(const auto number_of_inputs, get_number_of_input_streams(network_group_name));
    const auto size = core_op->get_input_streams().size();
    CHECK((number_of_inputs == size),
        HAILO_INVALID_ARGUMENT, "passed configure_params for network group {} did not contain all input streams", network_group_name);

    TRY(const auto number_of_outputs, get_number_of_output_streams(network_group_name));
    CHECK((number_of_outputs == core_op->get_output_streams().size()),
        HAILO_INVALID_ARGUMENT, "passed configure_params for network group {} did not contain all output streams", network_group_name);

    return HAILO_SUCCESS;
}

Expected<CONTROL_PROTOCOL__nn_stream_config_t> HefConfigurator::parse_nn_stream_config(const ProtoHEFEdgeLayerBase &edge_layer,
    bool hw_padding_supported, const ProtoHEFEdgeConnectionType &edge_connection_type)
{
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(edge_layer.core_bytes_per_buffer()), HAILO_INVALID_HEF,
        "core_bytes_per_buffer is too big");
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(edge_layer.core_buffers_per_frame()), HAILO_INVALID_HEF,
        "core_buffers_per_frame is too big");
    CHECK_AS_EXPECTED(!((ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__DDR == edge_connection_type) &&
        hw_padding_supported), HAILO_INVALID_HEF, "DDR layer can't have hw_padding_supported");
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT32(edge_layer.padded_width() * edge_layer.padded_features() *
        edge_layer.padded_height() * edge_layer.data_bytes()), HAILO_INVALID_HEF, "padded shape too big");

    CONTROL_PROTOCOL__nn_stream_config_t stream_config = {};

    stream_config.core_buffers_per_frame = static_cast<uint16_t>(edge_layer.core_buffers_per_frame());
    stream_config.core_bytes_per_buffer = static_cast<uint16_t>(edge_layer.core_bytes_per_buffer());

    // TODO HRT-10993: Remove these parameters for the parse_nn_stream_config function call
    // Initial periph register values - these values will get overrided in update_layer_info in resource_manager_builder,
    //  except in case of where we dont have resource manager (ethernet)
    stream_config.periph_buffers_per_frame = static_cast<uint16_t>(edge_layer.core_buffers_per_frame());
    stream_config.periph_bytes_per_buffer = static_cast<uint16_t>(edge_layer.core_bytes_per_buffer());

    // If hw padding is enabled - and shape fits in uint16t - change initial periph value to be row size - in any case
    // Will get updated if there is resource manager - and in ethernet will have either core register values - and if hw 
    // padding will have hw padding values
    if (hw_padding_supported) {
        if (IS_FIT_IN_UINT16(edge_layer.width() * edge_layer.features() * edge_layer.data_bytes())) {
            stream_config.periph_bytes_per_buffer = static_cast<uint16_t>(edge_layer.width() * edge_layer.features() *
                edge_layer.data_bytes());
        }

        // We currently only support HW padding in hailort with format HAILO_FORMAT_ORDER_NHCW - which is padded by feature
        // Padding should not affect the periph register values.
        const uint32_t feature_padding_payload_32bit = static_cast<uint32_t>(edge_layer.width()) *
            static_cast<uint32_t>(edge_layer.data_bytes());
        CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(static_cast<uint32_t>(edge_layer.width()) * static_cast<uint32_t>(edge_layer.data_bytes())),
            HAILO_INVALID_HEF, "frame width {} is too big", feature_padding_payload_32bit);
        stream_config.feature_padding_payload = static_cast<uint16_t>(feature_padding_payload_32bit);
    }

    return stream_config;
}

bool HefConfigurator::is_core_hw_padding_supported(const LayerInfo &layer_info, const uint32_t max_periph_bytes_value,
    const bool is_core_hw_padding_config_in_dfc)
{
    if (!(LayerType::BOUNDARY == layer_info.type) || layer_info.is_mux || is_core_hw_padding_config_in_dfc) {
        return false;
    }

    // TODO: HRT-4462 support more orders
    switch (layer_info.format.order)
    {
    case HAILO_FORMAT_ORDER_NHCW:
        break;
    default:
        LOGGER__DEBUG("HW padding is not supported for format {} ", static_cast<int>(layer_info.format.order));
        return false;
    }

    /* If the network is transposed, the width and height are swapped in LayerInfo c'tor, so need to swap it again for calculations */
    auto height = layer_info.shape.height;
    auto width = layer_info.shape.width;
    if (layer_info.format.flags & HAILO_FORMAT_FLAGS_TRANSPOSED) {
        std::swap(height, width);
    }


    if (layer_info.nn_stream_config.core_buffers_per_frame != height) {
        // TODO: HRT-3278
        LOGGER__DEBUG("HW padding is supported only on layers with core_buffers_per_frame == height");
        return false;
    }

    if (((width * layer_info.shape.features) % 8) != 0) {
        // TODO: HRT-963 support chunks
        LOGGER__DEBUG("HW padding is supported only when periph_bytes_per_buffer is a multiple of 8");
        return false;
    }

    if ((width * layer_info.shape.features * layer_info.hw_data_bytes) > (max_periph_bytes_value - 1)) {
        // TODO: HRT-4177
        LOGGER__DEBUG("HW padding is supported only on layers with shape size < stream size");
        return false;
    }
    return true;
}

Expected<std::vector<hailo_stream_info_t>> Hef::Impl::get_input_stream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    TRY(const auto core_op_metadata, get_core_op_metadata(net_group_name));
    return core_op_metadata->get_input_stream_infos(network_name);
}

Expected<std::vector<hailo_stream_info_t>> Hef::Impl::get_output_stream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    TRY(const auto core_op_metadata, get_core_op_metadata(net_group_name));
    return core_op_metadata->get_output_stream_infos(network_name);
}

Expected<std::vector<hailo_stream_info_t>> Hef::Impl::get_all_stream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    TRY(const auto core_op_metadata, get_core_op_metadata(net_group_name));
    return core_op_metadata->get_all_stream_infos(network_name);
}

Expected<std::vector<hailo_network_info_t>> Hef::Impl::get_network_infos(const std::string &net_group_name)
{
    CHECK_AS_EXPECTED(contains(m_network_group_metadata, net_group_name), HAILO_NOT_FOUND);
    return m_network_group_metadata.at(net_group_name).get_network_infos();
}

Expected<hailo_stream_info_t> Hef::Impl::get_stream_info_by_name(const std::string &stream_name,
    hailo_stream_direction_t stream_direction, const std::string &net_group_name)
{
    TRY(auto core_op_metadata, get_core_op_metadata(net_group_name));

    if (HAILO_H2D_STREAM == stream_direction) {
        TRY(auto stream_infos, core_op_metadata->get_input_stream_infos());
        for (auto &stream_info : stream_infos) {
            if (stream_name == stream_info.name) {
                return std::move(stream_info);
            }
        }
    } else {
        TRY(auto stream_infos, core_op_metadata->get_output_stream_infos());
        for (auto &stream_info : stream_infos) {
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
    CHECK_AS_EXPECTED(contains(m_network_group_metadata, net_group_name), HAILO_NOT_FOUND);
    return m_network_group_metadata.at(net_group_name).get_input_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::Impl::get_output_vstream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    CHECK_AS_EXPECTED(contains(m_network_group_metadata, net_group_name), HAILO_NOT_FOUND);
    return m_network_group_metadata.at(net_group_name).get_output_vstream_infos(network_name);
}

Expected<std::vector<hailo_vstream_info_t>> Hef::Impl::get_all_vstream_infos(const std::string &net_group_name,
    const std::string &network_name)
{
    CHECK_AS_EXPECTED(contains(m_network_group_metadata, net_group_name), HAILO_NOT_FOUND);
    return m_network_group_metadata.at(net_group_name).get_all_vstream_infos(network_name);
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

const NetworkGroupMetadata Hef::Impl::network_group_metadata(const std::string &net_group_name) const
{
    assert(contains(m_network_group_metadata, net_group_name));
    auto metadata = m_network_group_metadata.at(net_group_name);
    return metadata;
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
        network_group_name = is_multi_layout(get_device_arch()) ?
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
            network_group_ptr = is_multi_layout(get_device_arch()) ?
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

    LOGGER__ERROR("Failed to find network or network_group with the name '{}'",
        name);
    return make_unexpected(HAILO_NOT_FOUND);
}

// TODO: core_ops names?
Expected<std::shared_ptr<ProtoHEFCoreOpMock>> Hef::Impl::get_core_op_by_net_group_name(const std::string &net_group_name)
{
    if ("" == net_group_name) {
        auto network_group_ptr = m_groups[0];
        auto network_group_name = HefUtils::get_network_group_name(*network_group_ptr, m_supported_features);
        LOGGER__TRACE("No network_group name was given. Addressing default network_group: {}", network_group_name);
        const auto &core_op = m_core_ops_per_group[network_group_name][0];
        if (is_multi_layout(get_device_arch())) {
            auto partial_core_op = core_op.partial_core_ops[0];
            return std::make_shared<ProtoHEFCoreOpMock>(*(partial_core_op->core_op));
        }
        return std::make_shared<ProtoHEFCoreOpMock>(core_op);
    }
    CHECK_AS_EXPECTED(contains(m_core_ops_per_group, net_group_name), HAILO_NOT_FOUND,
        "HEF does not contain network_group with name {}", net_group_name);
    const auto &core_op = m_core_ops_per_group[net_group_name][0];
    if (is_multi_layout(get_device_arch())) {
        auto partial_core_op = core_op.partial_core_ops[0];
        return std::make_shared<ProtoHEFCoreOpMock>(*(partial_core_op->core_op));
    }
    return std::make_shared<ProtoHEFCoreOpMock>(core_op);
}

Expected<size_t> Hef::Impl::get_number_of_input_streams(const std::string &net_group_name)
{
    TRY(const auto core_op_metadata, get_core_op_metadata(net_group_name));
    TRY(const auto input_stream_infos, core_op_metadata->get_input_stream_infos());
    return input_stream_infos.size();
}

Expected<size_t> Hef::Impl::get_number_of_output_streams(const std::string &net_group_name)
{
    TRY(const auto core_op_metadata, get_core_op_metadata(net_group_name));
    TRY(const auto output_stream_infos, core_op_metadata->get_output_stream_infos());
    return output_stream_infos.size();
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
        LOGGER__ERROR("Not supported edge connection type {}", static_cast<int>(edge_connection_type));
        return make_unexpected(HAILO_INVALID_HEF);
    }
}

static hailo_3d_image_shape_t parse_layer_shape(const ProtoHEFEdgeLayerBase &base_info) {
    if (HEF__FORMAT__NMS != base_info.format()) {
        return hailo_3d_image_shape_t{base_info.height(), base_info.width(), base_info.features()};
    } else {
        return hailo_3d_image_shape_t{static_cast<uint32_t>(base_info.additional_info().nms_info().number_of_classes()),
            HailoRTCommon::BBOX_PARAMS, static_cast<uint32_t>(base_info.additional_info().nms_info().max_output_size() *
            base_info.additional_info().nms_info().input_division_factor())};
    }
}

static hailo_3d_image_shape_t parse_layer_hw_shape(const ProtoHEFEdgeLayerBase &base_info,
    const bool is_core_hw_padding_supported)
{
    if (is_core_hw_padding_supported) {
        return hailo_3d_image_shape_t{base_info.height(), base_info.width(), base_info.features()};
    } else {
        return hailo_3d_image_shape_t{base_info.padded_height(), base_info.padded_width(), base_info.padded_features()}; 
    }
}

hailo_status HefUtils::fill_layer_info_with_base_info(const ProtoHEFEdgeLayerBase &base_info, 
    const ProtoHEFEdgeConnectionType &edge_connection_type, const ProtoHEFNetworkGroupMetadata &network_group_proto,
    bool transposed, const uint16_t context_index, const uint8_t network_index, LayerInfo &layer_info,
    const SupportedFeatures &supported_features, const ProtoHEFHwArch &hef_arch, const bool is_part_of_mux_layer)
{
    TRY(layer_info.format.order, HailoRTDefaults::get_device_format_order(base_info.format()));
    TRY(layer_info.type, get_layer_type(edge_connection_type));

    // Parse host shape - parse hw shape after determining if core hw padding is supported
    layer_info.shape = parse_layer_shape(base_info);
    layer_info.hw_data_bytes = base_info.data_bytes();

    // TODO: remove duplications with stream info parse
    layer_info.format.flags = HAILO_FORMAT_FLAGS_NONE;

    // The check network_group_proto.transposed_net() is for supporting backward compatability for old hefs
    if ((network_group_proto.transposed_net() || transposed) && (layer_info.format.order != HAILO_FORMAT_ORDER_NC))  {
        std::swap(layer_info.shape.height, layer_info.shape.width);
        layer_info.format.flags |= HAILO_FORMAT_FLAGS_TRANSPOSED;
    }

    if (base_info.host_argmax()) {
        LOGGER__ERROR("Using legacy implementation of Argmax in host. Please re-compile your model with latest DFC version");
        return HAILO_HEF_NOT_SUPPORTED;
    }

    TRY(layer_info.format.type, HailoRTCommon::get_format_type(layer_info.hw_data_bytes));

    // TODO HRT-12099 - return invalid hef error when remove support for hefs with no max_shmifo size
    const auto max_periph_bytes = (0 == base_info.max_shmifo_size()) ? HAILO1X_PERIPH_BYTES_PER_BUFFER_MAX_SIZE :
        std::min(HAILO1X_PERIPH_BYTES_PER_BUFFER_MAX_SIZE, base_info.max_shmifo_size());

    // TODO HRT-12051: remove when is_core_hw_padding_supported function is removed
    // Need to set layer_info.nn_stream_config.core_buffers_per_frame for condition in is_core_hw_padding_supported
    layer_info.nn_stream_config.core_buffers_per_frame = static_cast<uint16_t>(base_info.core_buffers_per_frame());
    // TODO HRT-12051: is_part_of_mux_layer is only used for mux layer predecessors to make sure they dont have
    //  core HW padding enabled - remove when core hw padding is removed
    const bool core_hw_padding_supported = is_part_of_mux_layer ? false :
        HefConfigurator::is_core_hw_padding_supported(layer_info, max_periph_bytes,
        supported_features.core_hw_padding_config_in_dfc);
    TRY(layer_info.nn_stream_config, HefConfigurator::parse_nn_stream_config(base_info, core_hw_padding_supported,
        edge_connection_type), "Failed parse nn stream config");
    layer_info.network_index = network_index;
    layer_info.context_index = context_index;

    // TODO HRT-12051 - reunite with parse_layer_shape when is_core_hw_padding_supported function is removed
    layer_info.hw_shape = parse_layer_hw_shape(base_info, core_hw_padding_supported);

    CHECK(IS_FIT_IN_UINT8(base_info.sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", base_info.sys_index());
    layer_info.stream_index = static_cast<uint8_t>(base_info.sys_index());
    CHECK(IS_FIT_IN_UINT8(base_info.engine_id()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid engine_id: {}.", base_info.engine_id());
    layer_info.dma_engine_index = static_cast<uint8_t>(base_info.engine_id());

    if (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == layer_info.format.order) {
        TRY(layer_info.nms_info, parse_proto_nms_info(base_info.additional_info().nms_info(), supported_features.nms_burst_mode,
            hef_arch));
    }

    layer_info.max_shmifo_size = base_info.max_shmifo_size();

    if (IS_PP_DISABLED()) {
        layer_info.shape = layer_info.hw_shape;
    }

    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_layer_info(const ProtoHEFEdgeLayerInfo &info, 
    const ProtoHEFEdgeConnectionType &edge_connection_type, const ProtoHEFCoreOpMock &core_op,
    hailo_stream_direction_t direction, const uint16_t context_index, const std::string &partial_network_name, 
    uint8_t network_index, LayerInfo &layer_info, const SupportedFeatures &supported_features, const ProtoHEFHwArch &hef_arch,
    const bool is_part_of_mux_layer)
{
    if (HAILO_MAX_STREAM_NAME_SIZE < (info.name().length() + 1)) {
        LOGGER__ERROR("The edge layer '{}' has a too long name (max is HAILO_MAX_STREAM_NAME_SIZE)", info.name());
        return HAILO_INTERNAL_FAILURE;
    }
    if (HAILO_MAX_NETWORK_NAME_SIZE < (partial_network_name.length() + 1)) {
        LOGGER__ERROR("The network '{}' has a too long name (max is HAILO_MAX_NETWORK_NAME_SIZE)", partial_network_name);
        return HAILO_INTERNAL_FAILURE;
    }
    layer_info.name = info.name();

    if (IS_PP_DISABLED() && (HAILO_D2H_STREAM == direction)) {
        // The output names in the layer info can have an added index at the end: "<name>_<index>"
        // In case we want to disable post processing, we need to remove this index from the name
        // We do it by replacing the name with the longest prefix match from the original names (without the "_<index>")
        const std::string orig_name = layer_info.name;
        std::string best_match;
        for (const auto &output_name : core_op.sorted_outputs_order) {
            if (output_name.size() > orig_name.size()) {
                continue;
            }
            if ((orig_name.find(output_name) == 0) && (output_name.size() > best_match.size())) {
                best_match = output_name;
            }
        }

        if (!best_match.empty()) {
            layer_info.name = best_match;
        }
    }

    layer_info.network_name = HefUtils::get_network_name(core_op, partial_network_name);
    layer_info.is_mux = false;
    layer_info.direction = direction;
    layer_info.quant_info.limvals_max = info.numeric_info().limvals_max();
    layer_info.quant_info.limvals_min = info.numeric_info().limvals_min();
    layer_info.quant_info.qp_scale = info.numeric_info().qp_scale();
    layer_info.quant_info.qp_zp = info.numeric_info().qp_zp();

    auto status = fill_layer_info_with_base_info(info.edge_layer_base(), edge_connection_type, core_op.network_group_metadata,
        info.transposed(), context_index, network_index, layer_info, supported_features, hef_arch, is_part_of_mux_layer);
    CHECK_SUCCESS(status);

    int number_of_qps = (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == layer_info.format.order) ? NMS_NUMBER_OF_QPS : layer_info.shape.features;
    bool is_qp_zps_empty = info.numeric_info().qp_zps().empty();
    bool is_qp_scales_empty = info.numeric_info().qp_scales().empty();

    if ((supported_features.output_scale_by_feature) && (direction == HAILO_D2H_STREAM)) {
        CHECK((info.numeric_info().qp_zps().size() == number_of_qps || is_qp_zps_empty) && (info.numeric_info().qp_scales().size() == number_of_qps || is_qp_scales_empty),
            HAILO_INVALID_HEF, "Invalid quantization infos vector in HEF!");
        // We set quant_info to INVALID_QUANT_INFO to indicate that we work with scale by feature
        layer_info.quant_info = INVALID_QUANT_INFO;
    }
    for (int i = 0; i < number_of_qps; i++) {
        hailo_quant_info_t quant_info = {};
        quant_info.limvals_min = info.numeric_info().limvals_min();
        quant_info.limvals_max = info.numeric_info().limvals_max();
        if (supported_features.output_scale_by_feature) {
            quant_info.qp_zp = is_qp_zps_empty ? info.numeric_info().qp_zp()
                                                : static_cast<float32_t>(info.numeric_info().qp_zps()[i]);
            quant_info.qp_scale = is_qp_scales_empty ? info.numeric_info().qp_scale()
                                                  : static_cast<float32_t>(info.numeric_info().qp_scales()[i]);
        } else {
            quant_info.qp_zp =  info.numeric_info().qp_zp();
            quant_info.qp_scale =  info.numeric_info().qp_scale();
            layer_info.quant_infos.push_back(std::move(quant_info));
            break; // When working without scale by feature, vector length will always be one
        }
        layer_info.quant_infos.push_back(std::move(quant_info));
    }

    if (HAILO_H2D_STREAM == direction) {
        bool are_all_qps_the_same = true;
        for (const auto &quant_info : layer_info.quant_infos) {
            if (0 != memcmp(&quant_info, &layer_info.quant_infos[0], sizeof(quant_info))) {
                are_all_qps_the_same = false;
                break;
            }
        }
        CHECK(are_all_qps_the_same, HAILO_INVALID_HEF, "Different quantization infos are not allowed for input streams (H2D)!");
    }

    // Simulation info
    assert (1 == info.edge_layer_base().buffer_indices_size());
    layer_info.buffer_indices.cluster_index = info.edge_layer_base().buffer_indices(0).cluster_index();
    layer_info.buffer_indices.index = info.edge_layer_base().buffer_indices(0).index();

    layer_info.is_defused_nms = core_op.fused_layers_metadata.network_has_fused_layers() &&
        (HAILO_FORMAT_ORDER_HAILO_NMS_ON_CHIP == layer_info.format.order) && layer_info.nms_info.is_defused;

    if (layer_info.is_defused_nms) {
        for (const auto &fused_layer : core_op.fused_layers_metadata.fused_layers()) {
            if (fused_layer.layer_info().name() == layer_info.nms_info.defuse_info.original_name) {
                // This creates a new LayerInfo for the fused layer *for each defused layer*, even though they all share the same fused layer.
                // TODO Make it so all defused layer reference the same LayerInfo of the fused layer.
                LayerInfo fused_layer_info = {};
                status = fill_fused_nms_info(fused_layer, fused_layer_info, layer_info.quant_info, layer_info.network_name,
                    supported_features.nms_burst_mode, hef_arch);
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
    hailo_quant_info_t &defuse_quant_info, const std::string &network_name, const bool burst_mode_enabled,
    const ProtoHEFHwArch &hef_arch)
{
    auto base_info = info.layer_info().edge_layer_base();
    TRY(layer_info.format.order, HailoRTDefaults::get_device_format_order(base_info.format()));
    layer_info.format.flags = HAILO_FORMAT_FLAGS_NONE;

    layer_info.shape.height = static_cast<uint32_t>(info.nms_info().number_of_classes());
    layer_info.shape.width = HailoRTCommon::BBOX_PARAMS;
    layer_info.shape.features = static_cast<uint32_t>(info.nms_info().max_output_size() *
        info.nms_info().input_division_factor());

    layer_info.hw_data_bytes = base_info.data_bytes();

    TRY(layer_info.format.type, HailoRTCommon::get_format_type(layer_info.hw_data_bytes));

    TRY(layer_info.nms_info, parse_proto_nms_info(info.nms_info(), burst_mode_enabled, hef_arch));

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
    const ProtoHEFEdgeConnectionType &edge_connection_type, const ProtoHEFCoreOpMock &core_op,
    hailo_stream_direction_t direction, const uint16_t context_index, const std::string &partial_network_name, 
    uint8_t network_index, LayerInfo &layer_info, const SupportedFeatures &supported_features, const ProtoHEFHwArch &hef_arch)
{
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

    const bool NOT_TRANSPOSED = false;
    auto status = fill_layer_info_with_base_info(info.edge_layer_base(), edge_connection_type, core_op.network_group_metadata,
        NOT_TRANSPOSED, context_index, network_index, layer_info, supported_features, hef_arch, true);
    CHECK_SUCCESS(status);

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
                status = fill_layer_info(info.predecessors(i).layer_info(), edge_connection_type, core_op, direction,
                    context_index, partial_network_name, network_index, temp_layer, supported_features, hef_arch, true);
                if (HAILO_SUCCESS != status) {
                    return status;
                }
                layer_info.predecessor.push_back(temp_layer);
                break;
            case ProtoHefEdge::kLayerMux:
                status = fill_mux_info(info.predecessors(i).layer_mux(), edge_connection_type, core_op, direction,
                    context_index, partial_network_name, network_index, temp_layer, supported_features, hef_arch);
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

Expected<hailo_format_order_t> convert_planes_format_to_hailo_format_order(const ProtoHEFEPlanesFormat &planes_format)
{
    switch (planes_format) {
    case ProtoHEFEPlanesFormat::PROTO__PLANES__FORMAT__NV12:
        return HAILO_FORMAT_ORDER_NV12;
    case ProtoHEFEPlanesFormat::PROTO__PLANES__FORMAT__NV21:
        return HAILO_FORMAT_ORDER_NV21;
    case ProtoHEFEPlanesFormat::PROTO__PLANES__FORMAT__I420:
        return HAILO_FORMAT_ORDER_I420;
    default:
        LOGGER__ERROR("Invalid planes format");
        return make_unexpected(HAILO_INVALID_HEF);
    }
}

hailo_status HefUtils::fill_planes_info(const ProtoHEFEdgeLayerPlanes &info,
    const ProtoHEFEdgeConnectionType &edge_connection_type, const ProtoHEFCoreOpMock &core_op,
    hailo_stream_direction_t direction, const uint16_t context_index, const std::string &partial_network_name, 
    uint8_t network_index, LayerInfo &layer_info, const SupportedFeatures &supported_features, const ProtoHEFHwArch &hef_arch)
{
    TRY(layer_info.type, get_layer_type(edge_connection_type));
    layer_info.direction = direction;

    layer_info.shape.height = info.height();
    layer_info.hw_shape.height = info.height();
    layer_info.shape.width = info.width();
    layer_info.hw_shape.width = info.width();
    layer_info.shape.features = info.features();
    layer_info.hw_shape.features = info.features();

    TRY(layer_info.format.order, convert_planes_format_to_hailo_format_order(info.planes_format()));
    layer_info.format.flags = HAILO_FORMAT_FLAGS_NONE;
    layer_info.quant_info = {}; // quant_info doesnt make any sense as this is a logical layer
    layer_info.quant_infos = std::vector<hailo_quant_info_t>(1); // quant_info doesnt make any sense as this is a logical layer

    CHECK(HAILO_MAX_STREAM_NAME_SIZE >= (info.name().length() + 1), HAILO_INTERNAL_FAILURE,
        "The edge layer '{}' has a too long name (max is HAILO_MAX_STREAM_NAME_SIZE)", info.name());
    CHECK(HAILO_MAX_NETWORK_NAME_SIZE >= (partial_network_name.length() + 1), HAILO_INTERNAL_FAILURE,
        "The network '{}' has a too long name (max is HAILO_MAX_NETWORK_NAME_SIZE)", partial_network_name);

    layer_info.name = info.name();
    layer_info.network_name = HefUtils::get_network_name(core_op, partial_network_name);
    layer_info.network_index = network_index;
    layer_info.is_multi_planar = true;
    layer_info.planes.reserve(info.planes_size());

    for (uint8_t i = 0; i < info.planes_size(); i++) {
        LayerInfo temp_layer = {};
        if (info.planes(i).edge_case() == ProtoHefEdge::kLayerInfo) {
            auto status = fill_layer_info(info.planes(i).layer_info(), edge_connection_type, core_op, direction,
                context_index, partial_network_name, network_index, temp_layer, supported_features, hef_arch, false);
            CHECK_SUCCESS(status);
            temp_layer.plane_index = i;
            layer_info.planes.push_back(temp_layer);
        } else {
            LOGGER__ERROR("Invalid layer type - only info layers are acceptible under a planes layer");
            return HAILO_INTERNAL_FAILURE;
            break;
        }
    }
    // hw_data_bytes doesnt make any sense as this is a logical layer. we set the hw_data_bytes of one of its underlying layers
    layer_info.hw_data_bytes = layer_info.planes.begin()->hw_data_bytes;
    CHECK(std::all_of(layer_info.planes.begin(), layer_info.planes.end(),
            [&layer_info](const auto &underlying_layer) {
                return underlying_layer.hw_data_bytes == layer_info.hw_data_bytes;
            }),
        HAILO_INVALID_HEF, "Not all underlying layers of {} has the same format type", layer_info.name);
    TRY(layer_info.format.type, HailoRTCommon::get_format_type(layer_info.hw_data_bytes));

    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_boundary_layers_info(
    const ProtoHEFCoreOpMock &core_op,
    const uint16_t context_index,
    const ProtoHEFEdgeLayer &layer,
    const SupportedFeatures &supported_features,
    ContextMetadata &context_metadata,
    const ProtoHEFHwArch &hef_arch)
{
    TRY(auto layer_info, get_boundary_layer_info(core_op, context_index, layer, supported_features, hef_arch));
    context_metadata.add_boundary_layer(layer_info);

    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_inter_context_layers_info(
    const ProtoHEFCoreOpMock &core_op,
    const uint16_t context_index,
    const ProtoHEFEdgeLayer &layer,
    const SupportedFeatures &supported_features,
    ContextMetadata &context_metadata)
{
    TRY(auto layer_info, get_inter_context_layer_info(core_op, context_index, layer, supported_features));
    context_metadata.add_inter_context_layer(layer_info);
    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_ddr_layers_info(
    const ProtoHEFCoreOpMock &core_op,
    const uint16_t context_index,
    const ProtoHEFEdgeLayer &layer,
    const SupportedFeatures &supported_features,
    ContextMetadata &context_metadata)
{
    TRY(auto layer_info, get_ddr_layer_info(core_op, context_index, layer, supported_features));
    context_metadata.add_ddr_layer(layer_info);
    return HAILO_SUCCESS;
}

hailo_status HefUtils::fill_cache_layers_info(
    const ProtoHEFCoreOpMock &core_op,
    const uint16_t context_index,
    const ProtoHEFEdgeLayer &layer,
    const SupportedFeatures &supported_features,
    ContextMetadata &context_metadata)
{
    TRY(auto layer_info, get_cache_layer_info(core_op, context_index, layer, supported_features));
    context_metadata.add_cache_layer(layer_info);
    return HAILO_SUCCESS;
}

hailo_status HefUtils::check_ddr_pairs_match(
    const std::vector<LayerInfo> &context_ddr_input_layers,
    const std::vector<LayerInfo> &context_ddr_output_layers,
    const uint16_t context_index)
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
        LOGGER__ERROR("Unsupported trigger given {}", static_cast<int>(trigger_proto.trigger_case()));
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
    const SupportedFeatures &supported_features, bool &const_input_layer_found)
{
    switch (proto_action.action_case()) {
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
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT32(proto_action.enable_lcu().lcu_kernel_done_count()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid lcu_kernel_done_count: {}.", proto_action.enable_lcu().lcu_kernel_done_count());

            auto support_multi_networks = supported_features.multi_network_support;
            auto network_index = static_cast<uint8_t>((support_multi_networks) ? proto_action.enable_lcu().network_index() : 0);

            const auto cluster_index = static_cast<uint8_t>(proto_action.enable_lcu().cluster_index());
            const auto lcu_index = static_cast<uint8_t>(proto_action.enable_lcu().lcu_index());
            const auto kernel_done_address = static_cast<uint16_t>(proto_action.enable_lcu().lcu_kernel_done_address());
            const auto kernel_done_count = static_cast<uint32_t>(proto_action.enable_lcu().lcu_kernel_done_count());

            return EnableLcuAction::create(cluster_index, lcu_index, network_index, kernel_done_address,
                kernel_done_count);
        }
        case ProtoHEFAction::kSwitchLcuBatch:
        {
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.switch_lcu_batch().cluster_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid cluster_index: {}.", proto_action.switch_lcu_batch().cluster_index());
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.switch_lcu_batch().lcu_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid lcu_index: {}.", proto_action.switch_lcu_batch().lcu_index());

            auto support_multi_networks = supported_features.multi_network_support;
            auto network_index = static_cast<uint8_t>((support_multi_networks) ? proto_action.switch_lcu_batch().network_index() : 0);

            const auto cluster_index = static_cast<uint8_t>(proto_action.switch_lcu_batch().cluster_index());
            const auto lcu_index = static_cast<uint8_t>(proto_action.switch_lcu_batch().lcu_index());
            // the kernel_done_count field isn't used but required as legacy.
            const auto NULL_KERNEL_DONE_COUNT = (uint32_t)0;

            return SwitchLcuBatchAction::create(cluster_index, lcu_index, network_index,
                NULL_KERNEL_DONE_COUNT);
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

        case ProtoHEFAction::kAllowConfigChannels:
            CHECK(IS_FIT_IN_UINT8(proto_action.allow_config_channels().config_index()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid config_index: {}.", proto_action.allow_config_channels().config_index());
            return ConfigChannelPreAllowInputDataflowAction::create();

        case ProtoHEFAction::kDisableDataChannels:
            return DisableDataChannelsAction::create();

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

            CHECK_AS_EXPECTED((0 != proto_action.enable_nms().number_of_classes()) &&
                (0 != proto_action.enable_nms().burst_size()), HAILO_INVALID_HEF,
                "Enable NMS Action must have number of classes and burst size, Please update Hef to SDK version newer than 3.24");

            uint16_t number_of_classes = static_cast<uint16_t>(proto_action.enable_nms().number_of_classes());
            uint16_t burst_size = static_cast<uint16_t>(proto_action.enable_nms().burst_size());

            auto support_multi_networks = supported_features.multi_network_support;
            auto network_index = static_cast<uint8_t>((support_multi_networks) ? proto_action.enable_nms().network_index() : 0);

            const auto nms_unit_index = static_cast<uint8_t>(proto_action.enable_nms().nms_unit_index());

            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.enable_nms().division_factor()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid division factor: {}.", proto_action.enable_nms().division_factor());

            // If division_factor is not defined - use division_factor = 1
            const auto division_factor = (0 == proto_action.enable_nms().division_factor()) ? 
                DEFAULT_DIVISION_FACTOR : static_cast<uint8_t>(proto_action.enable_nms().division_factor());

            return EnableNmsAction::create(nms_unit_index, network_index, number_of_classes, burst_size, division_factor);
        }

        case ProtoHEFAction::kWriteDataByType:
        {
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT32(proto_action.write_data_by_type().address()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid write_data_by_type address: {} (should fit uint32_t).",
                proto_action.write_data_by_type().address());
            CHECK_AS_EXPECTED((0 == (proto_action.write_data_by_type().address() % ALIGNED_TO_4_BYTES)), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid write_data_by_type address. Address should be aligned to 4 bytes: {}.",
                proto_action.write_data_by_type().address());
            CHECK_AS_EXPECTED(proto_action.write_data_by_type().data_type() == ProtoHEFWriteDataType::DATA_FROM_ACTION ||
                proto_action.write_data_by_type().data_type() == ProtoHEFWriteDataType::BATCH_SIZE, HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid write_data_by_type data_type: {} ", static_cast<int>(proto_action.write_data_by_type().data_type()));
            CHECK_AS_EXPECTED(proto_action.write_data_by_type().data().length() <= CONTEXT_SWITCH_DEFS__WRITE_ACTION_BY_TYPE_MAX_SIZE, HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid write_data_by_type data size: {} ", proto_action.write_data_by_type().data().length());
            CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(proto_action.write_data_by_type().shift()), HAILO_INVALID_HEF,
                "Failed to parse HEF. Invalid write_data_by_type shift: {} (should fit uint8_t).",
                proto_action.write_data_by_type().shift());

            uint32_t data = 0x0;
            memcpy(&data, proto_action.write_data_by_type().data().data(),
                /* Limit the data to one register */
                MIN(CONTEXT_SWITCH_DEFS__WRITE_ACTION_BY_TYPE_MAX_SIZE, proto_action.write_data_by_type().data().length()));

            const auto address = static_cast<uint32_t>(proto_action.write_data_by_type().address());
            const auto data_type = static_cast<uint8_t>(proto_action.write_data_by_type().data_type());
            const auto mask = proto_action.write_data_by_type().mask();
            auto support_multi_networks = supported_features.multi_network_support;
            const auto network_index = static_cast<uint8_t>((support_multi_networks) ? proto_action.write_data_by_type().network_index() : 0);
            const auto shift = static_cast<uint8_t>(proto_action.write_data_by_type().shift());

            // If data_type is BATCH_SIZE - can't fast batch switch
            if (ProtoHEFWriteDataType::BATCH_SIZE == data_type) {
                const_input_layer_found = true;
            }

            return WriteDataByTypeAction::create(address, data_type, data, shift, mask, network_index);
        }

        case ProtoHEFAction::kDebug:
        {
            if (proto_action.debug().has_sleep()) {
                CHECK(proto_action.debug().sleep().duration_in_usec() >= MIN_SLEEP_TIME_USEC, HAILO_INVALID_HEF, "Sleep time must be at least {} & must be in microseconds", MIN_SLEEP_TIME_USEC);
                return SleepAction::create(proto_action.debug().sleep().duration_in_usec());
            } else if (proto_action.debug().has_halt()) {
                return HaltAction::create();
            } else {
                LOGGER__ERROR("Debug action must have sleep or halt field - action case: {}, action type: {}", static_cast<int>(proto_action.debug().action_case()),
                    static_cast<int>(proto_action.debug().type()));
                return make_unexpected(HAILO_INVALID_HEF);
            }
        }
        default:
            LOGGER__ERROR("Action {} not implemented", static_cast<int>(proto_action.action_case()));
            break;
    }

    // Default case
    return make_unexpected(HAILO_INTERNAL_FAILURE);
}

static Expected<Buffer> build_config_buffer(const std::vector<MemoryView> &ccw_buffers)
{
    size_t buffer_size = 0;
    for (const auto &ccw_buffer : ccw_buffers) {
        buffer_size += ccw_buffer.size();
    }

    TRY(auto config_buffer, Buffer::create(buffer_size));

    size_t current_offset = 0;
    for (const auto &ccw_buffer : ccw_buffers) {
        assert(current_offset + ccw_buffer.size() <= config_buffer.size());
        memcpy(config_buffer.data() + current_offset, ccw_buffer.data(), ccw_buffer.size());
        current_offset += ccw_buffer.size();
    }

    return config_buffer;
}

static hailo_status merge_write_ccw_actions(
    std::vector<ContextSwitchConfigActionPtr> &actions,
    ConfigBufferInfoMap &config_buffer_infos,
    const std::vector<const ProtoHEFActionWriteDataCcw *> &write_ccw_actions)
{
    // Map between config stream index and vector of config buffers.
    std::map<uint8_t, std::vector<MemoryView>> ccw_buffers_per_config_streams;
    for (const auto *write_ccw_action : write_ccw_actions) {
        CHECK(IS_FIT_IN_UINT8(write_ccw_action->cfg_channel_index()), HAILO_INVALID_HEF,
            "Invalid cfg channel index");
        const auto config_stream_index = static_cast<uint8_t>(write_ccw_action->cfg_channel_index());
        const auto write_ccw_buffer = MemoryView::create_const(write_ccw_action->data().data(), write_ccw_action->data().size());
        ccw_buffers_per_config_streams[config_stream_index].emplace_back(write_ccw_buffer);
    }

    for (const auto &ccw_buffers_per_config_stream : ccw_buffers_per_config_streams) {
        const auto config_stream_index = ccw_buffers_per_config_stream.first;
        const auto &ccw_buffers = ccw_buffers_per_config_stream.second;
        TRY(auto config_buffer, build_config_buffer(ccw_buffers));

        assert(config_buffer.size() < std::numeric_limits<uint32_t>::max());
        config_buffer_infos[config_stream_index].ccw_dma_transfers.emplace_back(static_cast<uint32_t>(config_buffer.size()));

        const size_t total_ccw_burst = ccw_buffers.size();
        TRY(auto action,
            WriteDataCcwActionByBuffer::create(std::move(config_buffer), config_stream_index, total_ccw_burst));
        actions.emplace_back(std::move(action));
    }

    return HAILO_SUCCESS;
}

static hailo_status build_write_ccw_actions(
    std::vector<ContextSwitchConfigActionPtr> &actions,
    ConfigBufferInfoMap &config_buffer_infos,
    const std::vector<const ProtoHEFActionWriteDataCcwPtr*> &write_ccw_actions,
    std::shared_ptr<SeekableBytesReader> hef_reader,
    size_t ccws_offset)
{
    std::unordered_map<uint8_t, std::vector<ccw_write_ptr_t>> ccw_write_ptrs_per_config_streams;
    for (const auto *write_ccw_action : write_ccw_actions) {
        CHECK(IS_FIT_IN_UINT8(write_ccw_action->cfg_channel_index()), HAILO_INVALID_HEF,
            "Invalid cfg channel index");
        const auto config_stream_index = static_cast<uint8_t>(write_ccw_action->cfg_channel_index());
        ccw_write_ptr_t ccw_write_ptr = {ccws_offset + write_ccw_action->offset(), write_ccw_action->size()}; // offset is relative to the start of the ccws
        ccw_write_ptrs_per_config_streams[config_stream_index].emplace_back(ccw_write_ptr);
        config_buffer_infos[config_stream_index].ccw_dma_transfers.emplace_back(ccw_write_ptr.size);
    }

    for (auto &ccw_write_ptrs_per_config_stream : ccw_write_ptrs_per_config_streams) {
        const auto config_stream_index = ccw_write_ptrs_per_config_stream.first;
        CHECK(IS_FIT_IN_UINT16(ccw_write_ptrs_per_config_stream.second.size()), HAILO_INVALID_HEF,
            "Too many ccw burst {} (must fit in uint16)", ccw_write_ptrs_per_config_stream.second.size());

        config_buffer_infos[config_stream_index].offset_from_hef_base = ccws_offset + ccw_write_ptrs_per_config_stream.second.begin()->offset;

        TRY(auto action,
            WriteDataCcwAction::create(std::move(ccw_write_ptrs_per_config_stream.second), config_stream_index,
                static_cast<uint16_t>(ccw_write_ptrs_per_config_stream.second.size()), hef_reader));
        actions.emplace_back(std::move(action));
    }

    return HAILO_SUCCESS;
}

static hailo_status parse_hef_actions(const ProtoHEFOperation &operation_proto, std::vector<ContextSwitchConfigActionPtr> &actions,
    ConfigBufferInfoMap &config_buffer_infos, const SupportedFeatures &supported_features, bool &const_input_layer_found,
    std::shared_ptr<SeekableBytesReader> hef_reader, size_t ccws_offset)
{
    std::vector<const ProtoHEFActionWriteDataCcwPtr*> current_write_ccw_ptr_actions;

    for (int action_index = 0; action_index < operation_proto.actions_size(); action_index++) {
        const auto &proto_action = operation_proto.actions(action_index);
        CHECK(proto_action.action_case() != ProtoHEFAction::kWriteDataCcw, HAILO_INVALID_HEF, "WriteDataCcw action is not supported for hef version 1");

        if (proto_action.action_case() == ProtoHEFAction::kWriteDataCcwPtr) {
            // Keep in vector, parse later
            current_write_ccw_ptr_actions.push_back(&proto_action.write_data_ccw_ptr());

            const auto next_action_index = action_index + 1;
            const bool is_last_ccw =
                (next_action_index == operation_proto.actions_size()) ||
                (operation_proto.actions(next_action_index).action_case() != ProtoHEFAction::kWriteDataCcwPtr);

            if (is_last_ccw) {
                assert(nullptr != hef_reader);
                auto status = build_write_ccw_actions(actions, config_buffer_infos, current_write_ccw_ptr_actions,
                    hef_reader, ccws_offset);
                CHECK_SUCCESS(status);
                current_write_ccw_ptr_actions.clear();
            }
        } else {
            TRY(auto action, parse_action(proto_action, supported_features, const_input_layer_found));
            actions.emplace_back(std::move(action));
        }
    }
    assert(current_write_ccw_ptr_actions.empty());

    return HAILO_SUCCESS;
}

static hailo_status prepare_aligned_ccws_transfers(const ProtoHEFOperation &operation_proto,
    ConfigBufferInfoMap &config_buffer_infos,
    std::unordered_map<uint8_t, uint64_t> &next_offset_per_config_channel)
{
    for (int action_index = 0; action_index < operation_proto.actions_size(); action_index++) {
        const auto &proto_action = operation_proto.actions(action_index);
        if (proto_action.action_case() == ProtoHEFAction::kWriteDataCcwPtr) {
            auto ccw_ptr_action_proto = proto_action.write_data_ccw_ptr();
            auto const &current_config_channel = static_cast<uint8_t>(ccw_ptr_action_proto.cfg_channel_index());
            if (0 == next_offset_per_config_channel[current_config_channel]) {
                // Meaning it's the first write_ccw in a burst
                config_buffer_infos[current_config_channel].ccw_bursts_offsets.emplace_back(ccw_ptr_action_proto.offset());
                config_buffer_infos[current_config_channel].ccw_bursts_sizes.emplace_back(ccw_ptr_action_proto.size());
                next_offset_per_config_channel[current_config_channel] = ccw_ptr_action_proto.offset() + ccw_ptr_action_proto.size();
            } else if (next_offset_per_config_channel[current_config_channel] == ccw_ptr_action_proto.offset()) {
                // consecutive in memory => concating
                next_offset_per_config_channel[current_config_channel] += ccw_ptr_action_proto.size();
                config_buffer_infos[current_config_channel].ccw_bursts_sizes.back() += ccw_ptr_action_proto.size();
            } else {
                // This write is not consecutive in memory => saving burst + starting a new burst
                config_buffer_infos[current_config_channel].ccw_bursts_offsets.emplace_back(ccw_ptr_action_proto.offset());
                config_buffer_infos[current_config_channel].ccw_bursts_sizes.emplace_back(ccw_ptr_action_proto.size());
                next_offset_per_config_channel[current_config_channel] = ccw_ptr_action_proto.offset() + ccw_ptr_action_proto.size();
            }
        }
    }

    return HAILO_SUCCESS;
}

static hailo_status parse_hef_actions(const ProtoHEFOperation &operation_proto,
    std::vector<ContextSwitchConfigActionPtr> &actions, ConfigBufferInfoMap &config_buffer_infos,
    const SupportedFeatures &supported_features, bool &const_input_layer_found)
{
    std::vector<const ProtoHEFActionWriteDataCcw*> current_write_ccw_actions;

    for (int action_index = 0; action_index < operation_proto.actions_size(); action_index++) {
        const auto &proto_action = operation_proto.actions(action_index);
        CHECK(proto_action.action_case() != ProtoHEFAction::kWriteDataCcwPtr, HAILO_INVALID_HEF, "kWriteDataCcwPtr action is not supported for hef version 0");

        if (proto_action.action_case() == ProtoHEFAction::kWriteDataCcw) {
            // Keep in vector, parse later
            current_write_ccw_actions.push_back(&proto_action.write_data_ccw());

            const auto next_action_index = action_index + 1;
            const bool is_last_ccw =
                (next_action_index == operation_proto.actions_size()) ||
                (operation_proto.actions(next_action_index).action_case() != ProtoHEFAction::kWriteDataCcw);
            if (is_last_ccw) {
                auto status = merge_write_ccw_actions(actions, config_buffer_infos, current_write_ccw_actions);
                CHECK_SUCCESS(status);
                current_write_ccw_actions.clear();
            }
        } else {
            TRY(auto action, parse_action(proto_action, supported_features, const_input_layer_found));
            actions.emplace_back(std::move(action));
        }
    }
    assert(current_write_ccw_actions.empty());

    return HAILO_SUCCESS;
}

static hailo_status parse_operation(std::vector<ContextSwitchConfigActionPtr> &actions,
    ConfigBufferInfoMap &config_buffer_infos,
    const ProtoHEFOperation &operation_proto,
    const SupportedFeatures &supported_features,
    bool &const_input_layer_found, uint32_t hef_version,
    std::shared_ptr<SeekableBytesReader> hef_reader, size_t ccws_offset,
    std::unordered_map<uint8_t, uint64_t> &next_offset_per_config_channel)
{
    TRY(auto trigger_action, parse_trigger_action(operation_proto.trigger()));
    actions.emplace_back(std::move(trigger_action));
    hailo_status status = HAILO_UNINITIALIZED;

    switch (hef_version)
    {
    case HEADER_VERSION_0:
        status = parse_hef_actions(operation_proto, actions, config_buffer_infos, supported_features, const_input_layer_found);
        CHECK_SUCCESS(status);
        break;
    case HEADER_VERSION_1:
    case HEADER_VERSION_2:
    case HEADER_VERSION_3:
        status = parse_hef_actions(operation_proto, actions, config_buffer_infos, supported_features, const_input_layer_found, hef_reader, ccws_offset);
        CHECK_SUCCESS(status);
        if (HEADER_VERSION_3 == hef_version) {
            // On V3 HEFs we support CCWs alignment, so we need to prepare the aligned transfers option
            status = prepare_aligned_ccws_transfers(operation_proto, config_buffer_infos, next_offset_per_config_channel);
            CHECK_SUCCESS(status);
        }
        break;
    default:
        LOGGER__ERROR("Unsupported hef version {}", hef_version);
        return HAILO_HEF_NOT_SUPPORTED;
    }

    return HAILO_SUCCESS;
}

static Expected<ContextMetadata> parse_operations(
    const google::protobuf::RepeatedPtrField<ProtoHEFOperation> &operations_proto,
    const SupportedFeatures &supported_features,
    uint32_t hef_version, std::shared_ptr<SeekableBytesReader> hef_reader, size_t ccws_offset)
{
    std::vector<ContextSwitchConfigActionPtr> actions;
    ConfigBufferInfoMap config_buffer_infos;
    bool const_input_layer_found = false;

    std::unordered_map<uint8_t, uint64_t> next_offset_per_config_channel;
    for (auto &config_buffer_info : config_buffer_infos) {
        next_offset_per_config_channel[config_buffer_info.first] = 0;
    }

    for (const auto &operation_proto : operations_proto) {
        auto status = parse_operation(actions, config_buffer_infos, operation_proto, supported_features,
            const_input_layer_found, hef_version, hef_reader, ccws_offset, next_offset_per_config_channel);
        CHECK_SUCCESS_AS_EXPECTED(status);
    }

    return ContextMetadata(std::move(actions), std::move(config_buffer_infos), const_input_layer_found);
}

Expected<ContextMetadata> HefUtils::parse_preliminary_context(const ProtoHEFPreliminaryConfig &preliminary_proto,
    const SupportedFeatures &supported_features, uint32_t hef_version, std::shared_ptr<SeekableBytesReader> hef_reader, 
    size_t ccws_offset)
{
    return parse_operations(preliminary_proto.operation(), supported_features, hef_version, hef_reader, ccws_offset);
}

Expected<ContextMetadata> HefUtils::parse_single_dynamic_context(const ProtoHEFCoreOpMock &core_op,
    const ProtoHEFContext &context_proto, uint16_t context_index, const SupportedFeatures &supported_features,
    const ProtoHEFHwArch &hef_arch, uint32_t hef_version, std::shared_ptr<SeekableBytesReader> hef_reader,
    size_t ccws_offset)
{
    ContextMetadata context_metadata;
    TRY(context_metadata, parse_operations(context_proto.operations(), supported_features, hef_version, hef_reader, ccws_offset));

    for (const auto &edge_layer : context_proto.metadata().edge_layers()) {
        if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__BOUNDARY ==
                edge_layer.context_switch_info().edge_connection_type()) {
            auto status = fill_boundary_layers_info(core_op, context_index, edge_layer,
                supported_features, context_metadata, hef_arch);
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
        } else if (ProtoHEFEdgeConnectionType::PROTO__EDGE_CONNECTION_TYPE__CACHE ==
                edge_layer.context_switch_info().edge_connection_type()) {
            auto status = fill_cache_layers_info(core_op, context_index, edge_layer,
                supported_features, context_metadata);
            CHECK_SUCCESS_AS_EXPECTED(status);
        } else {
            LOGGER__ERROR("Unsupported edge connection type given {}", static_cast<int>(edge_layer.context_switch_info().edge_connection_type()));
            return make_unexpected(HAILO_INVALID_HEF);
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

Expected<std::vector<ContextMetadata>> HefUtils::parse_dynamic_contexts(const ProtoHEFCoreOpMock &core_op, const SupportedFeatures &supported_features,
    const ProtoHEFHwArch &hef_arch, uint32_t hef_version, std::shared_ptr<SeekableBytesReader> hef_reader, size_t ccws_offset)
{
    std::vector<ContextMetadata> contexts_metadata;
    for (uint16_t context_index = 0; context_index < core_op.contexts.size(); context_index++) {
        auto &context_proto = core_op.contexts[context_index];
        TRY(auto context_metadata, parse_single_dynamic_context(core_op, context_proto, context_index, supported_features,
            hef_arch, hef_version, hef_reader, ccws_offset));
        contexts_metadata.emplace_back(std::move(context_metadata));
    }

    const auto status = validate_unique_boundary_names(contexts_metadata);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return contexts_metadata;
}

static Expected<hailo_nms_burst_type_t> get_nms_burst_mode(const ProtoHEFNmsInfo &nms_info,
    const ProtoHEFHwArch &hef_arch)
{
    switch (hef_arch) {
    case PROTO__HW_ARCH__HAILO8:
    case PROTO__HW_ARCH__HAILO8P:
    case PROTO__HW_ARCH__HAILO8R:
    case PROTO__HW_ARCH__SAGE_B0:
    case PROTO__HW_ARCH__HAILO8L:
        // First generation of hw NMS - included in hailo8.
        switch (nms_info.burst_type()) {
        case PROTO__NMS_BURST_TYPE__H8_PER_CLASS:
            return HAILO_BURST_TYPE_H8_PER_CLASS;
        default:
            LOGGER__ERROR("Unsupported burst type was given {} for arch {}", static_cast<int>(nms_info.burst_type()), static_cast<int>(hef_arch));
            return make_unexpected(HAILO_INVALID_HEF);
        }
    case PROTO__HW_ARCH__HAILO1XH:
    case PROTO__HW_ARCH__HAILO15M:
    case PROTO__HW_ARCH__GINGER:
    case PROTO__HW_ARCH__LAVENDER:
    case PROTO__HW_ARCH__PLUTO:
    case PROTO__HW_ARCH__HAILO15L:
        // Second generation of hw NMS - included in hailo15, hailo10 and hailo15l.
        switch (nms_info.burst_type()) {
        case PROTO__NMS_BURST_TYPE__H15_PER_CLASS:
            return HAILO_BURST_TYPE_H15_PER_CLASS;
        case PROTO__NMS_BURST_TYPE__H15_PER_FRAME:
            return HAILO_BURST_TYPE_H15_PER_FRAME;
        default:
            LOGGER__ERROR("Unsupported burst type was given {} for arch {}", static_cast<int>(nms_info.burst_type()), static_cast<int>(hef_arch));
            return make_unexpected(HAILO_INVALID_HEF);
        }
    default:
        LOGGER__ERROR("Not supported hef arch {}", static_cast<int>(hef_arch));
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

static Expected<hailo_nms_burst_type_t> get_nms_bbox_mode(const ProtoHEFNmsInfo &nms_info,
    const ProtoHEFHwArch &hef_arch)
{
    CHECK_AS_EXPECTED(0 == nms_info.burst_type(),
        HAILO_INVALID_HEF, "Invalid HEF, nms burst extension is disabled yet burst type {} is not zero",
        static_cast<int>(nms_info.burst_type()));

    switch (hef_arch) {
    case PROTO__HW_ARCH__HAILO8:
    case PROTO__HW_ARCH__HAILO8P:
    case PROTO__HW_ARCH__HAILO8R:
    case PROTO__HW_ARCH__SAGE_B0:
    case PROTO__HW_ARCH__HAILO8L:
        return HAILO_BURST_TYPE_H8_BBOX;
    case PROTO__HW_ARCH__HAILO1XH:
    case PROTO__HW_ARCH__HAILO15M:
    case PROTO__HW_ARCH__GINGER:
    case PROTO__HW_ARCH__LAVENDER:
    case PROTO__HW_ARCH__PLUTO:
    case PROTO__HW_ARCH__HAILO15L:
        return HAILO_BURST_TYPE_H15_BBOX;

    default:
        LOGGER__ERROR("Not supported hef arch {}", static_cast<int>(hef_arch));
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
}

Expected<hailo_nms_info_t> HefUtils::parse_proto_nms_info(const ProtoHEFNmsInfo &proto_nms_info, const bool burst_mode_enabled,
    const ProtoHEFHwArch &hef_arch)
{
    hailo_nms_info_t nms_info = {};
    nms_info.number_of_classes = static_cast<uint32_t>(proto_nms_info.number_of_classes());
    nms_info.bbox_size = static_cast<uint32_t>(proto_nms_info.bbox_size());
    nms_info.max_bboxes_per_class = static_cast<uint32_t>(proto_nms_info.max_output_size());
    nms_info.max_bboxes_total = nms_info.max_bboxes_per_class * nms_info.number_of_classes;
    nms_info.chunks_per_frame = static_cast<uint32_t>(proto_nms_info.input_division_factor());

    if (burst_mode_enabled) {
        nms_info.burst_size = static_cast<uint32_t>(proto_nms_info.burst_size());
        TRY(nms_info.burst_type, get_nms_burst_mode(proto_nms_info, hef_arch));
        CHECK_AS_EXPECTED((nms_info.burst_size * nms_info.bbox_size) <= HailoRTCommon::MAX_NMS_BURST_SIZE,
            HAILO_INVALID_HEF, "Invalid HEF, nms burst size {} larger than maximum burst size {}",
            (nms_info.burst_size * nms_info.bbox_size), HailoRTCommon::MAX_NMS_BURST_SIZE);
    } else {
        // In case of bbox mode make burst size DEFAULT_NMS_NO_BURST_SIZE
        nms_info.burst_size = DEFAULT_NMS_NO_BURST_SIZE;
        TRY(nms_info.burst_type, get_nms_bbox_mode(proto_nms_info, hef_arch));
    }

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
    const uint16_t context_index, const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features,
    const ProtoHEFHwArch &hef_arch)
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
    TRY(const auto partial_network_name, HefUtils::get_partial_network_name_by_index(core_op, network_index, supported_features));

    if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type()) {
        // TODO: return LayerInfo
        auto status = fill_layer_info(layer.layer_info(), layer.context_switch_info().edge_connection_type(), core_op,
            direction, context_index, partial_network_name, network_index, result, supported_features, hef_arch, false);
        CHECK_SUCCESS_AS_EXPECTED(status);
    } else if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__MUX == layer.edge_layer_type()) {
        // TODO: return LayerInfo
        auto status = fill_mux_info(layer.layer_mux(), layer.context_switch_info().edge_connection_type(), core_op,
            direction, context_index, partial_network_name, network_index, result, supported_features, hef_arch);
        CHECK_SUCCESS_AS_EXPECTED(status);
    } else if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__PLANES == layer.edge_layer_type()) {
        // TODO: return LayerInfo
        auto status = fill_planes_info(layer.layer_planes(), layer.context_switch_info().edge_connection_type(), core_op,
            direction, context_index, partial_network_name, network_index, result, supported_features, hef_arch);
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
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(connected_context_proto.index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid connected_context_index: {}.", connected_context_proto.index());

    ConnectedContextInfo connected_context{};
    connected_context.context_index = static_cast<uint16_t>(connected_context_proto.index());
    connected_context.stream_index = static_cast<uint8_t>(connected_context_proto.sys_index());
    connected_context.dma_engine_index = static_cast<uint8_t>(connected_context_proto.engine_id());
    return connected_context;
}

Expected<LayerInfo> HefUtils::get_inter_context_layer_info(const ProtoHEFCoreOpMock &core_op,
    const uint16_t context_index, const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features)
{
    LayerInfo result = {};
    CHECK_AS_EXPECTED(PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type(), HAILO_INVALID_HEF, "Inter-context layer can't be mux.");

    result.type = LayerType::INTER_CONTEXT;
    auto support_multi_networks = supported_features.multi_network_support;
    result.network_index = static_cast<uint8_t>((support_multi_networks) ? layer.network_index() : 0);
    TRY(const auto partial_network_name, HefUtils::get_partial_network_name_by_index(core_op, result.network_index, supported_features));
    result.network_name = HefUtils::get_network_name(core_op, partial_network_name);
    result.context_index = context_index;
    result.name = layer.layer_info().name();

    // Core hw padding is only supported on boundary layers
    const bool CORE_HW_PADDING_NOT_SUPPORTED = false;
    TRY(result.nn_stream_config, HefConfigurator::parse_nn_stream_config(layer.layer_info().edge_layer_base(),
        CORE_HW_PADDING_NOT_SUPPORTED, layer.context_switch_info().edge_connection_type()));
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", layer.layer_info().edge_layer_base().sys_index());
    result.stream_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().sys_index());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().engine_id()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid engine_id: {}.", layer.layer_info().edge_layer_base().engine_id());
    result.dma_engine_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().engine_id());

    result.max_shmifo_size = layer.layer_info().edge_layer_base().max_shmifo_size();

    result.shape = parse_layer_shape(layer.layer_info().edge_layer_base());
    result.hw_shape = parse_layer_hw_shape(layer.layer_info().edge_layer_base(), CORE_HW_PADDING_NOT_SUPPORTED);
    result.hw_data_bytes = layer.layer_info().edge_layer_base().data_bytes();

    TRY(result.format.order,
        HailoRTDefaults::get_device_format_order(layer.layer_info().edge_layer_base().format()));
    result.format.flags = HAILO_FORMAT_FLAGS_NONE;
    TRY(result.format.type, HailoRTCommon::get_format_type(result.hw_data_bytes));

    result.direction = (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST ==
            layer.direction()) ? HAILO_D2H_STREAM : HAILO_H2D_STREAM;

    // HRT-7201 - The system supports one src and multiple dstinations. Right now we're saving only one dstination
    CHECK_AS_EXPECTED(layer.context_switch_info().connected_contexts_size() >= 1, HAILO_INVALID_HEF,
        "Inter context layer info must contain connected_context");
    TRY(result.connected_context_info,
        parse_connected_context_info(layer.context_switch_info().connected_contexts(0)));

    return result;
}

Expected<LayerInfo> HefUtils::get_ddr_layer_info(const ProtoHEFCoreOpMock &core_op,
    const uint16_t context_index, const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features)
{
    LayerInfo result = {};
    CHECK_AS_EXPECTED(PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type(), HAILO_INVALID_HEF, "DDR layer can't be mux.");

    result.type = LayerType::DDR;

    auto support_multi_networks = supported_features.multi_network_support;
    result.network_index = static_cast<uint8_t>((support_multi_networks) ? layer.network_index() : 0);
    TRY(const auto partial_network_name,
        HefUtils::get_partial_network_name_by_index(core_op, result.network_index, supported_features));
    result.network_name = HefUtils::get_network_name(core_op, partial_network_name);
    result.context_index = context_index;
    result.name = layer.layer_info().name();

    // Core hw padding is only supported on boundary layers
    const bool CORE_HW_PADDING_NOT_SUPPORTED = false;
    TRY(result.nn_stream_config,
        HefConfigurator::parse_nn_stream_config(layer.layer_info().edge_layer_base(),
        CORE_HW_PADDING_NOT_SUPPORTED, layer.context_switch_info().edge_connection_type()));
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", layer.layer_info().edge_layer_base().sys_index());
    result.stream_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().sys_index());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().engine_id()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid engine_id: {}.", layer.layer_info().edge_layer_base().engine_id());
    result.dma_engine_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().engine_id());
    result.max_shmifo_size = layer.layer_info().edge_layer_base().max_shmifo_size();

    CHECK_AS_EXPECTED(layer.context_switch_info().connected_contexts_size() == 1, HAILO_INVALID_HEF,
        "Only single connected context is supported on DDR channels");
    TRY(result.connected_context_info,
        parse_connected_context_info(layer.context_switch_info().connected_contexts(0)));
    CHECK_AS_EXPECTED(context_index == result.connected_context_info.context_index,
        HAILO_INVALID_HEF, "for ddr layer, connected_context_index must be same to the edge layer's context");

    result.direction = (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST ==
            layer.direction()) ? HAILO_D2H_STREAM : HAILO_H2D_STREAM;

    result.shape = parse_layer_shape(layer.layer_info().edge_layer_base());
    result.hw_shape = parse_layer_hw_shape(layer.layer_info().edge_layer_base(), CORE_HW_PADDING_NOT_SUPPORTED);
    result.hw_data_bytes = layer.layer_info().edge_layer_base().data_bytes();

    TRY(result.format.order,
        HailoRTDefaults::get_device_format_order(layer.layer_info().edge_layer_base().format()));
    result.format.flags = HAILO_FORMAT_FLAGS_NONE;
    TRY(result.format.type, HailoRTCommon::get_format_type(result.hw_data_bytes));

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(layer.layer_info().edge_layer_base().core_buffers_per_frame()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid core_buffers_per_frame: {}.", layer.layer_info().edge_layer_base().core_buffers_per_frame());
    result.ddr_info.total_buffers_per_frame = static_cast<uint16_t>(layer.layer_info().edge_layer_base().core_buffers_per_frame());

    CHECK_AS_EXPECTED(IS_FIT_IN_UINT16(layer.context_switch_info().buffers()), HAILO_INVALID_HEF, 
        "calculated number of transfers for DDR buffer is out of UINT16_T range");
    result.ddr_info.min_buffered_rows = static_cast<uint16_t>(layer.context_switch_info().buffers());

    return result;
}

Expected<LayerInfo> HefUtils::get_cache_layer_info(
    const ProtoHEFCoreOpMock &core_op, const uint16_t context_index,
    const ProtoHEFEdgeLayer &layer, const SupportedFeatures &supported_features)
{
    LayerInfo result{};
    CHECK_AS_EXPECTED(PROTO__EDGE_LAYER_TYPE__INFO == layer.edge_layer_type(), HAILO_INVALID_HEF, "Inter-context layer can't be mux.");

    result.type = LayerType::CACHE;
    const auto support_multi_networks = supported_features.multi_network_support;
    result.network_index = static_cast<uint8_t>((support_multi_networks) ? layer.network_index() : 0);
    TRY(const auto partial_network_name, HefUtils::get_partial_network_name_by_index(core_op, result.network_index, supported_features));
    result.network_name = HefUtils::get_network_name(core_op, partial_network_name);
    result.context_index = context_index;
    result.name = layer.layer_info().name();

    // Core hw padding is only supported on boundary layers
    const bool CORE_HW_PADDING_NOT_SUPPORTED = false;
    TRY(result.nn_stream_config, HefConfigurator::parse_nn_stream_config(layer.layer_info().edge_layer_base(),
        CORE_HW_PADDING_NOT_SUPPORTED, layer.context_switch_info().edge_connection_type()));
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().sys_index()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid sys_index: {}.", layer.layer_info().edge_layer_base().sys_index());
    result.stream_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().sys_index());
    CHECK_AS_EXPECTED(IS_FIT_IN_UINT8(layer.layer_info().edge_layer_base().engine_id()), HAILO_INVALID_HEF,
        "Failed to parse HEF. Invalid engine_id: {}.", layer.layer_info().edge_layer_base().engine_id());
    result.dma_engine_index = static_cast<uint8_t>(layer.layer_info().edge_layer_base().engine_id());

    result.max_shmifo_size = layer.layer_info().edge_layer_base().max_shmifo_size();

    result.shape = parse_layer_shape(layer.layer_info().edge_layer_base());
    result.hw_shape = parse_layer_hw_shape(layer.layer_info().edge_layer_base(), CORE_HW_PADDING_NOT_SUPPORTED);
    result.hw_data_bytes = layer.layer_info().edge_layer_base().data_bytes();

    TRY(result.format.order,
        HailoRTDefaults::get_device_format_order(layer.layer_info().edge_layer_base().format()));
    result.format.flags = HAILO_FORMAT_FLAGS_NONE;
    TRY(result.format.type, HailoRTCommon::get_format_type(result.hw_data_bytes));

    result.direction = (ProtoHEFEdgeLayerDirection::PROTO__EDGE_LAYER_DIRECTION__DEVICE_TO_HOST ==
            layer.direction()) ? HAILO_D2H_STREAM : HAILO_H2D_STREAM;

    // Negative cache_id means that the cache is not used
    const int32_t cache_id = layer.context_switch_info().cache_id();
    CHECK_AS_EXPECTED(cache_id >= 0, HAILO_INVALID_HEF, "Invalid cache_id: {}", cache_id);
    result.cache_info.cache_id = static_cast<uint32_t>(cache_id);

    return result;
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
    if (is_multi_layout(hef_arch)) {
        // Full chip arch (ex: Hailo8) can work with partitial chip arch (ex: Hailo8L) configurations.
        // in that case we choose one of the configurations.
        for (auto &partial_core_op : core_op.partial_core_ops) {
            if (partial_clusters_layout_bitmap == partial_core_op->layout.partial_clusters_layout_bitmap()
                    || (HAILO_ARCH_HAILO8 == device_arch && ProtoHEFHwArch::PROTO__HW_ARCH__HAILO8L == hef_arch)
                    || (HAILO_ARCH_HAILO15H == device_arch && ProtoHEFHwArch::PROTO__HW_ARCH__HAILO15M == hef_arch)) {
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
    CHECK_AS_EXPECTED(contains(m_network_group_metadata, net_group_name), HAILO_NOT_FOUND);
    auto res = m_network_group_metadata.at(net_group_name).get_sorted_output_names();
    return res;
}

ProtoHEFHwArch Hef::Impl::get_device_arch()
{
    return m_header.hw_arch();
}

std::shared_ptr<SeekableBytesReader> Hef::Impl::get_hef_reader() const
{
    return m_hef_reader;
}

size_t Hef::Impl::get_offset_zero_point() const
{
    return m_offset_zero_point;
}

uint64_t Hef::Impl::get_ccws_section_size() const
{
    return m_ccws_section_size;
}

bool Hef::Impl::zero_copy_config_over_descs() const
{
    // TODO: HRT-17833 - Remove the zero copy by fixing cma issues
    static constexpr uint64_t CCW_SECTION_SIZE_TO_FORCE_ZERO_COPY = 512 * 1024 * 1024; // 512 MB

    // Zero copy config over descs if:
    // 1. supported only on aligned CCWs (V3)
    // 2. not disabled by env variable
    // 3. One of:
    //    - shared config is supported (weights-sharing)
    //    - HEF is marked for memory_footprint_optimization (indicated by 'm_zero_copy_config_over_descs')
    //    - CCW section size is bigger than CCW_SECTION_SIZE_TO_FORCE_ZERO_COPY
    return (HEADER_VERSION_3 == m_hef_version) && (!is_env_variable_on(HAILO_DISABLE_ALIGNED_CCWS_ENV_VAR) &&
           (m_supported_features.shared_config || m_zero_copy_config_over_descs ||
            (m_ccws_section_size > CCW_SECTION_SIZE_TO_FORCE_ZERO_COPY)));
}

hailo_status Hef::Impl::validate_hef_version() const
{
    if (!m_supported_features.strict_versioning || is_env_variable_on(HAILO_IGNORE_STRICT_VERSION_ENV_VAR)) {
        return HAILO_SUCCESS;
    }

    hailo_version_t library_version{};
    CHECK_SUCCESS(hailo_get_library_version(&library_version));

    CHECK((m_header.sdk_version().sdk_version_major() == library_version.major) &&
        (m_header.sdk_version().sdk_version_minor() == library_version.minor), HAILO_INVALID_HEF,
        "DFC version from which this HEF was created ({}.{}.{}) is different from library version ({}.{}.{}), which is not allowed for this model.",
            m_header.sdk_version().sdk_version_major(), m_header.sdk_version().sdk_version_minor(), m_header.sdk_version().sdk_version_revision(),
            library_version.major, library_version.minor, library_version.revision);

    return HAILO_SUCCESS;
}

Expected<float64_t> Hef::Impl::get_bottleneck_fps(const std::string &net_group_name)
{
    TRY(const auto core_op, get_core_op_by_net_group_name(net_group_name));
    return core_op->network_group_metadata.bottleneck_fps();
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
    CHECK_AS_EXPECTED(contains(m_network_group_metadata, net_group_name), HAILO_NOT_FOUND);
    return m_network_group_metadata.at(net_group_name).get_stream_names_from_vstream_name(vstream_name);
}

Expected<std::vector<std::string>> Hef::Impl::get_vstream_names_from_stream_name(const std::string &stream_name,
    const std::string &net_group_name)
{
    CHECK_AS_EXPECTED(contains(m_network_group_metadata, net_group_name), HAILO_NOT_FOUND);
    return m_network_group_metadata.at(net_group_name).get_vstream_names_from_stream_name(stream_name);
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
    TRY(const auto core_op, get_core_op_by_net_group_name(net_group_name));

    std::string results;

    for (const auto &context : core_op->contexts) {
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
            } else if (is_h2d_boundary_planes_layer(layer_info)) {
                for (const auto &plane : layer_info.layer_planes().planes()) {
                    for (const auto &name : plane.layer_info().original_names()) {
                        if (original_name == name) {
                            results = std::string(layer_info.layer_planes().name());
                        }
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
    TRY(const auto copre_op, get_core_op_by_net_group_name(net_group_name));

    std::vector<std::string> results;

    for (const auto &context : copre_op->contexts) {
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
            } else if (is_h2d_boundary_planes_layer(layer_info)) {
                if (vstream_name == layer_info.layer_planes().name()) {
                    for (const auto &plane : layer_info.layer_planes().planes()) {
                        for (const auto &name : plane.layer_info().original_names()) {
                            results.push_back(name);
                        }
                    }
                    return results;
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
                } else if (ProtoHEFEdgeLayerType::PROTO__EDGE_LAYER_TYPE__PLANES == layer.edge_layer_type()) {
                    layer_name = layer.layer_planes().name();
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

std::vector<std::string> Hef::get_network_groups_names() const
{
    return pimpl->get_network_groups_names();
}

Expected<NetworkGroupsParamsMap> Hef::create_configure_params(hailo_stream_interface_t stream_interface)
{
    NetworkGroupsParamsMap results;
    for (const auto &name : pimpl->get_network_groups_names()) {
        TRY(auto params, create_configure_params(stream_interface, name));
        results.emplace(std::make_pair(name, params));
    }
    return results;
}

Expected<ConfigureNetworkParams> Hef::create_configure_params(hailo_stream_interface_t stream_interface, const std::string &network_group_name)
{
    return pimpl->create_configure_params(stream_interface, network_group_name);
}

std::string Hef::hash() const
{
    const auto &hash = pimpl->get_hash_as_memview();
    const bool LOWERCASE = false;
    return StringUtils::to_hex_string(hash.data(), hash.size(), LOWERCASE);
}

std::vector<std::string> Hef::Impl::get_network_groups_names()
{
    std::vector<std::string> results;
    results.reserve(m_groups.size());

    for (const auto &net_group : m_groups) {
        auto &network_group_name = is_multi_layout(get_device_arch()) ?
            net_group->partial_network_groups(0).network_group().network_group_metadata().network_group_name()
            : net_group->network_group_metadata().network_group_name();
        results.push_back(network_group_name);
    }
    return results;
}

Expected<std::vector<hailo_network_group_info_t>> Hef::get_network_groups_infos() const
{
    return pimpl->get_network_groups_infos();
}

Expected<std::vector<std::string>> Hef::Impl::get_stream_infos_description(const std::string &network_group_name, const std::string &network_name)
{
    std::vector<std::string> infos_strings;
    TRY(const auto input_stream_infos,
        get_input_stream_infos(network_group_name, network_name),
        "Failed to parse input stream infos");
    TRY(const auto output_stream_infos,
        get_output_stream_infos(network_group_name, network_name),
        "Failed to parse output stream infos");
    infos_strings.reserve(input_stream_infos.size() + output_stream_infos.size());
    std::string infos_string;

    for (const auto &stream_info : input_stream_infos) {
        auto shape_str = get_shape_str(stream_info);
        infos_string = "Input  " + std::string(stream_info.name) + " " + shape_str + "\n";
        infos_strings.emplace_back(infos_string);
    }

    for (const auto &stream_info : output_stream_infos) {
        auto shape_str = get_shape_str(stream_info);
        infos_string = "Output " + std::string(stream_info.name) + " " + shape_str + "\n";
        infos_strings.emplace_back(infos_string);
    }

    return infos_strings;
}

Expected<std::vector<std::string>> Hef::Impl::get_vstream_infos_description(const std::string &network_group_name, const std::string &network_name)
{
    std::vector<std::string> infos_strings;
    TRY(const auto input_vstream_infos,
        get_input_vstream_infos(network_group_name, network_name),
        "Failed to parse input vstream infos");
    TRY(const auto output_vstream_infos,
        get_output_vstream_infos(network_group_name, network_name),
        "Failed to parse output vstream infos");
    infos_strings.reserve(input_vstream_infos.size() + output_vstream_infos.size());
    std::string infos_string;

    for (const auto &vstream_info : input_vstream_infos) {
        auto shape_str = get_shape_str(vstream_info);
        infos_string = "Input  " + std::string(vstream_info.name) + " " + shape_str + "\n";
        infos_strings.emplace_back(infos_string);
    }

    for (const auto &vstream_info : output_vstream_infos) {
        auto shape_str = get_shape_str(vstream_info);
        infos_string = "Output " + std::string(vstream_info.name) + " " + shape_str + "\n";
        infos_strings.emplace_back(infos_string);
    }

    return infos_strings;
}

Expected<std::vector<std::string>> Hef::Impl::get_post_processes_infos_description(const std::string &network_group_name)
{
    std::vector<std::string> infos_strings;
    std::string infos_string;

    CHECK_AS_EXPECTED(contains(m_network_group_metadata, network_group_name), HAILO_INTERNAL_FAILURE);

    auto post_process = m_network_group_metadata.at(network_group_name).m_ops_metadata;
    for (const auto &post_process_info : post_process) {
        infos_string = post_process_info->get_op_description();
    }
    /* If the string is empty there is no need to continue. */
    if (infos_string.empty()) {
        return infos_strings;
    }

    /* Splitting the info string and assembling the vector from each token. */
    std::string token;
    size_t pos;
    while ((pos = infos_string.find(",")) != std::string::npos) {
        token.assign(infos_string.begin(), infos_string.begin() + pos);
        token += "\n";
        infos_strings.push_back(token);
        /* Assuming each token is separated with ", " */
        infos_string.erase(0, pos + SKIP_SPACE_COMMA_CHARACTERS);
    }
    infos_strings.push_back(infos_string + "\n");

    return infos_strings;
}

Expected<std::string> Hef::get_description(bool stream_infos, bool vstream_infos) const
{
    TRY(const auto compatible_archs, get_compatible_device_archs());
    return pimpl->get_description(stream_infos, vstream_infos, compatible_archs);
}

Expected<std::string> Hef::Impl::get_description(bool stream_infos, bool vstream_infos,
    std::vector<hailo_device_architecture_t> compatible_archs)
{
    std::string hef_infos;
    std::string hef_arch_str = HailoRTCommon::get_device_arch_str(compatible_archs.at(0));
    for (size_t i = 1; i < compatible_archs.size(); i++) {
        hef_arch_str += ", " + HailoRTCommon::get_device_arch_str(compatible_archs[i]);
    }
    hef_infos += "HEF Compatible for: " + hef_arch_str + "\n";

    TRY(const auto network_group_infos, get_network_groups_infos());
    for (const auto &network_group_info : network_group_infos) {
        TRY(const auto core_op_metadata, get_core_op_metadata(network_group_info.name));
        const auto number_of_dynamic_contexts = core_op_metadata->get_dynamic_contexts_count();
        auto contexts_str = network_group_info.is_multi_context ?
            "Multi Context - Number of contexts: " + std::to_string(number_of_dynamic_contexts) :
            "Single Context";
        hef_infos += "Network group name: " + std::string(network_group_info.name) + ", " + contexts_str + "\n";

        TRY(const auto network_infos, get_network_infos(network_group_info.name),
            "Failed to parse networks infos");

        for (const auto &network_info : network_infos) {
            hef_infos += add_tabs(1) + "Network name: " + network_info.name + "\n";
            if (stream_infos) {
                TRY(const auto stream_infos_strings,
                    get_stream_infos_description(network_group_info.name, network_info.name));
                hef_infos += add_tabs(2) + "Stream infos:" + "\n";
                for (auto stream_info_string : stream_infos_strings) {
                    hef_infos += add_tabs(3) + stream_info_string;
                }
            }
            if (vstream_infos) {
                TRY(const auto vstream_infos_strings,
                    get_vstream_infos_description(network_group_info.name, network_info.name));
                hef_infos += add_tabs(2) + "VStream infos:" + "\n";
                for (auto vstream_info_string : vstream_infos_strings) {
                    hef_infos += add_tabs(3) + vstream_info_string;
                }

                TRY(const auto post_processes_infos_strings,
                    get_post_processes_infos_description(network_group_info.name));
                /* Validating that there is a postprocess info. */
                if (post_processes_infos_strings.size() <= 0) {
                    continue;
                }
                hef_infos += add_tabs(3) + "Operation:" + "\n";
                for (auto post_process_info_string : post_processes_infos_strings) {
                    hef_infos += add_tabs(4) + post_process_info_string;
                }
            }
        }
    }

    return hef_infos;
}


Expected<MemoryView> Hef::get_external_resources(const std::string &resource_name) const
{
    return pimpl->get_external_resources(resource_name);
}

std::vector<std::string> Hef::get_external_resource_names() const
{
    return pimpl->get_external_resource_names();
}

Expected<MemoryView> Hef::Impl::get_external_resources(const std::string &resource_name) const
{
    MemoryView resource_memview = {};
    auto hef_reader = get_hef_reader();
    CHECK_SUCCESS(hef_reader->open());
    for (auto &name_to_external_resource_info : m_hef_external_resources) {
        if (resource_name == name_to_external_resource_info.first) {
            auto &external_resource_info = name_to_external_resource_info.second;
            const auto offset = external_resource_info.offset + get_offset_zero_point();
            const auto size = external_resource_info.size;
            const auto checksum = external_resource_info.xxhash;
            TRY(resource_memview, hef_reader->read_from_offset_as_memview(offset, size));

            if (checksum != 0) { // validate checksum only if filled in the HEF
                TRY(auto resource_checksum, Xxhash::calc_xxh3_on_buffer(resource_memview));
                CHECK(checksum == resource_checksum, HAILO_HEF_FILE_CORRUPTED,
                    "Resource '{}' checksum does not match", resource_name);
            }
            CHECK_SUCCESS(hef_reader->close());
            return resource_memview;
        }
    }
    CHECK_SUCCESS(hef_reader->close());
    return make_unexpected(HAILO_NOT_FOUND);
}

std::vector<std::string> Hef::Impl::get_external_resource_names() const
{
    std::vector<std::string> resource_names;
    resource_names.reserve(m_hef_external_resources.size());

    for (const auto &name_to_external_resource_info : m_hef_external_resources) {
        resource_names.push_back(name_to_external_resource_info.first);
    }

    return resource_names;
}

Expected<std::vector<hailo_network_group_info_t>> Hef::Impl::get_network_groups_infos()
{
    std::vector<hailo_network_group_info_t> results;
    results.reserve(m_core_ops_per_group.size());

    for (const auto &group_name_to_core_op : m_core_ops_per_group) {
        const auto &core_op = group_name_to_core_op.second[0];
        hailo_network_group_info_t info = {};
        auto &network_group_name = is_multi_layout(get_device_arch()) ?
            core_op.partial_core_ops[0]->core_op->network_group_metadata.network_group_name()
            : core_op.network_group_metadata.network_group_name();
        CHECK_AS_EXPECTED(HAILO_MAX_NETWORK_GROUP_NAME_SIZE >= (network_group_name.length() + 1), HAILO_INTERNAL_FAILURE,
            "The network group '{}' has a too long name (max is HAILO_MAX_NETWORK_GROUP_NAME_SIZE)", network_group_name);
        strncpy(info.name, network_group_name.c_str(), network_group_name.length() + 1);
        const auto number_contexts = is_multi_layout(get_device_arch()) ?
            core_op.partial_core_ops[0]->core_op->contexts.size() : core_op.contexts.size();
        info.is_multi_context = (1 < number_contexts);
        results.push_back(info);
    }
    return results;
}

Expected<std::map<std::string, hailo_vstream_params_t>> Hef::make_input_vstream_params(
    const std::string &name, bool /*unused*/, hailo_format_type_t format_type, uint32_t timeout_ms,
    uint32_t queue_size)
{
    TRY(const auto network_pair, pimpl->get_network_group_and_network_name(name)); 
    return pimpl->make_input_vstream_params(network_pair.first, network_pair.second, format_type, 
        timeout_ms, queue_size);
}

Expected<std::map<std::string, hailo_vstream_params_t>> Hef::Impl::make_input_vstream_params(
    const std::string &net_group_name, const std::string &network_name,
    hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    std::map<std::string, hailo_vstream_params_t> input_vstreams_params;
    auto status = fill_missing_input_vstream_params_with_default(net_group_name,
        network_name, input_vstreams_params, format_type, timeout_ms, queue_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return input_vstreams_params;
}

Expected<std::map<std::string, hailo_vstream_params_t>> Hef::make_output_vstream_params(
    const std::string &name, bool /*unused*/, hailo_format_type_t format_type, uint32_t timeout_ms,
    uint32_t queue_size)
{
    TRY(const auto network_pair, pimpl->get_network_group_and_network_name(name));
    return pimpl->make_output_vstream_params(network_pair.first, network_pair.second, format_type, 
        timeout_ms, queue_size);
}

Expected<std::map<std::string, hailo_vstream_params_t>> Hef::Impl::make_output_vstream_params(
    const std::string &net_group_name, const std::string &network_name,
    hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    std::map<std::string, hailo_vstream_params_t> output_vstreams_params;
    auto status = fill_missing_output_vstream_params_with_default(net_group_name,
        network_name, output_vstreams_params, format_type, timeout_ms, queue_size);
    CHECK_SUCCESS_AS_EXPECTED(status);

    return output_vstreams_params;
}

hailo_status Hef::Impl::fill_missing_input_vstream_params_with_default(const std::string &net_group_name,
    const std::string &network_name, std::map<std::string, hailo_vstream_params_t> &input_vstreams_params,
    hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    CHECK(contains(m_network_group_metadata, net_group_name), HAILO_NOT_FOUND);
    TRY(const auto input_vstream_infos,
        m_network_group_metadata.at(net_group_name).get_input_vstream_infos(network_name));

    return fill_missing_vstream_params_with_default(input_vstreams_params, input_vstream_infos,
        format_type, timeout_ms, queue_size);
}

hailo_status Hef::Impl::fill_missing_output_vstream_params_with_default(const std::string &net_group_name,
    const std::string &network_name, std::map<std::string, hailo_vstream_params_t> &output_vstream_params,
    hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size)
{
    CHECK(contains(m_network_group_metadata, net_group_name), HAILO_NOT_FOUND);
    TRY(const auto output_vstream_infos,
        m_network_group_metadata.at(net_group_name).get_output_vstream_infos(network_name));

    return fill_missing_vstream_params_with_default(output_vstream_params, output_vstream_infos,
        format_type, timeout_ms, queue_size);
}

hailo_status Hef::Impl::fill_missing_vstream_params_with_default(std::map<std::string, hailo_vstream_params_t> &vstream_params,
    const std::vector<hailo_vstream_info_t> &vstream_infos, hailo_format_type_t format_type, uint32_t timeout_ms,
    uint32_t queue_size)
{
    hailo_format_flags_t flags = HAILO_FORMAT_FLAGS_NONE;

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
    TRY(params.stream_params_by_name,
        create_stream_parameters_by_name(network_group_name, stream_interface));
    TRY(params.network_params_by_name,
        create_network_parameters_by_name(network_group_name));

    return params;
}

Expected<std::map<std::string, hailo_stream_parameters_t>> Hef::create_stream_parameters_by_name(
    const std::string &net_group_name, hailo_stream_interface_t stream_interface)
{
    TRY(const auto network_group_name_pair,
        pimpl->get_network_group_and_network_name(net_group_name));
    const auto &net_group_name_str = network_group_name_pair.first;

    return pimpl->create_stream_parameters_by_name(net_group_name_str, stream_interface);
}

Expected<std::map<std::string, hailo_stream_parameters_t>> Hef::Impl::create_stream_parameters_by_name(
    const std::string &net_group_name, hailo_stream_interface_t stream_interface)
{
    TRY(const auto core_op_metadata, get_core_op_metadata(net_group_name));

    std::map<std::string, hailo_stream_parameters_t> results;
    TRY(const auto input_stream_infos,
        core_op_metadata->get_input_stream_infos());
    for (const auto &input_layer : input_stream_infos) {
        TRY(auto params,
            HailoRTDefaults::get_stream_parameters(stream_interface, HAILO_H2D_STREAM));
        results.emplace(std::make_pair(input_layer.name, params));
    }
    TRY(const auto output_stream_infos,
        core_op_metadata->get_output_stream_infos());
    for (const auto &output_layer : output_stream_infos) {
        TRY(auto params,
            HailoRTDefaults::get_stream_parameters(stream_interface, HAILO_D2H_STREAM));
        results.emplace(std::make_pair(output_layer.name, params));
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
    TRY(const auto core_op, get_core_op_by_net_group_name(net_group_name));
    TRY(const auto core_op_metadata, get_core_op_metadata(net_group_name));

    std::map<std::string, hailo_network_parameters_t> results;

    if (core_op_metadata->supported_features().multi_network_support) {
        CHECK_AS_EXPECTED((core_op->networks_names.size() != 0), HAILO_INTERNAL_FAILURE, 
        "Hef support multiple networks, but no networks found in the proto");
        for (const auto &partial_network_name : core_op->networks_names) {
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

void Hef::set_memory_footprint_optimization(bool should_optimize)
{
    return pimpl->set_memory_footprint_optimization(should_optimize);
}

void Hef::Impl::set_memory_footprint_optimization(bool should_optimize)
{
    m_zero_copy_config_over_descs = should_optimize;
}

Expected<std::string> Hef::hash(const std::string &hef_path)
{
    TRY(auto hef_reader, SeekableBytesReader::create_reader(hef_path));
    CHECK_SUCCESS(hef_reader->open());

    hef__header_t hef_header = {};
    CHECK_SUCCESS(hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header), HEF_COMMON_SIZE));

    auto hef_version = BYTE_ORDER__htonl(hef_header.version);
    // Starting DFC version 5.0.0, all HEFs are version 3
    CHECK(hef_version == HEADER_VERSION_3, HAILO_HEF_NOT_SUPPORTED,
        "Only HEF version 3 is supported for hashing. Current version: {}", hef_version);

    CHECK_SUCCESS(hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header.distinct), sizeof(hef__header_distinct_t::v3)));
    auto xxh3_64bits = BYTE_ORDER__htonll(hef_header.distinct.v3.xxh3_64bits);
    const bool LOWERCASE = false;
    CHECK_SUCCESS(hef_reader->close());

    return StringUtils::to_hex_string(reinterpret_cast<uint8_t*>(&xxh3_64bits), sizeof(xxh3_64bits), LOWERCASE);
}

Expected<std::map<std::string, BufferPtr>> Hef::extract_hef_external_resources(const std::string &file_path)
{
    return Hef::Impl::extract_hef_external_resources(file_path);
}

Expected<std::map<std::string, BufferPtr>> Hef::Impl::extract_hef_external_resources(const std::string &file_path)
{
    TRY(auto hef_reader, SeekableBytesReader::create_reader(file_path));
    CHECK_SUCCESS(hef_reader->open());

    // Read and parse the HEF header and proto to extract external resources
    hef__header_t hef_header = {};
    CHECK_SUCCESS(hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header), HEF_COMMON_SIZE));

    auto hef_version = BYTE_ORDER__htonl(hef_header.version);
    CHECK(hef_version == HEADER_VERSION_3, HAILO_HEF_NOT_SUPPORTED,
        "Only HEF version 3 is supported. Current version: {}", hef_version);

    CHECK_SUCCESS(hef_reader->read(reinterpret_cast<uint8_t*>(&hef_header.distinct), sizeof(hef__header_distinct_t::v3)));
    auto proto_size = BYTE_ORDER__htonl(hef_header.hef_proto_size);

    // Read and parse the proto message to extract external resource info
    TRY(auto proto_buffer, Buffer::create_shared(proto_size));
    CHECK_SUCCESS(hef_reader->read(proto_buffer->data(), proto_size));

    ProtoHEFHef hef_message;
    auto parse_result = hef_message.ParseFromArray(proto_buffer->data(), static_cast<int>(proto_size));
    CHECK(parse_result, HAILO_HEF_FILE_CORRUPTED, "Failed to parse HEF proto message");

    // Extract external resource info and calculate file size excluding external resources
    std::map<std::string, BufferPtr> external_resources;

    size_t offset_zero_point = HEF_HEADER_SIZE_V3 + proto_size + BYTE_ORDER__htonl(hef_header.distinct.v3.hef_padding_size);

    for (const auto &external_resource : hef_message.external_resources()) {
        const auto& resource_name = external_resource.name();
        const auto resource_size = external_resource.size();
        const auto resource_offset = external_resource.offset();

        TRY(auto resource_buffer, Buffer::create_shared(resource_size));

        // Seek to the resource location in the file
        size_t absolute_offset = offset_zero_point + resource_offset;
        CHECK_SUCCESS(hef_reader->seek(absolute_offset));
        CHECK_SUCCESS(hef_reader->read(resource_buffer->data(), resource_size));

        const uint64_t resource_checksum = external_resource.xxhash();
        // Check if xxhash is set (not default value 0)
        if (resource_checksum != 0) {
            TRY(auto resource_checksum_result, Xxhash::calc_xxh3_on_buffer(MemoryView::create_const(resource_buffer->data(), resource_size)));
            CHECK(resource_checksum == resource_checksum_result, HAILO_HEF_FILE_CORRUPTED,
                "Resource '{}' checksum does not match", resource_name);
        }

        external_resources[resource_name] = resource_buffer;
    }

    CHECK_SUCCESS(hef_reader->close());

    return external_resources;
}


} /* namespace hailort */
