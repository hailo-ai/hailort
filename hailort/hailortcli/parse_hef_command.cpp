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

    auto hef_info = hef.get_hef_description(stream_infos, vstream_infos);
    CHECK_EXPECTED_AS_STATUS(hef_info, "Failed to parse HEF");
    std::cout << hef_info.release();
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
