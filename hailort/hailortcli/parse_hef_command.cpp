/**
 * Copyright (c) 2019-2026 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file parse_hef_command.cpp
 * @brief Parses HEF and print info to stdout
 **/

#include "parse_hef_command.hpp"
#include "common/file_utils.hpp"
#include "common/filesystem.hpp"
#include "common/genai/constants.hpp"
#include "hailo/hailort_common.hpp"
#include "hailo/hef.hpp"

#include <nlohmann/json.hpp>

#include <sstream>

static constexpr const char *GENAI_INFO_INDENT = "    ";

static constexpr int JSON_PRETTY_PRINT_INDENT = 4;
static constexpr char JSON_PRETTY_PRINT_INDENT_CHAR = ' ';
static constexpr bool JSON_ENSURE_ASCII = false;

static std::string genai_indent(uint8_t count)
{
    std::string res;
    for (uint8_t i = 0; i < count; i++) {
        res += GENAI_INFO_INDENT;
    }
    return res;
}

static hailo_status print_genai_info(const Hef &hef)
{
    auto config_view_expected = hef.get_external_resources(genai::HAILO_CONFIG_JSON);
    if (HAILO_NOT_FOUND == config_view_expected.status()) {
        std::cout << "Not a GenAI HEF\n";
        return HAILO_SUCCESS;
    }
    CHECK_EXPECTED(config_view_expected, "Failed to read '{}' from HEF", genai::HAILO_CONFIG_JSON);
    const auto config_view = config_view_expected.release();

    const auto *json_begin = reinterpret_cast<const char *>(config_view.data());
    const auto *json_end = json_begin + config_view.size();
    auto parsed_json = nlohmann::json::parse(json_begin, json_end, nullptr, /*allow_exceptions=*/false);
    CHECK(!parsed_json.is_discarded(), HAILO_INVALID_HEF, "Failed to parse '{}' as JSON",
        genai::HAILO_CONFIG_JSON);

    std::ostringstream oss;
    oss << "GenAI info:\n";
    oss << genai_indent(1) << "External resources:\n";
    for (const auto &name : hef.get_external_resource_names()) {
        oss << genai_indent(2) << name << "\n";
    }

    oss << genai_indent(1) << genai::HAILO_CONFIG_JSON << ":\n";
    const auto pretty = parsed_json.dump(JSON_PRETTY_PRINT_INDENT, JSON_PRETTY_PRINT_INDENT_CHAR,
        JSON_ENSURE_ASCII, nlohmann::json::error_handler_t::replace);
    oss << pretty << "\n";
    std::cout << oss.str();

    return HAILO_SUCCESS;
}

ParseHefCommand::ParseHefCommand(CLI::App &parent_app) :
    Command(parent_app.add_subcommand("parse-hef", "Parse HEF to get information about its components"))
{
    m_app->add_option("hef", m_hef_path, "An existing HEF file/directory path")
        ->check(CLI::ExistingFile | CLI::ExistingDirectory)
        ->required();
    m_app->add_flag("--parse-streams", m_parse_streams, "Parse stream infos")->default_val(false);
    m_app->add_flag("--parse-vstreams", m_parse_vstreams, "Parse vstream infos")->default_val(true);
    m_app->add_flag("--genai-info", m_parse_genai_info, "Parse and print GenAI info from the HEF")
        ->default_val(false)
        ->group(""); // --genai-info will be hidden in the --help print.
}


hailo_status ParseHefCommand::execute()
{
    TRY(const auto is_dir, Filesystem::is_directory(m_hef_path.c_str()), "Failed checking if path is directory");
    if (is_dir) {
        return ParseHefCommand::parse_hefs_infos_dir(m_hef_path, m_parse_streams, m_parse_vstreams,
            m_parse_genai_info);
    } else {
        return ParseHefCommand::parse_hefs_info(m_hef_path, m_parse_streams, m_parse_vstreams, m_parse_genai_info);
    }
}

hailo_status ParseHefCommand::parse_hefs_info(const std::string &hef_path, bool stream_infos, bool vstream_infos,
    bool genai_info)
{
    // External resources require a memory-backed HEF, so load the file when --genai-info is set.
    BufferPtr hef_buffer;
    if (genai_info) {
        TRY(const auto file_size, get_istream_size(hef_path));
        TRY(hef_buffer, Buffer::create_shared(file_size));
        TRY(const auto bytes_read, read_binary_file(hef_path, hef_buffer->as_view()));
        CHECK(bytes_read == file_size, HAILO_FILE_OPERATION_FAILURE,
            "Short read of HEF '{}': expected {} bytes, got {}", hef_path, file_size, bytes_read);
    }

    TRY(const auto hef, genai_info ? Hef::create(hef_buffer) : Hef::create(hef_path));
    TRY(const auto hef_info, hef.get_description(stream_infos, vstream_infos));
    std::cout << hef_info;
    if (genai_info) {
        CHECK_SUCCESS(print_genai_info(hef));
    }
    return HAILO_SUCCESS;
}

hailo_status ParseHefCommand::parse_hefs_infos_dir(const std::string &hef_path, bool stream_infos, bool vstream_infos,
    bool genai_info)
{
    bool contains_hef = false;
    std::string hef_dir = hef_path;
    TRY(const auto files, Filesystem::get_files_in_dir_flat(hef_dir));

    for (const auto &full_path : files) {
        if (Filesystem::has_suffix(full_path, ".hef")) {
            contains_hef = true;
            std::cout << std::string(80, '*') << std::endl << "Parsing " << full_path << ":"<< std::endl;
            auto status = ParseHefCommand::parse_hefs_info(full_path, stream_infos, vstream_infos, genai_info);
            CHECK_SUCCESS(status, "Failed to parse HEF {}", full_path);
        }
    }

    CHECK(contains_hef, HAILO_INVALID_ARGUMENT, "No HEF files were found in the directory: {}", hef_dir);

    return HAILO_SUCCESS;
}
