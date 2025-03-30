/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file parse_hef_command.hpp
 * @brief Parse HEF and print metadata to stdout
 **/

#ifndef _HAILO_PARSE_COMMAND_COMMAND_HPP_
#define _HAILO_PARSE_COMMAND_COMMAND_HPP_

#include "hailortcli.hpp"
#include "command.hpp"

#include "hailo/hailort.h"
#include "hailo/device.hpp"
#include "hailo/buffer.hpp"
#include "CLI/CLI.hpp"


class ParseHefCommand : public Command {
public:
    explicit ParseHefCommand(CLI::App &parent_app);

    virtual hailo_status execute() override;

private:
    static hailo_status parse_hefs_infos_dir(const std::string &hef_path, bool stream_infos, bool vstream_infos);
    static hailo_status parse_hefs_info(const std::string &hef_path, bool stream_infos, bool vstream_infos);

    std::string m_hef_path;
    bool m_parse_streams;
    bool m_parse_vstreams;
};

#endif /* _HAILO_PARSE_COMMAND_COMMAND_HPP_ */
