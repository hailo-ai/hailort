/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file server_main.cpp
 * @brief Hailo Server main function
 **/

#include "hailort_server.hpp"
#include "hailo/hailort.h"
#include "utils/hailort_logger.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#ifdef __unix__
#include <spdlog/sinks/syslog_sink.h>
#endif

#ifdef HAILO_GENAI_SERVER
#include "llm/llm_server.hpp"
#include "vlm/vlm_server.hpp"
#include "speech2text/speech2text_server.hpp"
#include "common/genai/connection_ports.hpp"
#endif // HAILO_GENAI_SERVER

#include <iostream>

using namespace hailort;

#define LOGGER_PATTERN ("[%Y-%m-%d %X.%e] [%P] [%t] [%n] [%^%l%$] [%s:%#] [%!] %v")

void init_logger(const std::string &name)
{
    auto console_sink = hailort::make_shared_nothrow<spdlog::sinks::stderr_color_sink_mt>();
    if (nullptr == console_sink) {
        std::cerr << "Failed to create console sink for hailort server log!" << std::endl;
        return;
    }
    console_sink->set_level(spdlog::level::info);
    console_sink->set_pattern(LOGGER_PATTERN);

    std::vector<std::shared_ptr<spdlog::sinks::sink>> sink_vector = { console_sink };
#ifdef __unix__
    auto syslog_sink = hailort::make_shared_nothrow<spdlog::sinks::syslog_sink_mt>(name, 0, LOG_USER, true);
    if (nullptr == syslog_sink) {
        std::cerr << "Failed to create syslog sink for hailort server log!" << std::endl;
        return;
    }
    auto syslog_level_env_var = get_env_variable(HAILORT_SYSLOG_LOGGER_LEVEL_ENV_VAR);
    if (syslog_level_env_var) {
        auto syslog_level = HailoRTLogger::get_console_logger_level_from_string(syslog_level_env_var.value());
        if (syslog_level) {
            syslog_sink->set_level(syslog_level.value());
        }
    } else {
        syslog_sink->set_level(spdlog::level::info);
    }
    syslog_sink->set_pattern(HAILORT_SYSLOG_LOGGER_PATTERN);
    sink_vector.push_back(syslog_sink);
#endif
    auto logger = hailort::make_shared_nothrow<spdlog::logger>(name, sink_vector.begin(), sink_vector.end());
    if (nullptr == logger) {
        std::cerr << "Failed to create logger for hailort server!" << std::endl;
        return;
    }

    // Setting loggr level to min active level, as traces will only show if the sink level is set to their level
    logger->set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
    spdlog::set_default_logger(logger);
}

int main(int argc, char* argv[])
{
    init_logger("HailoRT-Server");

    std::string ip_addr = "";
    if (argc > 1) {
        ip_addr = argv[1];
        LOGGER__INFO("Using IP address: {}", ip_addr);
    } else {
        LOGGER__INFO("Using PCIe");
    }

    // Create classic HailoRT-server
    TRY(auto server, HailoRTServer::create_unique(ip_addr));
    TRY(auto dispatcher, server->create_dispatcher());
    server->set_dispatcher(dispatcher);
    auto hailort_server_th = std::thread([&server]() {
        auto status = server->serve();
        if (status != HAILO_SUCCESS) {
            LOGGER__ERROR("Error in serve, status = {}", status);
        }
    });

#ifdef HAILO_GENAI_SERVER
    // Create GenAI server
    auto llm_thread = std::thread([ip_addr, &server]() {
        TRY(auto server_connection, SessionListener::create_shared(genai::DEFAULT_LLM_CONNECTION_PORT, ip_addr));
        while (true) {
            TRY(auto session, server_connection->accept());
            auto th = std::thread([session, &server]() {
                auto llm_server = genai::LLMServerManager::create(session, server->vdevice_manager());
                if (!llm_server) {
                    LOGGER__ERROR("Failed to create LLMServer, status = {}", llm_server.status());
                    return;
                }
                llm_server.value()->flow();
            });
            th.detach();
        }
    });
    llm_thread.detach();

    auto vlm_thread = std::thread([ip_addr, &server]() {
        TRY(auto server_connection, SessionListener::create_shared(genai::DEFAULT_VLM_CONNECTION_PORT, ip_addr));
        while (true) {
            TRY(auto session, server_connection->accept());
            auto th = std::thread([session, &server]() {
                auto vlm_server = genai::VLMServerManager::create(session, server->vdevice_manager());
                if (!vlm_server) {
                    LOGGER__ERROR("Failed to create VLMServer, status = {}", vlm_server.status());
                    return;
                }
                vlm_server.value()->flow();
            });
            th.detach();
        }
    });
    vlm_thread.detach();

    auto speech2text_thread = std::thread([ip_addr, &server]() {
        TRY(auto server_connection, SessionListener::create_shared(genai::DEFAULT_SPEECH2TEXT_CONNECTION_PORT, ip_addr));
        while (true) {
            TRY(auto session, server_connection->accept());
            auto th = std::thread([session, &server]() {
                auto speech2text_server = genai::Speech2TextServerManager::create(session, server->vdevice_manager());
                if (!speech2text_server) {
                    LOGGER__ERROR("Failed to create Speech2TextServer, status = {}", speech2text_server.status());
                    return;
                }
                speech2text_server.value()->flow();
            });
            th.detach();
        }
    });
    speech2text_thread.detach();

#endif // HAILO_GENAI_SERVER

    hailort_server_th.join();
    return 0;
}
