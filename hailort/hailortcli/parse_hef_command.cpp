/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file parse_hef_command.cpp
 * @brief Parses HEF and print info to stdout
 **/

#include "parse_hef_command.hpp"
#include "common/filesystem.hpp"
#include "hailo/hailort_common.hpp"

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
    case HAILO_FORMAT_ORDER_HAILO_NMS:
        return HailoRTCommon::get_format_type_str(stream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(stream_info.format.order) +
            "(number of classes: " + std::to_string(stream_info.nms_info.number_of_classes) +
            ", max_bboxes_per_class: "+ std::to_string(stream_info.nms_info.max_bboxes_per_class) + ")";
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
    case HAILO_FORMAT_ORDER_HAILO_NMS:
        return HailoRTCommon::get_format_type_str(vstream_info.format.type) + ", " + HailoRTCommon::get_format_order_str(vstream_info.format.order) +
            "(number of classes: " + std::to_string(vstream_info.nms_shape.number_of_classes) +
            ", max_bboxes_per_class: " + std::to_string(vstream_info.nms_shape.max_bboxes_per_class) + ")";
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

ParseHefCommand::ParseHefCommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("parse-hef", "Parse HEF to get information about its components"))
{
    m_app->add_option("hef", m_hef_path, "An existing HEF file/directory path")
        ->check(CLI::ExistingFile | CLI::ExistingDirectory)
        ->required();
    m_app->add_flag("--parse-streams", m_parse_streams, "Parse stream infos")->default_val(false);
    m_app->add_flag("--parse-vstreams", m_parse_vstreams, "Parse vstream infos")->default_val(true);
}


hailo_status ParseHefCommand::execute()
{
    auto is_dir = Filesystem::is_directory(m_hef_path.c_str());
    CHECK_EXPECTED_AS_STATUS(is_dir, "Failed checking if path is directory");

    if (is_dir.value()){
        return ParseHefCommand::parse_hefs_infos_dir(m_hef_path, m_parse_streams, m_parse_vstreams);
    } else {
        return ParseHefCommand::parse_hefs_info(m_hef_path, m_parse_streams, m_parse_vstreams);
    }
}

hailo_status ParseHefCommand::parse_hefs_info(const std::string &hef_path, bool stream_infos, bool vstream_infos)
{
    auto hef_exp = Hef::create(hef_path);
    CHECK_EXPECTED_AS_STATUS(hef_exp, "Failed to parse HEF");
    auto hef = hef_exp.release();

    auto network_group_infos = hef.get_network_groups_infos();
    CHECK_EXPECTED_AS_STATUS(network_group_infos);
    for (auto &network_group_info : network_group_infos.release()) {
        auto contexts_str = (network_group_info.is_multi_context ? "Multi Context" : "Single Context");
        std::cout << "Network group name: " << network_group_info.name << " (" << contexts_str << ")" << std::endl;
        auto network_infos = hef.get_network_infos(network_group_info.name);
        CHECK_EXPECTED_AS_STATUS(network_infos, "Failed to parse networks infos");
        for (auto &network_info : network_infos.value()) {
            std::cout << add_tabs(1) << "Network name: " << network_info.name << std::endl;
            if (stream_infos) {
                std::cout << add_tabs(2) << "Stream infos:" << std::endl;
                auto input_stream_infos = hef.get_input_stream_infos(network_info.name);
                CHECK_EXPECTED_AS_STATUS(input_stream_infos, "Failed to parse input stream infos");
                for (auto &stream_info : input_stream_infos.value()) {
                    auto shape_str = get_shape_str(stream_info);
                    std::cout << add_tabs(3) << "Input  " << stream_info.name << " " << shape_str << std::endl;
                }
                auto output_stream_infos = hef.get_output_stream_infos(network_info.name);
                CHECK_EXPECTED_AS_STATUS(output_stream_infos, "Failed to parse output stream infos");
                for (auto &stream_info : output_stream_infos.value()) {
                    auto shape_str = get_shape_str(stream_info);
                    std::cout << add_tabs(3) << "Output " << stream_info.name << " " << shape_str << std::endl;
                }
            }
            if (vstream_infos) {
                std::cout << add_tabs(2) << "VStream infos:" << std::endl;
                auto input_vstream_infos = hef.get_input_vstream_infos(network_info.name);
                CHECK_EXPECTED_AS_STATUS(input_vstream_infos, "Failed to parse input vstream infos");
                for (auto &vstream_info : input_vstream_infos.value()) {
                    auto shape_str = get_shape_str(vstream_info);
                    std::cout << add_tabs(3) << "Input  " << vstream_info.name << " " << shape_str << std::endl;
                }
                auto output_vstream_infos = hef.get_output_vstream_infos(network_info.name);
                CHECK_EXPECTED_AS_STATUS(output_vstream_infos, "Failed to parse output vstream infos");
                for (auto &vstream_info : output_vstream_infos.value()) {
                    auto shape_str = get_shape_str(vstream_info);
                    std::cout << add_tabs(3) << "Output " << vstream_info.name << " " << shape_str << std::endl;
                }
            }
        }
    }
    std::cout << std::endl;
    return HAILO_SUCCESS;
}

hailo_status ParseHefCommand::parse_hefs_infos_dir(const std::string &hef_path, bool stream_infos, bool vstream_infos)
{
    bool contains_hef = false;
    std::string hef_dir = hef_path;
    const auto files = Filesystem::get_files_in_dir_flat(hef_dir);
    CHECK_EXPECTED_AS_STATUS(files);

    for (const auto &full_path : files.value()) {
        if (Filesystem::has_suffix(full_path, ".hef")) {
            contains_hef = true;
            std::cout << std::string(80, '*') << std::endl << "Parsing " << full_path << ":"<< std::endl;
            auto status = ParseHefCommand::parse_hefs_info(full_path, stream_infos, vstream_infos);
            CHECK_SUCCESS(status, "Failed to parse HEF {}", full_path);
        }
    }

    CHECK(contains_hef, HAILO_INVALID_ARGUMENT, "No HEF files were found in the directory: {}", hef_dir);

    return HAILO_SUCCESS;
}
