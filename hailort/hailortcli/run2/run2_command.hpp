/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file run2_command.hpp
 * @brief Run inference on hailo device
 **/

#ifndef _HAILO_HAILORTCLI_RUN2_RUN2_COMMAND_HPP_
#define _HAILO_HAILORTCLI_RUN2_RUN2_COMMAND_HPP_

#include "../command.hpp"
#include "network_runner.hpp"

#include <type_traits>


class Run2Command : public Command {
public:
    explicit Run2Command(CLI::App &parent_app);

    hailo_status execute() override;
private:
};

class IoApp : public CLI::App
{
public:
    enum class Type {
        STREAM,
        VSTREAM
    };

    IoApp(const std::string &description, const std::string &name, Type type);
    Type get_type() const;
    const VStreamParams& get_vstream_params() const;
    const StreamParams& get_stream_params() const;

protected:
    Type m_type;
    VStreamParams m_vstream_params;
    StreamParams m_stream_params;
};

class NetworkApp : public CLI::App
{
public:
    NetworkApp(const std::string &description, const std::string &name);
    const NetworkParams& get_params();

private:
    template <typename T>
    CLI::App *add_io_app_subcom(const std::string &description, const std::string &name,
        CLI::Option *hef_path_option, CLI::Option *net_group_name_option)
    {
        static_assert(std::is_base_of<IoApp, T>::value, "T is not a subclass of IoApp");

        auto io_app = std::make_shared<T>(description, name, hef_path_option, net_group_name_option);
        io_app->immediate_callback();
        io_app->callback([this, description, name, io_app, hef_path_option, net_group_name_option]() {
            if (io_app->get_type() == IoApp::Type::VSTREAM) {
                auto vstream_params = io_app->get_vstream_params();
                m_params.vstream_params.push_back(vstream_params);
            } else {
                auto stream_params = io_app->get_stream_params();
                m_params.stream_params.push_back(stream_params);
            }

            // Throw an error if anything is left over and should not be.
            _process_extras();

            // NOTE: calling "net_app->clear(); m_params = NetworkParams();" is not sufficient because default values
            //         need to be re-set. we can override clear and reset them but there might be other issues as well
            //         and this one feels less hacky ATM
            remove_subcommand(io_app.get());
            // Remove from parsed_subcommands_ as well (probably a bug in CLI11)
            parsed_subcommands_.erase(std::remove_if(
                parsed_subcommands_.begin(), parsed_subcommands_.end(),
                [io_app](auto x){return x == io_app.get();}),
                parsed_subcommands_.end());
            add_io_app_subcom<T>(description, name, hef_path_option, net_group_name_option);
        });

        // Must set fallthrough to support nested repeated subcommands.
        io_app->fallthrough();
        return add_subcommand(io_app);
    }

    NetworkParams m_params;
};

#endif /* _HAILO_HAILORTCLI_RUN2_RUN2_COMMAND_HPP_ */
